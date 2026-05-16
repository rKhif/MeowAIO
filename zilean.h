#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace zilean
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::zilean_const
    // ============================================================================
    namespace spell_data
    {
        // Q — Time Bomb (circular ground placement, 0.7s arc + 3s bomb)
        constexpr float q_range          = 875.f;
        constexpr float q_radius         = 150.f;     // bomb collision half-width
        constexpr float q_effect_radius  = 350.f;     // detonation AOE
        constexpr float q_delay          = 0.70f;     // 0.25 cast + 0.45 arc travel
        constexpr float q_total_delay    = 0.70f;
        constexpr float q_bomb_duration  = 3.f;
        constexpr float q_stun_duration[5] = { 1.1f, 1.2f, 1.3f, 1.4f, 1.5f };

        // W — Rewind (self-cast, resets Q/E/R cooldowns)

        // E — Time Warp (targeted speed/slow)
        constexpr float e_range          = 550.f;
        constexpr float e_duration       = 2.5f;

        // R — Chronoshift (targeted ally heal-on-death revive)
        constexpr float r_range          = 900.f;
        constexpr float r_resurrect_time = 3.f;
        constexpr float r_heal_base[3]   = { 600.f, 850.f, 1100.f };
        constexpr float r_heal_ap_ratio  = 2.0f;

        // Buff hashes
        constexpr uint32_t buff_hash_q_bomb        = 0x2EC0F961u; // ZileanQEnemyBomb
        constexpr uint32_t buff_hash_e_haste       = 0xEFF47C6Au; // TimeWarp
        constexpr uint32_t buff_hash_e_slow        = 0xCF9C21FBu; // timewarpslow
        constexpr uint32_t buff_hash_r_chronoshift = 0x9A270666u; // ChronoShift
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_use_w       = true;     // Q→W→Q double-bomb chain
        inline bool combo_use_e       = true;
        inline int  combo_q_hitchance = pred_sdk::hitchance::high;
        inline int  combo_disable_aa_lvl = 9;     // disable AA in combo from this level (0=never)

        // --- Harass ---
        inline bool harass_use_q      = true;
        inline bool harass_use_w      = false;    // double-bomb in harass
        inline int  harass_mana_pct   = 50;

        // --- Killsteal ---
        inline bool ks_use_q          = true;

        // --- Auto R (Chronoshift) ---
        inline bool auto_r_enabled    = true;
        inline int  auto_r_hp_pct     = 10;       // R when ally HP% under
        inline int  auto_r_min_dmg_pct = 5;       // require this much incoming dmg %

        // --- Auto E ---
        inline bool e_flee_self       = true;     // E on self when in flee mode
        inline bool e_auto_ally_melee = false;    // auto-E ally when melee enemy is on them

        // --- Clear ---
        inline bool clear_use_q       = true;
        inline int  clear_q_min       = 2;

        // --- Drawings ---
        inline bool draw_q_range      = true;
        inline bool draw_e_range      = true;
        inline bool draw_r_range      = false;
        inline bool draw_only_ready   = true;
        inline bool draw_status_text  = true;
        inline bool draw_damage_bar   = true;
        inline bool draw_r_dmg_bar    = true;

        // --- Colors (ARGB) ---
        inline uint32_t color_q       = 0xFF00FF00u; // green (also R)
        inline uint32_t color_e       = 0xFFFFD700u; // gold
        inline uint32_t color_r       = 0xFF00FFFFu; // cyan
        inline uint32_t color_r_dmg   = 0x9000FFFFu; // cyan w/ alpha (R dmg bar — unused for revive)
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
    bool __stdcall  on_before_attack(orb_sdk::event_data* data);

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
