#pragma once

#include "enums.hpp"
#include <cstdint>
#include <cstddef>
#include <string_view>

namespace venplus::db
{

struct spell_geometry
{
    spell_type type           = spell_type::none;
    float      range          = 0.f;
    float      radius         = 0.f;
    float      speed          = 0.f;
    float      delay          = 0.f;
    float      proc_delay     = 0.f;
    float      missile_w      = 0.f;
    float      angle          = 0.f;
    float      inner_r        = 0.f;
    bool       collision      = false;
    bool       fixed_range    = false;
    bool       is_wall        = false;
    uint8_t    charges        = 0;
    float      range2         = 0.f;
    float      radius2        = 0.f;
    float      speed2         = 0.f;
    uint8_t    projectile_count = 1;
};

struct damage_entry
{
    damage_type type        = damage_type::magic;
    float       base[ 6 ]  = {};
    float       ap_ratio[ 6 ]  = {};
    float       bonus_ad[ 6 ]  = {};
    float       total_ad[ 6 ]  = {};
    float       max_hp[ 6 ]    = {};
    float       curr_hp[ 6 ]   = {};
    float       miss_hp[ 6 ]   = {};
    float       flat_pen       = 0.f;
};

struct cooldown_entry
{
    float base[ 6 ] = {};
    float cost[ 6 ] = {};
};

struct spell_data
{
    slot             spell_slot      = slot::Q;
    spell_geometry   geometry        = {};
    damage_entry     damage          = {};
    cooldown_entry   cooldowns       = {};
    std::string_view name            = "";
    std::string_view display         = "";
    cc_type          cc              = cc_type::none;
    float            cc_duration     = 0.f;
    uint8_t          severity_rating = 0;
};

struct champion_stats
{
    float hp_base          = 0.f;
    float hp_per_level     = 0.f;
    float hp_regen_base    = 0.f;
    float hp_regen_per_lvl = 0.f;

    resource_type resource = resource_type::mana;
    float mp_base          = 0.f;
    float mp_per_level     = 0.f;
    float mp_regen_base    = 0.f;
    float mp_regen_per_lvl = 0.f;

    float ad_base          = 0.f;
    float ad_per_level     = 0.f;
    float as_base          = 0.f;
    float as_ratio         = 0.f;
    float as_per_level     = 0.f;
    float aa_range         = 0.f;

    float armor_base       = 0.f;
    float armor_per_level  = 0.f;
    float mr_base          = 0.f;
    float mr_per_level     = 0.f;

    float ms_base          = 0.f;
    float crit_dmg         = 1.75f;

    float aa_missile_speed = 0.f;
    float windup_percent   = 0.f;
    float gameplay_radius  = 65.f;
    float pathing_radius   = 35.f;

    // Stat(level) = Base + Growth * (level - 1) * (0.7025 + 0.0175 * (level - 1))
    constexpr float stat_at( float base, float growth, int level ) const noexcept
    {
        if ( level <= 1 ) return base;
        float n = static_cast< float >( level - 1 );
        return base + growth * n * ( 0.7025f + 0.0175f * n );
    }

    constexpr float hp_at( int level ) const noexcept    { return stat_at( hp_base, hp_per_level, level ); }
    constexpr float ad_at( int level ) const noexcept    { return stat_at( ad_base, ad_per_level, level ); }
    constexpr float armor_at( int level ) const noexcept { return stat_at( armor_base, armor_per_level, level ); }
    constexpr float mr_at( int level ) const noexcept    { return stat_at( mr_base, mr_per_level, level ); }
};

struct buff_info
{
    std::string_view name     = "";
    uint32_t         hash     = 0;
    float            duration = 0.f;
    bool             is_cc    = false;
    bool             is_shield = false;
};

struct champion_meta
{
    uint16_t         id       = 0;
    std::string_view name     = "";
    std::string_view display  = "";
    std::string_view title    = "";
    resource_type    resource = resource_type::mana;
    float            aa_range = 525.f;
    bool             is_melee = false;
};

struct passive_damage
{
    std::string_view name        = "";
    int              parent_slot = -1;
    damage_entry     damage      = {};
    int              proc_count  = 0;
};

struct champion_db
{
    champion_meta  meta          = {};
    champion_stats stats         = {};
    spell_data     spells[ 4 ]   = {};
    buff_info      buffs[ 12 ]   = {};
    uint8_t        buff_count    = 0;
    passive_damage passives[ 4 ] = {};
    uint8_t        passive_count = 0;

    constexpr const spell_data&    Q() const noexcept     { return spells[ 0 ]; }
    constexpr const spell_data&    W() const noexcept     { return spells[ 1 ]; }
    constexpr const spell_data&    E() const noexcept     { return spells[ 2 ]; }
    constexpr const spell_data&    R() const noexcept     { return spells[ 3 ]; }
    constexpr const spell_geometry& Q_geo() const noexcept { return spells[ 0 ].geometry; }
    constexpr const spell_geometry& W_geo() const noexcept { return spells[ 1 ].geometry; }
    constexpr const spell_geometry& E_geo() const noexcept { return spells[ 2 ].geometry; }
    constexpr const spell_geometry& R_geo() const noexcept { return spells[ 3 ].geometry; }

    constexpr const passive_damage* find_passive( std::string_view pname ) const noexcept
    {
        for ( uint8_t i = 0; i < passive_count; ++i )
            if ( passives[ i ].name == pname ) return &passives[ i ];
        return nullptr;
    }

    constexpr const passive_damage* find_passive_by_slot( int pslot ) const noexcept
    {
        for ( uint8_t i = 0; i < passive_count; ++i )
            if ( passives[ i ].parent_slot == pslot ) return &passives[ i ];
        return nullptr;
    }
};

struct item_stats
{
    float ad              = 0.f;
    float ap              = 0.f;
    float hp              = 0.f;
    float mana            = 0.f;
    float armor           = 0.f;
    float mr              = 0.f;
    float ability_haste   = 0.f;
    float attack_speed    = 0.f;
    float crit_chance     = 0.f;
    float crit_damage     = 0.f;
    float omnivamp        = 0.f;
    float life_steal      = 0.f;
    float move_speed_flat = 0.f;
    float move_speed_pct  = 0.f;
    float hp_regen        = 0.f;
    float hp_regen_pct    = 0.f;
    float mana_regen_pct  = 0.f;
    float flat_armor_pen  = 0.f;
    float flat_magic_pen  = 0.f;
    float lethality       = 0.f;
    float on_hit_heal     = 0.f;
    float gold_per_10     = 0.f;
};

struct item_effect
{
    item_effect_type type              = item_effect_type::none;
    std::string_view name              = "";
    std::string_view buff_name         = "";
    damage_type      dmg_type          = damage_type::physical;
    float            base_damage       = 0.f;
    float            ap_ratio          = 0.f;
    float            bonus_ad_ratio    = 0.f;
    float            base_ad_ratio     = 0.f;
    float            target_hp_ratio   = 0.f;
    float            crit_chance_ratio = 0.f;
    float            damage_multiplier = 0.f;
    float            crit_damage_bonus = 0.f;
    float            cooldown          = 0.f;
    float            ms_flat           = 0.f;
    float            ms_duration       = 0.f;
    float            hp_threshold      = 0.f;
    float            per_stack_value   = 0.f;
    float            stack_duration    = 0.f;
    uint16_t         max_stacks        = 0;
    bool             is_current_hp     = true;
    bool             below_threshold   = false;
};

struct item_data
{
    uint16_t         id           = 0;
    std::string_view name         = "";
    std::string_view display      = "";
    item_category    category     = item_category::basic;
    item_stats       stats        = {};
    uint16_t         gold_total   = 0;
    uint16_t         gold_sell    = 0;
    item_effect      effects[ 4 ] = {};
    uint8_t          effect_count = 0;
    std::string_view limit_group  = "";
};

struct masterwork_upgrade
{
    uint16_t   base_id    = 0;
    uint16_t   upgrade_id = 0;
    item_stats bonus      = {};
};

struct rune_damage
{
    float       base_min   = 0.f;
    float       base_max   = 0.f;
    float       bonus_ad   = 0.f;
    float       total_ad   = 0.f;
    float       ap         = 0.f;
    float       max_hp     = 0.f;
    float       curr_hp    = 0.f;
    float       miss_hp    = 0.f;
    bool        adaptive   = false;
    damage_type fixed_type = damage_type::physical;

    constexpr float base_at( int level ) const noexcept
    {
        if ( level <= 1 )  return base_min;
        if ( level >= 18 ) return base_max;
        return base_min + ( base_max - base_min ) * static_cast< float >( level - 1 ) / 17.f;
    }
};

struct rune_buff
{
    rune_buff_type type    = rune_buff_type::stack;
    uint32_t       hash    = 0;
    bool           on_self = false;
};

struct rune_data
{
    uint32_t         id           = 0;
    std::string_view name         = "";
    std::string_view display      = "";
    rune_path        path         = rune_path::precision;
    rune_slot        slot         = rune_slot::keystone;
    rune_damage      damage       = {};
    float            dmg_amp      = 0.f;
    float            dmg_amp_dur  = 0.f;
    float            cooldown     = 0.f;
    uint8_t          stacks       = 0;
    float            stack_window = 0.f;
    float            proc_delay   = 0.f;
    rune_buff        buffs[ 4 ]   = {};
    uint8_t          buff_count   = 0;

    constexpr const rune_buff* find_buff( rune_buff_type t ) const noexcept
    {
        for ( uint8_t i = 0; i < buff_count; ++i )
            if ( buffs[ i ].type == t ) return &buffs[ i ];
        return nullptr;
    }
};

struct level_scaling
{
    float at_1  = 0.f;
    float at_18 = 0.f;

    constexpr float at( int level ) const noexcept
    {
        if ( level <= 1 )  return at_1;
        if ( level >= 18 ) return at_18;
        return at_1 + ( at_18 - at_1 ) * static_cast< float >( level - 1 ) / 17.f;
    }

    constexpr bool has_value() const noexcept { return at_1 != 0.f || at_18 != 0.f; }
};

struct summoner_data
{
    summoner_id      id             = summoner_id::flash;
    std::string_view name           = "";
    std::string_view display        = "";
    float            range          = 0.f;
    float            cooldown       = 0.f;
    level_scaling    damage         = {};
    damage_type      dmg_type       = damage_type::true_dmg;
    level_scaling    heal           = {};
    level_scaling    shield         = {};
    float            duration       = 0.f;
    float            dmg_reduction  = 0.f;
    bool             grievous       = false;
    bool             unamplifiable  = false;
    bool             breaks_stealth = false;
    uint32_t         buff_hash      = 0;
};

} // namespace venplus::db
