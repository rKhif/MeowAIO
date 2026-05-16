#pragma once

#include "sdk.hpp"
#include <cfloat>
#include <string>

namespace ashe
{
    // ============================================================================
    // SPELL SLOT CONSTANTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — values aligned with venplus db::champions::ashe
    // ============================================================================
    namespace spell_data
    {
        // Q — Ranger's Focus (self-cast, requires 4 Focus stacks from AAs)
        constexpr float q_duration       = 6.f;

        // W — Volley (cone, 7-11 arrows depending on rank)
        constexpr float w_range          = 1200.f;
        constexpr float w_radius         = 50.f;     // pred half-radius (linear approx of cone)
        constexpr float w_speed          = 2000.f;
        constexpr float w_delay          = 0.25f;
        // Half-angle (degrees) per W rank — index = rank, [0] is unused
        constexpr float w_half_angle_deg[6] = { 0.f, 13.875f, 18.5f, 18.5f, 23.125f, 23.125f };

        // R — Enchanted Crystal Arrow (global, variable travel speed)
        constexpr float r_radius         = 130.f;
        constexpr float r_delay          = 0.25f;
        constexpr float r_base_speed     = 1500.f;
        constexpr float r_acceleration   = 200.f;
        constexpr float r_max_speed      = 2100.f;
        constexpr float r_accel_cap_time = 3.f;

        // R speed scales with distance — closer = slower, far = full speed
        inline float estimate_r_speed(float distance) noexcept
        {
            if (distance <= 0.f) return r_base_speed;
            constexpr float half_a   = r_acceleration * 0.5f;
            constexpr float d_at_cap = r_base_speed * r_accel_cap_time
                                     + half_a * r_accel_cap_time * r_accel_cap_time;
            float travel_time;
            if (distance <= d_at_cap) {
                travel_time = (-r_base_speed + std::sqrt(r_base_speed * r_base_speed
                              + 2.f * r_acceleration * distance)) / r_acceleration;
            } else {
                const float remaining = distance - d_at_cap;
                travel_time = r_accel_cap_time + remaining / r_max_speed;
            }
            if (travel_time <= 0.f) return r_base_speed;
            return distance / travel_time;
        }

        // Buff hashes
        constexpr uint32_t buff_hash_q_active = 0x40A3FA4Du; // AsheQ
        constexpr uint32_t buff_hash_q_stacks = 0xD05B60B5u; // AsheQAttack
        constexpr uint32_t buff_hash_frost    = 0x3639871Au; // ashepassiveslow
    }

    // ============================================================================
    // MENU VARIABLES
    // ============================================================================
    namespace menu
    {
        // --- Combo ---
        inline bool combo_use_q       = true;
        inline bool combo_use_w       = true;
        inline bool combo_use_r       = true;
        inline bool combo_r_only_kill = true;
        inline int  combo_w_hitchance = pred_sdk::hitchance::high;
        inline int  combo_r_hitchance = pred_sdk::hitchance::very_high;

        // --- Harass ---
        inline bool harass_use_q      = false;
        inline bool harass_use_w      = true;
        inline int  harass_mana_pct   = 30;

        // --- Clear ---
        inline bool clear_use_w       = true;
        inline int  clear_mana_pct    = 30;

        // --- Jungle ---
        inline bool jungle_use_w      = true;
        inline int  jungle_mana_pct   = 20;

        // --- Semi-R ---
        inline bool        semi_r_active     = false;
        inline int         semi_r_max_range  = 3500;
        inline std::string semi_r_key_str;
        inline uint32_t    infotab_semi_r    = 0;

        // --- Drawings ---
        inline bool draw_w_range      = true;
        inline bool draw_aa_range     = false;
        inline bool draw_q_status     = true;
        inline bool draw_damage_bar   = true;
        inline bool draw_r_dmg_bar    = true;
        inline bool draw_killable_txt = true;
        inline bool draw_only_ready   = false;

        // --- Colors (ARGB) ---
        inline uint32_t color_w       = 0xFF80FFFFu; // light cyan
        inline uint32_t color_aa      = 0xFF00FF00u; // green
        inline uint32_t color_r       = 0xFF00E600u; // bright green (R KILL text)
        inline uint32_t color_r_dmg   = 0x9000E600u; // bright green w/ alpha (R dmg bar)
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
    // VALIDATION HELPERS
    // ============================================================================
    bool is_valid_target(game_object* unit, float range = FLT_MAX);
    bool is_spell_learned(int slot);
    bool is_spell_ready(int slot);
    bool has_enough_mana(int slot);
    bool is_casting();
    float mana_pct();
}
