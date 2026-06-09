#include "ryze.h"

#include "damage/calc.hpp"
#include "db/champions/ryze.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace ryze
{
    namespace sc = venplus::db::champions::ryze_const;

    namespace menu
    {
        inline bool combo_use_q = true;
        inline bool combo_use_w = true;
        inline bool combo_use_e = true;
        inline bool combo_flux_q_collision = true;
        inline int  combo_q_hitchance = pred_sdk::hitchance::very_high;
        inline int  combo_mode = 0;

        inline bool harass_use_q = true;
        inline bool harass_use_w = false;
        inline bool harass_use_e = true;
        inline int  harass_mana_percent = 40;

        inline bool clear_use_q = true;
        inline bool clear_use_e = true;
        inline int  clear_q_min_hit = 3;
        inline int  clear_mana_percent = 30;

        inline bool lasthit_use_q = true;
        inline int  lasthit_mana_percent = 30;

        inline bool flee_use_ew = true;
        inline bool        semi_root_active = false;
        inline std::string semi_root_key = "G";
        inline uint32_t    infotab_semi_root = 0;
        inline uint32_t    infotab_combo_mode = 0;
        inline std::string combo_mode_key = "Y";

        inline bool draw_q_range = true;
        inline bool draw_w_range = true;
        inline bool draw_e_range = true;
        inline bool draw_r_range = false;
        inline bool draw_r_rgb = false;
        inline bool draw_r_minimap = false;
        inline bool draw_damage_bar = true;
        inline bool draw_killable_text = true;
        inline bool draw_flux = true;
        inline bool draw_combo_mode = true;
        inline bool draw_only_ready = false;
        inline uint32_t color_q = 0x80FFFFFFu;
        inline uint32_t color_w = 0x800088FFu;
        inline uint32_t color_e = 0x809966FFu;
        inline uint32_t color_r = 0x80AA66FFu;
        inline uint32_t color_damage_bar = 0x90AA66FFu;
    }

    namespace
    {
        struct flux_entry
        {
            game_object* target = nullptr;
            float expire_time = 0.f;
            bool active = false;
        };

        static menu_category* s_menu = nullptr;
        static std::array<flux_entry, 10> s_fluxed_targets{};
        static float s_last_spell_cast_time = 0.f;
        static int   s_rune_stacks = 0;
        static game_object* s_semi_root_target = nullptr;
        static float s_semi_root_expire_time = 0.f;
        static bool s_combo_mode_toggle_pressed = false;

        static constexpr float MIN_SPELL_DELAY = 0.05f;
        static constexpr float SEMI_ROOT_SPELL_DELAY = 0.015f;
        static constexpr float SEMI_ROOT_BUFFER_WINDOW = 0.35f;
        static constexpr uint32_t RYZE_FLUX_HASH = 0x86A30558u;

        static game_object* player()
        {
            return g_sdk && g_sdk->object_manager ? g_sdk->object_manager->get_local_player() : nullptr;
        }

        static float game_time()
        {
            return g_sdk && g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        }

        static float mana_pct()
        {
            auto* p = player();
            if (!p) return 0.f;
            const float max_mana = p->get_max_par();
            if (max_mana <= 0.f) return 0.f;
            return (p->get_par() / max_mana) * 100.f;
        }

        static bool is_spell_learned(int slot)
        {
            auto* p = player();
            if (!p) return false;
            auto* s = p->get_spell(slot);
            return s && s->get_level() > 0;
        }

        static bool is_spell_ready(int slot)
        {
            auto* p = player();
            if (!p) return false;
            auto* s = p->get_spell(slot);
            return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
        }

        static int spell_rank(int slot)
        {
            auto* p = player();
            if (!p) return 0;
            auto* s = p->get_spell(slot);
            return s ? s->get_level() : 0;
        }

        static bool has_enough_mana(int slot)
        {
            auto* p = player();
            if (!p) return false;
            return p->get_par() >= p->get_spell_mana_cost(slot);
        }

        static bool is_casting()
        {
            auto* p = player();
            if (!p) return false;

            auto* ac = p->get_active_spell_cast();
            if (!ac) return false;

            auto* scast = ac->get_spell_cast();
            if (!scast) return false;
            if (scast->is_basic_attack() || scast->is_special_attack()) return false;

            return ac->get_cast_end_time() > game_time();
        }

        static bool is_valid_target(game_object* unit, float range = FLT_MAX)
        {
            auto* p = player();
            if (!p || !unit) return false;
            if (!unit->is_valid() || unit->is_dead() || !unit->is_targetable() || !unit->is_visible()) return false;
            if (unit->get_team_id() == p->get_team_id()) return false;
            if (range != FLT_MAX && p->get_position().distance(unit->get_position()) > range) return false;
            return true;
        }

        static bool is_valid_enemy_minion(game_object* unit, float range = FLT_MAX)
        {
            auto* p = player();
            if (!p || !unit) return false;
            if (!unit->is_valid() || unit->is_dead() || !unit->is_targetable() || !unit->is_visible()) return false;
            if (unit->is_plant()) return false;
            if (unit->get_team_id() == p->get_team_id()) return false;
            if (range != FLT_MAX && p->get_position().distance(unit->get_position()) > range) return false;
            return true;
        }

        static game_object* pick_target(float range)
        {
            if (!sdk::target_selector) return nullptr;

            if (auto* forced = sdk::target_selector->get_forced_target())
            {
                if (is_valid_target(forced, range))
                    return forced;
            }

            for (auto* hero : sdk::target_selector->get_sorted_heroes())
            {
                if (is_valid_target(hero, range))
                    return hero;
            }

            return nullptr;
        }

        static int effective_q_hitchance(game_object* target, int hitchance)
        {
            auto* p = player();
            if (!p || !target || !target->is_hero())
                return hitchance;

            const float dist = p->get_position().distance(target->get_position());
            int effective = hitchance;

            if (dist > 850.f)
                effective = (std::max)(effective, static_cast<int>(pred_sdk::hitchance::very_high));
            else if (dist > 650.f)
                effective = (std::max)(effective, static_cast<int>(pred_sdk::hitchance::high));

            if (dist > 550.f && target->get_move_speed() > 420.f)
                effective = (std::max)(effective, static_cast<int>(pred_sdk::hitchance::very_high));

            return effective;
        }

        static bool is_unstable_q_prediction(game_object* target, const pred_sdk::pred_data& pred)
        {
            auto* p = player();
            if (!p || !target || !target->is_hero())
                return false;

            const float dist = p->get_position().distance(target->get_position());
            if (dist <= 600.f)
                return false;

            return pred.state == pred_sdk::prediction_state::juking
                || pred.state == pred_sdk::prediction_state::awaiting_netvar_update;
        }

        // Geometric post-validation of a Q prediction result.
        //
        // Checks four things from first principles:
        //   1. Perpendicular distance from the missile line to the target's predicted
        //      center must fit within the effective hit radius (Q_RADIUS + target hitbox).
        //   2. Edge-to-edge range: target center must be within Q_RANGE + target_radius.
        //   3. Precise total time: Q windup delay + flight time until the missile edge
        //      first contacts the target edge.
        //   4. Reachability: the distance the target would need to travel from its
        //      current position to the predicted position must not exceed what it can
        //      cover at its current move-speed within that total time (+ 15% buffer).
        static bool verify_q_geometry(game_object* target, const pred_sdk::pred_data& pred)
        {
            auto* p = player();
            if (!p || !target || !pred.is_valid) return false;

            const float target_radius = target->get_bounding_radius();
            const float eff_radius    = sc::Q_RADIUS + target_radius;

            const auto player_pos = p->get_position();
            const auto cast_pos   = pred.cast_position;
            const auto pred_pos   = pred.predicted_position;

            // Cast direction unit vector (x/z plane, matching distance())
            const float dir_x  = cast_pos.x - player_pos.x;
            const float dir_z  = cast_pos.z - player_pos.z;
            const float dir_len = std::sqrt(dir_x * dir_x + dir_z * dir_z);
            if (dir_len < 1.f) return false;

            const float nx = dir_x / dir_len;
            const float nz = dir_z / dir_len;

            // Scalar projection of predicted target center onto missile direction
            const float dpx  = pred_pos.x - player_pos.x;
            const float dpz  = pred_pos.z - player_pos.z;
            const float proj = dpx * nx + dpz * nz;
            if (proj <= 0.f) return false;

            // Perpendicular distance from predicted center to missile line
            const float perp_x    = dpx - proj * nx;
            const float perp_z    = dpz - proj * nz;
            const float perp_dist = std::sqrt(perp_x * perp_x + perp_z * perp_z);
            if (perp_dist > eff_radius) return false;

            // Edge-to-edge range check
            const float dist_center = player_pos.distance(pred_pos);
            if (dist_center > sc::Q_RANGE + target_radius) return false;

            // Precise forward distance at first missile-edge / target-edge contact:
            //   proj - sqrt(eff_radius^2 - perp_dist^2)
            const float inside_sq  = (std::max)(0.f, eff_radius * eff_radius - perp_dist * perp_dist);
            const float hit_proj   = (std::max)(0.f, proj - std::sqrt(inside_sq));

            // Total time = windup delay + flight
            const float flight_time = hit_proj / sc::Q_SPEED;
            const float total_time  = sc::Q_DELAY + flight_time;

            // Reachability: target must be able to reach pred_pos within total_time
            const float dist_to_pred = target->get_position().distance(pred_pos);
            const float max_travel   = target->get_move_speed() * total_time * 1.15f;
            if (dist_to_pred > max_travel) return false;

            return true;
        }

        static pred_sdk::pred_data predict_q(game_object* target, int hitchance)
        {
            pred_sdk::spell_data spell{};
            spell.spell_type = pred_sdk::spell_type::linear;
            spell.targetting_type = pred_sdk::targetting_type::edge_to_edge;
            spell.spell_slot = SLOT_Q;
            spell.range = sc::Q_RANGE;
            spell.radius = sc::Q_RADIUS;
            spell.delay = sc::Q_DELAY;
            spell.projectile_speed = sc::Q_SPEED;
            spell.expected_hitchance = hitchance;
            spell.forbidden_collisions = {
                pred_sdk::collision_type::unit,
                pred_sdk::collision_type::terrain
            };

            pred_sdk::pred_data pred{};
            try { pred = sdk::prediction->predict(target, spell); }
            catch (...) { pred.is_valid = false; }
            return pred;
        }

        static bool cast_q(game_object* target, int hitchance)
        {
            if (!sdk::common || !sdk::prediction || !target) return false;
            if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

            const int effective_hc = effective_q_hitchance(target, hitchance);
            const auto pred = predict_q(target, effective_hc);
            if (!pred.is_valid || pred.hitchance < effective_hc) return false;
            if (is_unstable_q_prediction(target, pred)) return false;
            if (!verify_q_geometry(target, pred)) return false;

            const bool ok = sdk::common->cast_spell(SLOT_Q, pred.cast_position);
            if (ok) s_last_spell_cast_time = game_time();
            return ok;
        }

        static bool cast_w(game_object* target, bool bypass_anti_buffering = false)
        {
            auto* p = player();
            if (!p || !target) return false;
            if (!sdk::common && !bypass_anti_buffering) return false;
            if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
            if (!is_valid_target(target, sc::W_RANGE)) return false;

            bool ok = false;
            if (bypass_anti_buffering)
            {
                p->cast_spell(SLOT_W, target);
                ok = true;
            }
            else
            {
                ok = sdk::common->cast_spell(SLOT_W, target);
            }

            if (ok) s_last_spell_cast_time = game_time();
            return ok;
        }

        static bool cast_e(game_object* target, bool bypass_anti_buffering = false)
        {
            auto* p = player();
            if (!p || !target) return false;
            if (!sdk::common && !bypass_anti_buffering) return false;
            if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
            if (!is_valid_target(target, sc::E_RANGE)) return false;

            bool ok = false;
            if (bypass_anti_buffering)
            {
                p->cast_spell(SLOT_E, target);
                ok = true;
            }
            else
            {
                ok = sdk::common->cast_spell(SLOT_E, target);
            }

            if (ok) s_last_spell_cast_time = game_time();
            return ok;
        }

        static const char* get_combo_mode_name()
        {
            switch (menu::combo_mode)
            {
            case 0: return "Basic";
            case 1: return "QEQWQ";
            case 2: return "Root";
            default: return "Smart";
            }
        }

        static void draw_combo_damage_overlay(game_object* target, float damage)
        {
            if (!target || damage <= 0.f || !g_sdk || !g_sdk->renderer)
                return;

            const float hp = target->get_hp();
            const float max_hp = target->get_max_hp();
            if (hp <= 0.f || max_hp <= 0.f)
                return;

            const auto hbar = target->get_health_bar_position();
            if (hbar.x < 1.f && hbar.y < 1.f)
                return;

            const float scale = static_cast<float>(g_sdk->renderer->get_window_height()) / 1080.f;
            constexpr float HERO_OX = -45.6f;
            constexpr float HERO_OY = 15.5f;
            constexpr float HERO_BW = 106.4f;
            constexpr float HERO_BH = 11.5f;

            const float ox = HERO_OX * scale;
            const float oy = HERO_OY * scale;
            const float bw = HERO_BW * scale;
            const float bh = HERO_BH * scale;

            const float r0 = hp / max_hp;
            const float r1 = (std::max)(0.f, hp - damage) / max_hp;
            if (r0 <= r1)
                return;

            const float bl = hbar.x + ox;
            const float bt = hbar.y + oy;

            math::rect rc;
            rc.left = bl + r1 * bw;
            rc.top = bt;
            rc.right = bl + r0 * bw;
            rc.bottom = bt + bh;

            g_sdk->renderer->add_rectangle_filled(rc, menu::color_damage_bar);
        }

        static void cycle_combo_mode()
        {
            menu::combo_mode = (menu::combo_mode + 1) % 4;
        }

        static bool is_flux_buff(buff_instance* buff)
        {
            if (!buff) return false;
            if (buff->get_hash() == RYZE_FLUX_HASH) return true;
            if (sc::BUFF_FLUX != 0u) return buff->get_hash() == sc::BUFF_FLUX;
            return buff->get_name().find(sc::BUFF_NAME_FLUX) != std::string::npos;
        }

        static bool is_passive_stack_buff(buff_instance* buff)
        {
            if (!buff) return false;
            if (sc::BUFF_PASSIVE_STACK != 0u) return buff->get_hash() == sc::BUFF_PASSIVE_STACK;
            return buff->get_name().find(sc::BUFF_NAME_PASSIVE_STACK) != std::string::npos;
        }

        static bool has_flux(game_object* target)
        {
            if (!target) return false;

            for (const auto& entry : s_fluxed_targets)
            {
                if (entry.active && entry.target == target)
                    return true;
            }

            if (auto* buff = target->get_buff_by_hash(RYZE_FLUX_HASH))
            {
                if (buff->is_active())
                    return true;
            }

            for (auto* buff : target->get_buffs())
            {
                if (buff && buff->is_active() && is_flux_buff(buff))
                    return true;
            }

            return false;
        }

        static void mark_flux(game_object* target)
        {
            if (!target) return;

            const float expire_time = game_time() + sc::FLUX_DURATION;

            for (auto& entry : s_fluxed_targets)
            {
                if (entry.active && entry.target == target)
                {
                    entry.expire_time = expire_time;
                    return;
                }
            }

            for (auto& entry : s_fluxed_targets)
            {
                if (!entry.active)
                {
                    entry.target = target;
                    entry.expire_time = expire_time;
                    entry.active = true;
                    return;
                }
            }
        }

        static void remove_flux(game_object* target)
        {
            if (!target) return;

            for (auto& entry : s_fluxed_targets)
            {
                if (entry.active && entry.target == target)
                {
                    entry = {};
                    return;
                }
            }
        }

        static void cleanup_flux()
        {
            const float now = game_time();

            for (auto& entry : s_fluxed_targets)
            {
                if (!entry.active) continue;
                if (!entry.target || !entry.target->is_valid() || entry.target->is_dead() || entry.expire_time < now)
                    entry = {};
            }
        }

        static int count_nearby_enemy_minions(game_object* center, float radius)
        {
            auto* p = player();
            if (!p || !center || !g_sdk || !g_sdk->object_manager) return 0;

            const auto center_pos = center->get_position();
            int count = 0;

            for (auto* minion : g_sdk->object_manager->get_minions())
            {
                if (!minion || minion == center) continue;
                if (!is_valid_enemy_minion(minion)) continue;
                if (minion->get_position().distance(center_pos) <= radius)
                    ++count;
            }

            return count;
        }

        static float predicted_hp(game_object* target, float seconds)
        {
            if (!target) return 0.f;
            if (sdk::health_prediction)
                return sdk::health_prediction->get_predicted_health(target, game_time() + seconds);
            return target->get_hp();
        }

        static venplus::damage::caster_stats make_caster_stats(game_object* p)
        {
            return {
                .ap = p->get_ability_power(),
                .bonus_ad = p->get_attack_damage() - p->get_base_attack_damage(),
                .total_ad = p->get_attack_damage(),
                .level = static_cast<float>(p->get_level()),
            };
        }

        static venplus::damage::target_info make_target_info(game_object* t)
        {
            return {
                .max_hp = t->get_max_hp(),
                .curr_hp = t->get_hp(),
                .armor = t->get_armor(),
                .bonus_armor = t->get_bonus_armor(),
                .mr = t->get_magic_resist(),
                .bonus_mr = t->get_bonus_magic_resist(),
            };
        }

        static venplus::damage::pen_stats make_magic_pen(game_object* p)
        {
            return {
                .pct_bonus_pen = p->get_percent_bonus_magic_penetration(),
                .pct_total_pen = p->get_percent_magic_penetration(),
                .flat_pen = p->get_flat_magic_penetration(),
            };
        }

        static float get_bonus_mana()
        {
            auto* p = player();
            if (!p) return 0.f;

            const float base_mana = venplus::db::champions::RYZE.stats.stat_at(
                venplus::db::champions::RYZE.stats.mp_base,
                venplus::db::champions::RYZE.stats.mp_per_level,
                p->get_level());

            return (std::max)(0.f, p->get_max_par() - base_mana);
        }

        static float get_q_damage(game_object* target, bool fluxed)
        {
            auto* p = player();
            if (!p || !target) return 0.f;

            const int rank = spell_rank(SLOT_Q);
            if (rank <= 0) return 0.f;

            const auto& dmg_entry = venplus::db::champions::RYZE.spells[SLOT_Q].damage;
            const auto caster = make_caster_stats(p);
            const auto info = make_target_info(target);
            const auto pen = make_magic_pen(p);

            float raw = venplus::damage::raw_spell_damage(dmg_entry, rank - 1, caster);
            raw += sc::Q_BONUS_MANA_RATIO * get_bonus_mana();

            if (fluxed)
            {
                const int r_rank = spell_rank(SLOT_R);
                raw *= 1.f + sc::Q_FLUX_BONUS[(std::clamp)(r_rank, 0, 3)];
            }

            const float eff_mr = venplus::damage::effective_resist(info.mr, info.bonus_mr, pen);
            return raw * venplus::damage::resist_multiplier(eff_mr);
        }

        static float get_w_damage(game_object* target)
        {
            auto* p = player();
            if (!p || !target) return 0.f;

            const int rank = spell_rank(SLOT_W);
            if (rank <= 0) return 0.f;

            auto caster = make_caster_stats(p);
            auto info = make_target_info(target);
            const auto pen = make_magic_pen(p);
            auto dmg_entry = venplus::db::champions::RYZE.spells[SLOT_W].damage;
            dmg_entry.base[rank - 1] += sc::W_BONUS_MANA_RATIO * get_bonus_mana();

            venplus::damage::pen_stats armor_pen{};
            return venplus::damage::final_spell_damage(dmg_entry, rank - 1, caster, info, armor_pen, pen);
        }

        static float get_e_damage(game_object* target)
        {
            auto* p = player();
            if (!p || !target) return 0.f;

            const int rank = spell_rank(SLOT_E);
            if (rank <= 0) return 0.f;

            auto caster = make_caster_stats(p);
            auto info = make_target_info(target);
            const auto pen = make_magic_pen(p);
            auto dmg_entry = venplus::db::champions::RYZE.spells[SLOT_E].damage;
            dmg_entry.base[rank - 1] += sc::E_BONUS_MANA_RATIO * get_bonus_mana();

            venplus::damage::pen_stats armor_pen{};
            return venplus::damage::final_spell_damage(dmg_entry, rank - 1, caster, info, armor_pen, pen);
        }

        static float get_combo_damage(game_object* target)
        {
            if (!target) return 0.f;

            float total = 0.f;
            bool target_fluxed = has_flux(target);

            if (is_spell_ready(SLOT_E))
            {
                total += get_e_damage(target);
                target_fluxed = true;
            }

            if (is_spell_ready(SLOT_W))
                total += get_w_damage(target);

            if (is_spell_learned(SLOT_Q))
            {
                int q_casts = 0;
                if (is_spell_ready(SLOT_Q)) ++q_casts;
                if (is_spell_ready(SLOT_E)) ++q_casts;
                if (is_spell_ready(SLOT_W)) ++q_casts;
                q_casts = (std::min)(q_casts, 3);

                for (int i = 0; i < q_casts; ++i)
                    total += get_q_damage(target, target_fluxed);
            }

            return total;
        }

        static bool try_q_on_fluxed_collision(game_object* target)
        {
            auto* p = player();
            if (!p || !target || !sdk::prediction || !sdk::common) return false;
            if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

            const auto player_pos = p->get_position();
            const auto target_pos = target->get_position();
            const float target_dist = player_pos.distance(target_pos);

            for (auto* minion : g_sdk->object_manager->get_minions())
            {
                if (!is_valid_enemy_minion(minion, sc::Q_RANGE)) continue;
                if (!has_flux(minion)) continue;

                const auto minion_pos = minion->get_position();
                const float minion_dist = player_pos.distance(minion_pos);
                if (minion_dist > target_dist) continue;

                const float dir_tx = target_pos.x - player_pos.x;
                const float dir_tz = target_pos.z - player_pos.z;
                const float dir_mx = minion_pos.x - player_pos.x;
                const float dir_mz = minion_pos.z - player_pos.z;

                const float len_t = std::sqrt(dir_tx * dir_tx + dir_tz * dir_tz);
                const float len_m = std::sqrt(dir_mx * dir_mx + dir_mz * dir_mz);
                if (len_t < 1.f || len_m < 1.f) continue;

                const float dot = dir_tx * dir_mx + dir_tz * dir_mz;
                const float alignment = dot / (len_t * len_m);
                if (alignment < 0.85f) continue;

                return cast_q(minion, pred_sdk::hitchance::medium);
            }

            return false;
        }

        static bool try_e_on_collision_setup(game_object* target)
        {
            auto* p = player();
            if (!p || !target || !g_sdk || !g_sdk->object_manager) return false;
            if (!menu::combo_flux_q_collision) return false;
            if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;

            const auto player_pos = p->get_position();
            const auto target_pos = target->get_position();
            const float target_dist = player_pos.distance(target_pos);
            const int direct_hc = effective_q_hitchance(target, menu::combo_q_hitchance);
            const auto direct_pred = predict_q(target, direct_hc);

            // Keep the normal direct pattern when Q already has a clean line.
            if (direct_pred.is_valid && direct_pred.hitchance >= direct_hc
                && !is_unstable_q_prediction(target, direct_pred))
                return false;

            game_object* best_minion = nullptr;
            float best_alignment = -1.f;

            for (auto* minion : g_sdk->object_manager->get_minions())
            {
                if (!is_valid_enemy_minion(minion, sc::E_RANGE)) continue;
                if (minion->get_position().distance(target_pos) > sc::E_SPREAD_RADIUS) continue;
                if (player_pos.distance(minion->get_position()) > sc::Q_RANGE) continue;

                const auto minion_pos = minion->get_position();
                const float minion_dist = player_pos.distance(minion_pos);
                if (minion_dist > target_dist) continue;

                const float dir_tx = target_pos.x - player_pos.x;
                const float dir_tz = target_pos.z - player_pos.z;
                const float dir_mx = minion_pos.x - player_pos.x;
                const float dir_mz = minion_pos.z - player_pos.z;

                const float len_t = std::sqrt(dir_tx * dir_tx + dir_tz * dir_tz);
                const float len_m = std::sqrt(dir_mx * dir_mx + dir_mz * dir_mz);
                if (len_t < 1.f || len_m < 1.f) continue;

                const float dot = dir_tx * dir_mx + dir_tz * dir_mz;
                const float alignment = dot / (len_t * len_m);
                if (alignment < 0.85f) continue;

                if (alignment > best_alignment)
                {
                    best_alignment = alignment;
                    best_minion = minion;
                }
            }

            return best_minion && cast_e(best_minion);
        }

        static bool cast_combo_q(game_object* target)
        {
            if (!target) return false;

            if (menu::combo_flux_q_collision)
            {
                const int direct_hc = effective_q_hitchance(target, menu::combo_q_hitchance);
                const auto direct_pred = predict_q(target, direct_hc);
                if (!direct_pred.is_valid || direct_pred.hitchance < direct_hc
                    || is_unstable_q_prediction(target, direct_pred))
                {
                    if (try_q_on_fluxed_collision(target)) return true;
                }
            }

            if (cast_q(target, menu::combo_q_hitchance)) return true;

            if (menu::combo_flux_q_collision)
                return try_q_on_fluxed_collision(target);

            return false;
        }

        static void combo()
        {
            auto* target = pick_target(sc::Q_RANGE);
            if (!target) return;

            const float dist = player()->get_position().distance(target->get_position());
            const bool fluxed = has_flux(target);

            if (menu::combo_mode == 0)
            {
                if (menu::combo_use_q && cast_combo_q(target)) return;

                if (menu::combo_use_e && try_e_on_collision_setup(target)) return;
                if (menu::combo_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E) && dist <= sc::E_RANGE)
                {
                    if (cast_e(target)) return;
                }

                if (menu::combo_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W) && dist <= sc::W_RANGE)
                {
                    if (cast_w(target)) return;
                }

                if (menu::combo_use_q && cast_combo_q(target)) return;
                return;
            }

            if (menu::combo_mode == 1)
            {
                if (menu::combo_use_q && cast_combo_q(target)) return;

                if (menu::combo_use_e && try_e_on_collision_setup(target)) return;
                if (menu::combo_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E) && dist <= sc::E_RANGE)
                {
                    if (cast_e(target)) return;
                }

                if (menu::combo_use_q && cast_combo_q(target)) return;

                if (menu::combo_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W) && dist <= sc::W_RANGE)
                {
                    if (cast_w(target)) return;
                }

                if (menu::combo_use_q && cast_combo_q(target)) return;
                return;
            }

            if (menu::combo_mode == 2)
            {
                if (menu::combo_use_e && try_e_on_collision_setup(target)) return;
                if (menu::combo_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E) && dist <= sc::E_RANGE)
                {
                    if (cast_e(target)) return;
                }

                if (menu::combo_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W) && dist <= sc::W_RANGE)
                {
                    if (cast_w(target)) return;
                }

                if (menu::combo_use_q && cast_combo_q(target)) return;
                return;
            }

            if (menu::combo_use_e && !fluxed && try_e_on_collision_setup(target)) return;
            if (menu::combo_use_e && !fluxed && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E) && dist <= sc::E_RANGE)
            {
                if (cast_e(target)) return;
            }

            if (menu::combo_use_w && fluxed && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W) && dist <= sc::W_RANGE)
            {
                if (cast_w(target)) return;
            }

            if (menu::combo_use_w && !fluxed && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
                && dist <= sc::W_RANGE && !is_spell_ready(SLOT_E))
            {
                if (cast_w(target)) return;
            }

            if (menu::combo_use_q && cast_combo_q(target)) return;

            if (menu::combo_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E) && dist <= sc::E_RANGE)
                cast_e(target);
        }

        static void harass()
        {
            if (mana_pct() < static_cast<float>(menu::harass_mana_percent)) return;

            auto* target = pick_target(sc::Q_RANGE);
            if (!target) return;

            const float dist = player()->get_position().distance(target->get_position());
            const bool fluxed = has_flux(target);

            if (menu::harass_use_e && !fluxed && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E) && dist <= sc::E_RANGE)
            {
                if (cast_e(target)) return;
            }

            if (menu::harass_use_w && fluxed && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W) && dist <= sc::W_RANGE)
            {
                if (cast_w(target)) return;
            }

            if (menu::harass_use_q)
                cast_q(target, menu::combo_q_hitchance);
        }

        static void clear()
        {
            auto* p = player();
            if (!p || !sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
            if (mana_pct() < static_cast<float>(menu::clear_mana_percent)) return;

            if (menu::clear_use_e && menu::clear_use_q && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)
                && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
            {
                game_object* best_target = nullptr;
                int best_nearby = 0;

                for (auto* minion : g_sdk->object_manager->get_minions())
                {
                    if (!is_valid_enemy_minion(minion, sc::E_RANGE)) continue;

                    const int nearby = count_nearby_enemy_minions(minion, sc::E_SPREAD_RADIUS);
                    if (nearby > best_nearby)
                    {
                        best_nearby = nearby;
                        best_target = minion;
                    }
                }

                if (best_target && best_nearby >= menu::clear_q_min_hit - 1)
                {
                    if (cast_e(best_target)) return;
                }
            }

            if (menu::clear_use_q && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
            {
                for (auto* minion : g_sdk->object_manager->get_minions())
                {
                    if (!is_valid_enemy_minion(minion, sc::Q_RANGE)) continue;
                    if (!has_flux(minion)) continue;

                    if (cast_q(minion, pred_sdk::hitchance::medium)) return;
                }
            }

            if (menu::clear_use_q && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
            {
                for (auto* minion : g_sdk->object_manager->get_minions())
                {
                    if (!is_valid_enemy_minion(minion, sc::Q_RANGE)) continue;

                    const float hp = predicted_hp(minion, sc::Q_DELAY + 0.1f);
                    if (hp > 0.f && hp <= get_q_damage(minion, has_flux(minion)))
                    {
                        if (cast_q(minion, pred_sdk::hitchance::medium)) return;
                    }
                }
            }
        }

        static void lasthit()
        {
            if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
            if (!menu::lasthit_use_q || mana_pct() < static_cast<float>(menu::lasthit_mana_percent)) return;
            if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;

            for (auto* minion : g_sdk->object_manager->get_minions())
            {
                if (!is_valid_enemy_minion(minion, sc::Q_RANGE)) continue;

                const float hp = predicted_hp(minion, sc::Q_DELAY + 0.1f);
                if (hp > 0.f && hp <= get_q_damage(minion, has_flux(minion)))
                {
                    if (cast_q(minion, pred_sdk::hitchance::medium)) return;
                }
            }
        }

        static void flee()
        {
            if (!menu::flee_use_ew) return;

            auto* target = pick_target(sc::E_RANGE);
            if (!target) return;

            if (is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E))
            {
                if (cast_e(target)) return;
            }

            if (has_flux(target) && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W))
                cast_w(target);
        }

        static void semi_root()
        {
            if (game_time() - s_last_spell_cast_time < SEMI_ROOT_SPELL_DELAY) return;

            if (s_semi_root_target)
            {
                if (!s_semi_root_target->is_valid() || s_semi_root_target->is_dead()
                    || s_semi_root_target->get_team_id() == player()->get_team_id()
                    || game_time() > s_semi_root_expire_time)
                {
                    s_semi_root_target = nullptr;
                    s_semi_root_expire_time = 0.f;
                }
            }

            const bool e_ready = is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E);
            const bool w_ready = is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W);
            if (!e_ready && !w_ready) return;

            if (s_semi_root_target && w_ready && is_valid_target(s_semi_root_target, sc::W_RANGE))
            {
                if (cast_w(s_semi_root_target, true))
                {
                    s_semi_root_target = nullptr;
                    s_semi_root_expire_time = 0.f;
                }
                return;
            }

            auto* target = pick_target(sc::E_RANGE);
            if (!target)
            {
                s_semi_root_target = nullptr;
                s_semi_root_expire_time = 0.f;
                return;
            }

            if (has_flux(target) && w_ready)
            {
                cast_w(target, true);
                return;
            }

            if (e_ready)
            {
                if (cast_e(target, true))
                {
                    s_semi_root_target = target;
                    s_semi_root_expire_time = game_time() + SEMI_ROOT_BUFFER_WINDOW;
                }
            }
        }

        static void setup_menu()
        {
            s_menu = g_sdk->menu_manager->add_category("ryze", "MeowAIO - Ryze");
            if (!s_menu) return;

            auto* q_menu = s_menu->add_sub_category("q_overload", "Q - Overload");
            q_menu->add_combo("combo_mode", "Combo Mode", { "Basic Q > E > W > Q", "Q > E > Q > W > Q", "Root E > W > Q", "Smart" }, menu::combo_mode,
                [](int v) { menu::combo_mode = v; });
            q_menu->add_hotkey("combo_mode_switch", "Cycle Combo Mode", 'Y', false, true,
                [](std::string* key, bool pressed) {
                    if (key) menu::combo_mode_key = *key;
                    if (pressed && !s_combo_mode_toggle_pressed)
                    {
                        cycle_combo_mode();
                    }
                    s_combo_mode_toggle_pressed = pressed;
                });
            q_menu->add_separator();
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_q,
                [](bool v) { menu::combo_use_q = v; });
            q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q,
                [](bool v) { menu::harass_use_q = v; });
            q_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_q,
                [](bool v) { menu::clear_use_q = v; });
            q_menu->add_checkbox("lasthit", "Use in Lasthit", menu::lasthit_use_q,
                [](bool v) { menu::lasthit_use_q = v; });
            q_menu->add_checkbox("flux_q", "Q Through Fluxed Minions", menu::combo_flux_q_collision,
                [](bool v) { menu::combo_flux_q_collision = v; });
            q_menu->add_slider_int("combo_hc", "Hitchance", 0, 100, 5, menu::combo_q_hitchance,
                [](int v) { menu::combo_q_hitchance = v; });
            q_menu->add_slider_int("clear_min", "Clear Min Minions", 2, 6, 1, menu::clear_q_min_hit,
                [](int v) { menu::clear_q_min_hit = v; });
            q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
                [](bool v) { menu::draw_q_range = v; });

            auto* w_menu = s_menu->add_sub_category("w_rune_prison", "W - Rune Prison");
            w_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_w,
                [](bool v) { menu::combo_use_w = v; });
            w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w,
                [](bool v) { menu::harass_use_w = v; });
            w_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_ew,
                [](bool v) { menu::flee_use_ew = v; });
            w_menu->add_hotkey("semi_root", "Insta Root", 'G', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_root_active = pressed;
                    if (key) menu::semi_root_key = *key;
                });
            w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
                [](bool v) { menu::draw_w_range = v; });

            auto* e_menu = s_menu->add_sub_category("e_spell_flux", "E - Spell Flux");
            e_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_e,
                [](bool v) { menu::combo_use_e = v; });
            e_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_e,
                [](bool v) { menu::harass_use_e = v; });
            e_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_e,
                [](bool v) { menu::clear_use_e = v; });
            e_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_ew,
                [](bool v) { menu::flee_use_ew = v; });
            e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
                [](bool v) { menu::draw_e_range = v; });

            auto* r_menu = s_menu->add_sub_category("r_realm_warp", "R - Realm Warp");
            r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
                [](bool v) { menu::draw_r_range = v; });
            r_menu->add_checkbox("rainbow", "Rainbow Range", menu::draw_r_rgb,
                [](bool v) { menu::draw_r_rgb = v; });
            r_menu->add_checkbox("draw_minimap", "Draw On Minimap", menu::draw_r_minimap,
                [](bool v) { menu::draw_r_minimap = v; });

            auto* mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
            mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_percent,
                [](int v) { menu::harass_mana_percent = v; });
            mana_menu->add_slider_int("clear", "Clear Mana %", 0, 100, 5, menu::clear_mana_percent,
                [](int v) { menu::clear_mana_percent = v; });
            mana_menu->add_slider_int("lasthit", "Lasthit Mana %", 0, 100, 5, menu::lasthit_mana_percent,
                [](int v) { menu::lasthit_mana_percent = v; });

            auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings");
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_separator();
            draw_menu->add_checkbox("combo_mode", "Combo Mode Indicator", menu::draw_combo_mode,
                [](bool v) { menu::draw_combo_mode = v; });
            draw_menu->add_checkbox("damage", "Combo Damage Indicator", menu::draw_damage_bar,
                [](bool v) { menu::draw_damage_bar = v; });
            draw_menu->add_checkbox("killable", "Killable Indicator", menu::draw_killable_text,
                [](bool v) { menu::draw_killable_text = v; });
            draw_menu->add_checkbox("flux", "Flux Indicator", menu::draw_flux, [](bool v) { menu::draw_flux = v; });
            draw_menu->add_separator();
            draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q,
                [](uint32_t c) { menu::color_q = c; });
            draw_menu->add_colorpicker("color_w", "W Color", menu::color_w,
                [](uint32_t c) { menu::color_w = c; });
            draw_menu->add_colorpicker("color_e", "E Color", menu::color_e,
                [](uint32_t c) { menu::color_e = c; });
            draw_menu->add_colorpicker("color_r", "R Color", menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
            draw_menu->add_colorpicker("color_damage", "Damage Bar Color", menu::color_damage_bar,
                [](uint32_t c) { menu::color_damage_bar = c; });
        }
    }

    void load()
    {
        auto* p = player();
        if (!p || p->get_char_name() != std::string("Ryze"))
            return;

        g_sdk->log_console("[MeowAIO] Loading Ryze...");

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        setup_menu();
        cleanup_flux();

        if (sdk::infotab && !menu::semi_root_key.empty())
        {
            menu::infotab_semi_root = sdk::infotab->add_hotkey_text(
                &menu::semi_root_key, { "Insta Root" }, []() -> infotab_sdk::text_entry {
                    const bool active = menu::semi_root_active;
                    const uint32_t color = active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { active ? "ON" : "OFF", color };
                });
        }
        if (sdk::infotab)
        {
            infotab_sdk::text_entry title_combo_mode{ "Combo Mode", 0xFFFFFFFFu };
            menu::infotab_combo_mode = sdk::infotab->add_text(title_combo_mode,
                []() -> infotab_sdk::text_entry {
                    return { get_combo_mode_name(), 0xFFAA66FFu };
                });
        }

        g_sdk->log_console("[MeowAIO] Ryze loaded successfully :3");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager)
        {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw_world));
            g_sdk->event_manager->unregister_callback(event_manager::event::present,
                reinterpret_cast<void*>(on_present));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }

        s_fluxed_targets.fill({});
        s_last_spell_cast_time = 0.f;
        s_rune_stacks = 0;
        s_semi_root_target = nullptr;
        s_semi_root_expire_time = 0.f;
        menu::semi_root_active = false;
        if (sdk::infotab && menu::infotab_semi_root)
        {
            sdk::infotab->remove_text(menu::infotab_semi_root);
            menu::infotab_semi_root = 0;
        }
        if (sdk::infotab && menu::infotab_combo_mode)
        {
            sdk::infotab->remove_text(menu::infotab_combo_mode);
            menu::infotab_combo_mode = 0;
        }
        s_menu = nullptr;

        if (g_sdk) g_sdk->log_console("[MeowAIO] Ryze unloaded successfully :3");
    }

    void __fastcall on_update()
    {
        auto* p = player();
        if (!p || p->is_dead() || !sdk::common) return;
        if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

        cleanup_flux();

        s_rune_stacks = 0;
        for (auto* buff : p->get_buffs())
        {
            if (buff && buff->is_active() && is_passive_stack_buff(buff))
            {
                s_rune_stacks = buff->get_count();
                break;
            }
        }

        if (menu::semi_root_active)
        {
            semi_root();
            return;
        }

        if (game_time() - s_last_spell_cast_time < MIN_SPELL_DELAY) return;
        if (is_casting()) return;

        if (!sdk::orbwalker) return;

        if (sdk::orbwalker->combo()) combo();
        else if (sdk::orbwalker->harass()) harass();
        else if (sdk::orbwalker->lasthit()) lasthit();
        else if (sdk::orbwalker->clear() || sdk::orbwalker->fast_clear()) clear();
        else if (sdk::orbwalker->flee()) flee();
    }

    void __fastcall on_draw_world()
    {
        auto* p = player();
        if (!p || p->is_dead()) return;

        const auto pos = p->get_position();
        const auto draw_circle = [&](float radius, uint32_t color)
        {
            if (sdk::shaders)
                sdk::shaders->draw_world_circle(pos, radius, 2.f, color);
            else if (g_sdk->renderer)
                g_sdk->renderer->add_circle_3d(pos, radius, 0.5f, color);
        };

        if (menu::draw_q_range && is_spell_learned(SLOT_Q)
            && (!menu::draw_only_ready || is_spell_ready(SLOT_Q)))
            draw_circle(sc::Q_RANGE, menu::color_q);

        if (menu::draw_w_range && is_spell_learned(SLOT_W)
            && (!menu::draw_only_ready || is_spell_ready(SLOT_W)))
            draw_circle(sc::W_RANGE, menu::color_w);

        if (menu::draw_e_range && is_spell_learned(SLOT_E)
            && (!menu::draw_only_ready || is_spell_ready(SLOT_E)))
            draw_circle(sc::E_RANGE, menu::color_e);

        if (menu::draw_r_range && is_spell_learned(SLOT_R)
            && (!menu::draw_only_ready || is_spell_ready(SLOT_R)))
        {
            const float r_range = venplus::db::champions::RYZE.spells[SLOT_R].geometry.range;
            if (menu::draw_r_rgb && sdk::shaders)
                sdk::shaders->draw_world_circle(pos, r_range, 2.f, menu::color_r, shaders_sdk::world_circle_style::rgb);
            else
                draw_circle(r_range, menu::color_r);
        }
    }

    void __fastcall on_present()
    {
        auto* p = player();
        if (!p || p->is_dead()) return;

        if (menu::draw_r_minimap && is_spell_learned(SLOT_R))
        {
            g_sdk->renderer->add_circle_minimap(
                p->get_position(),
                venplus::db::champions::RYZE.spells[SLOT_R].geometry.range,
                1.f,
                0xB4AA66FFu);
        }

        if (menu::draw_combo_mode)
        {
            const auto player_screen = g_sdk->renderer->world_to_screen(p->get_position());
            if (!(player_screen.x < 1.f && player_screen.y < 1.f))
            {
                std::string mode_text = std::string("Mode: ") + get_combo_mode_name();
                math::vector2 text_pos{ player_screen.x, player_screen.y + 28.f };
                g_sdk->renderer->add_text(mode_text, 13.f, text_pos, 1, 0xFFAA66FFu);
            }
        }

        const auto draw_flux_label = [&](game_object* obj)
        {
            if (!menu::draw_flux || !obj || !has_flux(obj))
                return;

            const auto screen = g_sdk->renderer->world_to_screen(obj->get_position());
            if (screen.x < 1.f && screen.y < 1.f)
                return;

            g_sdk->renderer->add_text("FLUX", 13.f, { screen.x, screen.y - 26.f }, 1, 0xFF8844FFu);
        };

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero)) continue;

            const float dmg = get_combo_damage(hero);
            if (menu::draw_damage_bar && dmg > 0.f)
                draw_combo_damage_overlay(hero, dmg);

            const auto screen = g_sdk->renderer->world_to_screen(hero->get_position());
            if (screen.x < 1.f && screen.y < 1.f) continue;

            if (menu::draw_killable_text && dmg >= hero->get_hp())
                g_sdk->renderer->add_text("Killable", 14.f, { screen.x, screen.y - 42.f }, 1, 0xFF00E600u);
            draw_flux_label(hero);
        }

        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (!is_valid_enemy_minion(minion))
                continue;
            draw_flux_label(minion);
        }
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        auto* p = player();
        if (!p || !obj || !buff) return;

        if (obj->get_team_id() != p->get_team_id() && is_flux_buff(buff))
            mark_flux(obj);

        if (obj == p && is_passive_stack_buff(buff))
            s_rune_stacks = buff->get_count();
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        auto* p = player();
        if (!p || !obj || !buff) return;

        if (obj->get_team_id() != p->get_team_id() && is_flux_buff(buff))
        {
            remove_flux(obj);
            if (s_semi_root_target == obj)
            {
                s_semi_root_target = nullptr;
                s_semi_root_expire_time = 0.f;
            }
        }

        if (obj == p && is_passive_stack_buff(buff))
            s_rune_stacks = 0;
    }
}
