#pragma once

#include "sdk.hpp"
#include "champion_drawings.hpp"
#include <cstdint>
#include <string>

namespace twitch
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
        // Q – Ambush (self-cast stealth)
        constexpr float q_detect_range = 500.f;

        // W – Venom Cask (circular projectile)
        constexpr float w_range  = 950.f;
        constexpr float w_radius = 150.f;
        constexpr float w_speed  = 1400.f;
        constexpr float w_delay  = 0.25f;

        // E – Contaminate (global AOE self-cast, applies around player)
        constexpr float e_range = 1200.f;

        // R – Spray and Pray (enhances AA range)
        constexpr float r_bonus_range = 300.f;

        // Buff hashes
        constexpr uint32_t buff_venom_stacks = 0x7F7378F4u; // TwitchDeadlyVenom
        constexpr uint32_t buff_q_stealth    = 0x39190976u; // TwitchHideInShadows
        constexpr uint32_t buff_q_as         = 0xA8076683u; // twitchhideinshadowsbuff
        constexpr uint32_t buff_r_active     = 0x936A439Eu; // TwitchFullAutomatic
    }

    // E damage per-stack base damage (fallback, by rank 0-4)
    constexpr float E_STACK_DMG[5] = { 15.f, 20.f, 25.f, 30.f, 35.f };

    // Passive true-damage DPS per stack (by level band)
    constexpr float PASSIVE_DMG[5]    = { 1.f, 2.f, 3.f, 4.f, 5.f };
    constexpr float PASSIVE_AP_RATIO  = 0.03f;

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo: Q ---
        inline bool combo_use_q         = false;
        inline bool combo_q_auto_kill   = false;

        // --- Combo: W ---
        inline bool combo_use_w              = true;
        inline int  combo_w_mode             = 0;   // 0 = After AA, 1 = Always
        inline bool combo_w_block_r          = true;
        inline int  combo_w_skip_if_killable = 3;
        inline float combo_w_as_cap          = 2.0f;
        inline bool w_cycle_held             = false;

        // --- Combo: E ---
        inline bool combo_use_e      = true;
        inline int  combo_e_mode     = 1;   // 0 = Always, 1 = Smart
        inline bool combo_save_mana_e = false;

        // --- Combo: R ---
        inline bool combo_use_r  = true;
        inline int  combo_r_mode = 1;   // 0 = Always, 1 = Smart

        // --- Harass ---
        inline bool  harass_use_w   = true;
        inline float harass_mana_pct = 0.30f;

        // --- Lane Clear ---
        inline bool  lane_use_w      = true;
        inline int   lane_w_min      = 3;
        inline float lane_w_mana_pct = 0.30f;
        inline bool  lane_use_e      = false;
        inline int   lane_e_min_kills = 3;
        inline float lane_e_mana_pct  = 0.30f;

        // --- Lasthit ---
        inline bool lasthit_e_cannon = false;

        // --- Jungle ---
        inline bool jungle_use_q      = true;
        inline bool jungle_use_w      = true;
        inline bool jungle_e_buff     = true;
        inline bool jungle_e_dragon   = true;
        inline bool jungle_e_baron    = true;
        inline bool jungle_e_herald   = true;
        inline bool jungle_e_voidgrubs = true;

        // --- Flee ---
        inline bool flee_use_w = true;

        // --- Range Drawings ---
        inline bool     draw_q_detect        = true;
        inline uint32_t draw_q_detect_color  = 0x40FF4444u;
        inline int      draw_q_timer_mode    = 1;    // 0=OFF, 1=Text, 2=Bar
        inline bool     draw_w               = true;
        inline uint32_t draw_w_color         = 0xFF00FF00u;
        inline bool     draw_e               = true;
        inline uint32_t draw_e_color         = 0xFFFF4444u;
        inline bool     draw_r               = true;
        inline uint32_t draw_r_color         = 0xFFFF8800u;

        // --- E Damage Bar / Glow (VEN+ style draw_config) ---
        inline meowaio::draw_config draw_cfg = []() {
            meowaio::draw_config c;
            c.hp_mode       = meowaio::draw_config::MODE_VEN_PLUS;
            c.hp_enemies    = true;
            c.hp_monsters   = true;
            c.hp_minions    = true;
            c.glow_enemies  = true;
            c.glow_monsters = true;
            c.glow_minions  = true;
            c.hp_color      = 0xBBAA44FFu; // purple — has stacks, survives
            c.hp_kill_color = 0xBBFF2222u; // red — dies from E
            c.glow_color    = 0xFFFF2222u; // red glow on lethal targets
            return c;
        }();

        // --- Venom Stack Counter (text above target) ---
        inline bool     draw_stacks       = true;
        inline uint32_t draw_stacks_color = 0xFFFFFFFFu;
        inline float    draw_stacks_size  = 14.f;
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
    void __fastcall on_buff_loss(game_object* target, buff_instance* buff);
    bool __stdcall  on_after_attack(orb_sdk::event_data* data);

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
    // HELPERS
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    float mana_pct();
}
