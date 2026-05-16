#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace orianna
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::orianna_const
    // ============================================================================
    namespace spell_data
    {
        // Ball constants
        constexpr float ball_q_missile_speed    = 1400.f;
        constexpr float ball_e_missile_speed    = 1850.f;
        constexpr float ball_leash_range        = 1290.f;
        constexpr float ball_leash_range_ally   = 1355.f;

        // Q — Command: Attack
        constexpr float q_range                 = 825.f;
        constexpr float q_radius                = 87.5f;     // pred half-radius
        constexpr float q_effect_radius         = 175.f;

        // W — Command: Dissonance (AOE around ball + speed boost field)
        constexpr float w_effect_radius         = 225.f;

        // E — Command: Protect (sends ball to self/ally, shields target)
        constexpr float e_range                 = 1120.f;
        constexpr float e_width                 = 160.f;     // ball travel hitbox

        // R — Command: Shockwave (AOE pull around ball)
        constexpr float r_effect_radius         = 415.f;

        // Buff hashes (0u → name fallback)
        constexpr uint32_t buff_hash_ball_on_self = 0u;
        constexpr uint32_t buff_hash_ball_on_ally = 0u;
        constexpr const char* buff_name_ball_self = "orianaghostself";
        constexpr const char* buff_name_ball_ally = "orianaghost";

        // Missile / object names
        constexpr const char* q_missile_name = "OrianaIzunaCommand";
        constexpr const char* e_missile_name = "OrianaRedactCommand";
        constexpr const char* ball_object_name = "TheDoomBall";
    }

    // Ball location state
    enum class ball_state : uint8_t
    {
        on_self,
        on_ally,
        on_ground,
        in_transit_q,
        in_transit_e,
    };

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_use_w       = true;
        inline bool combo_use_r       = true;
        inline int  combo_q_hitchance = pred_sdk::hitchance::medium;
        inline bool combo_r_killable  = true;     // also fire R if 1 hero is killable

        // --- Semi-manual R (hotkey held) ---
        inline bool semi_r_pressed     = false;   // set by hotkey callback
        inline int  semi_r_min_targets = 1;       // min enemies in R radius to fire

        // --- Auto R (independent of combo, fires on enemy count) ---
        inline bool auto_r_enabled     = false;
        inline int  auto_r_min_targets = 3;       // min enemies in R radius to fire

        // --- Killsteal (always-on, independent of orbwalker mode) ---
        inline bool ks_use_q       = true;
        inline bool ks_use_r       = true;

        // --- Auto Q (always-on, independent of orbwalker mode) ---
        inline bool auto_q_enabled   = false;
        inline int  auto_q_hitchance = pred_sdk::hitchance::high;

        // --- Auto W (fires when enemy hero is inside W radius of ball) ---
        inline bool auto_w_enabled     = false;

        // --- Harass ---
        inline bool harass_use_q      = true;
        inline bool harass_use_w      = true;
        inline int  harass_q_hitchance = pred_sdk::hitchance::medium;
        inline int  harass_mana_pct   = 40;

        // --- Clear ---
        inline bool clear_use_q       = true;
        inline bool clear_use_w       = true;
        inline int  clear_q_min       = 2;
        inline int  clear_w_min       = 3;
        inline int  clear_mana_pct    = 30;
        inline bool clear_save_for_r  = true;

        // --- Flee ---
        inline bool flee_use_w        = true;
        inline bool flee_use_e        = true;

        // --- Shield (E) ---
        inline bool shield_allies            = true;
        inline int  shield_hp_pct            = 30;
        inline bool shield_combo_self        = false;
        // Combo E behavior — 0=Off, 1=Aggressive, 2=Engage, 3=Smart (Engage→Aggressive)
        inline int  combo_e_mode             = 3;
        inline int  combo_engage_e_min_enemies = 3;      // min enemies near ally for R setup

        // --- Drawings ---
        inline bool draw_q_range      = true;
        inline bool draw_w_range      = true;     // W range at ball
        inline bool draw_e_range      = false;    // E range from player
        inline bool draw_r_range      = true;     // R range at ball
        inline bool draw_r_rgb        = true;     // rainbow cycle on R range circle
        inline bool draw_w_rgb        = false;    // rainbow cycle on W range circle
        inline bool draw_only_ready   = false;

        // --- Drawings: R damage bar ---
        inline bool draw_r_dmg_bar    = true;

        // --- Colors (ARGB) ---
        inline uint32_t color_q       = 0xFFFFFFFFu; // white
        inline uint32_t color_w       = 0xFF00FF80u; // green
        inline uint32_t color_e       = 0xFF40C0FFu; // blue
        inline uint32_t color_r       = 0xFFFF4040u; // red
        inline uint32_t color_r_dmg   = 0x90FF4040u; // red w/ alpha (R dmg bar)
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
    void __fastcall on_present();
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_create_missile(game_object* missile);
    void __fastcall on_delete_object(game_object* obj);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);

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
    bool has_enough_mana(int slot);
    bool is_casting();
    float mana_pct();
}
