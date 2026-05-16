#include "vayne.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace vayne
{
    static menu_category* s_menu = nullptr;

    // R (Final Hour) state
    static bool  r_active     = false;
    static float r_expire     = 0.f;

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
        // block spells.
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
    // E STUN — wall behind target check
    // ============================================================================
    static bool will_condemn_stun(game_object* target)
    {
        if (!target) return false;
        if (!g_sdk->nav_mesh) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto pp = p->get_position();
        const auto tp = target->get_position();

        // Direction player → target (the knockback direction)
        const float dx = tp.x - pp.x;
        const float dz = tp.z - pp.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1.f) return false;
        const float nx = dx / len;
        const float nz = dz / len;

        // Step from target outward in the knockback direction; if any sample
        // hits unpathable terrain within knockback range → wall stun.
        for (float d = spell_data::e_wall_check_step;
             d <= spell_data::e_wall_check_dist;
             d += spell_data::e_wall_check_step) {
            math::vector3 check{ tp.x + nx * d, tp.y, tp.z + nz * d };
            if (!g_sdk->nav_mesh->is_pathable(check)) return true;
        }
        return false;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool try_q_tumble(game_object* target, bool smart)
    {
        if (!sdk::common || !g_sdk->hud_manager) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto pp = p->get_position();
        const auto cursor = g_sdk->hud_manager->get_cursor_position();

        // Default: extend toward cursor by Q dash range
        math::vector3 tumble_pos = pp.extended(cursor, spell_data::q_dash_range, true);

        // Smart-Q: pick a position from 8 candidate angles that keeps the
        // target in AA range and prefers the cursor direction
        if (smart && target && g_sdk->nav_mesh) {
            const float aa_reach = p->get_attack_range() + p->get_bounding_radius()
                                 + target->get_bounding_radius();
            const auto tp = target->get_position();
            math::vector3 best = tumble_pos;
            float best_score = -FLT_MAX;

            for (int i = 0; i < 8; ++i) {
                const float angle = (float)i * 3.14159265f * 0.25f;
                math::vector3 cand{
                    pp.x + spell_data::q_dash_range * std::cos(angle),
                    pp.y,
                    pp.z + spell_data::q_dash_range * std::sin(angle)
                };
                if (!g_sdk->nav_mesh->is_pathable(cand)) continue;
                const float dt = cand.distance(tp);
                if (dt > aa_reach) continue;

                // Prefer kiting (further from target within reach) + cursor bias
                const float kite = dt / aa_reach;
                const float cursor_d = cand.distance(cursor);
                const float cursor_bias = 1.f - std::min(1.f, cursor_d / 1000.f);
                const float score = kite * 2.f + cursor_bias;
                if (score > best_score) {
                    best_score = score;
                    best = cand;
                }
            }
            tumble_pos = best;
        }

        bool ok = sdk::common->cast_spell(SLOT_Q, tumble_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_e_target(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (distance_to(target) > spell_data::e_range) return false;
        bool ok = sdk::common->cast_spell(SLOT_E, target);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static int count_enemies_near(float range)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        const auto pp = p->get_position();
        int n = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pp.distance(h->get_position()) <= range) ++n;
        }
        return n;
    }

    static bool try_r_activate()
    {
        if (r_active) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (count_enemies_near(spell_data::r_radius_check) < menu::combo_r_min)
            return false;

        bool ok = sdk::common->cast_spell(SLOT_R);
        if (ok) {
            r_active = true;
            r_expire = g_sdk->clock_facade->get_game_time() + 12.f; // approximate
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        }
        return ok;
    }

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(700.f);
        if (!target) return;

        // R when enemies are around (gated to combo)
        if (menu::combo_use_r && !r_active && is_spell_ready(SLOT_R) && has_enough_mana(SLOT_R))
            try_r_activate();

        // E condemn — only if we expect a wall stun (when option enabled),
        // but always fire if E alone would kill the target (when killable option enabled)
        if (menu::combo_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)
            && distance_to(target) <= spell_data::e_range) {
            bool e_kills = false;
            if (menu::combo_e_killable && sdk::damage) {
                const float e_dmg = sdk::damage->get_spell_damage(p, target, SLOT_E);
                // Use predicted HP (~0.3s lookahead) so in-flight AAs/projectiles
                // are accounted for instead of the raw current HP snapshot.
                float effective_hp = target->get_hp();
                if (sdk::health_prediction) {
                    const float pred = sdk::health_prediction->get_predicted_health(target, 0.3f);
                    if (pred > 0.f) effective_hp = std::min(effective_hp, pred);
                }
                // Account for shields shown in HP if the API exposes it via get_hp;
                // otherwise nothing extra to do here.
                e_kills = e_dmg >= effective_hp;
            }
            if (e_kills) {
                try_e_target(target);
            } else if (menu::combo_e_only_stun) {
                if (will_condemn_stun(target)) try_e_target(target);
            } else {
                try_e_target(target);
            }
        }
        // Q is fired in on_after_attack as an AA reset
    }

    void harass()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!menu::harass_use_e || !is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E))
            return;

        auto* target = pick_hero_target(spell_data::e_range);
        if (!target) return;
        if (!will_condemn_stun(target)) return;

        try_e_target(target);
    }

    void fast_clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::jungle_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        if (!sdk::target_selector) return;
        auto* monster = sdk::target_selector->get_monster_target(
            [](game_object* m) {
                if (!m || !m->is_valid() || !m->is_visible() || !m->is_targetable()) return false;
                return distance_to(m) <= 600.f;
            });
        if (!monster) return;

        if (menu::jungle_use_e && monster->is_epic_monster()
            && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E))
            try_e_target(monster);
    }

    void flee()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead() || !g_sdk->hud_manager) return;
        if (is_casting()) return;

        // Q tumble toward cursor
        if (is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)) {
            try_q_tumble(nullptr, /*smart=*/false);
        }

        // E knockback any enemy in range
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;
        const auto pp = p->get_position();
        game_object* closest = nullptr;
        float closest_d = spell_data::e_range + 50.f;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float d = pp.distance(h->get_position());
            if (d < closest_d) { closest_d = d; closest = h; }
        }
        if (closest) try_e_target(closest);
    }

    // ============================================================================
    // SEMI-AUTO E (hotkey held — fire E on closest enemy hero with wall stun)
    // ============================================================================
    static void try_e_semi()
    {
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        game_object* best = nullptr;
        float best_d = FLT_MAX;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h, spell_data::e_range)) continue;
            if (!will_condemn_stun(h)) continue;
            const float d = distance_to(h);
            if (d < best_d) { best_d = d; best = h; }
        }
        if (best) try_e_target(best);
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (r_active && now >= r_expire) r_active = false;

        if (menu::semi_e_active) try_e_semi();
        if (menu::semi_r_active && !r_active && is_spell_ready(SLOT_R) && has_enough_mana(SLOT_R)
            && sdk::common) {
            sdk::common->cast_spell(SLOT_R);
        }

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->flee())       flee();
    }

    static float get_w_proc_damage(game_object* target)
    {
        // 3rd-stack on-hit true damage: ~6% max HP per rank, monster-capped
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return 0.f;
        auto sp = p->get_spell(SLOT_W);
        if (!sp || sp->get_level() <= 0) return 0.f;
        const int rank = sp->get_level();

        const float pct = 0.05f + 0.01f * (float)rank;
        const float raw = pct * target->get_max_hp();
        const float min_dmg = 35.f + 15.f * (float)rank;
        float result = std::max(raw, min_dmg);
        if (target->is_monster()) {
            const float cap = 125.f + 15.f * (float)rank;
            result = std::min(result, cap);
        }
        return result;
    }

    static float get_combo_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;

        float total = 0.f;
        // 3 AAs of follow-up
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        total += aa * 3.f;
        // W procs once over 3 AAs
        if (is_spell_learned(SLOT_W)) total += get_w_proc_damage(target);
        // Q + E spell damage if learned
        if (is_spell_ready(SLOT_Q)) total += sdk::damage->get_spell_damage(p, target, SLOT_Q);
        if (is_spell_ready(SLOT_E)) total += sdk::damage->get_spell_damage(p, target, SLOT_E);
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

        if (menu::draw_aa_range) {
            const float aa_r = p->get_attack_range() + p->get_bounding_radius();
            draw_circle(pos, aa_r, menu::color_aa);
        }
        if (menu::draw_e_range && is_spell_learned(SLOT_E)) {
            uint32_t c = menu::color_e;
            auto sp = p->get_spell(SLOT_E);
            if (menu::draw_only_ready && sp && sp->get_cooldown() > 0.f) c = 0u;
            else if (sp && sp->get_cooldown() > 0.f) c = (c & 0x00FFFFFFu) | 0x40000000u;
            if (c) draw_circle(pos, spell_data::e_range, c);
        }

        // R Final Hour status above player
        if (menu::draw_r_status && r_active && g_sdk->renderer) {
            const float now = g_sdk->clock_facade->get_game_time();
            const float remaining = r_expire - now;
            if (remaining > 0.f) {
                const auto screen = g_sdk->renderer->world_to_screen(pos);
                char buf[32];
                std::snprintf(buf, sizeof(buf), "R: %.1fs", remaining);
                g_sdk->renderer->add_text(buf, 14.f, { screen.x, screen.y - 60.f }, 1, menu::color_r);
            }
        }

        // E stun indicators on enemies that would be wall-pinned
        if (menu::draw_stun_spots && is_spell_learned(SLOT_E) && g_sdk->renderer) {
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h, spell_data::e_range + 100.f)) continue;
                if (!will_condemn_stun(h)) continue;
                draw_circle(h->get_position(), 50.f, menu::color_stun);
                const auto hbar = h->get_health_bar_position();
                if (hbar.x < 1.f && hbar.y < 1.f) continue;
                g_sdk->renderer->add_text("E STUN", 13.f, { hbar.x, hbar.y - 20.f }, 1, menu::color_stun);
            }
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

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        if (buff->get_name().find(spell_data::buff_name_r_active) != std::string::npos) {
            r_active = true;
            r_expire = buff->get_end_time();
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        if (buff->get_name().find(spell_data::buff_name_r_active) != std::string::npos)
            r_active = false;
    }

    bool __stdcall on_after_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker) return true;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return true;

        auto* target = data->target;
        const bool in_combo = sdk::orbwalker->combo();
        const bool in_harass = sdk::orbwalker->harass();
        const bool in_clear = sdk::orbwalker->clear() || sdk::orbwalker->fast_clear();

        if (in_combo || in_harass) {
            const bool use_q = in_combo ? menu::combo_use_q : menu::harass_use_q;
            if (!use_q || !target->is_hero()) return true;
            if (in_harass && mana_pct() < (float)menu::harass_mana_pct) return true;
            try_q_tumble(target, in_combo && menu::combo_smart_q);
        }
        else if (in_clear) {
            if (!menu::clear_use_q) return true;
            if (mana_pct() < (float)menu::clear_mana_pct) return true;
            try_q_tumble(nullptr, /*smart=*/false);
        }
        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("vayne", "MeowAIO - Vayne");
        if (!s_menu) {
            g_sdk->log_console("[!] Vayne: failed to create menu category");
            return;
        }

        // Q — Tumble
        auto q_menu = s_menu->add_sub_category("q_tumble", "Q - Tumble");
        q_menu->add_label("Auto-fires after AAs (AA reset).");
        q_menu->add_separator();
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_q,  [](bool v) { menu::clear_use_q = v; });
        q_menu->add_checkbox("jungle", "Use in Jungle", menu::jungle_use_q, [](bool v) { menu::jungle_use_q = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("smart_q", "Smart Q (Kite Position)", menu::combo_smart_q,
            [](bool v) { menu::combo_smart_q = v; });

        // E — Condemn
        auto e_menu = s_menu->add_sub_category("e_condemn", "E - Condemn");
        e_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_e,  [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("only_stun", "Combo: Only If Wall Stun", menu::combo_e_only_stun,
            [](bool v) { menu::combo_e_only_stun = v; });
        e_menu->add_checkbox("killable", "Use If Killable)", menu::combo_e_killable,
            [](bool v) { menu::combo_e_killable = v; });
        e_menu->add_checkbox("harass", "Use in Harass (wall-stun only)", menu::harass_use_e,
            [](bool v) { menu::harass_use_e = v; });
        e_menu->add_checkbox("jungle_epic", "Use on Epic Monsters", menu::jungle_use_e,
            [](bool v) { menu::jungle_use_e = v; });
        e_menu->add_separator();
        e_menu->add_hotkey("semi_e", "Semi-Manual E (Wall Stun, Hold)", 'G', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_e_active = pressed;
                if (hk) menu::semi_e_key_str = *hk;
            });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });
        e_menu->add_checkbox("draw_stun", "Stun Indicator", menu::draw_stun_spots,
            [](bool v) { menu::draw_stun_spots = v; });

        // R — Final Hour
        auto r_menu = s_menu->add_sub_category("r_final_hour", "R - Final Hour");
        r_menu->add_checkbox("combo", "Auto Cast in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_slider_int("min", "Min Enemies in 1000u", 1, 5, 1, menu::combo_r_min,
            [](int v) { menu::combo_r_min = v; });
        r_menu->add_separator();
        r_menu->add_hotkey("semi_r", "Semi-Manual R (Hold)", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_r_active = pressed;
                if (hk) menu::semi_r_key_str = *hk;
            });

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
        draw_menu->add_checkbox("r_status",   "R Active Timer",    menu::draw_r_status,
            [](bool v) { menu::draw_r_status = v; });
        draw_menu->add_checkbox("dmg",        "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_aa",   "AA Color",   menu::color_aa,
            [](uint32_t c) { menu::color_aa = c; });
        draw_menu->add_colorpicker("col_e",    "E Color",    menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("col_stun", "Stun Color", menu::color_stun,
            [](uint32_t c) { menu::color_stun = c; });
        draw_menu->add_colorpicker("col_r", "R Timer Color", menu::color_r,
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
            // VEN+ Vayne uses on_after_attack for AA-reset Q
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_after_attack));
        }

        if (sdk::infotab) {
            menu::infotab_semi_e = sdk::infotab->add_hotkey_text(
                &menu::semi_e_key_str, { "Semi E", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    return { menu::semi_e_active ? "ON" : "OFF",
                             menu::semi_e_active ? 0xFF00FF00u : 0xFFFF0000u };
                });
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key_str, { "Semi R", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    return { menu::semi_r_active ? "ON" : "OFF",
                             menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u };
                });
        }

        g_sdk->log_console("[MeowAIO] Vayne module loaded");
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

        if (sdk::infotab) {
            if (menu::infotab_semi_e != 0) { sdk::infotab->remove_text(menu::infotab_semi_e); menu::infotab_semi_e = 0; }
            if (sdk::infotab && menu::infotab_semi_r != 0) { sdk::infotab->remove_text(menu::infotab_semi_r); menu::infotab_semi_r = 0; }
        }

        s_menu = nullptr;
        r_active = false;
        r_expire = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_e_active = false;
        menu::semi_r_active = false;

        if (g_sdk) g_sdk->log_console("[-] Vayne module unloaded");
    }
}
