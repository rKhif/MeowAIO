#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace kayle
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    namespace spell_data
    {
        // Q - Radiant Blast (linear skillshot, slows + damages)
        constexpr float q_range  = 900.f;
        constexpr float q_radius = 80.f;
        constexpr float q_delay  = 0.25f;
        constexpr float q_speed  = 1200.f;

        // W - Celestial Blessing (targeted heal + ms on self/ally)
        constexpr float w_range = 900.f;

        // E - Starfire Spellblade
        // Passive: bonus magic damage on AAs (scales with missing HP, true dmg at lvl 16).
        // Active: next AA is ranged 525, deals bonus dmg.
        constexpr float e_active_range = 525.f;

        // R - Divine Judgment (targeted ally invuln + AoE damage at end)
        constexpr float r_range  = 900.f;
        constexpr float r_radius = 575.f;
    }

    namespace menu
    {
        // Combo
        inline bool combo_q          = true;
        inline bool combo_e          = true;
        inline bool combo_e_extend   = true;   // use E to extend AA range when out of melee

        // Harass
        inline bool combo_q_harass   = true;
        inline bool combo_e_harass   = false;
        inline int  harass_mana_pct  = 35;

        // Lane / Jungle
        inline bool lane_q           = true;
        inline int  lane_q_min_hit   = 3;
        inline bool lane_e_cannon    = true;
        inline int  lane_mana_pct    = 40;
        inline bool jungle_q         = true;
        inline bool jungle_e         = true;

        // Auto-Q on CC / dashing
        inline bool auto_q_cc        = true;
        inline bool auto_q_dash      = true;

        // Killsteal
        inline bool ks_q             = true;
        inline bool ks_e             = true;

        // Auto W heal
        inline bool auto_w_self      = true;
        inline int  auto_w_self_hp   = 45;
        inline bool auto_w_ally      = true;
        inline int  auto_w_ally_hp   = 35;

        // Auto R
        inline bool auto_r_self      = true;
        inline int  auto_r_self_hp   = 18;
        inline bool auto_r_ally      = true;
        inline int  auto_r_ally_hp   = 14;
        inline int  auto_r_min_enemies = 1;
        inline bool semi_r_active    = false;
        inline std::string semi_r_key;
        inline uint32_t    infotab_semi_r = 0;

        // Per-ally R whitelist (network id -> enabled). Default-true when absent.
        inline std::unordered_map<uint32_t, bool> r_on_ally;

        // Flee
        inline bool flee_w           = true;

        // Hitchances
        inline int q_hitchance       = 70;

        // Drawings
        inline bool draw_q           = true;
        inline bool draw_e           = true;
        inline bool draw_r           = true;
        inline bool draw_damage      = true;
        inline bool draw_only_ready  = false;

        // Colors (ARGB)
        inline uint32_t color_q      = 0xFFDDCC66u;
        inline uint32_t color_e      = 0xFFFFAA00u;
        inline uint32_t color_r      = 0xFFFF4444u;
    }

    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
}
