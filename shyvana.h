#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace shyvana
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::shyvana_const
    // ============================================================================
    namespace spell_data
    {
        // Q — Emberstrike (AA reset; recast Q2 and Q3 Dragon-only)
        constexpr float q_recast_window     = 4.f;

        // W — Inferno Aegis (self-cast shield + MS; recast detonates)
        constexpr float w_effect_radius     = 350.f;
        constexpr float w_shield_duration   = 2.5f;
        constexpr float w_recast_min_delay  = 1.0f;

        // E — Molten Burst (linear skillshot, different stats per form)
        constexpr float e_range             = 800.f;
        constexpr float e_human_radius      = 60.f;
        constexpr float e_human_speed       = 1600.f;
        constexpr float e_human_cast_time   = 0.25f;
        constexpr float e_dragon_radius     = 50.f;
        constexpr float e_dragon_speed      = 1575.f;
        constexpr float e_dragon_cast_time  = 0.3333f;

        // R — Dragon's Descent (Human cast → dash + transform; 100 fury cost)
        constexpr float r_target_range_by_rank[3] = { 400.f, 1050.f, 1050.f };
        constexpr float r_target_range_max  = 1050.f;
        constexpr float r_cast_time         = 0.25f;
        constexpr float r_fury_cost         = 100.f;

        // Buff hashes
        constexpr uint32_t buff_hash_dragon_form     = 0x14055577u;
        constexpr uint32_t buff_hash_w_shield        = 0x007304AAu;
        constexpr uint32_t buff_hash_q_attack_ready  = 0x0F677F73u;
        constexpr uint32_t buff_hash_q_attack_ready2 = 0x80ED5353u;
        constexpr uint32_t buff_hash_q_attack_ready3 = 0x7FED51C0u;
        constexpr uint32_t buff_hash_q2              = 0x4D0130BEu;
        constexpr uint32_t buff_hash_q3              = 0x4E013251u;
        constexpr uint32_t buff_hash_q_lockout       = 0xFA9837E1u;
    }

    // R modes
    constexpr int R_MODE_DISABLED = 0;
    constexpr int R_MODE_KILLABLE = 1;
    constexpr int R_MODE_ENGAGE   = 2;
    constexpr int R_MODE_SEMI     = 3;

    // W recast modes
    constexpr int W_RECAST_SMART  = 0;
    constexpr int W_RECAST_ALWAYS = 1;
    constexpr int W_RECAST_NEVER  = 2;

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_recast_q    = true;     // allow Q2 recast
        inline bool combo_use_q3      = true;     // Dragon Q3
        inline bool combo_use_w       = true;
        inline int  w_recast_mode     = W_RECAST_SMART;
        inline int  w_recast_delay_ms = 200;
        inline int  w_heal_hp_pct     = 40;       // dragon heal threshold
        inline bool combo_use_e       = true;
        inline bool e_outside_aa_only = false;
        inline int  combo_e_hitchance = pred_sdk::hitchance::high;
        inline bool combo_use_r       = true;
        inline int  combo_r_mode      = R_MODE_ENGAGE;
        inline int  combo_r_min_tgts  = 1;
        inline int  combo_r_min_hp    = 20;
        inline bool combo_r_turret    = false;

        // --- Harass ---
        inline bool harass_use_q      = true;
        inline bool harass_use_e      = true;
        inline int  harass_e_hitchance = pred_sdk::hitchance::high;

        // --- Clear / jungle ---
        inline bool jungle_use_q      = true;
        inline bool jungle_use_w      = true;
        inline bool jungle_use_e      = true;
        inline bool lane_use_q        = true;
        inline bool lane_use_e        = true;
        inline int  lane_e_min        = 3;

        // --- Misc / auto ---
        inline bool auto_e_cc         = true;
        inline bool auto_e_dash       = true;
        inline bool ks_e              = true;
        inline bool flee_use_w        = true;

        // --- Semi hotkeys ---
        inline bool        semi_e_active = false;
        inline std::string semi_e_key_str;
        inline bool        semi_r_active = false;
        inline std::string semi_r_key_str;

        // --- Drawings ---
        inline bool draw_e_range      = true;
        inline bool draw_w_range      = false;
        inline bool draw_r_range      = true;
        inline bool draw_form         = true;
        inline bool draw_damage_bar   = true;
        inline bool draw_r_dmg_bar    = true;
        inline bool draw_only_ready   = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_e       = 0xFFFF6600u;
        inline uint32_t color_w       = 0xFF00FF00u;
        inline uint32_t color_r       = 0xFFFF0000u;
        inline uint32_t color_r_dmg   = 0x90FF0000u; // red w/ alpha (R dmg bar)
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
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_execute_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
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
    bool has_enough_resource(int slot);
    bool is_casting();
    float fury_pct();
    float hp_pct();
}
