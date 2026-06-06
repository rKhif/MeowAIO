#include "pch.hpp"

#include "champions/aphelios/aphelios.hpp"

#include "db/champions/aphelios.hpp"

namespace sora::champions
{

namespace db = sora::db::champions::aphelios;

bool aphelios::killable_with_severum_q( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    // Onslaught fires N rapid-fire hits over its channel (min 6 at base AS).
    // Per-hit damage scales with champion level. Conservative: assume min
    // hit count; if AS is higher we'll just over-deliver.
    const float bonus_ad   = ad_ - player_->get_base_attack_damage();
    const float per_hit    = db::sev_q_per_hit_raw_damage( level_, bonus_ad );
    const float total_raw  = per_hit * static_cast< float >( db::SEV_Q_MIN_HITS );
    const float q_dmg      = spell_damage( target, total_raw,
                                           sora::damage::damage_type::physical,
                                           sora::damage::spell_kind::normal );
    return sora::damage::will_kill( player_, target, q_dmg );
}

// Swap path: only with Crescendum offhand. Casts Q + enters state machine
// that auto-W-swaps to Crescendum after Q ends (rapid-fire builds chakram
// stacks; swap to Crescendum amplifies DPS via accumulated chakrams).
bool aphelios::try_severum_swap_q()
{
    if ( !sev_use_q_master_ )                                return false;
    if ( !sev_cres_swap_combo_ )                             return false;
    if ( offhand_weapon_ != db::weapon::crescendum )         return false;
    if ( !is_spell_ready( db::SLOT_Q ) )                     return false;
    if ( !is_spell_ready( db::SLOT_W ) )                     return false;

    auto* target = get_hero_target();
    if ( target == nullptr ) return false;
    if ( pos_.distance( target->get_position() ) > db::SEVERUM_AA_RANGE ) return false;

    const bool ok = cast_spell_self( db::SLOT_Q );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] sev_swap_q_fire t=%.3f result=%d\n",
                            game_time_, static_cast< int >( ok ) );
    }
    if ( !ok ) return false;

    severum_step_           = severum_step::q_active;
    severum_started_at_     = game_time_;
    severum_q_processed_at_ = 0.f;
    return true;
}

// Solo path: cast Severum Q standalone (no state machine, no auto-swap).
// Exceptions to the swap-with-Crescendum rule: fire anyway if either
//   (a) Severum Q will outright kill, or
//   (b) main ammo is at 1 AND mana >= 50% (dump the last bullet productively).
bool aphelios::try_severum_solo_q()
{
    if ( !sev_use_q_master_ )            return false;
    if ( !sev_solo_q_combo_ )            return false;
    if ( !is_spell_ready( db::SLOT_Q ) ) return false;

    auto* target = get_hero_target();
    if ( target == nullptr ) return false;
    if ( pos_.distance( target->get_position() ) > db::SEVERUM_AA_RANGE ) return false;

    const bool killable  = killable_with_severum_q( target );
    const bool dump_ammo = ( main_ammo_ == 1 && mana_percent_ >= 0.5f );

    if ( !killable && !dump_ammo )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] sev_solo_skip t=%.3f killable=0 dump=0 ammo=%d mana%%=%.2f\n",
                                game_time_, main_ammo_, mana_percent_ );
        }
        return false;
    }

    const bool ok = cast_spell_self( db::SLOT_Q );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] sev_solo_q_fire t=%.3f result=%d killable=%d dump=%d\n",
                            game_time_,
                            static_cast< int >( ok ),
                            static_cast< int >( killable ),
                            static_cast< int >( dump_ammo ) );
    }
    return ok;
}

void aphelios::clear_severum_state()
{
    severum_step_           = severum_step::idle;
    severum_started_at_     = 0.f;
    severum_q_processed_at_ = 0.f;
}

}
