#include "ziggs.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "infotab_sdk.hpp"
#include "dmg_sdk.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace ziggs
{
namespace
{
    // =========================================================
    //  Internal state
    // =========================================================
    menu_category* s_menu = nullptr;

    float s_last_cast[4] = {};
    bool  s_threw_w            = false;
    bool  s_e_recently_cast    = false;
    float s_e_cast_time        = 0.f;

    // Korean combo state machine: 0=idle, 1=R fired (wait to place W), 2=W placed (wait to detonate)
    int           s_korean_state      = 0;
    float         s_korean_start_time = 0.f;
    math::vector3 s_korean_w_pos;

    // Gapcloser W detonate (brief delay after placing W on gapcloser)
    bool  s_gapcloser_w_pending = false;
    float s_gapcloser_w_time    = 0.f;

    // Flee W detonation
    bool  s_flee_w_pending = false;
    float s_flee_w_time    = 0.f;

    // Turret damage thresholds per W level: 25 / 27.5 / 30 / 32.5 / 35 %
    constexpr float W_TURRET_PCT[5] = { 25.f, 27.5f, 30.f, 32.5f, 35.f };

    // =========================================================
    //  Basic helpers
    // =========================================================
    float now()
    {
        return (g_sdk && g_sdk->clock_facade) ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    game_object* player()
    {
        return (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
    }

    float hp_pct(game_object* obj)
    {
        if (!obj || obj->get_max_hp() <= 0.f) return 0.f;
        return (obj->get_hp() / obj->get_max_hp()) * 100.f;
    }

    float mana_pct()
    {
        auto* p = player();
        if (!p || p->get_max_par() <= 0.f) return 0.f;
        return (p->get_par() / p->get_max_par()) * 100.f;
    }

    bool spell_ready(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    int spell_level(int slot)
    {
        auto* p = player();
        if (!p) return 0;
        auto* s = p->get_spell(slot);
        return s ? s->get_level() : 0;
    }

    bool has_mana(int slot)
    {
        auto* p = player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    bool can_cast(int slot, float cd = 0.18f)
    {
        return now() - s_last_cast[slot] >= cd;
    }

    void mark_cast(int slot)
    {
        s_last_cast[slot] = now();
    }

    bool is_valid_enemy(game_object* obj, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead()) return false;
        if (!obj->is_targetable() || !obj->is_visible()) return false;
        if (obj->get_team_id() == p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(obj->get_position()) > range)
            return false;
        return true;
    }

    game_object* best_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_enemy(forced, range))
            return forced;
        for (auto* hero : sdk::target_selector->get_sorted_heroes())
            if (is_valid_enemy(hero, range)) return hero;
        return nullptr;
    }

    bool has_buff_by_name(game_object* obj, const char* name)
    {
        if (!obj || !name) return false;
        for (auto* b : obj->get_buffs())
        {
            if (!b || !b->is_active()) continue;
            const std::string n = b->get_name();
            if (!n.empty() && _stricmp(n.c_str(), name) == 0) return true;
        }
        return false;
    }

    bool is_invulnerable(game_object* obj)
    {
        if (!obj) return false;
        return obj->has_buff_of_type(buff_type::invulnerability)
            || obj->has_buff_of_type(buff_type::spell_immunity);
    }

    bool has_ziggs_w_buff()
    {
        return has_buff_by_name(player(), "ZiggsW");
    }

    bool obj_is_facing(game_object* obj, const math::vector3& target_pos)
    {
        if (!obj) return false;
        auto dir  = obj->get_direction();
        auto to   = (target_pos - obj->get_position()).normalized();
        return dir.dot(to) > 0.f;
    }

    bool has_hard_cc(game_object* obj)
    {
        if (!obj) return false;
        return obj->has_buff_of_type(buff_type::stun)
            || obj->has_buff_of_type(buff_type::snare)
            || obj->has_buff_of_type(buff_type::knockup)
            || obj->has_buff_of_type(buff_type::asleep)
            || obj->has_buff_of_type(buff_type::suppression)
            || obj->has_buff_of_type(buff_type::taunt)
            || obj->has_buff_of_type(buff_type::grounded);
    }

    float get_cc_remaining(game_object* obj)
    {
        if (!obj) return 0.f;
        const float t_now = now();
        float max_time = 0.f;
        for (auto* b : obj->get_buffs())
        {
            if (!b || !b->is_active()) continue;
            auto t = b->get_type();
            if (t == buff_type::stun || t == buff_type::snare || t == buff_type::knockup
                || t == buff_type::asleep || t == buff_type::suppression
                || t == buff_type::taunt  || t == buff_type::grounded)
            {
                const float remaining = b->get_end_time() - t_now;
                if (remaining > max_time) max_time = remaining;
            }
        }
        return max_time;
    }

    int count_enemies_near(const math::vector3& pos, float radius)
    {
        int n = 0;
        if (!g_sdk || !g_sdk->object_manager) return 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
            if (is_valid_enemy(hero) && hero->get_position().distance(pos) <= radius) ++n;
        return n;
    }

    int count_allies_near(const math::vector3& pos, float radius, int team)
    {
        int n = 0;
        if (!g_sdk || !g_sdk->object_manager) return 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!hero || !hero->is_valid() || hero->is_dead()) continue;
            if (hero->get_team_id() != team) continue;
            if (hero->get_position().distance(pos) <= radius) ++n;
        }
        return n;
    }

    // Base damage tables + AP ratios (Ziggs).  VEN's generic get_spell_damage
    // does not model Ziggs' spells correctly, so we compute the raw magic
    // damage ourselves and run it through calc_damage for MR mitigation.
    const float Q_BASE[5] = { 85.f, 135.f, 185.f, 235.f, 285.f };
    const float W_BASE[5] = { 70.f, 105.f, 140.f, 175.f, 210.f };
    const float R_BASE[3] = { 200.f, 300.f, 400.f };
    constexpr float Q_AP = 0.65f;
    constexpr float W_AP = 0.50f;
    constexpr float R_AP = 1.10f;

    float get_spell_dmg(int slot, game_object* target)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;

        const int lvl = spell_level(slot);
        if (lvl <= 0) return 0.f;

        const float ap = p->get_ability_power();
        float raw = 0.f;

        switch (slot)
        {
        case SLOT_Q: raw = Q_BASE[std::clamp(lvl - 1, 0, 4)] + Q_AP * ap; break;
        case SLOT_W: raw = W_BASE[std::clamp(lvl - 1, 0, 4)] + W_AP * ap; break;
        case SLOT_R: raw = R_BASE[std::clamp(lvl - 1, 0, 2)] + R_AP * ap; break;
        default:     return 0.f;
        }

        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
    }

    bool killable_q(game_object* t)      { return t && get_spell_dmg(SLOT_Q, t) > t->get_hp() + 10.f; }
    bool killable_w(game_object* t)      { return t && get_spell_dmg(SLOT_W, t) > t->get_hp() -  5.f; }
    bool killable_r(game_object* t)      { return t && get_spell_dmg(SLOT_R, t) > t->get_hp() + 10.f; }
    bool killable_r_auto(game_object* t, float extra) { return t && get_spell_dmg(SLOT_R, t) > t->get_hp() + extra; }
    bool killable_wr(game_object* t)     { return t && (get_spell_dmg(SLOT_W, t) + get_spell_dmg(SLOT_R, t)) > t->get_hp() + 10.f; }

    // =========================================================
    //  Prediction helpers
    // =========================================================
    pred_sdk::pred_data predict_q(game_object* t)
    {
        if (!sdk::prediction) return {};
        pred_sdk::spell_data sd;
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.range              = spell_data::q_cast_range;
        sd.radius             = spell_data::q_radius;
        sd.delay              = spell_data::q_delay;
        sd.projectile_speed   = spell_data::q_speed;
        sd.spell_slot         = SLOT_Q;
        sd.expected_hitchance = menu::q_hitchance;
        sd.forbidden_collisions = { pred_sdk::collision_type::yasuo_wall };
        return sdk::prediction->predict(t, sd);
    }

    pred_sdk::pred_data predict_w(game_object* t)
    {
        if (!sdk::prediction) return {};
        pred_sdk::spell_data sd;
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.range              = spell_data::w_range;
        sd.radius             = spell_data::w_radius;
        sd.delay              = spell_data::w_delay;
        sd.projectile_speed   = spell_data::w_speed;
        sd.spell_slot         = SLOT_W;
        sd.expected_hitchance = menu::w_hitchance;
        sd.forbidden_collisions = { pred_sdk::collision_type::yasuo_wall };
        return sdk::prediction->predict(t, sd);
    }

    pred_sdk::pred_data predict_e(game_object* t)
    {
        if (!sdk::prediction) return {};
        pred_sdk::spell_data sd;
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.range              = spell_data::e_range;
        sd.radius             = spell_data::e_radius;
        sd.delay              = spell_data::e_delay;
        sd.projectile_speed   = spell_data::e_speed;
        sd.spell_slot         = SLOT_E;
        sd.expected_hitchance = menu::e_hitchance;
        sd.forbidden_collisions = { pred_sdk::collision_type::yasuo_wall };
        return sdk::prediction->predict(t, sd);
    }

    pred_sdk::pred_data predict_r(game_object* t)
    {
        if (!sdk::prediction) return {};
        pred_sdk::spell_data sd;
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.range              = spell_data::r_range;
        sd.radius             = spell_data::r_radius;
        sd.delay              = spell_data::r_delay;
        sd.projectile_speed   = spell_data::r_speed;
        sd.spell_slot         = SLOT_R;
        sd.expected_hitchance = pred_sdk::hitchance::high;
        return sdk::prediction->predict(t, sd);
    }

    // =========================================================
    //  Cast helpers
    // =========================================================
    bool cast_q(game_object* target)
    {
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q) || !sdk::common || !can_cast(SLOT_Q)) return false;
        auto pred = predict_q(target);
        if (!pred.is_valid) return false;
        if (sdk::common->cast_spell(SLOT_Q, pred.cast_position)) { mark_cast(SLOT_Q); return true; }
        return false;
    }

    bool cast_w_at(const math::vector3& pos)
    {
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W) || !sdk::common || !can_cast(SLOT_W)) return false;
        if (sdk::common->cast_spell(SLOT_W, pos)) { mark_cast(SLOT_W); return true; }
        return false;
    }

    bool cast_w_target(game_object* target)
    {
        auto pred = predict_w(target);
        if (!pred.is_valid) return false;
        return cast_w_at(pred.cast_position);
    }

    bool cast_w_detonate()
    {
        if (!has_ziggs_w_buff() || !sdk::common) return false;
        return sdk::common->cast_spell(SLOT_W);
    }

    bool cast_e_at(const math::vector3& pos)
    {
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E) || !sdk::common || !can_cast(SLOT_E)) return false;
        if (sdk::common->cast_spell(SLOT_E, pos)) { mark_cast(SLOT_E); return true; }
        return false;
    }

    bool cast_e(game_object* target)
    {
        auto pred = predict_e(target);
        if (!pred.is_valid) return false;
        return cast_e_at(pred.cast_position);
    }

    bool cast_r_at(const math::vector3& pos)
    {
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R) || !sdk::common || !can_cast(SLOT_R, 0.25f)) return false;
        if (sdk::common->cast_spell(SLOT_R, pos)) { mark_cast(SLOT_R); return true; }
        return false;
    }

    // =========================================================
    //  Korean hidden combo (W+R burst on CC'd target)
    // =========================================================
    void korean_hidden_combo()
    {
        auto* p = player();
        if (!p || !menu::combo_korean)
        {
            s_korean_state = 0;
            return;
        }

        if (s_korean_state == 1)
        {
            float elapsed = now() - s_korean_start_time;
            if (elapsed >= 0.36f && elapsed < 1.5f)
            {
                if (cast_w_at(s_korean_w_pos))
                    s_korean_state = 2;
            }
            else if (elapsed >= 1.5f)
            {
                s_korean_state = 0;
            }
            return;
        }

        if (s_korean_state == 2)
        {
            float elapsed = now() - s_korean_start_time;
            if (elapsed >= 0.62f)
                cast_w_detonate();
            if (elapsed >= 1.0f)
            {
                s_korean_state = 0;
            }
            return;
        }

        // State 0: scan for a CC'd killable target
        if (!spell_ready(SLOT_W) || !spell_ready(SLOT_R)) return;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero, 1350.f)) continue;
            if (!has_hard_cc(hero))                continue;
            if (killable_q(hero))                  continue;
            if (hp_pct(hero) < 10.f)               continue;
            if (get_cc_remaining(hero) < 0.2f)     continue;
            if (!killable_wr(hero))                continue;

            float dist = p->get_position().distance(hero->get_position());
            if (dist > spell_data::w_range - 150.f) continue;

            math::vector3 r_pos = p->get_position().extended(hero->get_position(), dist - 425.f, true);
            math::vector3 w_pos = p->get_position().extended(hero->get_position(), dist + spell_data::w_push_distance, true);

            if (cast_r_at(r_pos))
            {
                s_korean_state      = 1;
                s_korean_start_time = now();
                s_korean_w_pos      = w_pos;
                return;
            }
        }
    }

    // =========================================================
    //  Auto R if killable
    // =========================================================
    void auto_r_if_killable()
    {
        if (!menu::auto_r_if_killable || !spell_ready(SLOT_R)) return;
        auto* p = player();
        if (!p) return;

        auto* t = best_target(spell_data::r_range);
        if (!t) return;

        if (killable_q(t) && spell_ready(SLOT_Q)
            && p->get_position().distance(t->get_position()) <= spell_data::q_cast_range - 150.f)
            return;

        if (hp_pct(t) < 13.f && count_enemies_near(p->get_position(), 550.f) >= 2)
            return;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero) || hero == t) continue;
            if (p->get_position().distance(hero->get_position()) <= spell_data::e_range)
                return;
        }

        auto pred = predict_r(t);
        if (!pred.is_valid) return;

        int allies = std::max(0, count_allies_near(t->get_position(), 700.f, t->get_team_id()) - 1);
        float margin = (allies == 0) ? 20.f : (allies == 1) ? 300.f : (allies == 2) ? 600.f : 800.f;

        if (killable_r_auto(t, margin))
            cast_r_at(pred.cast_position);
    }

    // =========================================================
    //  Auto R on AOE — cast R when its blast would hit enough enemies
    // =========================================================
    void auto_r_aoe()
    {
        if (!menu::auto_r_aoe || !spell_ready(SLOT_R)) return;
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        const int needed = menu::auto_r_aoe_min_enemies;

        // Use each in-range enemy's predicted landing spot as a blast center and
        // count how many enemies fall inside R's radius around it.
        game_object* best_center = nullptr;
        math::vector3 best_pos;
        int best_count = 0;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero, spell_data::r_range)) continue;

            auto pred = predict_r(hero);
            if (!pred.is_valid) continue;

            int count = 0;
            for (auto* other : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(other)) continue;
                if (other->get_position().distance(pred.cast_position) <= spell_data::r_radius)
                    ++count;
            }

            if (count > best_count)
            {
                best_count  = count;
                best_center = hero;
                best_pos    = pred.cast_position;
            }
        }

        if (best_center && best_count >= needed)
            cast_r_at(best_pos);
    }

    // =========================================================
    //  Auto W on destroyable turret
    // =========================================================
    void check_destroyable_turret()
    {
        if (!menu::auto_w_on_destroyable_turret || !spell_ready(SLOT_W) || s_korean_state != 0) return;
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        for (auto* turret : g_sdk->object_manager->get_turrets())
        {
            if (!turret || !turret->is_valid() || turret->is_dead()) continue;
            if (turret->get_team_id() == p->get_team_id()) continue;
            if (p->get_position().distance(turret->get_position()) > spell_data::w_range) continue;

            int wlvl = spell_level(SLOT_W);
            if (wlvl <= 0) continue;
            int idx = std::clamp(wlvl - 1, 0, 4);
            if (hp_pct(turret) < W_TURRET_PCT[idx])
                cast_w_at(turret->get_position());
        }
    }

    // =========================================================
    //  Auto W on channeling spells
    // =========================================================
    void auto_w_on_channeling()
    {
        if (!menu::auto_w_on_channeling || !spell_ready(SLOT_W) || s_korean_state != 0) return;
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero, spell_data::w_range)) continue;
            auto* cast_info = hero->get_active_spell_cast();
            if (!cast_info) continue;
            float channel_end = cast_info->get_cast_channeling_end_time();
            if (channel_end > 0.f && channel_end > now())
                cast_w_at(hero->get_position());
        }
    }

    // =========================================================
    //  W placement position for push-away logic
    // =========================================================
    math::vector3 compute_w_push_pos(game_object* target)
    {
        auto* p = player();
        float dist     = p->get_position().distance(target->get_position());
        float traveled = target->get_move_speed() * 0.25f;

        bool we_face_them  = obj_is_facing(p,      target->get_position());
        bool they_face_us  = obj_is_facing(target, p->get_position());

        if (!they_face_us && we_face_them)
            return p->get_position().extended(target->get_position(), dist + spell_data::w_push_distance + traveled, true);
        if (they_face_us && !we_face_them)
            return p->get_position().extended(target->get_position(), dist - spell_data::w_push_distance - traveled, true);
        return target->get_position();
    }

    // Try to cast W intelligently in combo.  e_cast_flag = true when E was recently cast (basic combo mode).
    void try_w_combo(game_object* target, bool e_cast_flag)
    {
        if (!menu::combo_w || has_ziggs_w_buff() || !spell_ready(SLOT_W)) return;
        auto* p = player();
        if (!p) return;

        float dist = p->get_position().distance(target->get_position());
        if (dist > spell_data::w_range) return;

        if (killable_w(target))
        {
            auto pred = predict_w(target);
            if (pred.is_valid) { cast_w_at(pred.cast_position); return; }
        }

        math::vector3 w_pos;
        float hp_diff = hp_pct(target) - hp_pct(p);

        if (hp_diff > static_cast<float>(menu::combo_w_hp_diff_push))
            w_pos = compute_w_push_pos(target);
        else if (hp_pct(p) > hp_pct(target) + 20.f)
        {
            float traveled = target->get_move_speed() * 0.25f;
            w_pos = p->get_position().extended(target->get_position(), dist + spell_data::w_push_distance + traveled, true);
        }
        else
            w_pos = target->get_position();

        if (menu::combo_w_only_when_e_ready)
        {
            if (spell_ready(SLOT_E) || e_cast_flag)
                cast_w_at(w_pos);
        }
        else
            cast_w_at(w_pos);
    }

    // =========================================================
    //  Combo modes
    // =========================================================
    void basic_combo()
    {
        auto* target = best_target(std::max<float>({ spell_data::q_cast_range, spell_data::w_range, spell_data::e_range }));
        if (!target || is_invulnerable(target)) return;

        auto* p   = player();
        float dist = p->get_position().distance(target->get_position());

        if (menu::combo_e && dist <= spell_data::e_range) cast_e(target);
        if (menu::combo_q)                                cast_q(target);
        try_w_combo(target, s_e_recently_cast);
        if (has_ziggs_w_buff() && menu::combo_w)          cast_w_detonate();
    }

    void e_into_w_combo()
    {
        auto* target = best_target(std::max<float>({ spell_data::q_cast_range, spell_data::w_range, spell_data::e_range }));
        if (!target || is_invulnerable(target)) return;

        auto* p   = player();
        float dist = p->get_position().distance(target->get_position());

        if (menu::combo_q && dist <= spell_data::q_cast_range) cast_q(target);
        try_w_combo(target, false);
        if (has_ziggs_w_buff() && menu::combo_w)          cast_w_detonate();

        if (dist <= spell_data::e_range && menu::combo_e)
        {
            if (has_buff_by_name(target, "moveawaycollision"))
            {
                auto pred = predict_e(target);
                if (pred.is_valid) cast_e_at(pred.cast_position);
            }
            else if (!s_threw_w)
            {
                if (!menu::combo_w || !spell_ready(SLOT_W))
                    cast_e(target);
            }
        }
    }

    // =========================================================
    //  Harass
    // =========================================================
    void harass()
    {
        if (mana_pct() < static_cast<float>(menu::harass_mana_pct)) return;
        auto* target = best_target(std::max<float>({ spell_data::q_cast_range, spell_data::e_range }));
        if (!target) return;

        auto* p   = player();
        float dist = p->get_position().distance(target->get_position());

        if (menu::harass_q)                              cast_q(target);
        if (menu::harass_w && dist <= spell_data::w_range) cast_w_target(target);
        if (menu::harass_e && dist <= spell_data::e_range) cast_e(target);
    }

    // =========================================================
    //  Lane clear
    // =========================================================
    void lane_clear()
    {
        if (mana_pct() < static_cast<float>(menu::lane_mana_pct)) return;
        if (!g_sdk || !g_sdk->object_manager) return;
        auto* p = player();
        if (!p) return;

        std::vector<game_object*> minions;
        for (auto* m : g_sdk->object_manager->get_minions())
        {
            if (!m || !m->is_valid() || m->is_dead()) continue;
            if (m->get_team_id() == p->get_team_id()) continue;
            if (!m->is_lane_minion()) continue;
            if (p->get_position().distance(m->get_position()) > spell_data::q_cast_range) continue;
            minions.push_back(m);
        }

        std::sort(minions.begin(), minions.end(), [&](game_object* a, game_object* b) {
            return p->get_position().distance(a->get_position()) < p->get_position().distance(b->get_position());
        });

        if (minions.empty()) return;

        if (menu::lane_q && spell_ready(SLOT_Q))
            if (cast_q(minions.front())) return;

        if (menu::lane_e && spell_ready(SLOT_E))
        {
            int count = 0;
            for (auto* m : minions)
                if (minions.front()->get_position().distance(m->get_position()) <= spell_data::e_mine_radius)
                    ++count;
            if (count >= menu::lane_e_min_minions)
                cast_e_at(minions.front()->get_position());
        }
    }

    // =========================================================
    //  Jungle clear
    // =========================================================
    void jungle_clear()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto* p = player();
        if (!p) return;

        std::vector<game_object*> monsters;
        for (auto* m : g_sdk->object_manager->get_monsters())
        {
            if (!m || !m->is_valid() || m->is_dead()) continue;
            if (p->get_position().distance(m->get_position()) > spell_data::q_cast_range) continue;
            monsters.push_back(m);
        }

        std::sort(monsters.begin(), monsters.end(), [](game_object* a, game_object* b) {
            return a->get_max_hp() > b->get_max_hp();
        });

        if (monsters.empty()) return;

        if (menu::jungle_q && spell_ready(SLOT_Q))
            if (cast_q(monsters.front())) return;

        if (menu::jungle_e && spell_ready(SLOT_E))
            cast_e_at(monsters.front()->get_position());
    }

    // =========================================================
    //  Flee — throw W behind self, detonate shortly after
    // =========================================================
    void flee()
    {
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->hud_manager) return;
        if (!menu::flee_w || !spell_ready(SLOT_W) || has_ziggs_w_buff() || s_flee_w_pending) return;

        auto cursor = g_sdk->hud_manager->get_cursor_position();
        auto my_pos = p->get_position();
        float dist  = my_pos.distance(cursor);
        // Place W 100 units past the player in the direction away from cursor
        math::vector3 flee_pos = cursor.extended(my_pos, dist + 100.f, true);

        if (cast_w_at(flee_pos))
        {
            s_flee_w_pending = true;
            s_flee_w_time    = now();
        }
    }

    // =========================================================
    //  Semi-manual R
    // =========================================================
    void semi_r()
    {
        if (!menu::semi_r_active || !spell_ready(SLOT_R)) return;
        auto* t = best_target(spell_data::r_range);
        if (!t) return;
        auto pred = predict_r(t);
        if (pred.is_valid) cast_r_at(pred.cast_position);
    }

    // =========================================================
    //  Drawing helper
    // =========================================================
    void draw_circle(const math::vector3& pos, float radius, uint32_t color)
    {
        if (!color) return;
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, radius, 2.f, color);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, radius, 1.f, color);
    }

    // Overlay the chunk of HP that R would remove onto the target's health bar.
    void draw_r_damage_overlay(game_object* target, float damage)
    {
        if (!target || damage <= 0.f || !g_sdk || !g_sdk->renderer) return;

        const float hp     = target->get_hp();
        const float max_hp = target->get_max_hp();
        if (hp <= 0.f || max_hp <= 0.f) return;

        const auto hbar = target->get_health_bar_position();
        if (hbar.x < 1.f && hbar.y < 1.f) return;

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
        if (r0 <= r1) return;

        const float bl = hbar.x + ox;
        const float bt = hbar.y + oy;

        math::rect rc;
        rc.left   = bl + r1 * bw;
        rc.top    = bt;
        rc.right  = bl + r0 * bw;
        rc.bottom = bt + bh;

        g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_damage_bar);
    }

    // =========================================================
    //  Menu setup
    // =========================================================
    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_ziggs", "MeowAIO - Ziggs");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Ziggs: failed to create menu category");
            return;
        }

        // Q - Bouncing Bomb
        if (auto* q = s_menu->add_sub_category("q_bouncing_bomb", "Q - Bouncing Bomb"))
        {
            q->add_checkbox("combo",  "Use in Combo",       menu::combo_q,  [](bool v) { menu::combo_q  = v; });
            q->add_checkbox("harass", "Use in Harass",      menu::harass_q, [](bool v) { menu::harass_q = v; });
            q->add_checkbox("lane",   "Use in Lane Clear",  menu::lane_q,   [](bool v) { menu::lane_q   = v; });
            q->add_checkbox("jungle", "Use in Jungle Clear",menu::jungle_q, [](bool v) { menu::jungle_q = v; });
        }


        // W - Satchel Charge
        if (auto* w = s_menu->add_sub_category("w_satchel_charge", "W - Satchel Charge"))
        {
            w->add_checkbox("combo",     "Use in Combo",     menu::combo_w,  [](bool v) { menu::combo_w  = v; });
            w->add_checkbox("harass",    "Use in Harass",    menu::harass_w, [](bool v) { menu::harass_w = v; });
            w->add_checkbox("flee",      "Use in Flee",      menu::flee_w,   [](bool v) { menu::flee_w   = v; });
            w->add_separator();
            w->add_checkbox("w_e_only",  "Only when E ready or killable", menu::combo_w_only_when_e_ready,
                [](bool v) { menu::combo_w_only_when_e_ready = v; });
            w->add_slider_int("w_push",  "Push if Enemy HP minus My HP >", 1, 99, 1, menu::combo_w_hp_diff_push,
                [](int v) { menu::combo_w_hp_diff_push = v; });
            w->add_checkbox("ew_pos_mode", "E into W Position Mode", menu::combo_mode_e_into_w,
                [](bool v) { menu::combo_mode_e_into_w = v; });
            w->add_separator();
            w->add_checkbox("korean",    "Korean Hidden Combo",          menu::combo_korean,
                [](bool v) { menu::combo_korean = v; });
            w->add_checkbox("w_gap",     "Auto W on Gapcloser",          menu::auto_w_on_gapclose,
                [](bool v) { menu::auto_w_on_gapclose = v; });
            w->add_checkbox("w_channel", "Auto W on Channeling Spells",  menu::auto_w_on_channeling,
                [](bool v) { menu::auto_w_on_channeling = v; });
            w->add_checkbox("w_turret",  "Auto W on Destroyable Turrets",menu::auto_w_on_destroyable_turret,
                [](bool v) { menu::auto_w_on_destroyable_turret = v; });
        }

        // E - Hexplosive Minefield
        if (auto* e = s_menu->add_sub_category("e_hexplosive_minefield", "E - Hexplosive Minefield"))
        {
            e->add_checkbox("combo",  "Use in Combo",        menu::combo_e,  [](bool v) { menu::combo_e  = v; });
            e->add_checkbox("harass", "Use in Harass",       menu::harass_e, [](bool v) { menu::harass_e = v; });
            e->add_checkbox("lane",   "Use in Lane Clear",   menu::lane_e,   [](bool v) { menu::lane_e   = v; });
            e->add_slider_int("e_min","  Min Minions to Hit",1, 6, 1, menu::lane_e_min_minions,
                [](int v) { menu::lane_e_min_minions = v; });
            e->add_checkbox("jungle", "Use in Jungle Clear", menu::jungle_e, [](bool v) { menu::jungle_e = v; });
            e->add_separator();
            e->add_checkbox("e_gap",      "Auto E on Gapcloser",       menu::auto_e_on_gapclose,
                [](bool v) { menu::auto_e_on_gapclose = v; });
            e->add_checkbox("e_gap_self", "  Cast on Self Position",   menu::auto_e_on_gapclose_self,
                [](bool v) { menu::auto_e_on_gapclose_self = v; });
        }


        // R - Mega Inferno Bomb
        if (auto* r = s_menu->add_sub_category("r_mega_inferno_bomb", "R - Mega Inferno Bomb"))
        {
            r->add_checkbox("auto_r",    "Auto R if Killable",    menu::auto_r_if_killable,
                [](bool v) { menu::auto_r_if_killable = v; });
            r->add_checkbox("auto_r_safe","  Only if No Enemies Nearby", menu::auto_r_only_no_enemies_nearby,
                [](bool v) { menu::auto_r_only_no_enemies_nearby = v; });
            r->add_hotkey("auto_r_toggle", "Toggle Auto R", 'Z', false, false,
                [](std::string* key, bool pressed) {
                    static bool was = false;
                    if (pressed && !was) menu::auto_r_if_killable = !menu::auto_r_if_killable;
                    was = pressed;
                    if (key) menu::auto_r_toggle_key = *key;
                });
            if (menu::auto_r_toggle_key.empty()) menu::auto_r_toggle_key = "Z";
            r->add_separator();
            r->add_checkbox("auto_r_aoe", "Auto R on AOE", menu::auto_r_aoe,
                [](bool v) { menu::auto_r_aoe = v; });
            r->add_slider_int("auto_r_aoe_min", "  Min Enemies to Hit", 2, 5, 1, menu::auto_r_aoe_min_enemies,
                [](int v) { menu::auto_r_aoe_min_enemies = v; });
            r->add_separator();
            r->add_hotkey("semi_r", "Semi-Manual R", 'A', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_r_active = pressed;
                    if (key) menu::semi_r_key = *key;
                });
            if (menu::semi_r_key.empty()) menu::semi_r_key = "A";
        }


        // Mana
        if (auto* mana = s_menu->add_sub_category("mana", "Mana Thresholds"))
        {
            mana->add_slider_int("harass", "Harass Mana %",    0, 100, 5, menu::harass_mana_pct,
                [](int v) { menu::harass_mana_pct = v; });
            mana->add_slider_int("lane",   "Lane Clear Mana %", 0, 100, 5, menu::lane_mana_pct,
                [](int v) { menu::lane_mana_pct = v; });
        }


        // Drawings
        if (auto* d = s_menu->add_sub_category("drawings", "Drawings"))
        {
            d->add_checkbox("only_ready", "Only Draw Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            d->add_separator();
            d->add_checkbox("draw_q",        "Draw Q Range",            menu::draw_q,             [](bool v) { menu::draw_q             = v; });
            d->add_checkbox("draw_w",        "Draw W Range",            menu::draw_w,             [](bool v) { menu::draw_w             = v; });
            d->add_checkbox("draw_e",        "Draw E Range",            menu::draw_e,             [](bool v) { menu::draw_e             = v; });
            d->add_checkbox("draw_r",        "Draw R Range",            menu::draw_r,             [](bool v) { menu::draw_r             = v; });
            d->add_checkbox("draw_r_mini",   "Draw R on Minimap",       menu::draw_r_minimap,     [](bool v) { menu::draw_r_minimap     = v; });
            d->add_checkbox("draw_r_kill",   "Draw R Killable Text",    menu::draw_r_killable,    [](bool v) { menu::draw_r_killable    = v; });
            d->add_checkbox("draw_r_dmg",    "Draw R Damage on HP Bar", menu::draw_r_damage_bar,  [](bool v) { menu::draw_r_damage_bar  = v; });
            d->add_separator();
            d->add_colorpicker("col_q",      "Q Color",            menu::color_q,            [](uint32_t c) { menu::color_q            = c; });
            d->add_colorpicker("col_w",      "W Color",            menu::color_w,            [](uint32_t c) { menu::color_w            = c; });
            d->add_colorpicker("col_e",      "E Color",            menu::color_e,            [](uint32_t c) { menu::color_e            = c; });
            d->add_colorpicker("col_r",      "R Color",            menu::color_r,            [](uint32_t c) { menu::color_r            = c; });
            d->add_colorpicker("col_r_dmg",  "R Damage Bar Color", menu::color_r_damage_bar, [](uint32_t c) { menu::color_r_damage_bar = c; });
        }
    }

    void setup_infotab()
    {
        if (!sdk::infotab) return;

        infotab_sdk::text_entry semi_r_title;
        semi_r_title.text = "Semi R";

        infotab_sdk::text_entry auto_r_title;
        auto_r_title.text = "Auto R";

        if (!menu::semi_r_key.empty())
        {
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key, semi_r_title, []() -> infotab_sdk::text_entry {
                    infotab_sdk::text_entry e;
                    e.text  = menu::semi_r_active ? "ON" : "OFF";
                    e.color = menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return e;
                });
        }

        if (!menu::auto_r_toggle_key.empty())
        {
            menu::infotab_auto_r = sdk::infotab->add_hotkey_text(
                &menu::auto_r_toggle_key, auto_r_title, []() -> infotab_sdk::text_entry {
                    infotab_sdk::text_entry e;
                    e.text  = menu::auto_r_if_killable ? "ON" : "OFF";
                    e.color = menu::auto_r_if_killable ? 0xFF00FF00u : 0xFFFF0000u;
                    return e;
                });
        }
    }

    // =========================================================
    //  Gapcloser handler (new_path event, internal only)
    // =========================================================
    void __fastcall on_new_path(game_object* obj, bool is_dash, float /*dash_speed*/)
    {
        if (!is_dash) return;
        auto* p = player();
        if (!p || p->is_dead()) return;
        if (!obj || !obj->is_hero() || !obj->is_valid() || obj->is_dead()) return;
        if (obj->get_team_id() == p->get_team_id()) return;

        if (menu::auto_e_on_gapclose && spell_ready(SLOT_E))
        {
            math::vector3 cast_pos = menu::auto_e_on_gapclose_self
                ? p->get_position()
                : obj->get_position();
            cast_e_at(cast_pos);
        }

        if (menu::auto_w_on_gapclose && spell_ready(SLOT_W) && !has_ziggs_w_buff())
        {
            math::vector3 w_pos = p->get_position().extended(obj->get_position(), 0.f, true);
            if (cast_w_at(w_pos))
            {
                s_gapcloser_w_pending = true;
                s_gapcloser_w_time    = now();
            }
        }
    }

} // anonymous namespace


// =========================================================
//  Public API
// =========================================================

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != std::string("Ziggs")) return;

    setup_menu();
    setup_infotab();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->register_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
    }

    g_sdk->log_console("[MeowAIO] Ziggs loaded MEOW!");
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->unregister_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
    }

    if (sdk::infotab)
    {
        if (menu::infotab_semi_r) { sdk::infotab->remove_text(menu::infotab_semi_r); menu::infotab_semi_r = 0; }
        if (menu::infotab_auto_r) { sdk::infotab->remove_text(menu::infotab_auto_r); menu::infotab_auto_r = 0; }
    }

    s_menu                 = nullptr;
    s_korean_state         = 0;
    s_threw_w              = false;
    s_e_recently_cast      = false;
    s_flee_w_pending       = false;
    s_gapcloser_w_pending  = false;
    menu::semi_r_active = false;
    menu::semi_r_key.clear();
    menu::auto_r_toggle_key.clear();
    for (auto& t : s_last_cast) t = 0.f;

    g_sdk->log_console("[MeowAIO] Ziggs unloaded successfully!");
}

void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = player();
    if (!p || p->is_dead()) return;

    // Timed W detonation for gapcloser and flee
    if (s_gapcloser_w_pending && now() - s_gapcloser_w_time >= 0.26f)
    {
        s_gapcloser_w_pending = false;
        cast_w_detonate();
    }
    if (s_flee_w_pending && now() - s_flee_w_time >= 0.285f)
    {
        s_flee_w_pending = false;
        cast_w_detonate();
    }

    // Expire E-casted flag after 1 s
    if (s_e_recently_cast && now() - s_e_cast_time > 1.0f)
        s_e_recently_cast = false;

    // Korean combo state machine (runs regardless of orbwalker mode)
    korean_hidden_combo();
    if (s_korean_state != 0) return;

    // Always-on auto spells
    auto_r_if_killable();
    auto_r_aoe();
    check_destroyable_turret();
    auto_w_on_channeling();
    semi_r();

    if (!sdk::orbwalker) return;

    if      (sdk::orbwalker->combo())                          { menu::combo_mode_e_into_w ? e_into_w_combo() : basic_combo(); }
    else if (sdk::orbwalker->harass())                         { harass(); }
    else if (sdk::orbwalker->flee())                           { flee(); }
    else if (sdk::orbwalker->clear() || sdk::orbwalker->lasthit()) { lane_clear(); jungle_clear(); }
}

void __fastcall on_draw()
{
    if (!g_sdk || !g_sdk->renderer || !g_sdk->object_manager) return;
    auto* p = player();
    if (!p || p->is_dead()) return;

    // R range on the minimap
    if (menu::draw_r_minimap && (!menu::draw_only_ready || spell_ready(SLOT_R)))
    {
        g_sdk->renderer->add_circle_minimap(
            p->get_position(), spell_data::r_range, 1.f, menu::color_r);
    }

    const bool r_up = spell_ready(SLOT_R);

    for (auto* hero : g_sdk->object_manager->get_heroes())
    {
        if (!is_valid_enemy(hero)) continue;
        if (!r_up) continue;

        const float r_dmg = get_spell_dmg(SLOT_R, hero);

        // R damage chunk on the enemy health bar
        if (menu::draw_r_damage_bar)
            draw_r_damage_overlay(hero, r_dmg);

        // "R KILLABLE" text drawn on each killable enemy
        if (menu::draw_r_killable && killable_r(hero))
        {
            const auto screen = g_sdk->renderer->world_to_screen(hero->get_position());
            if (screen.x > 1.f || screen.y > 1.f)
            {
                math::vector2 pos = { screen.x, screen.y - 60.f };
                g_sdk->renderer->add_text("R KILLABLE", 18.f, pos, 1, 0xFFFF0000u);
            }
        }
    }

    const auto player_screen = g_sdk->renderer->world_to_screen(p->get_position());
    const bool player_on_screen = player_screen.x > 1.f || player_screen.y > 1.f;

    // Auto R toggle status drawn below the player
    if (player_on_screen)
    {
        const bool on = menu::auto_r_if_killable;
        char label[32]{};
        sprintf_s(label, "Auto R: %s", on ? "ON" : "OFF");
        math::vector2 pos = { player_screen.x, player_screen.y + 30.f };
        g_sdk->renderer->add_text(label, 14.f, pos, 1, on ? 0xFF00CC00u : 0xFFCC0000u);
    }
}

void __fastcall on_draw_world()
{
    auto* p = player();
    if (!p || p->is_dead()) return;

    if (menu::draw_q && (!menu::draw_only_ready || spell_ready(SLOT_Q)))
        draw_circle(p->get_position(), spell_data::q_range, menu::color_q);
    if (menu::draw_w && (!menu::draw_only_ready || spell_ready(SLOT_W)))
        draw_circle(p->get_position(), spell_data::w_range, menu::color_w);
    if (menu::draw_e && (!menu::draw_only_ready || spell_ready(SLOT_E)))
        draw_circle(p->get_position(), spell_data::e_range, menu::color_e);
    if (menu::draw_r && (!menu::draw_only_ready || spell_ready(SLOT_R)))
        draw_circle(p->get_position(), spell_data::r_range, menu::color_r);
}

void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
{
    auto* p = player();
    if (!p || !obj || obj != p || !cast) return;

    const int slot = cast->get_spell_slot();

    if (slot == SLOT_W)
        s_threw_w = true;
    else if (slot == SLOT_E)
    {
        s_e_recently_cast = true;
        s_e_cast_time     = now();
    }
}

void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
{
    auto* p = player();
    if (!p || !obj || obj != p || !buff) return;

    const std::string name = buff->get_name();
    if (!name.empty() && _stricmp(name.c_str(), "ZiggsW") == 0)
        s_threw_w = false;
}

} // namespace ziggs
