#include "pch.hpp"

#include "champions/aphelios/aphelios.hpp"

#include "db/champions/aphelios.hpp"

namespace sora::champions
{

namespace db = sora::db::champions::aphelios;

bool aphelios::try_calibrum_q( game_object* target )
{
    if ( !cal_use_q_master_ )                             return false;
    if ( !can_cast( db::SLOT_Q, db::CALIBRUM_Q_DELAY ) )  return false;
    if ( !passes_ammo_check( target ) )                   return false;
    if ( !passes_calibrum_reserve() )                     return false;

    return cast_spell_pred( db::SLOT_Q, target, cal_q_hc() );
}

bool aphelios::passes_ammo_check( game_object* target ) const
{
    if ( main_ammo_ > cal_ammo_hold_ )       return true;
    if ( main_ammo_ <= 1 )                   return true;
    if ( target == nullptr )                 return false;
    return predicted_hp_on_hit( db::SLOT_Q, target ) <= 0.f;
}

bool aphelios::passes_calibrum_reserve() const
{
    if ( offhand_weapon_ != db::weapon::calibrum ) return true;
    if ( player_ == nullptr )                      return true;

    const float current_mana    = mana_;
    const float mana_after_cast = current_mana - db::Q_MANA_COST;
    return mana_after_cast >= db::CALIBRUM_RESERVE_MANA;
}

bool aphelios::try_qr_combo( game_object* target )
{
    const bool toggle_ok = cal_use_q_master_ && cal_use_r_master_ && cal_qr_combo_;
    const bool can_q     = can_cast( db::SLOT_Q, db::CALIBRUM_Q_DELAY );
    const bool r_ready   = is_spell_ready( db::SLOT_R );
    const float dist     = ( target != nullptr ) ? pos_.distance( target->get_position() ) : -1.f;
    const float r_range_safe = db::R_RANGE - db::R_RANGE_SAFETY;
    const bool in_range  = ( target != nullptr ) && dist <= r_range_safe;

    if ( trace_manual_ )
    {
        const uint32_t state = ( toggle_ok ? 1u : 0u )
                             | ( can_q     ? 2u : 0u )
                             | ( r_ready   ? 4u : 0u )
                             | ( in_range  ? 8u : 0u );
        if ( state != qr_gate_last_ )
        {
            g_sdk->log_console( "[trace] qr_gate t=%.3f toggle=%d can_q=%d r_ready=%d dist=%.0f in_range=%d\n",
                                game_time_,
                                static_cast< int >( toggle_ok ),
                                static_cast< int >( can_q ),
                                static_cast< int >( r_ready ),
                                dist,
                                static_cast< int >( in_range ) );
            qr_gate_last_ = state;
        }
    }

    if ( !toggle_ok )         return false;
    if ( !can_q )             return false;
    if ( !r_ready )           return false;
    if ( target == nullptr )  return false;
    if ( !in_range )          return false;
    if ( !killable_with_qr_aa2( target ) )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] qr_skip_not_killable t=%.3f\n", game_time_ );
        }
        return false;
    }

    // Atomic combo: chain kills (Q+R+AA+AA) → fire Q AND arm R together.
    // No "R only if needed" gate — the QR combo is a single committed
    // sequence. If the player engaged it, R goes with it.
    const bool q_cast = cast_spell_pred( db::SLOT_Q, target, cal_q_hc() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] qr_q_cast_result t=%.3f result=%d\n",
                            game_time_, static_cast< int >( q_cast ) );
    }
    if ( !q_cast ) return false;

    r_pending_            = true;
    r_pending_started_at_ = game_time_;
    q_cast_observed_      = false;
    return true;
}

bool aphelios::try_finish_qr_combo()
{
    if ( !r_pending_ ) return false;

    const float elapsed = game_time_ - r_pending_started_at_;
    if ( elapsed > db::QR_COMBO_TIMEOUT_SEC )
    {
        g_sdk->log_console( "[qr_timeout] t=%.3f elapsed=%.3f q_obs=%d\n",
                            game_time_, elapsed, static_cast< int >( q_cast_observed_ ) );
        r_pending_       = false;
        q_cast_observed_ = false;
        return false;
    }

    if ( !q_cast_observed_ )             return false;
    if ( is_currently_casting() )        return false;
    if ( !is_spell_ready( db::SLOT_R ) ) return false;

    auto* target = get_hero_target();
    if ( target == nullptr ) return false;

    if ( !cast_spell_pred( db::SLOT_R, target, cal_r_hc() ) ) return false;

    r_pending_       = false;
    q_cast_observed_ = false;

    if ( sdk::common != nullptr )
    {
        sdk::common->issue_order( game_object_order::attack_unit, target );
    }
    return true;
}

// Per-mark bonus damage of a Calibrum-empowered AA, mitigated.
// Wiki: "15 (+ 10% bonus AD) bonus physical damage per mark consumed".
float aphelios::calibrum_mark_bonus_per_aa( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return 0.f;
    const float bonus_ad = player_->get_attack_damage() - player_->get_base_attack_damage();
    const float raw      = db::CALIBRUM_MARK_BONUS_BASE
                         + db::CALIBRUM_MARK_BONUS_BAD * bonus_ad;
    return sora::damage::auto_attack_final( player_, target, raw );
}

// Calibrum mark consume always uses the offhand weapon. Different offhands
// modify the consume AA in different ways. Conservative model — only Infernum
// offhand's documented +10% AA dmg is applied. Other offhands (Severum heal,
// Gravitum slow, Crescendum chakram) don't alter target damage directly so
// keep multiplier at 1.0.
// Wiki: "consume Calibrum mark with Infernum basic attack does 10% more damage".
static float consume_aa_multiplier( sora::db::champions::aphelios::weapon offhand )
{
    if ( offhand == sora::db::champions::aphelios::weapon::infernum ) return 1.1f;
    return 1.0f;
}

// Marks placed by Q-Cal hit on target: 1 if target was unmarked, 0 if
// already marked (mark refreshes, doesn't stack on same target). Avoids
// overcounting bonus damage in the formulas below.
static int q_cal_new_marks_on_target( bool target_premarked )
{
    return target_premarked ? 0 : 1;
}

// Chain WITH R extension: Q-Cal lands first, places mark on target (refresh
// if premarked). AA#1 (forced via on_buff_gain on Q's mark) is the consume
// strike — off-hand AA at 1800 range. Per guide: a consume AA consumes ONLY
// the target's mark, but its damage scales with EVERY Calibrum mark currently
// active on the field. After AA#1, target unmarked. R lands shortly after
// (R has delay), placing a fresh mark on target. AA#2 is the consume strike
// of R's mark — active mark count is the same as AA#1 because R restored
// target's mark while other field marks remained untouched. The r_mv_bonus
// (+50/80/110) applies on AA#2, the AA that consumes R-applied mark. R AoE
// marks on adjacent enemies not modeled (single-target assumption).
bool aphelios::killable_with_qr_aa2( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad = ad_ - player_->get_base_attack_damage();

    // Q-Cal (Moonshot) damage — raw → spell_final
    const float q_raw = db::cal_q_raw_damage( level_, bonus_ad );
    const float q_dmg = spell_damage( target, q_raw,
                                      sora::damage::damage_type::physical,
                                      sora::damage::spell_kind::normal );

    // R base (MV projectile) damage — raw → spell_final, ultimate AoE amp
    const float r_raw = db::r_base_raw_damage( level_, bonus_ad );
    const float r_dmg = spell_damage( target, r_raw,
                                      sora::damage::damage_type::physical,
                                      sora::damage::spell_kind::ultimate_aoe );

    const float aa_dmg    = aa_damage( target ) * consume_aa_multiplier( offhand_weapon_ );
    const float per_mark  = calibrum_mark_bonus_per_aa( target );

    // Cal-MV mark consume bonus: +50/80/110 dmg on the consume AA that
    // triggers R-applied mark (AA#2 in this chain — R lands after AA#1).
    const float r_mv_bonus_raw = db::cal_mv_mark_bonus( level_ );
    const float r_mv_bonus     = sora::damage::auto_attack_final( player_, target, r_mv_bonus_raw );

    // Active mark count at consume time = field marks + Q-Cal's new mark on
    // target (0 if premarked, 1 if not). Both AA#1 and AA#2 see the same
    // count because R refreshes target's mark between the two consumes.
    const bool premarked    = is_marked( target->get_network_id() );
    const int  active_marks = static_cast< int >( marked_count_ )
                            + q_cal_new_marks_on_target( premarked );

    const float aa1 = aa_dmg + per_mark * static_cast< float >( active_marks );
    const float aa2 = aa_dmg + per_mark * static_cast< float >( active_marks ) + r_mv_bonus;

    return sora::damage::will_kill( player_, target, q_dmg + r_dmg + aa1 + aa2 );
}

// Chain WITHOUT R: Q-Cal hits (+0 or +1 mark on target), AA#1 is
// force-attacked via on_buff_gain when mark lands. No R = no 2nd reliable
// AA (orbwalker may or may not fire AA#2 depending on target position /
// animation / death). Used to decide if R is necessary — if 1 forced AA
// after Q is enough to kill, R is wasted.
bool aphelios::killable_with_q_aa1( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad = ad_ - player_->get_base_attack_damage();

    const float q_raw = db::cal_q_raw_damage( level_, bonus_ad );
    const float q_dmg = spell_damage( target, q_raw,
                                      sora::damage::damage_type::physical,
                                      sora::damage::spell_kind::normal );

    const float aa_dmg   = aa_damage( target ) * consume_aa_multiplier( offhand_weapon_ );
    const float per_mark = calibrum_mark_bonus_per_aa( target );

    const bool premarked = is_marked( target->get_network_id() );
    const int  aa1_marks = static_cast< int >( marked_count_ )
                         + q_cal_new_marks_on_target( premarked );

    const float aa1 = aa_dmg + per_mark * static_cast< float >( aa1_marks );

    return sora::damage::will_kill( player_, target, q_dmg + aa1 );
}

// R standalone: R + 1 forced empowered AA via mark-gain force-AA pattern.
// Used when Q is on cooldown so QR isn't an option, but R alone can
// execute. Doesn't include Q-Cal new mark (no Q cast).
bool aphelios::killable_with_r_aa1( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad = ad_ - player_->get_base_attack_damage();

    const float r_raw = db::r_base_raw_damage( level_, bonus_ad );
    const float r_dmg = spell_damage( target, r_raw,
                                      sora::damage::damage_type::physical,
                                      sora::damage::spell_kind::ultimate_aoe );

    const float aa_dmg   = aa_damage( target ) * consume_aa_multiplier( offhand_weapon_ );
    const float per_mark = calibrum_mark_bonus_per_aa( target );

    // R places a mark on target (AoE marks on adjacent enemies not modeled).
    // AA#1 is the consume strike of R's mark — consumes only the target's
    // mark but scales damage with every Calibrum mark currently active on
    // the field. If target was already marked before R, R refreshes (no
    // stack); otherwise R adds 1.
    const bool premarked = is_marked( target->get_network_id() );
    const int  aa1_marks = static_cast< int >( marked_count_ )
                         + ( premarked ? 0 : 1 );

    // Cal-MV mark consume bonus: +50/80/110 dmg on AA consuming R's mark.
    const float r_mv_bonus_raw = db::cal_mv_mark_bonus( level_ );
    const float r_mv_bonus     = sora::damage::auto_attack_final( player_, target, r_mv_bonus_raw );

    const float aa1 = aa_dmg + per_mark * static_cast< float >( aa1_marks ) + r_mv_bonus;

    return sora::damage::will_kill( player_, target, r_dmg + aa1 );
}

bool aphelios::is_marked( uint32_t net_id ) const
{
    for ( size_t i = 0u; i < marked_count_; ++i )
    {
        if ( marked_targets_[ i ] == net_id ) return true;
    }
    return false;
}

void aphelios::maybe_force_calibrum_bonus_aa()
{
    // Calibrum mark bonus range works whenever Cal is in hand — main OR
    // offhand. With Cal as main, player's normal AAs use Cal range; with
    // Cal as offhand, normal AAs use the other weapon's range but
    // mark-consume AAs still use Cal (extended). Force-AA on marked
    // heroes in extended range regardless of who's main.
    if ( main_weapon_    != db::weapon::calibrum
      && offhand_weapon_ != db::weapon::calibrum ) return;
    if ( infernum_step_ != infernum_step::idle ) return;
    if ( severum_step_  != severum_step::idle )  return;
    if ( r_pending_ )                            return;
    if ( !is_orbwalker_mode_active() )           return;
    if ( marked_count_ == 0u )                   return;
    if ( player_ == nullptr )                    return;
    if ( sdk::orbwalker == nullptr )             return;
    if ( sdk::target_selector == nullptr )       return;
    if ( would_cancel_attack() )                 return;
    if ( !is_attack_ready_in( 0.f ) )            return;

    // Orbwalker already has a target in normal AA range — let it work.
    if ( sdk::orbwalker->get_target_in_attack_range() != nullptr ) return;

    // Find highest-priority marked hero in bonus range (TS-sorted).
    const int team = player_->get_team_id();
    for ( auto* hero : sdk::target_selector->get_sorted_heroes() )
    {
        if ( hero == nullptr || !hero->is_valid() )     continue;
        if ( hero->is_dead() || !hero->is_visible() )   continue;
        if ( hero->get_team_id() == team )              continue;
        if ( !is_marked( hero->get_network_id() ) )     continue;

        const float dist = pos_.distance( hero->get_position() );
        if ( dist > db::CALIBRUM_BONUS_RANGE )          continue;
        if ( dist <= aa_range_ )                        continue;

        sdk::orbwalker->attack( hero );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] cal_bonus_aa t=%.3f target=%u dist=%.0f\n",
                                game_time_,
                                hero->get_network_id(),
                                dist );
        }
        return;
    }
}

// Cal-MV standalone: cast R as finisher when QR isn't an option (Q on
// cooldown) but R + 1 forced AA can execute. Skipped when QR is viable —
// QR delivers more damage so prefer it when both options exist.
bool aphelios::try_calibrum_r_standalone( game_object* target )
{
    if ( !cal_use_r_master_ )                     return false;
    if ( main_weapon_ != db::weapon::calibrum )   return false;
    if ( !is_spell_ready( db::SLOT_R ) )          return false;
    if ( target == nullptr )                      return false;

    const float dist         = pos_.distance( target->get_position() );
    const float r_range_safe = db::R_RANGE - db::R_RANGE_SAFETY;
    if ( dist > r_range_safe )                    return false;

    // Defer to QR if Q is castable AND QR-chain would kill — QR has more
    // damage so it's the strictly better option when available.
    if ( cal_use_q_master_ && cal_qr_combo_
      && can_cast( db::SLOT_Q, db::CALIBRUM_Q_DELAY )
      && killable_with_qr_aa2( target ) )
    {
        return false;
    }

    // Only commit R if R + 1 forced empowered AA actually kills.
    if ( !killable_with_r_aa1( target ) )         return false;

    const bool ok = cast_spell_pred( db::SLOT_R, target, cal_r_hc() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] cal_r_standalone t=%.3f result=%d\n",
                            game_time_, static_cast< int >( ok ) );
    }
    return ok;
}

}
