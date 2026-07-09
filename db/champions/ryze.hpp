#pragma once

#include "../core/structs.hpp"

namespace venplus::db::champions
{

namespace ryze_const
{
    // Buff hashes (0u = placeholder — verify in-game)
    inline constexpr uint32_t BUFF_FLUX             = 0u;  // "RyzeE" on enemies
    inline constexpr uint32_t BUFF_PASSIVE_STACK    = 0u;  // "ryzepassivestack" on self
    inline constexpr uint32_t BUFF_Q_FULL_CHARGE    = 0u;  // "ryzeqiconfullcharge" on self

    inline constexpr const char* BUFF_NAME_FLUX          = "RyzeE";
    inline constexpr const char* BUFF_NAME_PASSIVE_STACK = "ryzepassivestack";
    inline constexpr const char* BUFF_NAME_Q_FULL_CHARGE = "ryzeqiconfullcharge";

    // Q
    inline constexpr float Q_RANGE              = 1000.f;
    inline constexpr float Q_WIDTH              = 110.f;
    inline constexpr float Q_RADIUS             = 55.f;
    inline constexpr float Q_SPEED              = 1700.f;
    inline constexpr float Q_DELAY              = 0.25f;

    // Q flux bonus damage multiplier (by R rank: 0/1/2/3)
    inline constexpr float Q_FLUX_BONUS[]       = { 0.25f, 0.50f, 0.75f, 1.00f };

    // Q bonus mana ratio per rank (flat 2% all ranks)
    inline constexpr float Q_BONUS_MANA_RATIO   = 0.02f;

    // W
    inline constexpr float W_RANGE              = 550.f;
    inline constexpr float W_DELAY              = 0.25f;
    inline constexpr float W_BONUS_MANA_RATIO   = 0.03f;

    // E
    inline constexpr float E_RANGE              = 550.f;
    inline constexpr float E_DELAY              = 0.25f;
    inline constexpr float E_SPREAD_RADIUS      = 350.f;
    inline constexpr float E_BONUS_MANA_RATIO   = 0.02f;

    // Flux
    inline constexpr float FLUX_DURATION        = 4.f;

    // Passive rune stacks
    inline constexpr int   MAX_RUNE_STACKS      = 2;
    inline constexpr float RUNE_STACK_DURATION   = 4.f;

    // Q movespeed at 2 stacks
    inline constexpr float Q_MS_DURATION        = 2.f;
}

inline constexpr champion_db RYZE = {
    .meta = {
        .id       = 13,
        .name     = "Ryze",
        .display  = "Ryze",
        .title    = "The Rune Mage",
        .resource = resource_type::mana,
        .aa_range = 550.f,
    },

    .stats = {
        .hp_base          = 645.f,
        .hp_per_level     = 124.f,
        .hp_regen_base    = 8.f,
        .hp_regen_per_lvl = 0.8f,

        .resource         = resource_type::mana,
        .mp_base          = 300.f,
        .mp_per_level     = 70.f,
        .mp_regen_base    = 8.f,
        .mp_regen_per_lvl = 1.f,

        .ad_base          = 58.f,
        .ad_per_level     = 3.f,
        .as_base          = 0.658f,
        .as_ratio         = 0.625f,
        .as_per_level     = 2.11f,
        .aa_range         = 550.f,

        .armor_base       = 22.f,
        .armor_per_level  = 4.2f,
        .mr_base          = 32.f,
        .mr_per_level     = 1.3f,

        .ms_base          = 340.f,

        .aa_missile_speed = 2400.f,
        .windup_percent   = 0.2f,
        .gameplay_radius  = 65.f,
        .pathing_radius   = 35.f,
    },

    .spells =
    { {
            .spell_slot = slot::Q,
            .geometry = {
                .type      = spell_type::linear,
                .range     = 1000.f,
                .radius    = 55.f,
                .speed     = 1700.f,
                .delay     = 0.25f,
                .collision = true,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 75.f, 95.f, 115.f, 135.f, 155.f },
                .ap_ratio = { 0.55f, 0.55f, 0.55f, 0.55f, 0.55f },
            },
            .cooldowns = {
                .base = { 5.f, 5.f, 5.f, 5.f, 5.f },
                .cost = { 40.f, 38.f, 36.f, 34.f, 32.f },
            },
            .name    = "RyzeQ",
            .display = "Overload",
        },
        {
            .spell_slot = slot::W,
            .geometry = {
                .type  = spell_type::targeted,
                .range = 550.f,
                .delay = 0.25f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 60.f, 90.f, 120.f, 150.f, 180.f },
                .ap_ratio = { 0.60f, 0.60f, 0.60f, 0.60f, 0.60f },
            },
            .cooldowns = {
                .base = { 11.f, 10.5f, 10.f, 9.5f, 9.f },
                .cost = { 50.f, 60.f, 70.f, 80.f, 90.f },
            },
            .name    = "RyzeW",
            .display = "Rune Prison",
        },
        {
            .spell_slot = slot::E,
            .geometry = {
                .type  = spell_type::targeted,
                .range = 550.f,
                .speed = 4000.f,
                .delay = 0.25f,
            },
            .damage = {
                .type     = damage_type::magic,
                .base     = { 60.f, 90.f, 120.f, 150.f, 180.f },
                .ap_ratio = { 0.50f, 0.50f, 0.50f, 0.50f, 0.50f },
            },
            .cooldowns = {
                .base = { 3.5f, 3.25f, 3.f, 2.75f, 2.5f },
                .cost = { 35.f, 45.f, 55.f, 65.f, 75.f },
            },
            .name    = "RyzeE",
            .display = "Spell Flux",
        },
        {
            .spell_slot = slot::R,
            .geometry = {
                .type  = spell_type::none,
                .range = 3000.f,
            },
            .cooldowns = {
                .base = { 180.f, 160.f, 140.f },
                .cost = { 100.f, 100.f, 100.f },
            },
            .name    = "RyzeR",
            .display = "Realm Warp",
        },
    },

    .buffs = {
        { "RyzeE",               ryze_const::BUFF_FLUX,          4.f,  false, false },
        { "ryzepassivestack",    ryze_const::BUFF_PASSIVE_STACK, 4.f,  false, false },
        { "ryzeqiconfullcharge", ryze_const::BUFF_Q_FULL_CHARGE, 0.f,  false, false },
    },
    .buff_count = 3,
};

} // namespace venplus::db::champions
