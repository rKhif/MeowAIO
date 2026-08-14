#pragma once

#include "item_ids.hpp"
#include "sdk.hpp"

#include <algorithm>

namespace meow::item_damage
{

// Rune IDs from Riot's live game data. Legacy masteries have no current perk
// ID and are intentionally set to 0 so has_rune() never produces a false hit.
namespace rune_id
{
    inline constexpr uint32_t SUMMON_AERY          = 8214u;
    inline constexpr uint32_t ARCANE_COMET         = 8229u;
    inline constexpr uint32_t STORMRAIDERS_SURGE   = 0u; // Legacy mastery.
    inline constexpr uint32_t DEATHFIRE_TOUCH      = 0u; // Legacy mastery.
    inline constexpr uint32_t AXIOM_ARCANIST       = 8224u;
    inline constexpr uint32_t MANAFLOW_BAND        = 8226u;
    inline constexpr uint32_t NIMBUS_CLOAK         = 8275u;
    inline constexpr uint32_t TRANSCENDENCE        = 8210u;
    inline constexpr uint32_t CELERITY             = 8234u;
    inline constexpr uint32_t ABSOLUTE_FOCUS       = 8233u;
    inline constexpr uint32_t SCORCH               = 8237u;
    inline constexpr uint32_t WATERWALKING         = 8232u;
    inline constexpr uint32_t GATHERING_STORM      = 8236u;

    inline constexpr uint32_t LEGEND_HASTE         = 9105u;
    inline constexpr uint32_t PRESENCE_OF_MIND     = 8009u;
    inline constexpr uint32_t TRIUMPH              = 9111u;
    inline constexpr uint32_t ABSORB_LIFE          = 9101u;
    inline constexpr uint32_t LEGEND_BLOODLINE     = 9103u;
    inline constexpr uint32_t LEGEND_ALACRITY      = 9104u;
    inline constexpr uint32_t COUP_DE_GRACE        = 8014u;
    inline constexpr uint32_t CUT_DOWN             = 8017u;
    inline constexpr uint32_t LAST_STAND           = 8299u;
}

namespace rune_values
{
    inline constexpr float COUP_DE_GRACE_AMP       = 0.08f;
    inline constexpr float COUP_DE_GRACE_HP        = 0.40f;
    inline constexpr float CUT_DOWN_AMP            = 0.08f;
    inline constexpr float CUT_DOWN_HP             = 0.60f;
    inline constexpr float LAST_STAND_MIN_AMP      = 0.05f;
    inline constexpr float LAST_STAND_MAX_AMP      = 0.11f;
    inline constexpr float LAST_STAND_START_HP     = 0.60f;
    inline constexpr float LAST_STAND_MAX_HP       = 0.30f;
    inline constexpr float AXIOM_ARCANIST_AMP      = 0.12f;
    inline constexpr float AXIOM_ARCANIST_AOE_AMP  = 0.08f;
}

namespace dorans_ring
{
    inline constexpr float MINION_DAMAGE = 5.f;
}

namespace fated_ashes
{
    inline constexpr int   MAX_TICKS          = 6;
    inline constexpr float DAMAGE_PER_TICK    = 2.5f;
    inline constexpr float MONSTER_BONUS_TICK = 7.5f;
}

namespace blackfire_torch
{
    inline constexpr int   MAX_TICKS          = 6;
    inline constexpr float DAMAGE_PER_TICK    = 10.f;
    inline constexpr float AP_RATIO_PER_TICK  = 0.01f;
    inline constexpr float MONSTER_BONUS_TICK = 10.f;
}

namespace hextech_rocketbelt
{
    inline constexpr float BASE_DAMAGE = 100.f;
    inline constexpr float AP_RATIO    = 0.10f;
}

namespace hextech_alternator
{
    inline constexpr float BASE_DAMAGE = 65.f;
}

namespace liandrys_torment
{
    inline constexpr int   MAX_TICKS                = 6;
    inline constexpr int   MAX_SUFFERING_STACKS     = 3;
    inline constexpr float MAX_HP_RATIO_PER_TICK    = 0.01f;
    inline constexpr float MONSTER_DAMAGE_TICK_CAP  = 20.f;
    inline constexpr float DAMAGE_AMP_PER_STACK     = 0.02f;
}

namespace lich_bane
{
    inline constexpr float BASE_AD_RATIO = 0.75f;
    inline constexpr float AP_RATIO      = 0.40f;
}

namespace malignance
{
    inline constexpr int   MAX_TICKS         = 12;
    inline constexpr float DAMAGE_PER_TICK   = 15.f;
    inline constexpr float AP_RATIO_PER_TICK = 0.0125f;
}

namespace ludens_echo
{
    inline constexpr int   MAX_ECHOES_ON_PRIMARY  = 6;
    inline constexpr float BASE_DAMAGE            = 75.f;
    inline constexpr float AP_RATIO               = 0.05f;
    inline constexpr float REPEAT_DAMAGE_MODIFIER = 0.20f;
}

namespace shadowflame
{
    inline constexpr float HP_THRESHOLD     = 0.40f;
    inline constexpr float DAMAGE_MULTIPLIER = 1.20f;
}

namespace horizon_focus
{
    inline constexpr float HYPERSHOT_RANGE    = 600.f;
    inline constexpr float DAMAGE_MULTIPLIER  = 1.10f;
}

namespace stormsurge
{
    inline constexpr float TRIGGER_MAX_HP_RATIO = 0.25f;
    inline constexpr float BASE_DAMAGE          = 125.f;
    inline constexpr float AP_RATIO             = 0.10f;
}

// Backward-compatible name from the first version of this helper.
inline constexpr int BLACKFIRE_TORCH_ID = item_id::BLACKFIRE_TORCH;

inline bool has_item( game_object* source, int id ) noexcept
{
    if ( !source ) return false;

    int slot = -1;
    return source->has_item( id, &slot );
}

inline bool has_rune( game_object* source, uint32_t id ) noexcept
{
    return source && id != 0u && source->has_rune( id );
}

inline float level_scaled( game_object* source, float minimum, float maximum ) noexcept
{
    if ( !source ) return 0.f;
    const float t = ( std::clamp )( ( source->get_level() - 1.f ) / 17.f, 0.f, 1.f );
    return minimum + ( maximum - minimum ) * t;
}

// Precision row-three runes are mutually exclusive.
inline float precision_damage_multiplier( game_object* source, game_object* target ) noexcept
{
    if ( !source || !target || !target->is_hero() ) return 1.f;

    const float target_hp = target->get_max_hp() > 0.f
        ? target->get_hp() / target->get_max_hp() : 1.f;

    if ( has_rune( source, rune_id::CUT_DOWN ) && target_hp > rune_values::CUT_DOWN_HP )
        return 1.f + rune_values::CUT_DOWN_AMP;
    if ( has_rune( source, rune_id::COUP_DE_GRACE ) && target_hp < rune_values::COUP_DE_GRACE_HP )
        return 1.f + rune_values::COUP_DE_GRACE_AMP;

    if ( has_rune( source, rune_id::LAST_STAND ) && source->get_max_hp() > 0.f )
    {
        const float source_hp = source->get_hp() / source->get_max_hp();
        if ( source_hp < rune_values::LAST_STAND_START_HP )
        {
            const float t = ( std::clamp )(
                ( rune_values::LAST_STAND_START_HP - source_hp )
                / ( rune_values::LAST_STAND_START_HP - rune_values::LAST_STAND_MAX_HP ), 0.f, 1.f );
            return 1.f + rune_values::LAST_STAND_MIN_AMP
                + t * ( rune_values::LAST_STAND_MAX_AMP - rune_values::LAST_STAND_MIN_AMP );
        }
    }
    return 1.f;
}

inline float axiom_arcanist_multiplier( game_object* source, bool is_ultimate,
                                        bool is_aoe ) noexcept
{
    if ( !is_ultimate || !has_rune( source, rune_id::AXIOM_ARCANIST ) ) return 1.f;
    return 1.f + ( is_aoe ? rune_values::AXIOM_ARCANIST_AOE_AMP
                           : rune_values::AXIOM_ARCANIST_AMP );
}

inline float apply_spell_rune_modifiers( game_object* source, game_object* target,
                                         float damage, bool is_ultimate = false,
                                         bool is_aoe = false ) noexcept
{
    return damage * precision_damage_multiplier( source, target )
        * axiom_arcanist_multiplier( source, is_ultimate, is_aoe );
}

struct ability_rune_context
{
    float cast_distance = 0.f;
    bool aery_available = true;
    bool comet_available = true;
    bool scorch_available = true;
};

inline float magic_damage( game_object* source, game_object* target, float raw,
                           int liandry_combat_stacks = 0,
                           bool horizon_focus_active = false ) noexcept;

// Damage procs caused by one ability hit. Availability flags let callers that
// track cooldowns/Aery travel suppress a proc; the default is an optimistic
// ready-state estimate, matching the item proc helper below.
inline float on_ability_rune_damage( game_object* source, game_object* target,
                                     const ability_rune_context& context = {} ) noexcept
{
    if ( !source || !target || !target->is_hero() ) return 0.f;

    const float bonus_ad = ( std::max )( 0.f,
        source->get_attack_damage() - source->get_base_attack_damage() );
    float raw_magic = 0.f;

    if ( context.aery_available && has_rune( source, rune_id::SUMMON_AERY ) )
        raw_magic += level_scaled( source, 10.f, 50.f )
            + source->get_ability_power() * 0.05f + bonus_ad * 0.10f;

    if ( context.comet_available && has_rune( source, rune_id::ARCANE_COMET ) )
    {
        const float distance_amp = 1.f + ( std::clamp )(
            context.cast_distance / 750.f, 0.f, 1.f );
        raw_magic += ( level_scaled( source, 15.f, 100.f )
            + source->get_ability_power() * 0.05f + bonus_ad * 0.10f ) * distance_amp;
    }

    if ( context.scorch_available && has_rune( source, rune_id::SCORCH ) )
        raw_magic += level_scaled( source, 20.f, 40.f );

    if ( raw_magic <= 0.f ) return 0.f;
    return magic_damage( source, target, raw_magic );
}

inline bool has_blackfire_torch( game_object* source ) noexcept
{
    return has_item( source, item_id::BLACKFIRE_TORCH );
}

inline float liandrys_suffering_multiplier( game_object* source, game_object* target,
                                            int combat_stacks ) noexcept
{
    if ( !source || !target || !target->is_hero()
         || ( !has_item( source, item_id::LIANDRYS_TORMENT )
              && !has_item( source, item_id::HAUNTING_GUISE ) ) )
        return 1.f;

    combat_stacks = ( std::clamp )( combat_stacks, 0, liandrys_torment::MAX_SUFFERING_STACKS );
    return 1.f + liandrys_torment::DAMAGE_AMP_PER_STACK * static_cast< float >( combat_stacks );
}

inline float shadowflame_multiplier( game_object* source, game_object* target ) noexcept
{
    if ( !source || !target || !has_item( source, item_id::SHADOWFLAME ) ) return 1.f;
    if ( target->get_max_hp() <= 0.f ) return 1.f;

    return target->get_hp() / target->get_max_hp() < shadowflame::HP_THRESHOLD
        ? shadowflame::DAMAGE_MULTIPLIER
        : 1.f;
}

inline float horizon_focus_multiplier( game_object* source, game_object* target,
                                       bool hypershot_active ) noexcept
{
    if ( !hypershot_active || !source || !target || !target->is_hero()
         || !has_item( source, item_id::HORIZON_FOCUS ) )
        return 1.f;

    return horizon_focus::DAMAGE_MULTIPLIER;
}

// Converts raw magic damage and then applies item damage amplifiers. Sorcerer's
// Shoes, Void Staff, and Cryptbloom penetration are already represented in the
// source stats used by VEN.Damage.
inline float magic_damage( game_object* source, game_object* target, float raw,
                           int liandry_combat_stacks,
                           bool horizon_focus_active ) noexcept
{
    if ( !source || !target || raw <= 0.f ) return 0.f;

    float result = sdk::damage
        ? sdk::damage->calc_damage( dmg_sdk::damage_type::magical, source, target, raw )
        : raw;

    result *= liandrys_suffering_multiplier( source, target, liandry_combat_stacks );
    result *= shadowflame_multiplier( source, target );
    result *= precision_damage_multiplier( source, target );
    result *= horizon_focus_multiplier( source, target, horizon_focus_active );
    return result;
}

// Use this on already-calculated champion magic damage when the champion's own
// damage helper does not account for Shadowflame or Liandry's Suffering.
inline float apply_magic_damage_modifiers( game_object* source, game_object* target,
                                           float post_mitigation_damage,
                                           int liandry_combat_stacks = 0,
                                           bool horizon_focus_active = false ) noexcept
{
    if ( post_mitigation_damage <= 0.f ) return 0.f;

    return post_mitigation_damage
        * liandrys_suffering_multiplier( source, target, liandry_combat_stacks )
        * shadowflame_multiplier( source, target )
        * precision_damage_multiplier( source, target )
        * horizon_focus_multiplier( source, target, horizon_focus_active );
}

inline float apply_true_damage_modifiers( game_object* source, game_object* target,
                                          float true_damage,
                                          bool horizon_focus_active = false ) noexcept
{
    if ( true_damage <= 0.f ) return 0.f;
    return true_damage * shadowflame_multiplier( source, target )
        * precision_damage_multiplier( source, target )
        * horizon_focus_multiplier( source, target, horizon_focus_active );
}

inline float apply_physical_damage_modifiers( game_object* source, game_object* target,
                                              float post_mitigation_damage,
                                              int liandry_combat_stacks = 0,
                                              bool horizon_focus_active = false ) noexcept
{
    if ( post_mitigation_damage <= 0.f ) return 0.f;

    return post_mitigation_damage
        * liandrys_suffering_multiplier( source, target, liandry_combat_stacks )
        * precision_damage_multiplier( source, target )
        * horizon_focus_multiplier( source, target, horizon_focus_active );
}

inline float dorans_ring_raw_damage( game_object* source, game_object* target ) noexcept
{
    if ( !source || !target || !target->is_lane_minion()
         || !has_item( source, item_id::DORANS_RING ) )
        return 0.f;

    return dorans_ring::MINION_DAMAGE;
}

inline float dorans_ring_damage( game_object* source, game_object* target ) noexcept
{
    const float raw = dorans_ring_raw_damage( source, target );
    if ( raw <= 0.f ) return 0.f;

    return sdk::damage
        ? sdk::damage->calc_damage( dmg_sdk::damage_type::physical, source, target, raw )
        : raw;
}

inline float hextech_alternator_raw_damage( game_object* source, game_object* target,
                                            bool revved_ready = true ) noexcept
{
    if ( !revved_ready || !source || !target || !target->is_hero()
         || !has_item( source, item_id::HEXTECH_ALTERNATOR ) )
        return 0.f;

    return hextech_alternator::BASE_DAMAGE;
}

inline float hextech_alternator_damage( game_object* source, game_object* target,
                                        bool revved_ready = true,
                                        int liandry_combat_stacks = 0,
                                        bool horizon_focus_active = false ) noexcept
{
    return magic_damage( source, target,
        hextech_alternator_raw_damage( source, target, revved_ready ),
        liandry_combat_stacks, horizon_focus_active );
}

inline float stormsurge_trigger_damage( game_object* target ) noexcept
{
    return target ? target->get_max_hp() * stormsurge::TRIGGER_MAX_HP_RATIO : 0.f;
}

inline float stormsurge_raw_damage( game_object* source, game_object* target,
                                    bool squall_triggered = true ) noexcept
{
    if ( !squall_triggered || !source || !target || !target->is_hero()
         || !has_item( source, item_id::STORMSURGE ) )
        return 0.f;

    return stormsurge::BASE_DAMAGE
        + stormsurge::AP_RATIO * source->get_ability_power();
}

inline float stormsurge_damage( game_object* source, game_object* target,
                                bool squall_triggered = true,
                                int liandry_combat_stacks = 0,
                                bool horizon_focus_active = false ) noexcept
{
    return magic_damage( source, target,
        stormsurge_raw_damage( source, target, squall_triggered ),
        liandry_combat_stacks, horizon_focus_active );
}

inline float fated_ashes_raw_damage( game_object* source, game_object* target,
                                     int ticks = fated_ashes::MAX_TICKS ) noexcept
{
    if ( !source || !target || !has_item( source, item_id::FATED_ASHES ) ) return 0.f;

    ticks = ( std::clamp )( ticks, 0, fated_ashes::MAX_TICKS );
    float damage_per_tick = fated_ashes::DAMAGE_PER_TICK;
    if ( target->is_monster() )
        damage_per_tick += fated_ashes::MONSTER_BONUS_TICK;

    return damage_per_tick * static_cast< float >( ticks );
}

inline float fated_ashes_damage( game_object* source, game_object* target,
                                 int ticks = fated_ashes::MAX_TICKS,
                                 int liandry_combat_stacks = 0,
                                 bool horizon_focus_active = false ) noexcept
{
    return magic_damage( source, target,
        fated_ashes_raw_damage( source, target, ticks ),
        liandry_combat_stacks, horizon_focus_active );
}

inline float blackfire_torch_raw_damage( game_object* source, game_object* target,
                                         int ticks = blackfire_torch::MAX_TICKS ) noexcept
{
    if ( !source || !target || !has_item( source, item_id::BLACKFIRE_TORCH ) ) return 0.f;

    ticks = ( std::clamp )( ticks, 0, blackfire_torch::MAX_TICKS );

    float damage_per_tick = blackfire_torch::DAMAGE_PER_TICK
        + blackfire_torch::AP_RATIO_PER_TICK * source->get_ability_power();

    if ( target->is_monster() )
        damage_per_tick += blackfire_torch::MONSTER_BONUS_TICK;

    return damage_per_tick * static_cast< float >( ticks );
}

inline float blackfire_torch_damage( game_object* source, game_object* target,
                                     int ticks = blackfire_torch::MAX_TICKS,
                                     int liandry_combat_stacks = 0 ) noexcept
{
    return magic_damage( source, target,
        blackfire_torch_raw_damage( source, target, ticks ), liandry_combat_stacks );
}

inline float hextech_rocketbelt_raw_damage( game_object* source ) noexcept
{
    if ( !source || !has_item( source, item_id::HEXTECH_ROCKETBELT ) ) return 0.f;
    return hextech_rocketbelt::BASE_DAMAGE
        + hextech_rocketbelt::AP_RATIO * source->get_ability_power();
}

inline float hextech_rocketbelt_damage( game_object* source, game_object* target,
                                        int liandry_combat_stacks = 0 ) noexcept
{
    return magic_damage( source, target,
        hextech_rocketbelt_raw_damage( source ), liandry_combat_stacks );
}

inline float liandrys_torment_raw_damage( game_object* source, game_object* target,
                                          int ticks = liandrys_torment::MAX_TICKS ) noexcept
{
    if ( !source || !target || !has_item( source, item_id::LIANDRYS_TORMENT ) ) return 0.f;

    ticks = ( std::clamp )( ticks, 0, liandrys_torment::MAX_TICKS );
    float damage_per_tick = target->get_max_hp() * liandrys_torment::MAX_HP_RATIO_PER_TICK;

    if ( target->is_monster() )
        damage_per_tick = ( std::min )( damage_per_tick, liandrys_torment::MONSTER_DAMAGE_TICK_CAP );

    return damage_per_tick * static_cast< float >( ticks );
}

inline float liandrys_torment_damage( game_object* source, game_object* target,
                                      int ticks = liandrys_torment::MAX_TICKS,
                                      int combat_stacks = 0 ) noexcept
{
    return magic_damage( source, target,
        liandrys_torment_raw_damage( source, target, ticks ), combat_stacks );
}

inline float lich_bane_raw_damage( game_object* source ) noexcept
{
    if ( !source || !has_item( source, item_id::LICH_BANE ) ) return 0.f;

    return lich_bane::BASE_AD_RATIO * source->get_base_attack_damage()
        + lich_bane::AP_RATIO * source->get_ability_power();
}

inline float lich_bane_damage( game_object* source, game_object* target,
                               int liandry_combat_stacks = 0 ) noexcept
{
    return magic_damage( source, target,
        lich_bane_raw_damage( source ), liandry_combat_stacks );
}

inline float malignance_raw_damage( game_object* source, game_object* target,
                                    int ticks = malignance::MAX_TICKS ) noexcept
{
    if ( !source || !target || !target->is_hero()
         || !has_item( source, item_id::MALIGNANCE ) )
        return 0.f;

    ticks = ( std::clamp )( ticks, 0, malignance::MAX_TICKS );
    const float damage_per_tick = malignance::DAMAGE_PER_TICK
        + malignance::AP_RATIO_PER_TICK * source->get_ability_power();
    return damage_per_tick * static_cast< float >( ticks );
}

inline float malignance_damage( game_object* source, game_object* target,
                                int ticks = malignance::MAX_TICKS,
                                int liandry_combat_stacks = 0 ) noexcept
{
    return magic_damage( source, target,
        malignance_raw_damage( source, target, ticks ), liandry_combat_stacks );
}

// echoes_on_primary is 1 with secondary targets available and up to 6 when
// unused echoes repeat on the primary target. Pass 0 when Echo is unavailable.
inline float ludens_echo_raw_damage( game_object* source, int echoes_on_primary = 1 ) noexcept
{
    if ( !source || !has_item( source, item_id::LUDENS_ECHO ) ) return 0.f;

    echoes_on_primary = ( std::clamp )( echoes_on_primary, 0, ludens_echo::MAX_ECHOES_ON_PRIMARY );
    if ( echoes_on_primary == 0 ) return 0.f;

    const float first_echo = ludens_echo::BASE_DAMAGE
        + ludens_echo::AP_RATIO * source->get_ability_power();
    return first_echo * ( 1.f + ludens_echo::REPEAT_DAMAGE_MODIFIER
        * static_cast< float >( echoes_on_primary - 1 ) );
}

inline float ludens_echo_damage( game_object* source, game_object* target,
                                 int echoes_on_primary = 1,
                                 int liandry_combat_stacks = 0 ) noexcept
{
    return magic_damage( source, target,
        ludens_echo_raw_damage( source, echoes_on_primary ), liandry_combat_stacks );
}

struct ability_proc_context
{
    int fated_ashes_ticks      = fated_ashes::MAX_TICKS;
    int blackfire_ticks       = blackfire_torch::MAX_TICKS;
    int liandry_ticks         = liandrys_torment::MAX_TICKS;
    int malignance_ticks      = 0;
    int ludens_echoes_primary = 1;
    int liandry_combat_stacks = 0;
    bool hextech_alternator_ready = true;
    bool stormsurge_triggered = false;
    bool horizon_focus_active = false;
};

// Item damage caused by one ability hit. Burn refreshes should only be counted
// once in a normal short-combo estimate.
inline float on_ability_damage( game_object* source, game_object* target,
                                const ability_proc_context& context = {} ) noexcept
{
    if ( !source || !target ) return 0.f;

    float raw = 0.f;
    raw += fated_ashes_raw_damage( source, target, context.fated_ashes_ticks );
    raw += blackfire_torch_raw_damage( source, target, context.blackfire_ticks );
    raw += liandrys_torment_raw_damage( source, target, context.liandry_ticks );
    raw += malignance_raw_damage( source, target, context.malignance_ticks );
    raw += ludens_echo_raw_damage( source, context.ludens_echoes_primary );
    raw += hextech_alternator_raw_damage( source, target,
        context.hextech_alternator_ready );
    raw += stormsurge_raw_damage( source, target, context.stormsurge_triggered );
    return magic_damage( source, target, raw, context.liandry_combat_stacks,
        context.horizon_focus_active );
}

inline float on_attack_after_ability( game_object* source, game_object* target,
                                      int liandry_combat_stacks = 0 ) noexcept
{
    return dorans_ring_damage( source, target )
        + lich_bane_damage( source, target, liandry_combat_stacks );
}

inline float on_basic_attack( game_object* source, game_object* target,
                              bool consume_lich_bane = false,
                              int liandry_combat_stacks = 0 ) noexcept
{
    float total = dorans_ring_damage( source, target );
    if ( consume_lich_bane )
        total += lich_bane_damage( source, target, liandry_combat_stacks );
    return total;
}

} // namespace meow::item_damage
