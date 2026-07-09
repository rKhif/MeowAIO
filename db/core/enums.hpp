#pragma once

#include <cstdint>

namespace venplus::db
{

enum class spell_type : uint8_t
{
    none       = 0,
    linear     = 1,
    circular   = 2,
    cone       = 3,
    arc        = 4,
    ring       = 5,
    rect       = 6,
    targeted   = 7,
    self_cast  = 8,
    dash       = 9,
    wall       = 10,
    global     = 11,
    trap       = 12,
};

enum class damage_type : uint8_t
{
    physical = 0,
    magic    = 1,
    true_dmg = 2,
    mixed    = 3,
};

enum class resource_type : uint8_t
{
    mana     = 0,
    energy   = 1,
    none     = 2,
    health   = 3,
    fury     = 4,
    ferocity = 5,
    heat     = 6,
    grit     = 7,
    courage  = 8,
    flow     = 9,
    shield   = 10,
    bloodwell = 11,
    custom   = 12,
};

enum class slot : uint8_t
{
    Q = 0, W = 1, E = 2, R = 3,
};

enum class danger_level : uint8_t
{
    none    = 0,
    low     = 1,
    medium  = 2,
    high    = 3,
    extreme = 4,
};

enum class cc_type : uint8_t
{
    none      = 0,
    slow      = 1,
    root      = 2,
    stun      = 3,
    knockup   = 4,
    knockback = 5,
    charm     = 6,
    fear      = 7,
    taunt     = 8,
    suppress  = 9,
    silence   = 10,
    blind     = 11,
    ground    = 12,
    sleep     = 13,
    polymorph = 14,
    pull      = 15,
    stasis    = 16,
    nearsight = 17,
    disarm    = 18,
};

enum class item_category : uint8_t
{
    starter    = 0,
    basic      = 1,
    epic       = 2,
    legendary  = 3,
    boots      = 4,
    support    = 5,
    jungle     = 6,
    consumable = 7,
};

enum class item_effect_type : uint8_t
{
    none            = 0,
    on_hit          = 1,
    on_hit_minion   = 2,
    spellblade      = 3,
    armor_shred     = 4,
    burn_champion   = 5,
    spell_crit      = 6,
    crit_modifier   = 7,
    stacking_stats  = 8,
    movement_speed  = 9,
    shield_gen      = 10,
    healing         = 11,
    gold_generation = 12,
    mana_charge     = 13,
    stasis          = 14,
    cleanse         = 15,
    active_dash     = 16,
    active_slow     = 17,
    active_haste    = 18,
    ward            = 19,
};

enum class rune_path : uint8_t
{
    precision   = 0,
    domination  = 1,
    sorcery     = 2,
    resolve     = 3,
    inspiration = 4,
};

enum class rune_slot : uint8_t
{
    keystone   = 0,
    row_1      = 1,
    row_2      = 2,
    row_3      = 3,
    stat_shard = 4,
};

enum class rune_buff_type : uint8_t
{
    stack    = 0,
    exposure = 1,
    lockout  = 2,
    proc     = 3,
    passive  = 4,
};

enum class summoner_id : uint8_t
{
    flash    = 0,
    ignite   = 1,
    exhaust  = 2,
    heal     = 3,
    barrier  = 4,
    cleanse  = 5,
    teleport = 6,
    smite    = 7,
    ghost    = 8,
    snowball = 9,
};

enum class location_id : uint8_t
{
    top_lane = 0,
    mid_lane = 1,
    bot_lane = 2,
    jungle   = 3,
    base     = 4,
};

} // namespace venplus::db
