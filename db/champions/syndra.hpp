#pragma once

#include "../core/structs.hpp"

namespace venplus::db::champions
{

namespace syndra
{
    // Buff hashes (verified in-game)
    inline constexpr uint32_t BUFF_PASSIVE_STACKS    = 0x9FCE5B0A;  // syndrapassivestacks (count = splinters)
    inline constexpr uint32_t BUFF_PASSIVE           = 0x423F78AB;  // SyndraPassive
    inline constexpr uint32_t BUFF_R_PASSIVE         = 0x95E87B61;  // SyndraRPassive
    inline constexpr uint32_t BUFF_R_VISUAL          = 0x65B25248;  // syndrarvisual
    inline constexpr uint32_t BUFF_Q_VISUAL_COUNTER  = 0xA612C0DF;  // SyndraQVisualCounter
    inline constexpr uint32_t BUFF_R_UPGRADE         = 0xAD6E5C21;  // syndrapassiverupgrade (execute at 100)

    inline constexpr int SPLINTER_Q_CHARGES   = 40;
    inline constexpr int SPLINTER_W_TRUE_DMG  = 60;
    inline constexpr int SPLINTER_E_WIDE_CONE = 80;
    inline constexpr int SPLINTER_R_EXECUTE   = 100;

    // Q
    inline constexpr float Q_SPHERE_DURATION  = 6.f;

    // W
    inline constexpr float W_GRAB_RANGE       = 925.f;
    inline constexpr float W_THROW_RANGE      = 950.f;
    // W Transcendent true damage (60 splinters): raw_w * (0.12 + 0.02 * AP/100)
    // Quadratic in AP — cannot be stored as flat damage_entry without losing accuracy.
    // Computed directly in syndra.cpp from W().damage + get_ability_power().

    // E
    inline constexpr float E_SPHERE_RANGE     = 800.f;
    inline constexpr float E_MAX_PUSH_RANGE   = 1200.f;
    inline constexpr float E_SPHERE_HIT_BONUS = 100.f;
    inline constexpr float E_BASE_ANGLE       = 56.f;
    inline constexpr float E_TRANSCENDENT_ANGLE = 84.f;

    // E push geometry
    inline constexpr float E_PUSH_SPEED       = 2000.f;
    inline constexpr float E_PUSH_RADIUS      = 100.f;
    inline constexpr float E_STUN_RANGE       = 1300.f;
    inline constexpr float E_CONE_SPEED       = 2500.f;
    inline constexpr float E_CONE_SPEED_FAST  = 1600.f;
    inline constexpr float E_CONE_SPEED_SLOW  = 1100.f;
    inline constexpr float E_CAST_DELAY       = 0.25f;

    // Q effective range (range + radius for edge hits)
    inline constexpr float Q_EFFECTIVE_RANGE  = 920.f;

    // W throw prediction
    inline constexpr float W_THROW_DELAY      = 0.25f;
    inline constexpr float W_THROW_SPEED      = 1450.f;
    inline constexpr float W_THROW_RADIUS     = 112.5f;

    // W transcendent true damage (60 splinters): raw_w * (0.12 + 0.02 * AP/100)
    inline constexpr float W_TRUE_DMG_BASE    = 0.12f;
    inline constexpr float W_TRUE_DMG_SCALE   = 0.02f;

    // R
    inline constexpr int   R_BASE_SPHERES     = 3;
    inline constexpr int   R_MAX_GRAB_SPHERES = 4;
    inline constexpr float R_EXECUTE_PCT      = 0.15f;
    inline constexpr float R_OVERKILL_FACTOR  = 1.05f;

    // Sphere tracking
    inline constexpr float Q_PROC_DELAY       = 0.6f;
    inline constexpr float MIN_SPHERE_DIST    = 400.f;

    // W state
    inline constexpr float W_HOLD_DURATION    = 5.f;
    inline constexpr float W_MIN_HOLD_TIME    = 0.5f;
    inline constexpr float W_FORCE_THROW_TIME = 4.5f;
    inline constexpr float W_CAST_COOLDOWN    = 0.25f;
    inline constexpr float SPHERE_MATCH_RADIUS = 200.f;

    // Spell name hashes for on_process_cast
    inline constexpr uint32_t SPELL_W_GRAB    = 0u;  // "syndraw" — verify in-game
    inline constexpr uint32_t SPELL_W_CAST    = 0u;  // "syndrawcast" — verify in-game
    inline constexpr const char* SPELL_NAME_W_GRAB = "syndraw";
    inline constexpr const char* SPELL_NAME_W_CAST = "syndrawcast";
    inline constexpr const char* BUFF_NAME_W       = "SyndraW";
}

inline constexpr champion_db SYNDRA = {
    .meta = {
        .id       = 134,
        .name     = "Syndra",
        .display  = "Syndra",
        .title    = "The Dark Sovereign",
        .resource = resource_type::mana,
        .aa_range = 550.f,
    },

    .stats = {
        .hp_base          = 563.f,
        .hp_per_level     = 115.68f,
        .hp_regen_base    = 6.5f,
        .hp_regen_per_lvl = 0.694f,

        .resource         = resource_type::mana,
        .mp_base          = 480.f,
        .mp_per_level     = 46.27f,
        .mp_regen_base    = 8.f,
        .mp_regen_per_lvl = 0.925f,

        .ad_base          = 54.f,
        .ad_per_level     = 3.355f,
        .as_base          = 0.658f,
        .as_ratio         = 0.625f,
        .as_per_level     = 2.314f,
        .aa_range         = 550.f,

        .armor_base       = 25.f,
        .armor_per_level  = 4.627f,
        .mr_base          = 30.f,
        .mr_per_level     = 1.503f,

        .ms_base          = 330.f,

        .aa_missile_speed = 1800.f,
        .windup_percent   = 0.1875f,
        .gameplay_radius  = 65.f,
        .pathing_radius   = 35.f,
    },

    .spells =
    { {
            .spell_slot = slot::Q,
            .geometry = {
                .type       = spell_type::circular,
                .range      = 800.f,
                .radius     = 105.f,
                .speed      = FLT_MAX,
                .proc_delay = 0.6f,
                .charges    = 1,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 80.f, 115.f, 150.f, 185.f, 220.f },
                .ap_ratio = { 0.65f, 0.65f, 0.65f, 0.65f, 0.65f },
            },
            .cooldowns = {
                .base = { 7.f, 7.f, 7.f, 7.f, 7.f },
                .cost = { 40.f, 45.f, 50.f, 55.f, 60.f },
            },
            .name    = "SyndraQ",
            .display = "Dark Sphere",
        },
        {
            .spell_slot = slot::W,
            .geometry = {
                .type   = spell_type::circular,
                .range  = 925.f,
                .radius = 112.5f,
                .speed  = 1450.f,
                .delay  = 0.25f,
                .range2 = 950.f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 70.f, 100.f, 130.f, 160.f, 190.f },
                .ap_ratio = { 0.65f, 0.65f, 0.65f, 0.65f, 0.65f },
            },
            .cooldowns = {
                .base = { 12.f, 11.f, 10.f, 9.f, 8.f },
                .cost = { 60.f, 70.f, 80.f, 90.f, 100.f },
            },
            .name    = "SyndraW",
            .display = "Force of Will",
            .cc = cc_type::slow,
            .cc_duration = 1.5f,
        },
        {
            .spell_slot = slot::E,
            .geometry = {
                .type    = spell_type::cone,
                .range   = 700.f,
                .radius  = 70.f,
                .speed   = 2500.f,
                .delay   = 0.25f,
                .angle   = 56.f,
                .range2  = 1200.f,
                .radius2 = 100.f,
                .speed2  = 2000.f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 60.f, 95.f, 130.f, 165.f, 200.f },
                .ap_ratio = { 0.6f, 0.6f, 0.6f, 0.6f, 0.6f },
            },
            .cooldowns = {
                .base = { 15.f, 15.f, 15.f, 15.f, 15.f },
                .cost = { 50.f, 50.f, 50.f, 50.f, 50.f },
            },
            .name    = "SyndraE",
            .display = "Scatter the Weak",
            .cc = cc_type::stun,
            .cc_duration = 1.5f,
        },
        {
            .spell_slot = slot::R,
            .geometry = {
                .type  = spell_type::targeted,
                .range = 675.f,
                .speed = 1100.f,
                .delay = 0.264f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 90.f, 135.f, 180.f },
                .ap_ratio = { 0.2f, 0.2f, 0.2f },
            },
            .cooldowns = {
                .base = { 120.f, 100.f, 80.f },
                .cost = { 100.f, 100.f, 100.f },
            },
            .name    = "SyndraR",
            .display = "Unleashed Power",
        },
    },

    .buffs = {
        { "syndrapassivestacks",    syndra::BUFF_PASSIVE_STACKS,   0.f,   false, false },
        { "SyndraPassive",          syndra::BUFF_PASSIVE,          0.f,   false, false },
        { "SyndraRPassive",         syndra::BUFF_R_PASSIVE,        0.f,   false, false },
        { "syndrapassiverupgrade",  syndra::BUFF_R_UPGRADE,        0.f,   false, false },
        { "syndrarvisual",          syndra::BUFF_R_VISUAL,         0.f,   false, false },
        { "SyndraQVisualCounter",   syndra::BUFF_Q_VISUAL_COUNTER, 0.f,   false, false },
    },
    .buff_count = 6,
};

} // namespace venplus::db::champions
