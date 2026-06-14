#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <string>

namespace hwei
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // Raw spell constants cross-referenced against Data.cpp.
    namespace spell_data
    {
        // Q-subject (Disaster)
        constexpr float qq_range  = 900.f;
        constexpr float qq_delay  = 0.25f;
        constexpr float qq_radius = 70.f;
        constexpr float qq_speed  = 2000.f;

        constexpr float qw_range  = 1900.f;
        constexpr float qw_delay  = 1.0f;
        constexpr float qw_radius = 225.f;

        constexpr float qe_range  = 1250.f;
        constexpr float qe_delay  = 0.60f; // path erupts 0.6s after cast (0.35 windup is only the lock animation)
        constexpr float qe_radius = 110.f;
        constexpr float qe_speed  = 1500.f; // shockwaves march outward from the cast point

        // W-subject (Serenity)
        constexpr float wq_range  = 1200.f;
        constexpr float wq_delay  = 0.25f;
        constexpr float wq_radius = 145.f;
        constexpr float wq_speed  = 1700.f;

        // WW (Stirring Lights) - self-centered ally heal
        constexpr float ww_range = 650.f;

        // E-subject (Torment)
        constexpr float eq_range  = 1100.f;
        constexpr float eq_delay  = 0.25f;
        constexpr float eq_radius = 70.f;
        constexpr float eq_speed  = 1300.f;

        constexpr float ew_range  = 900.f;
        constexpr float ew_delay  = 0.95f; // 0.25s cast + 0.7s until the eye locks on and launches
        constexpr float ew_radius = 350.f;
        constexpr float ew_speed  = 1700.f;

        constexpr float ee_range  = 800.f;
        constexpr float ee_delay  = 0.625f;
        constexpr float ee_radius = 120.f;

        // R
        constexpr float r_range  = 1300.f;
        constexpr float r_delay  = 0.25f;
        constexpr float r_radius = 90.f;
        constexpr float r_speed  = 1400.f;
    }

    enum class combo_mode : int
    {
        basic = 0,
        qe_qw,
        ew_qw,
        ew_r_qw,
        all_in_burst,
        teamfight_all_in,
    };

    namespace menu
    {
        // Combo
        inline int  combo_mode_sel = 0;
        inline bool draw_combo_mode_indicator = true;
        inline bool combo_mode_cycle_enabled  = true;

        // Per-mode instant binds (held = force that mode)
        inline bool bind_mode_basic_active      = false;
        inline bool bind_mode_qeqw_active       = false;
        inline bool bind_mode_ewqw_active       = false;
        inline bool bind_mode_ewrqw_active      = false;
        inline bool bind_mode_burst_active      = false;
        inline bool bind_mode_teamfight_active  = false;

        inline int combo_qq_hitchance = pred_sdk::hitchance::high;
        inline int combo_qw_hitchance = pred_sdk::hitchance::high;
        inline int combo_qe_hitchance = pred_sdk::hitchance::high;
        inline int combo_eq_hitchance = pred_sdk::hitchance::high;
        inline int combo_ee_hitchance = pred_sdk::hitchance::medium;
        inline int combo_r_hitchance  = pred_sdk::hitchance::high;

        // Killsteal
        inline bool killsteal_qq = false;
        inline bool killsteal_qe = false;
        inline bool killsteal_qw = true;
        inline bool killsteal_eq = false;
        inline bool killsteal_r  = false;

        // Auto-cast on hard CC
        inline bool auto_qw_on_cc = false;
        inline int  auto_qw_cc_min_missing = 30;
        inline bool auto_r_on_cc  = false;

        // Channel interrupter (Torment CC)
        inline bool interrupt_eq = true;
        inline bool interrupt_ee = false;

        // Debug
        inline bool debug_logs = false;

        // Harass
        inline bool harass_use_qq = true;
        inline bool harass_use_qw = true;
        inline bool harass_use_qe = true;
        inline bool harass_use_eq = true;
        inline bool harass_use_we = true;
        inline int  harass_mana_pct = 55;

        // Flee
        inline bool flee_use_wq = true;

        // WW heal (Stirring Lights)
        inline bool use_ww         = true;
        inline bool use_ww_self    = true;
        inline int  ww_self_hp_pct = 45;
        inline int  ww_ally_hp_pct = 40;

        // Lane Clear
        inline bool lane_use_qq = true;
        inline bool lane_use_qe = true;
        inline int  lane_qe_min_minions = 3;
        inline int  lane_mana_pct = 45;

        // Jungle Clear
        inline bool jungle_use_qq = true;
        inline bool jungle_use_qe = true;
        inline int  jungle_qe_min_monsters = 2;

        // Semi-manual keys
        inline bool bind_qw_active = false;
        inline bool bind_we_active = false;
        inline bool bind_ee_active = false;
        inline bool bind_eq_active = false;
        inline bool bind_ew_active = false;
        inline bool bind_r_active  = false;

        // Drawing
        inline bool draw_qq = true;
        inline bool draw_qw = true;
        inline bool draw_qe = true;
        inline bool draw_eq = true;
        inline bool draw_ew = true;
        inline bool draw_ee = false;
        inline bool draw_r  = true;
        inline bool draw_qw_minimap = false;
        inline bool draw_only_ready = false;
        inline bool draw_only_active_spell = false;
        inline bool draw_only_mode_ranges = false;
        inline bool draw_r_damage_indicator     = true;
        inline bool draw_combo_damage_indicator = true;

        // Colors (ARGB)
        inline uint32_t color_qq = 0xFFA080FFu;
        inline uint32_t color_qw = 0xFF70C0FFu;
        inline uint32_t color_qe = 0xFF40D0FFu;
        inline uint32_t color_eq = 0xFFFFA040u;
        inline uint32_t color_ew = 0xFFFFC060u;
        inline uint32_t color_ee = 0xFFE87C9Eu;
        inline uint32_t color_r  = 0xFFFF5050u;
        inline uint32_t color_r_dmg = 0xCCFF7A5Au;
    }

    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_present();
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
}
