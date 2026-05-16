#include "zeri.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace zeri
{
    static menu_category* s_menu = nullptr;

    // ============================================================================
    // WALL JUMP SPOTS (Summoner's Rift) — Zeri's E dashes through these walls
    // ============================================================================
    struct wall_jump_spot
    {
        math::vector3 start;
        math::vector3 end;
    };

    static const wall_jump_spot WALL_JUMPS[] = {
        { { 7614.f,   53.f, 3040.f  }, { 10206.f,  49.f, 2836.f  } },
        { { 10219.f,  14.f, 6105.f  }, { 11800.f,  34.f, 4889.f  } },
        { { 7260.f,   52.f, 6622.f  }, { 8580.f,  -70.f, 5720.f  } },
        { { 5799.f,   51.f, 3494.f  }, { 5529.f,   51.f, 1773.f  } },
        { { 3326.f,    9.f, 9880.f  }, { 2020.f,   69.f, 11394.f } },
        { { 4072.f,   96.f, 534.f   }, { 1076.f,  158.f, 392.f   } },
        { { 14296.f,  91.f, 11074.f }, { 14438.f, 164.f, 13868.f } },
        { { 524.f,    96.f, 4008.f  }, { 176.f,   184.f, 778.f   } },
        { { 13222.f,  41.f, 1058.f  }, { 10434.f,  51.f, 792.f   } },
        { { 13876.f,  48.f, 1854.f  }, { 14078.f,  52.f, 3992.f  } },
        { { 4712.f,  106.f, 600.f   }, { 7758.f,   49.f, 734.f   } },
        { { 14246.f,  91.f, 10458.f }, { 14146.f,  52.f, 7360.f  } },
        { { 700.f,    53.f, 7556.f  }, { 624.f,    96.f, 4508.f  } },
        { { 4144.f,   50.f, 5064.f  }, { 2770.f,   56.f, 6718.f  } },
        { { 6912.f,   52.f, 6192.f  }, { 6642.f,   49.f, 4832.f  } },
        { { 10984.f,  91.f, 14380.f }, { 13964.f, 172.f, 14468.f } },
        { { 8064.f,   53.f, 14180.f }, { 10472.f,  91.f, 14310.f } },
        { { 9068.f,   53.f, 4216.f  }, { 10418.f,  49.f, 3460.f  } },
        { { 9400.f,  -71.f, 5362.f  }, { 9210.f,   56.f, 3874.f  } },
        { { 6492.f,   52.f, 5982.f  }, { 7704.f,   50.f, 5390.f  } },
        { { 6312.f,  -71.f, 9090.f  }, { 7556.f,   53.f, 8086.f  } },
        { { 5260.f,   57.f, 11348.f }, { 5986.f,  -57.f, 9436.f  } },
        { { 5704.f,   57.f, 10898.f }, { 3874.f,  -70.f, 11172.f } },
        { { 7248.f,   56.f, 11772.f }, { 4510.f,   56.f, 12090.f } },
        { { 10738.f,  53.f, 9838.f  }, { 12056.f,  52.f, 8150.f  } },
        { { 13170.f,  55.f, 6110.f  }, { 12138.f,  52.f, 7268.f  } },
        { { 10224.f,  52.f, 7212.f  }, { 11720.f,  52.f, 7308.f  } },
        { { 12174.f,  52.f, 5080.f  }, { 11882.f,  52.f, 6550.f  } },
        { { 7930.f,   52.f, 8684.f  }, { 8242.f,   50.f, 10098.f } },
    };
    static constexpr size_t WALL_JUMP_COUNT = sizeof(WALL_JUMPS) / sizeof(WALL_JUMPS[0]);

    static const wall_jump_spot* find_nearest_wall_jump(const math::vector3& pos, float max_dist)
    {
        const wall_jump_spot* best = nullptr;
        float best_d = max_dist;
        for (size_t i = 0; i < WALL_JUMP_COUNT; ++i) {
            const float d = std::min(pos.distance(WALL_JUMPS[i].start),
                                     pos.distance(WALL_JUMPS[i].end));
            if (d < best_d) { best_d = d; best = &WALL_JUMPS[i]; }
        }
        return best;
    }

    // True if `target_pos` lies within the front 180° toward the cursor.
    static bool is_in_flee_direction(const math::vector3& target_pos)
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->hud_manager) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pos = p->get_position();
        const auto cursor = g_sdk->hud_manager->get_cursor_position();
        const float fx = cursor.x - pos.x, fz = cursor.z - pos.z;
        const float tx = target_pos.x - pos.x, tz = target_pos.z - pos.z;
        return (fx * tx + fz * tz) > 0.f;
    }

    // --- State refreshed each tick ---
    static bool  overcharged       = false;  // R active
    static bool  lightning_rounds  = false;  // E wall-jump empower
    static bool  passive_charged   = false;  // passive ready (next AA enhanced)
    static int   hypercharged_stk  = 0;
    static float attack_speed      = 0.658f;
    static float bonus_as          = 0.f;
    static float total_ad          = 56.f;

    // --- Cast pacing ---
    static float last_q_cast_time = 0.f;
    static float last_w_cast_time = 0.f;
    static float last_spell_cast_time = 0.f;
    static constexpr float MIN_SPELL_DELAY = 0.05f;

    // ============================================================================
    // SPELL VALIDATION HELPERS
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
        // Q is free, treat as always-affordable
        if (slot == SLOT_Q) return true;
        return p->get_par() >= p->get_spell_mana_cost(slot);
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
    // STATE REFRESH
    // ============================================================================
    static void refresh_state()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        attack_speed = p->get_attack_speed_mod();
        bonus_as     = attack_speed / 0.658f - 1.f;
        total_ad     = p->get_attack_damage();

        overcharged      = p->get_buff_by_hash(spell_data::buff_hash_r_overcharged) != nullptr;
        lightning_rounds = p->get_buff_by_hash(spell_data::buff_hash_e_lightning) != nullptr;
        passive_charged  = p->get_buff_by_hash(spell_data::buff_hash_q_passive_ready) != nullptr;
        if (auto* b = p->get_buff_by_hash(spell_data::buff_hash_r_stacks))
            hypercharged_stk = (int)b->get_stacks();
        else
            hypercharged_stk = 0;
    }

    // Effective Q range — extends with on-hit AA range bonuses
    static float get_q_range()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return spell_data::q_range;
        const float aa_extra = std::max(0.f, p->get_attack_range() - spell_data::passive_aa_range);
        return spell_data::q_range + aa_extra;
    }

    static float get_q_speed()
    {
        return overcharged ? spell_data::q_speed_overcharged : spell_data::q_speed;
    }

    static float get_q_cast_time()
    {
        const float cap = spell_data::q_as_cap;
        const float ct = spell_data::q_windup_pct / std::min(attack_speed, cap);
        return overcharged ? ct * spell_data::q_overcharged_windup_mult : ct;
    }

    static float get_w_cast_time()
    {
        return std::max(spell_data::w_cast_time_max - spell_data::w_cast_time_scale * bonus_as,
                        spell_data::w_cast_time_min);
    }

    static float get_passive_execute_threshold()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const int level = std::max(1, p->get_level());
        const float ap = p->get_ability_power();
        // 60..150 over levels 1-18
        const float base = 60.f + (150.f - 60.f) / 17.f * (level - 1);
        return base + 0.18f * ap;
    }

    // ============================================================================
    // PREDICTION (crash-safe)
    // ============================================================================
    static pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_Q;
        spell.range              = get_q_range();
        spell.radius             = spell_data::q_radius;
        spell.delay              = get_q_cast_time();
        spell.projectile_speed   = get_q_speed();
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    static pred_sdk::pred_data predict_w(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range_pulse;
        spell.radius             = spell_data::w_radius;
        spell.delay              = get_w_cast_time();
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // Manual minion-collision check between player and the W cast endpoint
    static bool w_blocked_by_minion(const math::vector3& cast_pos, game_object* skip)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto src = p->get_position();
        const float dx = cast_pos.x - src.x;
        const float dz = cast_pos.z - src.z;
        const float seg_len2 = dx * dx + dz * dz;
        if (seg_len2 <= 1.f) return false;

        const float w_half = spell_data::w_radius;
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
    enum class q_result { casted, failed };

    static q_result try_q(game_object* target, int hitchance)
    {
        if (!target || !sdk::common) return q_result::failed;
        if (!is_spell_ready(SLOT_Q)) return q_result::failed;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return q_result::failed;

        // For structures, just point Q toward them
        if (target->is_turret() || target->is_inhibitor() || target->is_nexus()) {
            const auto tp = target->get_position();
            const auto cast_pos = p->get_position().extended(tp, get_q_range(), true);
            if (sdk::common->cast_spell(SLOT_Q, cast_pos)) {
                last_q_cast_time = g_sdk->clock_facade->get_game_time();
                last_spell_cast_time = last_q_cast_time;
                return q_result::casted;
            }
            return q_result::failed;
        }

        if (!sdk::prediction) return q_result::failed;

        auto pred = predict_q(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return q_result::failed;

        if (!sdk::common->cast_spell(SLOT_Q, pred.cast_position)) return q_result::failed;
        last_q_cast_time = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = last_q_cast_time;
        return q_result::casted;
    }

    static bool try_w(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_w_cast_time < 0.1f) return false;

        if (distance_to(target) > spell_data::w_range_pulse + 100.f) return false;

        auto pred = predict_w(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        if (target->is_hero() && w_blocked_by_minion(pred.cast_position, target))
            return false;

        if (sdk::common->cast_spell(SLOT_W, pred.cast_position)) {
            last_w_cast_time = now;
            last_spell_cast_time = now;
            return true;
        }
        return false;
    }

    static bool try_e_gap_close(game_object* target)
    {
        if (!menu::combo_use_e || !target) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (!sdk::common) return false;

        const float dist = distance_to(target);
        if (dist <= get_q_range()) return false;

        // Don't dash into too many enemies
        int nearby = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (distance_to(h) <= 1200.f) ++nearby;
        }
        if (nearby > menu::combo_e_max_near) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto dash_end = p->get_position().extended(target->get_position(),
                                                          spell_data::e_range, true);
        bool ok = sdk::common->cast_spell(SLOT_E, dash_end);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_e_flee()
    {
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (!sdk::common || !g_sdk->hud_manager) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pos = p->get_position();

        // Pick the nearest wall-jump spot. The spot data already encodes
        // exactly where to stand (near_pt) and where you'll land (far_pt),
        // so once a spot is in range, the cast direction toward far_pt is
        // all the engine needs to trigger the wall-vault.
        auto* spot = find_nearest_wall_jump(pos, menu::flee_wall_jump_max_dist);
        if (!spot) return false;

        const float d_start = pos.distance(spot->start);
        const float d_end   = pos.distance(spot->end);
        const auto& far_pt  = (d_start < d_end) ? spot->end : spot->start;
        if (!is_in_flee_direction(far_pt)) return false;

        // Aim from the player's current position toward far_pt. If the
        // player is slightly off the near marker, the angle self-corrects
        // to still point at the landing point.
        const auto dash_end = pos.extended(far_pt, spell_data::e_range, true);

        bool ok = sdk::common->cast_spell(SLOT_E, dash_end);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r(int min_enemies)
    {
        if (overcharged) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (!sdk::common) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pos = p->get_position();
        int n = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pos.distance(h->get_position()) <= spell_data::r_radius + 25.f)
                ++n;
        }
        if (n < min_enemies) return false;

        bool ok = sdk::common->cast_spell(SLOT_R);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r_smart()
    {
        if (overcharged) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pos = p->get_position();

        int in_r = 0, in_fight = 0;
        game_object* closest = nullptr;
        float closest_d = FLT_MAX;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float d = pos.distance(h->get_position());
            if (d <= spell_data::r_radius + 25.f) ++in_r;
            if (d <= 1500.f) ++in_fight;
            if (d < closest_d) { closest_d = d; closest = h; }
        }

        if (in_r >= menu::combo_r_min && in_fight >= 2)
            return try_r(in_r);

        // Execute fallback: 1 hero in R range + would die
        if (in_r >= 1 && closest && sdk::damage) {
            if (venplus::utility::has_anti_burst_buff(closest)) return false;
            const float dmg = sdk::damage->get_spell_damage(p, closest, SLOT_R);
            const float ehp = closest->get_hp() + closest->get_all_shield();
            if (dmg >= ehp) return try_r(1);
        }
        return false;
    }

    // ============================================================================
    // KILLSTEAL
    // ============================================================================
    static void killsteal_w()
    {
        if (!menu::combo_w_killsteal) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::damage) return;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (venplus::utility::has_anti_burst_buff(h)) continue;
            if (distance_to(h) > spell_data::w_range_pulse) continue;
            const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_W);
            const float ehp = h->get_hp() + h->get_all_shield();
            if (dmg >= ehp && try_w(h, menu::w_hitchance)) return;
        }
    }

    // ============================================================================
    // AA SUBSTITUTION (on_before_attack)
    // ============================================================================
    static bool allow_normal_aa(game_object* target)
    {
        if (!target) return false;
        const float ehp = target->get_hp() + target->get_all_shield();
        const bool execute = ehp <= get_passive_execute_threshold();

        switch (menu::aa_mode) {
        case AA_OFF:        return false;
        case AA_FULL_CHARGE: return passive_charged && target->is_hero();
        case AA_EXECUTE:    return execute;
        case AA_SMART:
        default:
            if (passive_charged && target->is_hero()) return true;
            return execute;
        }
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker) return true;

        auto* target = data->target;
        const bool q_ready = is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q);

        // Try Q first if ready; if cast succeeded, suppress the AA
        if (q_ready) {
            if (try_q(target, pred_sdk::hitchance::low) == q_result::casted)
                return false;
        }

        // Decide whether the AA itself should fire
        if (target->is_minion()) {
            const float ehp = target->get_hp() + target->get_all_shield();
            return ehp <= get_passive_execute_threshold();
        }
        if (target->is_turret() || target->is_inhibitor() || target->is_nexus()) {
            return !passive_charged;  // save charged AA for heroes
        }
        if (target->is_hero()) {
            return allow_normal_aa(target);
        }
        return true;
    }

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo()
    {
        if (!sdk::target_selector) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        const float w_max_range = spell_data::w_range_pulse + 100.f;
        auto* target = pick_hero_target(w_max_range);
        if (!target) return;

        const float dist = distance_to(target);

        // R inside its radius
        if (menu::combo_use_r && !overcharged && dist <= spell_data::r_radius + 125.f) {
            if (menu::combo_r_mode == R_MODE_SMART) try_r_smart();
            else                                     try_r(menu::combo_r_min);
        }

        // E gap close just outside Q range
        if (menu::combo_use_e && dist > get_q_range()
            && dist <= get_q_range() + spell_data::e_range + 100.f) {
            try_e_gap_close(target);
        }

        // W (long-range poke)
        if (menu::combo_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
            try_w(target, menu::w_hitchance);
        }

        // Q — fire directly when target is in Q range. Q is also fired via
        // on_before_attack (AA replacement), but that path only triggers when
        // the orbwalker is attempting an AA. Q range extends past AA range, so
        // we need this manual path for poke at the edge of Q range.
        if (dist <= get_q_range() && is_spell_ready(SLOT_Q)) {
            try_q(target, pred_sdk::hitchance::low);
        }
    }

    void harass()
    {
        if (!sdk::target_selector) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        const float w_max_range = spell_data::w_range_pulse + 100.f;
        auto* target = pick_hero_target(w_max_range);
        if (!target) return;

        const float dist = distance_to(target);

        // Q first if target is in Q range (extends past AA range)
        if (dist <= get_q_range() && is_spell_ready(SLOT_Q)) {
            if (try_q(target, pred_sdk::hitchance::low) == q_result::casted) return;
        }

        if (menu::harass_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
            const int hc = std::max((int)pred_sdk::hitchance::very_high, menu::w_hitchance);
            try_w(target, hc);
        }
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::lane_mana_pct) return;
        if (!menu::lane_use_w) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        // Use the orbwalker's spell_farm helper: lasthit + when_attack_not_ready
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range_pulse;
        spell.radius             = spell_data::w_radius;
        spell.delay              = get_w_cast_time();
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
            last_w_cast_time = g_sdk->clock_facade->get_game_time();
            last_spell_cast_time = last_w_cast_time;
        }
    }

    void fast_clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::lane_mana_pct) return;

        if (menu::lane_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
            && !sdk::orbwalker->would_cancel_attack()) {

            pred_sdk::spell_data spell{};
            spell.spell_type         = pred_sdk::spell_type::linear;
            spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
            spell.spell_slot         = SLOT_W;
            spell.range              = spell_data::w_range_pulse;
            spell.radius             = spell_data::w_radius;
            spell.delay              = get_w_cast_time();
            spell.projectile_speed   = spell_data::w_speed;
            spell.expected_hitchance = pred_sdk::hitchance::any;

            pred_sdk::pred_data pred{};
            try {
                pred = sdk::orbwalker->spell_farm(
                    spell, 1, spell_farm_flag::when_attack_not_ready,
                    dmg_sdk::damage_type::physical);
            } catch (...) { pred.is_valid = false; }

            if (pred.is_valid && sdk::common) {
                sdk::common->cast_spell(SLOT_W, pred.cast_position);
                last_w_cast_time = g_sdk->clock_facade->get_game_time();
                last_spell_cast_time = last_w_cast_time;
                return;
            }
        }

        // Jungle fallback
        if (menu::jungle_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
            && !sdk::orbwalker->would_cancel_attack() && sdk::target_selector) {
            auto* monster = sdk::target_selector->get_monster_target(
                [](game_object* m) {
                    return m && m->is_valid() && m->is_visible() && m->is_targetable()
                        && distance_to(m) <= spell_data::w_range_pulse;
                });
            if (monster) try_w(monster, pred_sdk::hitchance::any);
        }
    }

    void lasthit()
    {
        if (!menu::lh_w_cannon) return;
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::damage) return;

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
            if (m->get_team_id() == p->get_team_id()) continue;
            if (!m->is_lane_minion_siege() && !m->is_lane_minion_super()) continue;

            const float d = distance_to(m);
            if (d <= get_q_range() || d > spell_data::w_range_pulse) continue;

            const float wdmg = sdk::damage->get_spell_damage(p, m, SLOT_W);
            if (wdmg >= m->get_hp()) {
                if (try_w(m, pred_sdk::hitchance::medium)) return;
            }
        }
    }

    void flee()
    {
        if (menu::flee_use_e) try_e_flee();
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        refresh_state();

        // Semi-W (manual W on hold)
        if (menu::semi_w_active && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
            auto* t = pick_hero_target(spell_data::w_range_pulse);
            if (t) try_w(t, pred_sdk::hitchance::high);
            return;
        }

        killsteal_w();

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
        float dmg = 0.f;
        if (is_spell_ready(SLOT_Q)) dmg += sdk::damage->get_spell_damage(p, target, SLOT_Q);
        if (is_spell_ready(SLOT_W)) dmg += sdk::damage->get_spell_damage(p, target, SLOT_W);
        if (is_spell_ready(SLOT_E)) dmg += sdk::damage->get_spell_damage(p, target, SLOT_E);
        if (is_spell_ready(SLOT_R)) dmg += sdk::damage->get_spell_damage(p, target, SLOT_R);
        // 3 AAs (Zeri spams Q/AA hybrid quickly)
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        dmg += aa * 3.f;
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
            if (c) draw_circle(pos, get_q_range(), c);
        }
        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(pos, spell_data::w_range_pulse, c);
        }
        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(pos, spell_data::e_range, c);
        }
        if (menu::draw_r_range) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(pos, spell_data::r_radius, c);
        }

        if (menu::draw_jump_spots && is_spell_learned(SLOT_E)) {
            for (size_t i = 0; i < WALL_JUMP_COUNT; ++i) {
                const auto& s = WALL_JUMPS[i];
                const float d = std::min(pos.distance(s.start), pos.distance(s.end));
                if (d > menu::draw_jump_max_dist) continue;
                draw_circle(s.start, 30.f, menu::color_jump);
                draw_circle(s.end,   30.f, menu::color_jump);
            }
        }

        // Status text above player
        if (menu::draw_status_text && g_sdk->renderer) {
            const auto screen = g_sdk->renderer->world_to_screen(pos);
            const char* aa_label = "";
            switch (menu::aa_mode) {
            case AA_SMART:       aa_label = "AA: Smart"; break;
            case AA_FULL_CHARGE: aa_label = "AA: Full Charge"; break;
            case AA_EXECUTE:     aa_label = "AA: Execute"; break;
            case AA_OFF:         aa_label = "AA: Off"; break;
            }
            const uint32_t aa_clr = (menu::aa_mode == AA_OFF) ? 0xFFFF6060u : 0xFF80FFC0u;
            g_sdk->renderer->add_text(aa_label, 14.f, { screen.x, screen.y - 80.f }, 1, aa_clr);

            if (overcharged) {
                g_sdk->renderer->add_text("OVERCHARGED", 14.f,
                    { screen.x, screen.y - 60.f }, 1, 0xFFFFFF00u);
            } else if (passive_charged) {
                g_sdk->renderer->add_text("PASSIVE READY", 14.f,
                    { screen.x, screen.y - 60.f }, 1, 0xFF00FFA0u);
            }
            if (lightning_rounds) {
                g_sdk->renderer->add_text("LIGHTNING ROUNDS", 13.f,
                    { screen.x, screen.y - 40.f }, 1, 0xFFFFB400u);
            }
        }

        if ((menu::draw_damage_bar || menu::draw_r_dmg_bar) && sdk::damage && g_sdk->renderer) {
            const bool r_learned = is_spell_learned(SLOT_R);
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h, spell_data::w_range_pulse)) continue;
                if (menu::draw_damage_bar) {
                    const float dmg = get_combo_damage(h);
                    if (dmg > 0.f) g_sdk->renderer->add_damage_indicator(h, dmg);
                }
                if (menu::draw_r_dmg_bar && r_learned) {
                    const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                    const float hp = h->get_hp(), mhp = h->get_max_hp();
                    if (dmg > 0.f && mhp > 0.f && hp > 0.f) {
                        const auto hbar = h->get_health_bar_position();
                        if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                            const float r0 = hp / mhp;
                            const float r1 = std::max(0.f, hp - dmg) / mhp;
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

    void __fastcall on_process_cast(game_object* /*obj*/, spell_cast* /*cast*/)
    {
        // Reserved — pending-cast tracking already lives in last_q/w_cast_time
    }

    void __fastcall on_buff_gain(game_object* /*obj*/, buff_instance* /*buff*/) {}
    void __fastcall on_buff_loss(game_object* /*obj*/, buff_instance* /*buff*/) {}

    // Block the in-game R keypress unless enough enemies are inside R's
    // radius — prevents wasting Lightning Crash on a single straggler.
    bool __fastcall on_cast_spell(game_object* obj, int spell_slot,
                                  math::vector3 /*position*/, game_object* /*target*/)
    {
        if (spell_slot != SLOT_R) return true;
        if (!menu::r_block_enabled) return true;
        if (overcharged) return true;

        auto p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
        if (!p || !obj || obj != p) return true;

        const int needed = menu::r_block_use_custom
            ? std::max(1, menu::r_block_custom_min)
            : 1;
        const auto pos = p->get_position();
        int count = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pos.distance(h->get_position())
                <= spell_data::r_radius + h->get_bounding_radius()) {
                if (++count >= needed) return true;
            }
        }
        return false;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("zeri", "MeowAIO - Zeri");
        if (!s_menu) {
            g_sdk->log_console("[!] Zeri: failed to create menu category");
            return;
        }

        // Main
        auto main_menu = s_menu->add_sub_category("main", "Main Settings");
        main_menu->add_combo("aa_mode", "AA Mode",
            { "Smart", "Full Charge", "Execute", "Off" }, menu::aa_mode,
            [](int v) { menu::aa_mode = v; });

        // Q — Burst Fire
        auto q_menu = s_menu->add_sub_category("q_burst_fire", "Q - Burst Fire");
        q_menu->add_label("Q replaces AAs automatically (always-on).");
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Ultrashock Laser
        auto w_menu = s_menu->add_sub_category("w_ultrashock_laser", "W - Ultrashock Laser");
        w_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_w,  [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w, [](bool v) { menu::harass_use_w = v; });
        w_menu->add_checkbox("clear",  "Use in Clear",  menu::lane_use_w,   [](bool v) { menu::lane_use_w = v; });
        w_menu->add_checkbox("lh_cannon", "Use to Save Cannon", menu::lh_w_cannon,
            [](bool v) { menu::lh_w_cannon = v; });
        w_menu->add_checkbox("jungle", "Use on Jungle Monsters", menu::jungle_use_w,
            [](bool v) { menu::jungle_use_w = v; });
        w_menu->add_checkbox("ks", "Killsteal", menu::combo_w_killsteal,
            [](bool v) { menu::combo_w_killsteal = v; });
        w_menu->add_separator();
        w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::w_hitchance,
            [](int v) { menu::w_hitchance = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });
        w_menu->add_hotkey("semi_w", "Semi-Manual W", 'H', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_w_active = pressed;
                if (hk) menu::semi_w_key_str = *hk;
            });

        // E — Spark Surge
        auto e_menu = s_menu->add_sub_category("e_spark_surge", "E - Spark Surge");
        e_menu->add_checkbox("combo", "Gap Close in Combo", menu::combo_use_e,
            [](bool v) { menu::combo_use_e = v; });
        e_menu->add_slider_int("max_near", "  Max Enemies in 1200u", 1, 5, 1, menu::combo_e_max_near,
            [](int v) { menu::combo_e_max_near = v; });
        e_menu->add_checkbox("flee", "Use E in Flee (only at wall jump spots)", menu::flee_use_e,
            [](bool v) { menu::flee_use_e = v; });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // R — Lightning Crash
        auto r_menu = s_menu->add_sub_category("r_lightning_crash", "R - Lightning Crash");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_combo("mode", "R Mode", { "Always", "Smart" }, menu::combo_r_mode,
            [](int v) { menu::combo_r_mode = v; });
        r_menu->add_slider_int("min", "Min Enemies in R", 1, 5, 1, menu::combo_r_min,
            [](int v) { menu::combo_r_min = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("block", "Block Manual R",
            menu::r_block_enabled, [](bool v) { menu::r_block_enabled = v; });
        r_menu->add_checkbox("block_custom", "  Use Custom Min Enemies",
            menu::r_block_use_custom, [](bool v) { menu::r_block_use_custom = v; });
        r_menu->add_slider_int("block_custom_min", "    Custom Min Enemies in R",
            1, 5, 1, menu::r_block_custom_min,
            [](int v) { menu::r_block_custom_min = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });

        // Mana
        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });
        mana_menu->add_slider_int("clear",  "Clear Mana %",  0, 100, 5, menu::lane_mana_pct,
            [](int v) { menu::lane_mana_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("status", "Status Text Above Player", menu::draw_status_text,
            [](bool v) { menu::draw_status_text = v; });
        draw_menu->add_checkbox("dmg",    "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("col_w", "W Color", menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("col_r", "R Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });
        draw_menu->add_separator();
        draw_menu->add_checkbox("jump_spots", "Draw Wall Jump Spots", menu::draw_jump_spots,
            [](bool v) { menu::draw_jump_spots = v; });
        draw_menu->add_slider_float("jump_max_dist", "  Jump Spot Draw Distance", 500.f, 3000.f, 100.f,
            menu::draw_jump_max_dist, [](float v) { menu::draw_jump_max_dist = v; });
        draw_menu->add_colorpicker("col_jump", "  Jump Spot Color", menu::color_jump,
            [](uint32_t c) { menu::color_jump = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->register_callback(event_manager::event::cast_spell,
            reinterpret_cast<void*>(on_cast_spell));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        g_sdk->log_console("[MeowAIO] Zeri module loaded");
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
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
            g_sdk->event_manager->unregister_callback(event_manager::event::cast_spell,
                reinterpret_cast<void*>(on_cast_spell));
        }

        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        overcharged = lightning_rounds = passive_charged = false;
        hypercharged_stk = 0;
        last_q_cast_time = 0.f;
        last_w_cast_time = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_w_active = false;

        if (g_sdk) g_sdk->log_console("[-] Zeri module unloaded");
    }
}
