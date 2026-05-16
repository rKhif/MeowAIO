#include "ashe.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace ashe
{
    static menu_category* s_menu = nullptr;

    // Q (Ranger's Focus) state
    static bool  q_active     = false;
    static float q_expire     = 0.f;

    static constexpr float MIN_SPELL_DELAY = 0.05f;
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

    static float distance_to(game_object* o)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !o) return FLT_MAX;
        return p->get_position().distance(o->get_position());
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
    // STATE
    // ============================================================================
    static void update_q_state()
    {
        const float now = g_sdk->clock_facade->get_game_time();
        if (q_active && now > q_expire) q_active = false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        auto* b = p->get_buff_by_hash(spell_data::buff_hash_q_active);
        if (b && b->is_active()) {
            q_active = true;
            q_expire = b->get_end_time();
        } else {
            q_active = false;
        }
    }

    // ============================================================================
    // PREDICTION (crash-safe)
    // ============================================================================
    static pred_sdk::pred_data predict_w(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range;
        spell.radius             = spell_data::w_radius;
        spell.delay              = spell_data::w_delay;
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    static pred_sdk::pred_data predict_r(game_object* target, float dist, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_R;
        spell.range              = std::max(dist + 500.f, 3000.f);
        spell.radius             = spell_data::r_radius;
        spell.delay              = spell_data::r_delay;
        spell.projectile_speed   = spell_data::estimate_r_speed(dist);
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool try_q_self()
    {
        if (!sdk::common) return false;
        if (q_active) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        // Game tracks Focus stacks via passive — we just request the cast and it
        // succeeds when 4 stacks are present.
        bool ok = sdk::common->cast_spell(SLOT_Q);
        if (ok) {
            const float now = g_sdk->clock_facade->get_game_time();
            q_active = true;
            q_expire = now + spell_data::q_duration;
            last_spell_cast_time = now;
        }
        return ok;
    }

    static bool try_w(game_object* target)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (distance_to(target) > spell_data::w_range) return false;

        auto pred = predict_w(target, menu::combo_w_hitchance);
        if (!pred.is_valid || pred.hitchance < menu::combo_w_hitchance) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        // Cone half-angle filter — predicted position must lie within W's cone arc
        auto sp = p->get_spell(SLOT_W);
        const int rank = sp ? sp->get_level() : 0;
        if (rank > 0 && rank <= 5) {
            const auto pp = p->get_position();
            const auto a = pred.cast_position - pp;
            const auto b = pred.predicted_position - pp;
            const float la = std::sqrt(a.x * a.x + a.z * a.z);
            const float lb = std::sqrt(b.x * b.x + b.z * b.z);
            if (la > 1e-3f && lb > 1e-3f) {
                const float dot = (a.x * b.x + a.z * b.z) / (la * lb);
                const float angle = std::acos(std::clamp(dot, -1.f, 1.f));
                const float half_rad = spell_data::w_half_angle_deg[rank]
                                     * (3.14159265f / 180.f);
                if (half_rad > 0.f && angle > half_rad + 0.1f) return false;
            }
        }

        bool ok = sdk::common->cast_spell(SLOT_W, pred.cast_position);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;

        const float dist = distance_to(target);
        auto pred = predict_r(target, dist, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        bool ok = sdk::common->cast_spell(SLOT_R, pred.cast_position);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r_execute(game_object* target)
    {
        if (!target || !sdk::common || !sdk::damage) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (venplus::utility::has_anti_burst_buff(target)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const float dist = distance_to(target);
        const float speed = spell_data::estimate_r_speed(dist);
        const float travel = spell_data::r_delay + dist / std::max(speed, 1.f);

        const float now = g_sdk->clock_facade->get_game_time();
        float pred_hp = target->get_hp() + target->get_all_shield();
        if (sdk::health_prediction)
            pred_hp = sdk::health_prediction->get_predicted_health(target, now + travel)
                    + target->get_all_shield();
        if (pred_hp <= 0.f) return false;

        const float dmg = sdk::damage->get_spell_damage(p, target, SLOT_R);
        if (dmg < pred_hp) return false;

        return try_r(target, pred_sdk::hitchance::high);
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

        auto* target = pick_hero_target(spell_data::w_range);
        if (!target) return;

        if (menu::combo_use_r && is_spell_ready(SLOT_R) && has_enough_mana(SLOT_R)) {
            if (try_r_execute(target)) return;
            if (!menu::combo_r_only_kill && try_r(target, menu::combo_r_hitchance)) return;
        }

        if (menu::combo_use_w) try_w(target);

        // Q is fired in on_after_attack (auto-activate when stacks built)
    }

    void harass()
    {
        if (!sdk::target_selector) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::w_range);
        if (!target) return;

        if (menu::harass_use_w) try_w(target);
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;
        if (!menu::clear_use_w || !is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range;
        spell.radius             = spell_data::w_radius;
        spell.delay              = spell_data::w_delay;
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = pred_sdk::hitchance::any;

        pred_sdk::pred_data pred{};
        try {
            pred = sdk::orbwalker->spell_farm(
                spell, 3, spell_farm_flag::when_attack_not_ready,
                dmg_sdk::damage_type::physical);
        } catch (...) { pred.is_valid = false; }

        if (pred.is_valid && sdk::common) {
            sdk::common->cast_spell(SLOT_W, pred.cast_position);
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        }
    }

    void fast_clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        // Lane minions present?
        bool has_lane = false;
        const auto pp = p->get_position();
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
            if (m->get_team_id() == p->get_team_id() || !m->is_lane_minion()) continue;
            if (pp.distance(m->get_position()) > spell_data::w_range) continue;
            has_lane = true;
            break;
        }

        if (has_lane) {
            if (!menu::clear_use_w || !is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
            if (sdk::orbwalker->would_cancel_attack()) return;

            pred_sdk::spell_data spell{};
            spell.spell_type         = pred_sdk::spell_type::linear;
            spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
            spell.spell_slot         = SLOT_W;
            spell.range              = spell_data::w_range;
            spell.radius             = spell_data::w_radius;
            spell.delay              = spell_data::w_delay;
            spell.projectile_speed   = spell_data::w_speed;
            spell.expected_hitchance = pred_sdk::hitchance::any;

            pred_sdk::pred_data pred{};
            try {
                pred = sdk::orbwalker->spell_farm(
                    spell, -1, spell_farm_flag::when_attack_not_ready,
                    dmg_sdk::damage_type::physical);
            } catch (...) { pred.is_valid = false; }

            if (pred.is_valid && sdk::common) {
                sdk::common->cast_spell(SLOT_W, pred.cast_position);
                last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            }
            return;
        }

        // Jungle fallback
        if (!menu::jungle_use_w || mana_pct() < (float)menu::jungle_mana_pct) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;
        if (!sdk::target_selector) return;

        auto* monster = sdk::target_selector->get_monster_target(
            [](game_object* m) {
                if (!m || !m->is_valid() || m->is_visible() == false || !m->is_targetable()) return false;
                return distance_to(m) <= spell_data::w_range;
            });
        if (monster) try_w(monster);
    }

    void lasthit()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!sdk::orbwalker->is_spell_farm_lasthit_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;
        if (!menu::clear_use_w || !is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range;
        spell.radius             = spell_data::w_radius;
        spell.delay              = spell_data::w_delay;
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = pred_sdk::hitchance::any;

        pred_sdk::pred_data pred{};
        try {
            pred = sdk::orbwalker->spell_farm(
                spell, 1,
                static_cast<spell_farm_flag>(spell_farm_flag::lasthit | spell_farm_flag::when_attack_not_ready),
                dmg_sdk::damage_type::physical);
        } catch (...) { pred.is_valid = false; }

        if (pred.is_valid && sdk::common) {
            sdk::common->cast_spell(SLOT_W, pred.cast_position);
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        }
    }

    void flee()
    {
        // W slows enemies in cone — useful while running
        if (!menu::clear_use_w || !is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (!sdk::target_selector) return;
        auto* target = pick_hero_target(spell_data::w_range);
        if (target) try_w(target);
    }

    static void semi_r()
    {
        if (!menu::semi_r_active) return;
        if (!sdk::target_selector) return;
        const float r_max = (float)menu::semi_r_max_range;
        auto* target = pick_hero_target(r_max);
        if (target) try_r(target, pred_sdk::hitchance::high);
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        update_q_state();

        if (menu::semi_r_active) { semi_r(); return; }

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->clear())      clear();
        else if (sdk::orbwalker->lasthit())    lasthit();
        else if (sdk::orbwalker->flee())       flee();
    }

    static float get_combo_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;

        float total = 0.f;
        if (is_spell_ready(SLOT_W)) total += sdk::damage->get_spell_damage(p, target, SLOT_W);
        if (is_spell_learned(SLOT_R)) total += sdk::damage->get_spell_damage(p, target, SLOT_R);

        // 3 AAs of follow-up — doubled-ish if Q would be active (Q fires faster flurries)
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        const float aa_total = aa * (q_active ? 5.f : 3.f);
        total += aa_total;
        return total;
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

        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(pos, spell_data::w_range, c);
        }
        if (menu::draw_aa_range) {
            const float aa_r = p->get_attack_range() + p->get_bounding_radius();
            draw_circle(pos, aa_r, menu::color_aa);
        }

        // Q (Ranger's Focus) active timer above player
        if (menu::draw_q_status && q_active && g_sdk->renderer) {
            const float now = g_sdk->clock_facade->get_game_time();
            const float remaining = q_expire - now;
            if (remaining > 0.f) {
                const auto screen = g_sdk->renderer->world_to_screen(pos);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "Q: %.1fs", remaining);
                g_sdk->renderer->add_text(buf, 14.f, { screen.x, screen.y - 60.f }, 1, 0xFF00CCFFu);
            }
        }

        if ((menu::draw_damage_bar || menu::draw_killable_txt || menu::draw_r_dmg_bar)
            && sdk::damage && g_sdk->renderer) {
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

                if (menu::draw_killable_txt && r_learned) {
                    const float ehp = h->get_hp() + h->get_all_shield();
                    const float r_dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                    if (ehp > 0.f && r_dmg >= ehp) {
                        const auto hbar = h->get_health_bar_position();
                        if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                            g_sdk->renderer->add_text("R KILL", 13.f,
                                { hbar.x, hbar.y - 20.f }, 1, menu::color_r);
                        }
                    }
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

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (buff->get_hash() == spell_data::buff_hash_q_active) {
            q_active = true;
            q_expire = buff->get_end_time();
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (buff->get_hash() == spell_data::buff_hash_q_active)
            q_active = false;
    }

    bool __stdcall on_after_attack(orb_sdk::event_data* data)
    {
        if (!data || !sdk::orbwalker) return true;
        if (q_active) return true;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return true;

        const bool in_combo = sdk::orbwalker->combo();
        const bool in_harass = sdk::orbwalker->harass();
        if (!in_combo && !in_harass) return true;

        const bool use_q = in_combo ? menu::combo_use_q : menu::harass_use_q;
        if (!use_q) return true;
        if (in_harass && mana_pct() < (float)menu::harass_mana_pct) return true;

        try_q_self();
        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("ashe", "MeowAIO - Ashe");
        if (!s_menu) {
            g_sdk->log_console("[!] Ashe: failed to create menu category");
            return;
        }

        // Q — Ranger's Focus
        auto q_menu = s_menu->add_sub_category("q_rangers_focus", "Q - Ranger's Focus");
        q_menu->add_label("Auto-activates after 4 stacks (built from AAs).");
        q_menu->add_separator();
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });

        // W — Volley
        auto w_menu = s_menu->add_sub_category("w_volley", "W - Volley");
        w_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_w,  [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w, [](bool v) { menu::harass_use_w = v; });
        w_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_w,  [](bool v) { menu::clear_use_w = v; });
        w_menu->add_checkbox("jungle", "Use on Jungle Monsters", menu::jungle_use_w,
            [](bool v) { menu::jungle_use_w = v; });
        w_menu->add_separator();
        w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 5, menu::combo_w_hitchance,
            [](int v) { menu::combo_w_hitchance = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });

        // R — Enchanted Crystal Arrow
        auto r_menu = s_menu->add_sub_category("r_crystal_arrow", "R - Enchanted Crystal Arrow");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_checkbox("only_kill", "R Only if Killable", menu::combo_r_only_kill,
            [](bool v) { menu::combo_r_only_kill = v; });
        r_menu->add_separator();
        r_menu->add_hotkey("semi_r", "Semi-Manual R", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_r_active = pressed;
                if (hk) menu::semi_r_key_str = *hk;
            });
        r_menu->add_slider_int("semi_range", "Semi-R Max Range", 1000, 6000, 250,
            menu::semi_r_max_range, [](int v) { menu::semi_r_max_range = v; });

        // Mana
        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });
        mana_menu->add_slider_int("clear",  "Clear Mana %",  0, 100, 5, menu::clear_mana_pct,
            [](int v) { menu::clear_mana_pct = v; });
        mana_menu->add_slider_int("jungle", "Jungle Mana %", 0, 100, 5, menu::jungle_mana_pct,
            [](int v) { menu::jungle_mana_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("aa_range",   "Draw AA Range",     menu::draw_aa_range,
            [](bool v) { menu::draw_aa_range = v; });
        draw_menu->add_checkbox("q_status",   "Q Active Timer",    menu::draw_q_status,
            [](bool v) { menu::draw_q_status = v; });
        draw_menu->add_checkbox("dmg",        "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_checkbox("kill_txt",   "R Killable Text",   menu::draw_killable_txt,
            [](bool v) { menu::draw_killable_txt = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_w",  "W Color",  menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("col_aa", "AA Color", menu::color_aa,
            [](uint32_t c) { menu::color_aa = c; });
        draw_menu->add_colorpicker("col_r",  "R KILL Text Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_after_attack));
        }

        if (sdk::infotab) {
            infotab_sdk::text_entry title{ "Semi R", 0xFFFFFFFFu };
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key_str, title,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_r_active ? "ON" : "OFF", clr };
                });
        }

        g_sdk->log_console("[MeowAIO] Ashe module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }
        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_after_attack));
        }

        if (sdk::infotab && menu::infotab_semi_r != 0) {
            sdk::infotab->remove_text(menu::infotab_semi_r);
            menu::infotab_semi_r = 0;
        }

        s_menu = nullptr;
        q_active = false;
        q_expire = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_r_active = false;

        if (g_sdk) g_sdk->log_console("[-] Ashe module unloaded");
    }
}
