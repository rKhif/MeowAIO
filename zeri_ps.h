#pragma once

// =====================================================================
// Zeri (Blue/PaidScripts source -> VEN SDK port)
// Ported from the legacy "Features::Champions::Zeri" implementation so
// that it can be tested side-by-side with the original VEN-native zeri
// module. Loaded as champion "Zeri" from MeowV1 main.cpp.
// =====================================================================

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace zeri_ps
{
    // Spell slots
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // Hitchance presets (used to map UI -> pred_sdk::hitchance)
    constexpr int HC_VERY_LOW   = 0;
    constexpr int HC_LOW        = 1;
    constexpr int HC_MEDIUM     = 2;
    constexpr int HC_HIGH       = 3;
    constexpr int HC_VERY_HIGH  = 4;
    constexpr int HC_DASH       = 5;

    // Spell data table (ported 1:1 from BlueZeri Initialize())
    namespace spell_data
    {
        // Q -- Burst Fire (linear, gets extra range from bonus AA range)
        constexpr float q_base_range   = 750.f;
        constexpr float q_radius       = 40.f;
        constexpr float q_speed_normal = 2600.f;
        constexpr float q_speed_r      = 3400.f;  // during R (Lightning Crash)
        constexpr float q_chain_range  = 650.f;   // bounce range while R active

        // W -- Ultrashock Laser (missile)
        constexpr float w_range        = 1200.f;
        constexpr float w_radius       = 40.f;
        constexpr float w_speed        = 2500.f;
        constexpr float w_cast_delay   = 0.10f;
        constexpr float w_extra_collision_radius = 20.f;

        // W -- Beam phase 2 (post-wall)
        constexpr float wbeam_range    = 1500.f;
        constexpr float wbeam_radius   = 100.f;

        // E -- Spark Surge
        constexpr float e_range        = 300.f;

        // R -- Lightning Crash
        constexpr float r_range        = 825.f;
        constexpr float r_cast_delay   = 0.25f;

        // Buff hashes (taken from the existing VEN-native zeri.h so we
        // know they're correct against the live game-data tables)
        constexpr uint32_t buff_q_passive_ready = 0x016E7450u; // ZeriQPassiveReady (laser ready)
        constexpr uint32_t buff_e_lightning     = 0xF00B5F96u; // ZeriESpecialRounds
        constexpr uint32_t buff_r_active        = 0x9A37D6E5u; // ZeriR
        constexpr uint32_t buff_r_stacks        = 0x34AE3384u; // ZeriRStacks
    }

    // ---- Menu state ---------------------------------------------------
    namespace menu
    {
        // Combo
        inline bool combo_q                = true;
        inline bool combo_q_ignore_col     = false;
        inline bool chain_logic            = false;
        inline bool chain_only_keep_alive  = false;
        inline bool combo_w_missile        = true;
        inline bool combo_w_beam           = true;
        inline bool only_out_of_range_w    = true;
        inline bool combo_e_killable       = true;
        inline bool combo_e_collision      = true;
        inline int  e_mode                 = 1;        // 0=Cursor,1=Safe
        inline bool only_reset             = false;
        inline bool collision_ignore_reset = true;
        inline int  enemy_limit_e          = 1;
        inline bool combo_r_killable       = true;
        inline bool combo_r_count          = true;
        inline int  r_count                = 2;

        // Harass
        inline bool harass_q               = true;
        inline bool harass_w_missile       = true;
        inline bool harass_w_beam          = true;

        // Lasthit / lane / jungle
        inline bool lasthit_q              = true;
        inline bool lane_q                 = true;
        inline bool lane_q_plant           = true;
        inline bool jungle_q               = true;

        // Misc
        inline bool ks_w                   = true;
        inline bool semi_auto_w_active     = false;
        inline std::string semi_auto_w_key;
        inline bool block_r                = true;
        inline bool jump_key_active        = false;
        inline std::string jump_key;
        inline bool save_mana_e            = false;

        // Hitchance
        inline int hitchance_w             = HC_MEDIUM;
        inline int hitchance_beam          = HC_HIGH;

        // Draw
        inline bool draw_q                 = true;
        inline bool draw_w                 = true;
        inline bool draw_e                 = false;
        inline bool draw_r                 = true;
        inline bool draw_chain_lightning   = true;
        inline bool draw_wall_jumps        = true;
        inline bool draw_jumps_only_close  = true;
        inline bool draw_wall_end_pos      = true;

        inline uint32_t color_q            = 0xFFB0C4B1u;
    }

    // ---- Lifecycle ----------------------------------------------------
    void load();
    void unload();
}
