#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace zeri
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::zeri_const
    // ============================================================================
    namespace spell_data
    {
        // Q — Burst Fire (Zeri's primary "auto attack" — replaces AAs)
        constexpr float q_range              = 710.f;
        constexpr float q_radius             = 65.f;
        constexpr float q_speed              = 2600.f;
        constexpr float q_speed_overcharged  = 3400.f;
        constexpr float q_as_cap             = 1.5f;
        constexpr float q_windup_pct         = 0.15625f;
        constexpr float q_overcharged_windup_mult = 0.80f;
        constexpr float passive_aa_range     = 500.f;

        // W — Ultrashock Laser (linear skillshot)
        constexpr float w_range_pulse        = 1200.f;
        constexpr float w_range_laser        = 1550.f;
        constexpr float w_radius             = 40.f;
        constexpr float w_speed              = 2500.f;
        constexpr float w_cast_time_max      = 0.55f;
        constexpr float w_cast_time_min      = 0.30f;
        constexpr float w_cast_time_scale    = 0.09f;

        // E — Spark Surge (short dash)
        constexpr float e_range              = 300.f;

        // R — Lightning Crash (self-cast AOE)
        constexpr float r_radius             = 800.f;
        constexpr float r_cast_time          = 0.25f;

        // Buff hashes
        constexpr uint32_t buff_hash_q_passive_ready = 0x016E7450u; // ZeriQPassiveReady
        constexpr uint32_t buff_hash_e_lightning     = 0xF00B5F96u; // ZeriESpecialRounds
        constexpr uint32_t buff_hash_r_overcharged   = 0x9A37D6E5u; // ZeriR (active)
        constexpr uint32_t buff_hash_r_stacks        = 0x34AE3384u; // ZeriRStacks
    }

    // AA modes — controls when on_before_attack returns true (allow AA)
    constexpr int AA_SMART       = 0; // AA only when passive charged on hero, or executable on minion
    constexpr int AA_FULL_CHARGE = 1; // AA only when passive charged
    constexpr int AA_EXECUTE     = 2; // AA only when executable (passive lethal)
    constexpr int AA_OFF         = 3; // never AA — fire Q always

    // R modes
    constexpr int R_MODE_ALWAYS = 0;
    constexpr int R_MODE_SMART  = 1;

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Main ---
        inline int  aa_mode           = AA_SMART;

        // --- Q collision ---
        inline bool q_minion_collision       = true;  // enable minion collision on Q
        inline int  q_collision_remove_level = 0;     // 1-18: auto-disable at this level; 0 = never

        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool harass_use_q      = true;
        inline bool combo_use_w       = true;
        inline bool combo_use_e       = true;
        inline int  combo_e_max_near  = 2;
        inline bool combo_use_r       = true;
        inline int  combo_r_mode      = R_MODE_SMART;
        inline int  combo_r_min       = 1;
        inline bool combo_w_killsteal = true;

        // --- W behavior toggles ---
        // Restrict every auto-W cast (combo / harass / clear / killsteal /
        // poke) to lines that pierce a terrain wall — turns W into a
        // wall-pierce-only sniper.
        inline bool w_only_walls       = false;
        // Auto-cast W on enemy heroes that are outside our AA range and
        // would be executed by W damage (poke finisher).
        inline bool w_kill_outside_aa  = true;

        // --- Harass ---
        inline bool harass_use_w      = true;
        inline int  harass_mana_pct   = 30;

        // --- Lane / lasthit / jungle ---
        inline bool lane_use_q_lh     = true;
        inline bool lane_use_q_clear  = true;
        inline bool lane_use_w        = true;
        inline int  lane_mana_pct     = 30;
        inline bool lh_w_cannon       = false;

        // --- Flee ---
        inline bool  flee_use_e              = true;
        // E in flee only fires when the player is within this distance of a
        // wall-jump spot's near endpoint.
        inline float flee_wall_jump_max_dist = 150.f;

        // --- Wall Jump hotkey ---
        inline bool        jump_key_active = false;
        inline std::string jump_key_str;

        // --- Q chain bounce while R (Hypercharged) is active ---
        inline bool combo_q_chain_r = true;

        // --- E Wall Auto-W (auto W mid-dash through a wall) ---
        inline bool e_wall_auto_w     = true;
        inline int  e_wall_auto_w_hc  = 40;   // hitchance (0-100)

        // --- Queue Manual E during Q ---
        inline bool queue_manual_e = true;

        // --- Q cast position humanization (small lateral jitter on hero Qs) ---
        inline bool  q_randomize       = false;
        inline float q_randomize_range = 20.f;  // max +/- lateral offset in units


        // --- Hitchance ---
        inline int  w_hitchance       = 70;

        // --- Semi-W hotkey ---
        inline bool        semi_w_active = false;
        inline std::string semi_w_key_str;

        // --- Auto-Q hero in farm hotkey (toggle) ---
        inline bool        auto_q_farm_enabled = false;
        inline std::string auto_q_farm_key_str;

        // --- Infotab entry IDs (0 = not registered) ---
        inline uint32_t infotab_semi_w      = 0;
        inline uint32_t infotab_wall_jump   = 0;
        inline uint32_t infotab_auto_q_farm = 0;

        // --- R input blocker ---
        // Master toggle: when on, the in-game R keypress is suppressed unless
        // at least one enemy hero is inside R's radius.
        inline bool r_block_enabled    = true;

        // --- Drawings ---
        inline bool draw_q_range      = true;
        inline bool draw_w_range      = true;
        inline bool draw_e_range      = false;
        inline bool draw_e_preview    = true;
        inline bool draw_r_range      = false;
        inline bool draw_status_text  = true;
        inline bool draw_r_dmg_bar    = true;
        inline bool draw_only_ready   = false;
        inline bool draw_jump_spots   = false;
        inline float draw_jump_max_dist = 1500.f;

        // --- Colors (ARGB) ---
        inline uint32_t color_q       = 0xFFB464C8u; // purple-ish
        inline uint32_t color_w       = 0xFFDCB43Cu; // gold
        inline uint32_t color_e       = 0xFFFFB400u; // amber
        inline uint32_t color_e_preview = 0xFF00FFFFu; // cyan
        inline uint32_t color_r       = 0xFF50FF78u; // green
        inline uint32_t color_r_dmg   = 0xFFAA44FFu; // purple — R damage bar
        inline uint32_t color_jump    = 0xFFFFFFFFu; // white
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
    bool __fastcall on_cast_spell(game_object* obj, int spell_slot, math::vector3 position, game_object* target);
    bool __stdcall  on_before_attack(orb_sdk::event_data* data);

    // ============================================================================
    // GAME MODE FUNCTIONS
    // ============================================================================
    void combo();
    void harass();
    void clear();
    void fast_clear();
    void lasthit();
    void flee();

    // ============================================================================
    // VALIDATION HELPERS
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    float mana_pct();
}
