#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <string>

namespace thresh
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    namespace spell_data
    {
        // Q - Death Sentence: long range linear hook.
        constexpr float q_range  = 1100.f;
        constexpr float q_radius = 70.f;
        constexpr float q_delay  = 0.5f;
        constexpr float q_speed  = 1900.f;

        // W - Dark Passage: lantern toss.
        constexpr float w_range        = 950.f;
        constexpr float w_grab_range   = 1500.f;
        constexpr float w_radius       = 120.f;

        // E - Flay: short cone push/pull.
        constexpr float e_range  = 525.f;
        constexpr float e_radius = 200.f;
        constexpr float e_speed  = 2000.f;

        // R - The Box.
        constexpr float r_range = 450.f;

        // How long after Q starts flying we hold attacks for the chain.
        constexpr float q_pull_delay = 1.0f;

        // Lantern lifetime (seconds) — original timeout.
        constexpr float lantern_lifetime = 6.f;
    }

    namespace menu
    {
        // --- Q / Death Sentence ---
        inline bool combo_q       = true;
        inline int  q_hitchance   = 70;     // raw % — snapped to nearest pred bucket internally
        inline int  q_max_range   = 1100;
        inline bool draw_q        = true;

        // --- W / Dark Passage ---
        inline bool w_lantern_q_hit    = true;
        inline bool w_lantern_displace = true;
        inline bool w_lantern_gapclose = true;
        inline bool w_lantern_cc       = true;

        // --- E / Flay ---
        inline bool combo_e        = true;
        inline int  e_mode         = 0;        // 0 = Pull, 1 = Push
        inline bool e_anti_gap     = true;
        inline bool e_force_into_q = false;    // E first, then Q with relaxed hitchance during the E window
        inline bool draw_e         = true;
        inline bool draw_e_mode    = true;

        // --- R / The Box ---
        inline bool combo_r       = false;
        inline int  r_min_enemies = 2;     // require >= N enemies in R range

        // --- Drawings ---
        inline bool draw_only_ready = true;
        inline uint32_t color_q     = 0x804F7942u;  // muted green
        inline uint32_t color_e     = 0x805F9EA0u;  // muted teal
        inline uint32_t color_lantern = 0xCC228B22u;
    }

    void load();
    void unload();
}
