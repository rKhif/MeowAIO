#include "jax.h"

#include "db/champions/jax.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace jax
{
namespace data = venplus::db::champions::jax_const;

namespace menu
{
    inline bool combo_use_q = true;
    inline int  combo_q_min_range = 250;
    inline bool combo_q_only_out_aa = true;
    inline bool combo_q_killable = true;
    inline bool combo_q_escaping = true;
    inline bool combo_use_w = true;
    inline bool combo_w_q_gapclose = true;
    inline bool combo_use_e = true;
    inline bool combo_e_midair_recast = true;
    inline bool combo_e_wait_max_charge = true;
    inline int  combo_e_recast_min_enemies = 1;
    inline bool auto_e_critical_aa = true;
    inline int  auto_e_critical_min_chance = 25;
    inline bool combo_use_r = true;
    inline int  combo_r_hp  = 40;

    inline bool harass_use_q = false;
    inline int  harass_q_min_range = 250;
    inline bool harass_use_w = true;
    inline int  harass_mana  = 45;

    inline bool clear_use_w = true;
    inline bool clear_use_e = false;
    inline int  clear_mana  = 30;

    inline bool flee_use_q = true;
    inline bool flee_q_ally_heroes = true;
    inline bool flee_q_ally_minions = true;
    inline bool flee_q_ward_jump = true;
    inline bool ks_q_active = false;
    inline std::string ks_q_key_str = "X";

    inline bool draw_q_range = true;
    inline bool draw_e_range = true;
    inline bool draw_e_timer = true;
    inline bool draw_only_ready = true;
    inline uint32_t color_q = 0x8000FFFF;
    inline uint32_t color_e = 0x80FFFFFF;
}

namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    menu_category* s_menu = nullptr;
    bool  e_active = false;
    float e_start_time = 0.f;
    bool  pending_e_recast = false;
    uint32_t pending_e_recast_target_id = 0;
    float pending_e_recast_time = 0.f;
    bool  ward_jump_pending = false;
    float ward_jump_time = 0.f;
    constexpr float WARD_JUMP_TIMEOUT = 0.6f;
    constexpr float WARD_PLACE_RANGE = 600.f;
    constexpr float Q_LEAP_SPEED = 1600.f;
    constexpr float E_RECAST_EXPIRY_BUFFER = 0.15f;
    constexpr float E_LEAVE_RANGE_CHECK_TIME = 0.25f;
    constexpr float E_LEAVE_RANGE_BUFFER = 35.f;

    bool is_jax_buff(buff_instance* buff, uint32_t hash, const char* name_fragment)
    {
        if (!buff) return false;
        if (hash != 0u) return buff->get_hash() == hash;
        return buff->get_name().find(name_fragment) != std::string::npos;
    }

    game_object* player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    buff_instance* active_e_buff()
    {
        auto* p = player();
        if (!p) return nullptr;

        if constexpr (data::BUFF_E_EVASION != 0u)
        {
            auto* buff = p->get_buff_by_hash(data::BUFF_E_EVASION);
            if (buff && buff->is_active())
                return buff;
        }

        static std::string e_buff_name = "JaxCounterStrike";
        if (auto* buff = p->get_buff_by_name(e_buff_name); buff && buff->is_active())
            return buff;

        for (auto* buff : p->get_buffs())
        {
            if (is_jax_buff(buff, data::BUFF_E_EVASION, data::BUFF_NAME_E) && buff->is_active())
                return buff;
        }

        return nullptr;
    }

    void sync_e_state_from_buff()
    {
        if (!g_sdk || !g_sdk->clock_facade) return;

        auto* buff = active_e_buff();
        if (!buff)
        {
            if (e_active)
            {
                e_active = false;
                e_start_time = 0.f;
                pending_e_recast = false;
                pending_e_recast_target_id = 0;
                pending_e_recast_time = 0.f;
            }
            return;
        }

        const bool was_active = e_active;
        e_active = true;
        const float start_time = buff->get_start_time();
        if (start_time > 0.f)
            e_start_time = start_time;
        else if (!was_active || e_start_time <= 0.f)
            e_start_time = g_sdk->clock_facade->get_game_time();
    }

    bool is_spell_ready(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    bool has_mana(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* s = p->get_spell(slot);
        if (!s || s->get_level() <= 0) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    float mana_percent()
    {
        auto* p = player();
        if (!p) return 0.f;
        const float mp = p->get_max_par();
        if (mp <= 0.f) return 0.f;
        return (p->get_par() / mp) * 100.f;
    }

    bool valid_enemy(game_object* t, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !t || !t->is_valid() || t->is_dead()) return false;
        if (!t->is_visible() || !t->is_targetable()) return false;
        if (t->get_team_id() == p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(t->get_position()) > range) return false;
        return true;
    }

    game_object* target(float range)
    {
        if (!sdk::target_selector) return nullptr;

        if (auto* forced = sdk::target_selector->get_forced_target(); valid_enemy(forced, range))
            return forced;

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (valid_enemy(h, range))
                return h;
        }
        return nullptr;
    }

    bool is_q_range_ok(game_object* t, float min_q_range)
    {
        auto* p = player();
        if (!p || !t) return false;
        const float d = p->get_position().distance(t->get_position());
        if (d < min_q_range) return false;
        return d <= data::Q_RANGE + t->get_bounding_radius();
    }

    bool is_in_aa_range(game_object* t)
    {
        auto* p = player();
        if (!p || !t) return false;
        if (sdk::orbwalker)
            return sdk::orbwalker->is_in_auto_attack_range(p, t);

        const float range = p->get_attack_range()
                          + p->get_bounding_radius()
                          + t->get_bounding_radius();
        return p->get_position().distance(t->get_position()) <= range;
    }

    bool is_target_escaping(game_object* t)
    {
        auto* p = player();
        if (!p || !t) return false;
        if (t->is_dashing()) return true;

        const auto pos = t->get_position();
        const auto vel = t->get_velocity();
        const float speed_sq = vel.x * vel.x + vel.z * vel.z;
        if (speed_sq < 150.f * 150.f) return false;

        const float away_x = pos.x - p->get_position().x;
        const float away_z = pos.z - p->get_position().z;
        const float away_len = std::sqrt(away_x * away_x + away_z * away_z);
        if (away_len <= 1.f) return false;

        const float away_speed = (vel.x * away_x + vel.z * away_z) / away_len;
        return away_speed > 120.f;
    }

    float q_damage(game_object* t, bool include_w)
    {
        auto* p = player();
        if (!p || !t || !sdk::damage) return 0.f;

        float damage = sdk::damage->get_spell_damage(p, t, SLOT_Q);
        if (include_w && is_spell_ready(SLOT_W) && has_mana(SLOT_W))
            damage += sdk::damage->get_spell_damage(p, t, SLOT_W);

        return damage;
    }

    bool is_q_killable(game_object* t, bool include_w)
    {
        if (!t || !sdk::damage) return false;

        float hp = t->get_hp()
                 + t->get_all_shield()
                 + t->get_physical_shield()
                 + t->get_magical_shield();
        if (sdk::health_prediction)
        {
            const float pred = sdk::health_prediction->get_predicted_health(t, 0.25f);
            if (pred > 0.f && pred <= t->get_max_hp())
                hp = pred + t->get_all_shield()
                          + t->get_physical_shield()
                          + t->get_magical_shield();
        }

        return hp > 0.f && q_damage(t, include_w) >= hp;
    }

    int enemies_in_e_radius()
    {
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return 0;

        int count = 0;
        const auto ppos = p->get_position();
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!valid_enemy(h)) continue;
            if (ppos.distance(h->get_position()) <= data::E_RADIUS + h->get_bounding_radius())
                ++count;
        }
        return count;
    }

    bool enemy_about_to_leave_e(game_object* t)
    {
        auto* p = player();
        if (!p || !t || !valid_enemy(t)) return false;

        const float hit_radius = data::E_RADIUS + t->get_bounding_radius();
        const float current_dist = p->get_position().distance(t->get_position());
        if (current_dist > hit_radius)
            return false;

        math::vector3 predicted = t->get_position();
        if (t->is_dashing())
        {
            auto path = t->get_path();
            if (!path.empty())
                predicted = path.back();
            else
            {
                const auto vel = t->get_velocity();
                predicted = {
                    predicted.x + vel.x * E_LEAVE_RANGE_CHECK_TIME,
                    predicted.y,
                    predicted.z + vel.z * E_LEAVE_RANGE_CHECK_TIME
                };
            }
        }
        else
        {
            const auto vel = t->get_velocity();
            predicted = {
                predicted.x + vel.x * E_LEAVE_RANGE_CHECK_TIME,
                predicted.y,
                predicted.z + vel.z * E_LEAVE_RANGE_CHECK_TIME
            };
        }

        const float predicted_dist = p->get_position().distance(predicted);
        return predicted_dist > hit_radius - E_LEAVE_RANGE_BUFFER;
    }

    bool any_enemy_about_to_leave_e()
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (enemy_about_to_leave_e(h))
                return true;
        }
        return false;
    }

    bool should_combo_q(game_object* t)
    {
        if (!t) return false;

        const bool outside_aa = !is_in_aa_range(t);
        const bool killable = menu::combo_q_killable
                           && is_q_killable(t, menu::combo_use_w && menu::combo_w_q_gapclose);
        const bool escaping = menu::combo_q_escaping && is_target_escaping(t);

        if (menu::combo_q_only_out_aa && outside_aa)
            return true;
        if (killable || escaping)
            return true;

        return !menu::combo_q_only_out_aa;
    }

    bool is_in_flee_direction(const math::vector3& candidate)
    {
        if (!g_sdk || !g_sdk->hud_manager) return true;
        auto* p = player();
        if (!p) return false;

        const auto cursor = g_sdk->hud_manager->get_cursor_position();
        const auto flee_dir = (cursor - p->get_position()).to_2d();
        const auto target_dir = (candidate - p->get_position()).to_2d();
        if (flee_dir == math::vector2{} || target_dir == math::vector2{}) return true;

        const float angle = flee_dir.angle_between(target_dir);
        if (std::isnan(angle)) return true;
        return angle <= 90.f;
    }

    bool is_q_jumpable_ally(game_object* obj)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead()) return false;
        if (!obj->is_visible() || !obj->is_targetable()) return false;
        if (obj->is_turret()) return false;
        if (obj->get_team_id() != p->get_team_id()) return false;
        return p->get_position().distance(obj->get_position()) <= data::Q_RANGE;
    }

    int find_ward_slot()
    {
        auto* p = player();
        if (!p) return -1;

        auto check = [&](int item_id, bool needs_charges) -> int
        {
            int found = -1;
            if (!p->has_item(item_id, &found)) return -1;
            const int spell_slot = found + 6;
            auto* entry = p->get_spell(spell_slot);
            if (!entry) return -1;
            if (needs_charges && entry->get_charges() <= 0) return -1;
            if (entry->get_cooldown() > 0.f) return -1;
            return spell_slot;
        };

        int s = check(data::WARD_TRINKET_ID, true);
        if (s >= 0) return s;
        s = check(data::CONTROL_WARD_ID, false);
        if (s >= 0) return s;
        return check(data::FARSIGHT_ID, true);
    }

    bool try_ward_jump()
    {
        if (!menu::flee_q_ward_jump || !g_sdk || !g_sdk->hud_manager || !sdk::common)
            return false;
        if (!is_spell_ready(SLOT_Q) || !has_mana(SLOT_Q))
            return false;

        if (ward_jump_pending && g_sdk->clock_facade->get_game_time() > ward_jump_time + WARD_JUMP_TIMEOUT)
            ward_jump_pending = false;

        if (ward_jump_pending) return true;

        for (auto* ward : g_sdk->object_manager->get_wards())
        {
            if (!is_q_jumpable_ally(ward)) continue;
            if (!is_in_flee_direction(ward->get_position())) continue;
            return sdk::common->cast_spell(SLOT_Q, ward);
        }

        for (auto* trap : g_sdk->object_manager->get_traps())
        {
            if (!trap || !trap->is_valid() || trap->is_dead()) continue;
            if (trap->get_team_id() != player()->get_team_id()) continue;
            if (player()->get_position().distance(trap->get_position()) > data::Q_RANGE) continue;
            if (trap->get_name().find("JammerDevice") == std::string::npos) continue;
            if (!is_in_flee_direction(trap->get_position())) continue;
            return sdk::common->cast_spell(SLOT_Q, trap);
        }

        const int ward_slot = find_ward_slot();
        if (ward_slot < 0) return false;

        auto* p = player();
        auto cursor = g_sdk->hud_manager->get_cursor_position();
        auto place_pos = cursor;
        const float dist = p->get_position().distance(cursor);
        if (dist > WARD_PLACE_RANGE)
            place_pos = p->get_position().extended(cursor, WARD_PLACE_RANGE);

        if (!sdk::common->cast_spell(ward_slot, place_pos)) return false;
        ward_jump_pending = true;
        ward_jump_time = g_sdk->clock_facade->get_game_time();
        return true;
    }

    void schedule_e_recast_for_q(game_object* t)
    {
        if (!menu::combo_e_midair_recast || !t || !e_active || !g_sdk || !g_sdk->clock_facade)
            return;
        if (!sdk::orbwalker || !sdk::orbwalker->combo())
            return;
        if (!is_spell_ready(SLOT_E))
            return;

        auto* p = player();
        if (!p) return;

        const float now = g_sdk->clock_facade->get_game_time();
        const float dist = p->get_position().distance(t->get_position());
        const float leap_time = std::clamp(dist / Q_LEAP_SPEED, 0.05f, 0.45f);
        const float min_recast_time = e_start_time + data::E_MIN_RECAST_DELAY;

        pending_e_recast = true;
        pending_e_recast_target_id = t->get_network_id();
        pending_e_recast_time = std::max(min_recast_time, now + leap_time - 0.05f);
    }

    bool try_e_recast(bool force)
    {
        if (!menu::combo_use_e || !e_active || !sdk::common || !g_sdk || !g_sdk->clock_facade)
            return false;
        if (!is_spell_ready(SLOT_E))
            return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now < e_start_time + data::E_MIN_RECAST_DELAY)
            return false;

        const bool expiring = now >= e_start_time + data::E_EVASION_DURATION - E_RECAST_EXPIRY_BUFFER;
        const bool enough_enemies = enemies_in_e_radius() >= menu::combo_e_recast_min_enemies;
        const bool leaving_range = any_enemy_about_to_leave_e();
        if (menu::combo_e_wait_max_charge && !expiring && !leaving_range)
            return false;
        if (!force && !expiring && !enough_enemies && !leaving_range)
            return false;

        if (!sdk::common->cast_spell(SLOT_E))
            return false;

        pending_e_recast = false;
        pending_e_recast_target_id = 0;
        pending_e_recast_time = 0.f;
        return true;
    }

    bool handle_pending_e_recast()
    {
        if (!pending_e_recast || !g_sdk || !g_sdk->clock_facade || !g_sdk->object_manager)
            return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (!e_active || now > e_start_time + data::E_EVASION_DURATION + 0.1f)
        {
            pending_e_recast = false;
            pending_e_recast_target_id = 0;
            pending_e_recast_time = 0.f;
            return false;
        }

        if (now < pending_e_recast_time)
            return false;

        bool force = false;
        auto* p = player();
        if (auto* t = g_sdk->object_manager->get_object_by_network_id(pending_e_recast_target_id))
            force = p && valid_enemy(t) && p->get_position().distance(t->get_position()) <= data::E_RADIUS + t->get_bounding_radius();

        return try_e_recast(force);
    }

    bool is_crit_threat(game_object* source)
    {
        if (!source || !source->is_hero()) return false;

        const float raw_crit = source->get_crit();
        const float crit = raw_crit <= 1.f ? raw_crit * 100.f : raw_crit;
        if (crit >= static_cast<float>(menu::auto_e_critical_min_chance))
            return true;

        return source->get_crit_damage_multiplier() > 2.05f;
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!menu::auto_e_critical_aa || !obj || !cast || !cast->is_basic_attack())
            return;
        if (!sdk::common || !g_sdk || !g_sdk->clock_facade)
            return;

        auto* p = player();
        if (!p || p->is_dead() || e_active)
            return;

        auto* source = cast->get_source();
        auto* target = cast->get_target();
        if (!source) source = obj;
        if (target != p)
            return;
        if (!valid_enemy(source))
            return;
        if (!is_crit_threat(source))
            return;
        if (!is_spell_ready(SLOT_E) || !has_mana(SLOT_E))
            return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - cast->get_process_time() > 0.25f)
            return;

        sdk::common->cast_spell(SLOT_E);
    }

    bool try_q_target(game_object* t, bool use_w_gapclose)
    {
        if (!t || !sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return false;

        if (menu::combo_use_w && use_w_gapclose && menu::combo_w_q_gapclose
            && is_spell_ready(SLOT_W) && has_mana(SLOT_W))
        {
            sdk::common->cast_spell(SLOT_W); // W before leap for empowered Q hit
        }

        if (!sdk::common->cast_spell(SLOT_Q, t)) return false;

        schedule_e_recast_for_q(t);
        return true;
    }

    void q_killsteal()
    {
        if (!menu::ks_q_active || !sdk::damage) return;
        auto* p = player();
        if (!p || p->is_dead()) return;
        if (!is_spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return;

        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!valid_enemy(h, data::Q_RANGE + h->get_bounding_radius())) continue;
            const float q_dmg = sdk::damage->get_spell_damage(p, h, SLOT_Q);
            if (q_dmg <= 0.f) continue;

            float hp = h->get_hp();
            if (sdk::health_prediction)
            {
                const float pred = sdk::health_prediction->get_predicted_health(h, 0.25f);
                if (pred > 0.f) hp = std::min(hp, pred);
            }

            if (q_dmg >= hp)
            {
                try_q_target(h, /*use_w_gapclose=*/false);
                return;
            }
        }
    }

    void combo()
    {
        auto* p = player();
        auto* t = target(data::Q_RANGE);
        if (!p || !t || !sdk::common) return;

        const float hp_pct = p->get_hp() / std::max(1.f, p->get_max_hp()) * 100.f;

        if (menu::combo_use_r && hp_pct <= static_cast<float>(menu::combo_r_hp)
            && is_spell_ready(SLOT_R) && has_mana(SLOT_R))
        {
            sdk::common->cast_spell(SLOT_R);
            return;
        }

        if (menu::combo_use_e && !e_active && is_spell_ready(SLOT_E) && has_mana(SLOT_E))
        {
            const bool in_e_range = p->get_position().distance(t->get_position()) <= data::E_RADIUS + t->get_bounding_radius();
            const bool preparing_q = menu::combo_use_q
                                  && is_q_range_ok(t, static_cast<float>(menu::combo_q_min_range))
                                  && should_combo_q(t);
            if (in_e_range || preparing_q)
            {
                sdk::common->cast_spell(SLOT_E);
                return;
            }
        }

        if (e_active && try_e_recast(false))
            return;

        if (menu::combo_use_q
            && is_q_range_ok(t, static_cast<float>(menu::combo_q_min_range))
            && should_combo_q(t))
        {
            try_q_target(t, /*use_w_gapclose=*/true);
        }
    }

    void harass()
    {
        if (mana_percent() < static_cast<float>(menu::harass_mana)) return;
        auto* t = target(data::Q_RANGE);
        auto* p = player();
        if (!p || !t || !sdk::common) return;

        if (menu::harass_use_q && is_q_range_ok(t, static_cast<float>(menu::harass_q_min_range)))
            try_q_target(t, /*use_w_gapclose=*/false);
    }

    void clear()
    {
        if (!sdk::common || mana_percent() < static_cast<float>(menu::clear_mana)) return;
        auto* p = player();
        if (!p) return;

        if (menu::clear_use_w && is_spell_ready(SLOT_W) && has_mana(SLOT_W))
            sdk::common->cast_spell(SLOT_W);
        if (menu::clear_use_e && is_spell_ready(SLOT_E) && has_mana(SLOT_E))
            sdk::common->cast_spell(SLOT_E);
    }

    void flee()
    {
        if (!menu::flee_use_q || !sdk::common || !is_spell_ready(SLOT_Q) || !has_mana(SLOT_Q))
            return;

        auto* p = player();
        if (!p) return;

        // 1) Ally champion jump toward cursor direction.
        if (menu::flee_q_ally_heroes)
        {
            game_object* best = nullptr;
            float best_cursor_dist = FLT_MAX;
            const auto cursor = g_sdk->hud_manager ? g_sdk->hud_manager->get_cursor_position() : p->get_position();
            for (auto* h : g_sdk->object_manager->get_heroes())
            {
                if (h == p) continue;
                if (!is_q_jumpable_ally(h)) continue;
                if (!is_in_flee_direction(h->get_position())) continue;
                const float cd = cursor.distance(h->get_position());
                if (cd < best_cursor_dist)
                {
                    best_cursor_dist = cd;
                    best = h;
                }
            }
            if (best)
            {
                sdk::common->cast_spell(SLOT_Q, best);
                return;
            }
        }

        // 2) Ally minion jump toward cursor direction.
        if (menu::flee_q_ally_minions)
        {
            game_object* best = nullptr;
            float best_cursor_dist = FLT_MAX;
            const auto cursor = g_sdk->hud_manager ? g_sdk->hud_manager->get_cursor_position() : p->get_position();
            for (auto* m : g_sdk->object_manager->get_minions())
            {
                if (!is_q_jumpable_ally(m)) continue;
                if (!is_in_flee_direction(m->get_position())) continue;
                const float cd = cursor.distance(m->get_position());
                if (cd < best_cursor_dist)
                {
                    best_cursor_dist = cd;
                    best = m;
                }
            }
            if (best)
            {
                sdk::common->cast_spell(SLOT_Q, best);
                return;
            }
        }

        // 3) Ward jump fallback (existing ally ward first, then place+Q).
        if (try_ward_jump()) return;

        game_object* best = nullptr;
        float best_dist = FLT_MAX;
        for (auto* obj : g_sdk->object_manager->get_minions())
        {
            if (!obj || !obj->is_valid() || obj->is_dead() || !obj->is_targetable()) continue;
            if (obj->get_team_id() != p->get_team_id()) continue;
            if (p->get_position().distance(obj->get_position()) > data::Q_RANGE) continue;
            const float d = p->get_position().distance(obj->get_position());
            if (d < best_dist)
            {
                best_dist = d;
                best = obj;
            }
        }
        if (best) sdk::common->cast_spell(SLOT_Q, best);
    }

    bool __stdcall on_after_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker || !sdk::common) return true;
        if (!is_spell_ready(SLOT_W) || !has_mana(SLOT_W)) return true;

        auto* t = data->target;
        bool want_w = false;

        if (t->is_hero())
        {
            if (sdk::orbwalker->combo() && menu::combo_use_w) want_w = true;
            else if (sdk::orbwalker->harass() && menu::harass_use_w) want_w = true;
        }
        else if (sdk::orbwalker->clear() && menu::clear_use_w && sdk::orbwalker->is_spell_farm_enabled())
        {
            want_w = t->is_lane_minion() || t->is_monster() || t->is_turret();
        }

        if (!want_w) return true;

        // This callback is wired through orbwalker's before_attack hook in this SDK,
        // but behaves as the post-attack reset point in existing modules.
        sdk::common->cast_spell(SLOT_W);
        return true;
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        auto* p = player();
        if (!p || !obj || obj != p || !buff) return;

        if (is_jax_buff(buff, data::BUFF_E_EVASION, data::BUFF_NAME_E))
        {
            e_active = true;
            const float start_time = buff->get_start_time();
            e_start_time = start_time > 0.f ? start_time : g_sdk->clock_facade->get_game_time();
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        auto* p = player();
        if (!p || !obj || obj != p || !buff) return;

        if (is_jax_buff(buff, data::BUFF_E_EVASION, data::BUFF_NAME_E))
        {
            e_active = false;
            e_start_time = 0.f;
            pending_e_recast = false;
            pending_e_recast_target_id = 0;
            pending_e_recast_time = 0.f;
        }
    }

    void __fastcall on_create_object(game_object* obj)
    {
        if (!ward_jump_pending || !obj) return;
        if (!sdk::orbwalker || !sdk::orbwalker->flee())
        {
            ward_jump_pending = false;
            return;
        }
        if (!obj->is_ward() && !obj->is_trap()) return;
        if (!player() || obj->get_team_id() != player()->get_team_id()) return;
        if (obj->is_trap() && obj->get_name().find("JammerDevice") == std::string::npos) return;
        if (player()->get_position().distance(obj->get_position()) > data::Q_RANGE) return;

        sdk::common->cast_spell(SLOT_Q, obj);
        ward_jump_pending = false;
    }
}

void __fastcall on_update()
{
    if (!g_sdk || !sdk::orbwalker || !g_sdk->renderer) return;
    if (!player() || player()->is_dead()) return;
    sync_e_state_from_buff();
    q_killsteal();
    if (handle_pending_e_recast()) return;

    if (sdk::orbwalker->combo())
        combo();
    else if (sdk::orbwalker->harass())
        harass();
    else if (sdk::orbwalker->clear())
        clear();
    else if (sdk::orbwalker->flee())
        flee();
}

void __fastcall on_draw()
{
    auto* p = player();
    if (!p || !g_sdk || !g_sdk->renderer) return;
    sync_e_state_from_buff();

    auto draw_circle = [&](math::vector3 const& center, float range, uint32_t color)
    {
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(center, range, 2.f, color);
    };

    if (menu::draw_q_range && (!menu::draw_only_ready || is_spell_ready(SLOT_Q)))
        draw_circle(p->get_position(), data::Q_RANGE, menu::color_q);
    if (menu::draw_e_range && (!menu::draw_only_ready || is_spell_ready(SLOT_E)))
        draw_circle(p->get_position(), data::E_RADIUS, menu::color_e);

    if (menu::draw_e_timer && e_active && g_sdk->clock_facade)
    {
        const float now = g_sdk->clock_facade->get_game_time();
        const float remaining = std::max(0.f, e_start_time + data::E_EVASION_DURATION - now);
        const float elapsed = std::clamp(now - e_start_time, 0.f, data::E_EVASION_DURATION);
        const float pct = elapsed / data::E_EVASION_DURATION;

        const auto screen = g_sdk->renderer->world_to_screen(p->get_position());
        if (screen.x > 0.f && screen.y > 0.f)
        {
            char text[32]{};
            std::snprintf(text, sizeof(text), "E %.1fs", remaining);
            const uint32_t clr = remaining <= E_RECAST_EXPIRY_BUFFER + 0.1f ? 0xFFFF4040u : 0xFFB8F7FFu;
            g_sdk->renderer->add_text(text, 15.f, { screen.x, screen.y - 72.f }, 1, clr);

            if (sdk::shaders)
                sdk::shaders->draw_percentage_circle({ screen.x, screen.y - 54.f }, 20.f, 3.f, pct, clr);
        }
    }
}

void load()
{
    s_menu = g_sdk->menu_manager->add_category("jax", "MeowAIO - Jax");
    if (s_menu)
    {
        auto* q_menu = s_menu->add_sub_category("q_leap_strike", "Q - Leap Strike");
        q_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_q,
            [](bool v) { menu::combo_use_q = v; });
        q_menu->add_slider_int("combo_min_range", "Combo Min Q Range", 0, 700, 10, menu::combo_q_min_range,
            [](int v) { menu::combo_q_min_range = v; });
        q_menu->add_checkbox("combo_out_aa", "Combo: Q if Outside AA", menu::combo_q_only_out_aa,
            [](bool v) { menu::combo_q_only_out_aa = v; });
        q_menu->add_checkbox("combo_killable", "Combo: Q if Killable", menu::combo_q_killable,
            [](bool v) { menu::combo_q_killable = v; });
        q_menu->add_checkbox("combo_escaping", "Combo: Q if Escaping", menu::combo_q_escaping,
            [](bool v) { menu::combo_q_escaping = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q,
            [](bool v) { menu::harass_use_q = v; });
        q_menu->add_slider_int("harass_min_range", "Harass Min Q Range", 0, 700, 10, menu::harass_q_min_range,
            [](int v) { menu::harass_q_min_range = v; });
        q_menu->add_hotkey("ks_q", "Instant Q Killsteal (Hold)", 'X', false, false,
            [](std::string* hk, bool pressed) {
                menu::ks_q_active = pressed;
                if (hk) menu::ks_q_key_str = *hk;
            });
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        auto* w_menu = s_menu->add_sub_category("w_empower", "W - Empower");
        w_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_w,
            [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("q_gapclose", "Use W before Q gapclose", menu::combo_w_q_gapclose,
            [](bool v) { menu::combo_w_q_gapclose = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w,
            [](bool v) { menu::harass_use_w = v; });
        w_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_w,
            [](bool v) { menu::clear_use_w = v; });

        auto* e_menu = s_menu->add_sub_category("e_counter_strike", "E - Counter Strike");
        e_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_e,
            [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("midair_recast", "Mid-air E recast with Q", menu::combo_e_midair_recast,
            [](bool v) { menu::combo_e_midair_recast = v; });
        e_menu->add_checkbox("wait_max_charge", "Wait Max Charge", menu::combo_e_wait_max_charge,
            [](bool v) { menu::combo_e_wait_max_charge = v; });
        e_menu->add_slider_int("recast_min_enemies", "Recast if Enemies Hit", 1, 5, 1, menu::combo_e_recast_min_enemies,
            [](int v) { menu::combo_e_recast_min_enemies = v; });
        e_menu->add_checkbox("auto_crit_aa", "Auto E Critical AA", menu::auto_e_critical_aa,
            [](bool v) { menu::auto_e_critical_aa = v; });
        e_menu->add_slider_int("auto_crit_chance", "Crit Chance Threshold", 1, 100, 1, menu::auto_e_critical_min_chance,
            [](int v) { menu::auto_e_critical_min_chance = v; });
        e_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_e,
            [](bool v) { menu::clear_use_e = v; });
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        auto* r_menu = s_menu->add_sub_category("r_grandmaster", "R - Grandmaster-at-Arms");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_slider_int("hp", "Use if HP % below", 1, 100, 1, menu::combo_r_hp,
            [](int v) { menu::combo_r_hp = v; });

        auto* flee_menu = s_menu->add_sub_category("flee", "Flee");
        flee_menu->add_checkbox("use", "Use in Flee", menu::flee_use_q,
            [](bool v) { menu::flee_use_q = v; });
        flee_menu->add_checkbox("ally_heroes", "Q to Ally Heroes", menu::flee_q_ally_heroes,
            [](bool v) { menu::flee_q_ally_heroes = v; });
        flee_menu->add_checkbox("ally_minions", "Q to Ally Minions", menu::flee_q_ally_minions,
            [](bool v) { menu::flee_q_ally_minions = v; });
        flee_menu->add_checkbox("ward_jump", "Ward Jump (Place + Q)", menu::flee_q_ward_jump,
            [](bool v) { menu::flee_q_ward_jump = v; });

        auto* mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 1, menu::harass_mana,
            [](int v) { menu::harass_mana = v; });
        mana_menu->add_slider_int("clear", "Clear Mana %", 0, 100, 1, menu::clear_mana,
            [](int v) { menu::clear_mana = v; });

        auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("e_timer", "Draw E Timer", menu::draw_e_timer,
            [](bool v) { menu::draw_e_timer = v; });
        draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
    }

    g_sdk->event_manager->register_callback(event_manager::event::game_update,
        reinterpret_cast<void*>(on_update));
    g_sdk->event_manager->register_callback(event_manager::event::draw_world,
        reinterpret_cast<void*>(on_draw));
    g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
        reinterpret_cast<void*>(on_buff_gain));
    g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
        reinterpret_cast<void*>(on_buff_loss));
    g_sdk->event_manager->register_callback(event_manager::event::process_cast,
        reinterpret_cast<void*>(on_process_cast));
    g_sdk->event_manager->register_callback(event_manager::event::create_object,
        reinterpret_cast<void*>(on_create_object));
    if (sdk::orbwalker)
    {
        sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
            reinterpret_cast<void*>(on_after_attack));
    }
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
    }
    if (sdk::orbwalker)
    {
        sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
            reinterpret_cast<void*>(on_after_attack));
    }

    s_menu = nullptr;
    e_active = false;
    e_start_time = 0.f;
    pending_e_recast = false;
    pending_e_recast_target_id = 0;
    pending_e_recast_time = 0.f;
    ward_jump_pending = false;
    ward_jump_time = 0.f;
}
}
