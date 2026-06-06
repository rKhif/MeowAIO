#include "pch.hpp"

#include "champions/aphelios/aphelios.hpp"

#include "db/champions/aphelios.hpp"

namespace sora::champions
{

namespace db = sora::db::champions::aphelios;

// Killable with BE + 1 forced AA. Conservative model: assumes orbwalker
// fires 1 AA on the rooted target during BE's 1s root window. BE damage
// computed from raw formula (db/aphelios.hpp) and routed through
// compute.hpp's spell_damage (champion_base helper) for proper
// mitigation + amps. Magic damage type; spell_kind::normal.
bool aphelios::killable_with_be_aa( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad  = ad_ - player_->get_base_attack_damage();
    const float be_raw    = db::be_raw_damage( level_, bonus_ad, ap_ );
    const float be_final  = spell_damage( target, be_raw,
                                          sora::damage::damage_type::magic,
                                          sora::damage::spell_kind::normal );
    const float aa_final  = aa_damage( target );

    return sora::damage::will_kill( player_, target, be_final + aa_final );
}

// Returns true if BE will land its CC component (root) on target.
// Spell-shield (Banshee/Sivir E/Nocturne W) is handled implicitly via
// will_kill returning 0 dmg, but we also need explicit checks for
// untargetable / invulnerability since those prevent the root regardless
// of damage modeling. is_targetable covers Zhonyas/Kayle-R-target/etc.
bool aphelios::target_be_castable( game_object* target ) const
{
    if ( target == nullptr )                                          return false;
    if ( !target->is_valid() )                                        return false;
    if ( target->is_dead() )                                          return false;
    if ( !target->is_targetable() )                                   return false;
    if ( target->has_buff_of_type( buff_type::invulnerability ) )     return false;
    if ( target->has_buff_of_type( buff_type::spell_immunity ) )      return false;
    if ( target->has_buff_of_type( buff_type::spell_shield ) )        return false;
    return true;
}

// Binding Eclipse cast. Point-and-click; engine global effect roots ALL
// Gra-marked enemies regardless of distance.
//
// Three firing paths (multi-root > single-target killable > dump-ammo),
// any one triggers the cast. Multi-root prioritized — if BE will root
// ≥ N enemies (slider), commit even if hero target alone wouldn't kill.
// "Don't waste R-applied marks by not rooting them."
//
// Single-target killable: hero target Gra-marked + killable_with_be_aa.
// Dump-ammo: last ammo + mana ≥ 50% (mirrors Severum solo Q).
bool aphelios::try_gravitum_be( game_object* target )
{
    if ( !gra_use_q_master_ )                     return false;
    if ( !gra_be_combo_ )                         return false;
    if ( main_weapon_ != db::weapon::gravitum )   return false;
    if ( !can_cast( db::SLOT_Q, db::BE_DELAY ) )  return false;

    // Path 1: Multi-root. BE roots all marked → commit if enough enemies
    // would be rooted (and aren't CC-immune). Independent of which
    // "target" we pass — engine roots globally.
    if ( gra_be_multi_combo_ )
    {
        const int rootable = count_rootable_gra_marked();
        if ( rootable >= gra_be_min_roots_ )
        {
            auto* cast_target = pick_any_gra_marked_target();
            if ( cast_target != nullptr )
            {
                const bool ok = cast_spell_target( db::SLOT_Q, cast_target );
                if ( trace_manual_ )
                {
                    g_sdk->log_console( "[trace] be_fire_multi t=%.3f rootable=%d need=%d result=%d\n",
                                        game_time_, rootable, gra_be_min_roots_,
                                        static_cast< int >( ok ) );
                }
                return ok;
            }
        }
    }

    // Paths 2/3: Single-target on hero target.
    if ( target == nullptr )                      return false;
    if ( !is_gra_marked( target->get_network_id() ) ) return false;

    if ( !target_be_castable( target ) )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] be_skip_cc_immune t=%.3f target=%u\n",
                                game_time_, target->get_network_id() );
        }
        return false;
    }

    const bool killable  = killable_with_be_aa( target );
    const bool dump_ammo = ( main_ammo_ == 1 && mana_percent_ >= 0.5f );

    if ( !killable && !dump_ammo )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] be_skip t=%.3f target=%u killable=0 dump=0 ammo=%d mana%%=%.2f\n",
                                game_time_, target->get_network_id(),
                                main_ammo_, mana_percent_ );
        }
        return false;
    }

    const bool ok = cast_spell_target( db::SLOT_Q, target );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] be_fire t=%.3f target=%u result=%d killable=%d dump=%d\n",
                            game_time_, target->get_network_id(),
                            static_cast< int >( ok ),
                            static_cast< int >( killable ),
                            static_cast< int >( dump_ammo ) );
    }
    return ok;
}

// Count enemies that BE would actually root: Gra-marked + valid +
// not CC-immune (spell shield / invulnerable / spell immunity skip
// the root regardless). Used by multi-root gate.
int aphelios::count_rootable_gra_marked() const
{
    if ( sdk::target_selector == nullptr || player_ == nullptr ) return 0;
    const int my_team = player_->get_team_id();
    int count = 0;

    for ( auto* hero : sdk::target_selector->get_sorted_heroes() )
    {
        if ( hero == nullptr || !hero->is_valid() )    continue;
        if ( hero->is_dead() || !hero->is_visible() )  continue;
        if ( hero->get_team_id() == my_team )          continue;
        if ( !is_gra_marked( hero->get_network_id() ) ) continue;
        if ( !target_be_castable( hero ) )             continue;
        ++count;
    }
    return count;
}

// Pick any rootable Gra-marked enemy as BE cast target. BE is global so
// the specific target doesn't matter for the root effect — it just needs
// a valid cast target for the engine. Returns first match in TS priority
// order so highest-priority enemy gets the dmg component.
game_object* aphelios::pick_any_gra_marked_target() const
{
    if ( sdk::target_selector == nullptr || player_ == nullptr ) return nullptr;
    const int my_team = player_->get_team_id();

    for ( auto* hero : sdk::target_selector->get_sorted_heroes() )
    {
        if ( hero == nullptr || !hero->is_valid() )    continue;
        if ( hero->is_dead() || !hero->is_visible() )  continue;
        if ( hero->get_team_id() == my_team )          continue;
        if ( !is_gra_marked( hero->get_network_id() ) ) continue;
        if ( !target_be_castable( hero ) )             continue;
        return hero;
    }
    return nullptr;
}

void aphelios::gra_mark_add( uint32_t net_id )
{
    if ( gra_marked_count_ >= gra_marked_targets_.size() ) return;

    for ( size_t i = 0u; i < gra_marked_count_; ++i )
    {
        if ( gra_marked_targets_[ i ] == net_id ) return;
    }

    gra_marked_targets_[ gra_marked_count_ ] = net_id;
    ++gra_marked_count_;
}

void aphelios::gra_mark_remove( uint32_t net_id )
{
    for ( size_t i = 0u; i < gra_marked_count_; ++i )
    {
        if ( gra_marked_targets_[ i ] != net_id ) continue;

        gra_marked_targets_[ i ] = gra_marked_targets_[ gra_marked_count_ - 1u ];
        --gra_marked_count_;
        return;
    }
}

bool aphelios::is_gra_marked( uint32_t net_id ) const
{
    for ( size_t i = 0u; i < gra_marked_count_; ++i )
    {
        if ( gra_marked_targets_[ i ] == net_id ) return true;
    }
    return false;
}

// Count alive allied heroes within `range` of `pos` (excludes self).
// Used by R-Gra engage gate — R should only fire when teammates can
// follow up on the slow/root.
int aphelios::count_allies_near( const math::vector3& pos, float range ) const
{
    if ( player_ == nullptr || g_sdk == nullptr || g_sdk->object_manager == nullptr )
        return 0;

    const int my_team = player_->get_team_id();
    int count = 0;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( hero == nullptr || !hero->is_valid() || hero->is_dead() ) continue;
        if ( hero == player_ )                                         continue;
        if ( hero->get_team_id() != my_team )                          continue;
        if ( pos.distance( hero->get_position() ) <= range )           ++count;
    }

    return count;
}

void aphelios::clear_gra_inf_state()
{
    gra_inf_step_            = gra_inf_step::idle;
    gra_inf_started_at_      = 0.f;
    gra_inf_be_processed_at_ = 0.f;
    gra_inf_target_id_       = 0u;
}

// Killable estimate for Gra->Inf cross-swap chain: BE + Duskwave +
// Duskwave's offhand AA (Gra base AA in this pairing) + 1 follow-up AA
// during the root window. All routed through compute.hpp.
bool aphelios::killable_with_be_duskwave_aa( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad = ad_ - player_->get_base_attack_damage();

    const float be_raw   = db::be_raw_damage( level_, bonus_ad, ap_ );
    const float be_final = spell_damage( target, be_raw,
                                         sora::damage::damage_type::magic,
                                         sora::damage::spell_kind::normal );

    const float q_raw    = db::inf_q_raw_damage( level_, bonus_ad );
    const float q_final  = spell_damage( target, q_raw,
                                         sora::damage::damage_type::physical,
                                         sora::damage::spell_kind::normal );

    // Duskwave fires offhand AA on each hit (Gra here = base AA).
    const float offhand_aa = aa_damage( target );
    // 1 follow-up AA during root window.
    const float follow_aa  = aa_damage( target );

    return sora::damage::will_kill( player_, target,
                                    be_final + q_final + offhand_aa + follow_aa );
}

// Gra->Inf cross-swap entry. Pre-condition: hero target is Gra-marked
// (player AA'd them with Gra at least once). Cast BE -> roots target ->
// on_game_update polls for BE processed + W ready -> W swap to Inf ->
// combo() finalize with Duskwave on rooted target.
//
// Optional killable gate (toggle): only fire if BE + Duskwave + AAs
// will kill (so we don't waste a heavy 3-spell commit on tanks).
bool aphelios::try_gra_inf_be()
{
    if ( !gra_inf_swap_combo_ )                   return false;
    if ( !gra_use_q_master_ )                     return false;
    if ( !inf_use_q_master_ )                     return false;
    if ( main_weapon_    != db::weapon::gravitum ) return false;
    if ( offhand_weapon_ != db::weapon::infernum ) return false;
    if ( !is_spell_ready( db::SLOT_Q ) )          return false;  // BE
    if ( !is_spell_ready( db::SLOT_W ) )          return false;

    auto* target = get_hero_target();
    if ( target == nullptr )                      return false;
    if ( !is_gra_marked( target->get_network_id() ) ) return false;
    if ( !target_be_castable( target ) )          return false;

    if ( gra_inf_swap_require_killable_
      && !killable_with_be_duskwave_aa( target ) )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_inf_skip_not_killable t=%.3f target=%u\n",
                                game_time_, target->get_network_id() );
        }
        return false;
    }

    const bool ok = cast_spell_target( db::SLOT_Q, target );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] gra_inf_be_fire t=%.3f target=%u result=%d\n",
                            game_time_, target->get_network_id(),
                            static_cast< int >( ok ) );
    }
    if ( !ok ) return false;

    gra_inf_step_            = gra_inf_step::be_in_flight;
    gra_inf_started_at_      = game_time_;
    gra_inf_be_processed_at_ = 0.f;
    gra_inf_target_id_       = target->get_network_id();
    return true;
}

// R-Gravitum (Gra-MV) engage: cast on enemy cluster that maximizes hits,
// requiring teammates near for follow-up. R lands → applies Gra mark
// (99% slow) → next combo() tick BE follow-up roots all marked.
//
// Decision criteria from the Aphelios guide:
//   - Teamfight scenario (≥ N allies near enemies)
//   - R will hit ≥ M enemies (max-cluster cast point)
//   - Master R toggle ON
//
// R fires regardless of master Q (slow alone has value — see guide:
// "even if you don't have follow-up, slow disrupts enemies").
bool aphelios::try_gravitum_r()
{
    if ( !gra_use_r_master_ )                     return false;
    if ( !gra_r_engage_combo_ )                   return false;
    if ( main_weapon_ != db::weapon::gravitum )   return false;
    if ( !is_spell_ready( db::SLOT_R ) )          return false;
    if ( player_ == nullptr )                     return false;
    if ( sdk::target_selector == nullptr )        return false;

    const int   my_team       = player_->get_team_id();
    const float r_range_safe  = db::R_RANGE - db::R_RANGE_SAFETY;

    math::vector3 best_pos { 0.f, 0.f, 0.f };
    int           best_count = 0;

    // Scan each enemy hero as potential cluster center; count enemies
    // within R radius of that center. Pick the densest cluster.
    for ( auto* center : sdk::target_selector->get_sorted_heroes() )
    {
        if ( center == nullptr || !center->is_valid() ) continue;
        if ( center->is_dead() || !center->is_visible() ) continue;
        if ( center->get_team_id() == my_team )         continue;

        const math::vector3 center_pos = center->get_position();
        if ( pos_.distance( center_pos ) > r_range_safe ) continue;

        int count = 0;
        for ( auto* other : sdk::target_selector->get_sorted_heroes() )
        {
            if ( other == nullptr || !other->is_valid() ) continue;
            if ( other->is_dead() || !other->is_visible() ) continue;
            if ( other->get_team_id() == my_team )        continue;
            if ( center_pos.distance( other->get_position() ) <= db::R_RADIUS )
                ++count;
        }

        if ( count > best_count )
        {
            best_count = count;
            best_pos   = center_pos;
        }
    }

    if ( best_count < gra_r_min_enemies_ )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_r_skip t=%.3f reason=enemies enemies=%d need=%d\n",
                                game_time_, best_count, gra_r_min_enemies_ );
        }
        return false;
    }

    const int allies = count_allies_near( best_pos, db::GRA_R_ALLY_RANGE );
    if ( allies < gra_r_min_allies_ )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_r_skip t=%.3f reason=allies allies=%d need=%d\n",
                                game_time_, allies, gra_r_min_allies_ );
        }
        return false;
    }

    const bool ok = cast_spell_pos( db::SLOT_R, best_pos );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] gra_r_fire t=%.3f enemies=%d allies=%d result=%d\n",
                            game_time_, best_count, allies, static_cast< int >( ok ) );
    }
    return ok;
}

}
