#include "milio.h"

#include "champs/milio/data.h"

#include <algorithm>
#include <cfloat>

namespace milio
{
namespace menu
{
    inline bool combo_use_q = true;
    inline bool draw_q_range = true;
    inline bool draw_w_range = true;
    inline bool draw_e_range = true;
    inline bool draw_r_range = true;
    inline bool draw_only_ready = true;

    inline uint32_t color_q = 0x8090E8FFu;
    inline uint32_t color_w = 0x80B0FF80u;
    inline uint32_t color_e = 0x80FFD27Au;
    inline uint32_t color_r = 0x80FF7A7Au;
}

namespace
{
    menu_category* s_menu = nullptr;

    game_object* get_player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    bool is_spell_learned(int slot)
    {
        auto* player = get_player();
        if (!player) return false;
        auto* spell = player->get_spell(slot);
        return spell && spell->get_level() > 0;
    }

    bool is_spell_ready(int slot)
    {
        auto* player = get_player();
        if (!player) return false;
        auto* spell = player->get_spell(slot);
        return spell && spell->get_level() > 0 && spell->get_cooldown() <= 0.f;
    }

    bool has_mana_for(int slot)
    {
        auto* player = get_player();
        if (!player) return false;
        auto* spell = player->get_spell(slot);
        return spell && spell->get_level() > 0 && player->get_par() >= player->get_spell_mana_cost(slot);
    }

    bool is_valid_enemy(game_object* target, float range = FLT_MAX)
    {
        auto* player = get_player();
        if (!player || !target || !target->is_valid() || target->is_dead()) return false;
        if (!target->is_targetable() || !target->is_visible()) return false;
        if (target->get_team_id() == player->get_team_id()) return false;
        if (range < FLT_MAX)
        {
            const float reach = range + target->get_bounding_radius();
            if (player->get_position().distance(target->get_position()) > reach)
                return false;
        }
        return true;
    }

    game_object* get_target(float range)
    {
        if (!sdk::target_selector) return nullptr;

        if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_enemy(forced, range))
            return forced;

        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (is_valid_enemy(hero, range))
                return hero;
        }
        return nullptr;
    }

    math::vector3 predict_position_along_path(game_object* target, float time)
    {
        if (!target || time <= 0.f)
            return target ? target->get_position() : math::vector3{};

        const float travel_budget = std::max(0.f, target->get_move_speed() * time);
        if (travel_budget <= 1.f)
            return target->get_position();

        const auto path = target->get_path();
        if (path.empty())
            return target->get_position();

        math::vector3 current = target->get_server_position();
        float remaining = travel_budget;

        for (const auto& node : path)
        {
            const float segment = current.distance(node);
            if (segment <= 1.f)
                continue;

            if (remaining <= segment)
                return current.extended(node, remaining, false);

            remaining -= segment;
            current = node;
        }

        return current;
    }

    bool get_q_cast_position(game_object* target, math::vector3& cast_pos)
    {
        auto* player = get_player();
        if (!player || !target) return false;

        const auto player_pos = player->get_position();
        const auto target_pos = target->get_position();
        const float current_dist = player_pos.distance(target_pos);
        if (current_dist > data::Q_RANGE + target->get_bounding_radius())
            return false;

        const float first_arrival = data::Q_DELAY + current_dist / data::Q_SPEED;
        math::vector3 predicted = predict_position_along_path(target, first_arrival);
        float predicted_dist = player_pos.distance(predicted);

        if (predicted_dist > data::Q_RANGE + target->get_bounding_radius())
            predicted = player_pos.extended(predicted, data::Q_RANGE, false);

        const float final_arrival = data::Q_DELAY + std::min(predicted_dist, data::Q_RANGE) / data::Q_SPEED;
        const math::vector3 final_predicted = predict_position_along_path(target, final_arrival);

        const float allowed_error = data::Q_WIDTH * 0.5f + target->get_bounding_radius();
        if (predicted.distance(final_predicted) > allowed_error)
            return false;

        cast_pos = final_predicted;
        return player_pos.distance(cast_pos) <= data::Q_RANGE + target->get_bounding_radius();
    }

    bool try_q_combo()
    {
        if (!menu::combo_use_q) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(data::SLOT_Q) || !has_mana_for(data::SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto* target = get_target(data::Q_RANGE);
        if (!target) return false;

        math::vector3 cast_pos{};
        if (!get_q_cast_position(target, cast_pos))
            return false;

        return sdk::common->cast_spell(data::SLOT_Q, cast_pos);
    }

    void combo()
    {
        try_q_combo();
    }

    void draw_range_circle(const math::vector3& pos, float range, uint32_t color)
    {
        if (!range || !color) return;
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, range, 2.f, color);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, range, 2.f, color);
    }

    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("milio", "MeowAIO - Milio");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Milio: failed to create menu category");
            return;
        }

        if (auto* q_menu = s_menu->add_sub_category("q_ultra_mega_fire_kick", "Q - Ultra Mega Fire Kick"))
        {
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_q,
                [](bool v) { menu::combo_use_q = v; });
            q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
                [](bool v) { menu::draw_q_range = v; });
        }

        if (auto* w_menu = s_menu->add_sub_category("w_cozy_campfire", "W - Cozy Campfire"))
        {
            w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
                [](bool v) { menu::draw_w_range = v; });
        }

        if (auto* e_menu = s_menu->add_sub_category("e_warm_hugs", "E - Warm Hugs"))
        {
            e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
                [](bool v) { menu::draw_e_range = v; });
        }

        if (auto* r_menu = s_menu->add_sub_category("r_breath_of_life", "R - Breath of Life"))
        {
            r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
                [](bool v) { menu::draw_r_range = v; });
        }

        if (auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings"))
        {
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q,
                [](uint32_t c) { menu::color_q = c; });
            draw_menu->add_colorpicker("color_w", "W Color", menu::color_w,
                [](uint32_t c) { menu::color_w = c; });
            draw_menu->add_colorpicker("color_e", "E Color", menu::color_e,
                [](uint32_t c) { menu::color_e = c; });
            draw_menu->add_colorpicker("color_r", "R Color", menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
        }
    }
}

void load()
{
    auto* player = get_player();
    if (!player || player->get_char_name() != "Milio")
        return;

    setup_menu();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
    }

    g_sdk->log_console("[MeowAIO] Milio loaded MEOW!");
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

    s_menu = nullptr;
    menu::combo_use_q = true;
    menu::draw_q_range = true;
    menu::draw_w_range = true;
    menu::draw_e_range = true;
    menu::draw_r_range = true;
    menu::draw_only_ready = true;
}

void __fastcall on_update()
{
    if (!g_sdk) return;
    auto* player = get_player();
    if (!player || player->is_dead()) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    if (sdk::orbwalker && sdk::orbwalker->combo())
        combo();
}

void __fastcall on_draw()
{
}

void __fastcall on_draw_world()
{
    auto* player = get_player();
    if (!player || player->is_dead()) return;

    const auto pos = player->get_position();

    if (menu::draw_q_range && is_spell_learned(data::SLOT_Q)
        && (!menu::draw_only_ready || is_spell_ready(data::SLOT_Q)))
        draw_range_circle(pos, data::Q_RANGE, menu::color_q);

    if (menu::draw_w_range && is_spell_learned(data::SLOT_W)
        && (!menu::draw_only_ready || is_spell_ready(data::SLOT_W)))
        draw_range_circle(pos, data::W_RANGE, menu::color_w);

    if (menu::draw_e_range && is_spell_learned(data::SLOT_E)
        && (!menu::draw_only_ready || is_spell_ready(data::SLOT_E)))
        draw_range_circle(pos, data::E_RANGE, menu::color_e);

    if (menu::draw_r_range && is_spell_learned(data::SLOT_R)
        && (!menu::draw_only_ready || is_spell_ready(data::SLOT_R)))
        draw_range_circle(pos, data::R_RADIUS, menu::color_r);
}
} // namespace milio
