#include "twitch.h"

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"
#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "dmg_sdk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace twitch
{

// ============================================================================
// MODULE STATE
// ============================================================================
static menu_category* s_menu = nullptr;

static bool  is_stealthed       = false;
static bool  r_active           = false;
static bool  q_reset_detected   = false;
static float q_reset_time       = 0.f;
static bool  w_cycle_held_local = false;

// ============================================================================
// HELPERS
// ============================================================================
bool is_spell_learned( int slot )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    auto* sp = p->get_spell( slot );
    return sp && sp->get_level() > 0;
}

bool is_spell_ready( int slot )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    auto* sp = p->get_spell( slot );
    return sp && sp->get_level() > 0 && sp->get_cooldown() <= 0.f;
}

bool has_enough_mana( int slot )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    return p->get_par() >= p->get_spell_mana_cost( slot );
}

float mana_pct()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return 0.f;
    float mx = p->get_max_par();
    return mx > 0.f ? ( p->get_par() / mx ) : 0.f;
}

bool is_valid_target( game_object* unit, float range )
{
    if ( !unit || !unit->is_valid() || unit->is_dead() || !unit->is_targetable() ) return false;
    if ( !unit->is_visible() ) return false;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    if ( unit->get_team_id() == p->get_team_id() ) return false;
    if ( range != FLT_MAX && p->get_position().distance( unit->get_position() ) > range ) return false;
    return true;
}

static bool is_valid_ally( game_object* ally )
{
    if ( !ally || !ally->is_valid() || ally->is_dead() ) return false;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    if ( ally->get_team_id() != p->get_team_id() ) return false;
    if ( ally->get_network_id() == p->get_network_id() ) return false;
    return true;
}

static float distance_to( game_object* o )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !o ) return FLT_MAX;
    return p->get_position().distance( o->get_position() );
}

static game_object* pick_hero_target( float range )
{
    if ( !sdk::target_selector ) return nullptr;
    if ( auto* forced = sdk::target_selector->get_forced_target() )
    {
        if ( is_valid_target( forced, range ) ) return forced;
    }
    for ( auto* h : sdk::target_selector->get_sorted_heroes() )
    {
        if ( is_valid_target( h, range ) ) return h;
    }
    return nullptr;
}

static bool would_starve_e( int slot )
{
    if ( !menu::combo_save_mana_e || !is_spell_learned( SLOT_E ) ) return false;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    return ( p->get_par() - p->get_spell_mana_cost( slot ) ) < p->get_spell_mana_cost( SLOT_E );
}

static int get_venom_stacks( game_object* target )
{
    if ( !target ) return 0;
    auto* buff = target->get_buff_by_hash( spell_data::buff_venom_stacks );
    if ( !buff ) return 0;
    return ( std::max )( static_cast< int >( buff->get_stacks() ), buff->get_count() );
}

static bool is_leaving_range( game_object* target, float range )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !target ) return false;

    float dist = distance_to( target );
    if ( dist > range + 50.f ) return false;
    if ( dist >= range - 30.f ) return true;
    if ( !target->is_moving() ) return false;

    auto delta = target->get_position() - p->get_position();
    auto dir   = target->get_direction();
    float d2 = delta.x * delta.x + delta.z * delta.z;
    float m2 = dir.x * dir.x + dir.z * dir.z;
    if ( d2 < 1e-6f || m2 < 1e-6f ) return false;

    float cos_angle = ( delta.x * dir.x + delta.z * dir.z ) / std::sqrt( d2 * m2 );
    if ( cos_angle <= 0.3f ) return false;

    float margin = ( range - dist ) / ( cos_angle + 0.01f );
    return margin < 150.f;
}

static int enemies_in_range( float range )
{
    int n = 0;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( is_valid_target( hero, range ) ) n++;
    }
    return n;
}

static bool is_in_lethal_danger()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    if ( p->get_hp() / p->get_max_hp() > 0.30f ) return false;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_target( hero, 800.f ) ) continue;
        float aa_dmg = sdk::damage
            ? sdk::damage->calc_damage( dmg_sdk::damage_type::physical, hero, p, hero->get_attack_damage() )
            : hero->get_attack_damage();
        if ( ( p->get_hp() - aa_dmg * 2.f ) <= p->get_max_hp() * 0.05f )
            return true;
    }
    return false;
}

static bool can_ally_finish( game_object* target, float hp_after_e )
{
    if ( hp_after_e <= 0.f ) return true;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;

    auto tp = target->get_position();
    for ( auto* ally : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_ally( ally ) ) continue;
        float dx = ally->get_position().x - tp.x;
        float dz = ally->get_position().z - tp.z;
        if ( ( dx * dx + dz * dz ) > 640000.f ) continue;

        float aa = sdk::damage
            ? sdk::damage->calc_damage( dmg_sdk::damage_type::physical, ally, target, ally->get_attack_damage() )
            : ally->get_attack_damage();
        if ( aa * 3.f >= hp_after_e ) return true;
    }
    return false;
}

static bool is_teamfight_nearby()
{
    return enemies_in_range( 1500.f ) >= 3;
}

static bool has_windwall_threat()
{
    if ( !sdk::awareness ) return false;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_target( hero, 1500.f ) ) continue;
        auto name = hero->get_char_name();
        bool yasuo = ( name == "Yasuo" );
        bool braum  = ( name == "Braum" );
        if ( !yasuo && !braum ) continue;

        int check_slot = yasuo ? SLOT_W : SLOT_E;
        auto* sp = hero->get_spell( check_slot );
        if ( sp && sp->get_cooldown() <= 0.f ) return true;
    }
    return false;
}

// ============================================================================
// DAMAGE CALCULATIONS
// ============================================================================
static float get_e_damage( game_object* target, int stacks )
{
    if ( stacks <= 0 || !target || !sdk::damage ) return 0.f;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return 0.f;

    auto* sp = p->get_spell( SLOT_E );
    int r = sp ? std::max( 0, sp->get_level() - 1 ) : 0;

    float bad = ( std::max )( 0.f, p->get_attack_damage() - p->get_base_attack_damage() );
    float ap  = p->get_ability_power();
    float stk = static_cast< float >( stacks );

    // Base E hit (fixed physical)
    static const float e_base_dmg[5] = { 20.f, 30.f, 40.f, 50.f, 60.f };
    float base_hit = e_base_dmg[ r ];

    // Per-stack component
    float ps_base = E_STACK_DMG[ r ];
    float ps_ad   = 0.35f;
    float ps_ap   = 0.35f;

    float phys  = base_hit + ( ps_base + ps_ad * bad ) * stk;
    float magic = ps_ap * ap * stk;

    float phys_mitigated  = sdk::damage->calc_damage( dmg_sdk::damage_type::physical, p, target, phys );
    float magic_mitigated = sdk::damage->calc_damage( dmg_sdk::damage_type::magical,  p, target, magic );
    return phys_mitigated + magic_mitigated;
}

static float get_venom_dps( int stacks )
{
    if ( stacks <= 0 ) return 0.f;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return 0.f;

    int level = static_cast< int >( p->get_level() );
    int band  = level >= 17 ? 4 : level >= 13 ? 3 : level >= 9 ? 2 : level >= 5 ? 1 : 0;
    float per  = PASSIVE_DMG[ band ];
    float ap_r = PASSIVE_AP_RATIO;

    return static_cast< float >( stacks ) * ( per + ap_r * p->get_ability_power() );
}

static bool is_e_lethal( game_object* target )
{
    int stacks = get_venom_stacks( target );
    if ( stacks <= 0 || !is_spell_learned( SLOT_E ) || !target->is_targetable() ) return false;
    if ( venplus::utility::has_anti_burst_buff( target ) ) return false;

    float dmg = get_e_damage( target, stacks );
    if ( target->is_monster() ) dmg *= 0.999f;

    return dmg >= ( target->get_hp() + target->get_all_shield() + 0.8f );
}

static int autos_to_kill( game_object* target )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !target || !sdk::damage ) return 999;
    float aa = sdk::damage->calc_damage( dmg_sdk::damage_type::physical, p, target, p->get_attack_damage() );
    if ( aa <= 0.f ) return 999;
    float ehp = target->get_hp() + target->get_all_shield();
    return static_cast< int >( std::ceil( ehp / aa ) );
}

// ============================================================================
// SPELL CASTS
// ============================================================================
static bool try_w( game_object* target )
{
    if ( !target || !sdk::common ) return false;
    if ( !is_spell_ready( SLOT_W ) || !has_enough_mana( SLOT_W ) || would_starve_e( SLOT_W ) ) return false;
    if ( menu::combo_w_skip_if_killable > 0 && autos_to_kill( target ) <= menu::combo_w_skip_if_killable ) return false;
    if ( !sdk::prediction ) return false;

    pred_sdk::spell_data spell{};
    spell.spell_type         = pred_sdk::spell_type::circular;
    spell.targetting_type    = pred_sdk::targetting_type::center;
    spell.spell_slot         = SLOT_W;
    spell.range              = spell_data::w_range;
    spell.radius             = spell_data::w_radius;
    spell.delay              = spell_data::w_delay;
    spell.projectile_speed   = spell_data::w_speed;
    spell.expected_hitchance = pred_sdk::hitchance::high;
    spell.forbidden_collisions = {
        pred_sdk::collision_type::yasuo_wall,
        pred_sdk::collision_type::braum_wall,
    };

    pred_sdk::pred_data pred{};
    try { pred = sdk::prediction->predict( target, spell ); }
    catch ( ... ) { pred.is_valid = false; }

    if ( !pred.is_valid || pred.hitchance < pred_sdk::hitchance::high ) return false;
    return sdk::common->cast_spell( SLOT_W, pred.cast_position );
}

static bool try_e()
{
    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) ) return false;
    if ( !sdk::common ) return false;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_target( hero, spell_data::e_range ) ) continue;
        if ( is_e_lethal( hero ) )
            return sdk::common->cast_spell( SLOT_E );
    }
    return false;
}

static bool try_e_smart()
{
    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) ) return false;
    if ( !sdk::common ) return false;

    struct target_info
    {
        game_object* obj;
        int   stacks;
        float e_dmg;
        bool  lethal;
        bool  in_aa;
        bool  leaving;
        bool  expiring;
    };

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    const float game_time = g_sdk->clock_facade->get_game_time();

    target_info targets[5];
    int count = 0, killable = 0;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( count >= 5 ) break;
        if ( !is_valid_target( hero, spell_data::e_range + 30.f ) ) continue;

        int stacks = get_venom_stacks( hero );
        if ( stacks <= 0 ) continue;

        auto* venom = hero->get_buff_by_hash( spell_data::buff_venom_stacks );
        float e_dmg  = get_e_damage( hero, stacks );

        bool lethal = false;
        if ( e_dmg > 0.f && hero->is_targetable() && !venplus::utility::has_anti_burst_buff( hero ) )
        {
            float ehp = hero->get_hp() + hero->get_all_shield() + 0.8f;
            lethal = ( e_dmg >= ehp );
        }

        auto& t     = targets[ count++ ];
        t.obj       = hero;
        t.stacks    = stacks;
        t.e_dmg     = e_dmg;
        t.lethal    = lethal;
        t.in_aa     = sdk::orbwalker && sdk::orbwalker->is_in_auto_attack_range( p, hero );
        t.leaving   = is_leaving_range( hero, spell_data::e_range );
        t.expiring  = venom && ( venom->get_end_time() - game_time ) < 1.0f;
        if ( lethal ) killable++;
    }

    if ( count == 0 ) return false;

    // Under turret with kill available – just do it
    if ( p->get_buff_by_hash( 0u ) == nullptr ) {} // keep compiler happy
    {
        bool under_turret = false;
        for ( auto* turret : g_sdk->object_manager->get_turrets() )
        {
            if ( !turret || !turret->is_valid() || turret->is_dead() ) continue;
            if ( turret->get_team_id() == p->get_team_id() ) continue;
            if ( distance_to( turret ) < 775.f ) { under_turret = true; break; }
        }
        if ( under_turret && killable > 0 )
            return sdk::common->cast_spell( SLOT_E );
    }

    if ( is_in_lethal_danger() )
        return sdk::common->cast_spell( SLOT_E );

    if ( r_active )
    {
        if ( killable >= 2 )
            return sdk::common->cast_spell( SLOT_E );
        for ( int i = 0; i < count; ++i )
        {
            if ( targets[i].lethal && targets[i].leaving )
                return sdk::common->cast_spell( SLOT_E );
        }
        return false;
    }

    if ( count >= 2 && killable >= 2 )
        return sdk::common->cast_spell( SLOT_E );

    if ( count >= 2 && killable == 1 )
    {
        for ( int i = 0; i < count; ++i )
            if ( targets[i].lethal && targets[i].leaving )
                return sdk::common->cast_spell( SLOT_E );
        for ( int i = 0; i < count; ++i )
            if ( targets[i].lethal && targets[i].expiring )
                return sdk::common->cast_spell( SLOT_E );
        for ( int i = 0; i < count; ++i )
            if ( targets[i].lethal )
                return sdk::common->cast_spell( SLOT_E );
        return false;
    }

    if ( count >= 2 )
    {
        for ( int i = 0; i < count; ++i )
            if ( targets[i].stacks >= 6 && ( targets[i].expiring || targets[i].leaving ) )
                return sdk::common->cast_spell( SLOT_E );
        return false;
    }

    auto& t = targets[0];

    if ( t.stacks >= 6 && !t.lethal && ( t.expiring || t.leaving ) )
        return sdk::common->cast_spell( SLOT_E );

    if ( t.lethal && t.in_aa && ( p->get_hp() / p->get_max_hp() ) > 0.25f && !is_teamfight_nearby() )
    {
        if ( autos_to_kill( t.obj ) <= 1 && !t.leaving )
            return false;
    }

    if ( t.stacks < 4 && !t.lethal )
    {
        float ehp = t.obj->get_hp() + t.obj->get_all_shield();
        if ( ehp > 0.f && t.e_dmg < ehp * 0.25f ) return false;
    }

    if ( t.lethal )
        return sdk::common->cast_spell( SLOT_E );

    return false;
}

static bool try_r()
{
    if ( !is_spell_ready( SLOT_R ) || !has_enough_mana( SLOT_R ) || r_active ) return false;
    if ( !sdk::common ) return false;
    return enemies_in_range( 900.f ) >= 2 && sdk::common->cast_spell( SLOT_R );
}

static bool try_r_smart()
{
    if ( !is_spell_ready( SLOT_R ) || !has_enough_mana( SLOT_R ) || r_active ) return false;
    if ( has_windwall_threat() ) return false;
    if ( !sdk::common ) return false;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;

    const float aa_range = p->get_attack_range() + p->get_bounding_radius();

    if ( is_in_lethal_danger() )
    {
        auto* target = pick_hero_target( aa_range );
        if ( target && enemies_in_range( aa_range + 100.f ) >= 2 )
        {
            float aa = sdk::damage
                ? sdk::damage->calc_damage( dmg_sdk::damage_type::physical, p, target, p->get_attack_damage() )
                : p->get_attack_damage();
            if ( target->get_hp() + target->get_all_shield() < aa * 4.f )
                return sdk::common->cast_spell( SLOT_R );
        }
        return false;
    }

    if ( is_stealthed && enemies_in_range( aa_range ) >= 2 )
        return sdk::common->cast_spell( SLOT_R );

    if ( enemies_in_range( aa_range + spell_data::r_bonus_range ) >= 2 )
        return sdk::common->cast_spell( SLOT_R );

    auto* target = pick_hero_target( aa_range + spell_data::r_bonus_range );
    if ( target && enemies_in_range( 2500.f ) <= 1 )
    {
        bool out_of_aa = !sdk::orbwalker || !sdk::orbwalker->is_in_auto_attack_range( p, target );
        if ( out_of_aa && get_venom_stacks( target ) > 0
            && is_leaving_range( target, aa_range + spell_data::r_bonus_range ) )
            return sdk::common->cast_spell( SLOT_R );
    }

    return false;
}

// ============================================================================
// GAME MODES
// ============================================================================
void combo()
{
    if ( !sdk::orbwalker ) return;

    if ( menu::combo_use_r )
    {
        if ( menu::combo_r_mode == 1 ) try_r_smart();
        else                           try_r();
    }

    if ( menu::combo_use_w )
    {
        auto* p = g_sdk->object_manager->get_local_player();
        bool w_blocked = menu::combo_w_block_r && r_active;
        bool as_high   = p && ( 1.f / p->get_attack_delay() ) > menu::combo_w_as_cap;

        if ( menu::combo_w_mode == 1 && !w_blocked && !as_high )
        {
            auto* target = pick_hero_target( spell_data::w_range );
            if ( target && !sdk::orbwalker->is_in_auto_attack_range( p, target, 25.f ) )
                try_w( target );
        }
    }

    if ( menu::combo_use_e )
    {
        if ( menu::combo_e_mode == 1 ) try_e_smart();
        else                           try_e();
    }
}

void harass()
{
    if ( !sdk::orbwalker || mana_pct() < menu::harass_mana_pct || !menu::harass_use_w ) return;
    auto* target = pick_hero_target( spell_data::w_range );
    if ( target ) try_w( target );
}

void clear()
{
    if ( !sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled() ) return;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return;

    if ( menu::lane_use_w && mana_pct() >= menu::lane_w_mana_pct
        && is_spell_ready( SLOT_W ) && has_enough_mana( SLOT_W ) && !would_starve_e( SLOT_W )
        && !sdk::orbwalker->would_cancel_attack() )
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::circular;
        spell.targetting_type    = pred_sdk::targetting_type::center;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range;
        spell.radius             = spell_data::w_radius;
        spell.delay              = spell_data::w_delay;
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = pred_sdk::hitchance::low;

        pred_sdk::pred_data r{};
        try {
            r = sdk::orbwalker->spell_farm(
                spell, menu::lane_w_min,
                spell_farm_flag::when_attack_not_ready,
                dmg_sdk::damage_type::physical );
        } catch ( ... ) { r.is_valid = false; }

        if ( r.is_valid && sdk::common )
        {
            sdk::common->cast_spell( SLOT_W, r.cast_position );
            return;
        }
    }

    if ( !menu::lane_use_e || mana_pct() < menu::lane_e_mana_pct
        || !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) )
        return;

    int kills = 0;
    for ( auto* obj : g_sdk->object_manager->get_minions() )
    {
        if ( !obj || !obj->is_valid() || obj->is_dead() || !obj->is_lane_minion() ) continue;
        if ( obj->get_team_id() == p->get_team_id() ) continue;
        if ( distance_to( obj ) > spell_data::e_range ) continue;

        int stacks = get_venom_stacks( obj );
        if ( stacks <= 0 ) continue;

        float hp  = obj->get_hp() + 2.f;
        float dmg = get_e_damage( obj, stacks );
        if ( dmg >= hp ) kills++;
    }

    if ( kills >= menu::lane_e_min_kills && sdk::common )
        sdk::common->cast_spell( SLOT_E );
}

void fast_clear()
{
    if ( !sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled() ) return;

    auto* monster = sdk::target_selector
        ? sdk::target_selector->get_monster_target(
            []( game_object* m ) {
                return m && m->is_valid() && m->is_visible() && m->is_targetable()
                    && distance_to( m ) <= 600.f;
            } )
        : nullptr;

    if ( !monster ) return;

    if ( menu::jungle_use_q && !is_stealthed && is_spell_ready( SLOT_Q ) && has_enough_mana( SLOT_Q )
        && !sdk::orbwalker->would_cancel_attack() && sdk::common )
    {
        sdk::common->cast_spell( SLOT_Q );
        return;
    }

    if ( menu::jungle_use_w && is_spell_ready( SLOT_W ) && has_enough_mana( SLOT_W ) && !would_starve_e( SLOT_W )
        && !sdk::orbwalker->would_cancel_attack() && sdk::common )
    {
        sdk::common->cast_spell( SLOT_W, monster->get_position() );
        return;
    }

    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) || !sdk::common ) return;

    for ( auto* obj : g_sdk->object_manager->get_monsters() )
    {
        if ( !obj || !obj->is_valid() || obj->is_dead() || !obj->is_targetable() ) continue;
        if ( distance_to( obj ) > spell_data::e_range || get_venom_stacks( obj ) <= 0 || !is_e_lethal( obj ) )
            continue;

        auto name = obj->get_char_name();
        bool secure = ( menu::jungle_e_buff     && obj->is_buff_monster() )
            || ( menu::jungle_e_dragon   && obj->is_epic_monster() && name.find( "Dragon" ) != std::string_view::npos )
            || ( menu::jungle_e_baron    && obj->is_epic_monster() && name.find( "Baron"  ) != std::string_view::npos )
            || ( menu::jungle_e_herald   && obj->is_epic_monster() && name.find( "Herald" ) != std::string_view::npos )
            || ( menu::jungle_e_voidgrubs && name.find( "Voidgrub" ) != std::string_view::npos );

        if ( secure )
        {
            sdk::common->cast_spell( SLOT_E );
            return;
        }
    }
}

void lasthit()
{
    if ( !sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()
        || !sdk::orbwalker->is_spell_farm_lasthit_enabled() )
        return;
    if ( !menu::lasthit_e_cannon || !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) || !sdk::common )
        return;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return;

    for ( auto* obj : g_sdk->object_manager->get_minions() )
    {
        if ( !obj || !obj->is_valid() || obj->is_dead() || !obj->is_lane_minion_siege() ) continue;
        if ( obj->get_team_id() == p->get_team_id() ) continue;
        if ( distance_to( obj ) > spell_data::e_range || get_venom_stacks( obj ) <= 0 ) continue;

        float pred_hp = sdk::health_prediction
            ? sdk::health_prediction->get_predicted_health( obj, p->get_attack_cast_delay() )
            : obj->get_hp();

        if ( is_e_lethal( obj ) && pred_hp <= 0.f )
        {
            sdk::common->cast_spell( SLOT_E );
            return;
        }
    }
}

void flee()
{
    if ( !sdk::orbwalker || !menu::flee_use_w ) return;
    if ( !is_spell_ready( SLOT_W ) || !has_enough_mana( SLOT_W ) || !sdk::prediction || !sdk::common ) return;

    pred_sdk::spell_data spell{};
    spell.spell_type         = pred_sdk::spell_type::circular;
    spell.targetting_type    = pred_sdk::targetting_type::center;
    spell.spell_slot         = SLOT_W;
    spell.range              = spell_data::w_range;
    spell.radius             = spell_data::w_radius;
    spell.delay              = spell_data::w_delay;
    spell.projectile_speed   = spell_data::w_speed;
    spell.expected_hitchance = pred_sdk::hitchance::low;

    auto* target = pick_hero_target( spell_data::w_range );
    if ( !target ) return;

    pred_sdk::pred_data pred{};
    try { pred = sdk::prediction->predict( target, spell ); }
    catch ( ... ) { pred.is_valid = false; }

    if ( pred.is_valid )
        sdk::common->cast_spell( SLOT_W, pred.cast_position );
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================
bool __stdcall on_after_attack( orb_sdk::event_data* data )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !sdk::orbwalker ) return true;

    bool in_combo  = sdk::orbwalker->combo();
    bool in_harass = sdk::orbwalker->harass();

    if ( ( in_combo && menu::combo_use_w )
        || ( in_harass && menu::harass_use_w && mana_pct() >= menu::harass_mana_pct ) )
    {
        bool w_blocked = menu::combo_w_block_r && r_active;
        bool as_high   = ( 1.f / p->get_attack_delay() ) > menu::combo_w_as_cap;

        if ( !w_blocked && !as_high )
        {
            auto* target = pick_hero_target( spell_data::w_range );
            if ( target ) try_w( target );
        }
    }

    if ( !in_combo ) return true;
    if ( !menu::combo_use_q || !is_spell_ready( SLOT_Q ) || !has_enough_mana( SLOT_Q )
        || would_starve_e( SLOT_Q ) || is_stealthed || !sdk::common )
        return true;

    sdk::common->cast_spell( SLOT_Q );
    return true;
}

void __fastcall on_buff_loss( game_object* target, buff_instance* buff )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !target || !buff || !p ) return;
    if ( target->get_team_id() == p->get_team_id() || !target->is_hero() ) return;
    if ( buff->get_hash() != spell_data::buff_venom_stacks || !target->is_dead() ) return;

    q_reset_detected = true;
    q_reset_time     = g_sdk->clock_facade->get_game_time();
}

void __fastcall on_update()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || p->is_dead() ) return;

    is_stealthed = p->get_buff_by_hash( spell_data::buff_q_stealth ) != nullptr;
    r_active     = p->get_buff_by_hash( spell_data::buff_r_active )  != nullptr;

    const float now = g_sdk->clock_facade->get_game_time();

    // Auto Q after kill reset
    if ( q_reset_detected )
    {
        if ( now - q_reset_time > 2.f )
        {
            q_reset_detected = false;
        }
        else if ( menu::combo_use_q && menu::combo_q_auto_kill && !is_stealthed
            && is_spell_ready( SLOT_Q ) && has_enough_mana( SLOT_Q )
            && enemies_in_range( 1300.f ) > 0 && sdk::common )
        {
            sdk::common->cast_spell( SLOT_Q );
            q_reset_detected = false;
        }
    }

    if ( !sdk::orbwalker ) return;

    if      ( sdk::orbwalker->combo() )      combo();
    else if ( sdk::orbwalker->harass() )     harass();
    else if ( sdk::orbwalker->fast_clear() ) fast_clear();
    else if ( sdk::orbwalker->clear() )      clear();
    else if ( sdk::orbwalker->lasthit() )    lasthit();
    else if ( sdk::orbwalker->flee() )       flee();
}

void __fastcall on_draw_world()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !sdk::shaders ) return;

    const auto pos      = p->get_position();
    const float now     = g_sdk->clock_facade->get_game_time();

    auto* qb      = p->get_buff_by_hash( spell_data::buff_q_stealth );
    float q_left  = qb ? qb->get_end_time() - now : 0.f;
    float q_total = qb ? qb->get_end_time() - qb->get_start_time() : 1.f;
    float q_ratio = q_total > 0.f ? q_left / q_total : 0.f;

    if ( q_left > 0.f && menu::draw_q_detect )
    {
        float cycle = 0.4f + 3.1f * q_ratio;
        float t     = std::fmod( now, cycle ) / cycle;
        float pulse = 0.15f + 0.10f * ( 1.0f + std::sin( t * 6.2831853f - 1.5707963f ) );
        sdk::shaders->draw_filled_world_segment( pos, pos, spell_data::q_detect_range,
            color( menu::draw_q_detect_color ), 1, pulse );
    }

    if ( q_left > 0.f && menu::draw_q_timer_mode == 1 && g_sdk->renderer )
    {
        auto s = g_sdk->renderer->world_to_screen( pos );
        char buf[16];
        std::snprintf( buf, sizeof( buf ), "Q: %.1fs", q_left );
        g_sdk->renderer->add_text( buf, 16.f, { s.x, s.y - 60.f }, 1,
            q_left < 3.f ? 0xFFFF4444u : 0xFF00AAFFu );
    }

    if ( q_left > 0.f && menu::draw_q_timer_mode == 2 && g_sdk->renderer )
    {
        auto s = g_sdk->renderer->world_to_screen( pos );
        float bw = 80.f, bh = 6.f;
        ::math::rect bg{ s.x - bw * 0.5f, s.y - 60.f, s.x + bw * 0.5f, s.y - 60.f + bh };
        ::math::rect fg{ bg.left, bg.top, bg.left + q_ratio * bw, bg.bottom };
        g_sdk->renderer->add_rectangle_filled( bg, 0x80000000u );
        g_sdk->renderer->add_rectangle_filled( fg, q_left < 3.f ? 0xFFFF4444u : 0xFF00AAFFu );
    }

    if ( menu::draw_w && is_spell_learned( SLOT_W ) )
        sdk::shaders->draw_world_circle( pos, spell_data::w_range, 0.5f, color( menu::draw_w_color ) );

    if ( menu::draw_e && is_spell_learned( SLOT_E ) )
        sdk::shaders->draw_world_circle( pos, spell_data::e_range, 0.5f, color( menu::draw_e_color ) );

    if ( menu::draw_r && r_active )
        sdk::shaders->draw_world_circle( pos, p->get_attack_range() + p->get_bounding_radius(), 0.5f, color( menu::draw_r_color ) );
}

void __fastcall on_draw()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !g_sdk->renderer ) return;

    // Damage bar overlay + glow on lethal targets (heroes / minions / monsters)
    menu::draw_cfg.draw( p, []( game_object* t ) -> float
    {
        if ( !is_spell_learned( SLOT_E ) ) return 0.f;
        int stacks = get_venom_stacks( t );
        return stacks > 0 ? get_e_damage( t, stacks ) : 0.f;
    } );

    // Venom stack counter text above every poisoned target
    if ( menu::draw_stacks )
    {
        auto draw_stacks_on = [ & ]( game_object* t )
        {
            if ( !t || !t->is_valid() || t->is_dead() || !t->is_visible() ) return;
            if ( t->get_team_id() == p->get_team_id() ) return;
            int stacks = get_venom_stacks( t );
            if ( stacks <= 0 ) return;

            auto world_pos = t->get_position();
            world_pos.y += t->get_bounding_radius() + 30.f;
            auto screen = g_sdk->renderer->world_to_screen( world_pos );
            if ( screen.x < 1.f ) return;

            char buf[8];
            std::snprintf( buf, sizeof( buf ), "%d", stacks );
            g_sdk->renderer->add_text( buf, menu::draw_stacks_size,
                { screen.x, screen.y - 20.f }, 1, menu::draw_stacks_color );
        };

        for ( auto* h : g_sdk->object_manager->get_heroes() )   draw_stacks_on( h );
        for ( auto* m : g_sdk->object_manager->get_minions() )  draw_stacks_on( m );
        for ( auto* m : g_sdk->object_manager->get_monsters() ) draw_stacks_on( m );
    }
}

// ============================================================================
// MENU SETUP
// ============================================================================
static void setup_menu()
{
    s_menu = g_sdk->menu_manager->add_category( "meow_twitch", "MeowAIO - Twitch" );
    if ( !s_menu ) return;

    // ── Q – Ambush ────────────────────────────────────────────────────────────
    auto* q = s_menu->add_sub_category( "q_ambush", "Q - Ambush" );
    q->add_checkbox( "combo", "Use Q in Combo", menu::combo_use_q,
        []( bool v ) { menu::combo_use_q = v; } );
    q->add_checkbox( "q_auto_kill", "Auto Q on Kill Reset", menu::combo_q_auto_kill,
        []( bool v ) { menu::combo_q_auto_kill = v; } );
    q->add_separator();
    q->add_label( "Stealth Visuals" );
    q->add_checkbox( "q_detect", "Show Q Detection Radius", menu::draw_q_detect,
        []( bool v ) { menu::draw_q_detect = v; } );
    q->add_colorpicker( "q_detect_color", "Detection Color", menu::draw_q_detect_color,
        []( uint32_t v ) { menu::draw_q_detect_color = v; } );
    q->add_combo( "q_timer", "Q Stealth Timer",
        std::vector< std::string >{ "Off", "Text", "Bar" },
        menu::draw_q_timer_mode, []( int v ) { menu::draw_q_timer_mode = v; } );

    // ── W – Venom Cask ────────────────────────────────────────────────────────
    auto* w = s_menu->add_sub_category( "w_venom_cask", "W - Venom Cask" );
    w->add_checkbox( "combo", "Use W in Combo", menu::combo_use_w,
        []( bool v ) { menu::combo_use_w = v; } );
    w->add_combo( "w_mode", "W Mode",
        std::vector< std::string >{ "After AA", "Always" },
        menu::combo_w_mode, []( int v ) { menu::combo_w_mode = v; } );
    w->add_hotkey( "w_cycle", "Cycle W Mode", 'T', false, false,
        []( std::string*, bool v )
        {
            if ( v && !w_cycle_held_local )
                menu::combo_w_mode = ( menu::combo_w_mode + 1 ) % 2;
            w_cycle_held_local = v;
        } );
    w->add_checkbox( "w_block_r", "Block W While R Active", menu::combo_w_block_r,
        []( bool v ) { menu::combo_w_block_r = v; } );
    w->add_slider_int( "w_skip_kill", "Block W if Killable in X AAs", 0, 5, 1,
        menu::combo_w_skip_if_killable, []( int v ) { menu::combo_w_skip_if_killable = v; } );
    w->add_slider_float( "w_as_cap", "W Attack Speed Cap", 1.5f, 3.5f, 0.1f,
        menu::combo_w_as_cap, []( float v ) { menu::combo_w_as_cap = v; } );
    w->add_separator();
    w->add_checkbox( "harass", "Use W in Harass", menu::harass_use_w,
        []( bool v ) { menu::harass_use_w = v; } );
    w->add_slider_float( "harass_mana", "Harass Min Mana %", 0.f, 1.f, 0.05f,
        menu::harass_mana_pct, []( float v ) { menu::harass_mana_pct = v; } );
    w->add_separator();
    w->add_checkbox( "flee", "Use W in Flee", menu::flee_use_w,
        []( bool v ) { menu::flee_use_w = v; } );
    w->add_separator();
    w->add_label( "Drawings" );
    w->add_checkbox( "w_range", "Draw W Range", menu::draw_w,
        []( bool v ) { menu::draw_w = v; } );
    w->add_colorpicker( "w_color", "W Color", menu::draw_w_color,
        []( uint32_t v ) { menu::draw_w_color = v; } );

    // ── E – Contaminate ───────────────────────────────────────────────────────
    auto* e = s_menu->add_sub_category( "e_contaminate", "E - Contaminate" );
    e->add_checkbox( "combo", "Use E in Combo", menu::combo_use_e,
        []( bool v ) { menu::combo_use_e = v; } );
    e->add_combo( "e_mode", "E Mode",
        std::vector< std::string >{ "Always", "Smart" },
        menu::combo_e_mode, []( int v ) { menu::combo_e_mode = v; } );
    e->add_checkbox( "save_mana_e", "Save Mana for E", menu::combo_save_mana_e,
        []( bool v ) { menu::combo_save_mana_e = v; } );
    e->add_label( "Blocks other spells if mana would fall below E cost" );
    e->add_separator();
    e->add_label( "Drawings" );
    e->add_checkbox( "e_range", "Draw E Range", menu::draw_e,
        []( bool v ) { menu::draw_e = v; } );
    e->add_colorpicker( "e_color", "E Color", menu::draw_e_color,
        []( uint32_t v ) { menu::draw_e_color = v; } );

    // ── R – Spray and Pray ────────────────────────────────────────────────────
    auto* r = s_menu->add_sub_category( "r_spray_and_pray", "R - Spray and Pray" );
    r->add_checkbox( "combo", "Use R in Combo", menu::combo_use_r,
        []( bool v ) { menu::combo_use_r = v; } );
    r->add_combo( "r_mode", "R Mode",
        std::vector< std::string >{ "Always", "Smart" },
        menu::combo_r_mode, []( int v ) { menu::combo_r_mode = v; } );
    r->add_separator();
    r->add_label( "Drawings" );
    r->add_checkbox( "r_range", "Draw R Range (Active)", menu::draw_r,
        []( bool v ) { menu::draw_r = v; } );
    r->add_colorpicker( "r_color", "R Color", menu::draw_r_color,
        []( uint32_t v ) { menu::draw_r_color = v; } );

    // ── Farming ───────────────────────────────────────────────────────────────
    auto* farm = s_menu->add_sub_category( "farming", "Farming" );

    auto* lc = farm->add_sub_category( "lane_clear", "Lane Clear" );
    lc->add_label( "W Settings" );
    lc->add_checkbox( "w", "Use W in Lane Clear", menu::lane_use_w,
        []( bool v ) { menu::lane_use_w = v; } );
    lc->add_slider_int( "w_min", "W on X+ Minions", 2, 5, 1,
        menu::lane_w_min, []( int v ) { menu::lane_w_min = v; } );
    lc->add_slider_float( "w_mana", "W Min Mana %", 0.f, 1.f, 0.05f,
        menu::lane_w_mana_pct, []( float v ) { menu::lane_w_mana_pct = v; } );
    lc->add_separator();
    lc->add_label( "E Settings" );
    lc->add_checkbox( "e", "Use E in Lane Clear", menu::lane_use_e,
        []( bool v ) { menu::lane_use_e = v; } );
    lc->add_slider_int( "e_min", "E to Kill X Minions", 1, 7, 1,
        menu::lane_e_min_kills, []( int v ) { menu::lane_e_min_kills = v; } );
    lc->add_slider_float( "e_mana", "E Min Mana %", 0.f, 1.f, 0.05f,
        menu::lane_e_mana_pct, []( float v ) { menu::lane_e_mana_pct = v; } );

    auto* lh = farm->add_sub_category( "lasthit", "Last Hit" );
    lh->add_checkbox( "e_cannon", "Use E to Save Cannon", menu::lasthit_e_cannon,
        []( bool v ) { menu::lasthit_e_cannon = v; } );

    auto* jg = farm->add_sub_category( "jungle", "Jungle Clear" );
    jg->add_label( "Q Settings" );
    jg->add_checkbox( "q", "Use Q in Jungle", menu::jungle_use_q,
        []( bool v ) { menu::jungle_use_q = v; } );
    jg->add_separator();
    jg->add_label( "W Settings" );
    jg->add_checkbox( "w", "Use W in Jungle", menu::jungle_use_w,
        []( bool v ) { menu::jungle_use_w = v; } );
    jg->add_separator();
    jg->add_label( "Auto E to Secure:" );
    jg->add_checkbox( "e_buff",     "Red / Blue",  menu::jungle_e_buff,
        []( bool v ) { menu::jungle_e_buff = v; } );
    jg->add_checkbox( "e_dragon",   "Dragon",      menu::jungle_e_dragon,
        []( bool v ) { menu::jungle_e_dragon = v; } );
    jg->add_checkbox( "e_baron",    "Baron",       menu::jungle_e_baron,
        []( bool v ) { menu::jungle_e_baron = v; } );
    jg->add_checkbox( "e_herald",   "Herald",      menu::jungle_e_herald,
        []( bool v ) { menu::jungle_e_herald = v; } );
    jg->add_checkbox( "e_voidgrubs","Voidgrubs",   menu::jungle_e_voidgrubs,
        []( bool v ) { menu::jungle_e_voidgrubs = v; } );

    // ── Drawings ─────────────────────────────────────────────────────────────
    auto* d = s_menu->add_sub_category( "drawings", "Drawings" );

    // Damage Drawings (VEN+ style overlay + glow)
    auto* dd = d->add_sub_category( "dmg_draws", "Damage Drawings" );
    menu::draw_cfg.setup_menu( dd );
    dd->add_separator();
    dd->add_label( "Damage Color  = target has stacks but survives E" );
    dd->add_label( "Killable Color = target dies from E" );

    // Venom Stack Counter Text
    auto* dst = d->add_sub_category( "stack_text", "Venom Stack Counter" );
    dst->add_checkbox( "enabled", "Show Stack Count on Targets", menu::draw_stacks,
        []( bool v ) { menu::draw_stacks = v; } );
    dst->add_colorpicker( "color", "Stack Text Color", menu::draw_stacks_color,
        []( uint32_t v ) { menu::draw_stacks_color = v; } );
    dst->add_slider_float( "size", "Stack Text Size", 10.f, 22.f, 1.f,
        menu::draw_stacks_size, []( float v ) { menu::draw_stacks_size = v; } );

    s_menu->add_separator();
}

// ============================================================================
// LIFECYCLE
// ============================================================================
void load()
{
    if ( !g_sdk || !g_sdk->menu_manager ) return;

    setup_menu();

    g_sdk->event_manager->register_callback( event_manager::event::game_update,
        reinterpret_cast< void* >( on_update ) );
    g_sdk->event_manager->register_callback( event_manager::event::draw_world,
        reinterpret_cast< void* >( on_draw_world ) );
    g_sdk->event_manager->register_callback( event_manager::event::present,
        reinterpret_cast< void* >( on_draw ) );
    g_sdk->event_manager->register_callback( event_manager::event::buff_loss,
        reinterpret_cast< void* >( on_buff_loss ) );

    if ( sdk::orbwalker )
    {
        sdk::orbwalker->register_callback( orb_sdk::event_type::after_attack,
            reinterpret_cast< void* >( on_after_attack ) );
    }

    g_sdk->log_console( "[MeowAIO] Twitch loaded MEOW!" );
}

void unload()
{
    if ( g_sdk && g_sdk->event_manager )
    {
        g_sdk->event_manager->unregister_callback( event_manager::event::game_update,
            reinterpret_cast< void* >( on_update ) );
        g_sdk->event_manager->unregister_callback( event_manager::event::draw_world,
            reinterpret_cast< void* >( on_draw_world ) );
        g_sdk->event_manager->unregister_callback( event_manager::event::present,
            reinterpret_cast< void* >( on_draw ) );
        g_sdk->event_manager->unregister_callback( event_manager::event::buff_loss,
            reinterpret_cast< void* >( on_buff_loss ) );
    }

    if ( sdk::orbwalker )
    {
        sdk::orbwalker->unregister_callback( orb_sdk::event_type::after_attack,
            reinterpret_cast< void* >( on_after_attack ) );
    }

    s_menu         = nullptr;
    is_stealthed   = false;
    r_active       = false;
    q_reset_detected = false;
    q_reset_time   = 0.f;
    w_cycle_held_local = false;

    if ( g_sdk ) g_sdk->log_console( "[MeowAIO] Twitch unloaded" );
}

} // namespace twitch
