#include "kayle.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "infotab_sdk.hpp"
#include "dmg_sdk.hpp"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>

namespace kayle
{
namespace
{
    // =========================================================
    //  Internal state
    // =========================================================
    menu_category* s_menu = nullptr;
    float s_last_cast[4] = {};

    // =========================================================
    //  Damage tables (Kayle)
    // =========================================================
    const float Q_BASE[5] = { 60.f,  95.f, 130.f, 165.f, 200.f };
    constexpr float Q_AP = 0.60f;

    const float E_ACTIVE_BASE[5]  = { 10.f, 40.f, 70.f, 100.f, 130.f };
    const float E_ACTIVE_PCTMHP[5]= { 0.04f, 0.06f, 0.08f, 0.10f, 0.12f };
    constexpr float E_ACTIVE_AP = 0.60f;

    const float E_PASSIVE_BASE[5] = { 10.f, 15.f, 20.f, 25.f, 30.f };
    constexpr float E_PASSIVE_AP_FLAT = 0.10f; // 10% AP flat bonus
    constexpr float E_PASSIVE_PCTMHP  = 0.10f; // 10% missing HP

    const float R_BASE[3] = { 200.f, 350.f, 500.f };
    constexpr float R_AP = 0.60f;

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

    float missing_hp(game_object* obj)
    {
        if (!obj) return 0.f;
        return (std::max)(0.f, obj->get_max_hp() - obj->get_hp());
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

    bool is_valid_ally(game_object* obj, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead()) return false;
        if (!obj->is_visible()) return false;
        if (obj->get_team_id() != p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(obj->get_position()) > range)
            return false;
        return true;
    }

    bool is_invulnerable(game_object* obj)
    {
        if (!obj) return false;
        return obj->has_buff_of_type(buff_type::invulnerability)
            || obj->has_buff_of_type(buff_type::spell_immunity)
            || obj->has_buff_of_type(buff_type::unkillable);
    }

    bool has_hard_cc(game_object* obj)
    {
        if (!obj) return false;
        return obj->has_buff_of_type(buff_type::stun)
            || obj->has_buff_of_type(buff_type::snare)
            || obj->has_buff_of_type(buff_type::knockup)
            || obj->has_buff_of_type(buff_type::asleep)
            || obj->has_buff_of_type(buff_type::suppression)
            || obj->has_buff_of_type(buff_type::taunt);
    }

    int count_enemies_near(const math::vector3& pos, float radius)
    {
        int n = 0;
        if (!g_sdk || !g_sdk->object_manager) return 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
            if (is_valid_enemy(hero) && hero->get_position().distance(pos) <= radius) ++n;
        return n;
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

    float aa_range_full(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        return p->get_attack_range() + p->get_bounding_radius() + target->get_bounding_radius();
    }

    // =========================================================
    //  Damage helpers
    // =========================================================
    float calc_q_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;
        const int lvl = spell_level(SLOT_Q);
        if (lvl <= 0) return 0.f;
        const float raw = Q_BASE[std::clamp(lvl - 1, 0, 4)] + Q_AP * p->get_ability_power();
        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
    }

    float calc_e_active_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;
        const int lvl = spell_level(SLOT_E);
        if (lvl <= 0) return 0.f;
        const int idx = std::clamp(lvl - 1, 0, 4);
        const float raw = E_ACTIVE_BASE[idx]
            + E_ACTIVE_AP * p->get_ability_power()
            + E_ACTIVE_PCTMHP[idx] * missing_hp(target);
        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
    }

    float calc_e_passive_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;
        const int lvl = spell_level(SLOT_E);
        if (lvl <= 0) return 0.f;
        const float ap = p->get_ability_power();
        const float raw = E_PASSIVE_BASE[std::clamp(lvl - 1, 0, 4)]
            + E_PASSIVE_AP_FLAT * ap
            + E_PASSIVE_PCTMHP * missing_hp(target);
        // True damage at level 16+ Transcendent form. Approximate by skipping mitigation.
        if (p->get_level() >= 16) return raw;
        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
    }

    float calc_r_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;
        const int lvl = spell_level(SLOT_R);
        if (lvl <= 0) return 0.f;
        const float raw = R_BASE[std::clamp(lvl - 1, 0, 2)] + R_AP * p->get_ability_power();
        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
    }

    float aa_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;
        return sdk::damage->get_aa_damage(p, target, true);
    }

    bool q_kills(game_object* t)   { return t && calc_q_damage(t)               > t->get_hp() + 5.f; }
    bool e_act_kills(game_object* t)
    {
        return t && (aa_damage(t) + calc_e_active_damage(t) + calc_e_passive_damage(t))
            > t->get_hp() + 5.f;
    }

    // =========================================================
    //  Prediction
    // =========================================================
    pred_sdk::pred_data predict_q(game_object* t)
    {
        if (!sdk::prediction) return {};
        pred_sdk::spell_data sd;
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.range              = spell_data::q_range;
        sd.radius             = spell_data::q_radius;
        sd.delay              = spell_data::q_delay;
        sd.projectile_speed   = spell_data::q_speed;
        sd.spell_slot         = SLOT_Q;
        sd.expected_hitchance = menu::q_hitchance;
        sd.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
            pred_sdk::collision_type::hero,
            pred_sdk::collision_type::unit,
        };
        return sdk::prediction->predict(t, sd);
    }

    // =========================================================
    //  Cast helpers
    // =========================================================
    bool cast_q(game_object* target, int hc_override = 0)
    {
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q) || !sdk::common || !can_cast(SLOT_Q)) return false;
        if (!is_valid_enemy(target, spell_data::q_range)) return false;

        pred_sdk::pred_data pred;
        if (hc_override > 0)
        {
            // override hitchance for opportunistic shots (CC / dashing targets)
            pred_sdk::spell_data sd;
            sd.spell_type         = pred_sdk::spell_type::linear;
            sd.range              = spell_data::q_range;
            sd.radius             = spell_data::q_radius;
            sd.delay              = spell_data::q_delay;
            sd.projectile_speed   = spell_data::q_speed;
            sd.spell_slot         = SLOT_Q;
            sd.expected_hitchance = hc_override;
            sd.forbidden_collisions = {
                pred_sdk::collision_type::yasuo_wall,
                pred_sdk::collision_type::braum_wall,
            };
            pred = sdk::prediction ? sdk::prediction->predict(target, sd) : pred_sdk::pred_data{};
        }
        else
        {
            pred = predict_q(target);
        }

        if (!pred.is_valid) return false;
        if (sdk::common->cast_spell(SLOT_Q, pred.cast_position))
        {
            mark_cast(SLOT_Q);
            return true;
        }
        return false;
    }

    bool cast_w_on(game_object* ally)
    {
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W) || !sdk::common || !can_cast(SLOT_W)) return false;
        if (!ally) return false;
        if (sdk::common->cast_spell(SLOT_W, ally))
        {
            mark_cast(SLOT_W);
            return true;
        }
        return false;
    }

    bool cast_e_self()
    {
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E) || !sdk::common || !can_cast(SLOT_E)) return false;
        if (sdk::common->cast_spell(SLOT_E))
        {
            mark_cast(SLOT_E);
            return true;
        }
        return false;
    }

    bool cast_r_on(game_object* ally)
    {
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R) || !sdk::common || !can_cast(SLOT_R, 0.30f)) return false;
        if (!ally) return false;
        if (sdk::common->cast_spell(SLOT_R, ally))
        {
            mark_cast(SLOT_R);
            return true;
        }
        return false;
    }

    // =========================================================
    //  Auto Q on CC / dashing
    // =========================================================
    void auto_q_opportunistic()
    {
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return;
        if (!menu::auto_q_cc && !menu::auto_q_dash) return;
        if (!g_sdk || !g_sdk->object_manager) return;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero, spell_data::q_range)) continue;
            const bool cc   = menu::auto_q_cc   && has_hard_cc(hero);
            const bool dash = menu::auto_q_dash && hero->is_dashing();
            if (cc || dash)
            {
                if (cast_q(hero, pred_sdk::hitchance::low)) return;
            }
        }
    }

    // =========================================================
    //  Killsteal
    // =========================================================
    void killsteal()
    {
        if (!g_sdk || !g_sdk->object_manager) return;

        if (menu::ks_q && spell_ready(SLOT_Q) && has_mana(SLOT_Q))
        {
            for (auto* h : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(h, spell_data::q_range)) continue;
                if (is_invulnerable(h)) continue;
                if (q_kills(h))
                {
                    if (cast_q(h, pred_sdk::hitchance::medium)) return;
                }
            }
        }

        if (menu::ks_e && spell_ready(SLOT_E) && has_mana(SLOT_E))
        {
            auto* p = player();
            if (!p) return;
            for (auto* h : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(h)) continue;
                if (is_invulnerable(h)) continue;
                const float dist = p->get_position().distance(h->get_position());
                if (dist > spell_data::e_active_range + h->get_bounding_radius()) continue;
                if (e_act_kills(h))
                {
                    if (cast_e_self()) return;
                }
            }
        }
    }

    // =========================================================
    //  Auto W self / ally heal
    // =========================================================
    void auto_w()
    {
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return;
        auto* p = player();
        if (!p) return;

        if (menu::auto_w_self && hp_pct(p) <= static_cast<float>(menu::auto_w_self_hp)
            && count_enemies_near(p->get_position(), 900.f) >= 1)
        {
            if (cast_w_on(p)) return;
        }

        if (!menu::auto_w_ally || !g_sdk || !g_sdk->object_manager) return;
        for (auto* ally : g_sdk->object_manager->get_heroes())
        {
            if (ally == p) continue;
            if (!is_valid_ally(ally, spell_data::w_range)) continue;
            if (hp_pct(ally) > static_cast<float>(menu::auto_w_ally_hp)) continue;
            if (count_enemies_near(ally->get_position(), 850.f) <= 0) continue;
            if (cast_w_on(ally)) return;
        }
    }

    // =========================================================
    //  Auto R
    // =========================================================
    bool r_allowed_on(game_object* ally)
    {
        if (!ally) return false;
        auto it = menu::r_on_ally.find(ally->get_network_id());
        return it == menu::r_on_ally.end() || it->second;
    }

    bool auto_r_target_valid(game_object* t, int hp_threshold)
    {
        if (!t || is_invulnerable(t)) return false;
        if (!r_allowed_on(t)) return false;
        if (hp_pct(t) > static_cast<float>(hp_threshold)) return false;
        if (count_enemies_near(t->get_position(), 850.f) < menu::auto_r_min_enemies) return false;
        return true;
    }

    void auto_r()
    {
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return;
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        // Self first
        if (menu::auto_r_self && auto_r_target_valid(p, menu::auto_r_self_hp))
        {
            if (cast_r_on(p)) return;
        }

        if (!menu::auto_r_ally) return;

        game_object* best = nullptr;
        float        worst_hp = 101.f;
        for (auto* ally : g_sdk->object_manager->get_heroes())
        {
            if (ally == p) continue;
            if (!is_valid_ally(ally, spell_data::r_range)) continue;
            if (!auto_r_target_valid(ally, menu::auto_r_ally_hp)) continue;
            const float hpp = hp_pct(ally);
            if (hpp < worst_hp) { worst_hp = hpp; best = ally; }
        }

        if (best) cast_r_on(best);
    }

    void semi_r()
    {
        if (!menu::semi_r_active || !spell_ready(SLOT_R) || !has_mana(SLOT_R)) return;
        auto* p = player();
        if (!p) return;

        // Hovered ally bypasses the whitelist (explicit user intent),
        // but the auto-fallback respects it.
        game_object* target = nullptr;
        if (g_sdk && g_sdk->hud_manager)
        {
            auto* hovered = g_sdk->hud_manager->get_hovered_target();
            if (hovered && is_valid_ally(hovered, spell_data::r_range) && !is_invulnerable(hovered))
                target = hovered;
        }

        if (!target && g_sdk && g_sdk->object_manager)
        {
            float worst = 101.f;
            for (auto* ally : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_ally(ally, spell_data::r_range)) continue;
                if (is_invulnerable(ally)) continue;
                if (!r_allowed_on(ally)) continue;
                const float hpp = hp_pct(ally);
                if (hpp < worst) { worst = hpp; target = ally; }
            }
        }

        if (!target) target = p;
        cast_r_on(target);
    }

    // =========================================================
    //  Combat modes
    // =========================================================
    void combo()
    {
        auto* p = player();
        auto* t = best_target(std::max(spell_data::q_range, spell_data::e_active_range));
        if (!p || !t || is_invulnerable(t)) return;

        const float dist = p->get_position().distance(t->get_position());
        const float reach = aa_range_full(t);

        if (menu::combo_e && spell_ready(SLOT_E) && has_mana(SLOT_E)
            && dist <= spell_data::e_active_range + t->get_bounding_radius())
        {
            // Use E if it kills the target outright, OR to extend reach to land an AA.
            const bool exec    = e_act_kills(t);
            const bool extend  = menu::combo_e_extend && dist > reach && dist <= spell_data::e_active_range;
            if (exec || extend)
                cast_e_self();
        }

        if (menu::combo_q && (dist > reach || has_hard_cc(t) || t->is_dashing()))
            cast_q(t);
    }

    void harass()
    {
        if (mana_pct() < static_cast<float>(menu::harass_mana_pct)) return;
        auto* p = player();
        auto* t = best_target(spell_data::q_range);
        if (!p || !t) return;

        if (menu::combo_e_harass && spell_ready(SLOT_E) && has_mana(SLOT_E))
        {
            const float dist = p->get_position().distance(t->get_position());
            if (dist > aa_range_full(t) && dist <= spell_data::e_active_range + t->get_bounding_radius())
                cast_e_self();
        }

        if (menu::combo_q_harass) cast_q(t);
    }

    void lane_clear()
    {
        if (mana_pct() < static_cast<float>(menu::lane_mana_pct)) return;
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!g_sdk || !g_sdk->object_manager) return;
        auto* p = player();
        if (!p) return;

        // Q on the densest minion cluster
        if (menu::lane_q && spell_ready(SLOT_Q) && has_mana(SLOT_Q))
        {
            game_object* best = nullptr;
            int          best_count = 0;
            for (auto* m : g_sdk->object_manager->get_minions())
            {
                if (!m || !m->is_valid() || m->is_dead()) continue;
                if (m->get_team_id() == p->get_team_id()) continue;
                if (!m->is_lane_minion()) continue;
                if (p->get_position().distance(m->get_position()) > spell_data::q_range) continue;

                int count = 0;
                for (auto* o : g_sdk->object_manager->get_minions())
                {
                    if (!o || !o->is_valid() || o->is_dead()) continue;
                    if (o->get_team_id() == p->get_team_id()) continue;
                    if (!o->is_lane_minion()) continue;
                    if (o->get_position().distance(m->get_position()) <= spell_data::q_radius * 2.f) ++count;
                }
                if (count > best_count) { best_count = count; best = m; }
            }
            if (best && best_count >= menu::lane_q_min_hit)
            {
                if (cast_q(best, pred_sdk::hitchance::low)) return;
            }
        }

        // E to last-hit cannon / siege minions
        if (menu::lane_e_cannon && spell_ready(SLOT_E) && has_mana(SLOT_E))
        {
            for (auto* m : g_sdk->object_manager->get_minions())
            {
                if (!m || !m->is_valid() || m->is_dead()) continue;
                if (m->get_team_id() == p->get_team_id()) continue;
                if (!m->is_lane_minion_siege()) continue;
                if (p->get_position().distance(m->get_position()) > spell_data::e_active_range + m->get_bounding_radius()) continue;
                const float dmg = aa_damage(m) + calc_e_active_damage(m) + calc_e_passive_damage(m);
                if (dmg + 5.f >= m->get_hp())
                {
                    if (cast_e_self()) return;
                }
            }
        }
    }

    void jungle_clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!g_sdk || !g_sdk->object_manager) return;
        auto* p = player();
        if (!p) return;

        game_object* big = nullptr;
        for (auto* m : g_sdk->object_manager->get_monsters())
        {
            if (!m || !m->is_valid() || m->is_dead()) continue;
            if (p->get_position().distance(m->get_position()) > spell_data::q_range) continue;
            if (!big || m->get_max_hp() > big->get_max_hp()) big = m;
        }
        if (!big) return;

        const float dist = p->get_position().distance(big->get_position());

        if (menu::jungle_q && spell_ready(SLOT_Q) && has_mana(SLOT_Q))
        {
            if (cast_q(big, pred_sdk::hitchance::low)) return;
        }

        if (menu::jungle_e && spell_ready(SLOT_E) && has_mana(SLOT_E)
            && dist <= spell_data::e_active_range + big->get_bounding_radius())
        {
            cast_e_self();
        }
    }

    void flee()
    {
        if (!menu::flee_w || !spell_ready(SLOT_W) || !has_mana(SLOT_W)) return;
        auto* p = player();
        if (!p) return;
        cast_w_on(p);
    }

    // =========================================================
    //  Drawings
    // =========================================================
    void draw_circle(const math::vector3& pos, float radius, uint32_t color)
    {
        if (!color) return;
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, radius, 2.f, color);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, radius, 1.f, color);
    }

    float estimate_combo_damage(game_object* t)
    {
        if (!t) return 0.f;
        float total = aa_damage(t) + calc_e_passive_damage(t);
        if (spell_ready(SLOT_E)) total += calc_e_active_damage(t);
        if (spell_ready(SLOT_Q)) total += calc_q_damage(t);
        return total;
    }

    // =========================================================
    //  Menu setup
    // =========================================================
    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_kayle", "MeowAIO - Kayle");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Kayle: failed to create menu category");
            return;
        }

        if (auto* q = s_menu->add_sub_category("q_radiant_blast", "Q - Radiant Blast"))
        {
            q->add_checkbox("combo",   "Use in Combo",        menu::combo_q,         [](bool v) { menu::combo_q         = v; });
            q->add_checkbox("harass",  "Use in Harass",       menu::combo_q_harass,  [](bool v) { menu::combo_q_harass  = v; });
            q->add_checkbox("lane",    "Use in Lane Clear",   menu::lane_q,          [](bool v) { menu::lane_q          = v; });
            q->add_slider_int("lane_min", "Lane Q Min Minions", 2, 6, 1, menu::lane_q_min_hit, [](int v) { menu::lane_q_min_hit = v; });
            q->add_checkbox("jungle",  "Use in Jungle Clear", menu::jungle_q,        [](bool v) { menu::jungle_q        = v; });
            q->add_separator();
            q->add_checkbox("auto_cc",   "Auto Q on Hard CC",   menu::auto_q_cc,   [](bool v) { menu::auto_q_cc   = v; });
            q->add_checkbox("auto_dash", "Auto Q on Dashing",   menu::auto_q_dash, [](bool v) { menu::auto_q_dash = v; });
            q->add_checkbox("ks_q",      "Q Killsteal",         menu::ks_q,        [](bool v) { menu::ks_q        = v; });
            q->add_slider_int("hc", "Hitchance %", 30, 100, 5, menu::q_hitchance, [](int v) { menu::q_hitchance = v; });
        }

        if (auto* w = s_menu->add_sub_category("w_celestial_blessing", "W - Celestial Blessing"))
        {
            w->add_checkbox("auto_self", "Auto W Self",  menu::auto_w_self, [](bool v) { menu::auto_w_self = v; });
            w->add_slider_int("self_hp", "Self HP %", 10, 90, 5, menu::auto_w_self_hp, [](int v) { menu::auto_w_self_hp = v; });
            w->add_checkbox("auto_ally", "Auto W Ally",  menu::auto_w_ally, [](bool v) { menu::auto_w_ally = v; });
            w->add_slider_int("ally_hp", "Ally HP %", 10, 90, 5, menu::auto_w_ally_hp, [](int v) { menu::auto_w_ally_hp = v; });
            w->add_checkbox("flee",      "Use in Flee", menu::flee_w,      [](bool v) { menu::flee_w      = v; });
        }

        if (auto* e = s_menu->add_sub_category("e_starfire_spellblade", "E - Starfire Spellblade"))
        {
            e->add_checkbox("combo",   "Use in Combo",         menu::combo_e,        [](bool v) { menu::combo_e        = v; });
            e->add_checkbox("extend",  "Extend AA Range",    menu::combo_e_extend, [](bool v) { menu::combo_e_extend = v; });
            e->add_checkbox("harass",  "Use in Harass",        menu::combo_e_harass, [](bool v) { menu::combo_e_harass = v; });
            e->add_checkbox("lane_e",  "Last-hit Cannons",     menu::lane_e_cannon,  [](bool v) { menu::lane_e_cannon  = v; });
            e->add_checkbox("jungle",  "Use in Jungle Clear",  menu::jungle_e,       [](bool v) { menu::jungle_e       = v; });
            e->add_checkbox("ks_e",    "E Killsteal",          menu::ks_e,           [](bool v) { menu::ks_e           = v; });
        }

        if (auto* r = s_menu->add_sub_category("r_divine_judgment", "R - Divine Judgment"))
        {
            r->add_checkbox("auto_self", "Auto R Self",  menu::auto_r_self,        [](bool v) { menu::auto_r_self = v; });
            r->add_slider_int("self_hp", "Self HP %",   5, 50, 1, menu::auto_r_self_hp, [](int v) { menu::auto_r_self_hp = v; });
            r->add_checkbox("auto_ally", "Auto R Ally",  menu::auto_r_ally,        [](bool v) { menu::auto_r_ally = v; });
            r->add_slider_int("ally_hp", "Ally HP %",   5, 50, 1, menu::auto_r_ally_hp, [](int v) { menu::auto_r_ally_hp = v; });
            r->add_slider_int("min_e",   "Min Nearby Enemies", 0, 5, 1, menu::auto_r_min_enemies, [](int v) { menu::auto_r_min_enemies = v; });
            r->add_separator();
            r->add_hotkey("semi", "Semi-Manual R (hovered ally / lowest HP)", 'T', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_r_active = pressed;
                    if (key) menu::semi_r_key = *key;
                });
            if (menu::semi_r_key.empty()) menu::semi_r_key = "T";

            r->add_separator();
            if (auto* sm = r->add_sub_category("r_on_allies", "R On Allies"))
            {
                auto* p = player();
                if (p && g_sdk && g_sdk->object_manager)
                {
                    for (auto* h : g_sdk->object_manager->get_heroes())
                    {
                        if (!h || !h->is_valid()) continue;
                        if (h->get_team_id() != p->get_team_id()) continue;

                        const uint32_t id = h->get_network_id();
                        menu::r_on_ally[id] = true;
                        sm->add_checkbox(
                            "r_" + std::to_string(id),
                            h->get_char_name(),
                            true,
                            [id](bool v) { menu::r_on_ally[id] = v; });
                    }
                }
            }
        }

        if (auto* mana = s_menu->add_sub_category("mana", "Mana Thresholds"))
        {
            mana->add_slider_int("harass", "Harass Mana %",     0, 100, 5, menu::harass_mana_pct, [](int v) { menu::harass_mana_pct = v; });
            mana->add_slider_int("lane",   "Lane Clear Mana %", 0, 100, 5, menu::lane_mana_pct,   [](int v) { menu::lane_mana_pct   = v; });
        }

        if (auto* d = s_menu->add_sub_category("drawings", "Drawings"))
        {
            d->add_checkbox("only_ready", "Only Draw Ready Spells", menu::draw_only_ready, [](bool v) { menu::draw_only_ready = v; });
            d->add_separator();
            d->add_checkbox("draw_q",   "Draw Q Range",     menu::draw_q,      [](bool v) { menu::draw_q      = v; });
            d->add_checkbox("draw_e",   "Draw E Range",     menu::draw_e,      [](bool v) { menu::draw_e      = v; });
            d->add_checkbox("draw_r",   "Draw R Range",     menu::draw_r,      [](bool v) { menu::draw_r      = v; });
            d->add_checkbox("draw_dmg", "Damage Indicator", menu::draw_damage, [](bool v) { menu::draw_damage = v; });
            d->add_separator();
            d->add_colorpicker("col_q", "Q Color", menu::color_q, [](uint32_t c) { menu::color_q = c; });
            d->add_colorpicker("col_e", "E Color", menu::color_e, [](uint32_t c) { menu::color_e = c; });
            d->add_colorpicker("col_r", "R Color", menu::color_r, [](uint32_t c) { menu::color_r = c; });
        }
    }

    void setup_infotab()
    {
        if (!sdk::infotab) return;

        infotab_sdk::text_entry semi_title;
        semi_title.text = "Semi R";

        if (!menu::semi_r_key.empty())
        {
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key, semi_title, []() -> infotab_sdk::text_entry {
                    infotab_sdk::text_entry e;
                    e.text  = menu::semi_r_active ? "ON" : "OFF";
                    e.color = menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return e;
                });
        }
    }

} // anonymous namespace

// =========================================================
//  Public API
// =========================================================
void load()
{
    auto* p = (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->get_char_name() != std::string("Kayle")) return;

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
    }

    g_sdk->log_console("[MeowAIO] Kayle loaded MEOW!");
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
    }

    if (sdk::infotab && menu::infotab_semi_r)
    {
        sdk::infotab->remove_text(menu::infotab_semi_r);
        menu::infotab_semi_r = 0;
    }

    s_menu              = nullptr;
    menu::semi_r_active = false;
    menu::semi_r_key.clear();
    menu::r_on_ally.clear();
    for (auto& t : s_last_cast) t = 0.f;

    if (g_sdk) g_sdk->log_console("[MeowAIO] Kayle unloaded successfully!");
}

void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->is_dead()) return;

    // Always-on automations (run regardless of orbwalker mode).
    semi_r();
    auto_r();
    auto_w();
    killsteal();
    auto_q_opportunistic();

    if (!sdk::orbwalker) return;

    if      (sdk::orbwalker->combo())  combo();
    else if (sdk::orbwalker->harass()) harass();
    else if (sdk::orbwalker->flee())   flee();
    else if (sdk::orbwalker->clear() || sdk::orbwalker->lasthit())
    {
        lane_clear();
        jungle_clear();
    }
}

void __fastcall on_draw()
{
    if (!menu::draw_damage) return;
    if (!g_sdk || !g_sdk->renderer || !g_sdk->object_manager) return;
    auto* p = (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->is_dead()) return;

    for (auto* hero : g_sdk->object_manager->get_heroes())
    {
        if (!is_valid_enemy(hero)) continue;
        const float dmg = estimate_combo_damage(hero);
        if (dmg > 0.f)
            g_sdk->renderer->add_damage_indicator(hero, dmg);
    }
}

void __fastcall on_draw_world()
{
    auto* p = (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->is_dead()) return;

    if (menu::draw_q && (!menu::draw_only_ready || spell_ready(SLOT_Q)))
        draw_circle(p->get_position(), spell_data::q_range, menu::color_q);
    if (menu::draw_e && (!menu::draw_only_ready || spell_ready(SLOT_E)))
        draw_circle(p->get_position(), spell_data::e_active_range, menu::color_e);
    if (menu::draw_r && (!menu::draw_only_ready || spell_ready(SLOT_R)))
        draw_circle(p->get_position(), spell_data::r_range, menu::color_r);
}

} // namespace kayle
