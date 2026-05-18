#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace mel
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::mel_const
    // ============================================================================
    namespace spell_data
    {
        // Q — Radiant Volley (circular AOE)
        constexpr float q_range          = 950.f;
        constexpr float q_radius         = 125.f;       // pred half-radius (250 total)
        constexpr float q_total_radius   = 250.f;
        constexpr float q_cast_time      = 0.35f;

        // W — Rebuttal (self-cast 0.75s shield/reflect)

        // E — Solar Snare (linear root)
        constexpr float e_range          = 1000.f;
        constexpr float e_radius         = 70.f;
        constexpr float e_speed          = 1100.f;
        constexpr float e_delay          = 0.25f;
        constexpr float e_cast_time      = 0.25f;
        constexpr float e_root_duration  = 1.5f;

        // R — Ascension (overwhelm execute, 0.75s cast time)
        constexpr float r_cast_time      = 0.75f;

        // Passive: Searing Brilliance (3 stacks per cast, max 9, 5s)
        constexpr int   brilliance_stacks_per_cast = 3;
        constexpr int   brilliance_max_stacks      = 9;
        constexpr float brilliance_duration        = 5.f;

        // Passive: Overwhelm (debuff stacks on enemies)
        constexpr float overwhelm_duration         = 5.f;

        // Buff names (hashes are 0u — name fallback)
        constexpr const char* buff_name_overwhelm  = "MelPassiveOverwhelm";
        constexpr const char* buff_name_brilliance = "MelPassiveBonusAttack";
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_use_e       = true;
        inline bool combo_use_r       = true;
        inline bool combo_eq_chain    = true;       // E→Q auto on rooted enemy
        inline int  combo_q_hitchance = pred_sdk::hitchance::medium;
        inline int  combo_e_hitchance = pred_sdk::hitchance::high;
        inline int  combo_r_min_stacks = 5;         // R when target has N+ overwhelm stacks
        inline bool combo_r_killable  = true;       // R if execute lethal regardless of stacks
        inline int  combo_r_min_kill_pct = 100;     // R dmg must be >= X% of target EHP to execute

        // --- Harass ---
        inline bool harass_use_q      = true;
        inline bool harass_use_e      = true;
        inline int  harass_q_hitchance = pred_sdk::hitchance::high;
        inline int  harass_e_hitchance = pred_sdk::hitchance::high;
        inline int  harass_mana_pct   = 40;

        // --- Clear ---
        inline bool clear_use_q       = true;
        inline bool clear_use_e       = false;
        inline int  clear_q_min       = 3;
        inline int  clear_mana_pct    = 30;

        // --- Flee ---
        inline bool flee_use_e        = true;

        // --- W Rebuttal (reflect specific enemy spells) ---
        inline bool w_reflect_enabled       = true;
        inline bool w_reflect_targeted_only = false;  // only react to direct-target spells

        // --- W Smart Modes ---
        inline bool w_mode_block_targeted = true; // fire W on any incoming spell/AA targeted on us
        inline bool w_mode_block_lethal   = true; // fire W when predicted post-damage HP <= threshold
        inline int  w_lethal_hp_pct       = 15;   // threshold: trigger if (hp+shields - dmg) <= max_hp * pct/100

        // --- Semi E (manual hold-to-cast on best target in range) ---
        inline bool semi_e_pressed    = false;

        // --- Auto Q (toggle: continuously fires Q on best target in range) ---
        inline bool auto_q_enabled    = false;
        inline int  auto_q_hitchance  = pred_sdk::hitchance::high;

        // --- Drawings ---
        inline bool draw_q_range      = true;
        inline bool draw_e_range      = true;
        inline bool draw_r_dmg_bar    = true;
        inline bool draw_only_ready   = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_q       = 0xFF00CCFFu; // cyan
        inline uint32_t color_e       = 0xFFFFD700u; // gold
        inline uint32_t color_overwhelm = 0xFFFFAA00u; // amber
        inline uint32_t color_r       = 0xFFFF40C0u; // pink (R READY/EXECUTE indicator)
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
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_basic_attack(game_object* obj, game_object* target, spell_cast* cast);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);

    // ============================================================================
    // GAME MODE FUNCTIONS
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
