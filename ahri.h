#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace ahri
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::ahri_const
    // ============================================================================
    namespace spell_data
    {
        // Q — Orb of Deception
        constexpr float q_range          = 900.f;
        constexpr float q_radius         = 100.f;     // pred half-radius (200 width)
        constexpr float q_speed          = 1550.f;
        constexpr float q_delay          = 0.25f;

        // W — Fox-Fire (self-cast, auto-targeted flames)
        constexpr float w_range          = 575.f;     // standard W reach
        constexpr float w_range_charm    = 725.f;     // priority range vs charmed targets
        constexpr float w_subseq_dmg_pct = 0.40f;     // 2nd/3rd flame damage multiplier

        // E — Charm (linear skillshot, blocks on minion)
        constexpr float e_range          = 1000.f;
        constexpr float e_radius         = 60.f;
        constexpr float e_speed          = 1550.f;
        constexpr float e_delay          = 0.25f;
        constexpr float e_cast_time      = 0.25f;

        // R — Spirit Rush (3-charge dash; recast within 15s)
        constexpr float r_dash_range     = 500.f;

        // Buff hashes (verified in venp_db)
        constexpr uint32_t buff_hash_w_active        = 0xA801999Au; // AhriW (flames active)
        constexpr uint32_t buff_hash_w_ms            = 0x35692204u; // ahriwspeedbuff
        constexpr uint32_t buff_hash_r_recast        = 0xA50194E1u; // AhriR (recast window)
        constexpr uint32_t buff_hash_e_charm        = 0x96017044u; // AhriE (charmed enemy)
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q          = true;
        inline bool combo_use_w          = true;
        inline bool combo_use_e          = true;
        inline bool combo_use_r          = false;     // R only on hotkey/gap-close by default
        inline bool combo_e_first        = false;     // toggleable mode — E before W/Q
        inline int  combo_q_hitchance    = pred_sdk::hitchance::medium;
        inline int  combo_e_hitchance    = pred_sdk::hitchance::high;

        // --- Harass ---
        inline bool harass_use_q         = true;
        inline bool harass_use_e         = false;
        inline int  harass_mana_pct      = 50;

        // --- Clear ---
        inline bool clear_use_q          = true;
        inline int  clear_q_min          = 3;
        inline int  clear_mana_pct       = 40;

        // --- Auto / misc ---
        inline bool auto_q_cc            = false;
        inline bool auto_e_cc            = false;
        inline bool antigap_e            = false;
        inline bool ks_use_q             = true;

        // --- Semi-R hotkey ---
        inline bool        semi_r_active = false;
        inline std::string semi_r_key_str;
        inline uint32_t    infotab_semi_r = 0;

        // --- Semi-E hotkey ---
        inline bool        semi_e_active   = false;
        inline std::string semi_e_key_str;
        inline uint32_t    infotab_semi_e  = 0;
        inline int         semi_e_hitchance = pred_sdk::hitchance::medium;

        // --- Drawings ---
        inline bool draw_q_range         = true;
        inline bool draw_e_range         = true;
        inline bool draw_w_range         = true;
        inline bool draw_r_range         = false;
        inline bool draw_damage_bar      = true;
        inline bool draw_r_dmg_bar       = true;
        inline bool draw_killable_text   = true;
        inline bool draw_combo_mode      = true;
        inline bool draw_only_ready      = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_q          = 0xFF00FF80u; // green
        inline uint32_t color_w          = 0xFFFF8800u; // orange
        inline uint32_t color_e          = 0xFFFF40C0u; // pink (charm)
        inline uint32_t color_r          = 0xFFE060FFu; // violet
        inline uint32_t color_r_dmg      = 0x90E060FFu; // violet w/ alpha (R dmg bar)
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
    void __fastcall on_new_path(game_object* obj, bool is_dash, float dash_speed);
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
