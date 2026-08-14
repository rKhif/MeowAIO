#include "shyvana.h"
#include "damage/items.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace shyvana
{
    static menu_category* s_menu = nullptr;

    // --- Form / state ---
    static bool  is_dragon          = false;
    static bool  q_pending_reset    = false;
    static float w_cast_time        = 0.f;

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

    bool has_enough_resource(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        // Shyvana's R costs fury (100). Q/W/E are free.
        if (slot != SLOT_R) return true;
        return p->get_par() >= spell_data::r_fury_cost;
    }

    bool is_casting()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto ac = p->get_active_spell_cast();
        if (!ac) return false;
        auto sc = ac->get_spell_cast();
        if (!sc) return false;
        if (sc->is_basic_attack() || sc->is_special_attack()) return false;
        return ac->get_cast_end_time() > g_sdk->clock_facade->get_game_time();
    }

    float fury_pct()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const float mx = p->get_max_par();
        if (mx <= 0.f) return 0.f;
        return (p->get_par() / mx) * 100.f;
    }

    float hp_pct()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const float mx = p->get_max_hp();
        if (mx <= 0.f) return 0.f;
        return (p->get_hp() / mx) * 100.f;
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
        if (!m->is_visible()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (m->get_team_id() == p->get_team_id()) return false;
        return true;
    }

    static float distance_to(game_object* o)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !o) return FLT_MAX;
        return p->get_position().distance(o->get_position());
    }

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

    static int count_enemies_near(float range)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        const auto pp = p->get_position();
        int n = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pp.distance(h->get_position()) <= range + h->get_bounding_radius())
                ++n;
        }
        return n;
    }

    // ============================================================================
    // FORM / BUFF STATE
    // ============================================================================
    static bool has_self_buff(uint32_t hash)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto* b = p->get_buff_by_hash(hash);
        return b && b->is_active();
    }

    static int get_q_stage()
    {
        if (has_self_buff(spell_data::buff_hash_q_attack_ready3)) return 3;
        if (has_self_buff(spell_data::buff_hash_q_attack_ready2)) return 2;
        if (has_self_buff(spell_data::buff_hash_q_attack_ready))  return 1;
        return 0;
    }

    static bool has_q_recast()
    {
        return has_self_buff(spell_data::buff_hash_q2)
            || has_self_buff(spell_data::buff_hash_q3);
    }

    static bool is_q_locked()
    {
        return has_self_buff(spell_data::buff_hash_q_lockout);
    }

    static void refresh_form()
    {
        is_dragon = has_self_buff(spell_data::buff_hash_dragon_form);
    }

    // Effective E params per form
    static float get_e_radius()    { return is_dragon ? spell_data::e_dragon_radius : spell_data::e_human_radius; }
    static float get_e_speed()     { return is_dragon ? spell_data::e_dragon_speed : spell_data::e_human_speed; }
    static float get_e_cast_time() { return is_dragon ? spell_data::e_dragon_cast_time : spell_data::e_human_cast_time; }

    static float get_r_range()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return spell_data::r_target_range_max;
        auto sp = p->get_spell(SLOT_R);
        if (!sp) return spell_data::r_target_range_max;
        const int rank = std::clamp(sp->get_level(), 1, 3);
        return spell_data::r_target_range_by_rank[rank - 1];
    }

    // ============================================================================
    // PREDICTION (crash-safe; form-aware E)
    // ============================================================================
    static pred_sdk::pred_data predict_e(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_E;
        spell.range              = spell_data::e_range;
        spell.radius             = get_e_radius();
        spell.delay              = get_e_cast_time();
        spell.projectile_speed   = get_e_speed();
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // Manual minion-collision check between player and E cast position
    // (only used in human form — dragon E has no unit collision)
    static bool e_blocked_by_minion(const math::vector3& cast_pos, game_object* skip)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto src = p->get_position();
        const float dx = cast_pos.x - src.x;
        const float dz = cast_pos.z - src.z;
        const float seg_len2 = dx * dx + dz * dz;
        if (seg_len2 <= 1.f) return false;
        const float w_half = get_e_radius();

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (m == skip || !m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
            if (m->get_team_id() == p->get_team_id()) continue;
            if (!m->is_lane_minion()) continue;

            const auto mp = m->get_position();
            const float rel_dx = mp.x - src.x;
            const float rel_dz = mp.z - src.z;
            float t = (rel_dx * dx + rel_dz * dz) / seg_len2;
            if (t < 0.f || t > 1.f) continue;
            const float cx = src.x + dx * t;
            const float cz = src.z + dz * t;
            const float ddx = mp.x - cx;
            const float ddz = mp.z - cz;
            if (std::sqrt(ddx * ddx + ddz * ddz) <= w_half + m->get_bounding_radius())
                return true;
        }
        return false;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool try_e(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_E)) return false;
        if (distance_to(target) > spell_data::e_range + target->get_bounding_radius()) return false;

        auto pred = predict_e(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        // Human form E collides with units — manual block check
        if (!is_dragon && target->is_hero()
            && e_blocked_by_minion(pred.cast_position, target))
            return false;

        bool ok = sdk::common->cast_spell(SLOT_E, pred.cast_position);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_w_shield()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_W)) return false;
        if (has_self_buff(spell_data::buff_hash_w_shield)) return false;
        bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) {
            const float now = g_sdk->clock_facade->get_game_time();
            w_cast_time = now;
            last_spell_cast_time = now;
        }
        return ok;
    }

    static bool try_w_recast(game_object* target)
    {
        if (!sdk::common) return false;
        if (!has_self_buff(spell_data::buff_hash_w_shield)) return false;
        if (!is_spell_ready(SLOT_W)) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        const float elapsed = now - w_cast_time;
        if (elapsed < spell_data::w_recast_min_delay) return false;
        if (elapsed * 1000.f < (float)menu::w_recast_delay_ms) return false;

        if (menu::w_recast_mode == W_RECAST_NEVER) return false;

        if (menu::w_recast_mode == W_RECAST_ALWAYS) {
            bool ok = sdk::common->cast_spell(SLOT_W);
            if (ok) last_spell_cast_time = now;
            return ok;
        }

        // Smart recast
        if (!target) return false;
        const float dist = distance_to(target);
        const float eff = spell_data::w_effect_radius + target->get_bounding_radius();

        // Dragon heal when low HP
        if (is_dragon && dist <= eff && hp_pct() <= (float)menu::w_heal_hp_pct) {
            bool ok = sdk::common->cast_spell(SLOT_W);
            if (ok) last_spell_cast_time = now;
            return ok;
        }

        // Killable target in range
        if (dist <= eff && sdk::damage) {
            auto p = g_sdk->object_manager->get_local_player();
            if (p) {
                const float dmg = sdk::damage->get_spell_damage(p, target, SLOT_W);
                const float ehp = target->get_hp() + target->get_all_shield();
                if (dmg >= ehp) {
                    bool ok = sdk::common->cast_spell(SLOT_W);
                    if (ok) last_spell_cast_time = now;
                    return ok;
                }
            }
        }

        // 2+ enemies in radius — pop for AOE value
        if (dist <= eff && count_enemies_near(spell_data::w_effect_radius) >= 2) {
            bool ok = sdk::common->cast_spell(SLOT_W);
            if (ok) last_spell_cast_time = now;
            return ok;
        }

        // Shield about to expire — pop for guaranteed value
        if (elapsed >= spell_data::w_shield_duration - 0.3f && dist <= eff + 100.f) {
            bool ok = sdk::common->cast_spell(SLOT_W);
            if (ok) last_spell_cast_time = now;
            return ok;
        }
        return false;
    }

    static bool try_r_engage(game_object* target)
    {
        if (!sdk::common || is_dragon) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_resource(SLOT_R)) return false;
        if (hp_pct() < (float)menu::combo_r_min_hp) return false;

        const float r_range = get_r_range();
        if (distance_to(target) > r_range + target->get_bounding_radius()) return false;
        if (count_enemies_near(r_range) < menu::combo_r_min_tgts) return false;

        bool ok = sdk::common->cast_spell(SLOT_R, target->get_position());
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // ============================================================================
    // KILLSTEAL / AUTO-E
    // ============================================================================
    static void auto_e_logic()
    {
        if (!is_spell_ready(SLOT_E)) return;
        if (!menu::auto_e_cc && !menu::auto_e_dash && !menu::ks_e) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const auto pp = p->get_position();

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pp.distance(h->get_position()) > spell_data::e_range + h->get_bounding_radius())
                continue;

            const bool cc = menu::auto_e_cc && (
                h->has_buff_of_type(buff_type::stun)
                || h->has_buff_of_type(buff_type::snare)
                || h->has_buff_of_type(buff_type::knockup)
                || h->has_buff_of_type(buff_type::suppression));
            const bool dash = menu::auto_e_dash && h->is_dashing();

            if (cc || dash) {
                if (try_e(h, pred_sdk::hitchance::low)) return;
            }

            if (menu::ks_e && sdk::damage) {
                const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_E);
                const float ehp = h->get_hp() + h->get_all_shield();
                if (dmg >= ehp) {
                    if (try_e(h, pred_sdk::hitchance::medium)) return;
                }
            }
        }
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

        auto* target = pick_hero_target(spell_data::e_range + 100.f);
        if (!target) return;

        const float dist = distance_to(target);
        const float aa_reach = p->get_attack_range() + p->get_bounding_radius()
                             + target->get_bounding_radius() + 50.f;

        // R engage (Human form only) — before everything for fear window
        if (menu::combo_use_r && !is_dragon) {
            if (menu::combo_r_mode == R_MODE_ENGAGE) {
                if (try_r_engage(target)) return;
            } else if (menu::combo_r_mode == R_MODE_KILLABLE && sdk::damage) {
                const float dmg = sdk::damage->get_spell_damage(p, target, SLOT_R);
                const float ehp = target->get_hp() + target->get_all_shield();
                if (dmg >= ehp) {
                    if (try_r_engage(target)) return;
                }
            }
        }

        if (dist > aa_reach) {
            // Out of melee — E first for slow + W for MS to close
            if (menu::combo_use_e) try_e(target, menu::combo_e_hitchance);
            if (menu::combo_use_w) try_w_shield();
        } else {
            // In melee — W first for shield/detonation, then E
            if (menu::combo_use_w) {
                if (has_self_buff(spell_data::buff_hash_w_shield))
                    try_w_recast(target);
                else
                    try_w_shield();
            }
            if (menu::combo_use_e && !menu::e_outside_aa_only)
                try_e(target, menu::combo_e_hitchance);
        }

        // W recast when shield is active and target is now in range
        if (menu::combo_use_w && has_self_buff(spell_data::buff_hash_w_shield) && dist <= aa_reach)
            try_w_recast(target);
    }

    void harass()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::e_range + 100.f);
        if (!target) return;

        if (menu::harass_use_e) try_e(target, menu::harass_e_hitchance);
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        // Jungle priority
        if (sdk::target_selector) {
            auto* monster = sdk::target_selector->get_monster_target(
                [](game_object* m) {
                    return m && m->is_valid() && m->is_visible() && m->is_targetable()
                        && distance_to(m) <= spell_data::e_range;
                });
            if (monster) {
                if (menu::jungle_use_w && !has_self_buff(spell_data::buff_hash_w_shield)
                    && is_spell_ready(SLOT_W) && sdk::common) {
                    sdk::common->cast_spell(SLOT_W);
                    last_spell_cast_time = now;
                }
                if (menu::jungle_use_e && is_spell_ready(SLOT_E))
                    try_e(monster, pred_sdk::hitchance::low);
                return;
            }
        }

        // Lane: E on minion cluster (use spell_farm)
        if (!menu::lane_use_e || !is_spell_ready(SLOT_E)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_E;
        spell.range              = spell_data::e_range;
        spell.radius             = get_e_radius();
        spell.delay              = get_e_cast_time();
        spell.projectile_speed   = get_e_speed();
        spell.expected_hitchance = pred_sdk::hitchance::any;

        pred_sdk::pred_data pred{};
        try {
            pred = sdk::orbwalker->spell_farm(
                spell, menu::lane_e_min,
                spell_farm_flag::outside_of_attack_range,
                dmg_sdk::damage_type::magical);
        } catch (...) { pred.is_valid = false; }

        if (pred.is_valid && sdk::common) {
            sdk::common->cast_spell(SLOT_E, pred.cast_position);
            last_spell_cast_time = now;
        }
    }

    void fast_clear() { clear(); }

    void flee()
    {
        if (!menu::flee_use_w) return;
        if (!is_spell_ready(SLOT_W)) return;
        if (has_self_buff(spell_data::buff_hash_w_shield)) return;
        if (!sdk::common) return;
        sdk::common->cast_spell(SLOT_W);
        const float now = g_sdk->clock_facade->get_game_time();
        w_cast_time = now;
        last_spell_cast_time = now;
    }

    static void semi_e()
    {
        if (!is_spell_ready(SLOT_E)) return;
        auto* target = pick_hero_target(spell_data::e_range + 100.f);
        if (target) try_e(target, pred_sdk::hitchance::medium);
    }

    static void semi_r()
    {
        if (is_dragon) return;
        if (!is_spell_ready(SLOT_R) || !has_enough_resource(SLOT_R)) return;
        if (!sdk::common) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        auto* target = pick_hero_target(get_r_range() + 200.f);
        const auto cast_pos = target
            ? target->get_position()
            : (g_sdk->hud_manager ? g_sdk->hud_manager->get_cursor_position() : p->get_position());
        sdk::common->cast_spell(SLOT_R, cast_pos);
        last_spell_cast_time = g_sdk->clock_facade->get_game_time();
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        refresh_form();

        // Always-on auto-E (CC / dash / killsteal)
        auto_e_logic();

        if (menu::semi_e_active) { semi_e(); return; }
        if (menu::semi_r_active) { semi_r(); return; }

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->clear())      clear();
        else if (sdk::orbwalker->flee())       flee();
    }

    static float get_combo_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;

        float total = 0.f;
        // 2 AAs (Q empowered), Q on-hit damage doubled effectively
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        total += aa * 2.f;
        if (is_spell_ready(SLOT_Q)) total += sdk::damage->get_spell_damage(p, target, SLOT_Q);
        if (is_spell_ready(SLOT_W) || has_self_buff(spell_data::buff_hash_w_shield))
            total += sdk::damage->get_spell_damage(p, target, SLOT_W);
        if (is_spell_ready(SLOT_E)) total += sdk::damage->get_spell_damage(p, target, SLOT_E);
        if (is_spell_ready(SLOT_R) && !is_dragon)
            total += sdk::damage->get_spell_damage(p, target, SLOT_R);
        if (total > 0.f)
        {
            const float distance = p->get_position().distance(target->get_position());
            meow::item_damage::ability_proc_context items{};
            items.horizon_focus_active = distance
                >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
            meow::item_damage::ability_rune_context runes{};
            runes.cast_distance = distance;
            total += meow::item_damage::on_ability_damage(p, target, items)
                + meow::item_damage::on_ability_rune_damage(p, target, runes);
        }
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

        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(pos, spell_data::e_range, c);
        }
        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(pos, spell_data::w_effect_radius, c);
        }
        if (menu::draw_r_range && !is_dragon) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(pos, get_r_range(), c);
        }

        // Form / Fury / Q stage indicator above player
        if (menu::draw_form && g_sdk->renderer) {
            auto screen = g_sdk->renderer->world_to_screen(pos);
            screen.y -= 60.f;

            const float fury = p->get_par();
            const float max_fury = p->get_max_par() > 0.f ? p->get_max_par() : 100.f;
            char buf[64];
            if (is_dragon) {
                std::snprintf(buf, sizeof(buf), "DRAGON | Fury: %.0f / %.0f", fury, max_fury);
                g_sdk->renderer->add_text(buf, 14.f, screen, 1, 0xFFFF4400u);
            } else {
                std::snprintf(buf, sizeof(buf), "Human | Fury: %.0f / %.0f", fury, max_fury);
                g_sdk->renderer->add_text(buf, 14.f, screen, 1, 0xFFCCCCCCu);
            }

            const int qs = get_q_stage();
            if (qs > 0) {
                screen.y += 18.f;
                std::snprintf(buf, sizeof(buf), "Q%d Ready", qs);
                g_sdk->renderer->add_text(buf, 13.f, screen, 1, 0xFF00FFFFu);
            }
            if (has_q_recast()) {
                screen.y += 18.f;
                const bool locked = is_q_locked();
                g_sdk->renderer->add_text(
                    locked ? "Q Recast (locked)" : "Q Recast (ready!)",
                    13.f, screen, 1, locked ? 0xFFFF0000u : 0xFF00FF00u);
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

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (cast->get_spell_slot() == SLOT_W)
            w_cast_time = g_sdk->clock_facade->get_game_time();
    }

    void __fastcall on_execute_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (!cast->is_basic_attack() && !cast->is_special_attack()) return;
        if (!q_pending_reset) return;

        q_pending_reset = false;
        if (!is_spell_ready(SLOT_Q) || get_q_stage() != 0 || has_q_recast()) return;
        if (sdk::common) sdk::common->cast_spell(SLOT_Q);
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (buff->get_hash() == spell_data::buff_hash_dragon_form)
            is_dragon = true;
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        if (buff->get_hash() == spell_data::buff_hash_dragon_form) {
            is_dragon = false;
            return;
        }

        // Q recast chain — when lockout buff expires, immediately recast if window open
        if (buff->get_hash() != spell_data::buff_hash_q_lockout) return;
        if (!sdk::orbwalker) return;
        const bool in_fight = sdk::orbwalker->combo() || sdk::orbwalker->harass()
                           || sdk::orbwalker->clear() || sdk::orbwalker->fast_clear();
        if (!in_fight) return;

        if (has_self_buff(spell_data::buff_hash_q2) && menu::combo_recast_q
            && is_spell_ready(SLOT_Q) && sdk::common) {
            sdk::common->cast_spell(SLOT_Q);
            return;
        }

        if (has_self_buff(spell_data::buff_hash_q3) && menu::combo_use_q3 && is_dragon
            && is_spell_ready(SLOT_Q) && sdk::common) {
            sdk::common->cast_spell(SLOT_Q);
        }
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker) return true;

        auto* target = data->target;
        bool want_q = false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return true;

        if (target->is_hero() && target->get_team_id() != p->get_team_id()) {
            if (sdk::orbwalker->combo() && menu::combo_use_q) want_q = true;
            else if (sdk::orbwalker->harass() && menu::harass_use_q) want_q = true;
        }
        else if ((target->is_monster() || target->is_lane_minion())
                 && (sdk::orbwalker->clear() || sdk::orbwalker->fast_clear())
                 && sdk::orbwalker->is_spell_farm_enabled()) {
            if (target->is_monster() && menu::jungle_use_q) want_q = true;
            else if (target->is_lane_minion() && menu::lane_use_q) want_q = true;
        }

        if (want_q && is_spell_ready(SLOT_Q) && get_q_stage() == 0 && !has_q_recast()) {
            q_pending_reset = true;
        }
        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("shyvana", "MeowAIO - Shyvana");
        if (!s_menu) {
            g_sdk->log_console("[!] Shyvana: failed to create menu category");
            return;
        }

        // Q — Emberstrike
        auto q_menu = s_menu->add_sub_category("q_emberstrike", "Q - Emberstrike");
        q_menu->add_separator();
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear_lane",   "Use on Lane Minions", menu::lane_use_q,
            [](bool v) { menu::lane_use_q = v; });
        q_menu->add_checkbox("clear_jg", "Use in clear",  menu::jungle_use_q,
            [](bool v) { menu::jungle_use_q = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("recast", "Allow Q2 Recast",  menu::combo_recast_q,
            [](bool v) { menu::combo_recast_q = v; });
        q_menu->add_checkbox("q3",     "Use Dragon Q3", menu::combo_use_q3,
            [](bool v) { menu::combo_use_q3 = v; });

        // W — Inferno Aegis
        auto w_menu = s_menu->add_sub_category("w_inferno_aegis", "W - Inferno Aegis");
        w_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_w,  [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("jungle", "Use on Monsters", menu::jungle_use_w,
            [](bool v) { menu::jungle_use_w = v; });
        w_menu->add_checkbox("flee",   "Use in Flee", menu::flee_use_w,
            [](bool v) { menu::flee_use_w = v; });
        w_menu->add_separator();
        w_menu->add_combo("recast", "Recast Mode",
            { "Smart (Defensive)", "Always", "Never" }, menu::w_recast_mode,
            [](int v) { menu::w_recast_mode = v; });
        w_menu->add_slider_int("delay", "Recast Delay (ms)", 0, 1000, 50, menu::w_recast_delay_ms,
            [](int v) { menu::w_recast_delay_ms = v; });
        w_menu->add_slider_int("hp", "Dragon Heal HP %", 10, 80, 5, menu::w_heal_hp_pct,
            [](int v) { menu::w_heal_hp_pct = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });

        // E — Molten Burst
        auto e_menu = s_menu->add_sub_category("e_molten_burst", "E - Molten Burst");
        e_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_e,  [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_e, [](bool v) { menu::harass_use_e = v; });
        e_menu->add_checkbox("clear_lane",   "Use on Lane Minions",   menu::lane_use_e,
            [](bool v) { menu::lane_use_e = v; });
        e_menu->add_slider_int("min", "  Lane Min Minions",  1, 7, 1, menu::lane_e_min,
            [](int v) { menu::lane_e_min = v; });
        e_menu->add_checkbox("clear_jg", "Use on Jungle Monsters", menu::jungle_use_e,
            [](bool v) { menu::jungle_use_e = v; });
        e_menu->add_separator();
        e_menu->add_slider_int("c_hc", "Combo Hitchance",  0, 100, 5, menu::combo_e_hitchance,
            [](int v) { menu::combo_e_hitchance = v; });
        e_menu->add_slider_int("h_hc", "Harass Hitchance", 0, 100, 5, menu::harass_e_hitchance,
            [](int v) { menu::harass_e_hitchance = v; });
        e_menu->add_checkbox("outside", "Combo: Prefer E Outside AA Range", menu::e_outside_aa_only,
            [](bool v) { menu::e_outside_aa_only = v; });
        e_menu->add_separator();
        e_menu->add_checkbox("auto_cc",   "Auto E on CC'd Hero",        menu::auto_e_cc,
            [](bool v) { menu::auto_e_cc = v; });
        e_menu->add_checkbox("auto_dash", "Auto E on Dashing Hero",     menu::auto_e_dash,
            [](bool v) { menu::auto_e_dash = v; });
        e_menu->add_checkbox("ks",        "Killsteal",                  menu::ks_e,
            [](bool v) { menu::ks_e = v; });
        e_menu->add_separator();
        e_menu->add_hotkey("semi_e", "Semi-Manual E (Hold)", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_e_active = pressed;
                if (hk) menu::semi_e_key_str = *hk;
            });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // R — Dragon's Descent
        auto r_menu = s_menu->add_sub_category("r_dragons_descent", "R - Dragon's Descent");
        r_menu->add_checkbox("combo", "Use in Combo (Human only)", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_combo("mode", "Mode",
            { "Disabled", "Killable Only", "Combo Engage", "Semi Manual Only" }, menu::combo_r_mode,
            [](int v) { menu::combo_r_mode = v; });
        r_menu->add_slider_int("min_tgts", "  Min Enemies in Range", 1, 5, 1, menu::combo_r_min_tgts,
            [](int v) { menu::combo_r_min_tgts = v; });
        r_menu->add_slider_int("min_hp",   "  Min Self HP %",       0, 100, 5, menu::combo_r_min_hp,
            [](int v) { menu::combo_r_min_hp = v; });
        r_menu->add_checkbox("turret", "Allow Turret Dives", menu::combo_r_turret,
            [](bool v) { menu::combo_r_turret = v; });
        r_menu->add_separator();
        r_menu->add_hotkey("semi_r", "Semi-Manual R (Hold)", 'G', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_r_active = pressed;
                if (hk) menu::semi_r_key_str = *hk;
            });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Range (Human form)", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("form", "Form / Fury / Q Stage Text", menu::draw_form,
            [](bool v) { menu::draw_form = v; });
        draw_menu->add_checkbox("dmg",  "Combo Damage Indicator",     menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("col_w", "W Color", menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("col_r", "R Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::execute_cast,
            reinterpret_cast<void*>(on_execute_cast));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        g_sdk->log_console("[MeowAIO] Shyvana module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
                reinterpret_cast<void*>(on_process_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::execute_cast,
                reinterpret_cast<void*>(on_execute_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }
        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        is_dragon = false;
        q_pending_reset = false;
        w_cast_time = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_e_active = false;
        menu::semi_r_active = false;

        if (g_sdk) g_sdk->log_console("[-] Shyvana module unloaded");
    }
}
