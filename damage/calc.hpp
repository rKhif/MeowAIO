#pragma once

#include "db/core/structs.hpp"
#include <algorithm>
#include <cmath>

namespace venplus::damage
{

inline constexpr int MAX_SPELL_RANKS = 6;

inline constexpr float resist_multiplier( float resist ) noexcept
{
    if ( resist >= 0.f )
        return 100.f / ( 100.f + resist );
    else
        return 2.f - 100.f / ( 100.f - resist );
}

struct pen_stats
{
    float pct_bonus_pen = 1.f; // 1.0 = no pen (SDK multiplier format)
    float pct_total_pen = 1.f; // 1.0 = no pen (SDK multiplier format)
    float flat_pen      = 0.f;
};

inline constexpr float effective_resist( float total_resist, float bonus_resist, const pen_stats& pen ) noexcept
{
    if ( total_resist <= 0.f )
        return total_resist;

    float base = total_resist - bonus_resist;

    // SDK pen values are multipliers: 1.0 = no pen, 0.65 = 35% pen
    float eff_base  = base         * pen.pct_total_pen;
    float eff_bonus = bonus_resist * pen.pct_bonus_pen * pen.pct_total_pen;

    return ( std::max )( 0.f, eff_base + eff_bonus - pen.flat_pen );
}

struct caster_stats
{
    float ap        = 0.f;
    float bonus_ad  = 0.f;
    float total_ad  = 0.f;
    float level     = 1.f;
};

struct target_info
{
    float max_hp      = 0.f;
    float curr_hp     = 0.f;
    float armor       = 0.f;
    float bonus_armor = 0.f;
    float mr          = 0.f;
    float bonus_mr    = 0.f;
};

inline constexpr float raw_spell_damage( const db::damage_entry& dmg, int rank, const caster_stats& caster ) noexcept
{
    if ( rank < 0 || rank >= MAX_SPELL_RANKS ) return 0.f;

    float total = dmg.base[ rank ];
    total += dmg.ap_ratio[ rank ]  * caster.ap;
    total += dmg.bonus_ad[ rank ]  * caster.bonus_ad;
    total += dmg.total_ad[ rank ]  * caster.total_ad;
    return total;
}

inline constexpr float raw_spell_damage_hp( const db::damage_entry& dmg, int rank, const caster_stats& caster, const target_info& target ) noexcept
{
    if ( rank < 0 || rank >= MAX_SPELL_RANKS ) return raw_spell_damage( dmg, rank, caster );

    float total = raw_spell_damage( dmg, rank, caster );
    total += dmg.max_hp[ rank ]  * target.max_hp;
    total += dmg.curr_hp[ rank ] * target.curr_hp;
    total += dmg.miss_hp[ rank ] * ( target.max_hp - target.curr_hp );
    return total;
}

inline constexpr float final_spell_damage( const db::damage_entry& dmg, int rank,
                                           const caster_stats& caster,
                                           const target_info& target,
                                           const pen_stats& armor_pen,
                                           const pen_stats& magic_pen ) noexcept
{
    float raw = raw_spell_damage_hp( dmg, rank, caster, target );

    switch ( dmg.type )
    {
        case db::damage_type::physical:
        {
            float eff = effective_resist( target.armor, target.bonus_armor, armor_pen );
            return raw * resist_multiplier( eff );
        }
        case db::damage_type::magic:
        {
            float eff = effective_resist( target.mr, target.bonus_mr, magic_pen );
            return raw * resist_multiplier( eff );
        }
        case db::damage_type::true_dmg:
            return raw;

        case db::damage_type::mixed:
        {
            float eff_ar = effective_resist( target.armor, target.bonus_armor, armor_pen );
            float eff_mr = effective_resist( target.mr,    target.bonus_mr,    magic_pen );
            return raw * resist_multiplier( ( std::min )( eff_ar, eff_mr ) );
        }
    }
    return raw;
}

inline constexpr float aa_damage( float total_ad,
                                  float total_armor, float bonus_armor,
                                  const pen_stats& pen,
                                  bool is_crit,
                                  float crit_multiplier ) noexcept
{
    float raw = total_ad;
    if ( is_crit ) raw *= crit_multiplier;
    float eff = effective_resist( total_armor, bonus_armor, pen );
    return raw * resist_multiplier( eff );
}

} // namespace venplus::damage
