#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace syndra
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::syndra.hpp
    // ============================================================================
    namespace spell_data
    {
        // Passive Splinter thresholds (Transcendent ranks)
        constexpr int splinter_q_charges    = 40;
        constexpr int splinter_w_true_dmg   = 60;
        constexpr int splinter_e_wide_cone  = 80;
        constexpr int splinter_r_execute    = 100;

        // Q — Dark Sphere (circular ground placement)
        constexpr float q_range            = 800.f;
        constexpr float q_radius           = 210.f;
        constexpr float q_proc_delay       = 0.6f;
        constexpr float q_effective_range  = 920.f; // range + radius slack for edge hits
        constexpr float q_sphere_duration  = 6.f;

        // W — Force of Will (grab + throw)
        constexpr float w_grab_range       = 925.f;
        constexpr float w_throw_range      = 950.f;
        constexpr float w_throw_radius     = 112.5f;
        constexpr float w_throw_speed      = 1450.f;
        constexpr float w_throw_delay      = 0.25f;
        constexpr float w_min_hold_time    = 0.5f;
        constexpr float w_force_throw_time = 4.5f;
        constexpr float w_cast_cooldown    = 0.25f;
        // Transcendent (60+) bonus true damage = raw_w * (0.12 + 0.02 * AP/100)
        constexpr float w_true_dmg_base    = 0.12f;
        constexpr float w_true_dmg_scale   = 0.02f;

        // E — Scatter the Weak (cone slash + sphere push)
        constexpr float e_cone_range       = 700.f;
        constexpr float e_cast_delay       = 0.25f;
        constexpr float e_base_angle       = 56.f;   // degrees
        constexpr float e_transcendent_angle = 84.f; // degrees, with 80+ splinters
        constexpr float e_sphere_range     = 800.f;  // max distance from player to sphere
        constexpr float e_max_push_range   = 1200.f; // max push end distance
        constexpr float e_stun_range       = 1300.f;
        constexpr float e_sphere_hit_bonus = 100.f;
        constexpr float e_push_speed       = 2000.f;
        constexpr float e_push_radius      = 100.f;
        constexpr float e_cone_speed       = 2500.f;

        // R — Unleashed Power
        constexpr float r_range            = 675.f;
        constexpr float r_speed            = 1100.f;
        constexpr float r_delay            = 0.264f;
        constexpr int   r_base_spheres     = 3;
        constexpr int   r_max_grab_spheres = 4;
        constexpr float r_execute_pct      = 0.15f;  // execute under 15% max HP at 100 splinters
        constexpr float r_overkill_factor  = 1.05f;

        // Sphere tracking
        constexpr float sphere_match_radius = 200.f;

        // Buff hashes (verified in venp_db)
        constexpr uint32_t buff_hash_passive_stacks = 0x9FCE5B0Au; // "syndrapassivestacks"
        constexpr uint32_t buff_hash_r_upgrade      = 0xAD6E5C21u; // "syndrapassiverupgrade"
        constexpr const char* buff_name_passive_stacks = "syndrapassivestacks";
        constexpr const char* buff_name_r_upgrade      = "syndrapassiverupgrade";
        constexpr const char* buff_name_w_holding      = "SyndraW";

        // Spell names for on_process_cast
        constexpr const char* spell_name_w_grab = "syndraw";
        constexpr const char* spell_name_w_cast = "syndrawcast";

        // Sphere object name (from create_object event)
        constexpr const char* sphere_object_name = "Seed";
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
        inline bool combo_r_only_kill = true;
        inline bool combo_fast_qe     = true;
        inline bool auto_qe_antigap   = false;
        inline int  combo_hitchance   = pred_sdk::hitchance::high;

        // --- Harass ---
        inline bool  harass_use_q      = true;
        inline bool  harass_use_w      = true;
        inline bool  harass_use_e      = false;
        inline int   harass_hitchance  = pred_sdk::hitchance::high;
        inline int   harass_mana_pct   = 30;

        // --- Clear / Fast clear ---
        inline bool  clear_use_q       = true;
        inline bool  clear_use_w       = false;
        inline int   clear_q_min_hits  = 3;
        inline int   clear_mana_pct    = 40;
        inline bool  fast_clear_use_q  = true;
        inline bool  fast_clear_use_w  = false;
        inline bool  fast_clear_use_e  = true;

        // --- Lasthit ---
        inline bool  lasthit_use_q     = true;
        inline int   lasthit_mana_pct  = 30;

        // --- Semi-R (manual hold-to-cast) ---
        inline bool        semi_r_active = false;
        inline std::string semi_r_key_str;
        inline uint32_t    infotab_semi_r = 0;

        // --- Save R (toggle that blocks all auto R casts) ---
        inline bool        save_r_active  = false;
        inline std::string save_r_key_str;
        inline uint32_t    infotab_save_r = 0;

        // --- Auto Q (idle-mode poke toggle) ---
        inline bool        auto_q_active  = false;
        inline std::string auto_q_key_str;
        inline uint32_t    infotab_auto_q = 0;
        inline int         auto_q_hitchance = pred_sdk::hitchance::high;

        // --- Semi-QE (manual hold-to-cast QE chain) ---
        inline bool        semi_qe_active = false;
        inline std::string semi_qe_key_str;
        inline uint32_t    infotab_semi_qe = 0;

        // --- Flash + Q + E (rising-edge macro) ---
        inline std::string flash_qe_key_str;
        inline uint32_t    infotab_flash_qe = 0;

        // --- Drawings ---
        inline bool draw_q_range      = true;
        inline bool draw_w_range      = true;
        inline bool draw_e_range      = true;
        inline bool draw_qe_range     = true;
        inline bool draw_qe_rgb       = true;
        inline bool draw_r_range      = false;
        inline bool draw_spheres      = true;
        inline bool draw_r_dmg_bar    = true;     // R-only HP-bar overlay
        inline bool draw_killable_txt = true;
        inline bool draw_only_ready   = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_q       = 0xFFB066FFu; // purple
        inline uint32_t color_w       = 0xFF00FFFFu; // cyan
        inline uint32_t color_e       = 0xFFFF55AAu; // pink-magenta
        inline uint32_t color_qe      = 0xFFFFAA00u; // orange (QE chain max range)
        inline uint32_t color_r       = 0xFFFF4040u; // red (R range)
        inline uint32_t color_r_dmg   = 0x90FF4040u; // red, 56% alpha (R dmg bar overlay)
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
    void __fastcall on_create_object(game_object* obj);
    void __fastcall on_delete_object(game_object* obj);
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
    void lasthit();

    // ============================================================================
    // TARGETING / VALIDATION HELPERS
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    bool is_casting();
    float mana_pct();

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    bool cast_q_on(game_object* target, int hitchance);
    bool cast_q_at(const math::vector3& pos);
    bool cast_w_grab();
    bool cast_w_throw(game_object* target, int hitchance, bool aggressive = false);
    bool cast_e_push(game_object* target);
    bool cast_e_cone(game_object* target);
    bool cast_qe_combo(game_object* target);
    bool cast_r(game_object* target, bool force = false);
}
