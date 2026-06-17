#pragma once

#include "sdk.hpp"
#include "champion_drawings.hpp"
#include <cstdint>
#include <string>

namespace kalista
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA
    // ============================================================================
    namespace spell_data
    {
        // Q - Pierce (linear skillshot)
        constexpr float q_range  = 1200.f;
        constexpr float q_radius = 40.f;
        constexpr float q_speed  = 2400.f;
        constexpr float q_delay  = 0.25f;

        // W - Sentinel (self-cast, scout)
        constexpr float w_range = 5000.f;

        // E - Rend (self-cast AOE, consumes spears)
        constexpr float e_range          = 1100.f;
        constexpr float e_expire_window  = 0.5f;   // seconds before expiry to auto-E

        // R - Fate's Call (targeted on oathsworn ally)
        constexpr float r_target_range      = 1200.f;
        constexpr float r_tether_radius     = 1100.f;

        // Buff hash for "kalistaexpungemarker" — per-target spear stacks.
        constexpr uint32_t buff_rend_stacks   = 0x8074DF9Eu;

        // Oathsworn marker buffs (carried by the bound ally).
        // "kalistapaltarbuffrise" / "KalistaPAltarBuff"
        constexpr uint32_t buff_oath_rise     = 0xB602CE7Cu;
        constexpr uint32_t buff_oath_buff     = 0xA609AD49u;
        // "kalistacoopstrikeally" — also present on the bound ally in lane.
        constexpr uint32_t buff_oath_coop     = 0x71C0793Cu;
    }

    // E damage tables (rank 0..4)
    constexpr float E_BASE_FIRST[5]       = { 20.f, 30.f, 40.f, 50.f, 60.f };
    constexpr float E_FIRST_RATIO_AD      = 0.70f;
    constexpr float E_FIRST_RATIO_AP      = 0.20f;
    constexpr float E_BASE_ADDITIONAL[5]  = { 8.f, 12.f, 16.f, 20.f, 24.f };
    constexpr float E_ADD_RATIO_AD[5]     = { 0.25f, 0.30f, 0.35f, 0.40f, 0.45f };
    constexpr float E_ADD_RATIO_AP        = 0.20f;

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Passive (Martial Poise) ---
        inline bool orb_minions   = true;
        inline bool restrict_orb  = true;

        // --- Combo: Q ---
        inline bool combo_use_q   = true;
        // ModeQ bits: 0 = Always, 1 = After Attack, 2 = Out of Range
        inline bool q_mode_always   = false;
        inline bool q_mode_after_aa = true;
        inline bool q_mode_oor      = true;
        inline float block_q_as     = 3.5f;

        // --- Combo: E ---
        inline bool combo_use_e          = true;
        inline bool allow_after_aa_e     = false;
        inline bool slow_e               = true;
        inline bool killsteal_e          = true;
        inline bool auto_e_before_expire = true;
        inline int  e_expire_min_stacks  = 3;     // 1..10
        inline int  damage_draw_mode     = 0;     // 0 = Damage, 1 = Percent, 2 = Off

        // --- Auto Save R (Fate's Call) ---
        inline bool auto_save_r        = true;
        inline int  r_save_hp_pct      = 20;   // 5..50 %
        inline bool r_save_combo_only  = false; // only auto-save while in combo mode

        // --- Jump Trick (passive distance abuse) ---
        inline bool  jump_trick_enabled   = true;   // master enable
        inline bool  jump_trick_active    = false;  // current hotkey state
        // Fixed: minimum angle (deg) between hop direction and cursor direction
        // before the trick fires. Below this the trick doesn't extend the dash.
        constexpr float jump_trick_min_angle = 30.f;

        // --- Lane / Jungle Clear ---
        inline bool lane_clear_e         = false;
        inline int  lane_minions_amount  = 3;
        inline bool jungle_clear_e_epic  = true;
        inline bool jungle_clear_e_large = true;
        inline bool jungle_clear_e_small = false;

        // --- Drawings ---
        inline bool     draw_q           = false;
        inline uint32_t draw_q_color     = 0x7A088F8Fu;  // teal
        inline bool     draw_e           = false;
        inline uint32_t draw_e_color     = 0x7A6495EDu;  // cornflower
        inline bool     draw_stacks      = true;
        inline uint32_t draw_stacks_color = 0xFFFF8C00u; // orange
        inline bool     draw_kill_text   = true;
        inline uint32_t draw_kill_color  = 0xFF00E600u;  // bright green

        // --- E damage health bar overlay ---
        inline meowaio::draw_config draw_cfg = []() {
            meowaio::draw_config c;
            c.hp_mode       = meowaio::draw_config::MODE_VEN_PLUS;
            c.hp_enemies    = true;
            c.hp_monsters   = true;
            c.hp_minions    = true;
            c.glow_enemies  = true;
            c.glow_monsters = true;
            c.glow_minions  = true;
            c.hp_color      = 0xBB5D3FD3u;  // purple - has stacks, survives
            c.hp_kill_color = 0xBBD70040u;  // red - dies from E
            c.glow_color    = 0xFFD70040u;
            return c;
        }();
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load();
    void unload();

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_basic_attack( game_object* sender, game_object* target, spell_cast* cast );
    bool __stdcall  on_before_attack( orb_sdk::event_data* data );
    bool __stdcall  on_after_attack( orb_sdk::event_data* data );

    // ============================================================================
    // GAME MODE FUNCTIONS
    // ============================================================================
    void combo();
    void clear();

    // ============================================================================
    // HELPERS
    // ============================================================================
    bool is_spell_learned( int slot );
    bool is_spell_ready( int slot );
    bool has_enough_mana( int slot );
}
