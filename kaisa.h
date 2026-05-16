#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace kaisa
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::kaisa.hpp
    // ============================================================================
    namespace spell_data
    {
        // Passive — Second Skin (plasma stacks on enemy, detonate on hit)
        constexpr int   plasma_max_stacks      = 5;
        constexpr float plasma_duration        = 4.f;
        constexpr float passive_ap_base_ratio          = 0.15f;
        constexpr float passive_ap_per_stack_ratio     = 0.025f;
        constexpr float passive_detonation_miss_hp     = 0.15f;
        constexpr float passive_detonation_ap_per100   = 0.06f;
        constexpr float passive_monster_cap            = 400.f;

        // Q — Icathian Rain (self-cast, fires missiles divided among targets in range)
        constexpr float q_range             = 600.f;
        constexpr int   q_missiles          = 6;
        constexpr int   q_missiles_evolved  = 12;
        constexpr float q_subsequent_factor = 0.25f;
        constexpr float q_minion_low_hp_pct = 0.35f;

        // W — Void Seeker (linear skillshot, 3000 range)
        constexpr float w_range         = 3000.f;
        constexpr float w_radius        = 100.f;       // half of 200 width
        constexpr float w_speed         = 1750.f;
        constexpr float w_delay         = 0.4f;
        constexpr int   w_plasma_stacks      = 2;
        constexpr int   w_plasma_stacks_evo  = 3;

        // E — Supercharge (self-cast, AS buff after charge)
        constexpr float e_charge_min        = 0.6f;
        constexpr float e_as_duration       = 4.f;
        constexpr float e_invis_duration    = 0.5f;

        // R — Killer Instinct (dash to marked enemy)
        constexpr float r_effect_radius     = 525.f;
        constexpr float r_ranges[3]         = { 2000.f, 2500.f, 3000.f };

        // Buff hashes (verified in venp_db)
        // Enemy buffs
        constexpr uint32_t buff_hash_plasma      = 0x090D4727u;  // kaisapassivemarker
        constexpr uint32_t buff_hash_r_marker    = 0x94E74ACFu;  // kaisapassivemarkerr
        // Self buffs
        constexpr uint32_t buff_hash_e_charging  = 0x644E418Fu;  // kaisaeweaponmaterials
        constexpr uint32_t buff_hash_e_as        = 0x25618A62u;  // kaisaeattackspeed
        constexpr uint32_t buff_hash_r_dash      = 0x1AFBEBD6u;  // KaisaR
        constexpr uint32_t buff_hash_q_evolved   = 0x89AE79FAu;  // KaisaQEvolved
        constexpr uint32_t buff_hash_w_evolved   = 0x788F22ACu;  // KaisaWEvolved
        constexpr uint32_t buff_hash_e_evolved   = 0x6FEF1046u;  // KaisaEEvolved
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline int  combo_q_max       = 2;     // max enemies/minions in Q range to fire
        inline bool combo_use_w       = true;
        inline int  combo_w_range     = 1500;  // configurable W max range
        inline bool combo_w_collision_check = true;
        inline bool combo_use_r       = true;
        inline bool combo_r_only_kill = true;
        inline int  combo_r_max_near  = 2;
        inline bool combo_rw_buffer   = true;  // cast W during R dash

        // --- Harass ---
        inline bool harass_use_q      = true;
        inline int  harass_q_max      = 3;
        inline bool harass_use_w      = true;
        inline int  harass_w_range    = 1500;
        inline int  harass_mana_pct   = 30;

        // --- Lane clear ---
        inline bool lane_use_q        = true;
        inline int  lane_q_min        = 3;     // min minions in Q range to fire
        inline bool lane_use_w        = false;
        inline int  lane_mana_pct     = 30;

        // --- Last hit ---
        inline bool lh_use_q          = true;
        inline int  lh_q_kills        = 1;
        inline bool lh_w_cannon       = true;
        inline int  lh_mana_pct       = 30;

        // --- Jungle ---
        inline bool jungle_use_q      = true;
        inline bool jungle_use_w      = true;
        inline int  jungle_mana_pct   = 20;

        // --- Flee ---
        inline bool flee_use_e        = true;

        // --- Killsteal ---
        inline bool ks_use_q          = true;
        inline bool ks_use_w          = true;

        // --- Hitchance ---
        inline int  w_hitchance       = 70;

        // --- Semi-W (manual hold-to-cast) ---
        inline bool        semi_w_active = false;
        inline std::string semi_w_key_str;
        inline uint32_t    infotab_semi_w = 0;

        // --- Drawings ---
        inline bool draw_q_range        = true;
        inline bool draw_w_range        = false;
        inline bool draw_r_range        = true;
        inline bool draw_plasma_stacks  = true;
        inline bool draw_evolution_text = true;
        inline bool draw_damage_bar     = true;
        inline bool draw_r_dmg_bar      = true;
        inline bool draw_only_ready     = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_q       = 0xFFFFFFFFu; // white
        inline uint32_t color_w       = 0xFF00FFFFu; // cyan
        inline uint32_t color_r       = 0xFFFF8800u; // orange
        inline uint32_t color_r_dmg   = 0x90FF8800u; // orange w/ alpha (R dmg bar)
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
    bool is_casting();
    float mana_pct();

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    bool cast_q();                                  // self-cast
    bool cast_w(game_object* target, float range, int hitchance);
    bool cast_e();                                  // self-cast
    bool cast_r(game_object* target, bool force = false);
}
