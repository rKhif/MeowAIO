#pragma once
//  meowaio::db::champions — Jax: The Grandmaster at Arms
//
//  Data extracted from jax.md (wiki). Growth values use the standard
//  LoL formula: Stat(L) = Base + Growth × (L-1) × (0.7025 + 0.0175 × (L-1))
//  All values verified against L1–L20 endpoints in the source doc.

#include "../core/structs.hpp"
#include <cfloat>
#include <string_view>

namespace meowaio::db::champions
{


namespace jax_const
{
    // Stacks on-attack, up to 8. Each stack = 5%–14% bonus AS (based on level).
    // Formula: 5% + 1.5% per 3 levels, max at level 19.
    // Not tracked in DB (game handles the buff automatically).
    inline constexpr int   PASSIVE_MAX_STACKS = 8;
    inline constexpr float PASSIVE_STACK_DURATION = 2.5f;

    inline constexpr float Q_RANGE = 700.f;

    inline constexpr float W_BONUS_RANGE = 50.f;   // extra range on empowered AA
    inline constexpr float W_BUFF_DURATION = 10.f;
    inline constexpr float W_STRUCTURE_MODIFIER = 0.5f;   // 50% damage to structures

    inline constexpr float E_RADIUS = 375.f;
    inline constexpr float E_EVASION_DURATION = 2.f;
    inline constexpr float E_MIN_RECAST_DELAY = 1.f;
    inline constexpr float E_STUN_DURATION = 1.f;
    inline constexpr float E_AOE_DAMAGE_REDUCTION = 0.25f;   // 25% reduced AoE damage
    inline constexpr float E_DAMAGE_INCREASE_PER_DODGE = 0.20f; // 20% per dodge
    inline constexpr int   E_MAX_DODGE_BONUS_STACKS = 5;       // 100% max increase (5 × 20%)

    inline constexpr float R_RADIUS = 375.f;
    inline constexpr float R_CAST_TIME = 0.25f;
    inline constexpr float R_BUFF_DURATION = 8.f;
    inline constexpr float R_STRUCTURE_MODIFIER = 0.5f;    // passive proc 50% vs structures

    // R passive: empowered attack every 3rd hit (every 2nd while R active)
    inline constexpr int   R_PASSIVE_PROC_INTERVAL = 3;
    inline constexpr int   R_PASSIVE_PROC_INTERVAL_ACTIVE = 2;

    // R active: bonus resistances
    inline constexpr float R_BONUS_ARMOR[3] = { 25.f, 50.f, 75.f };
    inline constexpr float R_BONUS_ARMOR_AD[3] = { 0.40f, 0.40f, 0.40f };
    inline constexpr float R_BONUS_MR[3] = { 27.f, 36.f, 45.f };
    inline constexpr float R_BONUS_MR_AD[3] = { 0.24f, 0.24f, 0.24f };
    inline constexpr float R_ARMOR_PER_CHAMP[3] = { 20.f, 25.f, 30.f };
    inline constexpr float R_ARMOR_PER_CHAMP_AD[3] = { 0.10f, 0.10f, 0.10f };
    inline constexpr float R_MR_PER_CHAMP[3] = { 12.f, 15.f, 18.f };
    inline constexpr float R_MR_PER_CHAMP_AD[3] = { 0.06f, 0.06f, 0.06f };

    inline constexpr float AA_RANGE = 125.f;
    inline constexpr float AA_WINDUP_PERCENT = 0.20811f;

    inline constexpr float GAMEPLAY_RADIUS = 65.f;
    inline constexpr float PATHING_RADIUS = 35.f;

    inline constexpr int   WARD_TRINKET_ID = 3340;
    inline constexpr int   CONTROL_WARD_ID = 2055;
    inline constexpr int   FARSIGHT_ID = 3363;

    // Set to 0 = use name-based fallback detection until real hashes obtained.
    inline constexpr uint32_t BUFF_E_EVASION = 0u;  // JaxCounterStrike
    inline constexpr uint32_t BUFF_W_EMPOWER = 0u;  // JaxEmpowerTwo
    inline constexpr uint32_t BUFF_R_ACTIVE = 0u;  // JaxRelentlessAssault / GrandmastersMight

    // Buff name fragments for fallback detection (case-sensitive substring match)
    inline constexpr const char* BUFF_NAME_E = "CounterStrike";
    inline constexpr const char* BUFF_NAME_W = "Empower";
    inline constexpr const char* BUFF_NAME_R = "Grandmaster";
}


inline constexpr champion_db JAX = {
    .meta = {
        .id = 24,
        .name = "Jax",
        .display = "Jax",
        .title = "Grandmaster at Arms",
        .resource = resource_type::mana,
        .aa_range = 125.f,
        .is_melee = true,
    },

    .stats = {
        .hp_base = 650.f,
        .hp_per_level = 103.f,
        .hp_regen_base = 8.5f,
        .hp_regen_per_lvl = 0.55f,

        .resource = resource_type::mana,
        .mp_base = 339.f,
        .mp_per_level = 52.f,
        .mp_regen_base = 8.2f,
        .mp_regen_per_lvl = 0.7f,

        .ad_base = 68.f,
        .ad_per_level = 4.25f,
        .as_base = 0.638f,
        .as_ratio = 0.638f,     // doc: N/A -> defaults to base AS
        .as_per_level = 3.4f,
        .aa_range = 125.f,

        .armor_base = 36.f,
        .armor_per_level = 4.2f,
        .mr_base = 32.f,
        .mr_per_level = 2.05f,

        .ms_base = 350.f,
        .crit_dmg = 2.0f,

        .aa_missile_speed = 0.f,        // melee
        .windup_percent = 0.20811f,
        .gameplay_radius = 65.f,
        .pathing_radius = 35.f,
    },

    .spells = {
        // Targeted dash to any unit. Deals physical damage to enemies on arrival.
        {
            .spell_slot = slot::Q,
            .geometry = {
                .type = spell_type::targeted,
                .range = 700.f,
                .delay = 0.f,
            },
            .damage = {
                .type = damage_type::physical,
                .base = { 65.f, 105.f, 145.f, 185.f, 225.f },
                .bonus_ad = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f },
            },
            .cooldowns = {
                .base = { 8.f, 7.5f, 7.f, 6.5f, 6.f },
                .cost = { 65.f, 65.f, 65.f, 65.f, 65.f },
            },
            .name = "JaxLeapStrike",
            .display = "Leap Strike",
        },

        // Empowers next AA/Q with bonus magic damage. Resets AA timer.
        {
            .spell_slot = slot::W,
            .geometry = {
                .type = spell_type::self_cast,
                .range = 125.f,
                .delay = 0.f,
            },
            .damage = {
                .type = damage_type::magic,
                .base = { 50.f, 85.f, 120.f, 155.f, 190.f },
                .ap_ratio = { 0.6f, 0.6f, 0.6f, 0.6f, 0.6f },
            },
            .cooldowns = {
                .base = { 7.f, 6.f, 5.f, 4.f, 3.f },
                .cost = { 30.f, 30.f, 30.f, 30.f, 30.f },
            },
            .name = "JaxEmpowerTwo",
            .display = "Empower",
        },

        // Toggle: evasion for 2s, recast to stun.
        // Damage stored is MINIMUM (0 dodges). Multiply by (1 + 0.2 * dodges) in code.
        // Includes 3.5% target max HP component.
        {
            .spell_slot = slot::E,
            .geometry = {
                .type = spell_type::self_cast,
                .range = 375.f,
                .radius = 375.f,
                .delay = 0.f,
            },
            .damage = {
                .type = damage_type::magic,
                .base = { 40.f, 70.f, 100.f, 130.f, 160.f },
                .ap_ratio = { 0.7f, 0.7f, 0.7f, 0.7f, 0.7f },
                .max_hp = { 0.035f, 0.035f, 0.035f, 0.035f, 0.035f },
            },
            .cooldowns = {
                .base = { 17.f, 15.f, 13.f, 11.f, 9.f },
                .cost = { 50.f, 60.f, 70.f, 80.f, 90.f },
            },
            .name = "JaxCounterStrike",
            .display = "Counter Strike",
        },

        // Self-centered AoE, grants armor/MR on champion hit.
        {
            .spell_slot = slot::R,
            .geometry = {
                .type = spell_type::self_cast,
                .range = 375.f,
                .radius = 375.f,
                .delay = 0.25f,
            },
            .damage = {
                .type = damage_type::magic,
                .base = { 100.f, 175.f, 250.f },
                .ap_ratio = { 1.0f, 1.0f, 1.0f },
            },
            .cooldowns = {
                .base = { 100.f, 90.f, 80.f },
                .cost = { 100.f, 100.f, 100.f },
            },
            .name = "JaxGrandmaster",
            .display = "Grandmaster-at-Arms",
        },
    },

    .buffs = {
        { "JaxCounterStrike",      jax_const::BUFF_E_EVASION, 2.f,  false, false },
        { "JaxEmpowerTwo",         jax_const::BUFF_W_EMPOWER, 10.f, false, false },
        { "JaxGrandmastersMight",  jax_const::BUFF_R_ACTIVE,  8.f,  false, false },
    },
    .buff_count = 3,

    // R passive: 3rd-hit bonus magic damage (2nd during R active).
    .passives =
    { {
            .name = "JaxRPassive",
            .parent_slot = 3,
            .damage = {
                .type = damage_type::magic,
                .base = { 75.f, 130.f, 185.f },
                .ap_ratio = { 0.6f, 0.6f, 0.6f },
            },
            .proc_count = 1,
        },
    },
    .passive_count = 1,
};

} // namespace meowaio::db::champions

