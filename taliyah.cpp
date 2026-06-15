#include "taliyah.h"

#include "infotab_sdk.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace taliyah
{
namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    namespace spell_data
    {
        constexpr float q_range = 1000.f;
        constexpr float q_radius = 100.f;
        constexpr float q_boulder_radius = 140.f;
        constexpr float q_delay = 0.25f;
        constexpr float q_speed = 3600.f;

        constexpr float w_range = 900.f;
        constexpr float w_radius = 150.f;
        constexpr float w_delay = 0.85f;
        constexpr float w_speed = 20000.f; // near-instant indicator; tiny but non-zero travel time

        constexpr float e_range = 950.f;
        constexpr float e_radius = 225.f;
        constexpr float e_delay = 0.25f;

        constexpr float r_ranges[4] = { 0.f, 2500.f, 4500.f, 6500.f };
    }

    namespace menu
    {
        inline bool combo_q = true;
        inline bool combo_w = true;
        inline bool combo_e = true;
        inline bool combo_e_before_w = true;
        inline bool combo_w_into_e = true;
        inline bool q_magnet = true;
        inline int w_direction = 0; // 0 away, 1 toward, 2 cursor, 3 smart

        inline bool harass_q = true;
        inline bool harass_e = false;
        inline int harass_mana_pct = 45;

        inline bool clear_q = true;
        inline bool clear_w = true;
        inline bool clear_e = true;
        inline int clear_mana_pct = 35;
        inline int clear_min_hits = 3;

        inline bool auto_e_cc = true;
        inline bool anti_gap_e = true;
        inline bool auto_ride_r = true;

        inline bool semi_w = false;
        inline std::string semi_w_key;
        inline uint32_t infotab_semi_w = 0;
        inline std::string w_mode_key;
        inline uint32_t infotab_w_mode = 0;

        inline int q_hitchance = pred_sdk::hitchance::high;
        inline int w_hitchance = pred_sdk::hitchance::medium;
        inline int e_hitchance = pred_sdk::hitchance::medium;

        inline bool draw_q = true;
        inline bool draw_w = true;
        inline bool draw_e = true;
        inline bool draw_r = false;
        inline bool draw_w_mode = true;
        inline bool draw_worked_ground = false;
        inline bool draw_only_ready = true;
        inline uint32_t color_q = 0xFF80FFF0u;
        inline uint32_t color_w = 0xFFFFC040u;
        inline uint32_t color_e = 0xFF80B0FFu;
        inline uint32_t color_r = 0xFFFF6060u;
    }

    menu_category* s_menu = nullptr;
    float s_last_spell_cast = 0.f;
    float s_last_magnet_move = 0.f;
    game_object* s_q_target = nullptr;
    math::vector3 s_q_cast_pos {};
    float s_q_magnet_until = 0.f;
    float s_last_ride = 0.f;
    float s_last_w_mode_switch = 0.f;
    math::vector3 s_last_e_pos {};
    float s_e_active_until = 0.f;

    struct ground_object
    {
        uint32_t network_id = 0;
        math::vector3 position {};
        float seen_time = 0.f;
    };

    std::vector<ground_object> s_worked_ground;

    game_object* player()
    {
        if (!g_sdk || !g_sdk->object_manager)
            return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    float game_time()
    {
        if (!g_sdk || !g_sdk->clock_facade)
            return 0.f;
        return g_sdk->clock_facade->get_game_time();
    }

    bool can_cast_now()
    {
        return game_time() - s_last_spell_cast >= 0.08f;
    }

    void mark_cast()
    {
        s_last_spell_cast = game_time();
    }

    std::string lowercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool spell_learned(int slot)
    {
        auto* p = player();
        if (!p)
            return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0;
    }

    bool spell_ready(int slot)
    {
        auto* p = player();
        if (!p)
            return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    bool has_mana(int slot)
    {
        auto* p = player();
        return p && p->get_spell(slot) && p->get_par() >= p->get_spell_mana_cost(slot);
    }

    float mana_pct()
    {
        auto* p = player();
        if (!p || p->get_max_par() <= 0.f)
            return 0.f;
        return p->get_par() / p->get_max_par() * 100.f;
    }

    bool is_valid_enemy(game_object* obj, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead() || !obj->is_targetable() || !obj->is_visible())
            return false;
        if (obj->get_team_id() == p->get_team_id())
            return false;
        return range >= FLT_MAX || p->get_position().distance(obj->get_position()) <= range + obj->get_bounding_radius();
    }

    bool is_casting()
    {
        auto* p = player();
        if (!p)
            return false;
        auto* active = p->get_active_spell_cast();
        return active && active->get_spell_cast() && game_time() < active->get_cast_end_time();
    }

    const char* spell_name(int slot)
    {
        auto* p = player();
        if (!p)
            return nullptr;
        auto* spell = p->get_spell(slot);
        if (!spell || !spell->get_data() || !spell->get_data()->get_static_data())
            return nullptr;
        return spell->get_data()->get_static_data()->get_name();
    }

    bool is_worked_ground_object(game_object* obj)
    {
        if (!obj)
            return false;

        const auto name = lowercase(obj->get_name());
        return name.find("taliyah_") != std::string::npos
            && name.find("_q_buffground") != std::string::npos;
    }

    void cleanup_worked_ground()
    {
        const float now = game_time();
        s_worked_ground.erase(
            std::remove_if(s_worked_ground.begin(), s_worked_ground.end(),
                [now](const ground_object& ground) {
                    return now - ground.seen_time > 55.f;
                }),
            s_worked_ground.end());
    }

    bool on_worked_ground()
    {
        auto* p = player();
        if (!p)
            return false;

        cleanup_worked_ground();
        const auto pos = p->get_position();
        for (const auto& ground : s_worked_ground)
        {
            if (pos.distance(ground.position) <= 275.f)
                return true;
        }
        return false;
    }

    bool e_field_active()
    {
        return game_time() <= s_e_active_until;
    }

    void remember_e_field(const math::vector3& pos)
    {
        s_last_e_pos = pos;
        s_e_active_until = game_time() + 4.15f;
    }

    const char* w_mode_name()
    {
        switch (menu::w_direction)
        {
        case 1:
            return "Toward";
        case 2:
            return "Cursor";
        case 3:
            return "Smart";
        default:
            return "Away";
        }
    }

    uint32_t w_mode_color()
    {
        switch (menu::w_direction)
        {
        case 1:
            return 0xFFFFC040u;
        case 2:
            return 0xFF80B0FFu;
        case 3:
            return 0xFFB060FFu;
        default:
            return 0xFF80FFF0u;
        }
    }

    void cycle_w_mode()
    {
        const float now = game_time();
        if (now - s_last_w_mode_switch < 0.2f)
            return;

        menu::w_direction = (menu::w_direction + 1) % 4;
        s_last_w_mode_switch = now;

        if (g_sdk)
            g_sdk->log_console("[MeowAIO] Taliyah W mode: %s", w_mode_name());
    }

    pred_sdk::spell_data q_data(bool boulder)
    {
        pred_sdk::spell_data d {};
        d.spell_type = pred_sdk::spell_type::linear;
        d.targetting_type = pred_sdk::targetting_type::center_to_edge;
        d.spell_slot = SLOT_Q;
        d.range = spell_data::q_range;
        d.radius = boulder ? spell_data::q_boulder_radius : spell_data::q_radius;
        d.delay = spell_data::q_delay;
        d.projectile_speed = spell_data::q_speed;
        d.expected_hitchance = menu::q_hitchance;
        d.forbidden_collisions = { pred_sdk::collision_type::yasuo_wall, pred_sdk::collision_type::braum_wall };
        return d;
    }

    pred_sdk::spell_data w_data()
    {
        pred_sdk::spell_data d {};
        d.spell_type = pred_sdk::spell_type::circular;
        d.targetting_type = pred_sdk::targetting_type::center;
        d.spell_slot = SLOT_W;
        d.range = spell_data::w_range;
        d.radius = spell_data::w_radius;
        d.delay = spell_data::w_delay;
        d.proc_delay = spell_data::w_delay;
        d.expected_hitchance = menu::w_hitchance;
        return d;
    }

    pred_sdk::spell_data e_data()
    {
        pred_sdk::spell_data d {};
        d.spell_type = pred_sdk::spell_type::circular;
        d.targetting_type = pred_sdk::targetting_type::center;
        d.spell_slot = SLOT_E;
        d.range = spell_data::e_range;
        d.radius = spell_data::e_radius;
        d.delay = spell_data::e_delay;
        d.expected_hitchance = menu::e_hitchance;
        return d;
    }

    game_object* target(float range)
    {
        if (!sdk::target_selector)
            return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_enemy(forced, range))
            return forced;
        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (is_valid_enemy(hero, range))
                return hero;
        }
        return nullptr;
    }

    float self_hp_pct()
    {
        auto* p = player();
        if (!p || p->get_max_hp() <= 0.f)
            return 100.f;
        return p->get_hp() / p->get_max_hp() * 100.f;
    }

    float enemy_hp_pct(game_object* obj)
    {
        if (!obj || obj->get_max_hp() <= 0.f)
            return 100.f;
        return obj->get_hp() / obj->get_max_hp() * 100.f;
    }

    int count_enemy_heroes_near(const math::vector3& pos, float range)
    {
        if (!g_sdk || !g_sdk->object_manager)
            return 0;

        int count = 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero))
                continue;
            if (hero->get_position().distance(pos) <= range)
                ++count;
        }
        return count;
    }

    namespace smart
    {
        constexpr float self_hp_pct_threshold = 30.f;
        constexpr float enemy_hp_pct_threshold = 30.f;
        constexpr int teamfight_count = 2;
        constexpr float teamfight_range = 1200.f;
        constexpr float ally_tower_range = 900.f; // tower attack range (775) + small buffer
    }

    bool under_ally_tower()
    {
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager)
            return false;

        const auto pos = p->get_position();
        const int team = p->get_team_id();

        for (auto* obj : g_sdk->object_manager->get_turrets())
        {
            if (!obj || obj->is_dead() || obj->get_team_id() != team)
                continue;
            if (pos.distance(obj->get_position()) <= smart::ally_tower_range)
                return true;
        }
        return false;
    }

    bool in_teamfight()
    {
        auto* p = player();
        if (!p)
            return false;
        return count_enemy_heroes_near(p->get_position(), smart::teamfight_range)
            >= smart::teamfight_count;
    }

    game_object* lowest_hp_enemy(float range)
    {
        auto* p = player();
        if (!p || !sdk::target_selector)
            return nullptr;

        if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_enemy(forced, range))
            return forced;

        game_object* best = nullptr;
        float best_hp = FLT_MAX;
        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(hero, range))
                continue;
            if (hero->get_hp() < best_hp)
            {
                best = hero;
                best_hp = hero->get_hp();
            }
        }
        return best;
    }

    // Resolves W direction (0 away / 1 toward / 2 cursor) when the menu is in Smart mode.
    // Priority:
    //   self-low-HP   -> away   (peel / escape)
    //   ally tower    -> toward (drag enemy into tower aggro)
    //   teamfight     -> toward (collapse onto squishy)
    //   enemy-low-HP  -> toward (kill secure)
    //   default       -> away
    int resolve_w_direction(game_object* w_target)
    {
        if (menu::w_direction != 3)
            return menu::w_direction;

        if (self_hp_pct() < smart::self_hp_pct_threshold)
            return 0; // shove away to peel

        if (under_ally_tower())
            return 1; // feed them to the tower

        if (in_teamfight())
            return 1; // pull whoever we hit toward our team

        if (w_target && enemy_hp_pct(w_target) < smart::enemy_hp_pct_threshold)
            return 1; // kill secure

        return 0;
    }

    // In Smart + teamfight, override the W target to the lowest-HP enemy in W range.
    // Falls back to the regular TS pick otherwise.
    game_object* resolve_w_target(game_object* default_target)
    {
        if (menu::w_direction == 3 && in_teamfight())
        {
            if (auto* low = lowest_hp_enemy(spell_data::w_range))
                return low;
        }
        return default_target;
    }

    bool cast_position(int slot, const math::vector3& pos)
    {
        if (!sdk::common || !can_cast_now() || !spell_ready(slot) || !has_mana(slot))
            return false;
        if (!sdk::common->cast_spell(slot, pos))
            return false;
        if (slot == SLOT_E)
            remember_e_field(pos);
        mark_cast();
        return true;
    }

    struct w_solution
    {
        bool will_hit = false;
        math::vector3 cast_position {};
        math::vector3 predicted_enemy {};
        float player_to_cast = 0.f;
        float travel_time = 0.f;
        float windup = 0.f;
        float total_time = 0.f;
        float effective_radius = 0.f;
        float miss_distance = 0.f;
    };

    // Manual W hit-check. Math chain:
    //   tTravel = dPlayerToCast / w_speed
    //   tTotal  = w_delay + tTravel
    //   enemyAt(tTotal) = predict_on_path(target, tTotal)
    //   hit if distance(castPos, enemyAt(tTotal)) <= w_radius + target hitbox
    w_solution solve_w(game_object* target_obj, const math::vector3& candidate_cast)
    {
        w_solution s {};
        s.cast_position = candidate_cast;

        auto* p = player();
        if (!p || !target_obj)
            return s;

        s.player_to_cast = p->get_position().distance(candidate_cast);

        s.travel_time = (spell_data::w_speed > 0.f && spell_data::w_speed < FLT_MAX * 0.5f)
            ? s.player_to_cast / spell_data::w_speed
            : 0.f;

        s.windup = spell_data::w_delay;
        s.total_time = s.windup + s.travel_time;

        if (sdk::prediction)
            s.predicted_enemy = sdk::prediction->predict_on_path(target_obj, s.total_time, false);
        else
            s.predicted_enemy = target_obj->get_position() + target_obj->get_velocity() * s.total_time;

        s.effective_radius = spell_data::w_radius + target_obj->get_bounding_radius();
        s.miss_distance = candidate_cast.distance(s.predicted_enemy);
        s.will_hit = s.miss_distance <= s.effective_radius;
        return s;
    }

    bool cast_w_toward(game_object* target_obj, const math::vector3& anchor)
    {
        if (!sdk::prediction || !sdk::common || !target_obj || !can_cast_now() || !spell_ready(SLOT_W) || !has_mana(SLOT_W))
            return false;

        auto pred = sdk::prediction->predict(target_obj, w_data());
        if (!pred.is_valid || pred.hitchance < menu::w_hitchance)
            return false;

        // Validate SDK's chosen cast point with our own math.
        auto solution = solve_w(target_obj, pred.cast_position);
        if (!solution.will_hit)
        {
            // Fallback: snap to where the enemy will actually be at impact time.
            solution = solve_w(target_obj, pred.predicted_position);
            if (!solution.will_hit)
                return false;
        }

        auto* p = player();
        math::vector3 cast_pos = solution.cast_position;
        if (p && p->get_position().distance(cast_pos) > spell_data::w_range)
            cast_pos = p->get_position().extended(cast_pos, spell_data::w_range, true);

        const auto end_pos = cast_pos.extended(anchor, 450.f, true);

        if (!sdk::common->cast_spell(SLOT_W, cast_pos, end_pos))
            return false;

        mark_cast();
        return true;
    }

    bool cast_w(game_object* target_obj, int direction_mode)
    {
        auto* p = player();
        if (!p)
            return false;

        if (direction_mode == 1)
            return cast_w_toward(target_obj, p->get_position());

        if (direction_mode == 2 && g_sdk && g_sdk->hud_manager)
            return cast_w_toward(target_obj, g_sdk->hud_manager->get_cursor_position());

        const auto player_pos = p->get_position();
        const auto target_pos = target_obj ? target_obj->get_position() : player_pos;
        const auto away_anchor = player_pos.extended(target_pos, player_pos.distance(target_pos) + 450.f, true);
        return cast_w_toward(target_obj, away_anchor);
    }

    struct q_solution
    {
        bool will_hit = false;
        math::vector3 predicted_enemy {};
        float player_to_cast = 0.f;
        float travel_time = 0.f;
        float windup = 0.f;
        float total_time = 0.f;
        float along_line = 0.f;   // projection of enemy onto Q's line from player
        float perp_distance = 0.f; // perpendicular distance from line to enemy
        float effective_radius = 0.f;
        float max_along = 0.f;
    };

    // Manual Q hit-check. Q is linear, so we test against the SEGMENT from player to castPos.
    // Math chain (with one refinement step for accuracy):
    //   pass 1: tHit = q_delay + |castPos - player| / q_speed
    //           enemyAt(tHit) = predict_on_path(target, tHit)
    //   pass 2: project enemy onto the line, use that projected distance for tHit, re-predict
    //   hit if projection lies on the segment AND perpendicular distance <= q_radius + hitbox
    q_solution solve_q(game_object* target_obj, const math::vector3& candidate_cast, bool boulder)
    {
        q_solution s {};
        auto* p = player();
        if (!p || !target_obj || !sdk::prediction)
            return s;

        const auto player_pos = p->get_position();
        const float dx = candidate_cast.x - player_pos.x;
        const float dz = candidate_cast.z - player_pos.z;
        const float dir_len = std::sqrt(dx * dx + dz * dz);
        if (dir_len < 1e-3f)
            return s;

        const float udx = dx / dir_len;
        const float udz = dz / dir_len;

        const float q_radius_now = boulder ? spell_data::q_boulder_radius : spell_data::q_radius;
        const float hitbox = target_obj->get_bounding_radius();
        s.effective_radius = q_radius_now + hitbox;
        s.max_along = spell_data::q_range + hitbox;
        s.windup = spell_data::q_delay;
        s.player_to_cast = dir_len;

        // Pass 1: initial hit time assuming the spell reaches castPos.
        float t = s.windup + dir_len / spell_data::q_speed;
        math::vector3 enemy = sdk::prediction->predict_on_path(target_obj, t, false);

        // Pass 2: refine using the projection of that predicted enemy onto the line.
        float ex = enemy.x - player_pos.x;
        float ez = enemy.z - player_pos.z;
        float along = ex * udx + ez * udz;
        if (along > 0.f && along < s.max_along)
        {
            t = s.windup + along / spell_data::q_speed;
            enemy = sdk::prediction->predict_on_path(target_obj, t, false);
            ex = enemy.x - player_pos.x;
            ez = enemy.z - player_pos.z;
            along = ex * udx + ez * udz;
        }

        s.travel_time = t - s.windup;
        s.total_time = t;
        s.predicted_enemy = enemy;
        s.along_line = along;

        // Perpendicular distance: |enemy - (player + udir * along)|
        const float foot_x = player_pos.x + udx * along;
        const float foot_z = player_pos.z + udz * along;
        const float qx = enemy.x - foot_x;
        const float qz = enemy.z - foot_z;
        s.perp_distance = std::sqrt(qx * qx + qz * qz);

        s.will_hit = along >= 0.f
                  && along <= s.max_along
                  && s.perp_distance <= s.effective_radius;
        return s;
    }

    bool cast_q(game_object* target_obj)
    {
        if (!sdk::prediction || !target_obj || !can_cast_now() || !spell_ready(SLOT_Q) || !has_mana(SLOT_Q))
            return false;

        const bool boulder = on_worked_ground();
        auto pred = sdk::prediction->predict(target_obj, q_data(boulder));
        if (!pred.is_valid || pred.hitchance < menu::q_hitchance)
            return false;

        // Veto if our own line math says the SDK's aim point won't actually land.
        // Don't re-aim for linear shots (predicted_position isn't a valid Q endpoint).
        // Wind-wall / Braum-wall collisions are already handled by sdk::prediction.
        const auto solution = solve_q(target_obj, pred.cast_position, boulder);
        if (!solution.will_hit)
            return false;

        if (!cast_position(SLOT_Q, pred.cast_position))
            return false;

        s_q_target = target_obj;
        s_q_cast_pos = pred.cast_position;
        s_q_magnet_until = game_time() + 0.95f;
        return true;
    }

    bool cast_e(game_object* target_obj)
    {
        if (!sdk::prediction || !target_obj || !can_cast_now() || !spell_ready(SLOT_E) || !has_mana(SLOT_E))
            return false;

        auto pred = sdk::prediction->predict(target_obj, e_data());
        if (!pred.is_valid || pred.hitchance < menu::e_hitchance)
            return false;

        return cast_position(SLOT_E, pred.cast_position);
    }

    // Default jungle policy (no menu): clear epic + large camps, skip small babies.
    bool is_allowed_monster(game_object* obj)
    {
        if (!obj || !obj->is_monster())
            return false;
        return obj->is_epic_monster() || obj->is_large_monster();
    }

    int count_units_near(const math::vector3& pos, float radius, bool jungle_only)
    {
        if (!g_sdk || !g_sdk->object_manager)
            return 0;

        int count = 0;
        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (!is_valid_enemy(minion))
                continue;
            if (jungle_only ? !is_allowed_monster(minion) : !minion->is_lane_minion())
                continue;
            if (minion->get_position().distance(pos) <= radius + minion->get_bounding_radius())
                ++count;
        }
        return count;
    }

    game_object* best_clear_target(float range, bool jungle_only)
    {
        if (!g_sdk || !g_sdk->object_manager)
            return nullptr;

        game_object* best = nullptr;
        int best_hits = 0;
        float best_health = 0.f;

        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (!is_valid_enemy(minion, range))
                continue;
            if (jungle_only ? !is_allowed_monster(minion) : !minion->is_lane_minion())
                continue;

            const int hits = count_units_near(minion->get_position(), 260.f, jungle_only);
            if (hits > best_hits || (hits == best_hits && minion->get_max_hp() > best_health))
            {
                best = minion;
                best_hits = hits;
                best_health = minion->get_max_hp();
            }
        }

        if (jungle_only)
            return best;
        return best_hits >= menu::clear_min_hits ? best : nullptr;
    }

    void q_magnet()
    {
        if (!menu::q_magnet || !sdk::common || !s_q_target || game_time() > s_q_magnet_until)
            return;
        if (!is_valid_enemy(s_q_target, spell_data::q_range + 250.f))
            return;
        if (game_time() - s_last_magnet_move < 0.12f)
            return;

        auto* p = player();
        if (!p)
            return;

        const auto target_pos = s_q_target->get_position();
        const auto keep_pos = target_pos.extended(s_q_cast_pos, 775.f, true);
        sdk::common->issue_order(game_object_order::move_to, keep_pos);
        s_last_magnet_move = game_time();
    }

    void auto_e()
    {
        if ((!menu::auto_e_cc && !menu::anti_gap_e) || !spell_ready(SLOT_E) || !has_mana(SLOT_E))
            return;

        if (!sdk::target_selector)
            return;

        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(hero, spell_data::e_range))
                continue;

            if (menu::anti_gap_e && hero->is_dashing())
            {
                cast_position(SLOT_E, hero->get_server_position());
                return;
            }

            if (menu::auto_e_cc && (hero->has_buff_of_type(buff_type::stun)
                || hero->has_buff_of_type(buff_type::snare)
                || hero->has_buff_of_type(buff_type::taunt)
                || hero->has_buff_of_type(buff_type::suppression)
                || hero->has_buff_of_type(buff_type::fear)
                || hero->has_buff_of_type(buff_type::charm)))
            {
                cast_e(hero);
                return;
            }
        }
    }

    void auto_ride()
    {
        if (!menu::auto_ride_r || !spell_ready(SLOT_R) || game_time() - s_last_ride < 1.f)
            return;

        const char* name = spell_name(SLOT_R);
        if (name && std::string(name).find("TaliyahRSurfOptIn") != std::string::npos)
        {
            if (sdk::common && sdk::common->cast_spell(SLOT_R))
                s_last_ride = game_time();
        }
    }

    void combo()
    {
        if (is_casting())
            return;

        auto* q_target = target(spell_data::q_range);
        auto* w_target = resolve_w_target(target(spell_data::w_range));
        auto* e_target = target(spell_data::e_range);

        const int w_dir = resolve_w_direction(w_target);

        if (menu::semi_w && w_target && cast_w(w_target, w_dir))
            return;

        if (menu::combo_e_before_w && menu::combo_e && e_target && cast_e(e_target))
            return;
        if (menu::combo_w_into_e && menu::combo_w && w_target && e_field_active() && cast_w_toward(w_target, s_last_e_pos))
            return;
        if (menu::combo_w && w_target && cast_w(w_target, w_dir))
            return;
        if (!menu::combo_e_before_w && menu::combo_e && e_target && cast_e(e_target))
            return;
        if (menu::combo_q && q_target)
            cast_q(q_target);
    }

    void harass()
    {
        if (mana_pct() < menu::harass_mana_pct || is_casting())
            return;
        if (menu::harass_e)
        {
            if (auto* e_target = target(spell_data::e_range); e_target && cast_e(e_target))
                return;
        }
        if (menu::harass_q)
        {
            if (auto* q_target = target(spell_data::q_range))
                cast_q(q_target);
        }
    }

    void clear()
    {
        if (mana_pct() < menu::clear_mana_pct || is_casting())
            return;

        const bool jungle = sdk::orbwalker && sdk::orbwalker->fast_clear();

        if (jungle)
        {
            auto* monster = best_clear_target(spell_data::q_range, true);
            if (!monster)
                return;

            // Pull the camp toward us so the AoE doesn't shove it out of W range.
            if (menu::clear_e && cast_position(SLOT_E, monster->get_position()))
                return;
            if (menu::clear_w && cast_w(monster, 1))
                return;
            if (menu::clear_q)
                cast_q(monster);
            return;
        }

        if (menu::clear_e)
        {
            if (auto* e_minion = best_clear_target(spell_data::e_range, false); e_minion && cast_position(SLOT_E, e_minion->get_position()))
                return;
        }
        if (menu::clear_w)
        {
            if (auto* w_minion = best_clear_target(spell_data::w_range, false); w_minion && cast_w(w_minion, 0))
                return;
        }
        if (menu::clear_q)
        {
            if (auto* q_minion = best_clear_target(spell_data::q_range, false))
                cast_q(q_minion);
        }
    }

    void draw_circle(const math::vector3& pos, float radius, uint32_t clr)
    {
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, radius, 2.f, color(clr));
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, radius, 2.f, clr);
    }
}

void __fastcall on_update()
{
    if (!player() || !sdk::common)
        return;

    auto_ride();
    auto_e();
    q_magnet();

    if (!sdk::orbwalker)
        return;

    if (sdk::orbwalker->combo())
        combo();
    else if (sdk::orbwalker->harass())
        harass();
    else if (sdk::orbwalker->clear() || sdk::orbwalker->fast_clear())
        clear();
}

void __fastcall on_draw()
{
    auto* p = player();
    if (!p)
        return;

    const auto pos = p->get_position();
    if (menu::draw_q && (!menu::draw_only_ready || spell_ready(SLOT_Q)))
        draw_circle(pos, spell_data::q_range, menu::color_q);
    if (menu::draw_w && (!menu::draw_only_ready || spell_ready(SLOT_W)))
        draw_circle(pos, spell_data::w_range, menu::color_w);
    if (menu::draw_e && (!menu::draw_only_ready || spell_ready(SLOT_E)))
        draw_circle(pos, spell_data::e_range, menu::color_e);
    if (menu::draw_r && spell_learned(SLOT_R))
    {
        auto* r = p->get_spell(SLOT_R);
        const int level = r ? std::clamp(r->get_level(), 0, 3) : 0;
        if (level > 0 && (!menu::draw_only_ready || spell_ready(SLOT_R)))
            draw_circle(pos, spell_data::r_ranges[level], menu::color_r);
    }
    if (menu::draw_worked_ground)
    {
        cleanup_worked_ground();
        for (const auto& ground : s_worked_ground)
            draw_circle(ground.position, 275.f, 0x5530C8FFu);
    }
    if (menu::draw_w_mode && g_sdk && g_sdk->renderer)
    {
        const auto screen = g_sdk->renderer->world_to_screen(pos);
        const std::string text = "W: " + std::string(w_mode_name());
        g_sdk->renderer->add_text(text, 14.f, { screen.x, screen.y + 52.f }, 1, w_mode_color());
    }
}

void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
{
    auto* p = player();
    if (!p || !obj || !cast || obj->get_network_id() != p->get_network_id())
        return;

    const int slot = cast->get_spell_slot();
    if (slot >= SLOT_Q && slot <= SLOT_R)
        mark_cast();
    if (slot == SLOT_E)
        remember_e_field(cast->get_cast_pos());
}

void __fastcall on_create_object(game_object* obj)
{
    if (!is_worked_ground_object(obj))
        return;

    const uint32_t network_id = obj->get_network_id();
    const auto it = std::find_if(s_worked_ground.begin(), s_worked_ground.end(),
        [network_id](const ground_object& ground) {
            return ground.network_id == network_id;
        });

    if (it != s_worked_ground.end())
    {
        it->position = obj->get_position();
        it->seen_time = game_time();
        return;
    }

    s_worked_ground.push_back({ network_id, obj->get_position(), game_time() });
}

void __fastcall on_delete_object(game_object* obj)
{
    if (!obj)
        return;

    const uint32_t network_id = obj->get_network_id();
    s_worked_ground.erase(
        std::remove_if(s_worked_ground.begin(), s_worked_ground.end(),
            [network_id](const ground_object& ground) {
                return ground.network_id == network_id;
            }),
        s_worked_ground.end());
}

void load()
{
    if (!g_sdk || !g_sdk->menu_manager)
        return;

    s_menu = g_sdk->menu_manager->add_category("taliyah", "MeowAIO - Taliyah");
    if (!s_menu)
    {
        g_sdk->log_console("[!] Taliyah: failed to create menu category");
        return;
    }

    auto* q = s_menu->add_sub_category("q_threaded_volley", "Q - Threaded Volley");
    q->add_checkbox("combo", "Use in Combo", menu::combo_q, [](bool v) { menu::combo_q = v; });
    q->add_checkbox("harass", "Use in Harass", menu::harass_q, [](bool v) { menu::harass_q = v; });
    q->add_checkbox("clear", "Use in Clear", menu::clear_q, [](bool v) { menu::clear_q = v; });
    q->add_checkbox("magnet", "Q Magnet", menu::q_magnet, [](bool v) { menu::q_magnet = v; });
    q->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::q_hitchance, [](int v) { menu::q_hitchance = v; });
    q->add_checkbox("draw", "Draw Range", menu::draw_q, [](bool v) { menu::draw_q = v; });

    auto* w = s_menu->add_sub_category("w_seismic_shove", "W - Seismic Shove");
    w->add_checkbox("combo", "Use in Combo", menu::combo_w, [](bool v) { menu::combo_w = v; });
    w->add_hotkey("semi", "Semi-Manual W", 'G', false, false,
        [](std::string* hk, bool pressed) {
            menu::semi_w = pressed;
            if (hk) menu::semi_w_key = *hk;
        });
    w->add_combo("direction", "Shove Direction", { "Away", "Toward", "Cursor", "Smart" }, menu::w_direction,
        [](int v) { menu::w_direction = v; });
    w->add_hotkey("switch_mode", "Switch W Mode", 'U', false, false,
        [](std::string* hk, bool pressed) {
            if (hk)
                menu::w_mode_key = *hk;
            if (pressed)
                cycle_w_mode();
        });
    w->add_checkbox("into_e", "Prioritize W Into E", menu::combo_w_into_e,
        [](bool v) { menu::combo_w_into_e = v; });
    w->add_checkbox("clear", "Use in Clear", menu::clear_w, [](bool v) { menu::clear_w = v; });
    w->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::w_hitchance, [](int v) { menu::w_hitchance = v; });
    w->add_checkbox("draw", "Draw Range", menu::draw_w, [](bool v) { menu::draw_w = v; });

    auto* e = s_menu->add_sub_category("e_unraveled_earth", "E - Unraveled Earth");
    e->add_checkbox("combo", "Use in Combo", menu::combo_e, [](bool v) { menu::combo_e = v; });
    e->add_checkbox("combo_pre_w", "Cast E Before W", menu::combo_e_before_w, [](bool v) { menu::combo_e_before_w = v; });
    e->add_checkbox("harass", "Use in Harass", menu::harass_e, [](bool v) { menu::harass_e = v; });
    e->add_checkbox("auto_cc", "Auto E on CC", menu::auto_e_cc, [](bool v) { menu::auto_e_cc = v; });
    e->add_checkbox("anti_gap", "Anti Gapclose E", menu::anti_gap_e, [](bool v) { menu::anti_gap_e = v; });
    e->add_checkbox("clear", "Use in Clear", menu::clear_e, [](bool v) { menu::clear_e = v; });
    e->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::e_hitchance, [](int v) { menu::e_hitchance = v; });
    e->add_checkbox("draw", "Draw Range", menu::draw_e, [](bool v) { menu::draw_e = v; });

    auto* r = s_menu->add_sub_category("r_weavers_wall", "R - Weaver's Wall");
    r->add_checkbox("auto_ride", "Auto Ride R", menu::auto_ride_r, [](bool v) { menu::auto_ride_r = v; });
    r->add_checkbox("draw", "Draw Range", menu::draw_r, [](bool v) { menu::draw_r = v; });

    auto* clear_menu = s_menu->add_sub_category("Mana Thresholds", "Mana Thresholds");
    clear_menu->add_slider_int("min_hits", "Lane Clear Min Hits", 1, 6, 1, menu::clear_min_hits,
        [](int v) { menu::clear_min_hits = v; });
    clear_menu->add_slider_int("mana_clear", "Clear Mana %", 0, 100, 5, menu::clear_mana_pct,
        [](int v) { menu::clear_mana_pct = v; });
    clear_menu->add_slider_int("mana_harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
        [](int v) { menu::harass_mana_pct = v; });

    auto* draw = s_menu->add_sub_category("drawings", "Drawings");
    draw->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
        [](bool v) { menu::draw_only_ready = v; });
    draw->add_checkbox("w_mode", "Draw W Mode", menu::draw_w_mode,
        [](bool v) { menu::draw_w_mode = v; });
    draw->add_checkbox("worked_ground", "Draw Worked Ground", menu::draw_worked_ground,
        [](bool v) { menu::draw_worked_ground = v; });
    draw->add_colorpicker("col_q", "Q Color", menu::color_q, [](uint32_t c) { menu::color_q = c; });
    draw->add_colorpicker("col_w", "W Color", menu::color_w, [](uint32_t c) { menu::color_w = c; });
    draw->add_colorpicker("col_e", "E Color", menu::color_e, [](uint32_t c) { menu::color_e = c; });
    draw->add_colorpicker("col_r", "R Color", menu::color_r, [](uint32_t c) { menu::color_r = c; });

    g_sdk->event_manager->register_callback(event_manager::event::game_update,
        reinterpret_cast<void*>(on_update));
    g_sdk->event_manager->register_callback(event_manager::event::draw_world,
        reinterpret_cast<void*>(on_draw));
    g_sdk->event_manager->register_callback(event_manager::event::process_cast,
        reinterpret_cast<void*>(on_process_cast));
    g_sdk->event_manager->register_callback(event_manager::event::create_object,
        reinterpret_cast<void*>(on_create_object));
    g_sdk->event_manager->register_callback(event_manager::event::delete_object,
        reinterpret_cast<void*>(on_delete_object));

    if (sdk::infotab)
    {
        menu::infotab_w_mode = sdk::infotab->add_hotkey_text(
            &menu::w_mode_key, { "W Mode", 0xFFFFFFFFu },
            []() -> infotab_sdk::text_entry {
                return { w_mode_name(), w_mode_color() };
            });

        menu::infotab_semi_w = sdk::infotab->add_hotkey_text(
            &menu::semi_w_key, { "Semi W", 0xFFFFFFFFu },
            []() -> infotab_sdk::text_entry {
                return { menu::semi_w ? "ON" : "OFF", menu::semi_w ? 0xFF00FF00u : 0xFFFF0000u };
            });
    }

    g_sdk->log_console("[MeowAIO] Taliyah module loaded");
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
    }

    if (sdk::infotab && menu::infotab_semi_w)
    {
        sdk::infotab->remove_text(menu::infotab_semi_w);
        menu::infotab_semi_w = 0;
    }
    if (sdk::infotab && menu::infotab_w_mode)
    {
        sdk::infotab->remove_text(menu::infotab_w_mode);
        menu::infotab_w_mode = 0;
    }

    s_menu = nullptr;
    s_last_spell_cast = 0.f;
    s_last_magnet_move = 0.f;
    s_q_target = nullptr;
    s_q_cast_pos = {};
    s_q_magnet_until = 0.f;
    s_last_ride = 0.f;
    s_last_w_mode_switch = 0.f;
    s_last_e_pos = {};
    s_e_active_until = 0.f;
    s_worked_ground.clear();
}
}
