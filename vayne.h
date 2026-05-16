#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace vayne
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::vayne
    // ============================================================================
    namespace spell_data
    {
        // Q — Tumble (short dash + AA reset)
        constexpr float q_dash_range         = 300.f;

        // W — Silver Bolts (passive, 3 stacks → %max HP true damage)
        constexpr int   w_stacks_to_proc     = 3;

        // E — Condemn (knockback 475u, stuns vs wall)
        constexpr float e_range              = 550.f; // standard cast range
        constexpr float e_knockback_dist     = 475.f;
        constexpr float e_wall_check_step    = 20.f;
        constexpr float e_wall_check_dist    = 420.f;

        // R — Final Hour (self-cast, increased AD + invis on Q + extended cooldown on takedown)
        constexpr float r_radius_check       = 1000.f; // search radius for "enemies nearby"

        // Buff names (hashes are 0u placeholders — name fallback)
        constexpr const char* buff_name_w_stacks  = "vaynesilvereddebuff";
        constexpr const char* buff_name_q_empower = "vaynetumblebonus";
        constexpr const char* buff_name_r_active  = "vayneinquisition";
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_use_e       = true;
        inline bool combo_e_only_stun = true;     // E only when wall-stun guaranteed
        inline bool combo_e_killable  = true;     // override: fire E if it would kill target
        inline bool combo_use_r       = true;
        inline int  combo_r_min       = 1;        // min enemies near to fire R
        inline bool combo_smart_q     = false;    // tumble toward kite-position vs cursor

        // --- Harass ---
        inline bool harass_use_q      = true;
        inline bool harass_use_e      = false;
        inline int  harass_mana_pct   = 30;

        // --- Clear ---
        inline bool clear_use_q       = true;
        inline int  clear_mana_pct    = 30;

        // --- Jungle ---
        inline bool jungle_use_q      = true;
        inline bool jungle_use_e      = false;
        inline int  jungle_mana_pct   = 20;

        // --- Semi hotkeys ---
        inline bool        semi_e_active = false;
        inline std::string semi_e_key_str;
        inline bool        semi_r_active = false;
        inline std::string semi_r_key_str;
        inline uint32_t    infotab_semi_e = 0;
        inline uint32_t    infotab_semi_r = 0;

        // --- Drawings ---
        inline bool draw_aa_range     = true;
        inline bool draw_e_range      = true;
        inline bool draw_damage_bar   = true;
        inline bool draw_r_dmg_bar    = true;
        inline bool draw_stun_spots   = true;
        inline bool draw_r_status     = true;
        inline bool draw_only_ready   = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_aa      = 0xFF60FF60u; // green
        inline uint32_t color_e       = 0xFFFF6666u; // red
        inline uint32_t color_stun    = 0xFFFF4444u; // bright red
        inline uint32_t color_r       = 0xFFFF40C0u; // pink (R: Final Hour timer)
        inline uint32_t color_r_dmg   = 0x90FF40C0u; // pink w/ alpha (R dmg bar)
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
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
    bool __stdcall  on_after_attack(orb_sdk::event_data* data);

    // ============================================================================
    // GAME MODE FUNCTIONS
    // ============================================================================
    void combo();
    void harass();
    void fast_clear();
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
