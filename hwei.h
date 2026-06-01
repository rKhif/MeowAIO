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
        constexpr float qe_delay  = 0.35f;
        constexpr float qe_radius = 110.f;
        constexpr float qe_speed  = 1500.f;

        // W-subject (Serenity)
        constexpr float wq_range  = 1200.f;
        constexpr float wq_delay  = 0.25f;
        constexpr float wq_radius = 145.f;
        constexpr float wq_speed  = 1700.f;

        constexpr float we_range  = 900.f;
        constexpr float we_delay  = 0.25f;
        constexpr float we_radius = 350.f;
        constexpr float we_speed  = 1700.f;

        // E-subject (Torment)
        constexpr float eq_range  = 1100.f;
        constexpr float eq_delay  = 0.25f;
        constexpr float eq_radius = 70.f;
        constexpr float eq_speed  = 1300.f;

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
    };

    namespace menu
    {
        // Combo
        inline int  combo_mode_sel = 0;
        inline bool combo_use_r  = true;
        inline bool draw_combo_mode_indicator = true;
        inline std::string combo_mode_cycle_key = "";

        inline int combo_qq_hitchance = pred_sdk::hitchance::high;
        inline int combo_qw_hitchance = pred_sdk::hitchance::high;
        inline int combo_qe_hitchance = pred_sdk::hitchance::high;
        inline int combo_eq_hitchance = pred_sdk::hitchance::high;
        inline int combo_ee_hitchance = pred_sdk::hitchance::medium;
        inline int combo_r_hitchance  = pred_sdk::hitchance::high;

        // Harass
        inline bool harass_use_qq = true;
        inline bool harass_use_qw = true;
        inline bool harass_use_qe = true;
        inline bool harass_use_we = true;
        inline int  harass_mana_pct = 55;

        // Flee
        inline bool flee_use_wq = true;

        // Lane Clear
        inline bool lane_use_qe = true;
        inline int  lane_qe_min_minions = 3;

        // Semi-manual keys
        inline bool bind_qw_active = false;
        inline bool bind_we_active = false;
        inline bool bind_ee_active = false;
        inline bool bind_r_active = false;
        inline std::string bind_qw_key = "G";
        inline std::string bind_we_key = "H";
        inline std::string bind_ee_key = "V";
        inline std::string bind_r_key  = "T";

        // Infotab
        inline uint32_t infotab_combo_mode = 0;

        // Drawing
        inline bool draw_qq = true;
        inline bool draw_qw = true;
        inline bool draw_qe = true;
        inline bool draw_eq = true;
        inline bool draw_ee = false;
        inline bool draw_r  = true;
        inline bool draw_only_ready = false;
        inline bool draw_only_active_spell = false;
        inline bool draw_r_damage_indicator = true;

        // Colors (ARGB)
        inline uint32_t color_qq = 0xFFA080FFu;
        inline uint32_t color_qw = 0xFF70C0FFu;
        inline uint32_t color_qe = 0xFF40D0FFu;
        inline uint32_t color_eq = 0xFFFFA040u;
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
}
