#include "aurora.hpp"

#include "evade_sdk.hpp"
#include "infotab_sdk.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aurora
{
namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    constexpr float Q_RANGE = 925.f;
    constexpr float Q_RADIUS = 80.f;
    constexpr float Q_DELAY = 0.25f;
    constexpr float Q_SPEED = 1800.f;

    constexpr float E_RANGE = 525.f;
    constexpr float E_RADIUS = 85.f;
    constexpr float E_DELAY = 0.25f;
    constexpr float E_SPEED = 1600.f;

    constexpr float R_RANGE = 850.f;
    constexpr float R_RADIUS = 100.f;
    constexpr float R_DELAY = 0.25f;
    constexpr float R_SPEED = 1400.f;

    constexpr float CAST_THROTTLE = 0.075f;

    menu_category* s_menu = nullptr;
    bool s_loaded = false;

    namespace menu
    {
        inline int  q_range = static_cast<int>(Q_RANGE);
        inline bool q_combo = true;
        inline int  q_combo_mode = 1; // 0 = off, 1 = auto-reset, 2 = combo only
        inline bool q_harass = true;
        inline int  q_harass_mode = 1;
        inline bool q_lane = false;
        inline bool q_lane_fast_only = false;
        inline int  q_lane_hits = 3;
        inline bool q_jungle = true;
        inline bool q_ks = true;
        inline int  q_hitchance = pred_sdk::hitchance::high;

        inline bool w_gapclose = true;
        inline bool w_flee = false;

        inline int  e_range = static_cast<int>(E_RANGE);
        inline bool e_combo = true;
        inline bool e_harass = false;
        inline bool e_lane = false;
        inline bool e_lane_fast_only = false;
        inline int  e_lane_hits = 3;
        inline bool e_jungle = false;
        inline bool e_flee = false;
        inline bool e_gapclose = false;
        inline int  e_hitchance = pred_sdk::hitchance::high;

        inline bool r_combo = true;
        inline bool r_flee = false;
        inline bool r_ks = false;
        inline int  r_min_enemies = 2;
        inline int  r_range_override = 800;
        inline int  r_hitchance = pred_sdk::hitchance::high;

        inline bool q_first_active = false;
        inline std::string q_first_key;
        inline bool semi_r_active = false;
        inline std::string semi_r_key;
        inline int  semi_r_min = 1;

        inline bool draw_q = true;
        inline bool draw_e = true;
        inline bool draw_r = true;
        inline bool draw_only_ready = true;
        inline uint32_t color_q = 0x80FFAA40u;
        inline uint32_t color_e = 0x8040C8FFu;
        inline uint32_t color_r = 0x80FF4040u;
    }

    float last_cast_time[6] = {};

    game_object* player()
    {
        return g_sdk && g_sdk->object_manager ? g_sdk->object_manager->get_local_player() : nullptr;
    }

    float now()
    {
        return g_sdk && g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    bool is_enemy(game_object* obj)
    {
        auto* p = player();
        return p && obj && obj->is_valid() && !obj->is_dead() && obj->is_visible() && obj->is_targetable()
            && obj->get_team_id() != p->get_team_id();
    }

    bool is_enemy_minion(game_object* obj)
    {
        return is_enemy(obj) && obj->is_lane_minion();
    }

    bool spell_ready(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    bool can_attempt_cast(int slot)
    {
        if (slot < 0 || slot >= 6) return false;
        const float t = now();
        return t - last_cast_time[slot] >= CAST_THROTTLE;
    }

    void mark_cast(int slot)
    {
        if (slot < 0 || slot >= 6) return;
        last_cast_time[slot] = now();
    }

    std::string active_spell_name(game_object* obj)
    {
        if (!obj) return {};
        auto* ac = obj->get_active_spell_cast();
        if (!ac) return {};
        auto* sc = ac->get_spell_cast();
        if (!sc) return {};
        auto* sd = sc->get_spell_data();
        if (!sd) return {};
        auto* static_data = sd->get_static_data();
        if (!static_data) return {};
        char* name = static_data->get_name();
        return name ? name : "";
    }

    bool is_casting_spell(const char* needle)
    {
        auto* p = player();
        if (!p || !needle) return false;
        const std::string active = active_spell_name(p);
        return !active.empty() && active.find(needle) != std::string::npos;
    }

    bool safe_to_cast()
    {
        auto* p = player();
        if (!p || p->is_dead()) return false;
        auto* ac = p->get_active_spell_cast();
        return !ac || ac->get_cast_end_time() <= now();
    }

    bool cast_position(int slot, const math::vector3& pos)
    {
        auto* p = player();
        if (!p || !sdk::common || !can_attempt_cast(slot) || !spell_ready(slot) || !safe_to_cast())
            return false;
        const bool ok = sdk::common->cast_spell(slot, pos);
        if (ok) mark_cast(slot);
        return ok;
    }

    bool cast_self(int slot)
    {
        auto* p = player();
        if (!p || !sdk::common || !can_attempt_cast(slot) || !spell_ready(slot) || !safe_to_cast())
            return false;
        const bool ok = sdk::common->cast_spell(slot);
        if (ok) mark_cast(slot);
        return ok;
    }

    bool cast_target(int slot, game_object* target)
    {
        auto* p = player();
        if (!p || !sdk::common || !target || !can_attempt_cast(slot) || !spell_ready(slot) || !safe_to_cast())
            return false;
        const bool ok = sdk::common->cast_spell(slot, target);
        if (ok) mark_cast(slot);
        return ok;
    }

    pred_sdk::spell_data make_q_data(int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type = pred_sdk::spell_type::linear;
        spell.targetting_type = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot = SLOT_Q;
        spell.range = static_cast<float>(menu::q_range);
        spell.radius = Q_RADIUS;
        spell.delay = Q_DELAY;
        spell.projectile_speed = Q_SPEED;
        spell.expected_hitchance = hitchance;
        return spell;
    }

    pred_sdk::spell_data make_e_data(int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type = pred_sdk::spell_type::linear;
        spell.targetting_type = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot = SLOT_E;
        spell.range = static_cast<float>(menu::e_range);
        spell.radius = E_RADIUS;
        spell.delay = E_DELAY;
        spell.projectile_speed = E_SPEED;
        spell.expected_hitchance = hitchance;
        return spell;
    }

    pred_sdk::spell_data make_r_data(int hitchance, float range_override)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type = pred_sdk::spell_type::linear;
        spell.targetting_type = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot = SLOT_R;
        spell.range = range_override > 0.f ? range_override : R_RANGE;
        spell.radius = R_RADIUS;
        spell.delay = R_DELAY;
        spell.projectile_speed = R_SPEED;
        spell.expected_hitchance = hitchance;
        return spell;
    }

    pred_sdk::pred_data predict_target(game_object* target, const pred_sdk::spell_data& spell)
    {
        pred_sdk::pred_data pred{};
        if (!sdk::prediction || !target) return pred;
        try
        {
            pred = sdk::prediction->predict(target, spell);
        }
        catch (...)
        {
            pred.is_valid = false;
        }
        return pred;
    }

    game_object* hero_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        return sdk::target_selector->get_hero_target([&](game_object* obj) {
            auto* p = player();
            if (!p || !obj || !is_enemy(obj)) return false;
            return p->get_position().distance(obj->get_position()) <= range;
        });
    }

    game_object* monster_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        return sdk::target_selector->get_monster_target([&](game_object* obj) {
            auto* p = player();
            if (!p || !obj || !is_enemy(obj) || !obj->is_monster()) return false;
            return p->get_position().distance(obj->get_position()) <= range;
        });
    }

    bool cast_predicted_q(game_object* target)
    {
        if (!target) return false;
        auto pred = predict_target(target, make_q_data(menu::q_hitchance));
        if (!pred.is_valid || pred.hitchance < menu::q_hitchance) return false;
        return cast_position(SLOT_Q, pred.cast_position);
    }

    bool cast_predicted_e(game_object* target, int hitchance = menu::e_hitchance)
    {
        auto pred = predict_target(target, make_e_data(hitchance));
        if (!pred.is_valid || pred.hitchance < hitchance) return false;
        return cast_position(SLOT_E, pred.cast_position);
    }

    bool cast_predicted_r(game_object* target)
    {
        auto pred = predict_target(target, make_r_data(menu::r_hitchance, static_cast<float>(menu::r_range_override)));
        if (!pred.is_valid || pred.hitchance < menu::r_hitchance) return false;
        return cast_position(SLOT_R, pred.cast_position);
    }

    bool enemy_is_dashing(game_object* obj)
    {
        return obj && obj->is_dashing();
    }

    int count_enemies_in_range(float range)
    {
        auto* p = player();
        if (!p) return 0;
        int count = 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_enemy(hero)) continue;
            if (p->get_position().distance(hero->get_position()) <= range)
                ++count;
        }
        return count;
    }

    bool q_auto_reset_enabled()
    {
        return menu::q_combo && menu::q_combo_mode == 1;
    }

    bool q_combo_tick_enabled()
    {
        return menu::q_combo && menu::q_combo_mode == 2;
    }

    bool q_harass_reset_enabled()
    {
        return menu::q_harass && menu::q_harass_mode == 1;
    }

    bool q_harass_tick_enabled()
    {
        return menu::q_harass && menu::q_harass_mode == 2;
    }

    void combo()
    {
        const float target_range = static_cast<float>(std::max(menu::q_range, std::max(menu::e_range, menu::r_range_override)));
        auto* t = hero_target(target_range);
        if (!t) return;

        if (menu::q_first_active)
        {
            if (q_combo_tick_enabled() && cast_predicted_q(t)) return;
            if (menu::e_combo && cast_predicted_e(t)) return;
        }
        else
        {
            if (menu::e_combo && cast_predicted_e(t)) return;
            if (q_combo_tick_enabled() && cast_predicted_q(t)) return;
        }

        if (menu::r_combo && count_enemies_in_range(static_cast<float>(menu::r_range_override)) >= menu::r_min_enemies)
        {
            cast_predicted_r(t);
        }
    }

    void harass()
    {
        const float target_range = static_cast<float>(std::max(menu::q_range, std::max(menu::e_range, menu::r_range_override)));
        auto* t = hero_target(target_range);
        if (!t) return;

        if (menu::q_first_active)
        {
            if (q_harass_tick_enabled() && cast_predicted_q(t)) return;
            if (menu::e_harass && cast_predicted_e(t)) return;
        }
        else
        {
            if (menu::e_harass && cast_predicted_e(t)) return;
            if (q_harass_tick_enabled() && cast_predicted_q(t)) return;
        }
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        const bool in_clear = sdk::orbwalker->clear();
        const bool in_fast = sdk::orbwalker->fast_clear();
        const bool in_lasthit = sdk::orbwalker->lasthit();
        if (!in_clear && !in_fast && !in_lasthit) return;

        auto* p = player();
        if (!p) return;

        const bool fast_clear = in_fast;
        const int q_farm_hits = in_lasthit ? 1 : menu::q_lane_hits;
        const int e_farm_hits = in_lasthit ? 1 : menu::e_lane_hits;
        const auto farm_flags = static_cast<spell_farm_flag>(
            spell_farm_flag::when_attack_not_ready | (in_lasthit ? spell_farm_flag::lasthit : spell_farm_flag::none));

        if (menu::q_lane && (!menu::q_lane_fast_only || fast_clear))
        {
            pred_sdk::spell_data spell = make_q_data(pred_sdk::hitchance::any);
            try
            {
                auto pred = sdk::orbwalker->spell_farm(spell, q_farm_hits,
                    farm_flags,
                    dmg_sdk::damage_type::magical);
                if (pred.is_valid && cast_position(SLOT_Q, pred.cast_position))
                    return;
            }
            catch (...)
            {
            }
        }

        if (menu::e_lane && (!menu::e_lane_fast_only || fast_clear))
        {
            pred_sdk::spell_data spell = make_e_data(pred_sdk::hitchance::any);
            try
            {
                auto pred = sdk::orbwalker->spell_farm(spell, e_farm_hits,
                    farm_flags,
                    dmg_sdk::damage_type::magical);
                if (pred.is_valid && cast_position(SLOT_E, pred.cast_position))
                    return;
            }
            catch (...)
            {
            }
        }

        if (menu::q_jungle)
        {
            if (auto* monster = monster_target(static_cast<float>(menu::q_range)))
            {
                if (cast_predicted_q(monster)) return;
            }
        }

        if (menu::e_jungle)
        {
            if (auto* monster = monster_target(static_cast<float>(menu::e_range)))
            {
                if (cast_predicted_e(monster)) return;
            }
        }
    }

    void flee()
    {
        auto* p = player();
        if (!p) return;

        const auto self = p->get_position();
        if (menu::w_flee && spell_ready(SLOT_W))
        {
            cast_self(SLOT_W);
        }

        if (menu::e_flee && spell_ready(SLOT_E))
        {
            const auto cursor = g_sdk->hud_manager ? g_sdk->hud_manager->get_cursor_position() : self;
            math::vector3 dir{ cursor.x - self.x, 0.f, cursor.z - self.z };
            const float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            if (len > 0.001f)
            {
                dir.x /= len;
                dir.z /= len;
                const math::vector3 dest{
                    self.x + dir.x * static_cast<float>(menu::e_range),
                    self.y,
                    self.z + dir.z * static_cast<float>(menu::e_range)
                };
                cast_position(SLOT_E, dest);
            }
        }

        if (menu::r_flee && spell_ready(SLOT_R))
        {
            if (auto* t = hero_target(static_cast<float>(menu::r_range_override)))
                cast_predicted_r(t);
        }
    }

    void killsteal()
    {
        auto* p = player();
        if (!p || !sdk::damage) return;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_enemy(hero)) continue;

            if (menu::q_ks && spell_ready(SLOT_Q) && p->get_position().distance(hero->get_position()) <= menu::q_range)
            {
                const float dmg = sdk::damage->get_spell_damage(p, hero, SLOT_Q);
                if (hero->get_hp() <= dmg && cast_predicted_q(hero))
                    return;
            }

            if (menu::r_ks && spell_ready(SLOT_R) && p->get_position().distance(hero->get_position()) <= menu::r_range_override)
            {
                const float dmg = sdk::damage->get_spell_damage(p, hero, SLOT_R);
                if (hero->get_hp() <= dmg && cast_predicted_r(hero))
                    return;
            }
        }
    }

    void anti_gap()
    {
        auto* p = player();
        if (!p) return;

        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_enemy(hero) || !enemy_is_dashing(hero)) continue;

            const float dist = p->get_position().distance(hero->get_position());
            if (menu::q_combo && spell_ready(SLOT_Q) && dist <= menu::q_range && cast_predicted_q(hero))
                return;

            if (menu::w_gapclose && spell_ready(SLOT_W))
            {
                cast_self(SLOT_W);
                return;
            }

            if (menu::e_gapclose && spell_ready(SLOT_E) && dist <= menu::e_range && cast_predicted_e(hero))
                return;
        }
    }

    void semi_manual_r()
    {
        if (!menu::semi_r_active || !spell_ready(SLOT_R)) return;
        auto* t = hero_target(static_cast<float>(menu::r_range_override));
        if (!t) return;
        if (count_enemies_in_range(static_cast<float>(menu::r_range_override)) < menu::semi_r_min) return;
        cast_predicted_r(t);
    }

    void draw_circle(float range, uint32_t color)
    {
        auto* p = player();
        if (!p || !g_sdk) return;

        if (sdk::shaders)
        {
            sdk::shaders->draw_world_circle(p->get_position(), range, 2.f, color);
            return;
        }

        if (g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(p->get_position(), range, 1.f, color);
    }
}

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Aurora")
        return;

    if (!g_sdk || !g_sdk->menu_manager)
        return;

    s_menu = g_sdk->menu_manager->add_category("aurora", "MeowAIO - Aurora");
    if (!s_menu)
        return;

    auto* q_menu = s_menu->add_sub_category("q_spell", "Q - Twofold Hex");
    q_menu->add_checkbox("combo", "Use in Combo", menu::q_combo, [](bool v) { menu::q_combo = v; });
    q_menu->add_combo("combo_mode", "Q Combo Mode",
        { "Off", "AutoReset", "ComboOnly" }, menu::q_combo_mode,
        [](int v) { menu::q_combo_mode = v; });
    q_menu->add_checkbox("harass", "Use in Harass", menu::q_harass, [](bool v) { menu::q_harass = v; });
    q_menu->add_combo("harass_mode", "Q Harass Mode",
        { "Off", "AutoReset", "ComboOnly" }, menu::q_harass_mode,
        [](int v) { menu::q_harass_mode = v; });
    q_menu->add_checkbox("lane", "Use in Lane", menu::q_lane, [](bool v) { menu::q_lane = v; });
    q_menu->add_checkbox("lane_fast", "  Only Fast Clear", menu::q_lane_fast_only,
        [](bool v) { menu::q_lane_fast_only = v; });
    q_menu->add_slider_int("lane_hits", "  Min Minions", 1, 6, 1, menu::q_lane_hits,
        [](int v) { menu::q_lane_hits = v; });
    q_menu->add_checkbox("jungle", "Use in Jungle", menu::q_jungle, [](bool v) { menu::q_jungle = v; });
    q_menu->add_checkbox("killsteal", "Use to KillSteal", menu::q_ks, [](bool v) { menu::q_ks = v; });
    q_menu->add_hotkey("q_first", "Q First", 'Z', false, false,
        [](std::string* key, bool pressed) {
            menu::q_first_active = pressed;
            if (key) menu::q_first_key = *key;
        });
    q_menu->add_slider_int("range", "Q Range", 100, 1500, 25, menu::q_range,
        [](int v) { menu::q_range = v; });
    q_menu->add_slider_int("hitchance", "Q Hitchance", 0, 100, 5, menu::q_hitchance,
        [](int v) { menu::q_hitchance = v; });

    auto* w_menu = s_menu->add_sub_category("w_spell", "W - Across the Veil");
    w_menu->add_checkbox("gapclose", "Anti-Gap W", menu::w_gapclose, [](bool v) { menu::w_gapclose = v; });
    w_menu->add_checkbox("flee", "Use in Flee", menu::w_flee, [](bool v) { menu::w_flee = v; });

    auto* e_menu = s_menu->add_sub_category("e_spell", "E - The Weirding");
    e_menu->add_checkbox("combo", "Use in Combo", menu::e_combo, [](bool v) { menu::e_combo = v; });
    e_menu->add_checkbox("harass", "Use in Harass", menu::e_harass, [](bool v) { menu::e_harass = v; });
    e_menu->add_checkbox("lane", "Use in Lane", menu::e_lane, [](bool v) { menu::e_lane = v; });
    e_menu->add_checkbox("lane_fast", "  Only Fast Clear", menu::e_lane_fast_only,
        [](bool v) { menu::e_lane_fast_only = v; });
    e_menu->add_slider_int("lane_hits", "  Min Minions", 1, 6, 1, menu::e_lane_hits,
        [](int v) { menu::e_lane_hits = v; });
    e_menu->add_checkbox("jungle", "Use in Jungle", menu::e_jungle, [](bool v) { menu::e_jungle = v; });
    e_menu->add_checkbox("flee", "Use in Flee", menu::e_flee, [](bool v) { menu::e_flee = v; });
    e_menu->add_checkbox("gapclose", "Gapclose E", menu::e_gapclose, [](bool v) { menu::e_gapclose = v; });
    e_menu->add_slider_int("range", "E Range", 100, 1500, 25, menu::e_range,
        [](int v) { menu::e_range = v; });
    e_menu->add_slider_int("hitchance", "E Hitchance", 0, 100, 5, menu::e_hitchance,
        [](int v) { menu::e_hitchance = v; });

    auto* r_menu = s_menu->add_sub_category("r_spell", "R - Between Worlds");
    r_menu->add_checkbox("combo", "Use in Combo", menu::r_combo, [](bool v) { menu::r_combo = v; });
    r_menu->add_checkbox("flee", "Use in Flee", menu::r_flee, [](bool v) { menu::r_flee = v; });
    r_menu->add_checkbox("killsteal", "Use to KillSteal", menu::r_ks, [](bool v) { menu::r_ks = v; });
    r_menu->add_slider_int("min_enemies", "  Min Enemies", 1, 5, 1, menu::r_min_enemies,
        [](int v) { menu::r_min_enemies = v; });
    r_menu->add_slider_int("range", "  Range Override", 100, 1500, 25, menu::r_range_override,
        [](int v) { menu::r_range_override = v; });
    r_menu->add_hotkey("semi_r", "Semi-Manual R", 'T', false, false,
        [](std::string* key, bool pressed) {
            menu::semi_r_active = pressed;
            if (key) menu::semi_r_key = *key;
        });
    r_menu->add_slider_int("semi_min", "  Semi Min Enemies", 1, 5, 1, menu::semi_r_min,
        [](int v) { menu::semi_r_min = v; });
    r_menu->add_slider_int("hitchance", "R Hitchance", 0, 100, 5, menu::r_hitchance,
        [](int v) { menu::r_hitchance = v; });

    auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings");
    draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
        [](bool v) { menu::draw_only_ready = v; });
    draw_menu->add_checkbox("draw_q", "Draw Q Range", menu::draw_q, [](bool v) { menu::draw_q = v; });
    draw_menu->add_checkbox("draw_e", "Draw E Range", menu::draw_e, [](bool v) { menu::draw_e = v; });
    draw_menu->add_checkbox("draw_r", "Draw R Range", menu::draw_r, [](bool v) { menu::draw_r = v; });
    draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q, [](uint32_t c) { menu::color_q = c; });
    draw_menu->add_colorpicker("color_e", "E Color", menu::color_e, [](uint32_t c) { menu::color_e = c; });
    draw_menu->add_colorpicker("color_r", "R Color", menu::color_r, [](uint32_t c) { menu::color_r = c; });

    if (g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->register_callback(orb_sdk::event_type::after_attack,
            reinterpret_cast<void*>(on_after_attack));
    }

    s_loaded = true;
    g_sdk->log_console("[MeowAIO] Aurora loaded MEOW!");
}

void unload()
{
    if (s_loaded && g_sdk)
    {
        if (g_sdk->event_manager)
        {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw_world));
        }

        if (sdk::orbwalker)
        {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::after_attack,
                reinterpret_cast<void*>(on_after_attack));
        }
    }

    s_menu = nullptr;
    s_loaded = false;
    std::fill(last_cast_time, last_cast_time + 6, 0.f);
    menu::q_first_active = false;
    menu::semi_r_active = false;
    menu::q_first_key.clear();
    menu::semi_r_key.clear();
}

void __fastcall on_update()
{
    if (!g_sdk || !player() || player()->is_dead()) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    if (sdk::orbwalker)
    {
        if (sdk::orbwalker->combo())
        {
            combo();
        }
        else if (sdk::orbwalker->harass())
        {
            harass();
        }
        else if (sdk::orbwalker->clear() || sdk::orbwalker->fast_clear() || sdk::orbwalker->lasthit())
        {
            clear();
        }
        else if (sdk::orbwalker->flee())
        {
            flee();
        }
    }

    killsteal();
    anti_gap();
    semi_manual_r();
}

void __fastcall on_draw_world()
{
    if (!g_sdk || !g_sdk->renderer) return;
    auto* p = player();
    if (!p || p->is_dead()) return;

    if (menu::draw_q && (!menu::draw_only_ready || spell_ready(SLOT_Q)))
        draw_circle(static_cast<float>(menu::q_range), menu::color_q);
    if (menu::draw_e && (!menu::draw_only_ready || spell_ready(SLOT_E)))
        draw_circle(static_cast<float>(menu::e_range), menu::color_e);
    if (menu::draw_r && (!menu::draw_only_ready || spell_ready(SLOT_R)))
        draw_circle(static_cast<float>(menu::r_range_override), menu::color_r);
}

bool __stdcall on_after_attack(orb_sdk::event_data* data)
{
    if (!data || !data->target || !g_sdk || !sdk::orbwalker) return true;
    auto* p = player();
    if (!p || p->is_dead()) return true;
    if (!is_enemy(data->target)) return true;

    const bool in_combo = sdk::orbwalker->combo();
    const bool in_harass = sdk::orbwalker->harass();
    if (!in_combo && !in_harass) return true;

    if (in_combo && !q_auto_reset_enabled()) return true;
    if (in_harass && !q_harass_reset_enabled()) return true;
    if (is_casting_spell("AuroraQ")) return true;

    if (p->get_position().distance(data->target->get_position()) > menu::q_range) return true;
    if (!spell_ready(SLOT_Q)) return true;

    cast_predicted_q(data->target);
    return true;
}

} // namespace aurora
