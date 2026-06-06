#include "pch.hpp"

#include "champions/aphelios/aphelios.hpp"

#include "db/champions/aphelios.hpp"

namespace sora::champions
{

namespace db = sora::db::champions::aphelios;

// Post-R kick tick rate (10 Hz).
static constexpr float POST_R_KICK_INTERVAL = 0.1f;
// Consecutive orbwalker->attack failures before falling back to direct
// engine order. ~300ms of stalled tracker is a confident "wedged" signal.
static constexpr int   POST_R_KICK_FAIL_THRESHOLD = 3;

// Reset missile-tracking state (out/returning/net_id). Does NOT touch
// chakram_clear_at_ — the timer is left to expire naturally so that
// weapon swaps during chakram flight don't leave AA permanently
// unblocked when the player swaps back to Crescendum mid-cycle.
void aphelios::clear_chakram_state()
{
    chakram_out_       = false;
    chakram_returning_ = false;
    chakram_net_id_    = 0u;
}

// Timer-based AA gate: chakram is "in flight" iff Crescendum is main AND
// the cycle-time deadline hasn't elapsed yet. Self-healing — even if the
// missile tracking events are missed (e.g. weapon swap mid-flight, missile
// callback not delivered), the timer expires on its own. Faster path also:
// when the returning chakram missile is properly deleted, on_delete_object
// resets chakram_clear_at_ to 0 for early termination.
bool aphelios::is_chakram_in_flight() const
{
    if ( main_weapon_ != db::weapon::crescendum ) return false;
    return game_time_ < chakram_clear_at_;
}

// Sentry Q (Crescendum's basic ability). Ground-targeted: deploys a
// small turret at the cast position after 0.35s, lasts 4s active /
// up to 20s inactive. Sentry attacks with a replica of the off-hand
// weapon (applying its off-hand effects to its hits — Cal mark, Gra
// slow, Sev heal, Inf cone).
//
// Cast strategy: target hero position so Sentry deploys close enough
// to AA them. Future Phase Cres-B can add cluster-based placement and
// off-hand-specific positioning (Cal-Sentry vs Gra-Sentry vs etc.).
//
// Gates: master Q + combo toggle + Cres main + Q ready + hero target
// in Sentry cast range.
bool aphelios::try_crescendum_sentry()
{
    if ( !cres_use_q_master_ )                     return false;
    if ( !cres_sentry_combo_ )                     return false;
    if ( main_weapon_ != db::weapon::crescendum )  return false;
    if ( !is_spell_ready( db::SLOT_Q ) )           return false;

    auto* target = get_hero_target();
    if ( target == nullptr )                       return false;
    if ( pos_.distance( target->get_position() ) > db::SENTRY_RANGE )
        return false;

    const bool ok = cast_spell_pos( db::SLOT_Q, target->get_position() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] cres_sentry_fire t=%.3f target=%u result=%d\n",
                            game_time_, target->get_network_id(),
                            static_cast< int >( ok ) );
    }
    return ok;
}

// R-Cres (Crescendum-MV) AoE engage: generates 5 chakrams base + 1 per
// target hit (min 6, max 10). Heavy chakram boost for subsequent AAs.
// Cast on densest enemy cluster to maximize chakram gen.
//
// Gate: master R + combo toggle + cluster has >= N enemies (slider).
bool aphelios::try_crescendum_r()
{
    if ( !cres_use_r_master_ )                     return false;
    if ( main_weapon_ != db::weapon::crescendum )  return false;
    if ( !is_spell_ready( db::SLOT_R ) )           return false;
    if ( player_ == nullptr )                      return false;
    if ( sdk::target_selector == nullptr )         return false;

    const int   my_team      = player_->get_team_id();
    const float r_range_safe = db::R_RANGE - db::R_RANGE_SAFETY;

    // Cal-Cres opener: in Cal-Cres pairing, R-Cres generates 6-10 chakrams
    // which boost AAs during the upcoming Cal-Sentry mark-spam phase.
    // Cast on hero target (single-target ok — value is chakram gen, not
    // AoE damage). Per guide: "Using Moonlight Vigil alongside Cal-Sentry".
    if ( cres_cal_mv_opener_
      && offhand_weapon_ == db::weapon::calibrum )
    {
        auto* hero = get_hero_target();
        if ( hero != nullptr
          && pos_.distance( hero->get_position() ) <= r_range_safe )
        {
            const bool ok = cast_spell_pos( db::SLOT_R, hero->get_position() );
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] cres_r_cal_opener t=%.3f result=%d\n",
                                    game_time_, static_cast< int >( ok ) );
            }
            if ( ok ) return true;
        }
    }

    if ( !cres_r_combo_ )                          return false;

    math::vector3 best_pos { 0.f, 0.f, 0.f };
    int           best_count = 0;

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

    if ( best_count < cres_r_min_enemies_ )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] cres_r_skip t=%.3f enemies=%d need=%d\n",
                                game_time_, best_count, cres_r_min_enemies_ );
        }
        return false;
    }

    const bool ok = cast_spell_pos( db::SLOT_R, best_pos );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] cres_r_fire t=%.3f enemies=%d result=%d\n",
                            game_time_, best_count, static_cast< int >( ok ) );
    }
    return ok;
}

// Post-R AA assist for Crescendum main.
//
// Problem: when R is cast with Crescendum as the main weapon, R spawns
// many secondary missiles (ApheliosOffHandOrbitMissile,
// ApheliosCrescendumAttackMisInMini, ApheliosCrescendumLineAttack) that
// pollute the orbwalker's internal "attacks in flight" tracker. The
// orbwalker then stops calling on_before_attack for several seconds —
// even with our own state clean (no chakram, no r_pending, not casting).
//
// Workaround: while inside an armed kick window (set on R missile spawn
// in on_create_missile when main = Crescendum), tick at 10 Hz:
//   1. Try sdk::orbwalker->attack(target) — this is the documented
//      "force attack" path (Kog'Maw W pattern), but it still respects
//      the orbwalker's internal readiness gate.
//   2. After POST_R_KICK_FAIL_THRESHOLD consecutive refusals (~300ms),
//      the tracker is confirmed wedged. Fall back to direct
//      sdk::common->issue_order(attack_unit, target) — bypasses the
//      orbwalker entirely, sending a raw engine order. Risky during
//      normal play (cancels queued AAs) but safe here because the
//      orbwalker is doing nothing.
//
// The window is re-armed in on_after_attack while still active and main
// is Crescendum (each successful AA spawns another batch of orbiting
// missiles, restarting the stall). It expires after POST_R_KICK_WINDOW_SEC
// of no AAs (orbwalker recovered or player stopped engaging).
void aphelios::tick_post_r_kick_assist()
{
    if ( post_r_kick_until_ <= 0.f ) return;

    if ( game_time_ >= post_r_kick_until_ )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] post_r_kick_expired t=%.3f\n", game_time_ );
        }
        post_r_kick_until_       = 0.f;
        post_r_kick_fail_streak_ = 0;
        return;
    }

    if ( !is_orbwalker_mode_active() )      return;
    if ( sdk::orbwalker == nullptr )        return;
    if ( is_currently_casting() )           return;
    if ( is_chakram_in_flight() )           return;
    if ( game_time_ - post_r_kick_last_at_ < POST_R_KICK_INTERVAL ) return;

    auto* target = get_hero_target();
    if ( target == nullptr ) return;

    post_r_kick_last_at_ = game_time_;

    if ( sdk::orbwalker->attack( target ) )
    {
        post_r_kick_fail_streak_ = 0;
        return;
    }

    ++post_r_kick_fail_streak_;

    if ( post_r_kick_fail_streak_ >= POST_R_KICK_FAIL_THRESHOLD
      && sdk::common != nullptr )
    {
        sdk::common->issue_order( game_object_order::attack_unit, target );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] post_r_kick_fallback_engine_order t=%.3f streak=%d\n",
                                game_time_, post_r_kick_fail_streak_ );
        }
    }
}

}
