#pragma once

#include "sdk.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace viktor
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // Values cross-referenced against meow_shared/db/champions/viktor.hpp.
    namespace spell_data
    {
        // Q: Siphon Power — targeted single-target.
        constexpr float q_range = 715.f;
        constexpr float q_cast_time = 0.25f;

        // W: Gravity Fiel.
        constexpr float w_range = 800.f;
        constexpr float w_radius = 340.f;
        constexpr float w_activation_delay = 0.75f;
        constexpr float w_stun_duration    = 1.5f;

        // E: Death Ray 
        constexpr float e_range  = 1250.f;
        constexpr float e_first_click = 550.f;
        constexpr float e_beam_len = e_range - e_first_click;
        constexpr float e_radius = 45.f;
        constexpr float e_speed  = 1050.f;
        constexpr float e_delay  = 0.25f;
        constexpr float e_aftershock_delay = 1.0f;

        // R: Chaos Storm — circular AoE, instant placement.
        constexpr float r_range = 700.f;
        constexpr float r_cast_time = 0.25f;
        constexpr float r_radius = 325.f;
        constexpr float r_move_rate = 0.15f;
        constexpr float r_duration  = 6.5f;
        constexpr int   r_max_ticks = 6;
    }

    // Gates "is this prediction worth trusting?" — small deltas are noise.
    namespace prediction_constants
    {
        constexpr float min_hp_difference = 5.f;
    }

    namespace buffs
    {
        constexpr const char* DISCHARGE       = "viktorpowertransferreturn";
        constexpr const char* CHAOS_STORM     = "ViktorChaosStormTimer";
    }

    // Targets we should not commit a finisher into. Mirrors entries in
    // meow_shared/utility/defensive_buffs.hpp.
    namespace anti_burst
    {
        constexpr uint32_t KINDRED_R = 0xE9EDCF9Eu;
        constexpr uint32_t ZILEAN_R  = 0x9A270666u;
    }

    namespace menu
    {
        // Combo
        inline bool use_q_flee = true;
        inline bool use_q_combo = true;
        inline bool use_w_combo = true;
        inline bool use_w_flee = true;
        inline bool use_e_combo = true;
        inline bool use_r_combo = true;

        // Harass
        inline bool use_q_harass = true;
        inline bool use_e_harass = true;
        inline bool use_w_harass = false;
        inline int  harass_mana_percent = 50;

        // Farm / Lasthit
        inline bool lasthit_q = true;
        inline bool use_q_clear = true;
        inline bool use_e_farm = true;
        inline int  e_farm_min_minions = 3;
        inline bool use_farm_monsters = true;
        inline int  farm_mana_percent = 30;

        // Killsteal
        inline bool ks_q = true;
        inline bool ks_e = true;
        inline bool ks_r = true;

        // W
        inline int  w_hitchance = 70;
        inline int  min_enemies_for_w = 1;
        inline bool auto_w_on_cc = true;
        inline bool use_w_antigap = true;
        // Walkers below this approach speed don't trigger anti-gap W; dashers
        // always trigger regardless.
        inline constexpr float anti_gap_min_speed = 200.f;
        // Populated at load() with every enemy hero; default enabled.
        inline std::unordered_map<std::string, bool> w_antigap_whitelist;

        // E
        inline int  e_hitchance = 65;
        inline bool log_e_casts = false;

        // R
        inline bool r_only_killable = false;
        inline int  min_enemies_for_r = 1;
        inline bool auto_r_movement = true;

        // Semi-manual keys
        inline bool         semi_w_active = false;
        inline std::string* semi_w_key = nullptr;
        inline bool         semi_r_active = false;
        inline std::string* semi_r_key = nullptr;

        // Drawing
        inline bool draw_q_range = true;
        inline bool draw_w_range = true;
        inline bool draw_e_range = true;
        inline bool draw_r_range = true;

        inline bool draw_e_rgb = true;
        inline bool draw_only_ready = false;
        inline bool draw_r_killable = true;
        inline bool draw_r_damage_indicator = true;

        // Colors (ARGB)
        inline uint32_t color_q = 0xFF87CEEBu;
        inline uint32_t color_w = 0xFF9400D3u;
        inline uint32_t color_e = 0xFF00FF7Fu;
        inline uint32_t color_r = 0xFFFF4500u;
        inline uint32_t color_r_dmg = 0xCCFF1493u;

        // Infotab handles
        inline uint32_t infotab_semi_w = 0;
        inline uint32_t infotab_semi_r = 0;
    }

    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_present();
    void __fastcall on_create_object(game_object* object);
    void __fastcall on_delete_object(game_object* object);
    void __fastcall on_process_cast(game_object* object, spell_cast* cast);

    bool __stdcall on_before_attack(orb_sdk::event_data* data);

    bool is_valid_target(game_object* unit, float range = FLT_MAX);

    void combo();
    void harass();
    void lasthit();
    void clear();
    void killsteal();
    void auto_r();

    bool cast_q(game_object* target);
    bool cast_w(game_object* target, bool force = false);
    bool cast_e(game_object* target);
    bool cast_r(game_object* target, bool manual = false);
    void move_r_to_target(game_object* target);
    // Returns true only when a packet was actually sent — false if suppressed
    // by dead-zone / hysteresis / range clamping.
    bool move_r_to_pos(math::vector3 pos, float score = 0.f, bool force = false);
    void anti_gap_w();
    void auto_w_on_cc();
    // Fires E at enemies exiting stasis (Zhonya's, GA, Bard R).
    void stasis_e();
    // Returns the aim target whose beam direction hits the most in-range
    // enemies. Falls back to primary when only one is in range.
    game_object* get_best_e_aim_target(game_object* primary);

    float get_q_damage(game_object* target);
    float get_e_damage(game_object* target);
    float get_r_damage(game_object* target);
    // Total Viktor combo damage (every learned spell + one AA) — used to gate
    // R "is this actually killable" decisions.
    float get_combo_damage(game_object* target);

    bool          validate_r_instance();
    game_object* get_r_instance();

    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    bool is_casting();
}
