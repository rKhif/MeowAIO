#include "teemo.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace teemo
{
namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    constexpr float Q_RANGE = 680.f;
    // R cast range scales with rank (index by spell level 1..3).
    constexpr float R_RANGES[4] = { 0.f, 600.f, 750.f, 900.f };
    constexpr float SHROOM_RADIUS = 100.f;
    constexpr float SHROOM_SPACING = 150.f;
    constexpr float SPOT_RECAST_DELAY = 2.f;
    constexpr float GAP_CLOSE_MIN_DASH = 400.f;

    const char* MUSHROOM_CHAR_NAME = "TeemoMushroom";

    // Pre-defined shroom placement spots, grouped by importance (from the original source).
    const std::vector<math::vector3> HP_SHROOMS = {
        { 4224.f, 48.f, 11820.f }, { 3876.f, -67.f, 9490.f }, { 4828.f, -20.f, 10610.f },
        { 6440.f, -20.f, 9196.f }, { 8434.f, -35.f, 5692.f }, { 9968.f, -46.f, 6616.f },
        { 9822.f, -71.f, 4698.f }, { 11046.f, -45.f, 5574.f }, { 10520.f, 53.f, 3042.f },
        { 9822.f, -71.f, 4698.f }
    };
    const std::vector<math::vector3> MP_SHROOMS = {
        { 3238.f, -56.f, 11858.f }, { 3106.f, -67.f, 10806.f }, { 3774.f, -70.f, 10956.f },
        { 4818.f, -20.f, 8084.f }, { 8434.f, 38.f, 8304.f }, { 6558.f, -70.f, 8364.f },
        { 8422.f, -71.f, 6442.f }, { 11004.f, -71.f, 4002.f }, { 11818.f, -68.f, 3994.f },
        { 11834.f, 38.f, 2678.f }
    };
    const std::vector<math::vector3> LP_SHROOMS = {
        { 3440.f, 53.f, 14012.f }, { 2612.f, 53.f, 13610.f }, { 2204.f, 53.f, 13424.f },
        { 1690.f, 53.f, 12978.f }, { 1238.f, 53.f, 12518.f }, { 1174.f, 53.f, 12094.f },
        { 2356.f, 37.f, 11738.f }, { 3708.f, 54.f, 12852.f }, { 5698.f, 53.f, 12754.f },
        { 5980.f, 56.f, 11142.f }, { 6690.f, 54.f, 9674.f }, { 5628.f, -71.f, 9166.f },
        { 3338.f, 52.f, 8450.f }, { 2270.f, 53.f, 5600.f }, { 4730.f, 52.f, 6028.f },
        { 6052.f, 52.f, 7242.f }, { 6712.f, 54.f, 7414.f }, { 7550.f, 53.f, 8102.f },
        { 7386.f, 53.f, 8712.f }, { 8768.f, 56.f, 9854.f }, { 9334.f, 54.f, 11448.f },
        { 11934.f, 52.f, 9400.f }, { 10072.f, 50.6f, 8870.f }, { 8800.f, 53.f, 7410.f },
        { 8256.f, 53.f, 7310.f }, { 7286.f, 53.f, 6678.f }, { 7396.f, 52.f, 6202.f },
        { 8596.f, 47.f, 5226.f }, { 9462.f, -71.f, 5652.f }, { 11452.f, 51.f, 6416.f },
        { 12206.f, -8.f, 3364.f }, { 12958.f, 51.3f, 3582.f }, { 13876.f, 51.3f, 3488.f },
        { 13604.f, 53.f, 2798.f }, { 13000.f, 52.f, 1992.f }, { 13564.f, 52.f, 2244.f },
        { 12706.f, 54.f, 1338.f }, { 12152.f, 54.f, 1278.f }, { 11070.f, 50.f, 912.f },
        { 10904.f, 51.f, 2020.f }, { 9258.f, 57.f, 2166.f }, { 8848.f, 53.3f, 3760.f },
        { 5490.f, 51.3f, 3468.f }
    };

    namespace menu
    {
        inline bool harass_laugh = false;

        inline bool combo_q = true;
        inline bool harass_q = true;
        inline bool prevent_ks_q = false;
        inline int  q_mode = 0; // 0 = On After AA, 1 = Always

        inline bool combo_w = true;
        inline bool w_after_aa = true;
        inline bool w_gap_close = true;

        inline bool combo_r = true;
        inline bool r_on_stasis = false;
        inline bool r_on_cc = true;
        inline bool r_on_gap = true;
        inline bool r_specific_spots = true;
        inline bool spots_high = true;
        inline bool spots_med = true;
        inline bool spots_low = false;

        inline bool draw_q = true;
        inline bool draw_r = true;
        inline bool draw_spots = true;
        inline uint32_t color_q = 0x80FFAA40u;
        inline uint32_t color_r = 0x80FF6060u;
    }

    menu_category* s_menu = nullptr;

    std::unordered_map<uint32_t, bool> s_use_q_on;
    std::unordered_map<uint32_t, math::vector3> s_shrooms;
    float s_last_spot_cast = 0.f;

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
        if (!p) return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0;
    }

    bool spell_ready(int slot)
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
        return s && s->get_level() > 0 && p->get_par() >= p->get_spell_mana_cost(slot);
    }

    uint32_t r_charges()
    {
        auto* p = player();
        if (!p) return 0;
        auto* s = p->get_spell(SLOT_R);
        return s ? s->get_charges() : 0;
    }

    int r_level()
    {
        auto* p = player();
        if (!p) return 0;
        auto* s = p->get_spell(SLOT_R);
        return s ? std::clamp(s->get_level(), 0, 3) : 0;
    }

    float r_range()
    {
        return R_RANGES[r_level()];
    }

    bool is_valid_enemy(game_object* obj, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead()) return false;
        if (!obj->is_targetable() || !obj->is_visible()) return false;
        if (obj->get_team_id() == p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(obj->get_position()) > range + obj->get_bounding_radius())
            return false;
        return true;
    }

    game_object* target(float range)
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

    float effective_hp(game_object* obj)
    {
        if (!obj) return FLT_MAX;
        return obj->get_hp() + obj->get_all_shield() + obj->get_magical_shield();
    }

    float q_damage(game_object* obj)
    {
        auto* p = player();
        if (!p || !obj || !sdk::damage) return 0.f;
        return sdk::damage->get_spell_damage(p, obj, SLOT_Q);
    }

    bool q_allowed_on(game_object* obj)
    {
        if (!obj) return false;
        const auto it = s_use_q_on.find(obj->get_network_id());
        if (it != s_use_q_on.end() && !it->second)
            return false;
        if (menu::prevent_ks_q && q_damage(obj) >= effective_hp(obj))
            return false;
        return true;
    }

    bool is_immobile(game_object* obj)
    {
        if (!obj) return false;
        return obj->has_buff_of_type(buff_type::stun)
            || obj->has_buff_of_type(buff_type::snare)
            || obj->has_buff_of_type(buff_type::taunt)
            || obj->has_buff_of_type(buff_type::charm)
            || obj->has_buff_of_type(buff_type::fear)
            || obj->has_buff_of_type(buff_type::suppression);
    }

    bool is_in_stasis(game_object* obj)
    {
        if (!obj) return false;
        if (obj->has_buff_of_type(buff_type::invulnerability)) return true;
        for (auto* buff : obj->get_buffs())
        {
            if (!buff || !buff->is_active()) continue;
            const auto name = lowercase(buff->get_name());
            if (name.find("stasis") != std::string::npos || name.find("zhonya") != std::string::npos)
                return true;
        }
        return false;
    }

    bool is_teemo_shroom(game_object* obj)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid()) return false;
        if (obj->get_team_id() != p->get_team_id()) return false;
        return obj->get_char_name() == MUSHROOM_CHAR_NAME;
    }

    bool has_shroom_near(const math::vector3& pos, float radius = SHROOM_SPACING)
    {
        for (const auto& [id, shroom_pos] : s_shrooms)
        {
            if (pos.distance(shroom_pos) <= radius)
                return true;
        }
        return false;
    }

    bool cast_q(game_object* obj)
    {
        auto* p = player();
        if (!p || !obj || !sdk::common) return false;
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return false;
        if (!is_valid_enemy(obj, Q_RANGE)) return false;
        if (!q_allowed_on(obj)) return false;
        return sdk::common->cast_spell(SLOT_Q, obj);
    }

    bool cast_w_self()
    {
        if (!sdk::common || !spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        return sdk::common->cast_spell(SLOT_W);
    }

    bool cast_r_at(const math::vector3& pos)
    {
        if (!sdk::common || !spell_ready(SLOT_R) || !has_mana(SLOT_R)) return false;
        return sdk::common->cast_spell(SLOT_R, pos);
    }

    void try_place_spot_group(const std::vector<math::vector3>& spots)
    {
        auto* p = player();
        if (!p) return;

        const auto player_pos = p->get_position();
        const float range = r_range();

        for (const auto& spot : spots)
        {
            if (spot.distance(player_pos) > range) continue;
            if (has_shroom_near(spot)) continue;

            if (cast_r_at(spot))
            {
                s_last_spot_cast = game_time();
                return;
            }
        }
    }

    void handle_shroom_spots()
    {
        if (!menu::r_specific_spots) return;
        if (!spell_ready(SLOT_R) || r_charges() <= 1) return;
        if (game_time() - s_last_spot_cast <= SPOT_RECAST_DELAY) return;

        // Only auto-place when idle (no orbwalker mode active), matching the original.
        if (sdk::orbwalker && (sdk::orbwalker->combo() || sdk::orbwalker->harass()
            || sdk::orbwalker->clear() || sdk::orbwalker->fast_clear() || sdk::orbwalker->lasthit()))
            return;

        if (menu::spots_high) { try_place_spot_group(HP_SHROOMS); if (game_time() - s_last_spot_cast <= 0.05f) return; }
        if (menu::spots_med)  { try_place_spot_group(MP_SHROOMS); if (game_time() - s_last_spot_cast <= 0.05f) return; }
        if (menu::spots_low)  { try_place_spot_group(LP_SHROOMS); }
    }

    void handle_auto_r()
    {
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return;
        if (!menu::r_on_stasis && !menu::r_on_cc) return;

        const float range = r_range();
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero, range)) continue;

            const auto pos = hero->get_position();
            if (has_shroom_near(pos)) continue;

            if (menu::r_on_stasis && is_in_stasis(hero))
            {
                if (cast_r_at(pos)) return;
            }
            if (menu::r_on_cc && is_immobile(hero))
            {
                if (cast_r_at(pos)) return;
            }
        }
    }

    void combo()
    {
        if (menu::q_mode != 1) return; // After-AA handled in on_basic_attack
        if (!menu::combo_q) return;

        if (auto* t = target(Q_RANGE))
            cast_q(t);
    }

    void harass()
    {
        if (menu::q_mode != 1) return;
        if (!menu::harass_q) return;

        if (auto* t = target(Q_RANGE))
            cast_q(t);
    }

    void draw_circle(const math::vector3& pos, float radius, uint32_t clr)
    {
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, radius, 2.f, color(clr));
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, radius, 2.f, clr);
    }

    void draw_spot_group(const std::vector<math::vector3>& spots, uint32_t clr)
    {
        if (!g_sdk || !g_sdk->renderer) return;
        for (const auto& spot : spots)
        {
            const auto screen = g_sdk->renderer->world_to_screen(spot);
            if (screen.x <= 1.f && screen.y <= 1.f) continue;
            if (has_shroom_near(spot)) continue;
            draw_circle(spot, SHROOM_RADIUS, clr);
        }
    }

    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("teemo", "MeowAIO - Teemo");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Teemo: failed to create menu category");
            return;
        }

        if (auto* p_menu = s_menu->add_sub_category("passive", "Passive - Guerrilla Warfare"))
        {
            p_menu->add_checkbox("laugh", "Laugh after AA", menu::harass_laugh,
                [](bool v) { menu::harass_laugh = v; });
        }

        if (auto* q_menu = s_menu->add_sub_category("q_blinding_dart", "Q - Blinding Dart"))
        {
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            q_menu->add_checkbox("harass", "Use in Harass", menu::harass_q,
                [](bool v) { menu::harass_q = v; });
            q_menu->add_checkbox("prevent_ks", "Prevent Kill Steal with Q", menu::prevent_ks_q,
                [](bool v) { menu::prevent_ks_q = v; });
            q_menu->add_combo("mode", "Usage", { "On After AA", "Always" }, menu::q_mode,
                [](int v) { menu::q_mode = v; });
            q_menu->add_checkbox("draw", "Draw Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });

            if (auto* use_menu = q_menu->add_sub_category("use_on", "Use On..."))
            {
                auto* p = player();
                if (p && g_sdk && g_sdk->object_manager)
                {
                    for (auto* hero : g_sdk->object_manager->get_heroes())
                    {
                        if (!hero || !hero->is_valid()) continue;
                        if (hero->get_team_id() == p->get_team_id()) continue;

                        const uint32_t id = hero->get_network_id();
                        s_use_q_on[id] = true;
                        const std::string key = "useq_" + std::to_string(id);
                        use_menu->add_checkbox(key, hero->get_char_name(), true,
                            [id](bool v) { s_use_q_on[id] = v; });
                    }
                }
            }
        }

        if (auto* w_menu = s_menu->add_sub_category("w_move_quick", "W - Move Quick"))
        {
            w_menu->add_checkbox("combo", "Use in Combo", menu::combo_w,
                [](bool v) { menu::combo_w = v; });
            w_menu->add_checkbox("after_aa", "Use After AA", menu::w_after_aa,
                [](bool v) { menu::w_after_aa = v; });
            w_menu->add_checkbox("gap_close", "Use on Gap Close", menu::w_gap_close,
                [](bool v) { menu::w_gap_close = v; });
        }

        if (auto* r_menu = s_menu->add_sub_category("r_noxious_trap", "R - Noxious Trap"))
        {
            r_menu->add_checkbox("combo", "Use in Combo", menu::combo_r,
                [](bool v) { menu::combo_r = v; });
            r_menu->add_checkbox("on_stasis", "Place on Stasis", menu::r_on_stasis,
                [](bool v) { menu::r_on_stasis = v; });
            r_menu->add_checkbox("on_cc", "Place on CC", menu::r_on_cc,
                [](bool v) { menu::r_on_cc = v; });
            r_menu->add_checkbox("on_gap", "Place on Gap Close", menu::r_on_gap,
                [](bool v) { menu::r_on_gap = v; });
            r_menu->add_separator();
            r_menu->add_checkbox("spots", "Auto Place on Specific Spots", menu::r_specific_spots,
                [](bool v) { menu::r_specific_spots = v; });
            r_menu->add_checkbox("spots_high", "Spots: High Importance", menu::spots_high,
                [](bool v) { menu::spots_high = v; });
            r_menu->add_checkbox("spots_med", "Spots: Medium Importance", menu::spots_med,
                [](bool v) { menu::spots_med = v; });
            r_menu->add_checkbox("spots_low", "Spots: Low Importance", menu::spots_low,
                [](bool v) { menu::spots_low = v; });
            r_menu->add_separator();
            r_menu->add_checkbox("draw", "Draw Range", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
            r_menu->add_checkbox("draw_spots", "Draw Shroom Spots", menu::draw_spots,
                [](bool v) { menu::draw_spots = v; });
        }
    }

    void initialize_preplaced_shrooms()
    {
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        s_shrooms.clear();
        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (is_teemo_shroom(minion))
                s_shrooms[minion->get_network_id()] = minion->get_position();
        }
    }
}

void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = player();
    if (!p || p->is_dead()) return;

    handle_auto_r();
    handle_shroom_spots();

    if (!sdk::orbwalker) return;
    if (sdk::orbwalker->combo())       combo();
    else if (sdk::orbwalker->harass()) harass();
}

void __fastcall on_basic_attack(game_object* attacker, game_object* target_obj, spell_cast* /*cast*/)
{
    auto* p = player();
    if (!p || !attacker || attacker->get_network_id() != p->get_network_id()) return;
    if (!sdk::orbwalker) return;

    const bool combo_mode = sdk::orbwalker->combo();
    const bool harass_mode = sdk::orbwalker->harass();

    if (menu::harass_laugh && (combo_mode || harass_mode) && g_sdk->game_gui)
        g_sdk->game_gui->send_emote(emote_type::laugh);

    if (!target_obj || !target_obj->is_hero()) return;
    if (!combo_mode && !harass_mode) return;

    // Q weave after auto attack.
    if (menu::q_mode == 0)
    {
        const bool entry = combo_mode ? menu::combo_q : menu::harass_q;
        if (entry && is_valid_enemy(target_obj, Q_RANGE))
        {
            if (cast_q(target_obj))
                return;
        }
    }

    // W after auto attack (combo only).
    if (combo_mode && menu::combo_w && menu::w_after_aa)
        cast_w_self();
}

void __fastcall on_new_path(game_object* obj, bool is_dash, float /*dash_speed*/)
{
    auto* p = player();
    if (!p || !obj || !is_dash) return;
    if (!obj->is_hero() || obj->is_dead()) return;
    if (obj->get_team_id() == p->get_team_id()) return;

    const auto path = obj->get_path();
    if (path.size() < 2) return;

    const auto start_pos = path.front();
    const auto end_pos = path.back();
    const auto player_pos = p->get_position();

    if (end_pos.distance(player_pos) > r_range()) return;
    if (start_pos.distance(end_pos) <= GAP_CLOSE_MIN_DASH) return;

    if (menu::w_gap_close && cast_w_self())
        return;

    if (menu::r_on_gap && !has_shroom_near(end_pos))
        cast_r_at(end_pos);
}

void __fastcall on_create_object(game_object* obj)
{
    if (!is_teemo_shroom(obj)) return;
    s_shrooms[obj->get_network_id()] = obj->get_position();
}

void __fastcall on_delete_object(game_object* obj)
{
    if (!obj) return;
    s_shrooms.erase(obj->get_network_id());
}

void __fastcall on_draw()
{
}

void __fastcall on_draw_world()
{
    auto* p = player();
    if (!p || p->is_dead()) return;

    const auto pos = p->get_position();

    if (menu::draw_q && spell_learned(SLOT_Q))
        draw_circle(pos, Q_RANGE, menu::color_q);

    if (menu::draw_r && r_level() > 0)
        draw_circle(pos, r_range(), menu::color_r);

    if (menu::draw_spots)
    {
        if (menu::spots_high) draw_spot_group(HP_SHROOMS, 0x801434A4u);
        if (menu::spots_med)  draw_spot_group(MP_SHROOMS, 0x80FFC000u);
        if (menu::spots_low)  draw_spot_group(LP_SHROOMS, 0x80D2042Du);
    }
}

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Teemo")
        return;

    setup_menu();
    initialize_preplaced_shrooms();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
        g_sdk->event_manager->register_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
    }

    g_sdk->log_console("[MeowAIO] Teemo loaded MEOW!");
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
        g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
        g_sdk->event_manager->unregister_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
    }

    s_menu = nullptr;
    s_use_q_on.clear();
    s_shrooms.clear();
    s_last_spot_cast = 0.f;
}
} // namespace teemo
