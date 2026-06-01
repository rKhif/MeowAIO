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

        enum class cast_engine_state : uint8_t
        {
            idle,
            selecting_subject,
            await_followup_window,
            casting_followup,
        };

        struct cast_intent
        {
            bool active = false;
            ability_info info{};
            math::vector3 cast_pos{};
            game_object* target = nullptr;
            float max_cast_range = FLT_MAX;
            float timeout_at = 0.f;
            bool selector_sent = false;
            float selector_sent_at = 0.f;
        };

        constexpr ability_info ABILITY_QQ{ subject::disaster, SLOT_Q, ability_cast::position };
        constexpr ability_info ABILITY_QW{ subject::disaster, SLOT_W, ability_cast::position };
        constexpr ability_info ABILITY_QE{ subject::disaster, SLOT_E, ability_cast::position };
        constexpr ability_info ABILITY_WQ{ subject::serenity, SLOT_Q, ability_cast::position };
        constexpr ability_info ABILITY_WE{ subject::serenity, SLOT_E, ability_cast::self };
        constexpr ability_info ABILITY_EQ{ subject::torment,  SLOT_Q, ability_cast::position };
        constexpr ability_info ABILITY_EE{ subject::torment,  SLOT_E, ability_cast::position };
        constexpr ability_info ABILITY_R { subject::none,      SLOT_R, ability_cast::position };

        constexpr float MIN_CAST_INTERVAL = 0.08f;
        constexpr float SUBJECT_MEMORY = 0.8f;
        constexpr float SUBJECT_CONFIRM_DELAY = 0.26f;
        constexpr float EE_QQ_FOLLOWUP_WINDOW = 1.25f;

        static menu_category* s_menu = nullptr;
        static pred_sdk::spell_data s_pred_qq{};
        static pred_sdk::spell_data s_pred_qw{};
        static pred_sdk::spell_data s_pred_wq{};
        static pred_sdk::spell_data s_pred_qe{};
        static pred_sdk::spell_data s_pred_eq{};
        static pred_sdk::spell_data s_pred_ee{};
        static pred_sdk::spell_data s_pred_r{};

        static float s_last_cast_time = 0.f;
        static float s_ee_qq_followup_until = 0.f;
        static cast_engine_state s_cast_state = cast_engine_state::idle;
        static cast_intent s_intent{};
        static uint32_t s_infotab_combo_mode = 0;
        static bool s_combo_mode_was_pressed = false;

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
            s_pred_qe.forbidden_collisions = {
                pred_sdk::collision_type::unit,
                pred_sdk::collision_type::hero,
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

        subject get_game_subject()
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
            if (std::strcmp(name, "HweiQ") == 0 || strstr(name, "HweiQQ") || strstr(name, "HweiQW") || strstr(name, "HweiQE"))
                return subject::disaster;
            if (std::strcmp(name, "HweiW") == 0 || strstr(name, "HweiWQ") || strstr(name, "HweiWW") || strstr(name, "HweiWE"))
                return subject::serenity;
            if (std::strcmp(name, "HweiE") == 0 || strstr(name, "HweiEQ") || strstr(name, "HweiEW") || strstr(name, "HweiEE"))
                return subject::torment;
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

        void reset_cast_engine()
        {
            s_cast_state = cast_engine_state::idle;
            s_intent = {};
        }

        void forget_subject_if_needed()
        {
            if (!s_intent.active)
                return;
            if (now() <= s_intent.timeout_at)
                return;
            reset_cast_engine();
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
            return true;
        }

        bool queue_cast_intent(const ability_info& info, const math::vector3& cast_pos, game_object* target, float max_cast_range = FLT_MAX, float timeout = SUBJECT_MEMORY)
        {
            auto* player = get_player();
            if (!player) return false;

            if (info.cast_type == ability_cast::position && max_cast_range < FLT_MAX)
            {
                if (player->get_position().distance(cast_pos) > max_cast_range)
                    return false;
            }

            if (!can_queue_subject_cast(info))
                return false;

            if (s_intent.active)
            {
                if (s_intent.info.mood != info.mood || s_intent.info.slot != info.slot)
                    return false;
                s_intent.cast_pos = cast_pos;
                s_intent.target = target;
                s_intent.max_cast_range = max_cast_range;
                return true;
            }

            s_intent.active = true;
            s_intent.info = info;
            s_intent.cast_pos = cast_pos;
            s_intent.target = target;
            s_intent.max_cast_range = max_cast_range;
            s_intent.timeout_at = now() + timeout;
            s_intent.selector_sent = false;
            s_intent.selector_sent_at = 0.f;
            s_cast_state = cast_engine_state::selecting_subject;
            return true;
        }

        bool execute_cast_engine()
        {
            if (!s_intent.active)
                return false;

            auto* player = get_player();
            if (!player || !sdk::common)
            {
                reset_cast_engine();
                return false;
            }

            const float t = now();
            if (t > s_intent.timeout_at)
            {
                reset_cast_engine();
                return false;
            }

            if (t - s_last_cast_time < MIN_CAST_INTERVAL)
                return false;

            if (s_intent.info.mood == subject::none)
            {
                bool casted = false;
                switch (s_intent.info.cast_type)
                {
                case ability_cast::position:
                    if (s_intent.max_cast_range < FLT_MAX
                        && player->get_position().distance(s_intent.cast_pos) > s_intent.max_cast_range)
                        return false;
                    casted = sdk::common->cast_spell(s_intent.info.slot, s_intent.cast_pos);
                    break;
                case ability_cast::target:
                    if (s_intent.target)
                        casted = sdk::common->cast_spell(s_intent.info.slot, s_intent.target);
                    break;
                case ability_cast::self:
                    casted = sdk::common->cast_spell(s_intent.info.slot);
                    break;
                }
                if (casted)
                {
                    s_last_cast_time = t;
                    reset_cast_engine();
                    return true;
                }
                return false;
            }

            const subject live_subject = get_game_subject();

            if (s_cast_state == cast_engine_state::selecting_subject)
            {
                if (live_subject == s_intent.info.mood)
                {
                    s_cast_state = cast_engine_state::await_followup_window;
                    if (s_intent.selector_sent_at <= 0.f)
                        s_intent.selector_sent_at = t;
                }
                else if (!s_intent.selector_sent)
                {
                    if (!player->cast_hwei_mood(static_cast<int>(s_intent.info.mood)))
                        return false;
                    s_intent.selector_sent = true;
                    s_intent.selector_sent_at = t;
                    s_cast_state = cast_engine_state::await_followup_window;
                    return false;
                }
                else
                {
                    s_cast_state = cast_engine_state::await_followup_window;
                }
            }

            if (s_cast_state == cast_engine_state::await_followup_window)
            {
                if (t - s_intent.selector_sent_at < SUBJECT_CONFIRM_DELAY)
                    return false;

                bool casted = false;
                switch (s_intent.info.cast_type)
                {
                case ability_cast::position:
                    if (s_intent.max_cast_range < FLT_MAX)
                    {
                        if (player->get_position().distance(s_intent.cast_pos) > s_intent.max_cast_range)
                            return false;
                    }
                    casted = sdk::common->cast_spell(s_intent.info.slot, s_intent.cast_pos);
                    break;
                case ability_cast::target:
                    if (s_intent.target)
                        casted = sdk::common->cast_spell(s_intent.info.slot, s_intent.target);
                    break;
                case ability_cast::self:
                    casted = sdk::common->cast_spell(s_intent.info.slot);
                    break;
                }

                if (casted)
                {
                    s_last_cast_time = t;
                    s_cast_state = cast_engine_state::casting_followup;
                    return true;
                }
            }

            return false;
        }

        bool try_cast_linear(const ability_info& info, pred_sdk::spell_data base_spell, int hitchance)
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
            if (!prediction.is_valid || prediction.hitchance < hitchance)
                return false;

            // Prevent forced walk-in: never cast if predicted point is out of range.
            const float cast_dist = player->get_position().distance(prediction.cast_position);
            if (cast_dist > base_spell.range)
                return false;

            if (!queue_cast_intent(info, prediction.cast_position, target, base_spell.range))
                return false;
            return execute_cast_engine();
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
            if (!prediction.is_valid || prediction.hitchance < hitchance)
                return false;

            // Prevent forced walk-in: never cast if predicted point is out of range.
            const float cast_dist = player->get_position().distance(prediction.cast_position);
            if (cast_dist > base_spell.range)
                return false;

            if (!queue_cast_intent(info, prediction.cast_position, target, base_spell.range))
                return false;
            return execute_cast_engine();
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

            if (!queue_cast_intent(ABILITY_QQ, cast_pos, target, s_pred_qq.range))
                return false;
            return execute_cast_engine();
        }

        bool try_cast_qw(int hitchance)
        {
            return try_cast_circular(ABILITY_QW, s_pred_qw, hitchance);
        }

        bool try_cast_qe(int hitchance)
        {
            return try_cast_linear(ABILITY_QE, s_pred_qe, hitchance);
        }

        bool try_cast_we()
        {
            if (!queue_cast_intent(ABILITY_WE, {}, nullptr))
                return false;
            return execute_cast_engine();
        }

        bool try_cast_eq(int hitchance)
        {
            return try_cast_linear(ABILITY_EQ, s_pred_eq, hitchance);
        }

        bool try_cast_ee(int hitchance)
        {
            return try_cast_circular(ABILITY_EE, s_pred_ee, hitchance);
        }

        bool try_cast_r(int hitchance)
        {
            if (!menu::combo_use_r) return false;
            if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R)) return false;
            return try_cast_linear(ABILITY_R, s_pred_r, hitchance);
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

            // Keep follow-up window active, but don't block other combo actions.
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
            if (!queue_cast_intent(ABILITY_WQ, cast_pos, nullptr, s_pred_wq.range))
                return false;
            return execute_cast_engine();
        }

        float get_r_damage(game_object* target)
        {
            auto* player = get_player();
            if (!player || !target || !sdk::damage) return 0.f;

            auto* r_spell = player->get_spell(SLOT_R);
            if (!r_spell || r_spell->get_level() <= 0) return 0.f;

            const int level = std::clamp(r_spell->get_level(), 0, 3);
            constexpr float BASE[] = { 0.f, 200.f, 360.f, 490.f };
            constexpr float AP_RATIO = 0.95f;

            const float ap = player->get_ability_power();
            const float raw = BASE[level] + AP_RATIO * ap;

            return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
        }

        bool handle_hotkeys()
        {
            if (s_intent.active)
                return execute_cast_engine();

            if (menu::bind_r_active && try_cast_r(menu::combo_r_hitchance))
                return true;
            if (menu::bind_we_active && try_cast_we())
                return true;
            if (menu::bind_qw_active && try_cast_qw(menu::combo_qw_hitchance))
                return true;
            if (menu::bind_ee_active && try_cast_ee(menu::combo_ee_hitchance))
                return true;
            return false;
        }

        void combo()
        {
            if (is_casting())
                return;
            if (s_intent.active)
            {
                execute_cast_engine();
                return;
            }

            switch (static_cast<combo_mode>(menu::combo_mode_sel))
            {
            case combo_mode::basic:
            default:
                if (handle_ee_qq_followup()) return;
                if (try_cast_r(menu::combo_r_hitchance)) return;
                if (try_cast_we()) return;
                if (try_cast_ee(menu::combo_ee_hitchance)) return;
                if (try_cast_qq(menu::combo_qq_hitchance)) return;
                break;
            }
        }

        void harass()
        {
            if (is_casting())
                return;
            if (s_intent.active)
            {
                execute_cast_engine();
                return;
            }

            if (mana_percent() < static_cast<float>(menu::harass_mana_pct))
                return;

            if (menu::harass_use_we && try_cast_we())
                return;

            if (menu::harass_use_qe && try_cast_linear(ABILITY_QE, s_pred_qe, menu::combo_qe_hitchance))
                return;

            if (menu::harass_use_qw && try_cast_circular(ABILITY_QW, s_pred_qw, menu::combo_qw_hitchance))
                return;

            if (menu::harass_use_qq && try_cast_linear(ABILITY_QQ, s_pred_qq, menu::combo_qq_hitchance))
                return;
        }

        void flee()
        {
            if (is_casting())
                return;
            if (s_intent.active)
            {
                execute_cast_engine();
                return;
            }

            cast_wq_flee();
        }

        void lane()
        {
            if (is_casting())
                return;
            if (s_intent.active)
            {
                execute_cast_engine();
                return;
            }

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
                if (queue_cast_intent(ABILITY_QE, best_pos, nullptr, s_pred_qe.range))
                    execute_cast_engine();
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

            subject active_subject = get_game_subject();

            bool show_q_group = active_subject == subject::disaster;
            bool show_e_group = active_subject == subject::torment;
            if (menu::draw_only_active_spell)
            {
                if (show_q_group)
                {
                    if (menu::draw_qq)
                        draw_circle(s_pred_qq.range, draw_ability_color(ABILITY_QQ, menu::color_qq));
                    else if (menu::draw_qw)
                        draw_circle(s_pred_qw.range, draw_ability_color(ABILITY_QW, menu::color_qw));
                    else if (menu::draw_qe)
                        draw_circle(s_pred_qe.range, draw_ability_color(ABILITY_QE, menu::color_qe));
                }
                if (show_e_group)
                {
                    if (menu::draw_eq)
                        draw_circle(s_pred_eq.range, draw_ability_color(ABILITY_EQ, menu::color_eq));
                    else if (menu::draw_ee)
                        draw_circle(s_pred_ee.range, draw_ability_color(ABILITY_EE, menu::color_ee));
                }
                if (menu::draw_r)
                    draw_circle(s_pred_r.range, draw_color(SLOT_R, menu::color_r));
                return;
            }

            if (menu::draw_qq)
                draw_circle(s_pred_qq.range, draw_ability_color(ABILITY_QQ, menu::color_qq));
            if (menu::draw_qw)
                draw_circle(s_pred_qw.range, draw_ability_color(ABILITY_QW, menu::color_qw));
            if (menu::draw_qe)
                draw_circle(s_pred_qe.range, draw_ability_color(ABILITY_QE, menu::color_qe));
            if (menu::draw_eq)
                draw_circle(s_pred_eq.range, draw_ability_color(ABILITY_EQ, menu::color_eq));
            if (menu::draw_ee)
                draw_circle(s_pred_ee.range, draw_ability_color(ABILITY_EE, menu::color_ee));
            if (menu::draw_r)
                draw_circle(s_pred_r.range, draw_color(SLOT_R, menu::color_r));
        }
    }

    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager)
            return;

        setup_prediction_templates();

        s_menu = g_sdk->menu_manager->add_category("hwei", "MeowAIO - Hwei");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Hwei: failed to create menu category");
            return;
        }

        if (auto* combo_menu = s_menu->add_sub_category("combo", "Combo"))
        {
            combo_menu->add_combo("mode", "Combo Mode",
                { "Basic (WE > EE > QQ)" }, menu::combo_mode_sel,
                [](int v) { menu::combo_mode_sel = v; });
            combo_menu->add_hotkey("mode_cycle", "Cycle Combo Mode", 'J', false, false,
                [](std::string* key, bool pressed) {
                    if (key && menu::combo_mode_cycle_key.empty())
                        menu::combo_mode_cycle_key = *key;
                    if (pressed && !s_combo_mode_was_pressed)
                    {
                        constexpr int MODE_COUNT = 1;
                        menu::combo_mode_sel = (menu::combo_mode_sel + 1) % MODE_COUNT;
                        if (g_sdk)
                        {
                            static constexpr const char* names[] = { "Basic" };
                            g_sdk->log_console("[+] Hwei Combo Mode: %s", names[menu::combo_mode_sel]);
                        }
                    }
                    s_combo_mode_was_pressed = pressed;
                });
            combo_menu->add_checkbox("draw_indicator", "Draw Mode Indicator", menu::draw_combo_mode_indicator,
                [](bool v) { menu::draw_combo_mode_indicator = v; });
        }

        if (auto* q_menu = s_menu->add_sub_category("q", "Q - Subject: Disaster"))
        {
            q_menu->add_hotkey("bind_qw", "Semi-Manual QW", 'G', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_qw_active = pressed;
                    if (key) menu::bind_qw_key = *key;
                });
            q_menu->add_slider_int("hc_qq", "  QQ Hitchance", 0, 100, 5, menu::combo_qq_hitchance,
                [](int v) { menu::combo_qq_hitchance = v; });
            q_menu->add_slider_int("hc_qw", "  QW Hitchance", 0, 100, 5, menu::combo_qw_hitchance,
                [](int v) { menu::combo_qw_hitchance = v; });
            q_menu->add_slider_int("hc_qe", "  QE Hitchance", 0, 100, 5, menu::combo_qe_hitchance,
                [](int v) { menu::combo_qe_hitchance = v; });
            q_menu->add_checkbox("draw_qq", "Draw QQ", menu::draw_qq,
                [](bool v) { menu::draw_qq = v; });
            q_menu->add_checkbox("draw_qw", "Draw QW", menu::draw_qw,
                [](bool v) { menu::draw_qw = v; });
            q_menu->add_checkbox("draw_qe", "Draw QE", menu::draw_qe,
                [](bool v) { menu::draw_qe = v; });
            q_menu->add_checkbox("lane_qe", "Lane Clear QE", menu::lane_use_qe,
                [](bool v) { menu::lane_use_qe = v; });
            q_menu->add_slider_int("lane_qe_min", "Lane Clear Min Minions", 1, 10, 1, menu::lane_qe_min_minions,
                [](int v) { menu::lane_qe_min_minions = v; });
            q_menu->add_checkbox("harass_qq", "Harass QQ", menu::harass_use_qq,
                [](bool v) { menu::harass_use_qq = v; });
            q_menu->add_checkbox("harass_qw", "Harass QW", menu::harass_use_qw,
                [](bool v) { menu::harass_use_qw = v; });
            q_menu->add_checkbox("harass_qe", "Harass QE", menu::harass_use_qe,
                [](bool v) { menu::harass_use_qe = v; });
        }

        if (auto* w_menu = s_menu->add_sub_category("w", "W - Subject: Serenity"))
        {
            w_menu->add_hotkey("bind_we", "Semi-Manual WE", 'H', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_we_active = pressed;
                    if (key) menu::bind_we_key = *key;
                });
            w_menu->add_checkbox("flee_wq", "Use in Flee WQ", menu::flee_use_wq,
                [](bool v) { menu::flee_use_wq = v; });
            w_menu->add_checkbox("harass_we", "Harass WE", menu::harass_use_we,
                [](bool v) { menu::harass_use_we = v; });
        }

        if (auto* e_menu = s_menu->add_sub_category("e", "E - Subject: Torment"))
        {
            e_menu->add_hotkey("bind_ee", "Semi-Manual EE", 'V', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_ee_active = pressed;
                    if (key) menu::bind_ee_key = *key;
                });
            e_menu->add_slider_int("hc_eq", "  EQ Hitchance", 0, 100, 5, menu::combo_eq_hitchance,
                [](int v) { menu::combo_eq_hitchance = v; });
            e_menu->add_slider_int("hc_ee", "  EE Hitchance", 0, 100, 5, menu::combo_ee_hitchance,
                [](int v) { menu::combo_ee_hitchance = v; });
            e_menu->add_checkbox("draw_eq", "Draw EQ", menu::draw_eq,
                [](bool v) { menu::draw_eq = v; });
            e_menu->add_checkbox("draw_ee", "Draw EE", menu::draw_ee,
                [](bool v) { menu::draw_ee = v; });
        }

        if (auto* r_menu = s_menu->add_sub_category("r", "R - Spiraling Despair"))
        {
            r_menu->add_checkbox("combo_r", "Use in Combo R", menu::combo_use_r,
                [](bool v) { menu::combo_use_r = v; });
            r_menu->add_hotkey("bind_r", "Semi-Manual R", 'T', false, false,
                [](std::string* key, bool pressed)
                {
                    menu::bind_r_active = pressed;
                    if (key) menu::bind_r_key = *key;
                });
            r_menu->add_slider_int("hc_r", "  R Hitchance", 0, 100, 5, menu::combo_r_hitchance,
                [](int v) { menu::combo_r_hitchance = v; });
            r_menu->add_checkbox("draw_r", "Draw R", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
        }

        if (auto* mana_menu = s_menu->add_sub_category("mana", "Mana"))
        {
            mana_menu->add_slider_int("harass_mana", "Harass Min Mana %", 0, 100, 5, menu::harass_mana_pct,
                [](int v) { menu::harass_mana_pct = v; });
        }

        if (auto* draw_menu = s_menu->add_sub_category("draw", "Drawings"))
        {
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_checkbox("draw_only_active", "Only Active Spells (off will draw all ranges at once)", menu::draw_only_active_spell,
                [](bool v) { menu::draw_only_active_spell = v; });
            draw_menu->add_checkbox("draw_r_damage", "Draw R Damage Indicator", menu::draw_r_damage_indicator,
                [](bool v) { menu::draw_r_damage_indicator = v; });
            draw_menu->add_colorpicker("color_qq", "  QQ Color", menu::color_qq,
                [](uint32_t c) { menu::color_qq = c; });
            draw_menu->add_colorpicker("color_qw", "  QW Color", menu::color_qw,
                [](uint32_t c) { menu::color_qw = c; });
            draw_menu->add_colorpicker("color_qe", "  QE Color", menu::color_qe,
                [](uint32_t c) { menu::color_qe = c; });
            draw_menu->add_colorpicker("color_eq", "  EQ Color", menu::color_eq,
                [](uint32_t c) { menu::color_eq = c; });
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
                    static constexpr const char* names[] = { "Basic" };
                    static constexpr uint32_t colors[] = { 0xFF00FF80u };
                    const int m = (menu::combo_mode_sel >= 0) ? menu::combo_mode_sel : 0;
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
        }

        if (sdk::infotab && s_infotab_combo_mode)
            sdk::infotab->remove_text(s_infotab_combo_mode);
        s_infotab_combo_mode = 0;

        s_menu = nullptr;
        s_last_cast_time = 0.f;
        s_ee_qq_followup_until = 0.f;
        reset_cast_engine();

        g_sdk->log_console("[-] Hwei module unloaded");
    }

    void __fastcall on_update()
    {
        forget_subject_if_needed();
        execute_cast_engine();

        auto* player = get_player();
        if (!player || player->is_dead())
            return;

        if (handle_hotkeys())
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
                static constexpr const char* mode_names[] = { "Basic" };
                const int m = (menu::combo_mode_sel >= 0) ? menu::combo_mode_sel : 0;
                const std::string text = std::string("Combo: ") + mode_names[m];
                g_sdk->renderer->add_text(text.c_str(), 16.f,
                    { screen.x, screen.y + 42.f }, 1, 0xFF00FF80u);
            }
        }
    }

    void __fastcall on_present()
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->renderer)
            return;

        if (!menu::draw_r_damage_indicator)
            return;

        auto* player = get_player();
        if (!player || player->is_dead())
            return;

        auto* r_spell = player->get_spell(SLOT_R);
        if (!r_spell || r_spell->get_level() == 0)
            return;

        const float scale = static_cast<float>(g_sdk->renderer->get_window_height()) / 1080.f;
        const float ox = -45.6f * scale;
        const float oy = 15.5f * scale;
        const float bw = 106.4f * scale;
        const float bh = 11.5f * scale;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!hero || !hero->is_valid() || hero->is_dead())
                continue;
            if (hero->get_team_id() == player->get_team_id())
                continue;
            if (!hero->is_visible())
                continue;

            const float dmg = get_r_damage(hero);
            if (dmg <= 0.f)
                continue;

            const float hp = hero->get_hp();
            const float max_hp = hero->get_max_hp();
            if (hp <= 0.f || max_hp <= 0.f)
                continue;

            const auto hbar = hero->get_health_bar_position();
            if (hbar.x < 1.f && hbar.y < 1.f)
                continue;

            const float r0 = hp / max_hp;
            const float r1 = std::max(0.f, hp - dmg) / max_hp;
            if (r0 <= r1)
                continue;

            math::rect rc;
            rc.left   = hbar.x + ox + r1 * bw;
            rc.top    = hbar.y + oy;
            rc.right  = hbar.x + ox + r0 * bw;
            rc.bottom = hbar.y + oy + bh;

            g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
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
            const subject selected_subject = get_subject_from_selector_name(cast_name);
            if (selected_subject != subject::none)
            {
                if (s_intent.active && s_intent.info.mood == selected_subject)
                {
                    s_intent.selector_sent = true;
                    s_intent.selector_sent_at = now();
                    s_cast_state = cast_engine_state::await_followup_window;
                }
                return;
            }

            if (!is_hwei_followup_spell_name(cast_name))
                return;

            if (cast_name && std::strcmp(cast_name, "HweiEE") == 0)
                s_ee_qq_followup_until = now() + EE_QQ_FOLLOWUP_WINDOW;
            else if (cast_name && std::strcmp(cast_name, "HweiQQ") == 0)
                s_ee_qq_followup_until = 0.f;

            s_last_cast_time = now();
            reset_cast_engine();
        }
    }
}
