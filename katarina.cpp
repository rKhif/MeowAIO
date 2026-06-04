#include "katarina.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace katarina
{
namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    constexpr float Q_RANGE = 625.f;
    constexpr float W_RANGE = 400.f;
    constexpr float E_RANGE = 725.f;
    constexpr float E_DAGGER_RANGE = 825.f;
    constexpr float R_RANGE = 550.f;
    constexpr float DAGGER_DAMAGE_RADIUS = 450.f;
    constexpr float DAGGER_DRAW_RADIUS = 400.f;
    constexpr float DAGGER_LIFETIME = 5.4f;
    constexpr float MIN_CAST_DELAY = 0.08f;

    struct dagger_info
    {
        math::vector3 position {};
        float start_time = 0.f;
        float end_time = 0.f;
    };

    menu_category* s_menu = nullptr;
    std::unordered_map<uint32_t, dagger_info> s_daggers;
    bool s_casting_r = false;
    float s_last_cast_time = 0.f;

    namespace menu
    {
        inline bool enabled = true;

        inline bool combo_q = true;
        inline bool combo_w = true;
        inline bool combo_e = true;
        inline bool combo_e_gapclose = true;
        inline bool combo_smart_jump = true;
        inline bool combo_r = true;
        inline int r_usage_mode = 0;
        inline bool r_cancel_killsteal = true;
        inline bool r_cancel_no_enemies = true;
        inline int r_daggers = 6;
        inline int r_min_enemies = 2;
        inline int r_overkill_hp = 35;
        inline bool dont_combo_not_killable = false;

        inline bool harass_enabled = true;
        inline int harass_mode = 0;
        inline bool harass_q_minion = true;
        inline bool lasthit_q = true;

        inline bool flee_w = true;
        inline bool flee_e = true;

        inline bool draw_only_ready = true;
        inline bool draw_q = true;
        inline bool draw_e = true;
        inline bool draw_r = true;
        inline bool draw_daggers = true;
        inline bool draw_dagger_lines = true;
        inline bool draw_dagger_times = true;
        inline bool draw_damage = true;
        inline uint32_t color_q = 0x8050D0FFu;
        inline uint32_t color_e = 0x80FF60C0u;
        inline uint32_t color_r = 0x80FF4040u;
        inline uint32_t color_dagger = 0x80FFB000u;
        inline uint32_t color_line = 0x70FFFFFFu;
    }

    game_object* player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    float game_time()
    {
        if (!g_sdk || !g_sdk->clock_facade) return 0.f;
        return g_sdk->clock_facade->get_game_time();
    }

    bool can_attempt_cast()
    {
        return game_time() - s_last_cast_time >= MIN_CAST_DELAY;
    }

    void mark_cast()
    {
        s_last_cast_time = game_time();
    }

    float hp_pct(game_object* obj)
    {
        if (!obj || obj->get_max_hp() <= 0.f) return 0.f;
        return obj->get_hp() / obj->get_max_hp() * 100.f;
    }

    bool spell_ready(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* spell = p->get_spell(slot);
        return spell && spell->get_level() > 0 && spell->get_cooldown() <= 0.f;
    }

    bool has_resource(int slot)
    {
        auto* p = player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    bool can_cast(int slot)
    {
        return menu::enabled && can_attempt_cast() && spell_ready(slot) && has_resource(slot) && sdk::common;
    }

    bool valid_enemy(game_object* obj, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead() || obj->is_zombie()) return false;
        if (!obj->is_visible() || !obj->is_targetable()) return false;
        if (obj->get_team_id() == p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(obj->get_position()) > range + obj->get_bounding_radius())
            return false;
        return true;
    }

    game_object* best_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target(); valid_enemy(forced, range))
            return forced;

        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (valid_enemy(hero, range))
                return hero;
        }
        return nullptr;
    }

    int count_enemies(const math::vector3& pos, float range)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0;
        int count = 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (valid_enemy(hero) && hero->get_position().distance(pos) <= range + hero->get_bounding_radius())
                ++count;
        }
        return count;
    }

    math::vector3 jump_point(game_object* target, float distance)
    {
        auto* p = player();
        if (!p || !target) return {};
        return target->get_position().extended(p->get_position(), distance, true);
    }

    math::vector3 dagger_jump_point(const math::vector3& dagger_pos, game_object* target)
    {
        if (!target) return dagger_pos;
        return dagger_pos.extended(target->get_position(), 170.f, true);
    }

    float q_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        auto* spell = p->get_spell(SLOT_Q);
        const int level = spell ? std::clamp(spell->get_level(), 0, 5) : 0;
        static constexpr float base[] = { 0.f, 80.f, 110.f, 140.f, 170.f, 200.f };
        const float raw = base[level] + 0.35f * p->get_ability_power();
        return sdk::damage ? sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw) : raw;
    }

    float e_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        auto* spell = p->get_spell(SLOT_E);
        const int level = spell ? std::clamp(spell->get_level(), 0, 5) : 0;
        static constexpr float base[] = { 0.f, 20.f, 35.f, 50.f, 65.f, 80.f };
        const float raw = base[level] + 0.40f * p->get_attack_damage() + 0.25f * p->get_ability_power();
        return sdk::damage ? sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw) : raw;
    }

    float r_dagger_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        auto* spell = p->get_spell(SLOT_R);
        const int level = spell ? std::clamp(spell->get_level(), 0, 3) : 0;
        static constexpr float base[] = { 0.f, 25.f, 37.5f, 50.f };
        const float bonus_ad = p->get_attack_damage() - p->get_base_attack_damage();
        const float raw = base[level] + 0.19f * p->get_attack_damage() + 0.22f * bonus_ad;
        return sdk::damage ? sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw) : raw;
    }

    float passive_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        static constexpr float base[] = {
            0.f, 68.f, 72.f, 77.f, 82.f, 89.f, 96.f, 103.f, 112.f, 121.f,
            131.f, 142.f, 154.f, 166.f, 180.f, 194.f, 208.f, 224.f, 240.f
        };
        const int level = std::clamp(p->get_level(), 1, 18);
        float ap_ratio = 0.65f;
        if (level >= 16) ap_ratio = 0.95f;
        else if (level >= 11) ap_ratio = 0.85f;
        else if (level >= 6) ap_ratio = 0.75f;

        const float bonus_ad = p->get_attack_damage() - p->get_base_attack_damage();
        const float raw = base[level] + bonus_ad * 0.60f + p->get_ability_power() * ap_ratio;
        return sdk::damage ? sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw) : raw;
    }

    float aa_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        return sdk::damage ? sdk::damage->get_aa_damage(p, target) : p->get_attack_damage();
    }

    float combo_damage(game_object* target)
    {
        if (!target) return 0.f;
        float damage = passive_damage(target) * 2.f + aa_damage(target);
        if (spell_ready(SLOT_Q)) damage += q_damage(target);
        if (spell_ready(SLOT_E)) damage += e_damage(target) * 2.f;
        if (spell_ready(SLOT_R)) damage += r_dagger_damage(target) * static_cast<float>(menu::r_daggers);
        return damage;
    }

    float effective_hp(game_object* target)
    {
        if (!target) return FLT_MAX;
        return target->get_hp() + target->get_all_shield() + target->get_magical_shield();
    }

    bool cast_q(game_object* target)
    {
        if (!target || !can_cast(SLOT_Q) || !valid_enemy(target, Q_RANGE)) return false;
        const bool ok = sdk::common->cast_spell(SLOT_Q, target);
        if (ok) mark_cast();
        return ok;
    }

    bool cast_w()
    {
        if (!can_cast(SLOT_W)) return false;
        const bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) mark_cast();
        return ok;
    }

    bool cast_e(const math::vector3& pos)
    {
        if (!can_cast(SLOT_E)) return false;
        const bool ok = sdk::common->cast_spell(SLOT_E, pos);
        if (ok) mark_cast();
        return ok;
    }

    bool cast_e(game_object* target)
    {
        if (!target || !can_cast(SLOT_E) || !valid_enemy(target, E_RANGE)) return false;
        return cast_e(target->get_position());
    }

    bool cast_r()
    {
        if (!can_cast(SLOT_R)) return false;
        const bool ok = sdk::common->cast_spell(SLOT_R);
        if (ok)
        {
            mark_cast();
            s_casting_r = true;
        }
        return ok;
    }

    void prune_daggers()
    {
        const float now = game_time();
        for (auto it = s_daggers.begin(); it != s_daggers.end();)
        {
            auto* obj = g_sdk && g_sdk->object_manager ? g_sdk->object_manager->get_object_by_network_id(it->first) : nullptr;
            if (now > it->second.end_time || !obj || !obj->is_valid())
                it = s_daggers.erase(it);
            else
                ++it;
        }
    }

    dagger_info* best_dagger_for(game_object* target, float max_player_range = E_DAGGER_RANGE)
    {
        auto* p = player();
        if (!p || !target) return nullptr;

        dagger_info* best = nullptr;
        int best_hits = 0;
        float best_dist = FLT_MAX;
        for (auto& [id, dagger] : s_daggers)
        {
            (void)id;
            const float player_dist = p->get_position().distance(dagger.position);
            if (player_dist > max_player_range) continue;

            const int hits = count_enemies(dagger.position, DAGGER_DAMAGE_RADIUS);
            if (target->get_position().distance(dagger.position) > DAGGER_DAMAGE_RADIUS && hits < 2)
                continue;

            const float target_dist = target->get_position().distance(dagger.position);
            if (hits > best_hits || (hits == best_hits && target_dist < best_dist))
            {
                best = &dagger;
                best_hits = hits;
                best_dist = target_dist;
            }
        }
        return best;
    }

    bool try_e_dagger(game_object* target)
    {
        if (!menu::combo_e || !target) return false;
        auto* dagger = best_dagger_for(target);
        if (!dagger) return false;
        return cast_e(dagger_jump_point(dagger->position, target));
    }

    bool try_e_target(game_object* target)
    {
        if (!menu::combo_e || !target || !valid_enemy(target, E_RANGE)) return false;
        return cast_e(jump_point(target, -target->get_bounding_radius()));
    }

    bool should_skip_combo(game_object* target)
    {
        if (!menu::dont_combo_not_killable || !target) return false;
        return spell_ready(SLOT_Q) && spell_ready(SLOT_W) && spell_ready(SLOT_E)
            && combo_damage(target) < effective_hp(target);
    }

    bool can_cancel_r_for_kill(game_object* target)
    {
        if (!target || !menu::r_cancel_killsteal) return false;
        if (spell_ready(SLOT_E) && e_damage(target) + q_damage(target) - 50.f >= effective_hp(target))
            return true;
        if (spell_ready(SLOT_Q) && q_damage(target) - 50.f >= effective_hp(target))
            return true;
        return false;
    }

    void set_r_channeling(bool channeling)
    {
        s_casting_r = channeling;
    }

    void killsteal_while_r()
    {
        if (!s_casting_r || !menu::r_cancel_killsteal) return;
        auto* target = best_target(E_DAGGER_RANGE);
        if (!target || !can_cancel_r_for_kill(target)) return;

        set_r_channeling(false);
        if (spell_ready(SLOT_E) && try_e_dagger(target)) return;
        if (spell_ready(SLOT_E) && try_e_target(target)) return;
        if (spell_ready(SLOT_Q)) cast_q(target);
    }

    void dagger_gapclose()
    {
        auto* p = player();
        if (!p || !menu::combo_e_gapclose || !spell_ready(SLOT_E) || s_casting_r || p->get_level() < 13) return;
        if (count_enemies(p->get_position(), E_DAGGER_RANGE) > 0) return;

        game_object* best_enemy = nullptr;
        dagger_info* best_dagger = nullptr;
        float best_damage = 0.f;

        for (auto& [id, dagger] : s_daggers)
        {
            (void)id;
            if (p->get_position().distance(dagger.position) > E_DAGGER_RANGE) continue;
            if (count_enemies(dagger.position, E_DAGGER_RANGE) == 0) continue;

            for (auto* enemy : g_sdk->object_manager->get_heroes())
            {
                if (!valid_enemy(enemy) || enemy->get_position().distance(dagger.position) > E_DAGGER_RANGE) continue;
                const float damage = combo_damage(enemy);
                if (damage >= effective_hp(enemy) && damage > best_damage)
                {
                    best_enemy = enemy;
                    best_dagger = &dagger;
                    best_damage = damage;
                }
            }
        }

        if (best_enemy && best_dagger)
            cast_e(dagger_jump_point(best_dagger->position, best_enemy));
    }

    void combo()
    {
        if (s_casting_r) return;

        dagger_gapclose();

        auto* target = best_target(1250.f);
        if (!target || should_skip_combo(target)) return;

        if (menu::combo_smart_jump && spell_ready(SLOT_E) && try_e_dagger(target))
            return;

        const float dist = player()->get_position().distance(target->get_position());

        if (menu::combo_q && spell_ready(SLOT_Q) && dist <= Q_RANGE && cast_q(target))
            return;

        if (menu::combo_e && spell_ready(SLOT_E) && dist <= E_RANGE)
        {
            if (try_e_dagger(target)) return;
            if (try_e_target(target)) return;
        }

        if (menu::combo_w && spell_ready(SLOT_W) && dist <= W_RANGE && hp_pct(target) > 7.f && cast_w())
            return;

        if (!menu::combo_r || !spell_ready(SLOT_R))
            return;

        auto* r_target = best_target(spell_ready(SLOT_E) ? E_RANGE : R_RANGE);
        if (!r_target) return;

        const int enemies_in_r = count_enemies(player()->get_position(), 525.f);
        const bool killable = r_dagger_damage(r_target) * static_cast<float>(menu::r_daggers)
            + passive_damage(r_target) + e_damage(r_target) + q_damage(r_target) >= effective_hp(r_target);

        if (menu::r_usage_mode == 0 && enemies_in_r < menu::r_min_enemies && !killable)
            return;

        if (spell_ready(SLOT_E) && player()->get_position().distance(r_target->get_position()) > R_RANGE)
            try_e_target(r_target);

        if (count_enemies(player()->get_position(), 525.f) == 1
            && q_damage(r_target) + e_damage(r_target) + aa_damage(r_target) > effective_hp(r_target)
            && hp_pct(player()) > static_cast<float>(menu::r_overkill_hp))
        {
            return;
        }

        if (hp_pct(r_target) > 10.f)
            cast_r();
    }

    void harass()
    {
        if (!menu::harass_enabled || s_casting_r) return;
        auto* target = best_target(Q_RANGE);
        if (!target) return;

        if (menu::harass_mode == 0)
        {
            if (menu::combo_q) cast_q(target);
            return;
        }

        if (spell_ready(SLOT_Q) && spell_ready(SLOT_W) && spell_ready(SLOT_E))
        {
            if (cast_e(target))
                cast_w();
        }
        else if (spell_ready(SLOT_Q) && spell_ready(SLOT_E))
        {
            if (cast_q(target))
                cast_e(jump_point(target, -target->get_bounding_radius()));
        }
    }

    void lasthit()
    {
        if (!menu::lasthit_q || !spell_ready(SLOT_Q) || !can_cast(SLOT_Q)) return;
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (!minion || !minion->is_lane_minion() || !valid_enemy(minion, Q_RANGE)) continue;
            if (p->get_position().distance(minion->get_position()) <= p->get_attack_range() + 200.f) continue;

            float hp = minion->get_hp() + minion->get_all_shield();
            if (sdk::health_prediction)
            {
                const float pred = sdk::health_prediction->get_predicted_health(minion, 0.35f);
                if (pred > 0.f) hp = std::min(hp, pred + minion->get_all_shield());
            }

            if (q_damage(minion) >= hp || q_damage(minion) + aa_damage(minion) >= hp)
            {
                cast_q(minion);
                return;
            }
        }
    }

    void flee()
    {
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->hud_manager || s_casting_r) return;

        if (menu::flee_w && spell_ready(SLOT_W))
        {
            if (cast_w()) return;
        }

        if (!menu::flee_e || !spell_ready(SLOT_E) || !can_cast(SLOT_E)) return;

        const auto cursor = g_sdk->hud_manager->get_cursor_position();
        game_object* best = nullptr;
        float best_cursor_dist = FLT_MAX;

        auto consider = [&](game_object* obj) {
            if (!obj || !obj->is_valid() || obj->is_dead() || !obj->is_targetable()) return;
            if (obj->get_network_id() == p->get_network_id()) return;
            if (obj->get_team_id() != p->get_team_id() && !obj->is_minion()) return;
            const float dist = p->get_position().distance(obj->get_position());
            if (dist < 150.f || dist > E_RANGE) return;
            const float cursor_dist = obj->get_position().distance(cursor);
            if (cursor_dist < best_cursor_dist)
            {
                best_cursor_dist = cursor_dist;
                best = obj;
            }
        };

        for (auto* minion : g_sdk->object_manager->get_minions())
            consider(minion);
        for (auto* hero : g_sdk->object_manager->get_heroes())
            consider(hero);

        if (best)
            cast_e(best->get_position());
    }

    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("katarina", "MeowAIO - Katarina");
        if (!s_menu) return;

        if (auto* q_menu = s_menu->add_sub_category("q_bouncing_blade", "Q - Bouncing Blade"))
        {
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            q_menu->add_checkbox("harass", "Use in Harass", menu::harass_enabled,
                [](bool v) { menu::harass_enabled = v; });
            q_menu->add_checkbox("harass_minion", "Use on Minions in Harass", menu::harass_q_minion,
                [](bool v) { menu::harass_q_minion = v; });
            q_menu->add_checkbox("lasthit", "Use in Last Hit", menu::lasthit_q,
                [](bool v) { menu::lasthit_q = v; });
            q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });
        }

        if (auto* w_menu = s_menu->add_sub_category("w_preparation", "W - Preparation"))
        {
            w_menu->add_checkbox("combo", "Use in Combo", menu::combo_w,
                [](bool v) { menu::combo_w = v; });
            w_menu->add_checkbox("flee", "Use in Flee", menu::flee_w,
                [](bool v) { menu::flee_w = v; });
            w_menu->add_checkbox("draw_daggers", "Draw Daggers Range", menu::draw_daggers,
                [](bool v) { menu::draw_daggers = v; });
            w_menu->add_checkbox("draw_lines", "Draw Dagger Lines", menu::draw_dagger_lines,
                [](bool v) { menu::draw_dagger_lines = v; });
            w_menu->add_checkbox("draw_times", "Draw Dagger Timers", menu::draw_dagger_times,
                [](bool v) { menu::draw_dagger_times = v; });
        }

        if (auto* e_menu = s_menu->add_sub_category("e_shunpo", "E - Shunpo"))
        {
            e_menu->add_checkbox("combo", "Use in Combo", menu::combo_e,
                [](bool v) { menu::combo_e = v; });
            e_menu->add_checkbox("dagger_gapclose", "Jump to Daggers for Gapclose", menu::combo_e_gapclose,
                [](bool v) { menu::combo_e_gapclose = v; });
            e_menu->add_checkbox("advanced_jump", "Advanced Jump Logic", menu::combo_smart_jump,
                [](bool v) { menu::combo_smart_jump = v; });
            e_menu->add_checkbox("flee", "Use in Flee", menu::flee_e,
                [](bool v) { menu::flee_e = v; });
            e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e,
                [](bool v) { menu::draw_e = v; });
        }

        if (auto* r_menu = s_menu->add_sub_category("r_death_lotus", "R - Death Lotus"))
        {
            r_menu->add_checkbox("combo", "Use in Combo", menu::combo_r,
                [](bool v) { menu::combo_r = v; });
            r_menu->add_combo("mode", "Usage Mode", { "Killable or X Enemies", "Always" }, menu::r_usage_mode,
                [](int v) { menu::r_usage_mode = v; });
            r_menu->add_checkbox("cancel_killable", "Cancel if Enemy Killable", menu::r_cancel_killsteal,
                [](bool v) { menu::r_cancel_killsteal = v; });
            r_menu->add_checkbox("cancel_empty", "Cancel if No Enemies", menu::r_cancel_no_enemies,
                [](bool v) { menu::r_cancel_no_enemies = v; });
            r_menu->add_slider_int("daggers", "Daggers in Damage Calc", 3, 12, 1, menu::r_daggers,
                [](int v) { menu::r_daggers = v; });
            r_menu->add_slider_int("min_enemies", "Min Enemies", 2, 5, 1, menu::r_min_enemies,
                [](int v) { menu::r_min_enemies = v; });
            r_menu->add_slider_int("overkill", "Ignore Overkill Under HP %", 10, 100, 5, menu::r_overkill_hp,
                [](int v) { menu::r_overkill_hp = v; });
            r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
        }

        if (auto* harass_menu = s_menu->add_sub_category("harass", "Harass"))
        {
            harass_menu->add_combo("mode", "Usage Mode", { "Only Q", "Slow Harass" }, menu::harass_mode,
                [](int v) { menu::harass_mode = v; });
        }

        if (auto* misc_menu = s_menu->add_sub_category("misc", "Misc"))
        {
            misc_menu->add_checkbox("dont_combo", "Don't Combo When Not Killable", menu::dont_combo_not_killable,
                [](bool v) { menu::dont_combo_not_killable = v; });
        }

        if (auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings"))
        {
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_checkbox("damage", "Combo Damage Indicator", menu::draw_damage,
                [](bool v) { menu::draw_damage = v; });
            draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
                [](uint32_t v) { menu::color_q = v; });
            draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
                [](uint32_t v) { menu::color_e = v; });
            draw_menu->add_colorpicker("col_r", "R Color", menu::color_r,
                [](uint32_t v) { menu::color_r = v; });
            draw_menu->add_colorpicker("col_dagger", "Daggers Color", menu::color_dagger,
                [](uint32_t v) { menu::color_dagger = v; });
            draw_menu->add_colorpicker("col_line", "Dagger Line Color", menu::color_line,
                [](uint32_t v) { menu::color_line = v; });
        }
    }
}

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Katarina") return;

    setup_menu();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update, reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present, reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world, reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::create_object, reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object, reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain, reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss, reinterpret_cast<void*>(on_buff_loss));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack, reinterpret_cast<void*>(on_before_attack));
        sdk::orbwalker->register_callback(orb_sdk::event_type::before_move, reinterpret_cast<void*>(on_before_move));
    }

    g_sdk->log_console("[MeowAIO] Katarina loaded MEOW!");
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update, reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::present, reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world, reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->unregister_callback(event_manager::event::create_object, reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::delete_object, reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain, reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss, reinterpret_cast<void*>(on_buff_loss));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack, reinterpret_cast<void*>(on_before_attack));
        sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_move, reinterpret_cast<void*>(on_before_move));
    }

    s_menu = nullptr;
    s_daggers.clear();
    s_casting_r = false;
    s_last_cast_time = 0.f;
}

void __fastcall on_update()
{
    if (!menu::enabled || !g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = player();
    if (!p || p->is_dead()) return;

    prune_daggers();

    if (s_casting_r)
    {
        if (menu::r_cancel_no_enemies && count_enemies(p->get_position(), 575.f) == 0)
            set_r_channeling(false);
        killsteal_while_r();
        return;
    }

    if (!sdk::orbwalker) return;
    if (sdk::orbwalker->combo()) combo();
    else if (sdk::orbwalker->harass()) harass();
    else if (sdk::orbwalker->lasthit()) lasthit();
    else if (sdk::orbwalker->flee()) flee();
}

void __fastcall on_draw()
{
    if (!g_sdk || !g_sdk->renderer) return;
    auto* p = player();
    if (!p || p->is_dead()) return;

    if (menu::draw_dagger_times)
    {
        const float now = game_time();
        for (const auto& [id, dagger] : s_daggers)
        {
            (void)id;
            auto screen = g_sdk->renderer->world_to_screen(dagger.position + math::vector3(0.f, 100.f, 0.f));
            if (!screen.is_on_screen(g_sdk->renderer->get_window_width(), g_sdk->renderer->get_window_height()))
                continue;

            char text[32] {};
            std::snprintf(text, sizeof(text), "[%.1fs]", std::clamp(dagger.end_time - now, 0.f, DAGGER_LIFETIME));
            g_sdk->renderer->add_text(text, 14.f, screen, 1, 0xFFFFFFFFu);
        }
    }

    if (menu::draw_damage && sdk::damage)
    {
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (valid_enemy(hero, 1400.f))
                g_sdk->renderer->add_damage_indicator(hero, combo_damage(hero));
        }
    }
}

void __fastcall on_draw_world()
{
    if (!g_sdk || !g_sdk->renderer) return;
    auto* p = player();
    if (!p || p->is_dead()) return;

    auto draw_circle = [](const math::vector3& pos, float range, uint32_t color) {
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, range, 2.f, color);
        else
            g_sdk->renderer->add_circle_3d(pos, range, 1.5f, color);
    };

    if (menu::draw_q && (!menu::draw_only_ready || spell_ready(SLOT_Q))) draw_circle(p->get_position(), Q_RANGE, menu::color_q);
    if (menu::draw_e && (!menu::draw_only_ready || spell_ready(SLOT_E))) draw_circle(p->get_position(), E_RANGE, menu::color_e);
    if (menu::draw_r && (!menu::draw_only_ready || spell_ready(SLOT_R))) draw_circle(p->get_position(), R_RANGE, menu::color_r);

    for (const auto& [id, dagger] : s_daggers)
    {
        (void)id;
        if (menu::draw_daggers)
            draw_circle(dagger.position, DAGGER_DRAW_RADIUS, menu::color_dagger);

        if (menu::draw_dagger_lines)
        {
            auto start = g_sdk->renderer->world_to_screen(p->get_position());
            auto end = g_sdk->renderer->world_to_screen(dagger.position);
            if (start.is_on_screen(g_sdk->renderer->get_window_width(), g_sdk->renderer->get_window_height())
                && end.is_on_screen(g_sdk->renderer->get_window_width(), g_sdk->renderer->get_window_height()))
            {
                g_sdk->renderer->add_line_2d(start, end, 2.f, menu::color_line);
            }
        }
    }
}

void __fastcall on_create_object(game_object* obj)
{
    auto* p = player();
    if (!obj || !obj->is_valid() || !p) return;

    const std::string name = obj->get_name();
    if (name.find("W_Indicator_Ally") == std::string::npos
        && name.find("Dagger_Ground_Indicator") == std::string::npos)
        return;

    if (p->get_position().distance(obj->get_position()) > 3500.f)
        return;

    const float now = game_time();
    s_daggers[obj->get_network_id()] = { obj->get_position(), now, now + DAGGER_LIFETIME };
}

void __fastcall on_delete_object(game_object* obj)
{
    if (!obj) return;
    s_daggers.erase(obj->get_network_id());
}

void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
{
    auto* p = player();
    if (!p || !obj || !buff || obj->get_network_id() != p->get_network_id()) return;

    std::string name = buff->get_name();
    if (name == "katarinarsound")
        set_r_channeling(true);
}

void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
{
    auto* p = player();
    if (!p || !obj || !buff || obj->get_network_id() != p->get_network_id()) return;

    std::string name = buff->get_name();
    if (name == "katarinarsound")
        set_r_channeling(false);
}

bool __stdcall on_before_attack(orb_sdk::event_data* data)
{
    (void)data;
    if (!s_casting_r) return true;
    auto* p = player();
    if (!p) return true;
    return menu::r_cancel_no_enemies && count_enemies(p->get_position(), R_RANGE) == 0;
}

bool __stdcall on_before_move(orb_sdk::event_data* data)
{
    (void)data;
    if (!s_casting_r) return true;
    auto* p = player();
    if (!p) return true;
    return menu::r_cancel_no_enemies && count_enemies(p->get_position(), R_RANGE) == 0;
}
}
