#include "viktor.h"
#include "utility/minion_manager.hpp"
#include "math/geometry.hpp"
#include <algorithm>
#include <cfloat>
#include <vector>
#include <string>

namespace viktor
{
    static game_object* r_instance = nullptr;
    static bool         r_storm_active = false;
    static float        r_storm_start_t = 0.f;
    static math::vector3 r_cast_pos{};
    static bool          r_cast_pos_valid = false;
    static float        last_r_move_time = 0.f;
    static float        last_cast_e = 0.f;
    static float        last_q_combo_time = 0.f;
    static float        last_q_harass_time = 0.f;
    static float        last_q_lasthit_time = 0.f;
    static bool         anti_gap_w_fired = false;

    static float        m_last_spell_cast_time = 0.f;
    static constexpr float MIN_SPELL_DELAY = 0.1f;

    struct e_cast_log
    {
        bool          armed         = false;
        uint32_t      target_net_id = 0;
        std::string   target_name;
        math::vector3 fp{};
        math::vector3 cp{};
        float         eval_time     = 0.f;
        float         tgt_dist_at_cast = 0.f;
        float         tgt_speed_at_cast = 0.f;
        const char*   branch        = "?";
    };
    static e_cast_log m_e_log{};

    template <typename... Args>
    static inline void log_e(const char* fmt, Args&&... args)
    {
        if (!menu::log_e_casts) return;
        if (!g_sdk) return;
        g_sdk->log_console(fmt, std::forward<Args>(args)...);
    }

    // Q scheduled by on_before_attack, consumed in on_update.
    static bool         m_q_scheduled        = false;
    static float        m_q_scheduled_time   = 0.f;
    static game_object* m_q_scheduled_target = nullptr;
    static constexpr float Q_AA_CAST_DELAY   = 0.05f;

    static constexpr float R_SEARCH_RADIUS    = 700.f;
    static constexpr float W_MIN_DIST         = 50.f;
    static constexpr float W_MIN_PLACEMENT    = 150.f;
    static constexpr float DASH_NEAR_DIST     = 400.f;

    // Storm physically moves at ~80 u/s (100 with Hex Core); 90 averages it.
    static constexpr float R_STORM_SPEED      = 90.f;
    static constexpr float R_STORM_LIFETIME   = 7.f;
    static constexpr float R_STOP_MOVING_AT   = 1.0f;
    static constexpr float R_DEAD_ZONE        = 35.f;
    static constexpr float R_HYSTERESIS       = 1.5f;
    static constexpr float R_HIT_RADIUS_FRAC  = 0.92f;
    static constexpr float R_PRED_BASE_DELAY  = 0.30f;

    static math::vector3 last_r_move_pos{};
    static float         last_r_move_score = 0.f;

    namespace scratch
    {
        inline std::vector<game_object*>& farm_targets()
        {
            static std::vector<game_object*> v;
            v.clear();
            return v;
        }
        inline std::vector<float>& farm_e_dmg()
        {
            static std::vector<float> v;
            v.clear();
            return v;
        }
        inline std::vector<game_object*>& storm_enemies()
        {
            static std::vector<game_object*> v;
            v.clear();
            return v;
        }

        struct hero_pred { game_object* hero; math::vector3 pred_pos; };
        inline std::vector<hero_pred>& e_in_range()
        {
            static std::vector<hero_pred> v;
            v.clear();
            return v;
        }
    }

    static bool is_hard_cc(game_object* o)
    {
        if (!o) return false;
        return o->has_buff_of_type(buff_type::stun) ||
               o->has_buff_of_type(buff_type::snare) ||
               o->has_buff_of_type(buff_type::knockup) ||
               o->has_buff_of_type(buff_type::knockback) ||
               o->has_buff_of_type(buff_type::charm) ||
               o->has_buff_of_type(buff_type::suppression) ||
               o->has_buff_of_type(buff_type::taunt) ||
               o->has_buff_of_type(buff_type::asleep);
    }

    static bool is_in_stasis(game_object* o)
    {
        if (!o) return false;
        if (!sdk::common || !sdk::common->buffs) return false;
        auto* stasis = sdk::common->buffs->get_stasis_buff(o);
        return stasis && stasis->is_active();
    }

    static bool is_damage_immune(game_object* o)
    {
        if (!o) return false;
        if (o->has_buff_of_type(buff_type::invulnerability)) return true;
        if (o->has_buff_of_type(buff_type::spell_immunity)) return true;
        return false;
    }

    static void mark_spell_cast(float now) { m_last_spell_cast_time = now; }

    static float effective_hp(game_object* o)
    {
        if (!o) return 0.f;
        return o->get_hp()
             + o->get_all_shield()
             + o->get_physical_shield()
             + o->get_magical_shield();
    }

    static float distance_2d(const math::vector3& a, const math::vector3& b)
    {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    static math::vector3 direction_2d(const math::vector3& from, const math::vector3& to)
    {
        const float dx = to.x - from.x;
        const float dz = to.z - from.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len <= 0.001f)
            return {};

        return { dx / len, 0.f, dz / len };
    }

    // Hashes mirror meow_shared/utility/defensive_buffs.hpp. 0u placeholders
    static bool has_anti_burst_buff(game_object* o)
    {
        if (!o) return false;
        constexpr uint32_t HASHES[] = {
            anti_burst::KINDRED_R,
            anti_burst::ZILEAN_R,
        };
        for (uint32_t h : HASHES)
        {
            if (h == 0u) continue;
            auto* b = o->get_buff_by_hash(h);
            if (b && b->is_active()) return true;
        }
        return false;
    }

    // Buff-based ground truth for storm liveness — needed when on_create_object
    static bool is_chaos_storm_buff_active(game_object* player)
    {
        if (!player) return false;
        std::string name = buffs::CHAOS_STORM;
        auto* b = player->get_buff_by_name(name);
        return b && b->is_active();
    }

    static inline bool is_valid_farm_target(game_object* o, int player_team)
    {
        return o && o->is_valid() && !o->is_dead()
            && o->is_targetable()
            && o->get_team_id() != player_team;
    }

    // Lane minions + jungle monsters; jungle additionally requires visibility.
    static bool is_q_lasthittable(game_object* m, game_object* player,
                                  float q_range, float now,
                                  float& out_score)
    {
        if (!is_valid_farm_target(m, player->get_team_id())) return false;

        const auto cls   = venplus::classify_unit(m);
        const bool lane  = venplus::db::minions::is_lane_minion(cls);
        const bool jung  = venplus::db::minions::is_jungle(cls);
        if (!lane && !jung) return false;
        if (jung && !m->is_visible()) return false;

        const auto ppos = player->get_position();
        const auto mpos = m->get_position();
        const float dist = ppos.distance(mpos);
        if (dist > q_range) return false;

        float hp = m->get_hp();
        const float q_dmg = get_q_damage(m);
        if (q_dmg <= 0.f) return false;

        if (sdk::health_prediction && g_sdk->clock_facade)
        {
            try
            {
                const float prediction_time = now + spell_data::q_cast_time;
                float predicted_hp = sdk::health_prediction->get_predicted_health(m, prediction_time);
                if (predicted_hp > 0.f
                    && predicted_hp <= m->get_max_hp()
                    && predicted_hp <= hp
                    && (hp - predicted_hp) > prediction_constants::min_hp_difference)
                {
                    hp = predicted_hp;
                }
            }
            catch (...) {}
        }

        // 15% / 15-HP safety margin (Viktor's existing tuning, looser than
        const float pct_margin   = q_dmg * 0.15f;
        const float fixed_margin = 15.f;
        const float safety       = std::max(pct_margin, fixed_margin);
        if (hp <= 0.f || hp > q_dmg - safety) return false;

        float score = hp;
        const float hp_pct = m->get_max_hp() > 0.f ? hp / m->get_max_hp() : 1.f;
        if      (hp_pct <= 0.1f) score -= 500.f;
        else if (hp_pct <= 0.2f) score -= 300.f;
        else if (hp_pct <= 0.3f) score -= 150.f;

        if      (cls == venplus::unit_class::monster_epic)  score -= 300000.f;
        else if (cls == venplus::unit_class::lane_super)    score -= 200000.f;
        else if (cls == venplus::unit_class::monster_large) score -= 200000.f;
        else if (cls == venplus::unit_class::lane_siege)    score -= 150000.f;

        score += dist * 0.1f; // mild distance preference for tie-break
        out_score = score;
        return true;
    }

    static bool is_discharge_ready(game_object* player)
    {
        if (!player) return false;
        std::string name = buffs::DISCHARGE;
        auto* b = player->get_buff_by_name(name);
        return b && b->is_active();
    }

    static bool looks_like_viktor_storm(game_object* obj, game_object* player)
    {
        if (!obj || !player || !obj->is_valid()) return false;
        if (!obj->is_particle()) return false;

        const auto name = obj->get_name();
        const bool has_viktor = name.find("Viktor") != std::string::npos
                             || name.find("Victor") != std::string::npos;
        const bool has_storm  = name.find("R_Aoe")       != std::string::npos
                             || name.find("ChaosStorm")  != std::string::npos
                             || name.find("ChaosField")  != std::string::npos;
        if (!has_viktor || !has_storm) return false;

        return obj->get_owner() == player;
    }

    bool is_spell_learned(int slot)
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return false;

        auto sp = player->get_spell(slot);
        if (!sp) return false;
        return sp->get_level() > 0;
    }

    bool is_spell_ready(int slot)
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return false;

        auto sp = player->get_spell(slot);
        if (!sp) return false;
        return sp->get_level() > 0 && sp->get_cooldown() <= 0.f;
    }

    bool has_enough_mana(int slot)
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return false;

        const float current_mana = player->get_par();
        const float cost = player->get_spell_mana_cost(slot);
        return current_mana >= cost;
    }

    bool is_casting()
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return false;

        auto active_cast = player->get_active_spell_cast();
        if (active_cast && active_cast->get_spell_cast())
            return true;

        // Treat dashing as casting so we don't start new long casts mid-dash.
        if (player->is_dashing())
            return true;

        return false;
    }

    void __fastcall on_create_object(game_object* obj)
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!looks_like_viktor_storm(obj, player)) return;

        r_instance      = obj;
        r_storm_active  = true;
        r_storm_start_t = g_sdk->clock_facade->get_game_time();
        }

    void __fastcall on_delete_object(game_object* obj)
    {
        if (obj == r_instance)
        {
            r_instance       = nullptr;
            r_storm_active   = false;
            r_cast_pos_valid = false;
        }
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        if (cast->is_basic_attack()) return;
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj != player) return;
        const int slot = cast->get_spell_slot();
        if (slot != SLOT_R) return;
        // Skip recast/move commands — those reuse the R slot but should leave
        // the original cast position untouched.
        const float now = g_sdk->clock_facade->get_game_time();
        r_storm_active = true;
        r_storm_start_t = now;

        // Fresh storm cast should not inherit hysteresis from a previous storm.
        last_r_move_time = 0.f;
        last_r_move_pos = {};
        last_r_move_score = 0.f;

        const auto cp = cast->get_end_pos();
        if (cp.x != 0.f || cp.y != 0.f || cp.z != 0.f)
        {
            r_cast_pos       = cp;
            r_cast_pos_valid = true;
        }
        else
        {
            r_cast_pos_valid = false;
        }
    }

    // Schedules a targeted Q on the AA target so the cast lands during the
    // AA windup, granting Discharge for the next AA. Always returns true.
    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker) return true;
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return true;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return true;

        auto* target = data->target;
        if (!target->is_valid() || target->is_dead() || !target->is_targetable())
            return true;
        if (!target->is_hero()) return true;

        const bool in_combo  = sdk::orbwalker->combo();
        const bool in_harass = sdk::orbwalker->harass();
        bool mode_ok = false;
        if (in_combo && menu::use_q_combo) mode_ok = true;
        else if (in_harass && menu::use_q_harass)
        {
            const float max_par = player->get_max_par();
            if (max_par > 0.f)
            {
                const float mana_pct = (player->get_par() / max_par) * 100.f;
                if (mana_pct >= static_cast<float>(menu::harass_mana_percent))
                    mode_ok = true;
            }
        }
        if (!mode_ok) return true;

        if (!is_spell_learned(SLOT_Q) || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q))
            return true;
        if (is_discharge_ready(player)) return true;
        if (target->has_buff_of_type(buff_type::spell_shield)) return true;

        auto q = player->get_spell(SLOT_Q);
        if (!q) return true;
        const float dist = player->get_position().distance(target->get_position());
        const float reach = q->get_cast_range()
                          + player->get_bounding_radius()
                          + target->get_bounding_radius();
        if (dist > reach) return true;

        const float now = g_sdk->clock_facade->get_game_time();
        m_q_scheduled        = true;
        m_q_scheduled_time   = now + Q_AA_CAST_DELAY;
        m_q_scheduled_target = target;

        return true; // never block the AA
    }


    void __fastcall on_update()
    {
        anti_gap_w_fired = false;

        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade)
        { return; }

        if (g_sdk->is_chat_open() || g_sdk->is_shop_open())
            return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead())
            return;

        // Consume scheduled Q from on_before_attack. Recheck everything in
        // case combo()/harass() already fired Q or the target became invalid.
        if (m_q_scheduled)
        {
            const float now_q = g_sdk->clock_facade->get_game_time();
            if (now_q >= m_q_scheduled_time)
            {
                auto* tgt = m_q_scheduled_target;
                if (tgt && tgt->is_valid() && !tgt->is_dead() && tgt->is_targetable()
                    && is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)
                    && !is_discharge_ready(player))
                {
                    if (cast_q(tgt))
                        last_q_combo_time = now_q;
                }
                m_q_scheduled        = false;
                m_q_scheduled_target = nullptr;
            }
        }

        // E beam-end miss diagnostic — fires at beam end, logs target drift
        // from the beam axis.
        if (m_e_log.armed)
        {
            const float now_e_eval = g_sdk->clock_facade->get_game_time();
            if (now_e_eval >= m_e_log.eval_time)
            {
                auto* tgt = g_sdk->object_manager->get_object_by_network_id(m_e_log.target_net_id);
                if (tgt && tgt->is_valid() && !tgt->is_dead())
                {
                    const auto p   = tgt->get_position();
                    const auto& a  = m_e_log.fp;
                    const auto& b  = m_e_log.cp;
                    const float dx = b.x - a.x, dz = b.z - a.z;
                    const float len_sq = dx * dx + dz * dz;
                    float seg_dist = 0.f;
                    if (len_sq > 0.f)
                    {
                        const float t = std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / len_sq, 0.f, 1.f);
                        const float ex = p.x - (a.x + t * dx);
                        const float ez = p.z - (a.z + t * dz);
                        seg_dist = std::sqrt(ex * ex + ez * ez);
                    }
                    const float hit_r = spell_data::e_radius + tgt->get_bounding_radius();
                    const float drift_from_cast = p.distance({
                        m_e_log.fp.x + dx * 0.5f, p.y, m_e_log.fp.z + dz * 0.5f
                    });
                    const bool likely_hit = seg_dist <= hit_r;

                    log_e("[E] %s tgt=%s d_at_cast=%.0f vel_at_cast=%.0f beam_drift=%.0f (hit_r=%.0f) center_drift=%.0f branch=%s",
                          likely_hit ? "HIT?" : "MISS?",
                          m_e_log.target_name.c_str(),
                          m_e_log.tgt_dist_at_cast, m_e_log.tgt_speed_at_cast,
                          seg_dist, hit_r, drift_from_cast, m_e_log.branch);
                }
                else
                {
                    log_e("[E] eval: target gone (died/invalid) tgt=%s",
                          m_e_log.target_name.c_str());
                }
                m_e_log.armed = false;
            }
        }

        killsteal();
        stasis_e();
        auto_w_on_cc();

        if (!sdk::orbwalker || !sdk::orbwalker->harass() || menu::use_w_harass)
        { anti_gap_w(); }

        if (menu::auto_r_movement)
        { auto_r(); }

        if (menu::semi_w_active && sdk::target_selector)
        {
            game_object* target = sdk::target_selector->get_forced_target();
            if (!target || !is_valid_target(target, spell_data::w_range))
                target = sdk::target_selector->get_hero_target();
            if (target && is_valid_target(target, spell_data::w_range))
                cast_w(target, true);
        }

        if (menu::semi_r_active && sdk::target_selector)
        {
            game_object* target = sdk::target_selector->get_forced_target();
            if (!target || !is_valid_target(target, spell_data::r_range))
                target = sdk::target_selector->get_hero_target();
            if (target && is_valid_target(target, spell_data::r_range))
                cast_r(target, true);
        }

        if (!sdk::orbwalker) { return; }

        // When Discharge is up during farm modes, redirect the AA onto a
        // minion that the empowered AA can kill but a regular AA cannot.
        const float now = g_sdk->clock_facade->get_game_time();
        std::string discharge_name = buffs::DISCHARGE;
        auto* discharge_buff = player->get_buff_by_name(discharge_name);
        const bool has_discharge = discharge_buff && discharge_buff->is_active();
        if (has_discharge ||
            (last_q_lasthit_time > 0.f && (now - last_q_lasthit_time) < 3.f))
        {
            if (sdk::orbwalker->lasthit() || sdk::orbwalker->clear())
            {
                const float aa_range = player->get_attack_range();
                const float aa_time = now + 0.2f;

                // Discharge bonus per Q rank (cross-checked against meow_shared
                // db; pre-rework 20/25/30/35/40 is roughly 3x too low).
                auto q_spell = player->get_spell(SLOT_Q);
                const int q_lvl = q_spell ? std::clamp(q_spell->get_level(), 0, 5) : 0;
                constexpr float Q_AA_BONUS_BASE[] = { 0.f, 20.f, 45.f, 70.f, 95.f, 120.f };
                const float q_aa_bonus_magic = Q_AA_BONUS_BASE[q_lvl] + player->get_ability_power() * 0.5f;
                const float base_ad = player->get_attack_damage();

                game_object* best_aa_target = nullptr;
                float        best_aa_score = FLT_MAX;

                for (auto minion : g_sdk->object_manager->get_minions())
                {
                    if (!minion || !minion->is_valid() || minion->is_dead() || !minion->is_targetable()) continue;
                    if (minion->is_plant()) continue;
                    if (minion->get_team_id() == player->get_team_id()) continue;

                    const float dist = player->get_position().distance(minion->get_position());
                    if (dist > aa_range) continue;

                    float hp = minion->get_hp();
                    if (sdk::health_prediction && g_sdk->clock_facade)
                    {
                        float predicted_hp = sdk::health_prediction->get_predicted_health(minion, aa_time);
                        if (predicted_hp > 0.f && predicted_hp <= minion->get_max_hp() && predicted_hp <= hp)
                            hp = predicted_hp;
                    }

                    float empowered_aa_dmg = base_ad;
                    if (sdk::damage)
                        empowered_aa_dmg += sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, minion, q_aa_bonus_magic);
                    else
                        empowered_aa_dmg += q_aa_bonus_magic;

                    if (hp > 0.f && hp <= empowered_aa_dmg)
                    {
                        float score = hp;
                        const float regular_aa_dmg = base_ad;
                        const bool can_lasthit_regular = hp <= regular_aa_dmg;

                        const float hp_percent = minion->get_max_hp() > 0.f ? hp / minion->get_max_hp() : 1.f;
                        if (hp_percent <= 0.1f) score -= 1000.f;
                        else if (hp_percent <= 0.2f) score -= 500.f;
                        else if (hp_percent <= 0.3f) score -= 200.f;

                        // Prefer minions a regular AA already kills — those won't waste the empower.
                        if (can_lasthit_regular) score -= 5000.f;

                        if (minion->is_lane_minion_super()) score -= 20000.f;
                        else if (minion->is_lane_minion_siege()) score -= 10000.f;

                        if (score < best_aa_score)
                        {
                            best_aa_score = score;
                            best_aa_target = minion;
                        }
                    }
                }

                if (best_aa_target && sdk::orbwalker->is_in_auto_attack_range(player, best_aa_target))
                {
                    sdk::orbwalker->attack(best_aa_target);
                    last_q_lasthit_time = 0.f;
                    return;
                }
            }
        }

        if (sdk::orbwalker->combo())        { combo(); }
        else if (sdk::orbwalker->harass())  { harass(); }
        else if (sdk::orbwalker->lasthit()) { lasthit(); }
        else if (sdk::orbwalker->fast_clear() || sdk::orbwalker->clear()) { clear(); }
        else if (sdk::orbwalker->flee())
        {
            if (menu::use_w_flee)
            {
                // Check if W is ready
                auto w = player->get_spell(SLOT_W);
                if (w && w->get_level() > 0 && w->get_cooldown() <= 0.f
                    && player->get_par() >= player->get_spell_mana_cost(SLOT_W))
                {
                    // Find closest enemy to Viktor (less restrictive - any enemy in range)
                    game_object* closest_enemy = nullptr;
                    float        closest_dist = spell_data::w_range;
                    const auto   ppos = player->get_position();

                    for (auto hero : g_sdk->object_manager->get_heroes())
                    {
                        if (!is_valid_target(hero)) continue;

                        const auto  epos = hero->get_position();
                        const float dist = ppos.distance(epos);
                        if (dist > spell_data::w_range || dist < 100.f) continue;

                        // Prioritize enemies moving toward Viktor, but accept any enemy in range
                        const auto  vel = hero->get_velocity();
                        const float to_vik_x = ppos.x - epos.x;
                        const float to_vik_z = ppos.z - epos.z;
                        const float inv_dist = 1.f / (dist + 0.001f);
                        const float approach_sp = (vel.x * to_vik_x + vel.z * to_vik_z) * inv_dist;

                        // Prefer chasers, but accept any enemy
                        const bool is_chaser = approach_sp > 50.f || hero->is_dashing();
                        const float priority = is_chaser ? (dist - 1000.f) : dist; // Chasers get priority

                        if (priority < closest_dist)
                        {
                            closest_dist = priority;
                            closest_enemy = hero;
                        }
                    }

                    // Cast W on closest enemy
                    if (closest_enemy)
                    {
                        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack())
                            return;

                        // Place W between Viktor and enemy to block their path.
                        // placement_dist is already clamped to w_range, so no
                        // extra final-clamp is needed.
                        const auto  epos = closest_enemy->get_position();
                        const auto  dir  = (epos - ppos).to_direction();
                        const float dist = ppos.distance(epos);
                        const float placement_dist = std::min(spell_data::w_range, dist * 0.4f);
                        const math::vector3 cast_pos = ppos + dir * placement_dist;

                        if (!sdk::common) player->cast_spell(SLOT_W, cast_pos);
                        else              sdk::common->cast_spell(SLOT_W, cast_pos);
                        mark_spell_cast(g_sdk->clock_facade->get_game_time());
                        return;
                    }
                }
            }

            if (menu::use_q_flee)
            {
                game_object* closest = nullptr;
                float        closest_dist = spell_data::q_range;

                for (auto minion : g_sdk->object_manager->get_minions())
                {
                    if (!is_valid_target(minion)) continue;
                    if (minion->is_plant()) continue;
                    const float d = player->get_position().distance(minion->get_position());
                    if (d < closest_dist) { closest_dist = d; closest = minion; }
                }
                for (auto hero : g_sdk->object_manager->get_heroes())
                {
                    if (!is_valid_target(hero)) continue;
                    const float d = player->get_position().distance(hero->get_position());
                    if (d < closest_dist) { closest_dist = d; closest = hero; }
                }

                if (closest) cast_q(closest);
            }
        }
    }

    void __fastcall on_draw()
    {
        if (!g_sdk || !g_sdk->object_manager)
            return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead())
            return;

        const auto player_pos = player->get_position();

        auto draw_circle = [&](math::vector3 const& center, float range, uint32_t clr)
            {
                if (sdk::shaders)
                    sdk::shaders->draw_world_circle(center, range, 2.f, clr);
                else if (g_sdk->renderer)
                    g_sdk->renderer->add_circle_3d(center, range, 2.f, clr);
            };

        auto get_draw_color = [&](int slot, uint32_t clr) -> uint32_t
            {
                auto sp = player->get_spell(slot);
                if (!sp || sp->get_level() == 0) return 0u;
                if (menu::draw_only_ready && sp->get_cooldown() > 0.f) return 0u;
                return sp->get_cooldown() <= 0.f ? clr : ((clr & 0x00FFFFFFu) | 0x40000000u);
            };

        if (menu::draw_q_range) { const uint32_t c = get_draw_color(SLOT_Q, menu::color_q); if (c) draw_circle(player_pos, spell_data::q_range, c); }
        if (menu::draw_w_range) { const uint32_t c = get_draw_color(SLOT_W, menu::color_w); if (c) draw_circle(player_pos, spell_data::w_range, c); }
        if (menu::draw_e_range)
        {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c)
            {
                if (menu::draw_e_rgb && sdk::shaders)
                    sdk::shaders->draw_world_circle(player_pos, spell_data::e_range, 2.f, c, shaders_sdk::world_circle_style::rgb);
                else
                    draw_circle(player_pos, spell_data::e_range, c);
            }
        }
        if (menu::draw_r_range) { const uint32_t c = get_draw_color(SLOT_R, menu::color_r); if (c) draw_circle(player_pos, spell_data::r_range, c); }

        if (g_sdk->renderer && menu::draw_r_killable)
        {
            auto r_sp = player->get_spell(SLOT_R);
            if (r_sp && r_sp->get_level() > 0 && r_sp->get_cooldown() <= 0.f)
            {
                for (auto hero : g_sdk->object_manager->get_heroes())
                {
                    if (!is_valid_target(hero)) continue;
                    const float r_dmg = get_r_damage(hero);
                    if (hero->get_hp() > 0.f && hero->get_hp() <= r_dmg)
                    {
                        auto screen = g_sdk->renderer->world_to_screen(hero->get_position());
                        g_sdk->renderer->add_text("R KILLABLE", 18.f,
                            { screen.x - 35.f, screen.y - 60.f }, 0, 0xFFFF4500u);

                    }
                }
            }
        }

    }

    void __fastcall on_present()
    {
        if (!g_sdk || !g_sdk->object_manager) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !player->is_valid() || player->is_dead()) return;
        if (!menu::draw_r_damage_indicator) return;
        if (!g_sdk->renderer) return;

        // Hide pre-6 so the bar isn't a flat zero.
        auto r_sp = player->get_spell(SLOT_R);
        if (!r_sp || r_sp->get_level() == 0) return;

        // Custom-drawn bar over the hero healthbar — the SDK's
        // add_damage_indicator has no color parameter. Geometry mirrors VENP's
        // champion_drawings::BAR_HERO, scaled to window height.
        const float scale = static_cast<float>(g_sdk->renderer->get_window_height()) / 1080.f;
        constexpr float HERO_OX = -45.6f, HERO_OY = 15.5f;
        constexpr float HERO_BW = 106.4f, HERO_BH = 11.5f;
        const float ox = HERO_OX * scale, oy = HERO_OY * scale;
        const float bw = HERO_BW * scale, bh = HERO_BH * scale;

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!hero || !hero->is_valid() || hero->is_dead())
                continue;
            if (hero->get_team_id() == player->get_team_id())
                continue;
            if (!hero->is_visible())
                continue;

            const float dmg = get_r_damage(hero);
            if (dmg <= 0.f) continue;

            const float hp  = hero->get_hp();
            const float mhp = hero->get_max_hp();
            if (mhp <= 0.f || hp <= 0.f) continue;

            const auto hbar = hero->get_health_bar_position();
            // Engine returns (0,0) for off-screen / occluded healthbars.
            if (hbar.x < 1.f && hbar.y < 1.f) continue;

            const float r0 = hp / mhp;
            const float r1 = std::max(0.f, hp - dmg) / mhp;
            if (r0 <= r1) continue;

            const float bl = hbar.x + ox;
            const float bt = hbar.y + oy;
            math::rect rc;
            rc.left   = bl + r1 * bw;
            rc.top    = bt;
            rc.right  = bl + r0 * bw;
            rc.bottom = bt + bh;

            g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
        }
    }

    bool is_valid_target(game_object* unit, float range)
    {
        if (!unit || !unit->is_valid() || unit->is_dead()) return false;
        if (!unit->is_visible() || !unit->is_targetable())  return false;

        if (!g_sdk || !g_sdk->object_manager) return false;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return false;
        if (unit->get_team_id() == player->get_team_id()) return false;

        if (range != FLT_MAX)
        {
            if (player->get_position().distance(unit->get_position()) > range)
                return false;
        }

        return true;
    }

    bool validate_r_instance()
    {
        if (!r_instance || !r_instance->is_valid())
        {
            r_instance = nullptr;
            return false;
        }
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) { r_instance = nullptr; return false; }
        if (r_instance->get_owner() != player) { r_instance = nullptr; return false; }
        return true;
    }

    game_object* get_r_instance()
    {
        if (validate_r_instance())
            return r_instance;
        return nullptr;
    }

    void combo()
    {
        if (!sdk::orbwalker) return;
        if (!sdk::orbwalker->combo()) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        const float combo_e_reach = spell_data::e_range;

        game_object* target = nullptr;

        // Forced TS > hero target > nearest enemy.
        if (sdk::target_selector)
        {
            target = sdk::target_selector->get_forced_target();
            if (!target || !is_valid_target(target, combo_e_reach))
                target = sdk::target_selector->get_hero_target();
        }

        if (!target || !is_valid_target(target, combo_e_reach))
        {
            float best_dist = combo_e_reach;
            for (auto hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero)) continue;
                const float d = player->get_position().distance(hero->get_position());
                if (d < best_dist) { best_dist = d; target = hero; }
            }
        }

        if (!target || !is_valid_target(target)) return;

        const float now_combo = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        const float q_aa_window = 0.15f;
        const bool  waiting_for_q_aa =
            (now_combo - last_q_combo_time < q_aa_window)
            && sdk::orbwalker->is_in_auto_attack_range(player, target);

        const bool target_in_aa_range = sdk::orbwalker->is_in_auto_attack_range(player, target);

        // Discharge already queued — let the orbwalker land the empowered AA
        // instead of layering more spells on top.
        if (target_in_aa_range && is_discharge_ready(player))
            return;

        // Don't return — E can still chain in the same tick.
        if (menu::use_q_combo && target_in_aa_range && !waiting_for_q_aa)
        {
            if (cast_q(target))
                last_q_combo_time = now_combo;
        }

        if (menu::use_e_combo && !waiting_for_q_aa)
        {
            auto e_aim = get_best_e_aim_target(target);
            if (e_aim && cast_e(e_aim)) return;
        }

        if (menu::use_q_combo && !target_in_aa_range && cast_q(target))
        {
            last_q_combo_time = now_combo;
            return;
        }
        if (menu::use_w_combo && !anti_gap_w_fired && cast_w(target)) return;

        // Clear a stale r_storm_active latch when particle, buff, and lifetime
        // all agree the storm is gone — otherwise cast_r will never retry.
        const float now_combo_r = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        if (r_storm_active)
        {
            const bool particle_alive = validate_r_instance();
            const bool buff_alive     = is_chaos_storm_buff_active(player);
            const bool lifetime_left  = r_storm_start_t > 0.f &&
                                        (now_combo_r - r_storm_start_t) <= R_STORM_LIFETIME;
            if (!particle_alive && !buff_alive && !lifetime_left)
            {
                r_storm_active   = false;
                r_cast_pos_valid = false;
            }
        }
        if (menu::use_r_combo && !r_storm_active && cast_r(target)) return;
    }

    void harass()
    {
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return;

        const float max_par = player->get_max_par();
        if (max_par <= 0.f) return;
        const float mana_pct = (player->get_par() / max_par) * 100.f;
        if (mana_pct < static_cast<float>(menu::harass_mana_percent)) return;

        const float harass_e_reach = spell_data::e_range;

        game_object* target = nullptr;

        // Forced TS > hero target > nearest enemy.
        if (sdk::target_selector)
        {
            target = sdk::target_selector->get_forced_target();
            if (!target || !is_valid_target(target, harass_e_reach))
                target = sdk::target_selector->get_hero_target();
        }

        if (!target || !is_valid_target(target, harass_e_reach))
        {
            float best_dist = harass_e_reach;
            for (auto hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero)) continue;
                const float d = player->get_position().distance(hero->get_position());
                if (d < best_dist) { best_dist = d; target = hero; }
            }
        }

        if (!target || !is_valid_target(target)) return;

        if (!sdk::orbwalker) return;

        if (is_casting()) return;

        const float now_harass = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        const float q_aa_window = 0.15f;
        const bool  waiting_for_q_aa =
            (now_harass - last_q_harass_time < q_aa_window)
            && sdk::orbwalker->is_in_auto_attack_range(player, target);

        const bool target_in_aa_range = sdk::orbwalker->is_in_auto_attack_range(player, target);

        if (target_in_aa_range && is_discharge_ready(player))
            return;

        // Q a nearby minion (preferring lasthittable ones) to charge Discharge
        // for the next AA on the hero target.
        if (menu::use_q_harass && target_in_aa_range && !waiting_for_q_aa)
        {
            auto q = player->get_spell(SLOT_Q);
            if (q && is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
            {
                const float q_range = q->get_cast_range();
                const auto  ppos = player->get_position();
                const auto  tpos = target->get_position();

                game_object* best_minion = nullptr;
                float        best_score = FLT_MAX;

                for (auto minion : g_sdk->object_manager->get_minions())
                {
                    if (!minion || !minion->is_valid() || minion->is_dead() || !minion->is_targetable()) continue;
                    if (minion->is_plant()) continue;
                    if (minion->get_team_id() == player->get_team_id()) continue;

                    const float dist = ppos.distance(minion->get_position());
                    if (dist > q_range) continue;

                    const float dist_to_target = minion->get_position().distance(tpos);
                    float score = dist_to_target;

                    const float q_dmg = get_q_damage(minion);
                    float hp = minion->get_hp();
                    if (sdk::health_prediction && g_sdk->clock_facade)
                    {
                        float predicted_hp = sdk::health_prediction->get_predicted_health(minion, now_harass + spell_data::q_cast_time);
                        if (predicted_hp > 0.f && predicted_hp <= minion->get_max_hp() && predicted_hp <= hp)
                            hp = predicted_hp;
                    }

                    if (hp > 0.f && hp <= q_dmg)
                        score -= 1000.f;

                    if (score < best_score)
                    {
                        best_score = score;
                        best_minion = minion;
                    }
                }

                if (best_minion && cast_q(best_minion))
                {
                    last_q_harass_time = now_harass;
                    return;
                }
            }
        }

        if (menu::use_e_harass && !waiting_for_q_aa)
        {
            auto e_aim = get_best_e_aim_target(target);
            if (e_aim) cast_e(e_aim);
        }

        if (waiting_for_q_aa && target_in_aa_range)
        {
            if (sdk::orbwalker->is_in_auto_attack_range(player, target))
                sdk::orbwalker->attack(target);
            return;
        }

        if (menu::use_q_harass && !target_in_aa_range) cast_q(target);
        if (menu::use_w_harass) cast_w(target);
    }

    void lasthit()
    {
        if (!menu::lasthit_q) return;

        // Honor the mode-specific spellfarm toggle.
        if (sdk::orbwalker)
        {
            if (sdk::orbwalker->clear())
            {
                if (!sdk::orbwalker->is_spell_farm_enabled()) return;
            }
            else if (sdk::orbwalker->lasthit())
            {
                if (!sdk::orbwalker->is_spell_farm_lasthit_enabled()) return;
            }
        }

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        if (!is_spell_learned(SLOT_Q)) return;
        if (!is_spell_ready(SLOT_Q)) return;
        if (!has_enough_mana(SLOT_Q)) return;

        auto q = player->get_spell(SLOT_Q);
        if (!q) return;

        const float q_range = q->get_cast_range();
        const float now = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;

        game_object* best = nullptr;
        float        best_score = FLT_MAX;

        for (auto minion : g_sdk->object_manager->get_minions())
        {
            float score = 0.f;
            if (!is_q_lasthittable(minion, player, q_range, now, score)) continue;
            if (score < best_score) { best_score = score; best = minion; }
        }

        if (best)
        {
            if (sdk::orbwalker && !sdk::orbwalker->can_spell(best, spell_data::q_cast_time))
                return;

            if (cast_q(best))
                last_q_lasthit_time = now;
        }
    }

    void clear()
    {
        lasthit();

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead() || !sdk::orbwalker) return;

        if (is_casting()) return;

        const bool fast_clear_mode = sdk::orbwalker->fast_clear();

        if (!fast_clear_mode && !sdk::orbwalker->is_spell_farm_enabled()) return;

        if (!fast_clear_mode)
        {
            const float max_par = player->get_max_par();
            if (max_par <= 0.f) return;
            const float mana_pct = (player->get_par() / max_par) * 100.f;
            if (mana_pct < static_cast<float>(menu::farm_mana_percent)) return;
        }

        if (menu::use_q_clear)
        {
                if (is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q))
            {
                auto q = player->get_spell(SLOT_Q);
                if (!q) return;
                const float q_range = q->get_cast_range();
                const float now_q = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;

                game_object* best = nullptr;
                float        best_score = FLT_MAX;

                for (auto m : g_sdk->object_manager->get_minions())
                {
                    float score = 0.f;
                    if (!is_q_lasthittable(m, player, q_range, now_q, score)) continue;
                    if (score < best_score) { best_score = score; best = m; }
                }

                if (menu::use_farm_monsters)
                {
                    for (auto m : g_sdk->object_manager->get_monsters())
                    {
                        float score = 0.f;
                        if (!is_q_lasthittable(m, player, q_range, now_q, score)) continue;
                        if (score < best_score) { best_score = score; best = m; }
                    }
                }

                if (best)
                {
                            if (sdk::orbwalker && !sdk::orbwalker->can_spell(best, spell_data::q_cast_time))
                        return;

                    cast_q(best);
                }
            }
        }

        if (!menu::use_e_farm) return;

        if (!is_spell_learned(SLOT_E)) return;
        if (!is_spell_ready(SLOT_E)) return;
        if (!has_enough_mana(SLOT_E)) return;

        if (!sdk::prediction) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        const float farm_e_reach = spell_data::e_range;

        pred_sdk::spell_data spell{};
        spell.spell_type = pred_sdk::spell_type::linear;
        spell.targetting_type = pred_sdk::targetting_type::center;
        spell.spell_slot = SLOT_E;
        spell.range = spell_data::e_range;
        spell.radius = spell_data::e_radius;
        spell.delay = spell_data::e_delay;
        spell.projectile_speed = spell_data::e_speed;
        spell.expected_hitchance = pred_sdk::hitchance::low;

        const auto ppos = player->get_position();
        auto pt_seg_dist = [](const math::vector3& p,
            const math::vector3& a,
            const math::vector3& b) -> float
            {
                const float dx = b.x - a.x, dz = b.z - a.z;
                const float len_sq = dx * dx + dz * dz;
                if (len_sq <= 0.f) return p.distance(a);
                const float t = std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / len_sq, 0.f, 1.f);
                const float ex = p.x - (a.x + t * dx);
                const float ez = p.z - (a.z + t * dz);
                return std::sqrt(ex * ex + ez * ez);
            };

        const int player_team = player->get_team_id();
        auto& targets = scratch::farm_targets();
        for (auto m : g_sdk->object_manager->get_minions())
        {
            if (!is_valid_farm_target(m, player_team)) continue;
            if (!venplus::db::minions::is_lane_minion(venplus::classify_unit(m))) continue;
            if (ppos.distance(m->get_position()) > farm_e_reach) continue;
            targets.push_back(m);
        }

        bool has_monster = false;
        if (menu::use_farm_monsters)
        {
            for (auto m : g_sdk->object_manager->get_monsters())
            {
                if (!is_valid_farm_target(m, player_team)) continue;
                if (!m->is_visible()) continue;
                const auto cls = venplus::classify_unit(m);
                if (!venplus::db::minions::is_jungle(cls)) continue;
                if (ppos.distance(m->get_position()) > farm_e_reach) continue;
                targets.push_back(m);
                if (cls == venplus::unit_class::monster_epic
                 || cls == venplus::unit_class::monster_large) has_monster = true;
            }
        }

        const int min_needed = has_monster ? 1 : menu::e_farm_min_minions;
        if (static_cast<int>(targets.size()) < min_needed) return;

        const float now_farm_pred = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        const float e_delay_time = spell_data::e_delay;
        const float first_click_f = spell_data::e_first_click;
        const float beam_len_f = spell_data::e_beam_len;

        auto& target_e_dmg = scratch::farm_e_dmg();
        target_e_dmg.reserve(targets.size());
        for (auto t : targets)
            target_e_dmg.push_back(get_e_damage(t));

        // Record the actual (fp, cp) pair — reconstructing geometry from a
        // single aim point would turn perpendicular sweeps into diagonals.
        math::vector3 best_fp{}, best_cp{};
        int           best_hit = 0;
        int           best_killable = 0;

        auto count_beam = [&](const math::vector3& fp, const math::vector3& cp,
            int& out_hit, int& out_killable)
            {
                out_hit = 0;
                out_killable = 0;
                for (size_t i = 0; i < targets.size(); ++i)
                {
                    auto other = targets[i];
                    if (!other || !other->is_valid()) continue;
                    const float hit_r = spell_data::e_radius + venplus::unit_gameplay_radius(other);
                    if (pt_seg_dist(other->get_position(), fp, cp) <= hit_r)
                    {
                        ++out_hit;
                        float hp = other->get_hp();
                        if (sdk::health_prediction && g_sdk->clock_facade)
                        {
                            const float prediction_time = now_farm_pred + e_delay_time;
                            float predicted_hp = sdk::health_prediction->get_predicted_health(other, prediction_time);
                            if (predicted_hp > 0.f && predicted_hp <= other->get_max_hp() && predicted_hp <= hp
                                && (hp - predicted_hp) > prediction_constants::min_hp_difference)
                                hp = predicted_hp;
                        }
                        if (hp > 0.f && hp <= target_e_dmg[i])
                            ++out_killable;
                    }
                }
            };

        auto try_update_best = [&](int hit, int killable,
            const math::vector3& fp, const math::vector3& cp)
            {
                if (killable > best_killable || (killable == best_killable && hit > best_hit))
                {
                    best_killable = killable;
                    best_hit      = hit;
                    best_fp       = fp;
                    best_cp       = cp;
                }
            };

        // Per-target seed beams.
        for (auto seed : targets)
        {
            pred_sdk::pred_data pred{};
            try { pred = sdk::prediction->predict(seed, spell); }
            catch (...) { continue; }
            if (!pred.is_valid) continue;

            const float pred_dist = distance_2d(ppos, pred.cast_position);
            const float fp_buf    = 30.f + pred_dist * 0.08f;
            const float fp_dist   = std::clamp(pred_dist - fp_buf, 0.f, first_click_f);
            const auto  dir_pred  = direction_2d(ppos, pred.cast_position);
            const auto  fp        = ppos + dir_pred * fp_dist;
            const auto  cp        = fp + dir_pred * beam_len_f;

            int hit, killable;
            count_beam(fp, cp, hit, killable);
            try_update_best(hit, killable, fp, cp);
        }

        // Centroid + horizontal sweeps.
        if (targets.size() >= 2)
        {
            math::vector3 centroid{};
            for (auto t : targets)
            {
                const auto tp = t->get_position();
                centroid.x += tp.x;
                centroid.y += tp.y;
                centroid.z += tp.z;
            }
            const float inv_n = 1.f / static_cast<float>(targets.size());
            centroid.x *= inv_n;
            centroid.y *= inv_n;
            centroid.z *= inv_n;

            const auto  to_c     = direction_2d(ppos, centroid);
            const float c_dist   = distance_2d(ppos, centroid);
            const float c_fp_buf = 30.f + c_dist * 0.08f;
            const float c_fp_dist = std::clamp(c_dist - c_fp_buf, 0.f, first_click_f);

            // Centroid-directed beam.
            {
                const auto c_fp = ppos + to_c * c_fp_dist;
                const auto c_cp = c_fp + to_c * beam_len_f;
                int hit, killable;
                count_beam(c_fp, c_cp, hit, killable);
                try_update_best(hit, killable, c_fp, c_cp);
            }

            // Perpendicular sweeps — catches waves lined up sideways relative
            // to Viktor. Sampled at three fp distances along the player→centroid
            // line so the bar lands wherever the wave actually is.
            {
                const math::vector3 perp_left  = { -to_c.z, 0.f,  to_c.x };
                const math::vector3 perp_right = {  to_c.z, 0.f, -to_c.x };

                const float fp_samples[3] = {
                    std::clamp(150.f,             0.f, first_click_f),
                    std::clamp(c_fp_dist * 0.5f,  0.f, first_click_f),
                    c_fp_dist,
                };

                for (float h_fp_dist : fp_samples)
                {
                    const auto h_fp = ppos + to_c * h_fp_dist;
                    for (int side = 0; side < 2; ++side)
                    {
                        const auto& perp = (side == 0) ? perp_left : perp_right;
                        const auto  h_cp = h_fp + perp * beam_len_f;
                        int hit, killable;
                        count_beam(h_fp, h_cp, hit, killable);
                        try_update_best(hit, killable, h_fp, h_cp);
                    }
                }
            }
        }

        if (best_hit >= min_needed)
        {
            const float now_farm = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
            if (now_farm - last_cast_e < spell_data::e_delay) return;

            player->cast_spell(SLOT_E, best_fp, best_cp);
            last_cast_e = now_farm;
            mark_spell_cast(now_farm);
        }
    }

    void killsteal()
    {
        if (!g_sdk || !g_sdk->object_manager) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return;

        const float now = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero)) continue;

            if (has_anti_burst_buff(hero)) continue;

            // Include shields so KS doesn't overcommit into Riven W / Karma E etc.
            const float base_hp = effective_hp(hero);

            if (menu::ks_q)
            {
                float hp = base_hp;
                if (sdk::health_prediction && g_sdk->clock_facade)
                {
                    const float prediction_time = now + spell_data::q_cast_time + 0.1f;
                    float predicted_hp = sdk::health_prediction->get_predicted_health(hero, prediction_time);
                    if (predicted_hp > 0.f && predicted_hp <= hero->get_max_hp())
                        hp = predicted_hp + hero->get_all_shield()
                                          + hero->get_physical_shield()
                                          + hero->get_magical_shield();
                }
                if (hp > 0.f && hp <= get_q_damage(hero))
                    if (cast_q(hero)) return;
            }

            if (menu::ks_e)
            {
                float hp = base_hp;
                if (sdk::health_prediction)
                {
                    const float p = sdk::health_prediction->get_predicted_health(hero, now + spell_data::e_delay);
                    if (p > 0.f && p <= hero->get_max_hp())
                        hp = p + hero->get_all_shield()
                              + hero->get_physical_shield()
                              + hero->get_magical_shield();
                }
                if (hp > 0.f && hp <= get_e_damage(hero))
                {
                    if (cast_e(hero)) return;
                }
            }

            if (menu::ks_r)
            {
                float hp = base_hp;
                if (sdk::health_prediction)
                {
                    const float p = sdk::health_prediction->get_predicted_health(hero, now + spell_data::r_cast_time);
                    if (p > 0.f && p <= hero->get_max_hp())
                        hp = p + hero->get_all_shield()
                              + hero->get_physical_shield()
                              + hero->get_magical_shield();
                }
                if (hp > 0.f && hp <= get_r_damage(hero))
                    if (cast_r(hero)) return;
            }
        }
    }

    void auto_r()
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return;

        // Particle / on_process_cast flag / buff — any of the three suffices.
        // The buff path is the only one that survives manual R casts where the
        // particle owner check loses the race.
        auto r_obj = get_r_instance();
        const bool buff_active = is_chaos_storm_buff_active(player);
        if (!r_obj && !r_storm_active && !buff_active) return;

        if (buff_active && !r_storm_active)
        {
            r_storm_active = true;
            // We missed the spawn — derive start time from the buff's
            // remaining duration so the dying-storm gate still trips.
            // Fall back to "halfway through lifetime" if the buff doesn't
            // expose a usable end time.
            const float now_buff = g_sdk->clock_facade->get_game_time();
            float inferred_start = now_buff - R_STORM_LIFETIME * 0.5f;
            std::string cs_name = buffs::CHAOS_STORM;
            if (auto* b = player->get_buff_by_name(cs_name); b && b->is_active())
            {
                const float end = b->get_end_time();
                if (end > now_buff)
                    inferred_start = end - R_STORM_LIFETIME;
            }
            r_storm_start_t = inferred_start;
        }
        if (!is_spell_learned(SLOT_R)) return;

        const float now = g_sdk->clock_facade->get_game_time();

        if (now - r_storm_start_t < spell_data::r_cast_time + 0.1f) return;

        // Don't move a dying storm — by the time the recast resolves and the
        // storm crawls there, it'll be gone.
        if (r_storm_start_t > 0.f)
        {
            const float elapsed = now - r_storm_start_t;
            if (elapsed > (R_STORM_LIFETIME - R_STOP_MOVING_AT))
                return;
        }

        if (now - last_r_move_time < spell_data::r_move_rate) return;

        // r_instance > r_cast_pos > player position.
        const auto storm_pos = r_obj           ? r_obj->get_position()
                              : r_cast_pos_valid ? r_cast_pos
                                                 : player->get_position();

        // Forced TS overrides multi-target scoring.
        if (sdk::target_selector)
        {
            game_object* forced = sdk::target_selector->get_forced_target();
            if (forced && is_valid_target(forced))
            {
                move_r_to_target(forced);
                return;
            }
        }

        // Filter invuln / spell-immune / stasis — storm can't tag them.
        auto& enemies = scratch::storm_enemies();
        enemies.reserve(5);

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero)) continue;
            if (hero->has_buff_of_type(buff_type::invulnerability)) continue;
            if (hero->has_buff_of_type(buff_type::spell_immunity))  continue;
            if (sdk::common && sdk::common->buffs)
            {
                auto stasis = sdk::common->buffs->get_stasis_buff(hero);
                if (stasis && stasis->is_active()) continue;
            }
            if (storm_pos.distance(hero->get_position()) <= R_SEARCH_RADIUS)
                enemies.push_back(hero);
        }

        if (enemies.empty())
        {
            // Fallback retarget: if the current focus died or left storm search
            // radius, move toward the nearest valid enemy so R does not stall.
            game_object* nearest_enemy = nullptr;
            float nearest_dist = FLT_MAX;

            for (auto hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero)) continue;
                if (hero->has_buff_of_type(buff_type::invulnerability)) continue;
                if (hero->has_buff_of_type(buff_type::spell_immunity))  continue;
                if (sdk::common && sdk::common->buffs)
                {
                    auto stasis = sdk::common->buffs->get_stasis_buff(hero);
                    if (stasis && stasis->is_active()) continue;
                }

                const float d = storm_pos.distance(hero->get_position());
                if (d < nearest_dist)
                {
                    nearest_dist = d;
                    nearest_enemy = hero;
                }
            }

            if (nearest_enemy)
                move_r_to_target(nearest_enemy);

            return;
        }

        // Lead by base lag + storm travel to current cluster centroid.
        math::vector3 raw_centroid{};
        for (auto h : enemies)
        {
            const auto p = h->get_position();
            raw_centroid.x += p.x; raw_centroid.y += p.y; raw_centroid.z += p.z;
        }
        const float inv_n = 1.f / static_cast<float>(enemies.size());
        raw_centroid.x *= inv_n; raw_centroid.y *= inv_n; raw_centroid.z *= inv_n;
        const float storm_travel = storm_pos.distance(raw_centroid) / R_STORM_SPEED;
        const float lead_time    = R_PRED_BASE_DELAY + std::min(storm_travel, 1.5f);

        struct enemy_pred {
            game_object*   obj;
            math::vector3  pos;
            bool           ccd;
            float          weight;
        };
        std::array<enemy_pred, 16> preds{};
        size_t pred_n = 0;
        for (auto h : enemies)
        {
            if (pred_n >= preds.size()) break;
            const auto p   = h->get_position();
            const auto v   = h->get_velocity();
            const bool cc  = is_hard_cc(h);
            math::vector3 pp = cc
                ? p
                : math::vector3{ p.x + v.x * lead_time, p.y, p.z + v.z * lead_time };

            // 1.0 base + 0.6 CC (eats all ticks) + 0.3 low-HP (KS).
            float w = 1.f;
            if (cc) w += 0.6f;
            const float maxhp = h->get_max_hp();
            if (maxhp > 0.f && (h->get_hp() / maxhp) < 0.25f) w += 0.3f;
            preds[pred_n++] = { h, pp, cc, w };
        }
        if (pred_n == 0) return;

        const float hit_r = spell_data::r_radius * R_HIT_RADIUS_FRAC;
        const float hit_r_sq = hit_r * hit_r;
        auto score_pos = [&](const math::vector3& cp) -> float
        {
            float s = 0.f;
            for (size_t i = 0; i < pred_n; ++i)
            {
                const float dx = cp.x - preds[i].pos.x;
                const float dz = cp.z - preds[i].pos.z;
                if (dx * dx + dz * dz <= hit_r_sq)
                    s += preds[i].weight;
            }
            return s;
        };

        // Candidates: each enemy's predicted pos + pairwise midpoints + centroid.
        math::vector3 best_pos = preds[0].pos;
        float         best_score = -1.f;

        auto try_candidate = [&](const math::vector3& cp)
        {
            const float s = score_pos(cp);
            if (s > best_score) { best_score = s; best_pos = cp; }
        };

        for (size_t i = 0; i < pred_n; ++i)
            try_candidate(preds[i].pos);
        for (size_t i = 0; i < pred_n; ++i)
            for (size_t j = i + 1; j < pred_n; ++j)
            {
                const math::vector3 mid = {
                    (preds[i].pos.x + preds[j].pos.x) * 0.5f,
                    (preds[i].pos.y + preds[j].pos.y) * 0.5f,
                    (preds[i].pos.z + preds[j].pos.z) * 0.5f
                };
                try_candidate(mid);
            }
        try_candidate(raw_centroid);

        if (best_score < 1.f) return;

        // Decay stale hysteresis. If the previous move's justification has
        // weakened (target died / left / lost CC), re-score last_r_move_pos
        // against the current enemy set and pull last_r_move_score down to
        // match. Without this, the storm gets stuck near a dead high-weight
        // target because no nearby live target can clear the score gate.
        const float current_score_at_last = score_pos(last_r_move_pos);
        if (current_score_at_last < last_r_move_score)
            last_r_move_score = current_score_at_last;

        move_r_to_pos(best_pos, best_score, /*force=*/false);
    }

    void auto_w_on_cc()
    {
        if (!menu::auto_w_on_cc) return;
        if (!g_sdk || !g_sdk->object_manager) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        if (!is_spell_learned(SLOT_W)) return;
        if (!is_spell_ready(SLOT_W)) return;
        if (!has_enough_mana(SLOT_W)) return;

        if (g_sdk->clock_facade)
        {
            const float now = g_sdk->clock_facade->get_game_time();
            if (now - m_last_spell_cast_time < MIN_SPELL_DELAY) return;
        }

        const auto  ppos = player->get_position();
        const float w_range = spell_data::w_range;

        game_object* best_target = nullptr;
        float        best_priority = 0.f;

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero, w_range)) continue;
            if (!is_hard_cc(hero)) continue;

            // Closer + higher HP = better target.
            const float dist = ppos.distance(hero->get_position());
            const float priority = hero->get_hp() / (dist + 1.f);

            if (priority > best_priority)
            {
                best_priority = priority;
                best_target = hero;
            }
        }

        if (!best_target) return;

        // CC'd target can't move — cast directly on them.
        const auto cast_pos = best_target->get_position();
        if (ppos.distance(cast_pos) > w_range) return;

        if (!sdk::common) player->cast_spell(SLOT_W, cast_pos);
        else              sdk::common->cast_spell(SLOT_W, cast_pos);
        if (g_sdk->clock_facade)
            mark_spell_cast(g_sdk->clock_facade->get_game_time());
    }

    void anti_gap_w()
    {
        if (!menu::use_w_antigap) return;
        if (!g_sdk || !g_sdk->object_manager) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        if (!is_spell_learned(SLOT_W)) return;
        if (!is_spell_ready(SLOT_W)) return;
        if (!has_enough_mana(SLOT_W)) return;

        const auto  ppos = player->get_position();
        const float cast_range = spell_data::w_range;

        game_object* best_target = nullptr;
        float         best_threat = 0.f;
        math::vector3 best_cast_pos{};

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero)) continue;

            {
                const auto it = menu::w_antigap_whitelist.find(hero->get_char_name());
                if (it != menu::w_antigap_whitelist.end() && !it->second) continue;
            }

            const auto  epos = hero->get_position();
            const float dist = ppos.distance(epos);
            // Field radius extends effective reach past the center cast range.
            if (dist > cast_range + spell_data::w_radius) continue;
            if (dist < W_MIN_DIST) continue;

            const auto  vel = hero->get_velocity();
            const float vel_x = vel.x, vel_z = vel.z;
            const float speed = std::sqrt(vel_x * vel_x + vel_z * vel_z);

            const float to_vik_x = ppos.x - epos.x;
            const float to_vik_z = ppos.z - epos.z;
            const float inv_dist = 1.f / (dist + 0.001f);
            const float approach_sp = (vel_x * to_vik_x + vel_z * to_vik_z) * inv_dist;

            const bool is_dash = hero->is_dashing();

            if (is_dash)
            {
                if (approach_sp <= 0.f) continue;
            }
            else
            {
                if (approach_sp < menu::anti_gap_min_speed) continue;
            }

            math::vector3 pred_pos;
            const float prediction_time = spell_data::w_activation_delay + 0.1f;

            if (is_dash && speed > 0.f)
            {
                const float ds = hero->get_dash_speed();
                const float scale = (ds > 0.f ? ds : speed) / speed;
                pred_pos = {
                    epos.x + vel_x * scale * prediction_time,
                    epos.y,
                    epos.z + vel_z * scale * prediction_time
                };
            }
            else
            {
                pred_pos = {
                    epos.x + vel_x * prediction_time,
                    epos.y,
                    epos.z + vel_z * prediction_time
                };
            }

            math::vector3 cast_pos;

            if (is_dash)
            {
                auto path = hero->get_path();
                if (!path.empty())
                {
                    const auto dash_end = path.back();
                    const float dash_end_dist = ppos.distance(dash_end);
                    const float dash_remaining = epos.distance(dash_end);
                    const float dash_spd = hero->get_dash_speed();
                    const float dash_time = (dash_spd > 0.01f) ? (dash_remaining / dash_spd) : 999.f;

                    if (dash_time < spell_data::w_activation_delay && dash_end_dist < DASH_NEAR_DIST)
                    {
                        // Dasher lands on us before W activates — place W on self.
                        cast_pos = ppos;
                    }
                    else if (dash_end_dist <= cast_range)
                    {
                        cast_pos = dash_end;
                    }
                    else
                    {
                        const auto dir_to_dash = (dash_end - ppos).to_direction();
                        cast_pos = ppos + dir_to_dash * std::min(cast_range, dash_end_dist * 0.6f);
                    }
                }
                else
                {
                    const float pred_dist = ppos.distance(pred_pos);
                    if (pred_dist > cast_range)
                    {
                        const float inv_pd = cast_range / (pred_dist + 0.001f);
                        cast_pos = {
                            ppos.x + (pred_pos.x - ppos.x) * inv_pd,
                            pred_pos.y,
                            ppos.z + (pred_pos.z - ppos.z) * inv_pd
                        };
                    }
                    else
                    {
                        const auto dir_to_pred = (pred_pos - ppos).to_direction();
                        cast_pos = ppos + dir_to_pred * (pred_dist * 0.6f);
                    }
                }
            }
            else
            {
                // Walkers — drop W between us and their predicted path so they
                // have to walk through it.
                const float pred_dist = ppos.distance(pred_pos);
                const auto dir_to_pred = (pred_pos - ppos).to_direction();

                const float placement_dist = std::min(cast_range, pred_dist * 0.5f);
                cast_pos = ppos + dir_to_pred * placement_dist;

                const float cast_dist = ppos.distance(cast_pos);
                if (cast_dist < W_MIN_PLACEMENT && pred_dist > W_MIN_PLACEMENT)
                {
                    cast_pos = ppos + dir_to_pred * W_MIN_PLACEMENT;
                }
            }

            const float threat = is_dash ? (approach_sp + 10000.f) : approach_sp;
            if (threat > best_threat)
            {
                best_threat = threat;
                best_target = hero;
                best_cast_pos = cast_pos;
            }
        }

        if (!best_target) return;

        // Final range check and clamping - ensure W is always within cast range
        const auto ppos_final = player->get_position();
        const float final_cast_dist = ppos_final.distance(best_cast_pos);
        if (final_cast_dist > cast_range)
        {
            // Clamp to max range
            const auto dir_final = (best_cast_pos - ppos_final).to_direction();
            best_cast_pos = ppos_final + dir_final * cast_range;
        }

        if (menu::min_enemies_for_w > 1)
        {
            int count = 0;
            for (auto hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero)) continue;
                if (best_cast_pos.distance(hero->get_position()) <= spell_data::w_radius)
                    ++count;
            }
            if (count < menu::min_enemies_for_w) return;
        }

        if (!sdk::common) player->cast_spell(SLOT_W, best_cast_pos);
        else              sdk::common->cast_spell(SLOT_W, best_cast_pos);
        if (g_sdk->clock_facade)
            mark_spell_cast(g_sdk->clock_facade->get_game_time());
        anti_gap_w_fired = true;
    }

    void stasis_e()
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
        if (!sdk::common || !sdk::common->buffs) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        if (!is_spell_learned(SLOT_E)) return;
        if (!is_spell_ready(SLOT_E)) return;
        if (!has_enough_mana(SLOT_E)) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_cast_e < spell_data::e_delay) return;
        if (now - m_last_spell_cast_time < MIN_SPELL_DELAY) return;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return;

        const auto  ppos = player->get_position();
        const float e_reach = spell_data::e_range;
        const float first_click = spell_data::e_first_click;
        const float beam_len = spell_data::e_beam_len;

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!hero || !hero->is_valid() || hero->is_dead()) continue;
            if (hero->get_team_id() == player->get_team_id()) continue;

            const auto  hpos = hero->get_position();
            const float dist = distance_2d(ppos, hpos);
            if (dist > e_reach) continue;

            auto stasis = sdk::common->buffs->get_stasis_buff(hero);
            if (!stasis || !stasis->is_active()) continue;

            const float end_time = stasis->get_end_time();
            const float remaining = end_time - now;
            if (remaining <= 0.f) continue;

            const float fp_d = std::clamp(dist - beam_len * 0.5f, 0.f, first_click);
            const float beam_travel = std::max(0.f, dist - fp_d) / spell_data::e_speed;
            const float total_time = spell_data::e_delay + beam_travel;

            if (remaining >= (total_time - 0.05f) && remaining <= (total_time + 0.15f))
            {
                const auto dir = direction_2d(ppos, hpos);
                const auto fp = ppos + dir * fp_d;
                const auto cp = fp + dir * beam_len;

                player->cast_spell(SLOT_E, fp, cp);
                last_cast_e = now;
                mark_spell_cast(now);
                return;
            }
        }
    }

    bool cast_q(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target) return false;

        if (!target->is_valid() || target->is_dead() || !target->is_targetable())
            return false;
        if (target->is_monster() && !target->is_visible()) return false;
        if (target->has_buff_of_type(buff_type::spell_shield)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - m_last_spell_cast_time < MIN_SPELL_DELAY) return false;

        if (!is_spell_learned(SLOT_Q)) return false;
        if (!is_spell_ready(SLOT_Q))   return false;
        if (!has_enough_mana(SLOT_Q))  return false;

        auto q = player->get_spell(SLOT_Q);
        if (!q) return false;

        // Include bounding radii — matches the engine's actual range check.
        const auto  ppos = player->get_position();
        const auto  tpos = target->get_position();
        const float dist = ppos.distance(tpos);

        float range = q->get_cast_range();
        const float extra_range =
            player->get_bounding_radius() +
            target->get_bounding_radius();

        if (dist > range + extra_range) return false;

        bool result = false;
        if (!sdk::common) { player->cast_spell(SLOT_Q, target); result = true; }
        else result = sdk::common->cast_spell(SLOT_Q, target);

        if (result)
            m_last_spell_cast_time = now;

        return result;
    }

    bool cast_w(game_object* target, bool force)
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target) return false;

        if (!target->is_valid() || target->is_dead() || !target->is_targetable())
            return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - m_last_spell_cast_time < MIN_SPELL_DELAY) return false;

        if (!is_spell_learned(SLOT_W)) return false;
        if (!is_spell_ready(SLOT_W))   return false;
        if (!has_enough_mana(SLOT_W))  return false;

        auto w = player->get_spell(SLOT_W);
        if (!w) return false;
        // Field extends w_radius past center cast point — targets up to
        // (w_range + w_radius) are still hittable with center clamped to range.
        const float w_dist = player->get_position().distance(target->get_position());
        if (w_dist > spell_data::w_range + spell_data::w_radius)
        { return false; }

        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        if (!sdk::prediction)
        {
            // Clamp to w_range — center past range gets dropped even though
            // the field extends w_radius further.
            const auto ppos_np = player->get_position();
            math::vector3 cast_np = target->get_position();
            const float dist_np = ppos_np.distance(cast_np);
            if (dist_np > spell_data::w_range)
                cast_np = ppos_np + (cast_np - ppos_np).to_direction() * spell_data::w_range;

            bool ok_np = false;
            if (!sdk::common) { player->cast_spell(SLOT_W, cast_np); ok_np = true; }
            else              { ok_np = sdk::common->cast_spell(SLOT_W, cast_np); }
            if (ok_np) mark_spell_cast(now);
            return ok_np;
        }

        const auto  tpos = target->get_position();
        const auto  tvel = target->get_velocity();
        const float speed_sq = tvel.x * tvel.x + tvel.z * tvel.z;
        const bool  is_moving = speed_sq > (50.f * 50.f);
        const bool  is_dash = target->is_dashing();

        math::vector3 cast_pos = tpos;

        if (sdk::prediction)
        {
            pred_sdk::spell_data spell{};
            spell.spell_type = pred_sdk::spell_type::circular;
            spell.targetting_type = pred_sdk::targetting_type::center;
            spell.spell_slot = SLOT_W;
            spell.range = spell_data::w_range;
            spell.radius = spell_data::w_radius;
            spell.delay = spell_data::w_activation_delay + 0.15f;
            spell.projectile_speed = FLT_MAX;
            spell.expected_hitchance = menu::w_hitchance;

            pred_sdk::pred_data pred{};
            try
            {
                pred = sdk::prediction->predict(target, spell);
            }
            catch (...)
            {
                pred.is_valid = false;
            }
            if (pred.is_valid && pred.hitchance >= spell.expected_hitchance)
            {
                cast_pos = pred.cast_position;
            }
            else if (is_moving || is_dash)
            {
                // Manual lead — SDK rejected the prediction but the target's
                // moving, so we lead in their velocity direction.
                const float speed = std::sqrt(speed_sq);
                if (speed > 0.01f)
                {
                    const float lead_time = spell_data::w_activation_delay + 0.2f;
                    math::vector3 predicted;

                    if (is_dash)
                    {
                        auto path = target->get_path();
                        if (!path.empty())
                        {
                            predicted = path.back();
                            const float dash_dist = tpos.distance(predicted);
                            if (dash_dist > 0.01f)
                            {
                                const float dash_speed = target->get_dash_speed();
                                const float dash_time = dash_speed > 0.01f ? (dash_dist / dash_speed) : lead_time;

                                // Dash lands before W activates — predict post-dash drift too.
                                if (dash_time < lead_time)
                                {
                                    const auto dash_dir = (predicted - tpos).to_direction();
                                    const float remaining_time = lead_time - dash_time;
                                    const float post_dash_speed = speed > 0.01f ? speed : 300.f;
                                    predicted = predicted + dash_dir * (post_dash_speed * remaining_time);
                                }
                            }
                        }
                        else
                        {
                            predicted = {
                                tpos.x + tvel.x * lead_time,
                                tpos.y,
                                tpos.z + tvel.z * lead_time
                            };
                        }
                    }
                    else
                    {
                        predicted = {
                            tpos.x + tvel.x * lead_time,
                            tpos.y,
                            tpos.z + tvel.z * lead_time
                        };
                    }

                    const auto ppos = player->get_position();
                    const float pred_dist = ppos.distance(predicted);
                    if (pred_dist <= spell_data::w_range)
                    {
                        // Slow targets — half-lead so the W doesn't overshoot
                        // when their velocity barely indicates direction.
                        const float current_dist = ppos.distance(tpos);
                        if (speed < 200.f && pred_dist > current_dist + 100.f)
                        {
                            const float blend = 0.5f;
                            cast_pos = {
                                tpos.x + (predicted.x - tpos.x) * blend,
                                tpos.y,
                                tpos.z + (predicted.z - tpos.z) * blend
                            };
                        }
                        else
                        {
                            cast_pos = predicted;
                        }
                    }
                    else
                    {
                        const auto dir_to_pred = (predicted - ppos).to_direction();
                        cast_pos = ppos + dir_to_pred * spell_data::w_range;
                    }
                }
            }
        }

        const auto ppos = player->get_position();
        const float cast_dist = ppos.distance(cast_pos);
        if (cast_dist > spell_data::w_range)
        {
            const auto dir = (cast_pos - ppos).to_direction();
            cast_pos = ppos + dir * spell_data::w_range;
        }

        // If single-target placement doesn't hit min_enemies, try the cluster
        // center-of-mass. Semi-manual (force) casts skip this.
        if (!force && menu::min_enemies_for_w > 1)
        {
            int count = 0;
            for (auto hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero)) continue;
                if (cast_pos.distance(hero->get_position()) <= spell_data::w_radius)
                    ++count;
            }

            if (count < menu::min_enemies_for_w)
            {
                // Compute center-of-mass of all enemies within W range (with velocity prediction)
                math::vector3 cluster_center = { 0.f, 0.f, 0.f };
                int cluster_n = 0;

                for (auto hero : g_sdk->object_manager->get_heroes())
                {
                    if (!is_valid_target(hero)) continue;
                    if (ppos.distance(hero->get_position()) > spell_data::w_range) continue;

                    const auto hvel = hero->get_velocity();
                    const float pt = spell_data::w_activation_delay;
                    cluster_center.x += hero->get_position().x + hvel.x * pt;
                    cluster_center.y += hero->get_position().y;
                    cluster_center.z += hero->get_position().z + hvel.z * pt;
                    ++cluster_n;
                }

                if (cluster_n >= menu::min_enemies_for_w)
                {
                    cluster_center.x /= static_cast<float>(cluster_n);
                    cluster_center.y /= static_cast<float>(cluster_n);
                    cluster_center.z /= static_cast<float>(cluster_n);

                    const float cd = ppos.distance(cluster_center);
                    if (cd > spell_data::w_range)
                    {
                        const auto cdir = (cluster_center - ppos).to_direction();
                        cluster_center = ppos + cdir * spell_data::w_range;
                    }

                    int new_count = 0;
                    for (auto hero : g_sdk->object_manager->get_heroes())
                    {
                        if (!is_valid_target(hero)) continue;
                        if (cluster_center.distance(hero->get_position()) <= spell_data::w_radius)
                            ++new_count;
                    }

                    if (new_count >= menu::min_enemies_for_w)
                        cast_pos = cluster_center;
                    else
                        return false;
                }
                else
                {
                    return false;
                }
            }
        }

        bool ok = false;
        if (!sdk::common) { player->cast_spell(SLOT_W, cast_pos); ok = true; }
        else              { ok = sdk::common->cast_spell(SLOT_W, cast_pos); }
        if (ok) mark_spell_cast(now);
        return ok;
    }

    game_object* get_best_e_aim_target(game_object* primary)
    {
        if (!g_sdk || !g_sdk->object_manager) return primary;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return primary;

        const auto  ppos = player->get_position();
        const float e_reach = spell_data::e_range;

        const float first_click = spell_data::e_first_click;
        const float beam_len = spell_data::e_beam_len;

        auto& in_range = scratch::e_in_range();
        in_range.reserve(5);

        constexpr float PRED_HORIZON = 0.5f;

        for (auto hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero, e_reach)) continue;
            if (is_in_stasis(hero)) continue;
            if (is_damage_immune(hero)) continue;

            const auto  hpos = hero->get_position();
            const auto  hvel = hero->get_velocity();
            math::vector3 pred{
                hpos.x + hvel.x * PRED_HORIZON,
                hpos.y,
                hpos.z + hvel.z * PRED_HORIZON
            };

            if (distance_2d(ppos, pred) > e_reach)
                pred = ppos + direction_2d(ppos, pred) * e_reach;

            in_range.push_back({ hero, pred });
        }

        if (in_range.size() <= 1) return primary;

        auto pt_seg_dist = [](const math::vector3& p,
            const math::vector3& a,
            const math::vector3& b) -> float
            {
                const float dx = b.x - a.x, dz = b.z - a.z;
                const float len_sq = dx * dx + dz * dz;
                if (len_sq <= 0.f) return p.distance(a);
                const float t = std::clamp(((p.x - a.x) * dx + (p.z - a.z) * dz) / len_sq, 0.f, 1.f);
                const float ex = p.x - (a.x + t * dx);
                const float ez = p.z - (a.z + t * dz);
                return std::sqrt(ex * ex + ez * ez);
            };

        auto count_beam_hits = [&](const math::vector3& aim_target) -> int
            {
                const float aim_d = distance_2d(ppos, aim_target);
                if (aim_d < 10.f) return 0;

                const auto  d = direction_2d(ppos, aim_target);
                const float ideal_fp = aim_d - beam_len * 0.5f;
                const float fpd = std::clamp(ideal_fp, 0.f, first_click);
                const auto  fp = ppos + d * fpd;
                const auto  cp = fp + d * beam_len;

                int hits = 0;
                for (const auto& info : in_range)
                {
                    if (!info.hero || !info.hero->is_valid()) continue;
                    const float hit_r = spell_data::e_radius + venplus::unit_gameplay_radius(info.hero);
                    if (pt_seg_dist(info.pred_pos, fp, cp) <= hit_r)
                        ++hits;
                }
                return hits;
            };

        // Per-enemy beams.
        int primary_hits = 0;
        bool primary_in_range = false;

        game_object* best_seed = primary;
        int          best_hits = 0;

        for (const auto& seed_info : in_range)
        {
            const int hits = count_beam_hits(seed_info.pred_pos);

            if (seed_info.hero == primary)
            {
                primary_in_range = true;
                primary_hits = hits;
            }

            if (hits > best_hits)
            {
                best_hits = hits;
                best_seed = seed_info.hero;
            }
        }

        // Midpoint + 1/3 / 2/3 blends between pairs — catches angles BETWEEN
        // enemies that the per-enemy beams miss.
        game_object* midpoint_seed = nullptr;
        int          midpoint_best_hits = best_hits;

        for (size_t i = 0; i < in_range.size(); ++i)
        {
            for (size_t j = i + 1; j < in_range.size(); ++j)
            {
                const auto& a = in_range[i];
                const auto& b = in_range[j];

                const math::vector3 mid = {
                    (a.pred_pos.x + b.pred_pos.x) * 0.5f,
                    (a.pred_pos.y + b.pred_pos.y) * 0.5f,
                    (a.pred_pos.z + b.pred_pos.z) * 0.5f
                };

                const int mid_hits = count_beam_hits(mid);
                if (mid_hits > midpoint_best_hits)
                {
                    midpoint_best_hits = mid_hits;
                    const float da = distance_2d(ppos, a.pred_pos);
                    const float db = distance_2d(ppos, b.pred_pos);
                    midpoint_seed = (da <= db) ? a.hero : b.hero;
                }

                const math::vector3 blend_a = {
                    a.pred_pos.x * 0.667f + b.pred_pos.x * 0.333f,
                    a.pred_pos.y * 0.667f + b.pred_pos.y * 0.333f,
                    a.pred_pos.z * 0.667f + b.pred_pos.z * 0.333f
                };
                const math::vector3 blend_b = {
                    a.pred_pos.x * 0.333f + b.pred_pos.x * 0.667f,
                    a.pred_pos.y * 0.333f + b.pred_pos.y * 0.667f,
                    a.pred_pos.z * 0.333f + b.pred_pos.z * 0.667f
                };

                const int hits_a = count_beam_hits(blend_a);
                if (hits_a > midpoint_best_hits)
                {
                    midpoint_best_hits = hits_a;
                    midpoint_seed = a.hero;
                }

                const int hits_b = count_beam_hits(blend_b);
                if (hits_b > midpoint_best_hits)
                {
                    midpoint_best_hits = hits_b;
                    midpoint_seed = b.hero;
                }
            }
        }

        if (midpoint_seed && midpoint_best_hits > best_hits)
        {
            best_hits = midpoint_best_hits;
            best_seed = midpoint_seed;
        }

        // Respect the primary unless the alternative hits significantly more.
        if (primary_in_range)
        {
            constexpr int MIN_HIT_DIFFERENCE = 2;
            if (best_hits <= primary_hits + MIN_HIT_DIFFERENCE)
                return primary;
        }

        return best_seed;
    }

    bool cast_e(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager) return false;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target) return false;

        if (!target->is_valid() || target->is_dead() || !target->is_targetable()) return false;
        if (target->is_monster() && !target->is_visible()) return false;
        if (is_in_stasis(target)) return false;
        if (is_damage_immune(target)) return false;

        if (!g_sdk->clock_facade) return false;
        const float now_e = g_sdk->clock_facade->get_game_time();

        if (now_e - m_last_spell_cast_time < MIN_SPELL_DELAY) return false;
        if (now_e - last_cast_e < spell_data::e_delay) return false;

        if (!is_spell_learned(SLOT_E)) return false;
        if (!is_spell_ready(SLOT_E)) return false;
        if (!has_enough_mana(SLOT_E)) return false;

        const auto  ppos = player->get_position();
        const float e_reach = spell_data::e_range;
        if (distance_2d(ppos, target->get_position()) > e_reach) return false;

        const float first_click = spell_data::e_first_click;
        const float beam_len    = spell_data::e_beam_len;

        const auto  tpos  = target->get_position();
        const float tgt_d = ppos.distance(tpos);
        const auto  vel   = target->get_velocity();
        const float spd_sq = vel.x * vel.x + vel.z * vel.z;

        const bool immobile     = is_hard_cc(target);
        const bool nearly_still = spd_sq < (50.f * 50.f);
        const bool is_dash      = target->is_dashing();

        // Place FP/CP along `dir`. beam_bias controls where the target sits
        // in the beam (0 = start, 1 = end). FP is clamped to first_click.
        auto fire = [&](const math::vector3& aim_pos, const math::vector3& dir,
                        float beam_bias, const char* branch_name) -> bool
        {
            const float dlen_sq = dir.x * dir.x + dir.z * dir.z;
            if (dlen_sq < 0.01f)
            {
                log_e("[E] reject: invalid dir (branch=%s)", branch_name);
                return false;
            }

            const float aim_dist = distance_2d(ppos, aim_pos);
            if (aim_dist < 10.f)
            {
                log_e("[E] reject: aim too close %.1f (branch=%s)", aim_dist, branch_name);
                return false;
            }

            const float clamped_aim = std::min(aim_dist, e_reach);
            const float ideal_fp    = clamped_aim - beam_len * std::clamp(beam_bias, 0.f, 1.f);
            const float fp_dist     = std::clamp(ideal_fp, 0.f, first_click);

            const math::vector3 fp = ppos + dir * fp_dist;
            const math::vector3 cp = fp   + dir * beam_len;

            player->cast_spell(SLOT_E, fp, cp);
            last_cast_e            = now_e;
            m_last_spell_cast_time = now_e;

            if (menu::log_e_casts)
            {
                const float beam_time = beam_len / spell_data::e_speed;
                m_e_log.armed             = true;
                m_e_log.target_net_id     = target->get_network_id();
                m_e_log.target_name       = target->get_char_name();
                m_e_log.fp                = fp;
                m_e_log.cp                = cp;
                m_e_log.eval_time         = now_e + spell_data::e_delay + beam_time;
                m_e_log.tgt_dist_at_cast  = tgt_d;
                m_e_log.tgt_speed_at_cast = std::sqrt(spd_sq);
                m_e_log.branch            = branch_name;

                log_e("[E] CAST tgt=%s d=%.0f vel=%.0f branch=%s fp_d=%.0f cp_d=%.0f",
                      m_e_log.target_name.c_str(), tgt_d, m_e_log.tgt_speed_at_cast,
                      branch_name, fp_dist, fp_dist + beam_len);
            }
            return true;
        };

        // Anti-dodge perpendicular E for close moving targets — guarantees a
        // hit on a 180-strafe where SDK prediction would jitter.
        if (tgt_d < 300.f && !immobile && !nearly_still && !is_dash
            && spd_sq > (100.f * 100.f))
        {
            const float speed = std::sqrt(spd_sq);
            const float perp_x = -vel.z / speed;
            const float perp_z =  vel.x / speed;
            const auto  to_tgt = direction_2d(ppos, tpos);
            const float sign   = (to_tgt.x * perp_x + to_tgt.z * perp_z > 0.f) ? 1.f : -1.f;

            const float offset = spell_data::e_radius + target->get_bounding_radius() + 50.f;
            math::vector3 aim_pos = {
                tpos.x + perp_x * sign * offset,
                tpos.y,
                tpos.z + perp_z * sign * offset
            };
            if (distance_2d(ppos, aim_pos) > e_reach)
                aim_pos = ppos + direction_2d(ppos, aim_pos) * e_reach;

            const auto dir = direction_2d(ppos, aim_pos);
            return fire(aim_pos, dir, 0.5f, "close-perp");
        }

        // Dash endpoint is engine-guaranteed — free hit.
        if (is_dash)
        {
            auto path = target->get_path();
            if (!path.empty())
            {
                math::vector3 dash_end = path.back();
                if (distance_2d(ppos, dash_end) > e_reach)
                    dash_end = ppos + direction_2d(ppos, dash_end) * e_reach;
                const auto dir = direction_2d(ppos, dash_end);
                return fire(dash_end, dir, 0.5f, "dash-end");
            }
        }

        if (immobile || nearly_still)
        {
            const auto dir = direction_2d(ppos, tpos);
            return fire(tpos, dir, 0.5f, immobile ? "cc'd" : "still");
        }

        // spell_type::vector knows about FP→CP geometry. With it, the SDK
        // computes intercept from the actual beam start, not Viktor's position.
        if (!sdk::prediction)
        {
            const float lead_t = spell_data::e_delay + std::max(0.f, tgt_d - first_click) / spell_data::e_speed;
            math::vector3 aim_pos = { tpos.x + vel.x * lead_t, tpos.y, tpos.z + vel.z * lead_t };
            if (distance_2d(ppos, aim_pos) > e_reach)
                aim_pos = ppos + direction_2d(ppos, aim_pos) * e_reach;
            const auto dir = direction_2d(ppos, aim_pos);
            return fire(aim_pos, dir, 0.5f, "no-pred-fb");
        }

        pred_sdk::spell_data spell{};
        spell.spell_type        = pred_sdk::spell_type::vector;
        spell.targetting_type   = pred_sdk::targetting_type::center;
        spell.spell_slot        = SLOT_E;
        spell.range             = spell_data::e_range;
        spell.cast_range        = first_click;
        spell.radius            = spell_data::e_radius;
        spell.delay             = spell_data::e_delay;
        spell.projectile_speed  = spell_data::e_speed;
        spell.expected_hitchance = menu::e_hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }

        if (!pred.is_valid)
        {
            log_e("[E] reject: pred invalid tgt=%s d=%.0f vel=%.0f",
                  target->get_char_name().c_str(), tgt_d, std::sqrt(spd_sq));
            return false;
        }
        if (pred.hitchance < menu::e_hitchance)
        {
            log_e("[E] reject: hc %d<%d tgt=%s d=%.0f vel=%.0f",
                  pred.hitchance, menu::e_hitchance,
                  target->get_char_name().c_str(), tgt_d, std::sqrt(spd_sq));
            return false;
        }

        math::vector3 fp = pred.first_cast_position;
        math::vector3 cp = pred.cast_position;

        auto beam_dir = direction_2d(fp, cp);
        float beam_dir_sq = beam_dir.x * beam_dir.x + beam_dir.z * beam_dir.z;
        if (beam_dir_sq < 0.01f)
        {
            // Fall back to ppos -> predicted target direction.
            beam_dir = direction_2d(ppos, pred.predicted_position);
            beam_dir_sq = beam_dir.x * beam_dir.x + beam_dir.z * beam_dir.z;
            if (beam_dir_sq < 0.01f)
            {
                log_e("[E] reject: zero beam dir tgt=%s", target->get_char_name().c_str());
                return false;
            }
        }

        // Reposition FP along beam_dir so the predicted target sits inside the
        // beam segment. SDK sometimes returns FP near the player, making the
        // beam fall short when target is past first_click. Use a dynamic bias
        // that nudges the beam toward its endpoint for far / fleeing targets.
        const auto& pp_pred = pred.predicted_position;
        const float tgt_t = (pp_pred.x - ppos.x) * beam_dir.x
                          + (pp_pred.z - ppos.z) * beam_dir.z;
        const float range_frac = std::clamp(tgt_d / e_reach, 0.f, 1.f);
        float bias = 0.5f + (range_frac - 0.5f) * 0.5f; // 0.25 → 0.75
        const float longitudinal_vel = vel.x * beam_dir.x + vel.z * beam_dir.z;
        if (longitudinal_vel > 0.f)
            bias += std::min(longitudinal_vel / 600.f, 0.35f);
        bias = std::clamp(bias, 0.2f, 0.9f);

        const float ideal_fp_dist = tgt_t - beam_len * bias;
        const float fp_dist = std::clamp(ideal_fp_dist, 0.f, first_click);
        fp = ppos + beam_dir * fp_dist;

        // Re-extend CP from FP so the beam length is exact (clamped to total reach).
        const float cp_total_dist = fp_dist + beam_len;
        cp = (cp_total_dist > e_reach)
            ? ppos + beam_dir * e_reach
            : fp + beam_dir * beam_len;

        // Reject FP behind the player relative to the target.
        const auto to_tgt = direction_2d(ppos, tpos);
        const auto fp_dir = direction_2d(ppos, fp);
        const float facing_dot = to_tgt.x * fp_dir.x + to_tgt.z * fp_dir.z;
        if (facing_dot < 0.2f)
        {
            log_e("[E] reject: FP behind player (dot=%.2f) tgt=%s",
                  facing_dot, target->get_char_name().c_str());
            return false;
        }

        if (menu::log_e_casts)
        {
            log_e("[E] pred ok hc=%d fp=(%.0f,%.0f) cp=(%.0f,%.0f) pp=(%.0f,%.0f) tgt=%s",
                  pred.hitchance, fp.x, fp.z, cp.x, cp.z,
                  pred.predicted_position.x, pred.predicted_position.z,
                  target->get_char_name().c_str());
        }

        // Use fire() so the arming/log path is shared with other branches.
        // beam_bias is irrelevant here because fp/cp came from the SDK; we just
        // need fire's bookkeeping. We reproduce the cast inline to keep the
        // exact SDK fp/cp without remapping through fire's geometry rebuild.
        player->cast_spell(SLOT_E, fp, cp);
        last_cast_e            = now_e;
        m_last_spell_cast_time = now_e;

        if (menu::log_e_casts)
        {
            const float beam_time = beam_len / spell_data::e_speed;
            m_e_log.armed             = true;
            m_e_log.target_net_id     = target->get_network_id();
            m_e_log.target_name       = target->get_char_name();
            m_e_log.fp                = fp;
            m_e_log.cp                = cp;
            m_e_log.eval_time         = now_e + spell_data::e_delay + beam_time;
            m_e_log.tgt_dist_at_cast  = tgt_d;
            m_e_log.tgt_speed_at_cast = std::sqrt(spd_sq);
            m_e_log.branch            = "sdk-vector";

            log_e("[E] CAST tgt=%s d=%.0f vel=%.0f branch=sdk-vector",
                  m_e_log.target_name.c_str(), tgt_d, m_e_log.tgt_speed_at_cast);
        }
        return true;
    }

    bool cast_r(game_object* target, bool manual)
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return false;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target) return false;

        if (!target->is_valid() || target->is_dead() || !target->is_targetable())
            return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - m_last_spell_cast_time < MIN_SPELL_DELAY) return false;

        if (!is_spell_learned(SLOT_R)) return false;
        if (!is_spell_ready(SLOT_R))   return false;
        if (!has_enough_mana(SLOT_R))  return false;

        auto r = player->get_spell(SLOT_R);
        if (!r) return false;
        const float r_dist = player->get_position().distance(target->get_position());
        if (r_dist > spell_data::r_range) return false;

        if (!manual && menu::r_only_killable)
        {
            // Gate against FULL combo damage, not R alone — R-only would
            // reject marginal kills that Q+E+R+AA actually finish.
            const float now_hp = g_sdk->clock_facade->get_game_time();
            float pred_hp = sdk::health_prediction
                ? sdk::health_prediction->get_predicted_health(target, now_hp + spell_data::r_cast_time)
                : target->get_hp();
            pred_hp += target->get_all_shield()
                     + target->get_physical_shield()
                     + target->get_magical_shield();
            if (pred_hp > get_combo_damage(target)) return false;
        }

        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        // pred_sdk crashes for Viktor's toggle/recast R — use velocity lead.
        const auto  tpos = target->get_position();
        const auto  tvel = target->get_velocity();
        const auto  ppos = player->get_position();
        math::vector3 cast_pos = {
            tpos.x + tvel.x * spell_data::r_cast_time,
            tpos.y,
            tpos.z + tvel.z * spell_data::r_cast_time
        };
        const float cast_dist = ppos.distance(cast_pos);
        if (cast_dist > spell_data::r_range)
        {
            if (cast_dist < 1.f) return false;
            cast_pos = ppos + (cast_pos - ppos).to_direction() * spell_data::r_range;
        }

        if (!manual && has_anti_burst_buff(target))
            return false;

        // Skip when manual / semi-manual.
        if (!manual && menu::min_enemies_for_r > 1)
        {
            int count = 0;
            for (auto hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero)) continue;
                if (cast_pos.distance(hero->get_position()) <= spell_data::r_radius)
                    ++count;
            }
            if (count < menu::min_enemies_for_r)
                return false;
        }

        player->cast_spell(SLOT_R, cast_pos);
        m_last_spell_cast_time = now;
        return true;
    }

    bool move_r_to_pos(math::vector3 pos, float score, bool force)
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player) return false;
        if (!is_spell_learned(SLOT_R)) return false;

        auto r_obj = get_r_instance();
        if (!r_obj) return false;

        const auto  ppos = player->get_position();
        const auto  spos = r_obj->get_position();

        // Engine range-clamps against the caster, not the storm.
        const float pdist = ppos.distance(pos);
        if (pdist > spell_data::r_range)
            pos = ppos + (pos - ppos).to_direction() * spell_data::r_range;

        // Dead-zone + hysteresis — bypassed when force=true (KS / forced TS).
        if (!force)
        {
            const float jitter = pos.distance(last_r_move_pos);
            if (jitter < R_DEAD_ZONE && score <= last_r_move_score + R_HYSTERESIS)
                return false;
            if (spos.distance(pos) < R_DEAD_ZONE && score <= last_r_move_score + R_HYSTERESIS)
                return false;
        }

        player->cast_spell(SLOT_R, pos);

        const float now = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        last_r_move_time       = now;
        last_r_move_pos        = pos;
        last_r_move_score      = score;
        m_last_spell_cast_time = now;
        return true;
    }

    void move_r_to_target(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target) return;

        auto r_obj = get_r_instance();
        if (!r_obj) return;
        if (!is_spell_learned(SLOT_R)) return;

        const auto ppos = player->get_position();
        const auto spos = r_obj->get_position();
        const auto tpos = target->get_position();
        const auto tvel = target->get_velocity();

        // CC'd → no lead; otherwise server lag + storm travel time.
        float lead = 0.f;
        if (!is_hard_cc(target))
        {
            const float storm_travel = spos.distance(tpos) / R_STORM_SPEED;
            lead = R_PRED_BASE_DELAY + std::min(storm_travel, 1.5f);
        }

        // Don't lead a dasher past their landing spot.
        math::vector3 pred = { tpos.x + tvel.x * lead, tpos.y, tpos.z + tvel.z * lead };
        if (target->is_dashing())
        {
            auto path = target->get_path();
            if (!path.empty())
            {
                const auto end = path.back();
                const float to_end = tpos.distance(end);
                const float to_pred = tpos.distance(pred);
                if (to_pred > to_end) pred = end;
            }
        }

        // force=true so KS / forced-TS recasts go through even on tiny moves.
        move_r_to_pos(pred, /*score=*/1.f, /*force=*/true);
    }

    float get_q_damage(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0.f;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target || !sdk::damage) return 0.f;

        auto q = player->get_spell(SLOT_Q);
        if (!q || q->get_level() == 0) return 0.f;

        return sdk::damage->get_spell_damage(player, target, SLOT_Q);
    }

    float get_e_damage(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0.f;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target || !sdk::damage) return 0.f;

        auto e = player->get_spell(SLOT_E);
        if (!e || e->get_level() == 0) return 0.f;

        // SDK handles E aftershock internally.
        return sdk::damage->get_spell_damage(player, target, SLOT_E);
    }

    float get_r_damage(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0.f;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target || !sdk::damage) return 0.f;

        auto r = player->get_spell(SLOT_R);
        if (!r || r->get_level() == 0) return 0.f;

        const int   lvl = std::clamp(r->get_level(), 0, 3);
        const float ap = player->get_ability_power();

        // Values from meow_shared/db/champions/viktor.hpp.
        //   R cast: 100/175/250 + 0.5 AP
        //   R tick: 65/105/145 + 0.35 AP
        constexpr float INIT[] = { 0.f, 100.f, 175.f, 250.f };
        const float initial_raw = INIT[lvl] + ap * 0.5f;

        constexpr float TICK[] = { 0.f, 65.f, 105.f, 145.f };
        const float tick_raw = TICK[lvl] + ap * 0.35f;

        const bool hard_cc = is_hard_cc(target);

        int ticks;
        if (hard_cc)
            ticks = 3;
        else if (target->has_buff_of_type(buff_type::slow))
            ticks = 2;
        else if (target->is_dashing())
            ticks = 1;
        else
            ticks = 2;

        const float total_raw = initial_raw + tick_raw * static_cast<float>(ticks);
        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, total_raw);
    }

    float get_combo_damage(game_object* target)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0.f;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !target || !sdk::damage) return 0.f;

        float total = 0.f;

        total += get_q_damage(target);
        total += get_e_damage(target);
        total += get_r_damage(target);

        // Flat AA estimate — pessimistic about Discharge empower so we don't
        // overcommit R on marginal kills.
        total += sdk::damage->get_aa_damage(player, target);

        return total;
    }

    void load()
    {
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->get_char_name() != "Viktor")
            return;

        g_sdk->log_console("[MeowAIO] Loading Viktor >.<");

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->register_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));

        // Drives the Q-AA empowered-Discharge chain.
        if (sdk::orbwalker)
        {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        auto cat = g_sdk->menu_manager->add_category("viktor", "MeowAIO - Viktor");
        if (!cat) return;

        auto q_menu = cat->add_sub_category("q_siphon_power", "Q - Siphon Power");
        q_menu->add_checkbox("combo", "Use in Combo", menu::use_q_combo, [](bool v) { menu::use_q_combo = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::use_q_harass, [](bool v) { menu::use_q_harass = v; });
        q_menu->add_checkbox("flee", "Use in Flee", menu::use_q_flee, [](bool v) { menu::use_q_flee = v; });
        q_menu->add_checkbox("lasthit", "Lasthit", menu::lasthit_q, [](bool v) { menu::lasthit_q = v; });
        q_menu->add_checkbox("clear", "Use in Clear", menu::use_q_clear, [](bool v) { menu::use_q_clear = v; });
        q_menu->add_checkbox("killsteal", "Killsteal", menu::ks_q, [](bool v) { menu::ks_q = v; });
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range, [](bool v) { menu::draw_q_range = v; });

        auto w_menu = cat->add_sub_category("w_gravity_field", "W - Gravity Field");
        w_menu->add_checkbox("combo", "Use in Combo", menu::use_w_combo, [](bool v) { menu::use_w_combo = v; });
        w_menu->add_checkbox("flee", "Use W in Flee", menu::use_w_flee, [](bool v) { menu::use_w_flee = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::use_w_harass, [](bool v) { menu::use_w_harass = v; });
        w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::w_hitchance,
            [](int v) { menu::w_hitchance = v; });
        w_menu->add_slider_int("min_enemies", "Min Enemies", 1, 5, 1, menu::min_enemies_for_w, [](int v) { menu::min_enemies_for_w = v; });
        w_menu->add_checkbox("auto_cc", "Auto W on CC", menu::auto_w_on_cc, [](bool v) { menu::auto_w_on_cc = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("antigap", "Anti Gap-Close", menu::use_w_antigap, [](bool v) { menu::use_w_antigap = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range, [](bool v) { menu::draw_w_range = v; });
        w_menu->add_hotkey("semi_w", "Semi-Manual W", 'T', false, false,
            [](std::string* key, bool val)
            {
                menu::semi_w_active = val;
                menu::semi_w_key    = key;
                // Infotab needs the key pointer — only available after first press.
                if (sdk::infotab && menu::infotab_semi_w == 0 && key)
                {
                    menu::infotab_semi_w = sdk::infotab->add_hotkey_text(
                        key, { "Semi W" },
                        []() -> infotab_sdk::text_entry {
                            return { menu::semi_w_active ? "ON" : "OFF",
                                     menu::semi_w_active ? 0xFF00FF00u : 0xFFFF0000u };
                        });
                }
            });

        auto wl_menu = w_menu->add_sub_category("w_whitelist", "Whitelist");
        {
            auto local = g_sdk->object_manager->get_local_player();
            if (local)
            {
                for (auto hero : g_sdk->object_manager->get_heroes())
                {
                    if (!hero || !hero->is_valid()) continue;
                    if (hero->get_team_id() == local->get_team_id()) continue;

                    const std::string name = hero->get_char_name();
                    if (name.empty()) continue;
                    if (menu::w_antigap_whitelist.count(name)) continue;

                    menu::w_antigap_whitelist[name] = true;
                    wl_menu->add_checkbox("wl_" + name, name,
                        menu::w_antigap_whitelist.at(name),
                        [name](bool v) { menu::w_antigap_whitelist[name] = v; });
                }
            }

            if (menu::w_antigap_whitelist.empty())
                wl_menu->add_label("No enemies detected — join a game and reload.");
        }

        auto e_menu = cat->add_sub_category("e_death_ray", "E - Death Ray");
        e_menu->add_checkbox("combo", "Use in Combo", menu::use_e_combo, [](bool v) { menu::use_e_combo = v; });
        e_menu->add_checkbox("harass", "Use in Harass", menu::use_e_harass, [](bool v) { menu::use_e_harass = v; });
        e_menu->add_checkbox("farm", "Farm", menu::use_e_farm, [](bool v) { menu::use_e_farm = v; });
        e_menu->add_slider_int("farm_min", "Farm Min Minions", 1, 10, 1, menu::e_farm_min_minions, [](int v) { menu::e_farm_min_minions = v; });
        e_menu->add_checkbox("farm_monsters", "Jungle Clear", menu::use_farm_monsters, [](bool v) { menu::use_farm_monsters = v; });
        e_menu->add_checkbox("killsteal", "Killsteal", menu::ks_e, [](bool v) { menu::ks_e = v; });
        e_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::e_hitchance,
            [](int v) { menu::e_hitchance = v; });
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range, [](bool v) { menu::draw_e_range = v; });
        //e_menu->add_checkbox("log_casts", "Log E Casts (debug)", menu::log_e_casts, [](bool v) { menu::log_e_casts = v; });
        e_menu->add_checkbox("draw_rgb", "RGB Range", menu::draw_e_rgb, [](bool v) { menu::draw_e_rgb = v; });

        auto r_menu = cat->add_sub_category("r_chaos_storm", "R - Chaos Storm");
        r_menu->add_checkbox("combo", "Use in Combo", menu::use_r_combo, [](bool v) { menu::use_r_combo = v; });
        r_menu->add_checkbox("killsteal", "Killsteal", menu::ks_r, [](bool v) { menu::ks_r = v; });
        r_menu->add_checkbox("only_kill", "Only if Killable", menu::r_only_killable, [](bool v) { menu::r_only_killable = v; });
        r_menu->add_slider_int("min_enemies", "Min Enemies", 1, 5, 1, menu::min_enemies_for_r, [](int v) { menu::min_enemies_for_r = v; });
        r_menu->add_checkbox("auto_move", "Auto Move", menu::auto_r_movement, [](bool v) { menu::auto_r_movement = v; });
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range, [](bool v) { menu::draw_r_range = v; });
        r_menu->add_hotkey("semi_r", "Semi-Manual R", 'Y', false, false,
            [](std::string* key, bool val)
            {
                menu::semi_r_active = val;
                menu::semi_r_key    = key;
                if (sdk::infotab && menu::infotab_semi_r == 0 && key)
                {
                    menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                        key, { "Semi R" },
                        []() -> infotab_sdk::text_entry {
                            return { menu::semi_r_active ? "ON" : "OFF",
                                     menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u };
                        });
                }
            });

        auto mana_menu = cat->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass_mana", "Harass Mana %", 0, 100, 5, menu::harass_mana_percent, [](int v) { menu::harass_mana_percent = v; });
        mana_menu->add_slider_int("farm_mana", "Farm Mana %", 0, 100, 5, menu::farm_mana_percent, [](int v) { menu::farm_mana_percent = v; });

        auto draw_menu = cat->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("draw_only_ready", "Only Ready Spells", menu::draw_only_ready, [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("r_killable", "R Killable Text", menu::draw_r_killable, [](bool v) { menu::draw_r_killable = v; });
        draw_menu->add_checkbox("r_damage_indicator", "R Damage Indicator", menu::draw_r_damage_indicator, [](bool v) { menu::draw_r_damage_indicator = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q, [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("color_w", "W Color", menu::color_w, [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("color_e", "E Color", menu::color_e, [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("color_r", "R Color", menu::color_r, [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("color_r_dmg", "R Damage Bar Color", menu::color_r_dmg, [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->log_console("[MeowAIO] Viktor loaded successfully :3");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager)
        {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::present,
                reinterpret_cast<void*>(on_present));
            g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
                reinterpret_cast<void*>(on_create_object));
            g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
                reinterpret_cast<void*>(on_delete_object));
            g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
                reinterpret_cast<void*>(on_process_cast));
        }

        if (sdk::orbwalker)
        {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        if (sdk::infotab)
        {
            if (menu::infotab_semi_w) sdk::infotab->remove_text(menu::infotab_semi_w);
            if (menu::infotab_semi_r) sdk::infotab->remove_text(menu::infotab_semi_r);
        }
        menu::infotab_semi_w = 0;
        menu::infotab_semi_r = 0;
        menu::semi_w_key     = nullptr;
        menu::semi_r_key     = nullptr;
        menu::semi_w_active  = false;
        menu::semi_r_active  = false;

        r_instance = nullptr;
        r_storm_active = false;
        r_storm_start_t = 0.f;
        r_cast_pos = {};
        r_cast_pos_valid = false;
        last_r_move_time = 0.f;
        last_r_move_pos = {};
        last_r_move_score = 0.f;
        last_cast_e = 0.f;
        last_q_combo_time = 0.f;
        last_q_harass_time = 0.f;
        last_q_lasthit_time = 0.f;
        m_last_spell_cast_time = 0.f;
        m_q_scheduled = false;
        m_q_scheduled_time = 0.f;
        m_q_scheduled_target = nullptr;
        m_e_log = {};

        g_sdk->log_console("[MeowAIO] Viktor unloaded successfully :3");
    }
} // namespace viktor
