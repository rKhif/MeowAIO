#include "zilean.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace zilean
{
    static menu_category* s_menu = nullptr;

    // --- Q→W→Q double-bomb chain state (combo + harass) ---
    static bool  combo_did_first_q   = false;
    static bool  combo_awaiting_w_q  = false;
    static bool  harass_did_first_q  = false;
    static bool  harass_awaiting_w_q = false;

    static constexpr float MIN_SPELL_DELAY  = 0.05f;
    static constexpr float ANTI_MELEE_RANGE = 300.f;
    static constexpr float AUTO_R_PRED_WINDOW = 1.5f;
    static constexpr float KS_OVERKILL_PCT  = 1.15f;
    static float           last_spell_cast_time = 0.f;

    // ============================================================================
    // SPELL VALIDATION
    // ============================================================================
    bool is_spell_learned(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto s = p->get_spell(slot);
        return s && s->get_level() > 0;
    }

    bool is_spell_ready(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    bool has_enough_mana(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    bool is_casting()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto ac = p->get_active_spell_cast();
        if (!ac) return false;
        auto sc = ac->get_spell_cast();
        if (!sc) return false;
        // Ignore basic-attack windups — they register as active casts but don't
        // block spells. Treating them as "casting" gates every spell until the
        // AA finishes, making the champ feel locked to AA timing.
        if (sc->is_basic_attack() || sc->is_special_attack()) return false;
        return ac->get_cast_end_time() > g_sdk->clock_facade->get_game_time();
    }

    float mana_pct()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const float mx = p->get_max_par();
        if (mx <= 0.f) return 0.f;
        return (p->get_par() / mx) * 100.f;
    }

    bool is_valid_target(game_object* u, float range)
    {
        if (!u || !u->is_valid() || u->is_dead() || !u->is_targetable()) return false;
        if (!u->is_visible()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (u->get_team_id() == p->get_team_id()) return false;
        if (range != FLT_MAX) {
            const float d = p->get_position().distance(u->get_position());
            if (d > range) return false;
        }
        return true;
    }

    static bool is_valid_enemy_minion(game_object* m)
    {
        if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (m->get_team_id() == p->get_team_id()) return false;
        return true;
    }

    // Walks TS's priority-sorted heroes and returns the first one within `range`.
    // Bypasses get_hero_target's internal AA-range cap so longer-range spells
    // can pick targets at their actual range.
    static game_object* pick_hero_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target()) {
            if (is_valid_target(forced, range)) return forced;
        }
        for (auto* h : sdk::target_selector->get_sorted_heroes()) {
            if (is_valid_target(h, range)) return h;
        }
        return nullptr;
    }

    // ============================================================================
    // STATE QUERIES
    // ============================================================================
    static bool enemy_has_bomb(game_object* enemy)
    {
        if (!enemy) return false;
        auto* b = enemy->get_buff_by_hash(spell_data::buff_hash_q_bomb);
        return b && b->is_active();
    }

    static bool ally_has_chronoshift(game_object* ally)
    {
        if (!ally) return false;
        auto* b = ally->get_buff_by_hash(spell_data::buff_hash_r_chronoshift);
        return b && b->is_active();
    }

    static bool has_revival_protection(game_object* target)
    {
        if (!target) return false;
        if (ally_has_chronoshift(target)) return true;
        return venplus::utility::has_anti_burst_buff(target);
    }

    static float get_q_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;
        return sdk::damage->get_spell_damage(p, target, SLOT_Q);
    }

    // ============================================================================
    // PREDICTION (crash-safe — circular bomb)
    // ============================================================================
    static pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::circular;
        spell.targetting_type    = pred_sdk::targetting_type::center;
        spell.spell_slot         = SLOT_Q;
        spell.range              = spell_data::q_range;
        spell.radius             = spell_data::q_radius;
        spell.delay              = spell_data::q_delay;
        spell.projectile_speed   = FLT_MAX;          // arc, treat as instant placement
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool cast_q_at(const math::vector3& pos)
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        math::vector3 cast_pos = pos;
        const auto pp = p->get_position();
        if (pp.distance(pos) > spell_data::q_range)
            cast_pos = pp.extended(pos, spell_data::q_range);

        bool ok = sdk::common->cast_spell(SLOT_Q, cast_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_q(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto pred = predict_q(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        return cast_q_at(pred.cast_position);
    }

    static bool try_w_self()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_e_target(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (p->get_position().distance(target->get_position()) > spell_data::e_range) return false;
        bool ok = sdk::common->cast_spell(SLOT_E, target);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r_target(game_object* ally)
    {
        if (!ally || !sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        bool ok = sdk::common->cast_spell(SLOT_R, ally);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // ============================================================================
    // AUTO R — Chronoshift on low-HP ally about to die
    // ============================================================================
    static void run_auto_r()
    {
        if (!menu::auto_r_enabled) return;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const auto pp = p->get_position();
        const float threshold = (float)menu::auto_r_hp_pct / 100.f;

        game_object* best = nullptr;
        float best_pct = 2.f;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || !h->is_hero() || h->is_dead()) continue;
            if (h->get_team_id() != p->get_team_id()) continue;
            if (pp.distance(h->get_position()) > spell_data::r_range + h->get_bounding_radius()) continue;
            if (has_revival_protection(h)) continue;

            const float max_hp = h->get_max_hp();
            if (max_hp <= 0.f) continue;

            const float current = h->get_hp() + h->get_all_shield();
            float pred_hp = current;
            if (sdk::health_prediction)
                pred_hp = sdk::health_prediction->get_predicted_health(h,
                    g_sdk->clock_facade->get_game_time() + AUTO_R_PRED_WINDOW);

            const float pct = std::max(0.f, pred_hp / max_hp);

            // Require meaningful incoming damage (avoid R on regen-tick fluctuations)
            if (sdk::health_prediction) {
                const float incoming_pct = (current - pred_hp) / max_hp * 100.f;
                if (incoming_pct < (float)menu::auto_r_min_dmg_pct) continue;
            }

            if (pct <= threshold && pct < best_pct) {
                best_pct = pct;
                best = h;
            }
        }
        if (best) try_r_target(best);
    }

    // ============================================================================
    // AUTO E — ally being attacked by melee enemy
    // ============================================================================
    static bool run_auto_e_anti_melee()
    {
        if (!menu::e_auto_ally_melee) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pp = p->get_position();

        for (auto* ally : g_sdk->object_manager->get_heroes()) {
            if (!ally || !ally->is_valid() || ally->is_dead() || ally == p) continue;
            if (ally->get_team_id() != p->get_team_id()) continue;
            if (pp.distance(ally->get_position()) > spell_data::e_range) continue;

            const auto ally_pos = ally->get_position();
            const float ally_br = ally->get_bounding_radius();

            for (auto* enemy : g_sdk->object_manager->get_heroes()) {
                if (!enemy || !enemy->is_valid() || enemy->is_dead()) continue;
                if (enemy->get_team_id() == p->get_team_id()) continue;
                if (!enemy->is_melee()) continue;

                const float d = ally_pos.distance(enemy->get_position());
                if (d > ANTI_MELEE_RANGE + enemy->get_bounding_radius() + ally_br) continue;

                if (try_e_target(ally)) return true;
            }
        }
        return false;
    }

    // ============================================================================
    // KILLSTEAL Q
    // ============================================================================
    static void run_killsteal()
    {
        if (!menu::ks_use_q) return;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h, spell_data::q_range + 200.f)) continue;
            const float ehp = h->get_hp() + h->get_all_shield();
            const float dmg = get_q_damage(h) * KS_OVERKILL_PCT;
            // Account for already-placed bomb (would detonate Q hit twice essentially)
            const float total = enemy_has_bomb(h) ? dmg * 2.f : dmg;
            if (ehp > 0.f && ehp <= total) {
                if (try_q(h, menu::combo_q_hitchance)) return;
            }
        }
    }

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo()
    {
        if (!sdk::target_selector) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::q_range);
        if (!target) return;

        // E enemy in range (slow) — only when no chain hesitation
        if (menu::combo_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)
            && !combo_awaiting_w_q) {
            if (p->get_position().distance(target->get_position()) <= spell_data::e_range) {
                try_e_target(target);
            }
        }

        // Q→W→Q double-bomb chain
        const bool w_learned = is_spell_learned(SLOT_W);
        const bool w_ready   = is_spell_ready(SLOT_W);
        const bool allow_q   = menu::combo_use_q && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)
                            && (!w_learned || w_ready || combo_awaiting_w_q);

        if (allow_q && try_q(target, menu::combo_q_hitchance)) {
            if (combo_awaiting_w_q) combo_awaiting_w_q = false;
            else                    combo_did_first_q = true;
            return;
        }

        if (menu::combo_use_w && combo_did_first_q && w_ready && has_enough_mana(SLOT_W)
            && !is_spell_ready(SLOT_Q)) {
            if (try_w_self()) {
                combo_awaiting_w_q = true;
                combo_did_first_q  = false;
            }
        }
    }

    void harass()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;
        if (!menu::harass_use_q) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!sdk::target_selector) return;
        auto* target = pick_hero_target(spell_data::q_range);
        if (!target) return;

        if (!menu::harass_use_w || !is_spell_learned(SLOT_W)) {
            // Single bomb harass
            if (is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
                try_q(target, menu::combo_q_hitchance);
            return;
        }

        // Q→W→Q in harass too
        const bool w_ready = is_spell_ready(SLOT_W);
        const bool allow_q = (w_ready || harass_awaiting_w_q)
                          && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q);

        if (allow_q && try_q(target, menu::combo_q_hitchance)) {
            if (harass_awaiting_w_q) harass_awaiting_w_q = false;
            else                     harass_did_first_q = true;
            return;
        }

        if (harass_did_first_q && w_ready && has_enough_mana(SLOT_W)
            && !is_spell_ready(SLOT_Q)) {
            if (try_w_self()) {
                harass_awaiting_w_q = true;
                harass_did_first_q  = false;
            }
        }
    }

    void clear()
    {
        if (!menu::clear_use_q) return;
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        const auto pp = p->get_position();
        const float radius = spell_data::q_effect_radius;

        // Find best minion cluster center within Q range
        int best_count = 0;
        math::vector3 best_pos = pp;
        auto minions = g_sdk->object_manager->get_minions();
        for (auto* m : minions) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            if (pp.distance(mp) > spell_data::q_range + m->get_bounding_radius()) continue;

            int count = 0;
            for (auto* o : minions) {
                if (!is_valid_enemy_minion(o)) continue;
                if (mp.distance(o->get_position()) <= radius + o->get_bounding_radius()) ++count;
            }
            if (count > best_count) {
                best_count = count;
                best_pos = mp;
            }
        }

        // Always Q jungle monsters in range
        bool ok = (best_count >= menu::clear_q_min);
        for (auto* m : minions) {
            if (!m || !m->is_valid() || m->is_dead()) continue;
            if (!m->is_monster()) continue;
            if (pp.distance(m->get_position()) <= spell_data::q_range + m->get_bounding_radius()) {
                ok = true;
                if (best_count < 1) best_pos = m->get_position();
                break;
            }
        }

        if (ok && pp.distance(best_pos) <= spell_data::q_range)
            cast_q_at(best_pos);
    }

    void flee()
    {
        // E on self — speed boost in flee mode
        if (!menu::e_flee_self) return;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        try_e_target(p);
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        // Reset combo chain state when not in combo
        if (!sdk::orbwalker || !sdk::orbwalker->combo()) {
            combo_awaiting_w_q = false;
            combo_did_first_q  = false;
        }
        if (!sdk::orbwalker || !sdk::orbwalker->harass()) {
            harass_awaiting_w_q = false;
            harass_did_first_q  = false;
        }

        // Always-on auto behaviors
        run_auto_r();
        run_auto_e_anti_melee();
        run_killsteal();

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())  combo();
        else if (sdk::orbwalker->harass()) harass();
        else if (sdk::orbwalker->clear())  clear();
        else if (sdk::orbwalker->flee())   flee();
    }

    static float get_combo_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;

        float dmg = 0.f;
        // Q damage — doubled if W learned (anticipates double-bomb chain)
        if (is_spell_learned(SLOT_Q)) {
            const float q = sdk::damage->get_spell_damage(p, target, SLOT_Q);
            dmg += is_spell_learned(SLOT_W) ? q * 2.f : q;
        }
        // 2 AAs
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        dmg += aa * 2.f;
        return dmg;
    }

    void __fastcall on_draw()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        const auto pos = p->get_position();

        auto draw_circle = [&](math::vector3 const& c, float r, uint32_t clr) {
            if (sdk::shaders) sdk::shaders->draw_world_circle(c, r, 2.f, clr);
            else if (g_sdk->renderer) g_sdk->renderer->add_circle_3d(c, r, 2.f, clr);
        };

        auto get_draw_color = [&](int slot, uint32_t clr) -> uint32_t {
            auto sp = p->get_spell(slot);
            if (!sp || sp->get_level() == 0) return 0u;
            if (menu::draw_only_ready && sp->get_cooldown() > 0.f) return 0u;
            return sp->get_cooldown() <= 0.f ? clr : ((clr & 0x00FFFFFFu) | 0x40000000u);
        };

        if (menu::draw_q_range) {
            const uint32_t c = get_draw_color(SLOT_Q, menu::color_q);
            if (c) draw_circle(pos, spell_data::q_range, c);
        }
        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(pos, spell_data::e_range, c);
        }
        if (menu::draw_r_range) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(pos, spell_data::r_range, c);
        }

        // Bomb marker on enemies that already have Q bomb stuck on them
        if (g_sdk->renderer) {
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                if (!enemy_has_bomb(h)) continue;
                const auto hbar = h->get_health_bar_position();
                if (hbar.x < 1.f && hbar.y < 1.f) continue;
                g_sdk->renderer->add_text("BOMB", 12.f, { hbar.x, hbar.y - 20.f }, 1, 0xFFFFAA00u);
            }
        }

        // Status text above player
        if (menu::draw_status_text && g_sdk->renderer) {
            const auto screen = g_sdk->renderer->world_to_screen(pos);
            const char* r_label = menu::auto_r_enabled ? "Auto R: ON" : "Auto R: OFF";
            const uint32_t r_clr = menu::auto_r_enabled ? 0xFF00FF00u : 0xFF888888u;
            g_sdk->renderer->add_text(r_label, 14.f, { screen.x, screen.y - 60.f }, 1, r_clr);
        }

        if ((menu::draw_damage_bar || menu::draw_r_dmg_bar) && sdk::damage && g_sdk->renderer) {
            const bool r_learned = is_spell_learned(SLOT_R);
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                if (menu::draw_damage_bar) {
                    const float dmg = get_combo_damage(h);
                    if (dmg > 0.f) g_sdk->renderer->add_damage_indicator(h, dmg);
                }
                if (menu::draw_r_dmg_bar && r_learned) {
                    const float r_dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                    const float hp = h->get_hp(), mhp = h->get_max_hp();
                    if (r_dmg > 0.f && mhp > 0.f && hp > 0.f) {
                        const auto hbar = h->get_health_bar_position();
                        if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                            const float r0 = hp / mhp;
                            const float r1 = std::max(0.f, hp - r_dmg) / mhp;
                            if (r0 > r1) {
                                math::rect rc;
                                rc.left   = hbar.x + ox + r1 * bw;
                                rc.top    = hbar.y + oy;
                                rc.right  = hbar.x + ox + r0 * bw;
                                rc.bottom = hbar.y + oy + bh;
                                g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
                            }
                        }
                    }
                }
            }
        }
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker) return true;
        if (!data->target->is_hero()) return true;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || data->target->get_team_id() == p->get_team_id()) return true;

        // Disable AA in combo/harass at higher levels (Zilean's AAs are weak +
        // they push wave undesirably; better to position for Q→W→Q)
        const bool in_fight = sdk::orbwalker->combo() || sdk::orbwalker->harass();
        if (in_fight && menu::combo_disable_aa_lvl > 0
            && p->get_level() >= menu::combo_disable_aa_lvl)
            return false;

        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("zilean", "MeowAIO - Zilean");
        if (!s_menu) {
            g_sdk->log_console("[!] Zilean: failed to create menu category");
            return;
        }

        // Q — Time Bomb
        auto q_menu = s_menu->add_sub_category("q_time_bomb", "Q - Time Bomb");
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_q,  [](bool v) { menu::clear_use_q = v; });
        q_menu->add_slider_int("c_min", "  Clear Min Minions", 1, 6, 1, menu::clear_q_min,
            [](int v) { menu::clear_q_min = v; });
        q_menu->add_checkbox("ks", "Killsteal", menu::ks_use_q, [](bool v) { menu::ks_use_q = v; });
        q_menu->add_separator();
        q_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 5, menu::combo_q_hitchance,
            [](int v) { menu::combo_q_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Rewind
        auto w_menu = s_menu->add_sub_category("w_rewind", "W - Rewind");
        w_menu->add_checkbox("combo",  "Use in Combo (Q→W→Q double-bomb)", menu::combo_use_w,
            [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("harass", "Use in Harass (double-bomb)", menu::harass_use_w,
            [](bool v) { menu::harass_use_w = v; });

        // E — Time Warp
        auto e_menu = s_menu->add_sub_category("e_time_warp", "E - Time Warp");
        e_menu->add_checkbox("combo", "Use in Combo (slow enemy)", menu::combo_use_e,
            [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("flee_self", "E on Self in Flee Mode", menu::e_flee_self,
            [](bool v) { menu::e_flee_self = v; });
        e_menu->add_checkbox("auto_ally_melee", "Auto E Ally vs Melee Enemy", menu::e_auto_ally_melee,
            [](bool v) { menu::e_auto_ally_melee = v; });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // R — Chronoshift
        auto r_menu = s_menu->add_sub_category("r_chronoshift", "R - Chronoshift");
        r_menu->add_checkbox("auto", "Auto R Ally", menu::auto_r_enabled,
            [](bool v) { menu::auto_r_enabled = v; });
        r_menu->add_slider_int("hp",    "  Ally HP %% Threshold", 5, 50, 5, menu::auto_r_hp_pct,
            [](int v) { menu::auto_r_hp_pct = v; });
        r_menu->add_slider_int("dmg",   "  Min Incoming Damage %%", 0, 100, 5, menu::auto_r_min_dmg_pct,
            [](int v) { menu::auto_r_min_dmg_pct = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });

        // Misc
        auto misc_menu = s_menu->add_sub_category("misc", "Misc");
        misc_menu->add_slider_int("disable_aa", "Disable AA in Combo/Harass from Level (0=never)",
            0, 18, 1, menu::combo_disable_aa_lvl,
            [](int v) { menu::combo_disable_aa_lvl = v; });
        misc_menu->add_slider_int("harass_mana", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("status", "Status Text Above Player", menu::draw_status_text,
            [](bool v) { menu::draw_status_text = v; });
        draw_menu->add_checkbox("dmg", "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_q", "Q/R Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("col_e", "E Color",   menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("col_r", "R Color",   menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        g_sdk->log_console("[MeowAIO] Zilean module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
        }
        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        combo_did_first_q = false;
        combo_awaiting_w_q = false;
        harass_did_first_q = false;
        harass_awaiting_w_q = false;
        last_spell_cast_time = 0.f;

        if (g_sdk) g_sdk->log_console("[-] Zilean module unloaded");
    }
}
