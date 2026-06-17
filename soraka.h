#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <string>

namespace soraka
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    namespace spell_data
    {
        // Q - Starcall: circular skillshot with a slow-moving "puff".
        constexpr float q_range  = 800.f;
        constexpr float q_radius = 230.f;
        constexpr float q_delay  = 0.25f;
        constexpr float q_speed  = 1000.f;

        // W - Astral Infusion: targeted heal on ally.
        constexpr float w_range  = 550.f;

        // E - Equinox: circular delayed AOE that snares after a wind-up.
        constexpr float e_range        = 925.f;
        constexpr float e_radius       = 260.f;
        constexpr float e_delay        = 0.25f;
        constexpr float e_speed        = 15000.f;
        // Time between Equinox landing and the snare resolving (used for "guaranteed snare").
        constexpr float e_snare_delay  = 1.75f;

        // R - Wish: self-cast global heal.
        constexpr float r_global_radius = 20000.f;
    }

    namespace menu
    {
        // --- Q / Starcall ---
        inline bool combo_q       = true;
        inline bool harass_q      = true;
        inline int  q_min_mana    = 35;
        inline int  q_hitchance   = 70;   // pred_sdk::hitchance::high
        inline bool draw_q        = true;

        // --- W / Astral Infusion ---
        inline bool w_toggle      = true;
        inline bool w_only_combo  = false;
        inline bool w_passive     = true;
        inline int  w_min_mana    = 33;
        inline bool draw_w        = true;

        // --- E / Equinox ---
        inline bool combo_e       = false;
        inline bool harass_e      = true;
        inline bool e_auto_cancel = true;
        inline bool e_auto_snare  = true;
        inline bool e_auto_gap    = true;
        inline int  e_min_mana    = 25;
        inline bool draw_e        = false;

        // --- R / Wish ---
        inline bool r_toggle      = true;
        inline int  r_min_allies  = 1;

        // --- Drawings ---
        inline bool draw_only_ready = true;
        inline uint32_t color_q = 0x80E49B0Fu;
        inline uint32_t color_w = 0x8050C878u;
        inline uint32_t color_e = 0x80E0B0FFu;
        inline uint32_t color_w_heal_outer = 0xFF50C878u;
        inline uint32_t color_w_heal_inner = 0x5050C878u;
    }

    void load();
    void unload();
}
