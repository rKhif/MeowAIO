#include "pch.hpp"

#include "champions/aphelios/aphelios.hpp"

#include "db/champions/aphelios.hpp"

namespace sora::champions
{

namespace db = sora::db::champions::aphelios;

bool aphelios::try_infernum_q1()
{
    if ( !inf_use_q_master_ )                               return false;
    if ( !inf_cal_swap_combo_ )                             return false;
    if ( !is_spell_ready( db::SLOT_Q ) )                    return false;
    if ( !is_spell_ready( db::SLOT_W ) )                    return false;

    auto* target = get_hero_target();
    if ( target == nullptr ) return false;
    if ( pos_.distance( target->get_position() ) > db::INFERNUM_AA_RANGE ) return false;

    const bool ok = cast_spell_pred( db::SLOT_Q, target, inf_q_hc() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] in_q1_fire t=%.3f result=%d\n",
                            game_time_, static_cast< int >( ok ) );
    }
    if ( !ok ) return false;

    infernum_step_            = infernum_step::q1_in_flight;
    infernum_target_id_       = target->get_network_id();
    infernum_started_at_      = game_time_;
    infernum_q1_processed_at_ = 0.f;
    return true;
}

void aphelios::clear_infernum_state()
{
    infernum_step_            = infernum_step::idle;
    infernum_started_at_      = 0.f;
    infernum_target_id_       = 0u;
    infernum_force_aa_until_  = 0.f;
    infernum_q1_processed_at_ = 0.f;
}

void aphelios::clear_inf_gra_state()
{
    inf_gra_step_           = inf_gra_step::idle;
    inf_gra_started_at_     = 0.f;
    inf_gra_q_processed_at_ = 0.f;
}

// Inf->Gra cross-weapon swap combo entry. Cast Q-Inf (Duskwave) — the
// offhand Gra AAs from Duskwave hits will apply Gra marks on each enemy
// in the cone. on_game_update polls for sufficient marks then casts W
// (swap to Gra main). After weapon flip, combo() drives BE on the
// accumulated marks for an AoE root.
//
// Gates: master Q (both Inf and Gra) + swap toggle + offhand=Gra +
// Q/W/BE ready + valid hero target in Duskwave range.
bool aphelios::try_inf_gra_q1()
{
    if ( !inf_use_q_master_ )                        return false;
    if ( !gra_use_q_master_ )                        return false;
    if ( !inf_gra_swap_combo_ )                      return false;
    if ( main_weapon_    != db::weapon::infernum )   return false;
    if ( offhand_weapon_ != db::weapon::gravitum )   return false;
    if ( !is_spell_ready( db::SLOT_Q ) )             return false;
    if ( !is_spell_ready( db::SLOT_W ) )             return false;

    auto* target = get_hero_target();
    if ( target == nullptr )                         return false;
    if ( pos_.distance( target->get_position() ) > db::INFERNUM_Q_RANGE )
        return false;

    const bool ok = cast_spell_pred( db::SLOT_Q, target, inf_q_hc() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] inf_gra_q1_fire t=%.3f result=%d\n",
                            game_time_, static_cast< int >( ok ) );
    }
    if ( !ok ) return false;

    inf_gra_step_           = inf_gra_step::q_in_flight;
    inf_gra_started_at_     = game_time_;
    inf_gra_q_processed_at_ = 0.f;
    return true;
}

// Killable estimate for Duskwave solo: Q-Inf damage + 1 offhand AA (fired
// by Duskwave on each enemy hit) + 1 follow-up AA from main weapon.
// If Cal is offhand, the offhand AA places a Cal mark and the follow-up
// AA consumes it for the mark-bonus damage at extended range.
bool aphelios::killable_with_duskwave_aa( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad = ad_ - player_->get_base_attack_damage();

    const float q_raw = db::inf_q_raw_damage( level_, bonus_ad );
    const float q_dmg = spell_damage( target, q_raw,
                                      sora::damage::damage_type::physical,
                                      sora::damage::spell_kind::normal );

    // Duskwave fires an offhand AA on each enemy hit (base AA damage).
    const float offhand_aa = aa_damage( target );

    // Player's next AA after Duskwave (main hand = Inf). If Cal is offhand,
    // this AA consumes the Cal mark placed by the Duskwave-fired Cal AA.
    const float follow_aa = aa_damage( target );

    float mark_bonus = 0.f;
    if ( offhand_weapon_ == db::weapon::calibrum )
    {
        mark_bonus = calibrum_mark_bonus_per_aa( target );
    }

    return sora::damage::will_kill( player_, target,
                                    q_dmg + offhand_aa + follow_aa + mark_bonus );
}

// Count enemy heroes near the Duskwave primary target — approximates how
// many enemies the cone will sweep through. Treats the cone as a cluster
// around the primary target (within INFERNUM_Q_RADIUS); not exact cone
// math but a reasonable predictor since Duskwave widens with distance
// and the primary target is on the centerline.
int aphelios::count_enemies_near_duskwave_target( game_object* primary ) const
{
    if ( primary == nullptr )                     return 0;
    if ( sdk::target_selector == nullptr )        return 0;
    if ( player_ == nullptr )                     return 0;

    const math::vector3 t_pos   = primary->get_position();
    const int           my_team = player_->get_team_id();
    int                 count   = 0;

    for ( auto* hero : sdk::target_selector->get_sorted_heroes() )
    {
        if ( hero == nullptr || !hero->is_valid() )   continue;
        if ( hero->is_dead() || !hero->is_visible() ) continue;
        if ( hero->get_team_id() == my_team )         continue;
        if ( t_pos.distance( hero->get_position() ) <= db::INFERNUM_Q_RADIUS )
            ++count;
    }
    return count;
}

// Q-Inf (Duskwave) standalone cast — runs outside the Inf->Cal swap
// combo (which is gated on offhand=Cal + swap toggle). Three fire paths:
//   1. killable_with_duskwave_aa: hero target dies from Q + AAs
//   2. Multi-target: cone would hit N+ enemies (cluster around target)
//   3. dump-ammo: last ammo + mana >= 50% (Sev/Gra pattern)
bool aphelios::try_infernum_duskwave_solo( game_object* target )
{
    if ( !inf_use_q_master_ )                     return false;
    if ( !inf_duskwave_solo_combo_ )              return false;
    if ( main_weapon_ != db::weapon::infernum )   return false;
    if ( !is_spell_ready( db::SLOT_Q ) )          return false;
    if ( target == nullptr )                      return false;
    if ( pos_.distance( target->get_position() ) > db::INFERNUM_Q_RANGE )
        return false;

    const bool killable  = killable_with_duskwave_aa( target );
    const bool dump_ammo = ( main_ammo_ == 1 && mana_percent_ >= 0.5f );

    int        multi_count = 0;
    bool       multi       = false;
    if ( inf_duskwave_multi_combo_ )
    {
        multi_count = count_enemies_near_duskwave_target( target );
        multi       = ( multi_count >= inf_q_min_targets_ );
    }

    if ( !killable && !dump_ammo && !multi )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] inf_duskwave_skip t=%.3f killable=0 dump=0 multi=%d/%d ammo=%d mana%%=%.2f\n",
                                game_time_, multi_count, inf_q_min_targets_,
                                main_ammo_, mana_percent_ );
        }
        return false;
    }

    const bool ok = cast_spell_pred( db::SLOT_Q, target, inf_q_hc() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] inf_duskwave_solo_fire t=%.3f result=%d killable=%d dump=%d multi=%d (need %d)\n",
                            game_time_, static_cast< int >( ok ),
                            static_cast< int >( killable ),
                            static_cast< int >( dump_ammo ),
                            multi_count, inf_q_min_targets_ );
    }
    return ok;
}

// R-Inf (Infernum-MV). Three paths:
//   1. Multi-target AoE engage: >= N enemies in R radius (slider).
//   2. Single-target finisher: hero target killable with R-Inf + 1 AA.
//   3. Anim cancel: after R fires, queue Duskwave for the signature
//      Inf-MV -> Duskwave burst combo (engine cancels R animation).
// Each path gated by its own toggle below master R.
bool aphelios::try_infernum_r()
{
    if ( !inf_use_r_master_ )                     return false;
    if ( main_weapon_ != db::weapon::infernum )   return false;
    if ( !is_spell_ready( db::SLOT_R ) )          return false;
    if ( player_ == nullptr )                     return false;
    if ( sdk::target_selector == nullptr )        return false;

    const int   my_team      = player_->get_team_id();
    const float r_range_safe = db::R_RANGE - db::R_RANGE_SAFETY;

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

    bool         fired       = false;
    const char*  cast_reason = "";

    // Path 1: Multi-target AoE.
    if ( inf_r_combo_ && best_count >= inf_r_min_enemies_ )
    {
        if ( cast_spell_pos( db::SLOT_R, best_pos ) )
        {
            fired       = true;
            cast_reason = "multi";
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] inf_r_fire_multi t=%.3f enemies=%d\n",
                                    game_time_, best_count );
            }
        }
    }

    auto* hero = get_hero_target();

    // Path 2: Single-target finisher — R-Inf + 1 AA kills hero target.
    if ( !fired && inf_r_solo_finisher_combo_
      && hero != nullptr
      && pos_.distance( hero->get_position() ) <= r_range_safe
      && killable_with_r_inf( hero ) )
    {
        if ( cast_spell_pos( db::SLOT_R, hero->get_position() ) )
        {
            fired       = true;
            cast_reason = "finisher";
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] inf_r_fire_finisher t=%.3f target=%u\n",
                                    game_time_, hero->get_network_id() );
            }
        }
    }

    if ( !fired )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] inf_r_skip t=%.3f best_cluster=%d need=%d\n",
                                game_time_, best_count, inf_r_min_enemies_ );
        }
        return false;
    }

    // Path 3: Anim cancel — queue Duskwave immediately after R. Engine
    // cancels remaining R animation, both spells fire in tight succession
    // for the signature Inf-MV -> Duskwave burst combo.
    if ( inf_anim_cancel_combo_
      && inf_use_q_master_
      && hero != nullptr
      && is_spell_ready( db::SLOT_Q )
      && pos_.distance( hero->get_position() ) <= db::INFERNUM_Q_RANGE )
    {
        const bool q_ok = cast_spell_pred( db::SLOT_Q, hero, inf_q_hc() );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] inf_anim_cancel_q t=%.3f reason=%s q_ok=%d\n",
                                game_time_, cast_reason, static_cast< int >( q_ok ) );
        }
    }

    return true;
}

bool aphelios::killable_with_r_inf( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;

    const float bonus_ad    = ad_ - player_->get_base_attack_damage();
    const float r_base_raw  = db::r_base_raw_damage( level_, bonus_ad );
    const float r_inf_extra = db::inf_mv_bonus_raw( level_, bonus_ad );
    const float r_final     = spell_damage( target, r_base_raw + r_inf_extra,
                                            sora::damage::damage_type::physical,
                                            sora::damage::spell_kind::ultimate_aoe );
    const float aa_final    = aa_damage( target );

    return sora::damage::will_kill( player_, target, r_final + aa_final );
}

// AA + Q anim cancel: triggered by on_create_missile when Infernum's AA
// missile spawns. At that point the AA is committed (damage will apply
// when the missile lands), so casting Q-Inf cancels only the AA recovery
// animation, not the AA itself. Earlier hooks (on_process_cast and
// on_after_attack) fire BEFORE the AA missile spawns and would cancel
// the AA entirely.
//
// Decision gate: killable_with_duskwave_aa OR multi-target (>= N enemies
// near primary). Same logic as try_infernum_duskwave_solo, but timed for
// post-AA anim cancel instead of pre-AA combo entry.
//
// Skipped when Inf->Cal swap state machine is active (that flow owns its
// own Q-Inf trigger via try_infernum_q1).
bool aphelios::try_inf_aa_q_anim_cancel()
{
    if ( !inf_aa_q_cancel_combo_ )                return false;
    if ( !inf_use_q_master_ )                     return false;
    if ( main_weapon_ != db::weapon::infernum )   return false;
    if ( infernum_step_ != infernum_step::idle )  return false;
    if ( !is_spell_ready( db::SLOT_Q ) )          return false;
    if ( !is_orbwalker_mode_active() )            return false;

    auto* target = get_hero_target();
    if ( target == nullptr )                      return false;
    if ( pos_.distance( target->get_position() ) > db::INFERNUM_Q_RANGE )
        return false;

    const bool killable = killable_with_duskwave_aa( target );
    int        multi_count = 0;
    bool       multi = false;
    if ( inf_duskwave_multi_combo_ )
    {
        multi_count = count_enemies_near_duskwave_target( target );
        multi       = ( multi_count >= inf_q_min_targets_ );
    }

    if ( !killable && !multi ) return false;

    const bool ok = cast_spell_pred( db::SLOT_Q, target, inf_q_hc() );
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] inf_aa_q_cancel t=%.3f result=%d killable=%d multi=%d/%d\n",
                            game_time_, static_cast< int >( ok ),
                            static_cast< int >( killable ),
                            multi_count, inf_q_min_targets_ );
    }
    return ok;
}

}
