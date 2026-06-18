#include "pch.hpp"

#include "champions/zyra/zyra.hpp"

#include "db/champions/zyra.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace sora::champions
{

namespace sc = sora::db::champions::zyra;

namespace
{
    static constexpr float instant_projectile_speed = 1e9f;

    enum role_mode : int
    {
        role_auto = 0,
        role_support,
        role_apc_bot,
        role_jungle,
    };

    enum playstyle_mode : int
    {
        play_poke_control = 0,
        play_catch_burst,
        play_anti_engage,
    };

    enum plant_policy_mode : int
    {
        plant_smart = 0,
        plant_aggressive,
        plant_conserve_one,
        plant_burst,
    };

    enum r_policy_mode : int
    {
        r_disabled = 0,
        r_combo_only,
        r_smart_teamfight,
        r_anti_engage,
    };
}

float zyra::segment_distance( const math::vector3& p, const math::vector3& a, const math::vector3& b )
{
    const float dx = b.x - a.x;
    const float dz = b.z - a.z;
    const float len_sq = dx * dx + dz * dz;
    if ( len_sq <= 0.f )
    {
        const float ex = p.x - a.x;
        const float ez = p.z - a.z;
        return std::sqrtf( ex * ex + ez * ez );
    }

    const float t = std::clamp( ( ( p.x - a.x ) * dx + ( p.z - a.z ) * dz ) / len_sq, 0.f, 1.f );
    const float cx = a.x + t * dx;
    const float cz = a.z + t * dz;
    const float fx = p.x - cx;
    const float fz = p.z - cz;
    return std::sqrtf( fx * fx + fz * fz );
}

math::vector3 zyra::offset_from_dir( const math::vector3& pos, const math::vector3& dir, float amount )
{
    return { pos.x + dir.x * amount, pos.y, pos.z + dir.z * amount };
}

bool zyra::is_valid_enemy( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;
    if ( !target->is_valid() || target->is_dead() || !target->is_visible() || !target->is_targetable() ) return false;
    return target->get_team_id() != player_->get_team_id();
}

bool zyra::is_valid_farm_target( game_object* target ) const
{
    if ( target == nullptr || player_ == nullptr ) return false;
    if ( !target->is_valid() || target->is_dead() || !target->is_visible() || !target->is_targetable() ) return false;
    return target->get_team_id() != player_->get_team_id();
}

bool zyra::has_mana_for( float cost ) const
{
    return mana_ >= cost;
}

bool zyra::can_use_q( float min_mana_pct ) const
{
    if ( !q_enabled_ || mana_percent_ < std::max( min_mana_pct, 0.f ) ) return false;
    return can_cast( Q ) && has_mana_for( sc::q_mana_cost( spells_[ Q ].rank ) );
}

bool zyra::can_use_w() const
{
    return w_enabled_ && w_charges() > 0;
}

bool zyra::can_use_e( float min_mana_pct ) const
{
    if ( !e_enabled_ || mana_percent_ < std::max( min_mana_pct, 0.f ) ) return false;
    return can_cast( E ) && has_mana_for( sc::e_mana_cost( spells_[ E ].rank ) );
}

bool zyra::can_use_r() const
{
    if ( !r_enabled_ || r_policy_ == r_disabled ) return false;
    return is_spell_ready( R ) && has_mana_for( sc::r_mana_cost( spells_[ R ].rank ) );
}

bool zyra::is_combo_active() const
{
    return sdk::orbwalker != nullptr && sdk::orbwalker->combo();
}

bool zyra::is_sane_cast_pos( const math::vector3& pos, float max_range ) const
{
    if ( player_ == nullptr ) return false;
    if ( !std::isfinite( pos.x ) || !std::isfinite( pos.z ) ) return false;
    const float d = pos_.distance( pos );
    if ( d < 1.f ) return false;
    return d <= max_range + 250.f;
}

int zyra::effective_role() const
{
    if ( role_mode_ != role_auto )
        return role_mode_;

    if ( sdk::orbwalker != nullptr && sdk::orbwalker->fast_clear() )
        return role_jungle;

    if ( sdk::orbwalker != nullptr && sdk::orbwalker->clear() )
        return role_apc_bot;

    return role_support;
}

int zyra::w_charges() const
{
    if ( player_ == nullptr ) return 0;

    auto* spell = player_->get_spell( W );
    if ( spell == nullptr ) return is_spell_ready( W ) ? 1 : 0;

    const int charges = static_cast< int >( spell->get_charges() );
    if ( charges > 0 ) return charges;

    return is_spell_ready( W ) ? 1 : 0;
}

bool zyra::can_spend_seed( bool critical ) const
{
    if ( !can_use_w() ) return false;

    const int charges = w_charges();
    if ( effective_role() == role_jungle ) return charges > 0;
    if ( critical || plant_policy_ == plant_burst ) return charges > 0;
    if ( plant_policy_ == plant_aggressive ) return charges > 0;
    if ( plant_policy_ == plant_conserve_one ) return charges > 1;

    return smart_conserve_last_ ? charges > 1 : charges > 0;
}

bool zyra::should_double_seed( game_object* target, bool critical ) const
{
    if ( !is_valid_enemy( target ) ) return false;
    if ( w_charges() < 2 ) return false;
    if ( effective_role() == role_jungle ) return true;
    if ( plant_policy_ == plant_burst && critical ) return true;
    if ( plant_policy_ == plant_aggressive && ( critical || is_combo_active() ) ) return true;
    if ( !smart_double_on_kill_ ) return false;
    return critical && health_with_shields( target ) <= get_combo_damage( target );
}

bool zyra::should_use_spell_for_harass() const
{
    if ( mana_percent_ < min_mana_pct_ ) return false;
    return true;
}

bool zyra::harass_q_shoves_wave( game_object* target ) const
{
    if ( !harass_respect_wave_ || target == nullptr ) return false;
    if ( effective_role() != role_support ) return false;
    return count_minions_in_radius( target->get_position(), sc::Q_RADIUS, false ) > 0;
}

math::vector3 zyra::clamp_seed_pos( const math::vector3& pos ) const
{
    if ( player_ == nullptr ) return pos;
    if ( pos_.distance( pos ) <= sc::W_RANGE ) return pos;
    return pos_.extended( pos, sc::W_RANGE );
}

math::vector3 zyra::robust_seed_on_line( const math::vector3& spell_pos, float trigger_radius, float plant_range ) const
{
    const float spell_dist = std::max( 1.f, pos_.distance( spell_pos ) );
    const float trigger_margin = std::max( 80.f, trigger_radius - sc::W_SEED_SAFE_MARGIN );
    const float seed_dist = std::clamp( spell_dist - trigger_margin * 0.5f, 120.f, std::min( sc::W_RANGE, spell_dist ) );
    math::vector3 seed = pos_.extended( spell_pos, seed_dist );

    if ( seed.distance( spell_pos ) > trigger_margin )
        seed = spell_pos.extended( seed, trigger_margin );

    if ( seed.distance( spell_pos ) > plant_range )
        seed = seed.extended( spell_pos, seed.distance( spell_pos ) - plant_range + 50.f );

    return clamp_seed_pos( seed );
}

math::vector3 zyra::robust_seed_near_target( game_object* target, const math::vector3& spell_pos, float trigger_radius, float plant_range ) const
{
    math::vector3 seed = spell_pos;
    math::vector3 dir = ( spell_pos - pos_ ).to_direction();

    if ( smart_lead_ && target != nullptr && target->is_moving() )
    {
        const math::vector3 move_dir = target->get_direction();
        const float len_sq = move_dir.x * move_dir.x + move_dir.z * move_dir.z;
        if ( len_sq > 0.001f )
            dir = move_dir;
    }

    const float offset = std::min( trigger_radius - sc::W_SEED_SAFE_MARGIN, 95.f );
    seed = offset_from_dir( spell_pos, dir, offset );

    const float trigger_margin = std::max( 80.f, trigger_radius - sc::W_SEED_SAFE_MARGIN );
    if ( seed.distance( spell_pos ) > trigger_margin )
        seed = spell_pos.extended( seed, trigger_margin );

    if ( target != nullptr && seed.distance( target->get_position() ) > plant_range - 50.f )
        seed = target->get_position().extended( seed, plant_range - 50.f );

    return clamp_seed_pos( seed );
}

zyra::seed_plan zyra::plan_seed_for_q( game_object* target, const math::vector3& q_pos, bool critical ) const
{
    if ( !plant_on_q_ || !can_spend_seed( critical ) ) return {};

    const math::vector3 seed = robust_seed_near_target( target, q_pos, sc::Q_SEED_TRIGGER_RADIUS, sc::Q_PLANT_RANGE );
    if ( seed.distance( q_pos ) > sc::Q_SEED_TRIGGER_RADIUS - 30.f ) return {};

    int score = 45;
    if ( target != nullptr && seed.distance( target->get_position() ) <= sc::Q_PLANT_RANGE )
        score += 35;
    if ( critical ) score += 20;

    return { true, seed, score };
}

zyra::seed_plan zyra::plan_seed_for_e( game_object* target, const math::vector3& e_pos, bool critical ) const
{
    if ( !plant_on_e_ || !can_spend_seed( critical ) ) return {};

    const math::vector3 seed = robust_seed_on_line( e_pos, sc::E_SEED_TRIGGER_RADIUS, sc::E_PLANT_RANGE );
    const math::vector3 line_end = pos_.extended( e_pos, sc::E_RANGE );
    if ( segment_distance( seed, pos_, line_end ) > sc::E_SEED_TRIGGER_RADIUS - 45.f ) return {};

    int score = 55;
    if ( target != nullptr && seed.distance( target->get_position() ) <= sc::E_PLANT_RANGE + 120.f )
        score += 25;
    if ( critical ) score += 20;

    return { true, seed, score };
}

zyra::seed_plan zyra::plan_seed_for_r( const math::vector3& r_pos, bool critical ) const
{
    if ( !plant_on_r_ || !can_spend_seed( critical ) ) return {};

    math::vector3 seed = clamp_seed_pos( pos_.extended( r_pos, std::min( pos_.distance( r_pos ), sc::W_RANGE ) ) );
    if ( seed.distance( r_pos ) > sc::R_RADIUS - sc::W_SEED_SAFE_MARGIN )
        seed = r_pos.extended( seed, sc::R_RADIUS - sc::W_SEED_SAFE_MARGIN );

    return { true, seed, critical ? 90 : 65 };
}

void zyra::schedule_seed( const math::vector3& pos, bool critical )
{
    if ( !is_sane_cast_pos( pos, sc::W_RANGE ) ) return;

    for ( auto& seed : pending_seeds_ )
    {
        if ( seed.active ) continue;
        seed.active = true;
        seed.pos = pos;
        seed.expire_time = game_time_ + sc::W_PENDING_EXPIRE;
        seed.critical = critical;
        return;
    }
}

bool zyra::cast_pending_seed( pending_seed& seed )
{
    if ( !seed.active ) return false;
    if ( game_time_ > seed.expire_time )
    {
        seed.active = false;
        return false;
    }

    if ( !can_spend_seed( seed.critical ) ) return false;
    if ( game_time_ - last_w_cast_time_ < sc::W_RETRY_DELAY ) return false;

    const math::vector3 w_pos = clamp_seed_pos( seed.pos );
    if ( !is_sane_cast_pos( w_pos, sc::W_RANGE ) ) { seed.active = false; return false; }
    if ( !cast_spell_pos( W, w_pos ) ) return false;

    seed.active = false;
    last_w_cast_time_ = game_time_;
    return true;
}

void zyra::process_pending_seeds()
{
    for ( auto& seed : pending_seeds_ )
    {
        if ( cast_pending_seed( seed ) )
            return;
    }
}

bool zyra::try_q( game_object* target, int hitchance, bool critical )
{
    if ( !is_valid_enemy( target ) ) return false;
    if ( !can_use_q( critical ? 0.f : min_mana_pct_ ) ) return false;
    if ( sdk::prediction == nullptr ) return false;

    auto pred = spells_[ Q ].pred;
    pred.expected_hitchance = hitchance;
    pred.range = sc::Q_RANGE + sc::Q_CAST_RANGE_BUFFER;

    const auto result = sdk::prediction->predict( target, pred );
    if ( !result.is_valid || result.hitchance < hitchance ) return false;

    math::vector3 q_pos = result.cast_position;
    if ( pos_.distance( q_pos ) > sc::Q_RANGE )
        q_pos = pos_.extended( q_pos, sc::Q_RANGE );
    if ( !is_sane_cast_pos( q_pos, sc::Q_RANGE ) ) return false;

    const auto seed = plan_seed_for_q( target, q_pos, critical );
    if ( !cast_spell_pos( Q, q_pos ) ) return false;

    if ( seed.valid )
    {
        schedule_seed( seed.pos, critical );
        if ( should_double_seed( target, critical ) )
        {
            const math::vector3 side = ( seed.pos - pos_ ).to_direction().perpendicular_left();
            schedule_seed( clamp_seed_pos( offset_from_dir( seed.pos, side, sc::W_SECOND_SEED_OFFSET ) ), true );
        }
    }

    return true;
}

bool zyra::try_e( game_object* target, int hitchance, bool critical )
{
    if ( !is_valid_enemy( target ) ) return false;
    if ( !can_use_e( critical ? 0.f : min_mana_pct_ ) ) return false;
    if ( sdk::prediction == nullptr ) return false;

    auto pred = spells_[ E ].pred;
    pred.expected_hitchance = hitchance;
    const auto result = sdk::prediction->predict( target, pred );
    if ( !result.is_valid || result.hitchance < hitchance ) return false;

    math::vector3 e_pos = result.cast_position;
    if ( pos_.distance( e_pos ) > sc::E_RANGE )
        e_pos = pos_.extended( e_pos, sc::E_RANGE );
    if ( !is_sane_cast_pos( e_pos, sc::E_RANGE ) ) return false;

    const auto seed = plan_seed_for_e( target, e_pos, critical );
    const bool seed_first = seed.valid && ( critical || distance_to( target ) <= 450.f );
    bool seed_cast_now = false;
    if ( seed_first && can_spend_seed( critical ) && is_sane_cast_pos( seed.pos, sc::W_RANGE )
      && cast_spell_pos( W, seed.pos ) )
    {
        seed_cast_now = true;
        last_w_cast_time_ = game_time_;
    }

    if ( !cast_spell_pos( E, e_pos ) ) return false;

    if ( seed.valid && !seed_cast_now )
    {
        schedule_seed( seed.pos, critical );
        if ( should_double_seed( target, critical ) )
        {
            const math::vector3 line = ( e_pos - pos_ ).to_direction();
            schedule_seed( clamp_seed_pos( offset_from_dir( seed.pos, line, sc::W_SECOND_SEED_OFFSET ) ), true );
        }
    }
    else if ( seed.valid && seed_cast_now && should_double_seed( target, critical ) )
    {
        const math::vector3 line = ( e_pos - pos_ ).to_direction();
        schedule_seed( clamp_seed_pos( offset_from_dir( seed.pos, line, sc::W_SECOND_SEED_OFFSET ) ), true );
    }

    return true;
}

bool zyra::try_r_smart( game_object* target, bool critical, bool force )
{
    if ( !is_valid_enemy( target ) || !can_use_r() ) return false;
    if ( !force )
    {
        if ( r_policy_ == r_combo_only && !is_combo_active() ) return false;
        if ( r_policy_ == r_anti_engage && !critical ) return false;
    }

    math::vector3 r_pos = target->get_position();
    if ( pos_.distance( r_pos ) > sc::R_RANGE )
        r_pos = pos_.extended( r_pos, sc::R_RANGE );
    if ( !is_sane_cast_pos( r_pos, sc::R_RANGE ) ) return false;

    const int nearby = count_enemies_in_radius( r_pos, sc::R_RADIUS );
    const bool killable = health_with_shields( target ) <= get_combo_damage( target );
    const int score = nearby * 35 + ( critical ? 30 : 0 ) + ( killable ? 25 : 0 );
    if ( !force && nearby < min_r_targets_ && !killable && score < min_r_cluster_score_ ) return false;

    if ( !cast_spell_pos( R, r_pos ) ) return false;

    const auto seed = plan_seed_for_r( r_pos, critical );
    if ( seed.valid && ( can_use_q( 0.f ) || can_use_e( 0.f ) ) )
        schedule_seed( seed.pos, critical );

    return true;
}

bool zyra::try_r_on_best_cluster( bool critical )
{
    if ( !can_use_r() || g_sdk == nullptr || g_sdk->object_manager == nullptr ) return false;

    game_object* best = nullptr;
    int best_score = -1;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( hero ) ) continue;
        if ( distance_to( hero ) > sc::R_RANGE + sc::R_RADIUS ) continue;

        const int count = count_enemies_in_radius( hero->get_position(), sc::R_RADIUS );
        const int score = count * 35 + ( health_with_shields( hero ) <= get_combo_damage( hero ) ? 25 : 0 );
        if ( score > best_score )
        {
            best = hero;
            best_score = score;
        }
    }

    return best != nullptr && best_score >= min_r_cluster_score_ && try_r_smart( best, critical );
}

bool zyra::try_r_semi()
{
    if ( !can_use_r() ) return false;

    auto* target = get_hero_target();
    if ( is_valid_enemy( target ) && distance_to( target ) <= sc::R_RANGE + sc::R_RADIUS )
        return try_r_smart( target, true, true );

    return false;
}

bool zyra::try_e_semi()
{
    auto* target = get_hero_target();
    if ( !is_valid_enemy( target ) ) return false;
    return try_e( target, pred_sdk::hitchance::medium, true );
}

bool zyra::enemy_in_e_threat() const
{
    if ( g_sdk == nullptr || g_sdk->object_manager == nullptr ) return false;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
        if ( is_valid_enemy( hero ) && distance_to( hero ) <= sc::E_RANGE + hero->get_bounding_radius() )
            return true;
    return false;
}

bool zyra::try_prepare_zone()
{
    if ( !w_enabled_ || !can_use_w() ) return false;
    if ( is_combo_active() ) return false;
    if ( sdk::orbwalker != nullptr && sdk::orbwalker->harass() ) return false;
    if ( game_time_ - last_w_cast_time_ < sc::W_RETRY_DELAY ) return false;
    if ( g_sdk == nullptr || g_sdk->hud_manager == nullptr ) return false;

    const int reserve = enemy_in_e_threat() ? 1 : 0;
    if ( w_charges() <= reserve ) return false;

    const math::vector3 cursor = clamp_seed_pos( g_sdk->hud_manager->get_cursor_position() );
    if ( !is_sane_cast_pos( cursor, sc::W_RANGE ) ) return false;

    math::vector3 seed = cursor;
    if ( w_charges() <= 1 )
    {
        const math::vector3 side = ( cursor - pos_ ).to_direction().perpendicular_left();
        const math::vector3 spread = clamp_seed_pos( offset_from_dir( cursor, side, sc::W_SECOND_SEED_OFFSET ) );
        if ( is_sane_cast_pos( spread, sc::W_RANGE ) ) seed = spread;
    }

    if ( !cast_spell_pos( W, seed ) ) return false;
    last_w_cast_time_ = game_time_;
    return true;
}

bool zyra::try_q_execute()
{
    if ( !auto_q_ks_ || g_sdk == nullptr || g_sdk->object_manager == nullptr ) return false;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( hero ) ) continue;
        if ( distance_to( hero ) > sc::Q_RANGE + sc::Q_CAST_RANGE_BUFFER ) continue;
        if ( health_with_shields( hero ) <= calc_q( hero ) )
            return try_q( hero, pred_sdk::hitchance::medium, true );
    }
    return false;
}

bool zyra::try_e_execute()
{
    if ( !auto_e_ks_ || g_sdk == nullptr || g_sdk->object_manager == nullptr ) return false;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( hero ) ) continue;
        if ( distance_to( hero ) > sc::E_RANGE + hero->get_bounding_radius() ) continue;
        if ( health_with_shields( hero ) <= calc_e( hero ) )
            return try_e( hero, pred_sdk::hitchance::medium, true );
    }
    return false;
}

bool zyra::try_jungle_spell_cycle()
{
    if ( effective_role() != role_jungle ) return false;

    auto* monster = get_monster_target();
    if ( !is_valid_farm_target( monster ) ) return false;

    const math::vector3 mpos = monster->get_position();
    if ( can_use_q( min_mana_pct_ ) && distance_to( monster ) <= sc::Q_RANGE + sc::Q_CAST_RANGE_BUFFER )
    {
        if ( count_minions_in_radius( mpos, sc::Q_RADIUS, true ) >= jungle_q_min_hit_ )
        {
            const auto seed = plan_seed_for_q( monster, mpos, true );
            if ( is_sane_cast_pos( mpos, sc::Q_RANGE ) && cast_spell_pos( Q, mpos ) )
            {
                if ( seed.valid ) schedule_seed( seed.pos, true );
                return true;
            }
        }
    }

    if ( can_use_e( min_mana_pct_ ) && distance_to( monster ) <= sc::E_RANGE + monster->get_bounding_radius() )
    {
        const math::vector3 line_end = pos_.extended( mpos, sc::E_RANGE );
        if ( count_minions_on_line( line_end, sc::E_RADIUS, true ) >= jungle_e_min_hit_ )
        {
            const auto seed = plan_seed_for_e( monster, mpos, true );
            if ( is_sane_cast_pos( mpos, sc::E_RANGE ) && cast_spell_pos( E, mpos ) )
            {
                if ( seed.valid ) schedule_seed( seed.pos, true );
                return true;
            }
        }
    }

    return false;
}

int zyra::count_enemies_in_radius( const math::vector3& pos, float radius ) const
{
    if ( g_sdk == nullptr || g_sdk->object_manager == nullptr ) return 0;

    int count = 0;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( hero ) ) continue;
        if ( pos.distance( hero->get_position() ) <= radius + hero->get_bounding_radius() )
            ++count;
    }
    return count;
}

int zyra::count_minions_in_radius( const math::vector3& pos, float radius, bool monsters ) const
{
    if ( g_sdk == nullptr || g_sdk->object_manager == nullptr ) return 0;

    int count = 0;
    for ( auto* minion : g_sdk->object_manager->get_minions() )
    {
        if ( !is_valid_farm_target( minion ) ) continue;
        if ( monsters != minion->is_monster() ) continue;
        if ( pos.distance( minion->get_position() ) <= radius + minion->get_bounding_radius() )
            ++count;
    }
    return count;
}

int zyra::count_minions_on_line( const math::vector3& end, float width, bool monsters ) const
{
    if ( g_sdk == nullptr || g_sdk->object_manager == nullptr ) return 0;

    int count = 0;
    for ( auto* minion : g_sdk->object_manager->get_minions() )
    {
        if ( !is_valid_farm_target( minion ) ) continue;
        if ( monsters != minion->is_monster() ) continue;
        if ( segment_distance( minion->get_position(), pos_, end ) <= width + minion->get_bounding_radius() )
            ++count;
    }
    return count;
}

float zyra::health_with_shields( game_object* target ) const
{
    if ( target == nullptr ) return 0.f;
    return target->get_hp() + target->get_all_shield();
}

float zyra::calc_q( game_object* target ) const
{
    return spell_damage( target, sc::q_raw_damage( spells_[ Q ].rank, player_ ), sora::damage::damage_type::magic );
}

float zyra::calc_e( game_object* target ) const
{
    return spell_damage( target, sc::e_raw_damage( spells_[ E ].rank, player_ ), sora::damage::damage_type::magic );
}

float zyra::calc_r( game_object* target ) const
{
    return spell_damage( target, sc::r_raw_damage( spells_[ R ].rank, player_ ), sora::damage::damage_type::magic,
                         sora::damage::spell_kind::ultimate_aoe );
}

float zyra::calc_plant( game_object* target, float expected_hits ) const
{
    const float raw = sc::plant_raw_damage( level_, player_ ) * expected_hits;
    return spell_damage( target, raw, sora::damage::damage_type::magic );
}

void zyra::on_init()
{
    setup_spell( Q, pred_sdk::spell_type::circular, pred_sdk::targetting_type::center );
    spells_[ Q ].pred.range = sc::Q_RANGE + sc::Q_CAST_RANGE_BUFFER;
    spells_[ Q ].pred.radius = sc::Q_RADIUS;
    spells_[ Q ].pred.delay = sc::Q_DELAY;
    spells_[ Q ].pred.projectile_speed = instant_projectile_speed;

    setup_spell( W, pred_sdk::spell_type::circular, pred_sdk::targetting_type::center );
    spells_[ W ].pred.range = sc::W_RANGE;
    spells_[ W ].pred.radius = sc::Q_SEED_TRIGGER_RADIUS;
    spells_[ W ].pred.delay = 0.f;
    spells_[ W ].pred.projectile_speed = instant_projectile_speed;

    setup_spell( E, pred_sdk::spell_type::linear, pred_sdk::targetting_type::center );
    spells_[ E ].pred.range = sc::E_RANGE;
    spells_[ E ].pred.radius = sc::E_RADIUS;
    spells_[ E ].pred.delay = sc::E_DELAY;
    spells_[ E ].pred.projectile_speed = sc::E_SPEED;
    spells_[ E ].pred.forbidden_collisions = {
        pred_sdk::collision_type::yasuo_wall,
        pred_sdk::collision_type::braum_wall,
    };

    setup_spell( R, pred_sdk::spell_type::circular, pred_sdk::targetting_type::center );
    spells_[ R ].pred.range = sc::R_RANGE;
    spells_[ R ].pred.radius = sc::R_RADIUS;
    spells_[ R ].pred.delay = sc::R_DELAY;
    spells_[ R ].pred.projectile_speed = instant_projectile_speed;

    set_hitchance( Q, pred_sdk::hitchance::high, pred_sdk::hitchance::very_high, pred_sdk::hitchance::medium );
    set_hitchance( E, pred_sdk::hitchance::high, pred_sdk::hitchance::very_high, pred_sdk::hitchance::medium );

    setup_menu();
}

void zyra::on_game_update()
{
    update_player_state();
    if ( player_ == nullptr || g_sdk == nullptr || g_sdk->object_manager == nullptr ) return;

    process_pending_seeds();

    if ( semi_r_ && try_r_semi() ) return;
    if ( semi_e_ && try_e_semi() ) return;

    if ( try_q_execute() ) return;
    if ( try_e_execute() ) return;

    if ( anti_engage_e_ || anti_engage_r_ )
    {
        for ( auto* hero : g_sdk->object_manager->get_heroes() )
        {
            if ( !is_valid_enemy( hero ) || !hero->is_dashing() ) continue;
            if ( distance_to( hero ) > 700.f + hero->get_bounding_radius() ) continue;

            if ( anti_engage_e_ && try_e( hero, pred_sdk::hitchance::low, true ) ) return;
            if ( anti_engage_r_ && try_r_smart( hero, true ) ) return;
        }
    }

    if ( prepare_zone_ && try_prepare_zone() ) return;
}

void zyra::combo()
{
    auto* target = get_hero_target();
    if ( !is_valid_enemy( target ) ) return;

    if ( playstyle_ == play_catch_burst || playstyle_ == play_anti_engage )
    {
        if ( try_e( target, e_hitchance_, true ) ) return;
        if ( try_q( target, combo_hitchance_, true ) ) return;
    }
    else
    {
        if ( try_q( target, combo_hitchance_, true ) ) return;
        if ( try_e( target, e_hitchance_, true ) ) return;
    }

    if ( try_r_smart( target, true ) ) return;
    try_r_on_best_cluster( true );
}

void zyra::harass()
{
    if ( !should_use_spell_for_harass() ) return;

    auto* target = get_hero_target();
    if ( !is_valid_enemy( target ) ) return;

    if ( harass_use_e_ && effective_role() == role_apc_bot && playstyle_ == play_catch_burst )
    {
        if ( try_e( target, harass_hitchance_, false ) ) return;
    }

    if ( !harass_q_shoves_wave( target ) && try_q( target, harass_hitchance_, false ) ) return;

    if ( harass_use_e_ && effective_role() == role_support && playstyle_ != play_poke_control )
        try_e( target, harass_hitchance_, false );
}

void zyra::clear()
{
    if ( sdk::orbwalker == nullptr || !sdk::orbwalker->is_spell_farm_enabled() ) return;
    if ( effective_role() == role_support && support_respect_wave_ ) return;

    const bool jungle = effective_role() == role_jungle;
    if ( jungle )
    {
        try_jungle_spell_cycle();
        return;
    }

    if ( can_use_q( min_mana_pct_ ) )
    {
        spells_[ Q ].pred.expected_hitchance = pred_sdk::hitchance::low;
        const auto result = sdk::orbwalker->spell_farm(
            spells_[ Q ].pred, lane_q_min_hit_, spell_farm_flag::when_attack_not_ready,
            dmg_sdk::damage_type::magical );
        if ( result.is_valid && is_sane_cast_pos( result.cast_position, sc::Q_RANGE ) )
        {
            const auto seed = plan_seed_for_q( nullptr, result.cast_position, false );
            if ( cast_spell_pos( Q, result.cast_position ) )
            {
                if ( seed.valid ) schedule_seed( seed.pos, false );
                return;
            }
        }
    }

    if ( can_use_e( min_mana_pct_ ) && g_sdk != nullptr && g_sdk->object_manager != nullptr )
    {
        for ( auto* minion : g_sdk->object_manager->get_minions() )
        {
            if ( !is_valid_farm_target( minion ) || !minion->is_lane_minion() ) continue;
            const math::vector3 line_end = pos_.extended( minion->get_position(), sc::E_RANGE );
            if ( count_minions_on_line( line_end, sc::E_RADIUS, false ) < lane_q_min_hit_ ) continue;
            if ( !is_sane_cast_pos( line_end, sc::E_RANGE ) ) continue;

            const auto seed = plan_seed_for_e( minion, line_end, false );
            if ( cast_spell_pos( E, line_end ) )
            {
                if ( seed.valid ) schedule_seed( seed.pos, false );
                return;
            }
        }
    }
}

void zyra::fast_clear()
{
    if ( try_jungle_spell_cycle() ) return;
    clear();
}

void zyra::flee()
{
    auto* target = get_hero_target();
    if ( is_valid_enemy( target ) )
    {
        if ( try_e( target, pred_sdk::hitchance::low, true ) ) return;
        try_q( target, pred_sdk::hitchance::low, true );
    }
}

float zyra::get_combo_damage( game_object* target ) const
{
    if ( !is_valid_enemy( target ) ) return 0.f;

    float total = 0.f;
    if ( spells_[ Q ].is_ready() ) total += calc_q( target ) + calc_plant( target, sc::PLANT_EXPECTED_HITS_COMBO );
    if ( spells_[ E ].is_ready() ) total += calc_e( target ) + calc_plant( target, sc::PLANT_EXPECTED_HITS_COMBO );
    if ( spells_[ R ].is_ready() ) total += calc_r( target ) + calc_plant( target, sc::PLANT_EXPECTED_HITS_R );
    return total;
}

void zyra::on_draw()
{
    if ( player_ == nullptr || player_->is_dead() || g_sdk == nullptr || g_sdk->renderer == nullptr ) return;
    if ( g_sdk->object_manager == nullptr ) return;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( hero ) ) continue;
        const float damage = get_combo_damage( hero );
        if ( damage > 0.f )
            g_sdk->renderer->add_damage_indicator( hero, damage );
    }
}

void zyra::on_draw_world()
{
    if ( player_ == nullptr || player_->is_dead() || g_sdk == nullptr || g_sdk->renderer == nullptr ) return;

    if ( draw_q_ ) g_sdk->renderer->add_circle_3d( pos_, sc::Q_RANGE, 0.5f, draw_q_color_ );
    if ( draw_e_ ) g_sdk->renderer->add_circle_3d( pos_, sc::E_RANGE, 0.5f, draw_e_color_ );
    if ( draw_r_ ) g_sdk->renderer->add_circle_3d( pos_, sc::R_RANGE, 0.5f, draw_r_color_ );
    if ( draw_w_seed_ ) g_sdk->renderer->add_circle_3d( pos_, sc::W_RANGE, 0.5f, draw_w_color_ );

    if ( prepare_zone_ && g_sdk->hud_manager != nullptr )
    {
        const math::vector3 cursor = clamp_seed_pos( g_sdk->hud_manager->get_cursor_position() );
        g_sdk->renderer->add_circle_3d( cursor, sc::Q_SEED_TRIGGER_RADIUS, 2.f, draw_w_color_ );
    }
}

void zyra::setup_menu()
{
    menu_ = g_sdk->menu_manager->add_category( "sora_zyra", "Sora Zyra" );
    if ( menu_ == nullptr ) return;

    auto* plan = menu_->add_sub_category( "plan", "Plan" );
    plan->add_combo( "role", "Role Mode", { "Auto", "Support", "APC Bot", "Jungle" },
        role_mode_, [ this ]( int v ) { role_mode_ = std::clamp( v, 0, 3 ); } );
    plan->add_combo( "playstyle", "Playstyle", { "Poke Control", "Catch/Burst", "Anti Engage" },
        playstyle_, [ this ]( int v ) { playstyle_ = std::clamp( v, 0, 2 ); } );
    plan->add_combo( "plant_policy", "Plant Policy", { "Smart", "Aggressive", "Conserve One Seed", "Burst" },
        plant_policy_, [ this ]( int v ) { plant_policy_ = std::clamp( v, 0, 3 ); } );
    plan->add_combo( "r_policy", "R Policy", { "Disabled", "Combo Only", "Smart Teamfight", "Anti Engage" },
        r_policy_, [ this ]( int v ) { r_policy_ = std::clamp( v, 0, 3 ); } );
    plan->add_slider_float( "min_mana", "Min Mana %", 0.f, 1.f, 0.05f, min_mana_pct_,
        [ this ]( float v ) { min_mana_pct_ = v; } );

    auto* seeds = menu_->add_sub_category( "seeds", "Seeds (W Planner)" );
    seeds->add_checkbox( "plant_q", "Plant on Q", plant_on_q_, [ this ]( bool v ) { plant_on_q_ = v; } );
    seeds->add_checkbox( "plant_e", "Plant on E", plant_on_e_, [ this ]( bool v ) { plant_on_e_ = v; } );
    seeds->add_checkbox( "plant_r", "Plant on R", plant_on_r_, [ this ]( bool v ) { plant_on_r_ = v; } );
    seeds->add_checkbox( "conserve_last", "Smart: Conserve Last Seed", smart_conserve_last_,
        [ this ]( bool v ) { smart_conserve_last_ = v; } );
    seeds->add_checkbox( "double_kill", "Smart: Double Seed Only on Kill", smart_double_on_kill_,
        [ this ]( bool v ) { smart_double_on_kill_ = v; } );
    seeds->add_checkbox( "lead", "Smart: Lead Seed by Movement", smart_lead_,
        [ this ]( bool v ) { smart_lead_ = v; } );
    seeds->add_hotkey( "prepare_zone", "Prepare Zone (at cursor)", 'K', false, false,
        [ this ]( std::string*, bool v ) { prepare_zone_ = v; } );

    auto* spells = menu_->add_sub_category( "spells", "Spells" );
    spells->add_checkbox( "q", "Q Enabled", q_enabled_, [ this ]( bool v ) { q_enabled_ = v; } );
    spells->add_checkbox( "w", "W Planner Enabled", w_enabled_, [ this ]( bool v ) { w_enabled_ = v; } );
    spells->add_checkbox( "e", "E Enabled", e_enabled_, [ this ]( bool v ) { e_enabled_ = v; } );
    spells->add_checkbox( "r", "R Enabled", r_enabled_, [ this ]( bool v ) { r_enabled_ = v; } );
    spells->add_combo( "combo_hc", "Q Combo Hitchance", { "Low", "Medium", "High", "Very High" }, 2,
        [ this ]( int v )
        {
            static constexpr int values[] = {
                pred_sdk::hitchance::low,
                pred_sdk::hitchance::medium,
                pred_sdk::hitchance::high,
                pred_sdk::hitchance::very_high,
            };
            combo_hitchance_ = values[ std::clamp( v, 0, 3 ) ];
        } );
    spells->add_combo( "e_hc", "E Hitchance", { "Low", "Medium", "High", "Very High" }, 2,
        [ this ]( int v )
        {
            static constexpr int values[] = {
                pred_sdk::hitchance::low,
                pred_sdk::hitchance::medium,
                pred_sdk::hitchance::high,
                pred_sdk::hitchance::very_high,
            };
            e_hitchance_ = values[ std::clamp( v, 0, 3 ) ];
        } );
    spells->add_hotkey( "semi_e", "Semi-Manual E", 'Y', false, false,
        [ this ]( std::string*, bool v ) { semi_e_ = v; } );

    auto* auto_menu = menu_->add_sub_category( "auto", "Auto" );
    auto_menu->add_checkbox( "q_ks", "Q Killsteal", auto_q_ks_, [ this ]( bool v ) { auto_q_ks_ = v; } );
    auto_menu->add_checkbox( "e_ks", "E Killsteal", auto_e_ks_, [ this ]( bool v ) { auto_e_ks_ = v; } );
    auto_menu->add_checkbox( "anti_e", "Anti Engage E", anti_engage_e_, [ this ]( bool v ) { anti_engage_e_ = v; } );
    auto_menu->add_checkbox( "anti_r", "Anti Engage R", anti_engage_r_, [ this ]( bool v ) { anti_engage_r_ = v; } );

    auto* r_menu = menu_->add_sub_category( "r_settings", "R Settings" );
    r_menu->add_slider_int( "min_targets", "R Min Targets", 1, 5, 1, min_r_targets_,
        [ this ]( int v ) { min_r_targets_ = v; } );
    r_menu->add_slider_int( "cluster_score", "R Cluster Score", 35, 140, 5, min_r_cluster_score_,
        [ this ]( int v ) { min_r_cluster_score_ = v; } );
    r_menu->add_hotkey( "semi_r", "Semi-Manual R", 'T', false, false,
        [ this ]( std::string*, bool v ) { semi_r_ = v; } );

    auto* harass_menu = menu_->add_sub_category( "harass", "Harass" );
    harass_menu->add_checkbox( "use_e", "Use E in Harass", harass_use_e_,
        [ this ]( bool v ) { harass_use_e_ = v; } );
    harass_menu->add_checkbox( "respect_wave", "Respect Wave (Support)", harass_respect_wave_,
        [ this ]( bool v ) { harass_respect_wave_ = v; } );
    harass_menu->add_combo( "harass_hc", "Harass Hitchance", { "Low", "Medium", "High", "Very High" }, 2,
        [ this ]( int v )
        {
            static constexpr int values[] = {
                pred_sdk::hitchance::low,
                pred_sdk::hitchance::medium,
                pred_sdk::hitchance::high,
                pred_sdk::hitchance::very_high,
            };
            harass_hitchance_ = values[ std::clamp( v, 0, 3 ) ];
        } );

    auto* clear_menu = menu_->add_sub_category( "clear", "Clear" );
    clear_menu->add_checkbox( "support_wave", "Support Respect Wave", support_respect_wave_,
        [ this ]( bool v ) { support_respect_wave_ = v; } );
    clear_menu->add_slider_int( "lane_q_min", "Lane Q Min Hit", 2, 6, 1, lane_q_min_hit_,
        [ this ]( int v ) { lane_q_min_hit_ = v; } );
    clear_menu->add_slider_int( "jungle_q_min", "Jungle Q Min Hit", 1, 5, 1, jungle_q_min_hit_,
        [ this ]( int v ) { jungle_q_min_hit_ = v; } );
    clear_menu->add_slider_int( "jungle_e_min", "Jungle E Min Hit", 1, 5, 1, jungle_e_min_hit_,
        [ this ]( int v ) { jungle_e_min_hit_ = v; } );

    auto* draw = menu_->add_sub_category( "draw", "Drawings" );
    draw->add_checkbox( "q", "Draw Q", draw_q_, [ this ]( bool v ) { draw_q_ = v; } );
    draw->add_colorpicker( "qc", "Q Color", draw_q_color_, [ this ]( uint32_t v ) { draw_q_color_ = v; } );
    draw->add_checkbox( "e", "Draw E", draw_e_, [ this ]( bool v ) { draw_e_ = v; } );
    draw->add_colorpicker( "ec", "E Color", draw_e_color_, [ this ]( uint32_t v ) { draw_e_color_ = v; } );
    draw->add_checkbox( "r", "Draw R", draw_r_, [ this ]( bool v ) { draw_r_ = v; } );
    draw->add_colorpicker( "rc", "R Color", draw_r_color_, [ this ]( uint32_t v ) { draw_r_color_ = v; } );
    draw->add_checkbox( "w", "Draw W Seed Range", draw_w_seed_, [ this ]( bool v ) { draw_w_seed_ = v; } );
    draw->add_colorpicker( "wc", "W Color", draw_w_color_, [ this ]( uint32_t v ) { draw_w_color_ = v; } );
}

std::unique_ptr< champion_base > create_zyra( game_object* player )
{
    return std::make_unique< zyra >( player );
}

} // namespace sora::champions
