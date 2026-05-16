#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace dr_mundo
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::dr_mundo_const
    // ============================================================================
    namespace spell_data
    {
        // Q — Infected Bonesaw (linear skillshot, % current HP magic damage)
        constexpr float q_range            = 1050.f;
        constexpr float q_radius           = 60.f;
        constexpr float q_speed            = 2000.f;
        constexpr float q_delay            = 0.25f;
        constexpr float q_curr_hp_pct[5]   = { 0.20f, 0.225f, 0.25f, 0.275f, 0.30f };
        constexpr float q_min_damage[5]    = { 80.f, 130.f, 180.f, 230.f, 280.f };
        constexpr float q_monster_cap[5]   = { 300.f, 375.f, 450.f, 525.f, 600.f };

        // W — Heart Zapper (self-cast AOE toggle)
        constexpr float w_radius              = 325.f;
        constexpr float w_duration            = 3.f;
        constexpr float w_recast_delay        = 0.5f;
        constexpr float w_high_storage_window = 0.75f;
        constexpr float w_recast_bonus_hp     = 0.07f;
        constexpr float w_base_damage[5]      = { 20.f, 35.f, 50.f, 65.f, 80.f };

        // E — Blunt Force Trauma (AA-reset empowered attack)
        constexpr float e_missing_hp_cap     = 0.70f;
        constexpr float e_missing_hp_max_amp = 0.40f;
        constexpr float e_bonus_hp_ratio     = 0.05f;
        constexpr float e_minion_mult        = 1.40f;
        constexpr float e_monster_mult       = 2.00f;
        constexpr float e_base_damage[5]     = { 5.f, 15.f, 25.f, 35.f, 45.f };

        // R — Maximum Dosage (self-buff, no direct damage)
        constexpr float r_duration           = 10.f;

        // Passive — Goes Where He Pleases (canister pickup)
        constexpr float passive_canister_time = 7.f;

        // Buff hashes (0u → name fallback)
        constexpr uint32_t buff_hash_w_active   = 0u;
        constexpr uint32_t buff_hash_e_empower  = 0u;
        constexpr uint32_t buff_hash_r_active   = 0u;

        // Buff name fragments
        constexpr const char* buff_name_w       = "DrMundoW";
        constexpr const char* buff_name_e       = "DrMundoE";
        constexpr const char* buff_name_r       = "DrMundoR";

        // Canister object name fragment (passive)
        constexpr const char* canister_name     = "DrMundoP";
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_use_w       = true;
        inline bool combo_use_e       = true;
        inline bool combo_use_r       = true;
        inline int  combo_q_hitchance = pred_sdk::hitchance::high;
        inline int  r_hp_pct          = 40;
        inline bool r_burst_pred     = true;
        inline bool r_only_nearby    = true;
        inline int  r_search_radius  = 900;

        // --- Harass ---
        inline bool harass_use_q     = true;
        inline bool harass_use_e     = true;
        inline int  harass_q_hitchance = pred_sdk::hitchance::high;
        inline int  harass_hp_pct    = 30;

        // --- Clear ---
        inline bool clear_use_q      = true;
        inline bool clear_use_w      = false;
        inline bool clear_use_e      = true;
        inline int  clear_hp_pct     = 20;

        // --- Fast clear ---
        inline bool fast_use_w       = true;
        inline bool fast_use_e       = true;

        // --- Lasthit ---
        inline bool lh_use_e         = true;

        // --- Flee ---
        inline bool flee_use_q       = true;

        // --- Drawings ---
        inline bool draw_q_range     = true;
        inline bool draw_w_range     = true;
        inline bool draw_canister    = true;
        inline bool draw_r_active    = true;     // R: Maximum Dosage active timer
        inline bool draw_damage_bar  = true;
        inline bool draw_r_dmg_bar   = true;
        inline bool draw_only_ready  = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_q          = 0xFF00CCFFu;  // cyan
        inline uint32_t color_w          = 0xFF00FF00u;  // green
        inline uint32_t color_canister   = 0xFFFFFF00u;  // yellow
        inline uint32_t color_r          = 0xFFFF4040u;  // red (R active text)
        inline uint32_t color_r_dmg      = 0x90FF4040u;  // red w/ alpha (R dmg bar overlay)
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load();
    void unload();

    // ============================================================================
    // EVENT CALLBACKS — __fastcall per SDK requirement
    // ============================================================================
    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_execute_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
    void __fastcall on_create_object(game_object* obj);
    void __fastcall on_delete_object(game_object* obj);
    bool __stdcall  on_before_attack(orb_sdk::event_data* data);

    // ============================================================================
    // GAME MODE FUNCTIONS
    // ============================================================================
    void combo();
    void harass();
    void clear();
    void fast_clear();
    void flee();

    // ============================================================================
    // VALIDATION HELPERS
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool is_casting();
    float hp_pct();
}
