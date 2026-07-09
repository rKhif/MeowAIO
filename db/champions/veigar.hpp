#pragma once

#include "../core/structs.hpp"
#include <cmath>

namespace venplus::db::champions
{

namespace veigar
{
    inline constexpr uint32_t BUFF_PASSIVE_STACKS    = 0u;
    inline constexpr uint32_t BUFF_E_CAGE            = 0u;
    inline constexpr uint32_t BUFF_E_STUN            = 0u;

    inline constexpr const char* BUFF_NAME_PASSIVE   = "veigarpassivestacks";
    inline constexpr const char* BUFF_NAME_E_CAGE    = "veigar_base_e_cage";
    inline constexpr const char* BUFF_NAME_E_STUN    = "VeigarEventHorizon";

    inline constexpr float Q_RANGE               = 1050.f;
    inline constexpr float Q_WIDTH               = 140.f;
    inline constexpr float Q_SPEED               = 2200.f;
    inline constexpr float Q_CAST_TIME           = 0.25f;
    inline constexpr int   Q_MAX_TARGETS         = 2;

    inline constexpr float W_RANGE               = 950.f;
    inline constexpr float W_RADIUS              = 240.f;
    inline constexpr float W_DELAY               = 1.221f;
    inline constexpr float W_CAST_TIME           = 0.25f;
    inline constexpr float W_BASE_CD             = 8.f;

    inline constexpr float E_RANGE               = 725.f;
    inline constexpr float E_OUTER_RADIUS        = 411.f;
    inline constexpr float E_INNER_RADIUS        = 290.f;
    inline constexpr float E_EDGE_MIDPOINT       = 350.f;
    inline constexpr float E_EDGE_WIDTH          = 121.f;
    inline constexpr float E_FORMATION_DELAY     = 0.5f;
    inline constexpr float E_CAST_TIME           = 0.25f;
    inline constexpr float E_TOTAL_DELAY         = 0.75f;
    inline constexpr float E_CAGE_DURATION       = 3.f;
    inline constexpr float E_RESTUN_IMMUNITY     = 4.5f;

    inline constexpr float R_RANGE               = 650.f;
    inline constexpr float R_CAST_TIME           = 0.25f;
    inline constexpr float R_MISSILE_SPEED       = 2000.f;
    inline constexpr float R_MISSING_HP_CAP      = 0.6667f;
    inline constexpr float R_MISSING_HP_SCALE    = 1.5f;

    inline float w_cooldown( int stacks )
    {
        float exponent = std::floor( static_cast< float >( stacks ) / 50.f );
        return W_BASE_CD * std::pow( 0.9f, exponent );
    }

    inline float r_damage( int rank, float ap, float target_missing_hp_pct )
    {
        float base_min[] = { 175.f, 250.f, 325.f };
        float ratio_min[] = { 0.65f, 0.70f, 0.75f };
        int idx = ( std::min )( rank - 1, 2 );
        if ( idx < 0 ) idx = 0;

        float base = base_min[ idx ] + ratio_min[ idx ] * ap;
        float bonus = ( std::min )( R_MISSING_HP_SCALE * target_missing_hp_pct, 1.f );
        return base * ( 1.f + bonus );
    }

    inline float r_kill_threshold_fraction( int rank, float ap, float target_max_hp, float effective_mr )
    {
        float base_min[] = { 175.f, 250.f, 325.f };
        float ratio_min[] = { 0.65f, 0.70f, 0.75f };
        int idx = ( std::min )( rank - 1, 2 );
        if ( idx < 0 ) idx = 0;

        float base = base_min[ idx ] + ratio_min[ idx ] * ap;
        float mitigated = base * ( 100.f / ( 100.f + effective_mr ) );
        return ( 2.5f * mitigated ) / ( target_max_hp + 1.5f * mitigated );
    }
}

inline constexpr champion_db VEIGAR = {
    .meta = {
        .id       = 45,
        .name     = "Veigar",
        .display  = "Veigar",
        .title    = "The Tiny Master of Evil",
        .resource = resource_type::mana,
        .aa_range = 550.f,
    },

    .stats = {
        .hp_base          = 580.f,
        .hp_per_level     = 124.93f,
        .hp_regen_base    = 6.5f,
        .hp_regen_per_lvl = 0.694f,

        .resource         = resource_type::mana,
        .mp_base          = 490.f,
        .mp_per_level     = 30.07f,
        .mp_regen_base    = 8.f,
        .mp_regen_per_lvl = 0.925f,

        .ad_base          = 52.f,
        .ad_per_level     = 3.124f,
        .as_base          = 0.625f,
        .as_ratio         = 0.625f,
        .as_per_level     = 2.59f,
        .aa_range         = 550.f,

        .armor_base       = 18.f,
        .armor_per_level  = 6.01f,
        .mr_base          = 32.f,
        .mr_per_level     = 1.503f,

        .ms_base          = 340.f,

        .aa_missile_speed = 1500.f,
        .windup_percent   = 0.19094f,
        .gameplay_radius  = 55.f,
        .pathing_radius   = 30.f,
    },

    .spells =
    { {
            .spell_slot = slot::Q,
            .geometry = {
                .type       = spell_type::linear,
                .range      = 1050.f,
                .radius     = 70.f,
                .speed      = 2200.f,
                .delay      = 0.25f,
                .collision  = true,
                .fixed_range = true,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 80.f, 120.f, 160.f, 200.f, 240.f },
                .ap_ratio = { 0.50f, 0.55f, 0.60f, 0.65f, 0.70f },
            },
            .cooldowns = {
                .base = { 6.f, 5.5f, 5.f, 4.5f, 4.f },
                .cost = { 30.f, 35.f, 40.f, 45.f, 50.f },
            },
            .name    = "VeigarBalefulStrike",
            .display = "Baleful Strike",
        },
        {
            .spell_slot = slot::W,
            .geometry = {
                .type       = spell_type::circular,
                .range      = 950.f,
                .radius     = 240.f,
                .speed      = FLT_MAX,
                .delay      = 0.25f,
                .proc_delay = 1.221f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 85.f, 140.f, 195.f, 250.f, 305.f },
                .ap_ratio = { 0.70f, 0.80f, 0.90f, 1.00f, 1.10f },
            },
            .cooldowns = {
                .base = { 8.f, 8.f, 8.f, 8.f, 8.f },
                .cost = { 60.f, 65.f, 70.f, 75.f, 80.f },
            },
            .name    = "VeigarDarkMatter",
            .display = "Dark Matter",
            .severity_rating = 3,
        },
        {
            .spell_slot = slot::E,
            .geometry = {
                .type       = spell_type::ring,
                .range      = 725.f,
                .radius     = 411.f,
                .speed      = FLT_MAX,
                .delay      = 0.25f,
                .proc_delay = 0.5f,
                .inner_r    = 290.f,
            },
            .damage = {
                .type = damage_type::magic,
            },
            .cooldowns = {
                .base = { 20.f, 18.5f, 17.f, 15.5f, 14.f },
                .cost = { 70.f, 75.f, 80.f, 85.f, 90.f },
            },
            .name    = "VeigarEventHorizon",
            .display = "Event Horizon",
            .cc = cc_type::stun,
            .cc_duration = 2.0f,
            .severity_rating = 5,
        },
        {
            .spell_slot = slot::R,
            .geometry = {
                .type  = spell_type::targeted,
                .range = 650.f,
                .speed = 2000.f,
                .delay = 0.25f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 175.f, 250.f, 325.f },
                .ap_ratio = { 0.65f, 0.70f, 0.75f },
            },
            .cooldowns = {
                .base = { 100.f, 80.f, 60.f },
                .cost = { 100.f, 100.f, 100.f },
            },
            .name    = "VeigarPrimordialBurst",
            .display = "Primordial Burst",
        },
    },

    .buffs = {
        { "veigarpassivestacks", veigar::BUFF_PASSIVE_STACKS, 0.f, false, false },
        { "veigar_base_e_cage", veigar::BUFF_E_CAGE,          0.f, false, false },
        { "VeigarEventHorizon", veigar::BUFF_E_STUN,          0.f, false, false },
    },
    .buff_count = 3,
};

} // namespace venplus::db::champions
