#include "zeri.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <random>
#include <span>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"
#include "infotab_sdk.hpp"
#include "sm_sdk.hpp"
#include <unordered_map>
#include <vector>

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

   
    static const wall_jump_spot* find_wall_jump_near_cursor(const math::vector3& player_pos,
                                                            const math::vector3& cursor_pos,
                                                            float player_max_dist)
    {
        const wall_jump_spot* best = nullptr;
        float best_score = FLT_MAX;
        for (size_t i = 0; i < WALL_JUMP_COUNT; ++i) {
            const auto& spot = WALL_JUMPS[i];
            const float ds_player = player_pos.distance(spot.start);
            const float de_player = player_pos.distance(spot.end);
            const float nearest_player = std::min(ds_player, de_player);
            if (nearest_player > player_max_dist) continue;

            
            const math::vector3& far_pt = (ds_player < de_player) ? spot.end : spot.start;
            const float cursor_d = cursor_pos.distance(far_pt);
            if (cursor_d < best_score) { best_score = cursor_d; best = &spot; }
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
    static int   hypercharged_stk  = 0;      // R stacks (Hypercharged)
    static float attack_speed      = 0.658f;
    static float bonus_as          = 0.f;
    static float total_ad          = 56.f;

    // --- Per-tick caches (refresh_state) ---
    static float c_q_range     = spell_data::q_range;
    static float c_q_speed     = spell_data::q_speed;
    static float c_q_cast_time = spell_data::q_windup_pct / 0.658f;
    static float c_w_cast_time = spell_data::w_cast_time_max;
    static float c_aa_range    = spell_data::passive_aa_range;

    // Hero snapshot — populated once per tick to avoid O(n) loops in every helper
    struct hero_snapshot {
        int           in_r        = 0;       // enemies inside R radius (+25 buffer)
        int           in_1200     = 0;       // enemies within 1200u (E dive cap)
        int           in_1500     = 0;       // enemies within 1500u (fight radius)
        game_object*  closest     = nullptr; // closest enemy hero
        float         closest_d   = FLT_MAX;
    };
    static hero_snapshot s_heroes{};

    // --- Wall jump hotkey state ---
    static math::vector3 s_jump_near        = {};
    static math::vector3 s_jump_far         = {};
    static bool          s_jump_locked      = false; // spot committed, walking to near pt
    static bool          s_jump_orb_blocked = false; // orbwalker frozen during walk
    static float         s_jump_last_move   = 0.f;   // throttle move_to spam

    // --- Cast pacing ---
    static float last_q_cast_time = 0.f;
    static float last_w_cast_time = 0.f;
    static float last_spell_cast_time = 0.f;
    static constexpr float MIN_SPELL_DELAY = 0.05f;

    // --- Queued manual E (deferred when Q is locking the cast) ---
    static bool          s_queued_e_active = false;
    static math::vector3 s_queued_e_pos    = {};
    static float         s_queued_e_expire = 0.f;

    // WALL JUMP HOTKEY — walk-to + vault
    static void release_jump_orb_block()
    {
        if (s_jump_orb_blocked && sdk::orbwalker) {
            sdk::orbwalker->set_forced_mode(orb_sdk::forced_mode_none);
        }
        s_jump_orb_blocked = false;
    }

    static void update_jump_spot()
    {
        if (!menu::jump_key_active) {
            release_jump_orb_block();
            s_jump_locked = false;
            return;
        }

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead() || !sdk::common) {
            release_jump_orb_block();
            menu::jump_key_active = false;
            s_jump_locked = false;
            return;
        }
        const auto pos = p->get_position();
        const float now = g_sdk->clock_facade->get_game_time();

        if (!s_jump_locked) {
            const wall_jump_spot* spot = nullptr;
            if (g_sdk->hud_manager) {
                const auto cursor = g_sdk->hud_manager->get_cursor_position();
                spot = find_wall_jump_near_cursor(pos, cursor, 750.f);
            }
            if (!spot) spot = find_nearest_wall_jump(pos, 500.f);
            if (!spot) { menu::jump_key_active = false; return; }

            const float d1 = pos.distance(spot->start);
            const float d2 = pos.distance(spot->end);
            if (d1 < d2) { s_jump_near = spot->start; s_jump_far = spot->end; }
            else          { s_jump_near = spot->end;   s_jump_far = spot->start; }

            s_jump_locked    = true;
            s_jump_last_move = 0.f;

            // Park the orbwalker so spell-farm / harass don't fight our walk.
            if (sdk::orbwalker) {
                sdk::orbwalker->set_forced_mode(orb_sdk::forced_mode_freeze);
                s_jump_orb_blocked = true;
            }

            sdk::common->issue_order(game_object_order::move_to, s_jump_near, false);
            s_jump_last_move = now;
            return;
        }

        // Cancel if the player's path destination diverged from the near point
        // (user clicked somewhere else).
        auto path = p->get_path();
        if (!path.empty() && path.back().distance(s_jump_near) > 80.f) {
            release_jump_orb_block();
            menu::jump_key_active = false;
            s_jump_locked = false;
            return;
        }

        if (pos.distance(s_jump_near) <= 25.f) {
            if (is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)) {
                // Normalize direction toward far_pt and cap cast position to
                // exactly E's max range. Passing a raw far_pt that's thousands
                // of units away makes the server short the dash on some spots.
                const float dx = s_jump_far.x - pos.x;
                const float dz = s_jump_far.z - pos.z;
                const float len = std::sqrt(dx * dx + dz * dz);
                math::vector3 cast_pos = s_jump_far;
                if (len > 1.f) {
                    const float nx = dx / len, nz = dz / len;
                    cast_pos = { pos.x + nx * spell_data::e_range,
                                 pos.y,
                                 pos.z + nz * spell_data::e_range };
                }
                if (sdk::common->cast_spell(SLOT_E, cast_pos)) {
                    last_spell_cast_time = now;
                    release_jump_orb_block();
                    menu::jump_key_active = false;
                    s_jump_locked = false;
                }
            }
        } else {
            // Throttle move_to to 10Hz instead of firing every tick (~60Hz).
            // Spamming move_to every frame can cause the server to drop or
            // reorder commands, jittering the path.
            if (now - s_jump_last_move >= 0.1f) {
                sdk::common->issue_order(game_object_order::move_to, s_jump_near, false);
                s_jump_last_move = now;
            }
        }
    }

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
        if (auto* b = p->get_buff_by_hash(spell_data::buff_hash_r_stacks); b && b->is_active())
            hypercharged_stk = std::max((int)b->get_stacks(), b->get_count());
        else
            hypercharged_stk = 0;

        // Cached ranges / cast times
        c_aa_range = p->get_attack_range();
        const float aa_extra = std::max(0.f, c_aa_range - spell_data::passive_aa_range);
        c_q_range  = spell_data::q_range + aa_extra;
        c_q_speed  = overcharged ? spell_data::q_speed_overcharged : spell_data::q_speed;
        float qct  = spell_data::q_windup_pct / std::min(attack_speed, spell_data::q_as_cap);
        if (overcharged) qct *= spell_data::q_overcharged_windup_mult;
        c_q_cast_time = qct;
        c_w_cast_time = std::max(spell_data::w_cast_time_max - spell_data::w_cast_time_scale * bonus_as,
                                 spell_data::w_cast_time_min);

        s_heroes = {};
        const auto pos = p->get_position();
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead() || !h->is_targetable()) continue;
            if (!h->is_visible()) continue;
            if (h->get_team_id() == p->get_team_id()) continue;
            const float d = pos.distance(h->get_position());
            if (d <= spell_data::r_radius + 25.f) ++s_heroes.in_r;
            if (d <= 1200.f) ++s_heroes.in_1200;
            if (d <= 1500.f) ++s_heroes.in_1500;
            if (d < s_heroes.closest_d) { s_heroes.closest_d = d; s_heroes.closest = h; }
        }
    }

    static float get_q_range()      { return c_q_range; }
    static float get_q_speed()      { return c_q_speed; }
    static float get_q_cast_time()  { return c_q_cast_time; }
    static float get_w_cast_time()  { return c_w_cast_time; }

    static bool q_will_kill(game_object* target)
    {
        if (!target || !sdk::damage) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float dmg = sdk::damage->get_spell_damage(p, target, SLOT_Q);
        const float ehp = target->get_hp() + target->get_all_shield();
        return dmg >= ehp;
    }

    // ------------------------------------------------------------------------
    // Passive empowered shot damage (Zeri's enhanced auto from the passive
    // charge buff). Constants ported from zerisource's reversed table.
    // ------------------------------------------------------------------------
    static float q_passive_dmg(game_object* target, bool charged)
    {
        if (!target) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const int level = std::clamp((int)p->get_level(), 1, 18);
        float raw = 0.f;
        if (charged) {
            const float base = 75.f + (float(level - 1) / 17.f) * 95.f;
            const float hp_pct = 0.01f + (float(level - 1) / 17.f) * 0.1118f;
            float hp_part = target->get_max_hp() * hp_pct;
            if (target->is_monster()) hp_part = std::min(300.f, hp_part);
            raw = base + p->get_ability_power() * 1.1f + hp_part;
        } else {
            raw = 10.f + (float(level - 1) / 17.f) * 17.35f
                + p->get_ability_power() * 0.03f;
        }
        if (sdk::damage)
            raw = sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
        return std::max(raw, 0.f);
    }

    // ------------------------------------------------------------------------
    // Incoming damage tracker — project turret damage that will land on
    // `target` within `window` seconds. Used by smart R / kill estimates to
    // avoid burning a spell on something a turret is about to finish.
    // ------------------------------------------------------------------------
    static float incoming_turret_dmg(game_object* target, float window)
    {
        if (!target || window <= 0.f) return 0.f;
        if (!g_sdk || !g_sdk->object_manager) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;

        float total = 0.f;
        for (auto* turret : g_sdk->object_manager->get_turrets()) {
            if (!turret || !turret->is_valid() || turret->is_dead()) continue;
            if (turret->get_team_id() == p->get_team_id()) continue;
            if (turret->get_turret_aggro_target() != target) continue;
            const float travel = turret->get_position().distance(target->get_position()) / 1200.f;
            if (window < turret->get_attack_cast_delay() + travel) continue;
            const float minutes = g_sdk->clock_facade->get_game_time() / 60.f;
            const float base = (minutes <= 4.5f) ? 152.f : 152.f + (minutes - 4.5f) * 4.f;
            total += base * (100.f / (target->get_armor() + 100.f));
        }
        return total;
    }

    // Multi-shot Q + passive volley execute predictor. Catches kills the
    // single-shot `q_will_kill` misses: full empowered passive shot landing
    // alongside Q damage tips many minions / low-HP heroes into kill range.
    static bool q_volley_will_kill(game_object* target, bool force_charge)
    {
        if (!target || !sdk::damage) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const bool charged = passive_charged;
        const float shield = std::max(target->get_all_shield(), 0.f);
        const float hp     = std::max(target->get_hp() + shield, 1.f);

        if (force_charge && charged) return true;

        const float q_dmg       = sdk::damage->get_spell_damage(p, target, SLOT_Q);
        const float passive_dmg = q_passive_dmg(target, charged);
        const float turret_dmg  = target->is_hero()
            ? incoming_turret_dmg(target, 0.4f) : 0.f;
        return (q_dmg + passive_dmg + turret_dmg) + 0.5f >= hp;
    }

    // ------------------------------------------------------------------------
    // Enemy "close-range spell" awareness — caches each enemy hero's CC spell
    // ranges (refreshed every 2.5s) and answers "does target have a dangerous
    // engage spell ready right now?" for kite/aggro decisions.
    // ------------------------------------------------------------------------
    struct enemy_spell_threat { int slot; float range; };
    static std::unordered_map<uint32_t, std::vector<enemy_spell_threat>> s_enemy_threats;
    static float s_enemy_threats_refresh = 0.f;

    static void refresh_enemy_threats()
    {
        s_enemy_threats.clear();
        if (!sdk::spell_manager || !g_sdk || !g_sdk->object_manager) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || !h->is_hero()) continue;
            if (h->get_team_id() == p->get_team_id()) continue;
            std::vector<enemy_spell_threat> records;
            try {
                auto static_spells = sdk::spell_manager->get_spells_static_data(h);
                for (const auto& sd : static_spells) {
                    if (!sd.valid || !sd.is_cc || sd.slot < 0 || sd.slot >= 4) continue;
                    float r = sd.get_range();
                    if (r <= 0.f || r > 5000.f) r = 750.f;
                    bool dup = false;
                    for (auto& rec : records) if (rec.slot == sd.slot) { dup = true; break; }
                    if (!dup) records.push_back({ sd.slot, r });
                }
            } catch (...) {}
            if (!records.empty())
                s_enemy_threats[h->get_network_id()] = std::move(records);
        }
    }

    static bool enemy_has_ready_close_spell(game_object* target)
    {
        if (!target) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto it = s_enemy_threats.find(target->get_network_id());
        if (it == s_enemy_threats.end()) return false;
        const float dist = p->get_position().distance(target->get_position());
        for (const auto& rec : it->second) {
            auto sp = target->get_spell(rec.slot);
            if (!sp || sp->get_level() <= 0 || sp->get_cooldown() > 0.f) continue;
            const float mana_cost = target->get_spell_mana_cost(rec.slot);
            if (mana_cost > 0.f && mana_cost > target->get_par() + 1.f) continue;
            if (std::max(target->get_attack_range(), rec.range) + 125.f >= dist) return true;
        }
        return false;
    }

    // ============================================================================
    // PREDICTION 
    static bool q_collision_free()
    {
        if (overcharged || lightning_rounds) return true;
        if (!menu::q_minion_collision) return true;
        if (menu::q_collision_remove_level > 0) {
            auto p = g_sdk->object_manager->get_local_player();
            if (p && p->get_level() >= menu::q_collision_remove_level) return true;
        }
        return false;
    }

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
        if (!q_collision_free())
            spell.forbidden_collisions = { pred_sdk::collision_type::unit };

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    static pred_sdk::pred_data predict_w(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range_pulse;
        spell.radius             = spell_data::w_radius;
        spell.delay              = get_w_cast_time();
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = hitchance;
        spell.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    
    static bool find_open_wall_window(const math::vector3& start,
                                      const math::vector3& dir,
                                      float max_distance,
                                      float& open_start,
                                      float& open_end)
    {
        auto* nav = g_sdk ? g_sdk->nav_mesh : nullptr;
        if (!nav) return false;

        bool found_start = false;
        int  open_count = 0, closed_count = 0;
        const float step = 4.f;
        for (float d = 0.f; d <= max_distance; d += step) {
            math::vector3 pt = {
                start.x + dir.x * d,
                start.y,
                start.z + dir.z * d
            };
            pt.y = nav->get_terrain_height(pt.x, pt.z);
            const bool blocked = !nav->is_pathable(pt);
            if (blocked) {
                ++closed_count;
                open_count = 0;
                if (found_start && closed_count >= 2) {
                    open_end = std::max(d - static_cast<float>(closed_count) * step,
                                        open_start);
                    return open_end > open_start;
                }
            } else {
                ++open_count;
                closed_count = 0;
                if (!found_start && open_count >= 2) {
                    found_start = true;
                    open_start = std::max(d - static_cast<float>(open_count - 1) * step,
                                          0.f);
                }
            }
        }
        return false;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================

    static math::vector3 jitter_q_cast(const math::vector3& from, const math::vector3& cast_pos)
    {
        if (!menu::q_randomize) return cast_pos;
        const float dx = cast_pos.x - from.x;
        const float dz = cast_pos.z - from.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1.f) return cast_pos;
        const float nx = dx / len, nz = dz / len;
        const float px = -nz, pz = nx;

        const float max_off = std::max(menu::q_randomize_range, 0.f);
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_real_distribution<float> dist(-max_off, max_off);
        const float off = dist(rng);
        return { cast_pos.x + px * off, cast_pos.y, cast_pos.z + pz * off };
    }

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

        const auto cast = jitter_q_cast(p->get_position(), pred.cast_position);
        if (!sdk::common->cast_spell(SLOT_Q, cast)) return q_result::failed;
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
        if (pred.is_valid && pred.hitchance >= hitchance) {
            if (sdk::common->cast_spell(SLOT_W, pred.cast_position)) {
                last_w_cast_time = now;
                last_spell_cast_time = now;
                return true;
            }
            return false;
        }

        // Cross-wall fallback: prediction was rejected (likely yasuo_wall /
        // braum_wall flagged a blocker, or the path looks blocked). Re-run
        // without forbidden collisions and check if the W can thread through
        // an open wall-window between us and the target.
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !g_sdk->nav_mesh) return false;

        pred_sdk::spell_data raw{};
        raw.spell_type         = pred_sdk::spell_type::linear;
        raw.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        raw.spell_slot         = SLOT_W;
        raw.range              = spell_data::w_range_pulse;
        raw.radius             = spell_data::w_radius;
        raw.delay              = get_w_cast_time();
        raw.projectile_speed   = spell_data::w_speed;
        raw.expected_hitchance = hitchance;

        pred_sdk::pred_data raw_pred{};
        try { raw_pred = sdk::prediction->predict(target, raw); }
        catch (...) { return false; }
        if (!raw_pred.is_valid || raw_pred.hitchance < hitchance) return false;

        const auto from = p->get_position();
        const auto cast = raw_pred.cast_position;
        const float dx = cast.x - from.x, dz = cast.z - from.z;
        const float seg_len = std::sqrt(dx * dx + dz * dz);
        if (seg_len < 1.f) return false;
        const math::vector3 dir = { dx / seg_len, 0.f, dz / seg_len };

        float open_start = 0.f, open_end = 0.f;
        const bool window =
            find_open_wall_window(from, dir, seg_len, open_start, open_end) &&
            open_start > 5.f && open_start < seg_len;
        if (!window) return false;

        if (sdk::common->cast_spell(SLOT_W, cast)) {
            last_w_cast_time = now;
            last_spell_cast_time = now;
            return true;
        }
        return false;
    }

    // Traces the E dash direction through the navmesh to find the actual
    // landing point, accounting for wall vaults. Returns `start` (zero-length
    // vault) when there's no dashable wall in front so callers can detect
    // "no preview" via `pos.distance(landing)` being near zero.
    //
    // Algorithm ported from the binary-accurate wall-dash solver:
    //   1. Walk forward in 5u steps up to E_RANGE+20 to find the first
    //      unpathable cell (wall entry).
    //   2. Reject if the wall starts past the aim point + 18u (wall is past
    //      where the player is pointing).
    //   3. Walk past the entry in 5u steps up to +700u to find the first
    //      pathable cell (far edge).
    //   4. Land at far_edge + 6u, then nudge +2u up to 80u total until a
    //      pathable cell is hit (avoids landing on the wall corner).
    static math::vector3 compute_e_landing(const math::vector3& start, const math::vector3& ideal)
    {
        auto* nav = g_sdk ? g_sdk->nav_mesh : nullptr;
        if (!nav) return start;

        const float dx = ideal.x - start.x;
        const float dz = ideal.z - start.z;
        const float aim_dist = std::sqrt(dx * dx + dz * dz);
        if (aim_dist < 8.f) return start;
        const float nx = dx / aim_dist, nz = dz / aim_dist;

        auto probe = [&](float t) -> math::vector3 {
            math::vector3 q = { start.x + nx * t, start.y, start.z + nz * t };
            q.y = nav->get_terrain_height(q.x, q.z);
            return q;
        };

        // 1. Find wall entry — first blocked probe in front of player
        float entry = -1.f;
        const float entry_cap = spell_data::e_range + 20.f;
        for (float t = 8.f; t <= entry_cap; t += 5.f) {
            math::vector3 pt = probe(t);
            if (!nav->is_pathable(pt)) { entry = t; break; }
        }
        if (entry < 0.f) return start;            // no wall in dash path
        if (entry > aim_dist + 18.f) return start; // wall starts past the aim

        // 2. Find far edge — first pathable probe past entry
        float exit_t = -1.f;
        for (float t = entry + 5.f; t <= entry + 700.f; t += 5.f) {
            math::vector3 pt = probe(t);
            if (nav->is_pathable(pt)) { exit_t = t; break; }
        }
        if (exit_t < 0.f) return start; // wall too thick

        // 3. Land just past the far edge, nudge forward onto clean ground
        float land = exit_t + 6.f;
        math::vector3 landing = probe(land);
        for (int i = 0; i < 40 && !nav->is_pathable(landing); ++i) {
            land += 2.f;
            landing = probe(land);
        }
        return landing;
    }

    static bool try_e_gap_close(game_object* target)
    {
        if (!menu::combo_use_e || !target) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (!sdk::common) return false;

        const float dist = distance_to(target);
        if (dist <= get_q_range()) return false;

        // Don't dash into too many enemies (cached snapshot)
        if (s_heroes.in_1200 > menu::combo_e_max_near) return false;

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

        auto* spot = find_nearest_wall_jump(pos, menu::flee_wall_jump_max_dist);
        if (!spot) return false;

        const float d_start = pos.distance(spot->start);
        const float d_end   = pos.distance(spot->end);

        // near_pt = whichever endpoint the player is closest to (the entry side).
        // Only trigger the jump if the NEAR endpoint (wall entry) is within
        // flee_wall_jump_max_dist, not the far landing point.
        const bool near_is_start = d_start < d_end;
        const float d_near = near_is_start ? d_start : d_end;
        if (d_near > menu::flee_wall_jump_max_dist) return false;

        const auto& far_pt = near_is_start ? spot->end : spot->start;
        if (!is_in_flee_direction(far_pt)) return false;

        // Normalize direction toward far_pt and cap cast position to E's
        // max range (300u). Some wall-jump endpoints are 2000-3000u away,
        // and passing a raw far_pt that out-of-range makes the server short
        // the dash to half distance on certain spots.
        const float dx = far_pt.x - pos.x;
        const float dz = far_pt.z - pos.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        math::vector3 cast_pos = far_pt;
        if (len > 1.f) {
            const float nx = dx / len, nz = dz / len;
            cast_pos = { pos.x + nx * spell_data::e_range,
                         pos.y,
                         pos.z + nz * spell_data::e_range };
        }
        bool ok = sdk::common->cast_spell(SLOT_E, cast_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r(int min_enemies)
    {
        if (overcharged) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (!sdk::common) return false;
        if (s_heroes.in_r < min_enemies) return false;

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

        if (s_heroes.in_r >= menu::combo_r_min && s_heroes.in_1500 >= 2)
            return try_r(s_heroes.in_r);

        // Execute fallback: scan ALL enemies inside R radius and fire if any
        // would die to R. The previous version only checked the closest enemy,
        // which would miss a lethal target if a full-HP enemy happened to be
        // closer.
        if (s_heroes.in_r >= 1 && sdk::damage) {
            const auto pos = p->get_position();
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                if (pos.distance(h->get_position()) > spell_data::r_radius + 25.f) continue;
                if (venplus::utility::has_anti_burst_buff(h)) continue;
                const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                const float ehp = h->get_hp() + h->get_all_shield();
                if (dmg >= ehp) return try_r(1);
            }
        }
        return false;
    }

    // ============================================================================
    // Q CHAIN BOUNCE during R (Hypercharged)
    // ============================================================================
    // When R is active Q bounces off minions onto nearby heroes. Find the
    // minion/monster in Q range that sits next to the LOWEST-HP enemy hero
    // (tie-break: candidate closest to us) and Q it — the bounce carries the
    // damage to the hero even though they're outside direct Q range.
    static bool try_q_chain_bounce_r()
    {
        if (!menu::combo_q_chain_r) return false;
        if (!overcharged) return false;
        if (!is_spell_ready(SLOT_Q)) return false;
        if (!sdk::common) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto pos     = p->get_position();
        const float q_rng  = get_q_range();

        game_object* best        = nullptr;
        float        best_hero_hp = FLT_MAX;
        float        best_dist    = FLT_MAX;

        auto consider = [&](game_object* cand) {
            if (!cand || !cand->is_valid() || cand->is_dead() || !cand->is_targetable()) return;
            if (!cand->is_visible() || cand->get_team_id() == p->get_team_id()) return;
            const float d_me = pos.distance(cand->get_position());
            if (d_me > q_rng) return;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                const float bounce_range = h->get_bounding_radius() + 650.f;
                if (cand->get_position().distance(h->get_position()) > bounce_range) continue;
                const float hp = h->get_hp();
                if (hp < best_hero_hp || (hp == best_hero_hp && d_me < best_dist)) {
                    best        = cand;
                    best_hero_hp = hp;
                    best_dist    = d_me;
                }
            }
        };

        for (auto* m : g_sdk->object_manager->get_minions())  consider(m);
        for (auto* m : g_sdk->object_manager->get_monsters()) consider(m);

        if (!best) return false;

        const auto cast = pos.extended(best->get_position(), q_rng, true);
        if (!sdk::common->cast_spell(SLOT_Q, cast)) return false;
        last_q_cast_time = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = last_q_cast_time;
        return true;
    }

    // ============================================================================
    // E WALL AUTO-W — fire W mid-dash while crossing through a wall
    // ============================================================================
    static bool try_e_wall_auto_w()
    {
        if (!menu::e_wall_auto_w) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (!p->is_dashing()) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (!sdk::prediction || !sdk::common || !g_sdk->nav_mesh) return false;

        // Only fire if our current cell is unpathable (i.e. we're inside the wall).
        // is_pathable takes a non-const reference, so we need an lvalue.
        math::vector3 here = p->get_position();
        if (g_sdk->nav_mesh->is_pathable(here)) return false;

        // Closest hero within W range
        game_object* best = nullptr;
        float best_d = spell_data::w_range_pulse + 100.f;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float d = distance_to(h);
            if (d < best_d) { best_d = d; best = h; }
        }
        if (!best) return false;

        // Fire with a low hitchance — the goal is opportunistic damage during
        // a window the player is committed to anyway.
        return try_w(best, menu::e_wall_auto_w_hc);
    }

    // ============================================================================
    // KILLSTEAL — picks the lowest-EHP killable hero so we don't waste a cast
    // on a 90% HP target while a 5% HP target was also in range.
    // ============================================================================
    static void killsteal_w()
    {
        if (!menu::combo_w_killsteal) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::damage) return;

        game_object* best = nullptr;
        float best_ehp = FLT_MAX;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (venplus::utility::has_anti_burst_buff(h)) continue;
            if (distance_to(h) > spell_data::w_range_pulse) continue;
            const float ehp = h->get_hp() + h->get_all_shield();
            if (ehp >= best_ehp) continue;
            const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_W);
            if (dmg >= ehp) { best_ehp = ehp; best = h; }
        }
        if (best) try_w(best, menu::w_hitchance);
    }

    // ============================================================================
    // AA SUBSTITUTION (on_before_attack)
    // ============================================================================
    // True when the basic-attack itself should be allowed to fire on a hero.
    // Q is fired first by on_before_attack; this controls the remaining AA.
    static bool allow_normal_aa(game_object* target)
    {
        if (!target) return false;
        const bool execute = q_will_kill(target);

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
        const bool in_combo = sdk::orbwalker->combo();
        const bool q_allowed = is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q)
            && (!in_combo || menu::combo_use_q);

        // Try Q first if ready; if cast succeeded, suppress the AA
        if (q_allowed) {
            if (try_q(target, pred_sdk::hitchance::low) == q_result::casted)
                return false;
        }

        // ---- Smart pre-attack gates per target type --------------------
        if (target->is_minion()) {
            // Jungle / non-lane: only attack large/epic monsters with the
            // empowered shot (don't waste the passive on a krug).
            if (!target->is_lane_minion() && target->is_monster()) {
                const bool big = target->is_large_monster() || target->is_epic_monster();
                if (big) return q_volley_will_kill(target, false);
                return true; // small monsters: always AA (no charge to waste)
            }
            // Lane minion: take the AA when a volley would kill (or when not charged).
            return q_volley_will_kill(target, in_combo);
        }
        if (target->is_turret()) {
            return passive_charged;
        }
        if (target->is_inhibitor() || target->is_nexus()) {
            return passive_charged;
        }
        if (target->is_ward() || target->is_trap() || target->is_plant()) {
            return passive_charged;
        }
        if (target->is_hero()) {
            // Defensive: back off heroes with a ready close-range engage spell.
            if (in_combo && enemy_has_ready_close_spell(target)) {
                const float aa_range = c_aa_range;
                if (distance_to(target) <= aa_range - 50.f) return false;
            }
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

        const float dist     = distance_to(target);
        const float q_range  = get_q_range();
        const float aa_range = c_aa_range;

        // R inside its radius
        if (menu::combo_use_r && !overcharged && dist <= spell_data::r_radius + 125.f) {
            if (menu::combo_r_mode == R_MODE_SMART) try_r_smart();
            else                                     try_r(menu::combo_r_min);
        }

        // Q chain bounce: during R, prefer Q'ing a minion next to the
        // lowest-HP enemy hero — the bounce often executes faster than
        // a direct Q on the closest hero.
        if (overcharged && try_q_chain_bounce_r()) return;

        // E gap close just outside Q range
        if (menu::combo_use_e && dist > q_range
            && dist <= q_range + spell_data::e_range + 100.f) {
            try_e_gap_close(target);
        }

        // W (long-range poke)
        if (menu::combo_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
            try_w(target, menu::w_hitchance);
        }

        const bool would_cancel = sdk::orbwalker && sdk::orbwalker->would_cancel_attack();
        if (menu::combo_use_q && dist > aa_range && dist <= q_range
            && is_spell_ready(SLOT_Q) && !would_cancel) {
            try_q(target, pred_sdk::hitchance::low);
        }
    }

    void harass()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        const float q_range     = get_q_range();
        const float aa_range    = c_aa_range;
        const float w_max_range = spell_data::w_range_pulse + 100.f;
        const bool  would_cancel = sdk::orbwalker && sdk::orbwalker->would_cancel_attack();
        // Q is free — let it fire regardless of mana threshold. W/E/R obey it.
        const bool  mana_ok      = mana_pct() >= (float)menu::harass_mana_pct;

        auto* hero = sdk::target_selector ? pick_hero_target(w_max_range) : nullptr;

        if (menu::harass_use_q && hero && is_spell_ready(SLOT_Q) && !would_cancel) {
            const float dh = distance_to(hero);
            if (dh > aa_range && dh <= q_range) {
                if (try_q(hero, pred_sdk::hitchance::low) == q_result::casted) return;
            }
        }

        if (mana_ok && hero && menu::harass_use_w
            && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
            const int hc = std::max((int)pred_sdk::hitchance::very_high, menu::w_hitchance);
            if (try_w(hero, hc)) return;
        }

        if (menu::harass_use_q && is_spell_ready(SLOT_Q) && sdk::damage && !would_cancel) {
            game_object* best = nullptr;
            float best_hp = FLT_MAX;
            for (auto* m : g_sdk->object_manager->get_minions()) {
                if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
                if (!m->is_visible() || m->get_team_id() == p->get_team_id()) continue;
                if (distance_to(m) > q_range) continue;
                const float mhp = m->get_hp();
                if (sdk::damage->get_spell_damage(p, m, SLOT_Q) >= mhp && mhp < best_hp) {
                    best_hp = mhp;
                    best    = m;
                }
            }
            if (best) try_q(best, pred_sdk::hitchance::low);
        }
    }

    static bool cast_q_at_pos(const math::vector3& pos)
    {
        if (!is_spell_ready(SLOT_Q) || !sdk::common) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto from = p->get_position();
        const float qr = get_q_range();
        const float d  = from.distance(pos);
        if (d > qr + 25.f) return false;

        const auto cast = (d > qr) ? from.extended(pos, qr, true) : pos;
        if (!sdk::common->cast_spell(SLOT_Q, cast)) return false;
        last_q_cast_time     = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = last_q_cast_time;
        return true;
    }

    // Q 
    static bool try_q_structure()
    {
        if (!is_spell_ready(SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float qr = get_q_range();

        auto try_list = [&](std::span<game_object*> objs) -> bool {
            for (auto* o : objs) {
                if (!is_valid_target(o, qr)) continue;
                if (cast_q_at_pos(o->get_position())) return true;
            }
            return false;
        };
        if (try_list(g_sdk->object_manager->get_turrets()))    return true;
        if (try_list(g_sdk->object_manager->get_inhibitors())) return true;
        if (try_list(g_sdk->object_manager->get_nexuses()))    return true;
        return false;
    }

    static bool try_q_jungle()
    {
        if (!is_spell_ready(SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float qr = get_q_range();

        game_object* best       = nullptr;
        float        best_mhp   = 0.f;
        for (auto* m : g_sdk->object_manager->get_monsters()) {
            if (!is_valid_target(m, qr)) continue;
            const float mhp = m->get_max_hp();
            if (mhp > best_mhp) { best_mhp = mhp; best = m; }
        }
        if (!best) return false;
        return cast_q_at_pos(best->get_position());
    }

    // Q the closest enemy ward in Q range.
    static bool try_q_wards()
    {
        if (!is_spell_ready(SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float qr = get_q_range();

        game_object* nearest = nullptr;
        float        best_d  = FLT_MAX;
        for (auto* w : g_sdk->object_manager->get_wards()) {
            if (!is_valid_target(w, qr)) continue;
            const float d = distance_to(w);
            if (d < best_d) { best_d = d; nearest = w; }
        }
        if (!nearest) return false;
        return cast_q_at_pos(nearest->get_position());
    }

    static bool try_q_traps_pets()
    {
        if (!is_spell_ready(SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float qr = get_q_range();

        game_object* nearest = nullptr;
        float        best_d  = FLT_MAX;

        auto consider = [&](game_object* o) {
            const float d = distance_to(o);
            if (d < best_d) { best_d = d; nearest = o; }
        };

        for (auto* t : g_sdk->object_manager->get_traps()) {
            if (is_valid_target(t, qr)) consider(t);
        }
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_target(m, qr)) continue;
            if (m->is_lane_minion()) continue;
            auto* owner = m->get_owner();
            if (!owner || !owner->is_hero()) continue;
            consider(m);
        }

        if (!nearest) return false;
        return cast_q_at_pos(nearest->get_position());
    }

    //Lightning Rounds
    static bool try_q_empowered_clear()
    {
        if (!is_spell_ready(SLOT_Q)) return false;
        if (!lightning_rounds && !overcharged) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float qr = get_q_range();

        game_object* best = nullptr;
        float best_d = FLT_MAX;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_target(m, qr)) continue;
            if (!m->is_lane_minion()) continue;
            const float d = distance_to(m);
            if (d < best_d) { best_d = d; best = m; }
        }
        if (!best) return false;
        return cast_q_at_pos(best->get_position());
    }

    // Q farm on lane minions. `lasthit_only=true` restricts to minions Q
    // can kill in one shot; otherwise we also fall through to the closest
    // minion outside AA range so clear-mode Q actually pushes the wave.
    //
    // Always uses cast_q_at_pos (raw cast) instead of try_q→predict_q so
    // unit-collision rejection can't block farming through a stacked wave.
    // For farm we *want* the projectile to hit whatever's first — that's
    // still a minion getting damaged.
    static void try_q_minion_farm(bool lasthit_only)
    {
        if (!is_spell_ready(SLOT_Q)) return;
        if (!sdk::damage || !sdk::orbwalker) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        const float q_range  = get_q_range();
        const float aa_range = c_aa_range;

        game_object* lethal      = nullptr;
        float        lethal_hp   = FLT_MAX;
        game_object* nearest     = nullptr;
        float        nearest_d   = FLT_MAX;

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
            if (!m->is_visible() || m->get_team_id() == p->get_team_id()) continue;
            if (!m->is_lane_minion()) continue;
            const float d = distance_to(m);
            if (d <= aa_range || d > q_range) continue;
            const float mhp = m->get_hp();
            const float dmg = sdk::damage->get_spell_damage(p, m, SLOT_Q);
            if (dmg >= mhp && mhp < lethal_hp) { lethal_hp = mhp; lethal = m; }
            if (d < nearest_d) { nearest_d = d; nearest = m; }
        }

        game_object* pick = lethal ? lethal
                                   : (lasthit_only ? nullptr : nearest);
        if (pick) cast_q_at_pos(pick->get_position());
    }

    static void try_q_lasthit_farm() { try_q_minion_farm(true);  }
    static void try_q_clear_farm()   { try_q_minion_farm(false); }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::lane_mana_pct) return;

        if (try_q_structure()) return;
        if (try_q_empowered_clear()) return;
        if (try_q_jungle()) return;

        // Q clear: any minion in the AA→Q gap, push the wave
        if (menu::lane_use_q_clear) try_q_clear_farm();

        if (try_q_wards())     return;
        if (try_q_traps_pets()) return;

        if (!menu::lane_use_w) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

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

        // Extended Q farm targets — same priority order as clear()
        if (try_q_structure()) return;
        if (try_q_empowered_clear()) return;
        if (try_q_jungle()) return;

        // Q clear: any minion in the AA→Q gap, push the wave
        if (menu::lane_use_q_clear) try_q_clear_farm();

        if (try_q_wards())     return;
        if (try_q_traps_pets()) return;

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
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;

        // Q last-hit beyond AA range
        if (menu::lane_use_q_lh) try_q_lasthit_farm();

        // W cannon save — gated by its own toggle
        if (!menu::lh_w_cannon) return;
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
    static bool q_cast_is_locking(game_object* p, float now)
    {
        if (!p) return false;
        if (auto* a = p->get_active_spell_cast()) {
            if (auto* sc = a->get_spell_cast()) {
                if (sc->get_spell_slot() == SLOT_Q && a->get_cast_end_time() > now)
                    return true;
            }
        }
        const float lock = std::max(p->get_attack_cast_delay(), 0.1f);
        return last_q_cast_time + lock > now;
    }

    static void try_queued_e_update()
    {
        if (!s_queued_e_active) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) { s_queued_e_active = false; return; }

        const float now = g_sdk->clock_facade->get_game_time();
        if (now > s_queued_e_expire || p->is_dead()) {
            s_queued_e_active = false;
            return;
        }
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;
        if (q_cast_is_locking(p, now)) return;
        if (!sdk::common) { s_queued_e_active = false; return; }

        const auto pos = p->get_position();
        math::vector3 cast = s_queued_e_pos;
        const float dx = cast.x - pos.x, dz = cast.z - pos.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len > spell_data::e_range && len > 1.f) {
            const float nx = dx / len, nz = dz / len;
            cast = { pos.x + nx * spell_data::e_range, pos.y,
                     pos.z + nz * spell_data::e_range };
        }
        if (sdk::common->cast_spell(SLOT_E, cast)) {
            last_spell_cast_time = now;
            s_queued_e_active = false;
        }
    }

    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        refresh_state();
        update_jump_spot();

        // Refresh enemy spell-threat cache every 2.5s (get_spells_static_data
        // is documented as init-only; running it every tick is wasteful).
        {
            const float now_t = g_sdk->clock_facade->get_game_time();
            if (now_t - s_enemy_threats_refresh > 2.5f) {
                refresh_enemy_threats();
                s_enemy_threats_refresh = now_t;
            }
        }

        try_queued_e_update();
        try_e_wall_auto_w();

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
        if (is_spell_ready(SLOT_R)) dmg += sdk::damage->get_spell_damage(p, target, SLOT_R);
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

        if (menu::draw_e_preview && is_spell_learned(SLOT_E)
            && g_sdk->renderer && g_sdk->hud_manager && g_sdk->nav_mesh) {
            const auto cursor = g_sdk->hud_manager->get_cursor_position();
            const float cdx = cursor.x - pos.x, cdz = cursor.z - pos.z;
            const float clen = std::sqrt(cdx * cdx + cdz * cdz);
            if (clen > 1.f) {
                const float nx = cdx / clen, nz = cdz / clen;

                bool wall_in_path = false;
                for (float d = 18.f; d <= 130.f; d += 18.f) {
                    math::vector3 pt = { pos.x + nx * d, pos.y, pos.z + nz * d };
                    if (!g_sdk->nav_mesh->is_pathable(pt)) { wall_in_path = true; break; }
                }

                if (wall_in_path) {
                    const math::vector3 dash_end = {
                        pos.x + nx * spell_data::e_range,
                        pos.y,
                        pos.z + nz * spell_data::e_range
                    };
                    const auto landing = compute_e_landing(pos, dash_end);

                    if (pos.distance(landing) > spell_data::e_range + 50.f) {
                        const uint32_t clr = menu::color_e_preview;
                        draw_circle(landing, 55.f, clr);

                        const auto s_start = g_sdk->renderer->world_to_screen(pos);
                        const auto s_end   = g_sdk->renderer->world_to_screen(landing);
                        if (s_start.x > 1.f && s_end.x > 1.f)
                            g_sdk->renderer->add_line_2d(s_start, s_end, 2.f, (clr & 0x00FFFFFFu) | 0xCC000000u);
                    }
                }
            }
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
            g_sdk->renderer->add_text(aa_label, 14.f, { screen.x, screen.y + 35.f }, 1, aa_clr);

            if (overcharged) {
                g_sdk->renderer->add_text("OVERCHARGED", 14.f,
                    { screen.x, screen.y + 55.f }, 1, 0xFFFFFF00u);
            } else if (passive_charged) {
                g_sdk->renderer->add_text("PASSIVE READY", 14.f,
                    { screen.x, screen.y + 55.f }, 1, 0xFF00FFA0u);
            }
            if (lightning_rounds) {
                g_sdk->renderer->add_text("LIGHTNING ROUNDS", 13.f,
                    { screen.x, screen.y + 75.f }, 1, 0xFFFFB400u);
            }
            if (hypercharged_stk > 0) {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "R STACKS: %d", hypercharged_stk);
                g_sdk->renderer->add_text(buf, 13.f,
                    { screen.x, screen.y + 95.f }, 1, 0xFF50FF78u);
            }
        }

        if (menu::draw_r_dmg_bar && sdk::damage && g_sdk->renderer && is_spell_learned(SLOT_R)) {
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h, FLT_MAX)) continue;
                const float r_dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                const float hp = h->get_hp(), mhp = h->get_max_hp();
                if (r_dmg <= 0.f || mhp <= 0.f || hp <= 0.f) continue;
                const auto hbar = h->get_health_bar_position();
                if (hbar.x < 1.f && hbar.y < 1.f) continue;
                const float r0 = hp / mhp;
                const float r1 = std::max(0.f, hp - r_dmg) / mhp;
                if (r0 <= r1) continue;
                math::rect rc;
                rc.left   = hbar.x + ox + r1 * bw;
                rc.top    = hbar.y + oy;
                rc.right  = hbar.x + ox + r0 * bw;
                rc.bottom = hbar.y + oy + bh;
                g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
            }
        }
    }

    bool __fastcall on_cast_spell(game_object* obj, int spell_slot,
                                  math::vector3 position, game_object* /*target*/)
    {
        auto p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
        if (!p || !obj) return true;
        if (obj->get_network_id() != p->get_network_id()) return true;

        if (spell_slot == SLOT_E) {
            if (!menu::queue_manual_e) return true;
            if (!is_spell_ready(SLOT_E)) return true;
            const float now = g_sdk->clock_facade->get_game_time();
            if (!q_cast_is_locking(p, now)) return true;
            s_queued_e_active = true;
            s_queued_e_pos    = position;
            s_queued_e_expire = now + 0.6f;
            return false;
        }

        if (spell_slot != SLOT_R) return true;
        if (!menu::r_block_enabled) return true;
        if (overcharged) return true;

        const int needed = 1;
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
        q_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_q,
            [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q,
            [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear", "Use in Clear", menu::lane_use_q_clear,
            [](bool v) { menu::lane_use_q_clear = v; });
        q_menu->add_checkbox("lh", "Use in Lasthit", menu::lane_use_q_lh,
            [](bool v) { menu::lane_use_q_lh = v; });
        q_menu->add_checkbox("minion_col", "Collision", menu::q_minion_collision,
            [](bool v) { menu::q_minion_collision = v; });
        q_menu->add_slider_int("col_remove_lvl", "Remove collision at Level", 0, 18, 1,
            menu::q_collision_remove_level, [](int v) { menu::q_collision_remove_level = v; });
        //q_menu->add_checkbox("randomize", "Humanize Q", menu::q_randomize,
            //[](bool v) { menu::q_randomize = v; });
        //q_menu->add_slider_float("randomize_range", "Range", 5.f, 40.f, 1.f,
            //menu::q_randomize_range, [](float v) { menu::q_randomize_range = v; });
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
        e_menu->add_checkbox("wall_auto_w", "Auto W", menu::e_wall_auto_w,
            [](bool v) { menu::e_wall_auto_w = v; });
        e_menu->add_slider_int("wall_auto_w_hc", "  W Hitchance", 0, 100, 5,
            menu::e_wall_auto_w_hc, [](int v) { menu::e_wall_auto_w_hc = v; });
        //e_menu->add_checkbox("queue_e", "Queue Manual E During Q", menu::queue_manual_e,
            //[](bool v) { menu::queue_manual_e = v; });
        e_menu->add_hotkey("wall_jump", "Wall Jump", 'N', false, false,
            [](std::string* hk, bool pressed) {
                if (pressed) {
                    // Toggle: first press activates, second cancels
                    menu::jump_key_active = !menu::jump_key_active;
                }
                if (hk) menu::jump_key_str = *hk;
            });
        e_menu->add_separator();
        e_menu->add_checkbox("e_preview", "E Landing Preview", menu::draw_e_preview,
            [](bool v) { menu::draw_e_preview = v; });
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // R — Lightning Crash
        auto r_menu = s_menu->add_sub_category("r_lightning_crash", "R - Lightning Crash");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_checkbox("block", "Block Manual R",
            menu::r_block_enabled, [](bool v) { menu::r_block_enabled = v; });
        r_menu->add_checkbox("q_chain", "Q Chain", menu::combo_q_chain_r,
            [](bool v) { menu::combo_q_chain_r = v; });
        r_menu->add_combo("mode", "R Mode", { "Always", "Smart" }, menu::combo_r_mode,
            [](int v) { menu::combo_r_mode = v; });
        r_menu->add_slider_int("min", "Min Enemies in R", 1, 5, 1, menu::combo_r_min,
            [](int v) { menu::combo_r_min = v; });
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
        draw_menu->add_checkbox("status", "Status Test", menu::draw_status_text,
            [](bool v) { menu::draw_status_text = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator", menu::draw_r_dmg_bar,
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
        draw_menu->add_colorpicker("col_e_preview", "E Preview Color", menu::color_e_preview,
            [](uint32_t c) { menu::color_e_preview = c; });
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
        g_sdk->event_manager->register_callback(event_manager::event::cast_spell,
            reinterpret_cast<void*>(on_cast_spell));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        if (sdk::infotab) {
            menu::infotab_semi_w = sdk::infotab->add_hotkey_text(
                &menu::semi_w_key_str,
                { "Semi W", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_w_active ? 0xFF00FF00u : 0xFFFF4040u;
                    return { menu::semi_w_active ? "ON" : "OFF", clr };
                });

            menu::infotab_wall_jump = sdk::infotab->add_hotkey_text(
                &menu::jump_key_str,
                { "Wall Jump", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::jump_key_active ? 0xFF00FF00u : 0xFFFF4040u;
                    return { menu::jump_key_active ? "ACTIVE" : "OFF", clr };
                });
        }

        g_sdk->log_console("[!] [MeowAIO] Zeri module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::cast_spell,
                reinterpret_cast<void*>(on_cast_spell));
        }

        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        if (sdk::infotab) {
            if (menu::infotab_semi_w != 0) {
                sdk::infotab->remove_text(menu::infotab_semi_w);
                menu::infotab_semi_w = 0;
            }
            if (menu::infotab_wall_jump != 0) {
                sdk::infotab->remove_text(menu::infotab_wall_jump);
                menu::infotab_wall_jump = 0;
            }
        }

        s_menu = nullptr;
        overcharged = lightning_rounds = passive_charged = false;
        hypercharged_stk = 0;
        s_heroes = {};
        release_jump_orb_block();
        s_jump_locked = false;
        s_jump_last_move = 0.f;
        menu::jump_key_active = false;
        last_q_cast_time = 0.f;
        last_w_cast_time = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_w_active = false;
        s_queued_e_active = false;
        s_queued_e_expire = 0.f;

        if (g_sdk) g_sdk->log_console("[!] [MeowAIO] Zeri module unloaded");
    }
}
