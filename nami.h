#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <string>

namespace nami
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    namespace spell_data
    {
        // Q - Aqua Prison: targeted-circular (delayed bubble, no projectile)
        constexpr float q_range  = 850.f;
        constexpr float q_radius = 200.f;
        constexpr float q_delay  = 0.25f;
        constexpr float q_travel = 0.726f;

        // W - Ebb and Flow: targeted, fast missile
        constexpr float w_range = 725.f;
        constexpr float w_speed = 2500.f;
        constexpr float w_delay = 0.25f;
        constexpr float w_bounce_range = 725.f;

        // E - Tidecaller's Blessing: targeted ally buff
        constexpr float e_range = 800.f;

        // R - Tidal Wave: linear, slow & wide
        constexpr float r_range  = 2750.f;
        constexpr float r_radius = 250.f;
        constexpr float r_delay  = 0.5f;
        constexpr float r_speed  = 850.f;

        // W heal scaling (per rank index)
        constexpr float heal_base[7]    = { 0.f, 55.f,  75.f,  95.f,  115.f, 135.f, 155.f };
        constexpr float heal_min[7]     = { 0.f, 38.5f, 52.5f, 66.5f, 80.5f, 94.5f, 108.5f };
        constexpr float heal_ap_ratio   = 0.25f;
        constexpr float heal_min_ratio  = 0.175f;
    }

    namespace menu
    {
        // --- Q / Aqua Prison ---
        inline bool combo_use_q       = true;
        inline bool combo_q_only_slow = false;
        inline bool combo_q_gapclose  = true;
        inline int  combo_q_hitchance = 70;   // pred_sdk::hitchance::high

        // --- W / Ebb and Flow ---
        inline bool combo_use_w   = true;
        inline bool heal_use_w    = true;
        inline int  heal_mana_pct = 60;
        inline int  heal_hp_pct   = 30;
        inline bool w_bounce      = true;

        // --- E / Tidecaller's Blessing ---
        inline bool e_use_on_ally   = true;
        inline bool e_only_in_combo = true;
        inline bool e_on_q          = false;
        inline bool e_on_w          = false;
        inline bool e_on_e          = false;
        inline bool e_on_r          = true;
        inline bool e_on_aa         = true;
        inline int  e_humanizer_ms  = 250;

        // --- R / Tidal Wave ---
        inline bool combo_use_r      = true;
        inline int  r_min_hits       = 3;
        inline int  r_hitchance      = 50;   // pred_sdk::hitchance::medium
        inline bool semi_r_active    = false;
        inline std::string semi_r_key = "T";
        inline float semi_r_radius   = 500.f;

        // --- Drawings ---
        inline bool draw_q_range = true;
        inline bool draw_w_range = true;
        inline bool draw_e_range = true;
        inline bool draw_r_range = false;
        inline bool draw_only_ready = true;

        inline uint32_t color_q = 0x8090E8FFu;
        inline uint32_t color_w = 0x8080FFC0u;
        inline uint32_t color_e = 0x80FFD27Au;
        inline uint32_t color_r = 0x80B070FFu;
    }

    void load();
    void unload();
}
