#include "hwei.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace hwei
{

    namespace
    {

        enum class subject : int
        {
            none = -1,
            disaster = 0,
            serenity = 1,
            torment = 2,
        };

        enum class ability_cast : uint8_t
        {
            position,
            target,
            self,
        };

        struct ability_info
        {
            subject mood;
            int slot;
            ability_cast cast_type;
        };

        struct pending_cast
        {
            bool active = false;
            ability_info info{};
            math::vector3 cast_pos{};
            game_object* target = nullptr;
            float max_range = FLT_MAX;
            float fire_at = 0.f;    // earliest time the follow-up may be sent
            float expire_at = 0.f;  // abort if still pending at this time
            bool repredict = false;
            pred_sdk::spell_data repredict_spell{};
            int repredict_hitchance = 0;
            // Keeps re-validating the (minion) target right up to the moment the
            // shot fires and aborts if it has died or is no longer last-hittable.
            bool lasthit_guard = false;
            // Set after fire_followup succeeds; cleared by on_process_cast confirmation.
            // Keeps consume_cast_engine_busy returning true until the game confirms the cast.
            bool followup_fired = false;
        };

        constexpr ability_info ABILITY_QQ{ subject::disaster, SLOT_Q, ability_cast::position };
        constexpr ability_info ABILITY_QW{ subject::disaster, SLOT_W, ability_cast::position };
        constexpr ability_info ABILITY_QE{ subject::disaster, SLOT_E, ability_cast::position };
        constexpr ability_info ABILITY_WQ{ subject::serenity, SLOT_Q, ability_cast::position };
        constexpr ability_info ABILITY_WW{ subject::serenity, SLOT_W, ability_cast::self };
        constexpr ability_info ABILITY_WE{ subject::serenity, SLOT_E, ability_cast::self };
        constexpr ability_info ABILITY_EQ{ subject::torment,  SLOT_Q, ability_cast::position };
        constexpr ability_info ABILITY_EW{ subject::torment,  SLOT_W, ability_cast::position };
        constexpr ability_info ABILITY_EE{ subject::torment,  SLOT_E, ability_cast::position };
        constexpr ability_info ABILITY_R { subject::none,      SLOT_R, ability_cast::position };

        constexpr float MIN_CAST_INTERVAL = 0.05f;
        constexpr float REQUEST_TIMEOUT = 1.25f;
        constexpr float SUBJECT_CONFIRM_DELAY = 0.30f;
        // Delay after the game *confirms* the selector via process_cast — much shorter
        // because the game has already registered the subject at that point.
        constexpr float SUBJECT_CONFIRM_FAST_DELAY = 0.05f;
        constexpr float SUBJECT_RETRY_LOCKOUT = 0.55f;
        constexpr float EE_QQ_FOLLOWUP_WINDOW = 1.25f;
        constexpr float EW_QW_FOLLOWUP_WINDOW = 1.25f;
        constexpr float EW_ROOT_FOLLOWUP_DELAY = 0.75f;
        // Max time the teamfight combo stalls QE/QW while waiting for R to land.
        constexpr float TEAMFIGHT_R_WAIT = 2.0f;
        static bool HWEI_DEBUG_LOGS = false;

        static menu_category* s_menu = nullptr;
        static pred_sdk::spell_data s_pred_qq{};
        static pred_sdk::spell_data s_pred_qw{};
        static pred_sdk::spell_data s_pred_wq{};
        static pred_sdk::spell_data s_pred_qe{};
        static pred_sdk::spell_data s_pred_eq{};
        static pred_sdk::spell_data s_pred_ew{};
        static pred_sdk::spell_data s_pred_ee{};
        static pred_sdk::spell_data s_pred_r{};

        static float s_last_cast_time = 0.f;
        static float s_ee_qq_followup_until = 0.f;
        static float s_ew_qw_followup_ready_at = 0.f;
        static float s_ew_qw_followup_until = 0.f;
        static pending_cast s_pending{};
        static float s_subject_lockout_until[3] = {};
        static uint32_t s_infotab_combo_mode = 0;
        static uint32_t s_infotab_bind_qw   = 0;
        static uint32_t s_infotab_bind_we   = 0;
        static uint32_t s_infotab_bind_ee   = 0;
        static uint32_t s_infotab_bind_eq   = 0;
        static uint32_t s_infotab_bind_r    = 0;
        static bool s_combo_mode_was_pressed = false;
        static std::string s_last_observed_cast_name;
        static float s_r_wait_started_at = 0.f;
        static subject s_cached_subject = subject::none;
        static bool s_subject_cache_authoritative = false;

        bool tick_pending();
        int  effective_combo_mode();
        float get_qq_damage(game_object* target);
        float get_qe_damage(game_object* target);
        float get_qw_damage(game_object* target);
        float get_eq_damage(game_object* target);
        float get_ee_damage(game_object* target);
        float get_ew_damage(game_object* target);
        float get_r_damage(game_object* target);
        float get_combo_damage(game_object* target);

        inline float now()
        {
            return g_sdk && g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
        }

        game_object* get_player()
        {
            return (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
        }

        bool is_spell_learned(int slot)
        {
            auto* player = get_player();
            if (!player) return false;
            auto* entry = player->get_spell(slot);
            return entry && entry->get_level() > 0;
        }

        bool is_spell_ready(int slot)
        {
            auto* player = get_player();
            if (!player) return false;
            if (!is_spell_learned(slot)) return false;
            auto* entry = player->get_spell(slot);
            return entry && entry->get_cooldown() <= 0.f;
        }

        bool has_mana_for(int slot)
        {
            auto* player = get_player();
            if (!player) return false;
            return player->get_par() >= player->get_spell_mana_cost(slot);
        }

        int subject_slot(subject mood)
        {
            switch (mood)
            {
            case subject::disaster:
                return SLOT_Q;
            case subject::serenity:
                return SLOT_W;
            case subject::torment:
                return SLOT_E;
            case subject::none:
            default:
                return SLOT_R;
            }
        }

        float mana_percent()
        {
            auto* player = get_player();
            if (!player) return 0.f;
            const float max_mp = player->get_max_par();
            if (max_mp <= 0.f) return 0.f;
            return (player->get_par() / max_mp) * 100.f;
        }

        bool is_casting()
        {
            auto* player = get_player();
            if (!player) return false;
            auto* ac = player->get_active_spell_cast();
            if (!ac) return false;
            auto* cast = ac->get_spell_cast();
            if (!cast) return false;
            if (cast->is_basic_attack() || cast->is_special_attack()) return false;
            return ac->get_cast_end_time() > now();
        }

        bool is_valid_target(game_object* target, float range)
        {
            if (!target || !target->is_valid() || target->is_dead()) return false;
            if (!target->is_visible() || !target->is_targetable()) return false;
            auto* player = get_player();
            if (!player) return false;
            if (target->get_team_id() == player->get_team_id()) return false;
            if (range < FLT_MAX)
            {
                const auto pos = player->get_position();
                if (pos.distance(target->get_position()) > range) return false;
            }
            return true;
        }

        game_object* pick_target(float range)
        {
            if (!sdk::target_selector) return nullptr;

            if (auto* forced = sdk::target_selector->get_forced_target(); forced && is_valid_target(forced, range))
                return forced;

            for (auto* hero : sdk::target_selector->get_sorted_heroes())
            {
                if (is_valid_target(hero, range))
                    return hero;
            }
            return nullptr;
        }

        bool has_hard_cc(game_object* target)
        {
            if (!target)
                return false;

            for (auto* buff : target->get_buffs())
            {
                if (!buff || !buff->is_active())
                    continue;

                switch (buff->get_type())
                {
                case buff_type::knockup:
                case buff_type::knockback:
                case buff_type::drowsy:
                case buff_type::taunt:
                case buff_type::snare:
                case buff_type::stun:
                case buff_type::suppression:
                case buff_type::flee:
                case buff_type::fear:
                case buff_type::charm:
                    return true;
                default:
                    break;
                }
            }

            return false;
        }

        bool is_isolated_for_qw(game_object* target)
        {
            if (!target || !g_sdk || !g_sdk->object_manager)
                return false;

            // Severing Bolt's missing-health bonus only applies when the bolt hits a single
            // enemy CHAMPION (or an immobilized one) -- not "no nearby allied units". So the
            // bonus is lost whenever another enemy champion is close enough to also be caught
            // in the ~225-radius AoE. Two points inside one such circle are at most ~450 apart,
            // so treat any other enemy champion within that distance as breaking isolation.
            // (Minions are irrelevant to the champion bonus and must not be counted.)
            const auto target_pos = target->get_position();
            const float shared_aoe = s_pred_qw.radius * 2.f;

            for (auto* hero : g_sdk->object_manager->get_heroes())
            {
                if (!hero || hero == target || !hero->is_valid() || hero->is_dead())
                    continue;
                if (hero->get_team_id() != target->get_team_id())
                    continue;
                if (!hero->is_visible())
                    continue;
                if (hero->get_position().distance(target_pos) <= shared_aoe)
                    return false;
            }

            return true;
        }

        bool line_blocked_by_enemy_minion(const math::vector3& cast_pos, float radius, game_object* skip = nullptr)
        {
            auto* player = get_player();
            if (!player || !g_sdk || !g_sdk->object_manager)
                return false;

            const auto src = player->get_position();
            const float dx = cast_pos.x - src.x;
            const float dz = cast_pos.z - src.z;
            const float len_sq = dx * dx + dz * dz;
            if (len_sq <= 1.f)
                return false;

            for (auto* minion : g_sdk->object_manager->get_minions())
            {
                if (!minion || minion == skip || !minion->is_valid() || minion->is_dead())
                    continue;
                if (!minion->is_visible() || !minion->is_targetable())
                    continue;
                if (minion->get_team_id() == player->get_team_id())
                    continue;
                if (!minion->is_lane_minion())
                    continue;

                const auto minion_pos = minion->get_position();
                const float rel_x = minion_pos.x - src.x;
                const float rel_z = minion_pos.z - src.z;
                const float t = (rel_x * dx + rel_z * dz) / len_sq;
                if (t <= 0.f || t >= 1.f)
                    continue;

                const float closest_x = src.x + dx * t;
                const float closest_z = src.z + dz * t;
                const float off_x = minion_pos.x - closest_x;
                const float off_z = minion_pos.z - closest_z;
                const float hit_radius = radius + minion->get_bounding_radius();
                if ((off_x * off_x + off_z * off_z) <= hit_radius * hit_radius)
                {
                    if (HWEI_DEBUG_LOGS && g_sdk)
                    {
                        g_sdk->log_console("[MeowAIO] collision block spell=HweiQQ minion=%s dist=%.0f radius=%.0f",
                            minion->get_name().c_str(),
                            std::sqrt(off_x * off_x + off_z * off_z),
                            hit_radius);
                    }
                    return true;
                }
            }

            return false;
        }

        void setup_prediction_templates()
        {
            s_pred_qq = {};
            s_pred_qq.spell_slot = SLOT_Q;
            s_pred_qq.spell_type = pred_sdk::spell_type::linear;
            s_pred_qq.targetting_type = pred_sdk::targetting_type::edge_to_edge;
            s_pred_qq.range = spell_data::qq_range;
            s_pred_qq.radius = spell_data::qq_radius;
            s_pred_qq.delay = spell_data::qq_delay;
            s_pred_qq.projectile_speed = spell_data::qq_speed;
            s_pred_qq.forbidden_collisions = {
                pred_sdk::collision_type::unit,
                pred_sdk::collision_type::hero,
                pred_sdk::collision_type::yasuo_wall,
                pred_sdk::collision_type::braum_wall
            };

            s_pred_qw = {};
            s_pred_qw.spell_slot = SLOT_W;
            s_pred_qw.spell_type = pred_sdk::spell_type::circular;
            s_pred_qw.targetting_type = pred_sdk::targetting_type::center;
            s_pred_qw.range = spell_data::qw_range;
            s_pred_qw.radius = spell_data::qw_radius;
            s_pred_qw.delay = spell_data::qw_delay;
            s_pred_qw.projectile_speed = FLT_MAX;

            s_pred_wq = {};
            s_pred_wq.spell_slot = SLOT_Q;
            s_pred_wq.spell_type = pred_sdk::spell_type::linear;
            s_pred_wq.targetting_type = pred_sdk::targetting_type::edge_to_edge;
            s_pred_wq.range = spell_data::wq_range;
            s_pred_wq.radius = spell_data::wq_radius;
            s_pred_wq.delay = spell_data::wq_delay;
            s_pred_wq.projectile_speed = spell_data::wq_speed;

            s_pred_qe = {};
            s_pred_qe.spell_slot = SLOT_E;
            s_pred_qe.spell_type = pred_sdk::spell_type::linear;
            s_pred_qe.targetting_type = pred_sdk::targetting_type::edge_to_edge;
            s_pred_qe.range = spell_data::qe_range;
            s_pred_qe.radius = spell_data::qe_radius;
            s_pred_qe.delay = spell_data::qe_delay;
            s_pred_qe.projectile_speed = spell_data::qe_speed;
            // QE erupts the ground — no unit or champion collision, only wind walls.
            s_pred_qe.forbidden_collisions = {
                pred_sdk::collision_type::yasuo_wall,
                pred_sdk::collision_type::braum_wall
            };

            s_pred_eq = {};
            s_pred_eq.spell_slot = SLOT_Q;
            s_pred_eq.spell_type = pred_sdk::spell_type::linear;
            s_pred_eq.targetting_type = pred_sdk::targetting_type::edge_to_edge;
            s_pred_eq.range = spell_data::eq_range;
            s_pred_eq.radius = spell_data::eq_radius;
            s_pred_eq.delay = spell_data::eq_delay;
            s_pred_eq.projectile_speed = spell_data::eq_speed;
            s_pred_eq.forbidden_collisions = {
                pred_sdk::collision_type::unit,
                pred_sdk::collision_type::hero,
                pred_sdk::collision_type::yasuo_wall,
                pred_sdk::collision_type::braum_wall
            };

            s_pred_ew = {};
            s_pred_ew.spell_slot = SLOT_W;
            // Gaze of the Abyss is a ground-target zone (radius 350) thrown to a
            // location, not a directional line. Model it as a circular AoE so the
            // predicted cast point is the spot to drop the eye on, not a line endpoint.
            s_pred_ew.spell_type = pred_sdk::spell_type::circular;
            s_pred_ew.targetting_type = pred_sdk::targetting_type::center;
            s_pred_ew.range = spell_data::ew_range;
            s_pred_ew.radius = spell_data::ew_radius;
            s_pred_ew.delay = spell_data::ew_delay;
            s_pred_ew.projectile_speed = spell_data::ew_speed;

            s_pred_ee = {};
            s_pred_ee.spell_slot = SLOT_E;
            s_pred_ee.spell_type = pred_sdk::spell_type::circular;
            s_pred_ee.targetting_type = pred_sdk::targetting_type::center;
            s_pred_ee.range = spell_data::ee_range;
            s_pred_ee.radius = spell_data::ee_radius;
            s_pred_ee.delay = spell_data::ee_delay;
            s_pred_ee.projectile_speed = FLT_MAX;

            s_pred_r = {};
            s_pred_r.spell_slot = SLOT_R;
            s_pred_r.spell_type = pred_sdk::spell_type::linear;
            s_pred_r.targetting_type = pred_sdk::targetting_type::edge_to_edge;
            s_pred_r.range = spell_data::r_range;
            s_pred_r.radius = spell_data::r_radius;
            s_pred_r.delay = spell_data::r_delay;
            s_pred_r.projectile_speed = spell_data::r_speed;
            s_pred_r.forbidden_collisions = {
                pred_sdk::collision_type::hero,
                pred_sdk::collision_type::yasuo_wall,
                pred_sdk::collision_type::braum_wall
            };
        }

        subject read_live_game_subject()
        {
            auto* player = get_player();
            if (!player) return subject::none;
            auto* spell_q = player->get_spell(SLOT_Q);
            if (!spell_q) return subject::none;
            auto* data = spell_q->get_data();
            if (!data) return subject::none;
            auto* static_data = data->get_static_data();
            if (!static_data) return subject::none;
            const char* name = static_data->get_name();
            if (!name) return subject::none;
            if (strstr(name, "HweiQQ") || strstr(name, "HweiQW") || strstr(name, "HweiQE"))
                return subject::disaster;
            if (strstr(name, "HweiWQ") || strstr(name, "HweiWW") || strstr(name, "HweiWE"))
                return subject::serenity;
            if (strstr(name, "HweiEQ") || strstr(name, "HweiEW") || strstr(name, "HweiEE"))
                return subject::torment;
            return subject::none;
        }

        void cache_selected_subject(subject mood, const char* reason)
        {
            s_cached_subject = mood;
            s_subject_cache_authoritative = true;

            if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] subject cache=%d reason=%s",
                    static_cast<int>(mood),
                    reason ? reason : "unknown");
            }
        }

        subject get_game_subject()
        {
            if (s_subject_cache_authoritative)
                return s_cached_subject;

            const subject live_subject = read_live_game_subject();
            if (live_subject != subject::none)
                return live_subject;

            return subject::none;
        }

        const char* get_cast_name(spell_cast* cast)
        {
            if (!cast) return nullptr;
            auto* data = cast->get_spell_data();
            if (!data) return nullptr;
            auto* static_data = data->get_static_data();
            return static_data ? static_data->get_name() : nullptr;
        }

        subject get_subject_from_selector_name(const char* name)
        {
            if (!name) return subject::none;
            // Some runtimes append suffixes to selector names; accept "HweiQ/W/E"
            // as long as they are not explicit follow-up spells (QQ/QW/QE etc).
            if (strstr(name, "HweiQQ") || strstr(name, "HweiQW") || strstr(name, "HweiQE")
                || strstr(name, "HweiWW") || strstr(name, "HweiWE") || strstr(name, "HweiWQ")
                || strstr(name, "HweiEE") || strstr(name, "HweiEW") || strstr(name, "HweiEQ"))
                return subject::none;

            if (strstr(name, "HweiQ")) return subject::disaster;
            if (strstr(name, "HweiW")) return subject::serenity;
            if (strstr(name, "HweiE")) return subject::torment;
            return subject::none;
        }

        bool is_hwei_followup_spell_name(const char* name)
        {
            if (!name) return false;
            return std::strcmp(name, "HweiQQ") == 0
                || std::strcmp(name, "HweiQW") == 0
                || std::strcmp(name, "HweiQE") == 0
                || std::strcmp(name, "HweiWQ") == 0
                || std::strcmp(name, "HweiWW") == 0
                || std::strcmp(name, "HweiWE") == 0
                || std::strcmp(name, "HweiEQ") == 0
                || std::strcmp(name, "HweiEW") == 0
                || std::strcmp(name, "HweiEE") == 0;
        }

        int subject_index(subject mood)
        {
            switch (mood)
            {
            case subject::disaster:
                return 0;
            case subject::serenity:
                return 1;
            case subject::torment:
                return 2;
            case subject::none:
            default:
                return -1;
            }
        }

        const char* expected_followup_name(const ability_info& info)
        {
            if (info.mood == subject::disaster)
            {
                if (info.slot == SLOT_Q) return "HweiQQ";
                if (info.slot == SLOT_W) return "HweiQW";
                if (info.slot == SLOT_E) return "HweiQE";
            }
            if (info.mood == subject::serenity)
            {
                if (info.slot == SLOT_Q) return "HweiWQ";
                if (info.slot == SLOT_W) return "HweiWW";
                if (info.slot == SLOT_E) return "HweiWE";
            }
            if (info.mood == subject::torment)
            {
                if (info.slot == SLOT_Q) return "HweiEQ";
                if (info.slot == SLOT_W) return "HweiEW";
                if (info.slot == SLOT_E) return "HweiEE";
            }
            return nullptr;
        }

        bool is_expected_followup_name(const char* name)
        {
            if (!name || !s_pending.active)
                return false;
            const char* expected = expected_followup_name(s_pending.info);
            return expected && std::strcmp(name, expected) == 0;
        }

        const char* cast_type_name(ability_cast type)
        {
            switch (type)
            {
            case ability_cast::position:
                return "position";
            case ability_cast::target:
                return "target";
            case ability_cast::self:
                return "self";
            default:
                return "unknown";
            }
        }

        const char* prediction_state_name(pred_sdk::prediction_state state)
        {
            switch (state)
            {
            case pred_sdk::prediction_state::standard:
                return "standard";
            case pred_sdk::prediction_state::no_recent_input:
                return "no_recent_input";
            case pred_sdk::prediction_state::coming_into_range:
                return "coming_into_range";
            case pred_sdk::prediction_state::juking:
                return "juking";
            case pred_sdk::prediction_state::dashing_or_casting:
                return "dashing_or_casting";
            case pred_sdk::prediction_state::undodgeable:
                return "undodgeable";
            case pred_sdk::prediction_state::stasis_or_revive:
                return "stasis_or_revive";
            case pred_sdk::prediction_state::awaiting_netvar_update:
                return "awaiting_netvar_update";
            case pred_sdk::prediction_state::wall_collision:
                return "wall_collision";
            default:
                return "unknown";
            }
        }

        void log_prediction_result(const char* spell_name, game_object* target, const pred_sdk::pred_data& prediction,
            int required_hitchance, float cast_distance, float range, const char* result)
        {
            if (!HWEI_DEBUG_LOGS || !g_sdk)
                return;

            g_sdk->log_console(
                "[MeowAIO] pred spell=%s target=%s result=%s valid=%d hc=%d need=%d state=%s dist=%.0f/%.0f hit_time=%.2f pred=(%.0f,%.0f) cast=(%.0f,%.0f) collisions=%zu",
                spell_name ? spell_name : "unknown",
                target ? target->get_name().c_str() : "none",
                result ? result : "unknown",
                prediction.is_valid,
                prediction.hitchance,
                required_hitchance,
                prediction_state_name(prediction.state),
                cast_distance,
                range,
                prediction.intersection_time,
                prediction.predicted_position.x,
                prediction.predicted_position.z,
                prediction.cast_position.x,
                prediction.cast_position.z,
                prediction.collided_units.size());
        }

        std::string pending_target_name()
        {
            if (!s_pending.active || !s_pending.target)
                return "none";
            return s_pending.target->get_name();
        }

        void apply_subject_lockout(subject mood)
        {
            const int idx = subject_index(mood);
            if (idx >= 0)
                s_subject_lockout_until[idx] = now() + SUBJECT_RETRY_LOCKOUT;
        }

        bool is_subject_lockout_active()
        {
            const float t = now();
            return t < s_subject_lockout_until[0]
                || t < s_subject_lockout_until[1]
                || t < s_subject_lockout_until[2];
        }

        bool has_combo_followup_window()
        {
            const float t = now();
            return (s_ee_qq_followup_until > 0.f && t <= s_ee_qq_followup_until)
                || (s_ew_qw_followup_until > 0.f && t <= s_ew_qw_followup_until);
        }

        bool consume_cast_engine_busy()
        {
            if (is_casting())
                return true;
            if (s_pending.active)
            {
                tick_pending();
                return true;
            }
            return is_subject_lockout_active();
        }

        bool cast_or_busy(bool attempted)
        {
            return attempted || s_pending.active;
        }

        bool fire_followup(game_object* player, const pending_cast& req)
        {
            if (!player || !req.active)
                return false;

            const int followup_slot = req.info.slot;

            if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] followup send expected=%s followup_slot=%d type=%s",
                    expected_followup_name(req.info) ? expected_followup_name(req.info) : "unknown",
                    followup_slot,
                    cast_type_name(req.info.cast_type));
            }

            switch (req.info.cast_type)
            {
            case ability_cast::position:
                if (req.max_range < FLT_MAX
                    && player->get_position().distance(req.cast_pos) > req.max_range)
                    return false;
                if (sdk::common && sdk::common->cast_spell(followup_slot, req.cast_pos))
                    return true;
                player->cast_spell(followup_slot, req.cast_pos);
                return true;
            case ability_cast::target:
                if (!req.target)
                    return false;
                if (sdk::common && sdk::common->cast_spell(followup_slot, req.target))
                    return true;
                player->cast_spell(followup_slot, req.target);
                return true;
            case ability_cast::self:
                if (sdk::common && sdk::common->cast_spell(followup_slot))
                    return true;
                player->cast_spell(followup_slot);
                return true;
            }

            return false;
        }

        bool send_hwei_selector(game_object* player, subject mood)
        {
            if (!player)
                return false;

            const int slot = subject_slot(mood);
            const bool sent = player->cast_hwei_mood(static_cast<int>(mood));
            if (sent)
            {
                cache_selected_subject(mood, "selector-sent");
                return true;
            }

            // cast_hwei_mood unavailable (game palette not yet initialized) — fall back to
            // raw cast_spell to open the palette, then let the followup fire normally.
            if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] selector mood-api failed, raw fallback subject=%d slot=%d",
                    static_cast<int>(mood), slot);
            }
            player->cast_spell(slot);
            cache_selected_subject(mood, "selector-raw-fallback");
            return true;
        }

        void clear_pending(bool apply_lockout = false, const char* reason = nullptr)
        {
            if (apply_lockout && s_pending.active)
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                {
                    const std::string target_name = pending_target_name();
                    g_sdk->log_console("[MeowAIO] clear reason=%s expected=%s observed=%s subject=%d followup=%d target=%s lockout=%.2fs",
                        reason ? reason : "timeout",
                        expected_followup_name(s_pending.info) ? expected_followup_name(s_pending.info) : "unknown",
                        s_last_observed_cast_name.empty() ? "none" : s_last_observed_cast_name.c_str(),
                        static_cast<int>(s_pending.info.mood),
                        s_pending.info.slot,
                        target_name.c_str(),
                        SUBJECT_RETRY_LOCKOUT);
                }
                apply_subject_lockout(s_pending.info.mood);
            }
            if (s_pending.active && (apply_lockout || (reason && std::strcmp(reason, "confirmed-followup") == 0)))
                cache_selected_subject(subject::none, reason ? reason : "reset");
            s_pending = {};
            s_last_observed_cast_name.clear();
        }

        bool can_queue_subject_cast(const ability_info& info)
        {
            if (info.slot < 0)
                return false;

            const int ready_slot = subject_slot(info.mood);
            if (!is_spell_learned(ready_slot))
                return false;
            if (!has_mana_for(ready_slot))
                return false;

            if (info.mood == subject::none)
                return is_spell_ready(ready_slot);

            const int idx = subject_index(info.mood);
            if (idx >= 0 && now() < s_subject_lockout_until[idx])
                return false;

            if (!is_spell_ready(ready_slot))
                return false;

            return true;
        }

        bool start_cast(const ability_info& info, const math::vector3& cast_pos, game_object* target, float max_range = FLT_MAX, float timeout = REQUEST_TIMEOUT)
        {
            auto* player = get_player();
            if (!player) return false;

            if (info.cast_type == ability_cast::position && max_range < FLT_MAX)
            {
                if (player->get_position().distance(cast_pos) > max_range)
                    return false;
            }

            if (!can_queue_subject_cast(info))
                return false;

            if (s_pending.active)
            {
                if (s_pending.info.mood != info.mood || s_pending.info.slot != info.slot)
                    return false;
                if (s_pending.followup_fired)
                    return true; // already in-flight, don't overwrite
                // Same spell still waiting to fire — update aim point so the follow-up
                // stays accurate if the target has moved since the first queue.
                s_pending.cast_pos = cast_pos;
                s_pending.target   = target;
                s_pending.max_range = max_range;
                return true;
            }

            s_last_observed_cast_name.clear();

            float fire_at;
            if (info.mood == subject::none)
            {
                fire_at = now(); // R: no selector step, fire immediately
            }
            else if (get_game_subject() == info.mood)
            {
                fire_at = now() + 0.02f; // subject already active — minimal settle time
            }
            else
            {
                send_hwei_selector(player, info.mood);
                fire_at = now() + SUBJECT_CONFIRM_DELAY;
            }

            s_pending.active   = true;
            s_pending.info     = info;
            s_pending.cast_pos = cast_pos;
            s_pending.target   = target;
            s_pending.max_range = max_range;
            s_pending.fire_at  = fire_at;
            s_pending.expire_at = now() + timeout;
            s_pending.repredict = false;
            s_pending.repredict_spell = {};
            s_pending.repredict_hitchance = 0;
            s_pending.lasthit_guard = false;

            if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] start expected=%s subject=%d followup=%d type=%s fire_in=%.2fs",
                    expected_followup_name(info) ? expected_followup_name(info) : "unknown",
                    static_cast<int>(info.mood),
                    info.slot,
                    cast_type_name(info.cast_type),
                    fire_at - now());
            }
            return true;
        }

        bool tick_pending()
        {
            if (!s_pending.active)
                return false;

            auto* player = get_player();
            if (!player)
            {
                clear_pending(true, "invalid-player");
                return false;
            }

            const float t = now();

            if (t > s_pending.expire_at)
            {
                clear_pending(true, "timeout");
                return false;
            }

            // Follow-up was already sent; stay busy until on_process_cast confirms it.
            if (s_pending.followup_fired)
                return true;

            if (t - s_last_cast_time < MIN_CAST_INTERVAL)
                return false;

            if (t < s_pending.fire_at)
                return false;

            // Lasthit guard: abort if the minion target is no longer killable.
            if (s_pending.lasthit_guard)
            {
                auto* tgt = s_pending.target;
                bool abort = (!tgt || !tgt->is_valid() || tgt->is_dead() || !tgt->is_targetable());
                if (!abort)
                {
                    const float dist = player->get_position().distance(tgt->get_position());
                    const float hit_time = spell_data::qq_delay + dist / spell_data::qq_speed;
                    float hp = tgt->get_hp();
                    if (sdk::health_prediction)
                    {
                        const float pred_hp = sdk::health_prediction->get_predicted_health(tgt, t + hit_time);
                        if (pred_hp >= 0.f)
                            hp = pred_hp;
                    }
                    if (hp <= 0.f || hp > get_qq_damage(tgt))
                        abort = true;
                }
                if (abort)
                {
                    clear_pending(false, "lasthit-target-gone");
                    return false;
                }
            }

            // Repredict: refresh aim point at fire time so a drifting target is
            // still led correctly and out-of-range predictions are rejected.
            if (s_pending.repredict
                && s_pending.target
                && s_pending.info.cast_type == ability_cast::position
                && sdk::prediction
                && is_valid_target(s_pending.target, FLT_MAX))
            {
                s_pending.repredict_spell.source = player;
                s_pending.repredict_spell.source_position = player->get_position();
                s_pending.repredict_spell.expected_hitchance = s_pending.repredict_hitchance;

                auto fresh = sdk::prediction->predict(s_pending.target, s_pending.repredict_spell);
                if (fresh.is_valid && fresh.hitchance >= s_pending.repredict_hitchance)
                {
                    const float fresh_dist = player->get_position().distance(fresh.cast_position);
                    const bool in_range = s_pending.max_range >= FLT_MAX
                        || fresh_dist <= s_pending.max_range;
                    const bool blocked = s_pending.info.mood == subject::disaster
                        && s_pending.info.slot == SLOT_Q
                        && line_blocked_by_enemy_minion(fresh.cast_position, s_pending.repredict_spell.radius, s_pending.target);
                    if (in_range && !blocked)
                        s_pending.cast_pos = fresh.cast_position;
                }
            }

            // R (subject::none) fires via common/cast_spell directly, no selector.
            if (s_pending.info.mood == subject::none)
            {
                bool casted = false;
                if (s_pending.max_range < FLT_MAX
                    && player->get_position().distance(s_pending.cast_pos) > s_pending.max_range)
                    return false;
                if (sdk::common)
                    casted = sdk::common->cast_spell(s_pending.info.slot, s_pending.cast_pos);
                else
                {
                    player->cast_spell(s_pending.info.slot, s_pending.cast_pos);
                    casted = true;
                }
                if (casted)
                {
                    s_last_cast_time = t;
                    clear_pending(false, "subjectless-cast-sent");
                    return true;
                }
                return false;
            }

            const bool fired = fire_followup(player, s_pending);
            if (fired)
            {
                s_last_cast_time = t;
                s_pending.followup_fired = true; // stay busy until on_process_cast confirms
                return true;
            }

            return false;
        }

        bool try_cast_linear(const ability_info& info, pred_sdk::spell_data base_spell, int hitchance, game_object* forced_target = nullptr)
        {
            if (!sdk::prediction) return false;

            auto* player = get_player();
            if (!player) return false;

            auto* target = forced_target ? forced_target : pick_target(base_spell.range);
            if (!is_valid_target(target, base_spell.range))
                return false;

            base_spell.source = player;
            base_spell.source_position = player->get_position();
            base_spell.expected_hitchance = hitchance;

            auto prediction = sdk::prediction->predict(target, base_spell);
            const float cast_dist = player->get_position().distance(prediction.cast_position);
            const char* pred_result = "accepted";
            if (!prediction.is_valid)
                pred_result = "invalid";
            else if (prediction.hitchance < hitchance)
                pred_result = "low-hitchance";
            else if (cast_dist > base_spell.range)
                pred_result = "out-of-range";
            else if (info.mood == subject::disaster
                && info.slot == SLOT_Q
                && line_blocked_by_enemy_minion(prediction.cast_position, base_spell.radius, target))
                pred_result = "collision";

            log_prediction_result(expected_followup_name(info), target, prediction, hitchance, cast_dist, base_spell.range, pred_result);

            if (!prediction.is_valid || prediction.hitchance < hitchance)
                return false;

            // Prevent forced walk-in: never cast if predicted point is out of range.
            if (cast_dist > base_spell.range)
                return false;

            if (std::strcmp(pred_result, "collision") == 0)
                return false;

            if (!start_cast(info, prediction.cast_position, target, base_spell.range))
                return false;
            if (!s_pending.repredict)
            {
                s_pending.repredict = true;
                s_pending.repredict_spell = base_spell;
                s_pending.repredict_hitchance = hitchance;
            }
            return tick_pending();
        }

        bool try_cast_circular(const ability_info& info, pred_sdk::spell_data base_spell, int hitchance)
        {
            if (!sdk::prediction) return false;

            auto* player = get_player();
            if (!player) return false;

            auto* target = pick_target(base_spell.range);
            if (!is_valid_target(target, base_spell.range))
                return false;

            base_spell.source = player;
            base_spell.source_position = player->get_position();
            base_spell.expected_hitchance = hitchance;

            auto prediction = sdk::prediction->predict(target, base_spell);
            const float cast_dist = player->get_position().distance(prediction.cast_position);
            const char* pred_result = "accepted";
            if (!prediction.is_valid)
                pred_result = "invalid";
            else if (prediction.hitchance < hitchance)
                pred_result = "low-hitchance";
            else if (cast_dist > base_spell.range)
                pred_result = "out-of-range";

            log_prediction_result(expected_followup_name(info), target, prediction, hitchance, cast_dist, base_spell.range, pred_result);

            if (!prediction.is_valid || prediction.hitchance < hitchance)
                return false;

            // Prevent forced walk-in: never cast if predicted point is out of range.
            if (cast_dist > base_spell.range)
                return false;

            if (!start_cast(info, prediction.cast_position, target, base_spell.range))
                return false;
            if (!s_pending.repredict)
            {
                s_pending.repredict = true;
                s_pending.repredict_spell = base_spell;
                s_pending.repredict_hitchance = hitchance;
            }
            return tick_pending();
        }

        bool try_cast_qq(int hitchance)
        {
            return try_cast_linear(ABILITY_QQ, s_pred_qq, hitchance);
        }

        bool try_cast_qq_followup()
        {
            // Primary path: normal configured hitchance.
            if (try_cast_qq(menu::combo_qq_hitchance))
                return true;

            // Follow-up fallback: slightly relaxed hitchance for EE->QQ chaining.
            if (try_cast_qq(pred_sdk::hitchance::medium))
                return true;

            // Final fallback: direct positional cast at target location in QQ range.
            auto* player = get_player();
            if (!player) return false;

            auto* target = pick_target(s_pred_qq.range);
            if (!is_valid_target(target, s_pred_qq.range))
                return false;

            const auto cast_pos = target->get_position();
            const float cast_dist = player->get_position().distance(cast_pos);
            if (cast_dist > s_pred_qq.range)
                return false;

            if (line_blocked_by_enemy_minion(cast_pos, s_pred_qq.radius, target))
                return false;

            if (!start_cast(ABILITY_QQ, cast_pos, target, s_pred_qq.range))
                return false;
            return tick_pending();
        }

        bool try_cast_qw(int hitchance)
        {
            return try_cast_circular(ABILITY_QW, s_pred_qw, hitchance);
        }

        bool try_cast_qw_on_target(game_object* target, int hitchance)
        {
            if (!sdk::prediction)
                return false;

            auto* player = get_player();
            if (!player || !is_valid_target(target, s_pred_qw.range))
                return false;

            auto spell = s_pred_qw;
            spell.source = player;
            spell.source_position = player->get_position();
            spell.expected_hitchance = hitchance;

            auto prediction = sdk::prediction->predict(target, spell);
            const float cast_dist = player->get_position().distance(prediction.cast_position);
            const char* pred_result = "accepted";
            if (!prediction.is_valid)
                pred_result = "invalid";
            else if (prediction.hitchance < hitchance)
                pred_result = "low-hitchance";
            else if (cast_dist > spell.range)
                pred_result = "out-of-range";

            log_prediction_result("HweiQW", target, prediction, hitchance, cast_dist, spell.range, pred_result);

            if (!prediction.is_valid || prediction.hitchance < hitchance)
                return false;

            if (cast_dist > spell.range)
                return false;

            if (!start_cast(ABILITY_QW, prediction.cast_position, target, spell.range))
                return false;

            return tick_pending();
        }

        bool try_cast_qe(int hitchance)
        {
            return try_cast_linear(ABILITY_QE, s_pred_qe, hitchance);
        }

        bool try_cast_we()
        {
            if (!start_cast(ABILITY_WE, {}, nullptr))
                return false;
            return tick_pending();
        }

        // WW (Stirring Lights): self-centered heal that prioritises the most
        // wounded nearby ally. Fires when our own HP% drops below the self
        // threshold, or a visible allied champion within heal range drops below
        // the ally threshold.
        bool handle_ww()
        {
            if (!menu::use_ww)
                return false;
            if (!is_spell_learned(SLOT_W) || !is_spell_ready(SLOT_W) || !has_mana_for(SLOT_W))
                return false;
            if (consume_cast_engine_busy())
                return false;

            auto* player = get_player();
            if (!player || !g_sdk || !g_sdk->object_manager)
                return false;

            bool should_heal = false;

            if (menu::use_ww_self)
            {
                const float self_max = player->get_max_hp();
                if (self_max > 0.f
                    && (player->get_hp() / self_max) * 100.f <= static_cast<float>(menu::ww_self_hp_pct))
                {
                    should_heal = true;
                }
            }

            if (!should_heal)
            {
                const auto ppos = player->get_position();
                for (auto* ally : g_sdk->object_manager->get_heroes())
                {
                    if (!ally || !ally->is_valid() || ally->is_dead() || !ally->is_visible())
                        continue;
                    if (ally->get_team_id() != player->get_team_id())
                        continue;
                    if (ally->get_network_id() == player->get_network_id())
                        continue;
                    if (ppos.distance(ally->get_position()) > spell_data::ww_range)
                        continue;

                    const float max_hp = ally->get_max_hp();
                    if (max_hp <= 0.f)
                        continue;
                    if ((ally->get_hp() / max_hp) * 100.f <= static_cast<float>(menu::ww_ally_hp_pct))
                    {
                        should_heal = true;
                        break;
                    }
                }
            }

            if (!should_heal)
                return false;

            if (!start_cast(ABILITY_WW, {}, nullptr))
                return false;
            return tick_pending();
        }

        bool try_cast_eq(int hitchance)
        {
            return try_cast_linear(ABILITY_EQ, s_pred_eq, hitchance);
        }

        bool try_cast_ew_root_setup()
        {
            if (!sdk::prediction) return false;

            auto* player = get_player();
            if (!player) return false;

            auto* target = pick_target(s_pred_ew.range);
            if (!is_valid_target(target, s_pred_ew.range))
                return false;

            auto spell = s_pred_ew;
            spell.source = player;
            spell.source_position = player->get_position();
            spell.expected_hitchance = pred_sdk::hitchance::medium;

            auto prediction = sdk::prediction->predict(target, spell);
            const float cast_dist = player->get_position().distance(prediction.cast_position);

            const char* pred_result = "accepted";
            if (!prediction.is_valid)
                pred_result = "invalid";
            else if (cast_dist > spell.range)
                pred_result = "out-of-range";

            log_prediction_result("HweiEW-root", target, prediction, pred_sdk::hitchance::medium,
                cast_dist, spell.range, pred_result);

            if (!prediction.is_valid || cast_dist > spell.range)
                return false;

            if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] EW root setup pred_pos=(%.0f,%.0f,%.0f) target=%s",
                    prediction.cast_position.x, prediction.cast_position.y,
                    prediction.cast_position.z, target->get_name().c_str());
            }

            if (!start_cast(ABILITY_EW, prediction.cast_position, target, s_pred_ew.range))
                return false;
            if (!s_pending.repredict)
            {
                s_pending.repredict = true;
                s_pending.repredict_spell = spell;
                s_pending.repredict_hitchance = 0; // root setup is lenient: refresh whenever prediction is valid
            }

            return tick_pending();
        }

        bool try_cast_ee(int hitchance)
        {
            return try_cast_circular(ABILITY_EE, s_pred_ee, hitchance);
        }

        // Returns false when the prediction should not be trusted for R:
        //  - cast position is within RANGE_MARGIN of max range (borderline shots miss on moving targets)
        //  - cast position is behind / orthogonal to the target (happens at melee range when target
        //    runs into or past the player, producing a backwards intersection point)
        bool validate_r_prediction(game_object* target,
            const pred_sdk::pred_data& prediction,
            float cast_dist, float range)
        {
            constexpr float RANGE_MARGIN = 60.f;
            if (cast_dist > range - RANGE_MARGIN)
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] R reject edge-of-range dist=%.0f margin=%.0f range=%.0f",
                        cast_dist, RANGE_MARGIN, range);
                return false;
            }

            auto* player = get_player();
            if (player && target)
            {
                const auto ppos      = player->get_position();
                const auto to_target = target->get_position() - ppos;
                const auto to_cast   = prediction.cast_position - ppos;
                const float dot      = to_target.x * to_cast.x + to_target.z * to_cast.z;
                if (dot <= 0.f)
                {
                    if (HWEI_DEBUG_LOGS && g_sdk)
                        g_sdk->log_console("[MeowAIO] R reject backwards cast_pos target=%s dot=%.1f",
                            target->get_name().c_str(), dot);
                    return false;
                }
            }

            return true;
        }

        bool try_cast_r_direct(int hitchance)
        {
            if (!sdk::prediction || !sdk::common) return false;
            if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R))
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] R skip ready=%d mana=%d", is_spell_ready(SLOT_R), has_mana_for(SLOT_R));
                return false;
            }

            auto* player = get_player();
            if (!player) return false;

            auto* target = pick_target(s_pred_r.range);
            if (!is_valid_target(target, s_pred_r.range))
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] R skip no valid target range=%.0f", s_pred_r.range);
                return false;
            }

            auto spell = s_pred_r;
            spell.source = player;
            spell.source_position = player->get_position();
            spell.expected_hitchance = hitchance;

            auto prediction = sdk::prediction->predict(target, spell);
            const float cast_dist = player->get_position().distance(prediction.cast_position);
            const char* pred_result = "accepted";
            if (!prediction.is_valid)
                pred_result = "invalid";
            else if (prediction.hitchance < hitchance)
                pred_result = "low-hitchance";
            else if (cast_dist > spell.range)
                pred_result = "out-of-range";

            log_prediction_result("HweiR", target, prediction, hitchance, cast_dist, spell.range, pred_result);

            if (!prediction.is_valid || prediction.hitchance < hitchance)
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] R skip prediction valid=%d hc=%d need=%d",
                        prediction.is_valid, prediction.hitchance, hitchance);
                return false;
            }

            if (cast_dist > spell.range)
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] R skip cast position out of range");
                return false;
            }

            if (!validate_r_prediction(target, prediction, cast_dist, spell.range))
                return false;

            const bool r_fired = sdk::common->cast_spell(SLOT_R, prediction.cast_position);
            if (r_fired)
                s_r_wait_started_at = 0.f;
            return r_fired;
        }

        bool try_cast_r_on_target(game_object* target, int hitchance)
        {
            if (!sdk::prediction || !sdk::common) return false;
            if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R)) return false;

            auto* player = get_player();
            if (!player || !is_valid_target(target, s_pred_r.range)) return false;

            auto spell = s_pred_r;
            spell.source = player;
            spell.source_position = player->get_position();
            spell.expected_hitchance = hitchance;

            auto prediction = sdk::prediction->predict(target, spell);
            const float cast_dist = player->get_position().distance(prediction.cast_position);

            const char* pred_result = "accepted";
            if (!prediction.is_valid)
                pred_result = "invalid";
            else if (prediction.hitchance < hitchance)
                pred_result = "low-hitchance";
            else if (cast_dist > spell.range)
                pred_result = "out-of-range";

            log_prediction_result("HweiR-KS", target, prediction, hitchance, cast_dist, spell.range, pred_result);

            if (!prediction.is_valid || prediction.hitchance < hitchance || cast_dist > spell.range)
                return false;

            if (!validate_r_prediction(target, prediction, cast_dist, spell.range))
                return false;

            const bool fired = sdk::common->cast_spell(SLOT_R, prediction.cast_position);
            if (fired)
                s_r_wait_started_at = 0.f;
            return fired;
        }

        bool try_cast_r_semi(int hitchance)
        {
            if (try_cast_r_direct(hitchance))
                return true;

            return try_cast_r_direct(pred_sdk::hitchance::medium);
        }

        bool should_wait_for_r()
        {
            if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R))
            {
                s_r_wait_started_at = 0.f;
                return false;
            }
            // Only stall the rest of the combo (QE/QW) while there is actually a target
            // within R range to wait for. Otherwise R can never land and we'd needlessly
            // withhold damage for the full wait window.
            if (!is_valid_target(pick_target(s_pred_r.range), s_pred_r.range))
            {
                s_r_wait_started_at = 0.f;
                return false;
            }
            const float wait_max = TEAMFIGHT_R_WAIT;
            if (wait_max <= 0.f)
                return false;
            const float t = now();
            if (s_r_wait_started_at <= 0.f)
                s_r_wait_started_at = t;
            if (t - s_r_wait_started_at >= wait_max)
            {
                s_r_wait_started_at = 0.f;
                return false;
            }
            return true;
        }

        void open_ew_qw_followup_window(float ready_delay, const char* reason)
        {
            const float current_time = now();
            s_ew_qw_followup_ready_at = current_time + ready_delay;
            s_ew_qw_followup_until = current_time + EW_QW_FOLLOWUP_WINDOW;

            if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] EW followup window opened reason=%s ready_delay=%.2fs",
                    reason ? reason : "unknown", ready_delay);
            }
        }

        bool handle_ee_qq_followup()
        {
            if (s_ee_qq_followup_until <= 0.f)
                return false;

            const float current_time = now();
            if (current_time > s_ee_qq_followup_until)
            {
                s_ee_qq_followup_until = 0.f;
                return false;
            }

            const bool casted = try_cast_qq_followup();
            if (casted)
            {
                s_ee_qq_followup_until = 0.f;
                return true;
            }

            return s_pending.active;
        }

        bool handle_ew_qw_followup(bool cast_r_first)
        {
            if (s_ew_qw_followup_until <= 0.f)
                return false;

            const float current_time = now();
            if (current_time > s_ew_qw_followup_until)
            {
                s_ew_qw_followup_ready_at = 0.f;
                s_ew_qw_followup_until = 0.f;
                return false;
            }

            if (current_time < s_ew_qw_followup_ready_at)
                return true;

            if (cast_r_first && try_cast_r_semi(menu::combo_r_hitchance))
                return true;

            if (cast_or_busy(try_cast_qw(menu::combo_qw_hitchance)))
                return true;

            return false;
        }

        bool combo_basic()
        {
            if (handle_ee_qq_followup()) return true;
            if (cast_or_busy(try_cast_we())) return true;
            if (cast_or_busy(try_cast_ee(menu::combo_ee_hitchance))) return true;
            if (cast_or_busy(try_cast_qq(menu::combo_qq_hitchance))) return true;
            return false;
        }

        bool combo_qe_qw()
        {
            if (cast_or_busy(try_cast_we())) return true;
            if (cast_or_busy(try_cast_qe(menu::combo_qe_hitchance))) return true;
            if (cast_or_busy(try_cast_qw(menu::combo_qw_hitchance))) return true;
            return false;
        }

        bool combo_ew_qw()
        {
            if (handle_ew_qw_followup(false)) return true;
            if (cast_or_busy(try_cast_ew_root_setup())) return true;
            return false;
        }

        bool combo_ew_r_qw()
        {
            if (handle_ew_qw_followup(true)) return true;
            if (cast_or_busy(try_cast_ew_root_setup())) return true;
            return false;
        }

        bool combo_all_in_burst()
        {
            if (handle_ee_qq_followup()) return true;
            if (cast_or_busy(try_cast_we())) return true;
            if (cast_or_busy(try_cast_ee(menu::combo_ee_hitchance))) return true;
            if (try_cast_r_semi(menu::combo_r_hitchance)) return true;
            if (cast_or_busy(try_cast_qq(menu::combo_qq_hitchance))) return true;
            if (cast_or_busy(try_cast_qw(menu::combo_qw_hitchance))) return true;
            return false;
        }

        bool combo_teamfight_all_in()
        {
            if (cast_or_busy(try_cast_we())) return true;
            if (cast_or_busy(try_cast_ee(menu::combo_ee_hitchance))) return true;
            if (try_cast_r_semi(menu::combo_r_hitchance)) return true;
            if (should_wait_for_r()) return true;
            if (cast_or_busy(try_cast_qe(menu::combo_qe_hitchance))) return true;
            if (cast_or_busy(try_cast_qw(menu::combo_qw_hitchance))) return true;
            return false;
        }

        bool cast_wq_flee()
        {
            if (!menu::flee_use_wq) return false;
            if (!sdk::common) return false;

            auto* player = get_player();
            if (!player) return false;

            auto cast_pos = player->get_position();
            math::vector3 cursor_pos = cast_pos + player->get_direction();

            if (g_sdk && g_sdk->hud_manager)
            {
                cursor_pos = g_sdk->hud_manager->get_cursor_position();
            }

            const auto delta = cursor_pos - cast_pos;
            const float dist_sq = delta.x * delta.x + delta.z * delta.z;
            if (dist_sq < 1.f)
            {
                cursor_pos = cast_pos + player->get_direction();
            }

            cast_pos = cast_pos.extended(cursor_pos, s_pred_wq.range, true);
            if (!start_cast(ABILITY_WQ, cast_pos, nullptr, s_pred_wq.range))
                return false;
            return tick_pending();
        }

        float get_r_damage(game_object* target)
        {
            auto* player = get_player();
            if (!player || !target || !sdk::damage) return 0.f;

            auto* r_spell = player->get_spell(SLOT_R);
            if (!r_spell || r_spell->get_level() <= 0) return 0.f;

            const int level = std::clamp(r_spell->get_level(), 1, 3);
            // Spiraling Despair (per Data.cpp HweiR): 230/360/490 + 0.95 AP
            constexpr float BASE[] = { 0.f, 230.f, 360.f, 490.f };
            constexpr float AP_RATIO = 0.95f;

            const float ap = player->get_ability_power();
            const float raw = BASE[level] + AP_RATIO * ap;

            return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
        }

        float get_qw_damage(game_object* target)
        {
            auto* player = get_player();
            if (!player || !target || !sdk::damage)
                return 0.f;

            // QW (Severing Bolt) is a Disaster sub-spell: it ranks with the Q slot
            // (Data.cpp HweiQW -> SkillshotSlot 'Q'), not W.
            auto* q_spell = player->get_spell(SLOT_Q);
            if (!q_spell || q_spell->get_level() <= 0)
                return 0.f;

            const int level = std::clamp(q_spell->get_level(), 1, 5);
            constexpr float BASE[] = { 0.f, 60.f, 85.f, 110.f, 135.f, 160.f };
            constexpr float MAX_MISSING_HP_MULT[] = { 0.f, 2.00f, 2.375f, 2.75f, 3.125f, 3.50f };

            float raw = BASE[level] + 0.25f * player->get_ability_power();

            if (has_hard_cc(target) || is_isolated_for_qw(target))
            {
                const float max_hp = target->get_max_hp();
                if (max_hp > 0.f)
                {
                    const float missing_pct = std::clamp((max_hp - target->get_hp()) / max_hp, 0.f, 1.f);
                    const float mult = 1.f + (MAX_MISSING_HP_MULT[level] - 1.f) * missing_pct;
                    raw *= mult;
                }
            }

            return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
        }

        float get_qq_damage(game_object* target)
        {
            auto* player = get_player();
            if (!player || !target || !sdk::damage) return 0.f;

            auto* q_spell = player->get_spell(SLOT_Q);
            if (!q_spell || q_spell->get_level() <= 0) return 0.f;

            const int idx = std::clamp(q_spell->get_level(), 1, 5) - 1;
            // Devastating Fire [Fireball] direct hit (per Data.cpp HweiQQ):
            //   base 60/90/120/150/180 + (3/4/5/6/7)% target max HP + 0.75 AP
            constexpr float BASE[]   = { 60.f, 90.f, 120.f, 150.f, 180.f };
            constexpr float HP_PCT[] = { 3.f,  4.f,  5.f,   6.f,   7.f   };
            constexpr float AP_RATIO = 0.75f;

            const float raw = BASE[idx]
                + HP_PCT[idx] * target->get_max_hp() / 100.f
                + AP_RATIO * player->get_ability_power();
            return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
        }

        float get_qe_damage(game_object* target)
        {
            auto* player = get_player();
            if (!player || !target || !sdk::damage) return 0.f;

            // QE (Molten Fissure) initial hit (per Data.cpp HweiQE):
            //   base 20/40/60/80/100 + 0.30 AP, ranks with Q slot
            auto* q_spell = player->get_spell(SLOT_Q);
            if (!q_spell || q_spell->get_level() <= 0) return 0.f;

            const int idx = std::clamp(q_spell->get_level(), 1, 5) - 1;
            constexpr float BASE[] = { 20.f, 40.f, 60.f, 80.f, 100.f };
            constexpr float AP_RATIO = 0.30f;

            const float raw = BASE[idx] + AP_RATIO * player->get_ability_power();
            return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
        }

        float get_eq_damage(game_object* target)
        {
            auto* player = get_player();
            if (!player || !target || !sdk::damage) return 0.f;

            // EQ (Grim Visage) (per Data.cpp HweiEQ):
            //   base 70/110/150/190/230 + 0.60 AP, ranks with E slot
            auto* e_spell = player->get_spell(SLOT_E);
            if (!e_spell || e_spell->get_level() <= 0) return 0.f;

            const int idx = std::clamp(e_spell->get_level(), 1, 5) - 1;
            constexpr float BASE[] = { 70.f, 110.f, 150.f, 190.f, 230.f };
            constexpr float AP_RATIO = 0.60f;

            const float raw = BASE[idx] + AP_RATIO * player->get_ability_power();
            return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
        }

        // EE (Crushing Maw) and EW (Gaze of the Abyss) share the same formula as EQ
        // (per Data.cpp HweiEE / HweiEW): 70/110/150/190/230 + 0.60 AP, E slot.
        float get_ee_damage(game_object* target) { return get_eq_damage(target); }
        float get_ew_damage(game_object* target) { return get_eq_damage(target); }

        float get_combo_damage(game_object* target)
        {
            if (!target) return 0.f;
            const bool q_ready = is_spell_ready(SLOT_Q) && has_mana_for(SLOT_Q);
            const bool e_ready = is_spell_ready(SLOT_E) && has_mana_for(SLOT_E);
            const bool r_ready = is_spell_ready(SLOT_R) && has_mana_for(SLOT_R);
            float total = 0.f;
            switch (static_cast<combo_mode>(effective_combo_mode()))
            {
            case combo_mode::basic:
                if (e_ready) total += get_ee_damage(target);
                if (q_ready) total += get_qq_damage(target);
                break;
            case combo_mode::qe_qw:
                if (q_ready) total += get_qe_damage(target) + get_qw_damage(target);
                break;
            case combo_mode::ew_qw:
                if (e_ready) total += get_ew_damage(target);
                if (q_ready) total += get_qw_damage(target);
                break;
            case combo_mode::ew_r_qw:
                if (e_ready) total += get_ew_damage(target);
                if (r_ready) total += get_r_damage(target);
                if (q_ready) total += get_qw_damage(target);
                break;
            case combo_mode::all_in_burst:
                if (e_ready) total += get_ee_damage(target);
                if (r_ready) total += get_r_damage(target);
                if (q_ready) total += get_qq_damage(target) + get_qw_damage(target);
                break;
            case combo_mode::teamfight_all_in:
                if (e_ready) total += get_ee_damage(target);
                if (r_ready) total += get_r_damage(target);
                if (q_ready) total += get_qe_damage(target) + get_qw_damage(target);
                break;
            default:
                break;
            }
            return total;
        }

        float get_magic_effective_hp(game_object* target, float delay)
        {
            if (!target)
                return FLT_MAX;

            float hp = target->get_hp();
            if (sdk::health_prediction)
            {
                const float predicted = sdk::health_prediction->get_predicted_health(target, now() + delay);
                if (predicted > 0.f && predicted <= target->get_max_hp())
                    hp = predicted;
            }

            return hp + target->get_all_shield() + target->get_magical_shield();
        }

        bool killsteal_qw()
        {
            if (!menu::killsteal_qw || !g_sdk || !g_sdk->object_manager)
                return false;
            if (consume_cast_engine_busy() || has_combo_followup_window())
                return false;
            if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q))
                return false;

            for (auto* hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero, s_pred_qw.range))
                    continue;

                const float hp = get_magic_effective_hp(hero, spell_data::qw_delay + 0.05f);
                const float damage = get_qw_damage(hero);
                if (hp <= 0.f || hp > damage)
                    continue;

                if (HWEI_DEBUG_LOGS && g_sdk)
                {
                    g_sdk->log_console("[MeowAIO] QW killsteal target=%s hp=%.0f dmg=%.0f",
                        hero->get_name().c_str(), hp, damage);
                }

                if (cast_or_busy(try_cast_qw_on_target(hero, menu::combo_qw_hitchance)))
                    return true;
            }

            return false;
        }

        bool killsteal_r()
        {
            if (!menu::killsteal_r || !g_sdk || !g_sdk->object_manager)
                return false;
            // R is a standalone ultimate (no subject selector), so it may fire during
            // a combo follow-up window to secure a kill — only the active cast engine blocks it.
            if (consume_cast_engine_busy())
                return false;
            if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R))
                return false;

            for (auto* hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero, s_pred_r.range))
                    continue;

                const float hp = get_magic_effective_hp(hero, spell_data::r_delay + 0.05f);
                const float damage = get_r_damage(hero);
                if (hp <= 0.f || hp > damage)
                    continue;

                if (HWEI_DEBUG_LOGS && g_sdk)
                {
                    g_sdk->log_console("[MeowAIO] R killsteal target=%s hp=%.0f dmg=%.0f",
                        hero->get_name().c_str(), hp, damage);
                }

                if (try_cast_r_on_target(hero, menu::combo_r_hitchance))
                    return true;
            }

            return false;
        }

        bool killsteal_qq()
        {
            if (!menu::killsteal_qq || !g_sdk || !g_sdk->object_manager)
                return false;
            if (consume_cast_engine_busy() || has_combo_followup_window())
                return false;
            if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q))
                return false;

            for (auto* hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero, s_pred_qq.range))
                    continue;

                const float hp = get_magic_effective_hp(hero, spell_data::qq_delay + 0.05f);
                const float damage = get_qq_damage(hero);
                if (hp <= 0.f || hp > damage)
                    continue;

                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] QQ killsteal target=%s hp=%.0f dmg=%.0f",
                        hero->get_name().c_str(), hp, damage);

                if (cast_or_busy(try_cast_linear(ABILITY_QQ, s_pred_qq, menu::combo_qq_hitchance, hero)))
                    return true;
            }

            return false;
        }

        bool killsteal_qe()
        {
            if (!menu::killsteal_qe || !g_sdk || !g_sdk->object_manager)
                return false;
            if (consume_cast_engine_busy() || has_combo_followup_window())
                return false;
            if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q))
                return false;

            for (auto* hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero, s_pred_qe.range))
                    continue;

                const float hp = get_magic_effective_hp(hero, spell_data::qe_delay + 0.05f);
                const float damage = get_qe_damage(hero);
                if (hp <= 0.f || hp > damage)
                    continue;

                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] QE killsteal target=%s hp=%.0f dmg=%.0f",
                        hero->get_name().c_str(), hp, damage);

                if (cast_or_busy(try_cast_linear(ABILITY_QE, s_pred_qe, menu::combo_qe_hitchance, hero)))
                    return true;
            }

            return false;
        }

        bool killsteal_eq()
        {
            if (!menu::killsteal_eq || !g_sdk || !g_sdk->object_manager)
                return false;
            if (consume_cast_engine_busy() || has_combo_followup_window())
                return false;
            if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E))
                return false;

            for (auto* hero : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_target(hero, s_pred_eq.range))
                    continue;

                const float hp = get_magic_effective_hp(hero, spell_data::eq_delay + 0.05f);
                const float damage = get_eq_damage(hero);
                if (hp <= 0.f || hp > damage)
                    continue;

                if (HWEI_DEBUG_LOGS && g_sdk)
                    g_sdk->log_console("[MeowAIO] EQ killsteal target=%s hp=%.0f dmg=%.0f",
                        hero->get_name().c_str(), hp, damage);

                if (cast_or_busy(try_cast_linear(ABILITY_EQ, s_pred_eq, menu::combo_eq_hitchance, hero)))
                    return true;
            }

            return false;
        }

        bool handle_hotkeys()
        {
            if (consume_cast_engine_busy())
                return true;

            if (menu::bind_r_active && try_cast_r_semi(menu::combo_r_hitchance))
                return true;
            if (menu::bind_we_active && cast_or_busy(try_cast_we()))
                return true;
            if (menu::bind_qw_active && cast_or_busy(try_cast_qw(menu::combo_qw_hitchance)))
                return true;
            if (menu::bind_ee_active && cast_or_busy(try_cast_ee(menu::combo_ee_hitchance)))
                return true;
            if (menu::bind_eq_active && cast_or_busy(try_cast_eq(menu::combo_eq_hitchance)))
                return true;
            return false;
        }

        int effective_combo_mode()
        {
            if (menu::bind_mode_basic_active)     return static_cast<int>(combo_mode::basic);
            if (menu::bind_mode_qeqw_active)      return static_cast<int>(combo_mode::qe_qw);
            if (menu::bind_mode_ewqw_active)      return static_cast<int>(combo_mode::ew_qw);
            if (menu::bind_mode_ewrqw_active)     return static_cast<int>(combo_mode::ew_r_qw);
            if (menu::bind_mode_burst_active)     return static_cast<int>(combo_mode::all_in_burst);
            if (menu::bind_mode_teamfight_active) return static_cast<int>(combo_mode::teamfight_all_in);
            return menu::combo_mode_sel;
        }

        bool any_mode_bind_active()
        {
            return menu::bind_mode_basic_active
                || menu::bind_mode_qeqw_active
                || menu::bind_mode_ewqw_active
                || menu::bind_mode_ewrqw_active
                || menu::bind_mode_burst_active
                || menu::bind_mode_teamfight_active;
        }

        void clear_all_mode_binds()
        {
            menu::bind_mode_basic_active     = false;
            menu::bind_mode_qeqw_active      = false;
            menu::bind_mode_ewqw_active      = false;
            menu::bind_mode_ewrqw_active     = false;
            menu::bind_mode_burst_active     = false;
            menu::bind_mode_teamfight_active = false;
        }

        void toggle_mode_bind(bool& target)
        {
            if (target)
            {
                target = false;
            }
            else
            {
                clear_all_mode_binds();
                target = true;
            }
        }

        void combo()
        {
            if (consume_cast_engine_busy())
                return;

            switch (static_cast<combo_mode>(effective_combo_mode()))
            {
            case combo_mode::basic:
                combo_basic();
                break;
            case combo_mode::qe_qw:
                combo_qe_qw();
                break;
            case combo_mode::ew_qw:
                combo_ew_qw();
                break;
            case combo_mode::ew_r_qw:
                combo_ew_r_qw();
                break;
            case combo_mode::all_in_burst:
                combo_all_in_burst();
                break;
            case combo_mode::teamfight_all_in:
                combo_teamfight_all_in();
                break;
            default:
                combo_basic();
                break;
            }
        }

        void harass()
        {
            if (consume_cast_engine_busy())
                return;

            if (mana_percent() < static_cast<float>(menu::harass_mana_pct))
                return;

            if (menu::harass_use_eq && cast_or_busy(try_cast_eq(menu::combo_eq_hitchance)))
                return;

            if (menu::harass_use_qe && cast_or_busy(try_cast_linear(ABILITY_QE, s_pred_qe, menu::combo_qe_hitchance)))
                return;

            if (menu::harass_use_qw && cast_or_busy(try_cast_circular(ABILITY_QW, s_pred_qw, menu::combo_qw_hitchance)))
                return;

            if (menu::harass_use_qq && cast_or_busy(try_cast_linear(ABILITY_QQ, s_pred_qq, menu::combo_qq_hitchance)))
                return;

            if (menu::harass_use_we && cast_or_busy(try_cast_we()))
                return;
        }

        void flee()
        {
            if (consume_cast_engine_busy())
                return;

            cast_wq_flee();
        }

        bool lane_qq_lasthit()
        {
            if (!menu::lane_use_qq) return false;
            if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q)) return false;

            auto* player = get_player();
            if (!player || !g_sdk || !g_sdk->object_manager) return false;

            const auto ppos = player->get_position();
            const float range  = spell_data::qq_range;
            const float speed  = spell_data::qq_speed;
            const float delay  = spell_data::qq_delay;

            game_object*  best_target   = nullptr;
            float         best_dist     = FLT_MAX;
            math::vector3 best_cast_pos{};

            for (auto* m : g_sdk->object_manager->get_minions())
            {
                if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable())
                    continue;
                if (m->get_team_id() == player->get_team_id())
                    continue;
                if (!m->is_lane_minion())
                    continue;

                const float dist = ppos.distance(m->get_position());
                if (dist > range) continue;

                const float hit_time = delay + dist / speed;

                float hp = m->get_hp();
                if (sdk::health_prediction)
                {
                    const float pred_hp = sdk::health_prediction->get_predicted_health(m, now() + hit_time);
                    if (pred_hp > 0.f && pred_hp <= m->get_max_hp())
                        hp = pred_hp;
                }

                const float dmg = get_qq_damage(m);
                if (hp <= 0.f || hp > dmg) continue;

                // Skip if another enemy minion sits on the path and would absorb the bolt
                if (line_blocked_by_enemy_minion(m->get_position(), spell_data::qq_radius, m))
                    continue;

                // Pick the closest killable, unobstructed minion
                if (dist < best_dist)
                {
                    best_dist     = dist;
                    best_target   = m;
                    best_cast_pos = m->get_position();
                }
            }

            if (!best_target) return false;

            if (start_cast(ABILITY_QQ, best_cast_pos, best_target, range))
            {
                s_pending.lasthit_guard = true;
                // Refresh the aim point at fire time so a minion that drifts during
                // the subject-select delay is still led correctly (and minion-blocked
                // shots are rejected). hitchance 0 = accept any valid minion prediction.
                if (!s_pending.repredict)
                {
                    s_pending.repredict = true;
                    s_pending.repredict_spell = s_pred_qq;
                    s_pending.repredict_hitchance = 0;
                }
                tick_pending();
                return true;
            }
            return false;
        }

        void lane_lasthit()
        {
            if (consume_cast_engine_busy())
                return;

            if (mana_percent() < static_cast<float>(menu::lane_mana_pct))
                return;

            lane_qq_lasthit();
        }

        void lane()
        {
            if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled())
                return;

            if (consume_cast_engine_busy())
                return;

            if (mana_percent() < static_cast<float>(menu::lane_mana_pct))
                return;

            if (lane_qq_lasthit())
                return;

            if (!menu::lane_use_qe)
                return;

            auto* player = get_player();
            if (!player || !g_sdk || !g_sdk->object_manager)
                return;

            const auto ppos = player->get_position();
            const float range = s_pred_qe.range;
            const float radius = s_pred_qe.radius;

            std::vector<game_object*> minions;
            for (auto* m : g_sdk->object_manager->get_minions())
            {
                if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable())
                    continue;
                if (m->get_team_id() == player->get_team_id())
                    continue;
                if (!m->is_lane_minion())
                    continue;
                if (ppos.distance(m->get_position()) > range)
                    continue;
                minions.push_back(m);
            }

            if (minions.empty())
                return;

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

            int best_hits = 0;
            math::vector3 best_pos{};

            for (auto* anchor : minions)
            {
                const auto apos = anchor->get_position();
                int hits = 0;
                for (auto* m : minions)
                {
                    const float hit_r = radius + m->get_bounding_radius();
                    if (pt_seg_dist(m->get_position(), ppos, apos) <= hit_r)
                        ++hits;
                }
                if (hits > best_hits)
                {
                    best_hits = hits;
                    best_pos = apos;
                }
            }

            if (best_hits >= menu::lane_qe_min_minions)
            {
                if (start_cast(ABILITY_QE, best_pos, nullptr, s_pred_qe.range))
                    tick_pending();
            }
        }

        uint32_t draw_color(int slot, uint32_t base_color)
        {
            auto* player = get_player();
            if (!player) return 0u;
            auto* entry = player->get_spell(slot);
            if (!entry || entry->get_level() <= 0) return 0u;
            if (menu::draw_only_ready && entry->get_cooldown() > 0.f)
                return 0u;
            if (entry->get_cooldown() > 0.f)
                return (base_color & 0x00FFFFFFu) | 0x50000000u;
            return base_color;
        }

        uint32_t draw_ability_color(const ability_info& info, uint32_t base_color)
        {
            return draw_color(subject_slot(info.mood), base_color);
        }

        void draw_ranges()
        {
            if (!g_sdk || !g_sdk->object_manager) return;
            auto* player = get_player();
            if (!player || player->is_dead()) return;

            const auto pos = player->get_position();

            auto draw_circle = [&](float radius, uint32_t color)
            {
                if (!color) return;
                if (sdk::shaders)
                    sdk::shaders->draw_world_circle(pos, radius, 2.f, color);
                else if (g_sdk->renderer)
                    g_sdk->renderer->add_circle_3d(pos, radius, 2.f, color);
            };

            auto draw_qq_range = [&]() { draw_circle(s_pred_qq.range, draw_ability_color(ABILITY_QQ, menu::color_qq)); };
            auto draw_qw_range = [&]() { draw_circle(s_pred_qw.range, draw_ability_color(ABILITY_QW, menu::color_qw)); };
            auto draw_qe_range = [&]() { draw_circle(s_pred_qe.range, draw_ability_color(ABILITY_QE, menu::color_qe)); };
            auto draw_eq_range = [&]() { draw_circle(s_pred_eq.range, draw_ability_color(ABILITY_EQ, menu::color_eq)); };
            auto draw_ew_range = [&]() { draw_circle(s_pred_ew.range, draw_ability_color(ABILITY_EW, menu::color_ew)); };
            auto draw_ee_range = [&]() { draw_circle(s_pred_ee.range, draw_ability_color(ABILITY_EE, menu::color_ee)); };
            auto draw_r_range = [&]() { draw_circle(s_pred_r.range, draw_color(SLOT_R, menu::color_r)); };

            auto draw_qq = [&]() { if (menu::draw_qq) draw_qq_range(); };
            auto draw_qw = [&]() { if (menu::draw_qw) draw_qw_range(); };
            auto draw_qe = [&]() { if (menu::draw_qe) draw_qe_range(); };
            auto draw_eq = [&]() { if (menu::draw_eq) draw_eq_range(); };
            auto draw_ew = [&]() { if (menu::draw_ew) draw_ew_range(); };
            auto draw_ee = [&]() { if (menu::draw_ee) draw_ee_range(); };
            auto draw_r = [&]() { if (menu::draw_r) draw_r_range(); };

            if (menu::draw_only_mode_ranges)
            {
                switch (static_cast<combo_mode>(effective_combo_mode()))
                {
                case combo_mode::basic:
                    draw_ee_range();
                    draw_qq_range();
                    break;
                case combo_mode::qe_qw:
                    draw_qe_range();
                    draw_qw_range();
                    break;
                case combo_mode::ew_qw:
                    draw_ew_range();
                    draw_qw_range();
                    break;
                case combo_mode::ew_r_qw:
                    draw_ew_range();
                    draw_r_range();
                    draw_qw_range();
                    break;
                case combo_mode::all_in_burst:
                    draw_ee_range();
                    draw_r_range();
                    draw_qq_range();
                    draw_qw_range();
                    break;
                case combo_mode::teamfight_all_in:
                    draw_ee_range();
                    draw_r_range();
                    draw_qe_range();
                    draw_qw_range();
                    break;
                default:
                    draw_ee_range();
                    draw_qq_range();
                    break;
                }
                return;
            }

            subject active_subject = get_game_subject();

            bool show_q_group = active_subject == subject::disaster;
            bool show_e_group = active_subject == subject::torment;
            if (menu::draw_only_active_spell)
            {
                if (show_q_group)
                {
                    if (menu::draw_qq) draw_qq();
                    else if (menu::draw_qw) draw_qw();
                    else if (menu::draw_qe) draw_qe();
                }
                if (show_e_group)
                {
                    if (menu::draw_eq) draw_eq();
                    else if (menu::draw_ew) draw_ew();
                    else if (menu::draw_ee) draw_ee();
                }
                draw_r();
                return;
            }

            draw_qq();
            draw_qw();
            draw_qe();
            draw_eq();
            draw_ew();
            draw_ee();
            draw_r();
        }
    }

    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager)
            return;

        setup_prediction_templates();
        HWEI_DEBUG_LOGS = menu::debug_logs;
        s_cached_subject = subject::none;
        s_subject_cache_authoritative = false;

        // Seed subject cache immediately — handles reconnect mid-game correctly
        const subject live_at_load = read_live_game_subject();
        if (live_at_load != subject::none)
            cache_selected_subject(live_at_load, "load-seed");

        s_menu = g_sdk->menu_manager->add_category("hwei", "MeowAIO - Hwei");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Hwei: failed to create menu category");
            return;
        }

        if (auto* combo_menu = s_menu->add_sub_category("combo", "Combo Modes"))
        {
            combo_menu->add_combo("mode", "Combo Mode",
                {
                    "Basic (WE > EE > QQ)",
                    "Zone (WE > QE > QW)",
                    "Root Execute (EW > QW)",
                    "Root (EW > R > QW)",
                    "Burst (WE > EE > R > QQ > QW)",
                    "Teamfight (WE > EE > R > QE > QW)"
                }, menu::combo_mode_sel,
                [](int v) { menu::combo_mode_sel = v; });
            combo_menu->add_separator();
            combo_menu->add_checkbox("cycle_enabled", "Enable Cycle Key", menu::combo_mode_cycle_enabled,
                [](bool v) { menu::combo_mode_cycle_enabled = v; });
            combo_menu->add_hotkey("mode_cycle", "Cycle Combo Mode", 'J', false, false,
                [](std::string* key, bool pressed) {
                    (void)key;
                    if (pressed && !s_combo_mode_was_pressed && menu::combo_mode_cycle_enabled)
                    {
                        constexpr int MODE_COUNT = 6;
                        menu::combo_mode_sel = (menu::combo_mode_sel + 1) % MODE_COUNT;
                        if (g_sdk)
                        {
                            static constexpr const char* names[] = {
                                "Basic",
                                "Zone Execute",
                                "Root Execute",
                                "Root",
                                "Burst",
                                "Teamfight"
                            };
                            const int m = std::clamp(menu::combo_mode_sel, 0, MODE_COUNT - 1);
                            g_sdk->log_console("[+] Hwei Combo Mode: %s", names[m]);
                        }
                    }
                    s_combo_mode_was_pressed = pressed;
                });
            combo_menu->add_separator();
            combo_menu->add_hotkey("bind_basic", "Basic (WE > EE > QQ)", 0, false, false,
                [](std::string* /*key*/, bool pressed) {
                    static bool s_was = false;
                    if (pressed && !s_was) toggle_mode_bind(menu::bind_mode_basic_active);
                    s_was = pressed;
                });
            combo_menu->add_hotkey("bind_qeqw", "Zone (WE > QE > QW)", 0, false, false,
                [](std::string* /*key*/, bool pressed) {
                    static bool s_was = false;
                    if (pressed && !s_was) toggle_mode_bind(menu::bind_mode_qeqw_active);
                    s_was = pressed;
                });
            combo_menu->add_hotkey("bind_ewqw", "Root Execute (EW > QW)", 0, false, false,
                [](std::string* /*key*/, bool pressed) {
                    static bool s_was = false;
                    if (pressed && !s_was) toggle_mode_bind(menu::bind_mode_ewqw_active);
                    s_was = pressed;
                });
            combo_menu->add_hotkey("bind_ewrqw", "Root (EW > R > QW)", 0, false, false,
                [](std::string* /*key*/, bool pressed) {
                    static bool s_was = false;
                    if (pressed && !s_was) toggle_mode_bind(menu::bind_mode_ewrqw_active);
                    s_was = pressed;
                });
            combo_menu->add_hotkey("bind_burst", "Burst (WE > EE > R > QQ > QW)", 0, false, false,
                [](std::string* /*key*/, bool pressed) {
                    static bool s_was = false;
                    if (pressed && !s_was) toggle_mode_bind(menu::bind_mode_burst_active);
                    s_was = pressed;
                });
            combo_menu->add_hotkey("bind_teamfight", "Teamfight (WE > EE > R > QE > QW)", 0, false, false,
                [](std::string* /*key*/, bool pressed) {
                    static bool s_was = false;
                    if (pressed && !s_was) toggle_mode_bind(menu::bind_mode_teamfight_active);
                    s_was = pressed;
                });
            combo_menu->add_separator();
            combo_menu->add_checkbox("draw_indicator", "Draw Mode Indicator", menu::draw_combo_mode_indicator,
                [](bool v) { menu::draw_combo_mode_indicator = v; });
        }

        if (auto* q_menu = s_menu->add_sub_category("q", "Q - Subject: Disaster"))
        {
            q_menu->add_hotkey("bind_qw", "Semi-Manual QW", 'G', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_qw_active = pressed;
                    if (sdk::infotab && s_infotab_bind_qw == 0 && key)
                        s_infotab_bind_qw = sdk::infotab->add_hotkey_text(key, { "QW" },
                            []() -> infotab_sdk::text_entry {
                                return { menu::bind_qw_active ? "ON" : "OFF",
                                         menu::bind_qw_active ? 0xFF00FF00u : 0xFFFF0000u };
                            });
                });
            q_menu->add_slider_int("hc_qq", "  QQ Hitchance", 0, 100, 5, menu::combo_qq_hitchance,
                [](int v) { menu::combo_qq_hitchance = v; });
            q_menu->add_slider_int("hc_qw", "  QW Hitchance", 0, 100, 5, menu::combo_qw_hitchance,
                [](int v) { menu::combo_qw_hitchance = v; });
            q_menu->add_slider_int("hc_qe", "  QE Hitchance", 0, 100, 5, menu::combo_qe_hitchance,
                [](int v) { menu::combo_qe_hitchance = v; });
            q_menu->add_checkbox("harass_qq", "Use in Harass QQ", menu::harass_use_qq,
                [](bool v) { menu::harass_use_qq = v; });
            q_menu->add_checkbox("harass_qw", "Use in Harass QW", menu::harass_use_qw,
                [](bool v) { menu::harass_use_qw = v; });
            q_menu->add_checkbox("harass_qe", "Use in Harass QE", menu::harass_use_qe,
                [](bool v) { menu::harass_use_qe = v; });
            q_menu->add_separator();
            q_menu->add_checkbox("ks_qq", "Killsteal QQ", menu::killsteal_qq,
                [](bool v) { menu::killsteal_qq = v; });
            q_menu->add_checkbox("ks_qe", "Killsteal QE", menu::killsteal_qe,
                [](bool v) { menu::killsteal_qe = v; });
            q_menu->add_checkbox("ks_qw", "Killsteal QW", menu::killsteal_qw,
                [](bool v) { menu::killsteal_qw = v; });
            q_menu->add_separator();
            q_menu->add_checkbox("lane_qq", "Use QQ to Lasthit", menu::lane_use_qq,
                [](bool v) { menu::lane_use_qq = v; });
            q_menu->add_checkbox("lane_qe", "Lane Clear QE", menu::lane_use_qe,
                [](bool v) { menu::lane_use_qe = v; });
            q_menu->add_slider_int("lane_qe_min", "Lane Clear Min Minions", 1, 10, 1, menu::lane_qe_min_minions,
                [](int v) { menu::lane_qe_min_minions = v; });
            q_menu->add_separator();
            q_menu->add_checkbox("draw_qq", "Draw QQ", menu::draw_qq,
                [](bool v) { menu::draw_qq = v; });
            q_menu->add_checkbox("draw_qw", "Draw QW", menu::draw_qw,
                [](bool v) { menu::draw_qw = v; });
            q_menu->add_checkbox("draw_qw_mm", "Draw QW On Minimap", menu::draw_qw_minimap,
                [](bool v) { menu::draw_qw_minimap = v; });
            q_menu->add_checkbox("draw_qe", "Draw QE", menu::draw_qe,
                [](bool v) { menu::draw_qe = v; });
           
        }

        if (auto* w_menu = s_menu->add_sub_category("w", "W - Subject: Serenity"))
        {
            w_menu->add_hotkey("bind_we", "Semi-Manual WE", 'H', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_we_active = pressed;
                    if (sdk::infotab && s_infotab_bind_we == 0 && key)
                        s_infotab_bind_we = sdk::infotab->add_hotkey_text(key, { "WE" },
                            []() -> infotab_sdk::text_entry {
                                return { menu::bind_we_active ? "ON" : "OFF",
                                         menu::bind_we_active ? 0xFF00FF00u : 0xFFFF0000u };
                            });
                });
            w_menu->add_checkbox("flee_wq", "Use in Flee WQ", menu::flee_use_wq,
                [](bool v) { menu::flee_use_wq = v; });
            w_menu->add_checkbox("harass_we", "Use in Harass WE", menu::harass_use_we,
                [](bool v) { menu::harass_use_we = v; });
            w_menu->add_separator();
            w_menu->add_checkbox("use_ww", "Use WW", menu::use_ww,
                [](bool v) { menu::use_ww = v; });
            w_menu->add_checkbox("use_ww_self", "Use on Self", menu::use_ww_self,
                [](bool v) { menu::use_ww_self = v; });
            w_menu->add_slider_int("ww_self_hp", "  WW Self HP %", 0, 100, 5, menu::ww_self_hp_pct,
                [](int v) { menu::ww_self_hp_pct = v; });
            w_menu->add_slider_int("ww_ally_hp", "  WW Ally HP %", 0, 100, 5, menu::ww_ally_hp_pct,
                [](int v) { menu::ww_ally_hp_pct = v; });
        }

        if (auto* e_menu = s_menu->add_sub_category("e", "E - Subject: Torment"))
        {
            e_menu->add_hotkey("bind_ee", "Semi-Manual EE", 'V', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_ee_active = pressed;
                    if (sdk::infotab && s_infotab_bind_ee == 0 && key)
                        s_infotab_bind_ee = sdk::infotab->add_hotkey_text(key, { "EE" },
                            []() -> infotab_sdk::text_entry {
                                return { menu::bind_ee_active ? "ON" : "OFF",
                                         menu::bind_ee_active ? 0xFF00FF00u : 0xFFFF0000u };
                            });
                });
            e_menu->add_hotkey("bind_eq", "Semi-Manual EQ", 'N', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_eq_active = pressed;
                    if (sdk::infotab && s_infotab_bind_eq == 0 && key)
                        s_infotab_bind_eq = sdk::infotab->add_hotkey_text(key, { "EQ" },
                            []() -> infotab_sdk::text_entry {
                                return { menu::bind_eq_active ? "ON" : "OFF",
                                         menu::bind_eq_active ? 0xFF00FF00u : 0xFFFF0000u };
                            });
                });
            e_menu->add_slider_int("hc_eq", "  EQ Hitchance", 0, 100, 5, menu::combo_eq_hitchance,
                [](int v) { menu::combo_eq_hitchance = v; });
            e_menu->add_slider_int("hc_ee", "  EE Hitchance", 0, 100, 5, menu::combo_ee_hitchance,
                [](int v) { menu::combo_ee_hitchance = v; });
            e_menu->add_separator();
            e_menu->add_checkbox("ks_eq", "Killsteal EQ", menu::killsteal_eq,
                [](bool v) { menu::killsteal_eq = v; });
            e_menu->add_separator();
            e_menu->add_checkbox("harass_eq", "Use in Harass EQ", menu::harass_use_eq,
                [](bool v) { menu::harass_use_eq = v; });
            e_menu->add_separator();
            e_menu->add_checkbox("draw_eq", "Draw EQ", menu::draw_eq,
                [](bool v) { menu::draw_eq = v; });
            e_menu->add_checkbox("draw_ew", "Draw EW", menu::draw_ew,
                [](bool v) { menu::draw_ew = v; });
            e_menu->add_checkbox("draw_ee", "Draw EE", menu::draw_ee,
                [](bool v) { menu::draw_ee = v; });
        }

        if (auto* r_menu = s_menu->add_sub_category("r", "R - Spiraling Despair"))
        {
            r_menu->add_hotkey("bind_r", "Semi-Manual R", 'T', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_r_active = pressed;
                    if (sdk::infotab && s_infotab_bind_r == 0 && key)
                        s_infotab_bind_r = sdk::infotab->add_hotkey_text(key, { "R" },
                            []() -> infotab_sdk::text_entry {
                                return { menu::bind_r_active ? "ON" : "OFF",
                                         menu::bind_r_active ? 0xFF00FF00u : 0xFFFF0000u };
                            });
                });
            r_menu->add_checkbox("ks_r", "Killsteal R", menu::killsteal_r,
                [](bool v) { menu::killsteal_r = v; });
            r_menu->add_slider_int("hc_r", "  R Hitchance", 0, 100, 5, menu::combo_r_hitchance,
                [](int v) { menu::combo_r_hitchance = v; });
            r_menu->add_checkbox("draw_r", "Draw R", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
        }

        if (auto* mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds"))
        {
            mana_menu->add_slider_int("harass_mana", "Harass Min Mana %", 0, 100, 5, menu::harass_mana_pct,
                [](int v) { menu::harass_mana_pct = v; });
            mana_menu->add_slider_int("lane_mana", "Lane Clear Min Mana %", 0, 100, 5, menu::lane_mana_pct,
                [](int v) { menu::lane_mana_pct = v; });
        }

        if (auto* draw_menu = s_menu->add_sub_category("draw", "Drawings"))
        {
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_separator();
            draw_menu->add_checkbox("draw_only_active", "Only Active Spells (off will draw all ranges at once)", menu::draw_only_active_spell,
                [](bool v) { menu::draw_only_active_spell = v; });
            draw_menu->add_checkbox("draw_only_mode", "Only Draw Active Combo Mode Ranges", menu::draw_only_mode_ranges,
                [](bool v) { menu::draw_only_mode_ranges = v; });
            draw_menu->add_checkbox("draw_r_damage", "Draw R Damage Indicator", menu::draw_r_damage_indicator,
                [](bool v) { menu::draw_r_damage_indicator = v; });
            draw_menu->add_checkbox("draw_combo_damage", "Draw Combo Damage Indicator", menu::draw_combo_damage_indicator,
                [](bool v) { menu::draw_combo_damage_indicator = v; });
            //draw_menu->add_checkbox("debug_logs", "Debug Logs", menu::debug_logs,
                //[](bool v) {
                    //menu::debug_logs = v;
                   // HWEI_DEBUG_LOGS = v;
               // });
            draw_menu->add_separator();
            draw_menu->add_colorpicker("color_qq", "  QQ Color", menu::color_qq,
                [](uint32_t c) { menu::color_qq = c; });
            draw_menu->add_colorpicker("color_qw", "  QW Color", menu::color_qw,
                [](uint32_t c) { menu::color_qw = c; });
            draw_menu->add_colorpicker("color_qe", "  QE Color", menu::color_qe,
                [](uint32_t c) { menu::color_qe = c; });
            draw_menu->add_colorpicker("color_eq", "  EQ Color", menu::color_eq,
                [](uint32_t c) { menu::color_eq = c; });
            draw_menu->add_colorpicker("color_ew", "  EW Color", menu::color_ew,
                [](uint32_t c) { menu::color_ew = c; });
            draw_menu->add_colorpicker("color_ee", "  EE Color", menu::color_ee,
                [](uint32_t c) { menu::color_ee = c; });
            draw_menu->add_colorpicker("color_r", "  R Color", menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
            draw_menu->add_colorpicker("color_r_dmg", "  R Damage Color", menu::color_r_dmg,
                [](uint32_t c) { menu::color_r_dmg = c; });
        }

        if (sdk::infotab)
        {
            s_infotab_combo_mode = sdk::infotab->add_text(
                infotab_sdk::text_entry{ "Combo", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    static constexpr const char* names[] = {
                        "Basic",
                        "Zone Execute",
                        "Root Execute",
                        "Root",
                        "Burst",
                        "Teamfight"
                    };
                    static constexpr uint32_t colors[] = {
                        0xFF00FF80u,
                        0xFF40D0FFu,
                        0xFFE87C9Eu,
                        0xFFFF5050u,
                        0xFFFFA040u,
                        0xFFA080FFu
                    };
                    const int m = std::clamp(effective_combo_mode(), 0, 5);
                    return { names[m], colors[m] };
                });

        }

        if (g_sdk->event_manager)
        {
            g_sdk->event_manager->register_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->register_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->register_callback(event_manager::event::present,
                reinterpret_cast<void*>(on_present));
            g_sdk->event_manager->register_callback(event_manager::event::process_cast,
                reinterpret_cast<void*>(on_process_cast));
            g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
        }

        g_sdk->log_console("[MeowAIO] Hwei module loaded");
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
            g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
                reinterpret_cast<void*>(on_process_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
        }

        if (sdk::infotab)
        {
            if (s_infotab_combo_mode) sdk::infotab->remove_text(s_infotab_combo_mode);
            if (s_infotab_bind_qw)   sdk::infotab->remove_text(s_infotab_bind_qw);
            if (s_infotab_bind_we)   sdk::infotab->remove_text(s_infotab_bind_we);
            if (s_infotab_bind_ee)   sdk::infotab->remove_text(s_infotab_bind_ee);
            if (s_infotab_bind_eq)   sdk::infotab->remove_text(s_infotab_bind_eq);
            if (s_infotab_bind_r)    sdk::infotab->remove_text(s_infotab_bind_r);
        }
        s_infotab_combo_mode = 0;
        s_infotab_bind_qw    = 0;
        s_infotab_bind_we    = 0;
        s_infotab_bind_ee    = 0;
        s_infotab_bind_eq    = 0;
        s_infotab_bind_r     = 0;

        s_menu = nullptr;
        s_last_cast_time = 0.f;
        s_ee_qq_followup_until = 0.f;
        s_ew_qw_followup_ready_at = 0.f;
        s_ew_qw_followup_until = 0.f;
        s_subject_lockout_until[0] = 0.f;
        s_subject_lockout_until[1] = 0.f;
        s_subject_lockout_until[2] = 0.f;
        s_cached_subject = subject::none;
        s_subject_cache_authoritative = false;
        s_r_wait_started_at = 0.f;
        clear_all_mode_binds();
        clear_pending();

        g_sdk->log_console("[-] Hwei module unloaded");
    }

    void __fastcall on_update()
    {
        tick_pending();

        auto* player = get_player();
        if (!player || player->is_dead())
            return;

        if (handle_hotkeys())
            return;

        if (killsteal_qw())
            return;

        if (killsteal_qq())
            return;

        if (killsteal_eq())
            return;

        if (killsteal_qe())
            return;

        if (killsteal_r())
            return;

        if (handle_ww())
            return;

        if (!sdk::orbwalker)
            return;

        if (sdk::orbwalker->combo())
        {
            combo();
        }
        else if (sdk::orbwalker->harass())
        {
            harass();
        }
        else if (sdk::orbwalker->flee())
        {
            flee();
        }
        else if (sdk::orbwalker->clear())
        {
            lane();
        }
        else if (sdk::orbwalker->lasthit())
        {
            lane_lasthit();
        }
    }

    void __fastcall on_draw()
    {
        draw_ranges();

        if (menu::draw_combo_mode_indicator && g_sdk && g_sdk->renderer)
        {
            auto* player = get_player();
            if (player && !player->is_dead())
            {
                const auto screen = g_sdk->renderer->world_to_screen(player->get_position());
                static constexpr const char* mode_names[] = {
                    "Basic",
                    "Zone",
                    "Root Execute",
                    "Root (R)",
                    "Burst (R)",
                    "Teamfight (R)"
                };
                static constexpr uint32_t mode_colors[] = {
                    0xFF00FF80u,
                    0xFF40D0FFu,
                    0xFFE87C9Eu,
                    0xFFFF5050u,
                    0xFFFFA040u,
                    0xFFA080FFu
                };
                const int m = std::clamp(effective_combo_mode(), 0, 5);
                const bool toggled = any_mode_bind_active();
                const std::string text = std::string(toggled ? "* " : "") + mode_names[m];
                g_sdk->renderer->add_text(text.c_str(), 16.f,
                    { screen.x, screen.y + 42.f }, 1, mode_colors[m]);
            }
        }
    }

    void __fastcall on_present()
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->renderer)
            return;

        auto* player = get_player();
        if (!player || player->is_dead())
            return;

        if (menu::draw_qw_minimap && is_spell_learned(SLOT_Q))
        {
            g_sdk->renderer->add_circle_minimap(player->get_position(),
                s_pred_qw.range, 1.f, menu::color_qw);
        }

        const bool show_r_dmg = menu::draw_r_damage_indicator
            && [&]{ auto* s = player->get_spell(SLOT_R); return s && s->get_level() > 0; }();
        const bool show_combo_dmg = menu::draw_combo_damage_indicator;

        if (!show_r_dmg && !show_combo_dmg)
            return;

        const float scale = static_cast<float>(g_sdk->renderer->get_window_height()) / 1080.f;
        const float ox = -45.6f * scale;
        const float oy =  15.5f * scale;
        const float bw = 106.4f * scale;
        const float bh =  11.5f * scale;

        // Per-mode colors (match the mode name indicator in on_draw), semi-transparent.
        static constexpr uint32_t combo_dmg_colors[] = {
            0xCC00FF80u,  // basic
            0xCC40D0FFu,  // qe_qw
            0xCCE87C9Eu,  // ew_qw
            0xCCFF5050u,  // ew_r_qw
            0xCCFFA040u,  // all_in_burst
            0xCCA080FFu,  // teamfight_all_in
        };
        const uint32_t combo_color = combo_dmg_colors[std::clamp(effective_combo_mode(), 0, 5)];

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!hero || !hero->is_valid() || hero->is_dead())
                continue;
            if (hero->get_team_id() == player->get_team_id())
                continue;
            if (!hero->is_visible())
                continue;

            const float hp_raw = hero->get_hp();
            const float max_hp = hero->get_max_hp();
            if (hp_raw <= 0.f || max_hp <= 0.f)
                continue;

            const auto hbar = hero->get_health_bar_position();
            if (hbar.x < 1.f && hbar.y < 1.f)
                continue;

            const float effective_hp = hp_raw + hero->get_all_shield() + hero->get_magical_shield();
            const float hp_frac = hp_raw / max_hp;

            // Combo damage drawn first (base layer), R drawn on top — both on the health bar.
            if (show_combo_dmg)
            {
                const float dmg = get_combo_damage(hero);
                if (dmg > 0.f)
                {
                    const float r1 = std::max(0.f, effective_hp - dmg) / max_hp;
                    if (hp_frac > r1)
                    {
                        math::rect rc;
                        rc.left   = hbar.x + ox + r1 * bw;
                        rc.top    = hbar.y + oy;
                        rc.right  = hbar.x + ox + hp_frac * bw;
                        rc.bottom = hbar.y + oy + bh;
                        g_sdk->renderer->add_rectangle_filled(rc, combo_color);
                    }
                }
            }

            if (show_r_dmg)
            {
                const float dmg = get_r_damage(hero);
                if (dmg > 0.f)
                {
                    const float r1 = std::max(0.f, effective_hp - dmg) / max_hp;
                    if (hp_frac > r1)
                    {
                        math::rect rc;
                        rc.left   = hbar.x + ox + r1 * bw;
                        rc.top    = hbar.y + oy;
                        rc.right  = hbar.x + ox + hp_frac * bw;
                        rc.bottom = hbar.y + oy + bh;
                        g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
                    }
                }
            }
        }
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast)
            return;

        auto* player = get_player();
        if (!player || obj->get_network_id() != player->get_network_id())
            return;

        const int slot = cast->get_spell_slot();
        if (slot == SLOT_Q || slot == SLOT_W || slot == SLOT_E)
        {
            const char* cast_name = get_cast_name(cast);
            if (cast_name)
                s_last_observed_cast_name = cast_name;

            const subject selected_subject = get_subject_from_selector_name(cast_name);
            if (selected_subject != subject::none)
            {
                cache_selected_subject(selected_subject, "selector-process-cast");

                if (s_pending.active && s_pending.info.mood == selected_subject)
                {
                    // Game confirmed the selector — fast-forward so follow-up fires
                    // after SUBJECT_CONFIRM_FAST_DELAY instead of the full SUBJECT_CONFIRM_DELAY.
                    const float new_fire = now() + SUBJECT_CONFIRM_FAST_DELAY;
                    if (new_fire < s_pending.fire_at)
                        s_pending.fire_at = new_fire;
                }
                else if (s_pending.active && HWEI_DEBUG_LOGS && g_sdk)
                {
                    g_sdk->log_console("[MeowAIO] selector observed but mood mismatch expected=%s got=%s",
                        expected_followup_name(s_pending.info) ? expected_followup_name(s_pending.info) : "unknown",
                        cast_name ? cast_name : "unknown");
                }
                return;
            }

            if (!is_hwei_followup_spell_name(cast_name))
                return;

            cache_selected_subject(subject::none, cast_name ? cast_name : "followup-process-cast");

            if (cast_name && std::strcmp(cast_name, "HweiEE") == 0)
                s_ee_qq_followup_until = now() + EE_QQ_FOLLOWUP_WINDOW;
            else if (cast_name && std::strcmp(cast_name, "HweiEW") == 0)
            {
                open_ew_qw_followup_window(EW_ROOT_FOLLOWUP_DELAY, "ew-confirm-fallback");
            }
            else if (cast_name && std::strcmp(cast_name, "HweiQQ") == 0)
                s_ee_qq_followup_until = 0.f;
            else if (cast_name && std::strcmp(cast_name, "HweiQW") == 0)
            {
                s_ew_qw_followup_ready_at = 0.f;
                s_ew_qw_followup_until = 0.f;
            }

            if (!s_pending.active || is_expected_followup_name(cast_name))
            {
                if (HWEI_DEBUG_LOGS && g_sdk)
                {
                    g_sdk->log_console("[MeowAIO] confirm expected=%s got=%s",
                        s_pending.active ? (expected_followup_name(s_pending.info) ? expected_followup_name(s_pending.info) : "unknown") : "none",
                        cast_name ? cast_name : "unknown");
                }
                s_last_cast_time = now();
                clear_pending(false, "confirmed-followup");
            }
            else if (HWEI_DEBUG_LOGS && g_sdk)
            {
                g_sdk->log_console("[MeowAIO] nonmatching followup expected=%s got=%s",
                    expected_followup_name(s_pending.info) ? expected_followup_name(s_pending.info) : "unknown",
                    cast_name ? cast_name : "unknown");
            }
        }
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff)
            return;

        auto* player = get_player();
        if (!player || obj->get_team_id() == player->get_team_id())
            return;

        auto* caster = buff->get_caster();
        if (!caster || caster->get_network_id() != player->get_network_id())
            return;

        const auto name = buff->get_name();
        if (name != "HweiEWRoot")
            return;

        // Root is already active — open the window immediately (ready_delay = 0).
        open_ew_qw_followup_window(0.0f, "root-buff");

        if (HWEI_DEBUG_LOGS && g_sdk)
            g_sdk->log_console("[MeowAIO] EW root confirmed, QW followup window opened");
    }
}
