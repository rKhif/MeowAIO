#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <string>

namespace ziggs
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    namespace spell_data
    {
        // Q - Bouncing Bomb
        constexpr float q_range      = 850.f;    // visual/draw range (direct cast)
        constexpr float q_cast_range = 1225.f;   // true reach incl. bounce
        constexpr float q_radius     = 133.f;
        constexpr float q_speed      = 1700.f;
        constexpr float q_delay      = 0.25f;

        // W - Satchel Charge
        constexpr float w_range         = 1000.f;
        constexpr float w_radius        = 325.f;
        constexpr float w_speed         = 1750.f;
        constexpr float w_delay         = 0.25f;
        constexpr float w_push_distance = 165.5f;

        // E - Hexplosive Minefield
        constexpr float e_range       = 900.f;
        constexpr float e_radius      = 325.f;
        constexpr float e_mine_radius = 305.f;
        constexpr float e_speed       = 1550.f;
        constexpr float e_delay       = 0.25f;

        // R - Mega Inferno Bomb
        constexpr float r_range  = 5000.f;
        constexpr float r_radius = 525.f;
        constexpr float r_speed  = 2000.f;
        constexpr float r_delay  = 0.375f;
    }

    namespace menu
    {
        // Combo
        inline bool combo_q                    = true;
        inline bool combo_w                    = true;
        inline bool combo_w_only_when_e_ready  = false;
        inline int  combo_w_hp_diff_push       = 20;
        inline bool combo_e                    = true;
        inline bool combo_korean               = true;
        inline bool combo_mode_e_into_w        = false;

        // Harass
        inline bool harass_q       = true;
        inline bool harass_w       = false;
        inline bool harass_e       = true;
        inline int  harass_mana_pct = 40;

        // Lane Clear
        inline bool lane_q            = true;
        inline bool lane_e            = true;
        inline int  lane_e_min_minions = 3;
        inline int  lane_mana_pct      = 40;

        // Jungle Clear
        inline bool jungle_q = true;
        inline bool jungle_e = true;

        // Flee
        inline bool flee_w = true;

        // Misc
        inline bool auto_w_on_gapclose            = true;
        inline bool auto_e_on_gapclose            = true;
        inline bool auto_e_on_gapclose_self       = true;
        inline bool auto_w_on_channeling          = true;
        inline bool auto_w_on_destroyable_turret  = true;
        inline bool auto_r_if_killable            = true;
        inline bool auto_r_only_no_enemies_nearby = false;
        inline bool auto_r_aoe                    = false;
        inline int  auto_r_aoe_min_enemies        = 3;

        // Hitchances
        inline int q_hitchance = 70;
        inline int w_hitchance = 70;
        inline int e_hitchance = 50;

        // Hotkeys / semi-manual
        inline bool        semi_r_active      = false;
        inline std::string semi_r_key;
        inline std::string auto_r_toggle_key;
        inline uint32_t    infotab_semi_r     = 0;
        inline uint32_t    infotab_auto_r     = 0;

        // Drawings
        inline bool draw_q            = true;
        inline bool draw_w            = true;
        inline bool draw_e            = true;
        inline bool draw_r            = true;
        inline bool draw_r_killable   = true;
        inline bool draw_r_minimap    = true;
        inline bool draw_r_damage_bar = true;
        inline bool draw_only_ready   = false;

        // Colors (ARGB)
        inline uint32_t color_q            = 0xFFFFFF40u;
        inline uint32_t color_w            = 0xFFFF8040u;
        inline uint32_t color_e            = 0xFF40FFA0u;
        inline uint32_t color_r            = 0xFFFF4040u;
        inline uint32_t color_r_damage_bar = 0x90FF4040u;
    }

    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
}
