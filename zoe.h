#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace zoe
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venp db::champions::zoe
    // ============================================================================
    namespace spell_data
    {
        // ---- Passive: More Sparkles ----
        constexpr float passive_base[20] = {
            16.f, 20.f, 24.f, 28.f, 32.f, 36.f, 42.f, 48.f, 54.f, 60.f,
            66.f, 74.f, 82.f, 90.f, 100.f, 110.f, 120.f, 130.f, 140.f, 150.f
        };
        constexpr float passive_ap_ratio = 0.20f;

        // ---- Q: Paddle Star ----
        constexpr float q_level_base[20] = {
            2.f, 4.f, 6.f, 8.f, 10.f, 12.f, 14.f, 16.f, 18.f, 21.f,
            24.f, 27.f, 30.f, 34.f, 38.f, 42.f, 46.f, 50.f, 54.f, 58.f
        };
        constexpr float q_base[5]     = { 50.f, 80.f, 110.f, 140.f, 170.f };
        constexpr float q_ap_ratio    = 0.60f;

        constexpr float q1_range          = 800.f;
        constexpr float q1_speed          = 1200.f;
        constexpr float q1_radius         = 50.f;
        constexpr float q1_cast_time      = 0.25f;
        constexpr float q1_linger         = 1.0f;
        constexpr float q2_recast_delay   = 0.25f;
        constexpr float q2_range_from_zoe = 800.f;
        constexpr float q2_speed          = 2500.f;
        constexpr float q2_radius         = 70.f;
        constexpr float q_aoe_radius      = 90.f;

        // Q distance-based damage scaling
        constexpr float q_dist_min        = 800.f;
        constexpr float q_dist_max        = 2550.f;
        constexpr float q_dist_bonus      = 1.50f;

        // ---- E: Sleepy Trouble Bubble ----
        constexpr float e_base[5]         = { 70.f, 110.f, 150.f, 190.f, 230.f };
        constexpr float e_ap_ratio        = 0.45f;

        constexpr float e_base_range      = 785.f;
        constexpr float e_speed           = 1850.f;
        constexpr float e_radius          = 50.f;
        constexpr float e_delay           = 0.25f;
        constexpr float e_drowsy_dur      = 1.4f;
        constexpr float e_sleep_dur       = 2.25f;
        constexpr float e_mr_reduction    = 0.30f;

        // E wall extension
        constexpr float e_wall_scan_max   = 2500.f;
        constexpr float e_wall_scan_step  = 25.f;

        // ---- R: Portal Jump ----
        constexpr float r_range           = 575.f;
        constexpr float r_cast_time       = 0.25f;
        constexpr float r_blink_delay     = 0.40f;
        constexpr float r_stay_time       = 1.0f;
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q          = true;
        inline bool combo_use_e          = true;
        inline bool e_priority_before_q1 = true;

        // --- Harass ---
        inline bool  harass_use_q     = true;
        inline bool  harass_use_e     = false;
        inline int   harass_mana_pct  = 30;

        // --- Farm / Clear ---
        inline bool  farm_use_q       = false;
        inline int   farm_mana_pct    = 40;

        // --- Hitchance ---
        inline int   q2_hitchance     = pred_sdk::hitchance::high;
        inline int   e_hitchance      = pred_sdk::hitchance::high;

        // --- Q tuning ---
        inline float q1_setup_dist        = 700.f;
        inline float q1_max_start_dist    = 1600.f;
        inline float q2_min_delay_ms      = 100.f;
        inline float q2_force_release_ms  = 900.f;
        inline float q2_min_release_dist  = 400.f;
        inline bool  q2_release_on_sleep  = true;

        // --- E tuning ---
        inline float e_max_target_range   = 1500.f;
        inline bool  e_anti_gapclose      = true;
        inline bool  e_wall_helper        = true;
        inline bool  e_auto_interrupt     = true;

        // --- R assist ---
        inline bool  r_assist_q2          = true;
        inline float r_assist_min_range   = 300.f;
        inline float r_assist_max_range   = 575.f;
        inline bool  r_require_sleep      = false;
        inline bool  r_avoid_turret       = true;

        // --- Misc ---
        inline bool  killsteal_enabled    = true;
        inline bool  passive_weave        = true;

        // --- Drawings ---
        inline bool  draw_q_range         = true;
        inline bool  draw_e_range         = true;
        inline bool  draw_r_range         = false;
        inline bool  draw_star_pos        = true;
        inline bool  draw_damage_bar      = true;
        inline bool  draw_sleep_timer     = true;
        inline bool  draw_only_ready      = false;

        // --- Colors ---
        inline uint32_t color_q = 0xFFFFFFFFu;
        inline uint32_t color_e = 0xFFFF00FFu;
        inline uint32_t color_r = 0xFF00AAFFu;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load();
    void unload();

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_create_object(game_object* obj);
    void __fastcall on_delete_object(game_object* obj);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
    void __fastcall on_new_path(game_object* obj, bool is_dash, float dash_speed);
    bool __stdcall  on_before_attack(orb_sdk::event_data* data);
    void __stdcall  on_after_attack(orb_sdk::event_data* data);

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo();
    void harass();
    void clear();
    void flee();

    // ============================================================================
    // VALIDATION HELPERS
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    bool is_casting();
    float mana_pct();
}
