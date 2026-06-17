#include "kalista.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "dmg_sdk.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace kalista
{

// ============================================================================
// MODULE STATE
// ============================================================================
static menu_category* s_menu = nullptr;

static float          s_last_attack_time = -30.f;

// Direction of the most recent auto-attack hop (xz-plane), un-normalized.
static math::vector3  s_last_hop_dir{ 0.f, 0.f, 0.f };

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

static float distance_to( game_object* o )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !o ) return FLT_MAX;
    return p->get_position().distance( o->get_position() );
}

static bool is_valid_enemy( game_object* unit, float range = FLT_MAX )
{
    if ( !unit || !unit->is_valid() || unit->is_dead() || !unit->is_targetable() ) return false;
    if ( !unit->is_visible() ) return false;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;
    if ( unit->get_team_id() == p->get_team_id() ) return false;
    if ( range != FLT_MAX && p->get_position().distance( unit->get_position() ) > range ) return false;
    return true;
}

static game_object* pick_hero_target( float range )
{
    if ( !sdk::target_selector ) return nullptr;
    if ( auto* forced = sdk::target_selector->get_forced_target() )
    {
        if ( is_valid_enemy( forced, range ) ) return forced;
    }
    for ( auto* h : sdk::target_selector->get_sorted_heroes() )
    {
        if ( is_valid_enemy( h, range ) ) return h;
    }
    return nullptr;
}

static int e_rank_idx()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return 0;
    auto* sp = p->get_spell( SLOT_E );
    int lvl = sp ? sp->get_level() : 0;
    return std::clamp( lvl - 1, 0, 4 );
}

static int get_rend_stacks( game_object* target )
{
    if ( !target ) return 0;
    auto* buff = target->get_buff_by_hash( spell_data::buff_rend_stacks );
    if ( !buff ) return 0;
    return ( std::max )( static_cast< int >( buff->get_stacks() ), buff->get_count() );
}

static float get_e_damage( game_object* target, int additional_stacks = 0 )
{
    if ( !target || !sdk::damage ) return 0.f;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return 0.f;

    int r        = e_rank_idx();
    float bad    = ( std::max )( 0.f, p->get_attack_damage() - p->get_base_attack_damage() );
    float ap     = p->get_ability_power();

    auto* buff   = target->get_buff_by_hash( spell_data::buff_rend_stacks );
    if ( !buff ) return 0.f;

    int  cur     = ( std::max )( static_cast< int >( buff->get_stacks() ), buff->get_count() );
    int  stacks  = cur + additional_stacks - 1;   // bonus stacks past the first spear

    float dmg = E_BASE_FIRST[ r ]
              + E_FIRST_RATIO_AD * p->get_attack_damage()
              + E_FIRST_RATIO_AP * ap;

    if ( stacks > 0 )
    {
        dmg += static_cast< float >( stacks ) *
               ( E_BASE_ADDITIONAL[ r ] + E_ADD_RATIO_AD[ r ] * bad + E_ADD_RATIO_AP * ap );
    }

    float mit = sdk::damage->calc_damage( dmg_sdk::damage_type::physical, p, target, dmg );
    return target->is_epic_monster() ? mit * 0.5f : mit;
}

static bool can_attack_now()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !g_sdk->clock_facade ) return true;

    float now      = g_sdk->clock_facade->get_game_time();
    float delay    = p->get_attack_delay();
    float cast_del = p->get_attack_cast_delay();
    return ( now - s_last_attack_time ) >= ( delay + cast_del );
}

static bool should_jump_via_aa()
{
    // Approximation of the original "boots vs walk distance" logic.
    // We don't have item enumeration here, so we use a fixed jump range
    // that matches the original "basic boots" tier (the most common case).
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return true;

    const float move_speed  = p->get_move_speed();
    const float ping_s      = g_sdk->net_client ? ( g_sdk->net_client->get_ping() / 1000.f ) : 0.06f;
    const float aa_cast_del = p->get_attack_cast_delay();

    const float time_to_aa  = aa_cast_del + ping_s * 1.2f;
    const float walk_range  = move_speed * time_to_aa;
    const float jump_range  = 265.f;
    return jump_range > walk_range;
}

static int enemies_in_aa_range()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return 0;
    float r = p->get_attack_range() + p->get_bounding_radius();
    int n = 0;
    for ( auto* h : g_sdk->object_manager->get_heroes() )
    {
        if ( is_valid_enemy( h, r ) ) n++;
    }
    return n;
}

// ============================================================================
// SPELL CASTS
// ============================================================================
static bool killable_e()
{
    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) || !sdk::common ) return false;

    for ( auto* h : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( h, spell_data::e_range ) ) continue;
        int stacks = get_rend_stacks( h );
        if ( stacks <= 0 ) continue;

        float dmg = get_e_damage( h );
        float ehp = h->get_hp() + h->get_all_shield();
        if ( dmg >= ehp )
            return sdk::common->cast_spell( SLOT_E );
    }
    return false;
}

// Consume spears on an enemy hero just before they expire so we don't waste them.
static bool cast_e_before_expire()
{
    if ( !menu::auto_e_before_expire ) return false;
    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) ) return false;
    if ( !sdk::common || !g_sdk->clock_facade ) return false;

    const float now = g_sdk->clock_facade->get_game_time();

    for ( auto* h : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( h, spell_data::e_range ) ) continue;

        int stacks = get_rend_stacks( h );
        if ( stacks < menu::e_expire_min_stacks ) continue;

        auto* buff = h->get_buff_by_hash( spell_data::buff_rend_stacks );
        if ( !buff || !buff->is_active() ) continue;

        const float remaining = buff->get_end_time() - now;
        if ( remaining > 0.f && remaining <= spell_data::e_expire_window )
            return sdk::common->cast_spell( SLOT_E );
    }
    return false;
}

// ----------------------------------------------------------------------------
// Oathsworn (Fate's Call save target)
// ----------------------------------------------------------------------------
static bool is_oathsworn( game_object* ally )
{
    if ( !ally ) return false;
    return ally->get_buff_by_hash( spell_data::buff_oath_rise )
        || ally->get_buff_by_hash( spell_data::buff_oath_buff )
        || ally->get_buff_by_hash( spell_data::buff_oath_coop );
}

static game_object* find_oathsworn()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return nullptr;

    for ( auto* h : g_sdk->object_manager->get_heroes() )
    {
        if ( !h || !h->is_valid() || h == p ) continue;
        if ( h->get_team_id() != p->get_team_id() ) continue;
        if ( is_oathsworn( h ) ) return h;
    }
    return nullptr;
}

static bool cast_r_save_ally()
{
    if ( !menu::auto_save_r ) return false;
    if ( menu::r_save_combo_only && !( sdk::orbwalker && sdk::orbwalker->combo() ) )
        return false;
    if ( !is_spell_ready( SLOT_R ) || !has_enough_mana( SLOT_R ) || !sdk::common )
        return false;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;

    auto* ally = find_oathsworn();
    if ( !ally || ally->is_dead() ) return false;

    if ( distance_to( ally ) > spell_data::r_tether_radius ) return false;

    const float max_hp = ally->get_max_hp();
    if ( max_hp <= 0.f ) return false;

    const float hp_pct = ( ally->get_hp() + ally->get_all_shield() ) / max_hp * 100.f;
    if ( hp_pct >= static_cast< float >( menu::r_save_hp_pct ) ) return false;

    return sdk::common->cast_spell( SLOT_R, ally );
}

static bool slow_e()
{
    if ( !menu::slow_e ) return false;
    if ( !is_spell_ready( SLOT_E ) || !sdk::common ) return false;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;

    // Only consider when we have plenty of mana for E twice over.
    if ( p->get_par() < p->get_spell_mana_cost( SLOT_E ) * 2.f ) return false;

    bool any_stacked_enemy = false;
    for ( auto* h : g_sdk->object_manager->get_heroes() )
    {
        if ( !is_valid_enemy( h, spell_data::e_range ) ) continue;
        if ( get_rend_stacks( h ) > 0 ) { any_stacked_enemy = true; break; }
    }
    if ( !any_stacked_enemy ) return false;

    // Refund: only fire if there is a stacked minion in range we can finish (CD reset).
    for ( auto* m : g_sdk->object_manager->get_minions() )
    {
        if ( !is_valid_enemy( m, spell_data::e_range ) ) continue;
        int stacks = get_rend_stacks( m );
        if ( stacks <= 0 ) continue;
        if ( get_e_damage( m ) >= m->get_hp() )
            return sdk::common->cast_spell( SLOT_E );
    }
    return false;
}

static pred_sdk::spell_data make_q_spell()
{
    pred_sdk::spell_data s{};
    s.spell_type            = pred_sdk::spell_type::linear;
    s.targetting_type       = pred_sdk::targetting_type::edge_to_edge;
    s.spell_slot            = SLOT_Q;
    s.range                 = spell_data::q_range;
    s.radius                = spell_data::q_radius;
    s.delay                 = spell_data::q_delay;
    s.projectile_speed      = spell_data::q_speed;
    s.expected_hitchance    = pred_sdk::hitchance::medium;
    // Q is a piercing projectile that stops on the first unit/hero it touches,
    // so treat any in-line unit/hero as a hard collision.
    s.forbidden_collisions  = {
        pred_sdk::collision_type::unit,
        pred_sdk::collision_type::hero,
        pred_sdk::collision_type::yasuo_wall,
        pred_sdk::collision_type::braum_wall,
    };
    return s;
}

static bool try_q_combo( game_object* target )
{
    if ( !menu::combo_use_q || !target ) return false;
    if ( !is_spell_ready( SLOT_Q ) || !has_enough_mana( SLOT_Q ) ) return false;
    if ( !sdk::common || !sdk::prediction ) return false;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;

    // Mana gate: keep enough for an E follow-up.
    if ( p->get_par() < p->get_spell_mana_cost( SLOT_Q ) + p->get_spell_mana_cost( SLOT_E ) )
        return false;

    // Attack-speed lockout (BlockQ).
    if ( ( 1.f / p->get_attack_delay() ) > menu::block_q_as ) return false;

    auto spell = make_q_spell();
    pred_sdk::pred_data pred{};
    try { pred = sdk::prediction->predict( target, spell ); }
    catch ( ... ) { pred.is_valid = false; }
    if ( !pred.is_valid || pred.hitchance < pred_sdk::hitchance::medium ) return false;

    if ( menu::q_mode_always )
        return sdk::common->cast_spell( SLOT_Q, pred.cast_position );

    const float aa_r        = p->get_attack_range() + p->get_bounding_radius()
                            + target->get_bounding_radius();
    const bool  target_oor  = distance_to( target ) > aa_r;

    // After-AA mode: fire Q in the AA windup window, OR whenever the target
    // is out of AA range (we can't be "between attacks" if no AA is happening).
    if ( menu::q_mode_after_aa && ( !can_attack_now() || target_oor ) )
        return sdk::common->cast_spell( SLOT_Q, pred.cast_position );

    if ( menu::q_mode_oor && target_oor )
        return sdk::common->cast_spell( SLOT_Q, pred.cast_position );

    return false;
}

// ============================================================================
// GAME MODES
// ============================================================================
void combo()
{
    if ( !sdk::orbwalker ) return;

    slow_e();

    // Killsteal in on_update already covers this; only do it here if it doesn't.
    if ( menu::combo_use_e && !menu::killsteal_e )
        killable_e();

    const int enemies_aa = enemies_in_aa_range();

    if ( is_spell_ready( SLOT_Q ) )
    {
        auto* target = pick_hero_target( spell_data::q_range );
        if ( target && try_q_combo( target ) )
            return;
    }

    // Out of AA range -> orbwalk to closest minion (Martial Poise gap close).
    if ( enemies_aa == 0 && menu::orb_minions )
    {
        if ( menu::restrict_orb && !should_jump_via_aa() ) return;

        auto* p = g_sdk->object_manager->get_local_player();
        if ( !p ) return;
        float aa_r = p->get_attack_range() + p->get_bounding_radius();

        game_object* best = nullptr;
        float best_d = FLT_MAX;
        for ( auto* m : g_sdk->object_manager->get_minions() )
        {
            if ( !is_valid_enemy( m, aa_r ) ) continue;
            float d = distance_to( m );
            if ( d < best_d ) { best_d = d; best = m; }
        }
        if ( best ) sdk::orbwalker->attack( best );
    }
}

void clear()
{
    if ( !sdk::orbwalker ) return;
    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) || !sdk::common ) return;

    const bool any_jungle = menu::jungle_clear_e_epic
                         || menu::jungle_clear_e_large
                         || menu::jungle_clear_e_small;
    if ( !menu::lane_clear_e && !any_jungle ) return;

    int killable_lane = 0;

    for ( auto* m : g_sdk->object_manager->get_minions() )
    {
        if ( !is_valid_enemy( m, spell_data::e_range ) ) continue;
        if ( get_rend_stacks( m ) <= 0 ) continue;

        if ( get_e_damage( m ) >= m->get_hp() && m->is_lane_minion() )
            killable_lane++;
    }

    if ( menu::lane_clear_e && killable_lane >= menu::lane_minions_amount )
    {
        sdk::common->cast_spell( SLOT_E );
        return;
    }

    for ( auto* m : g_sdk->object_manager->get_monsters() )
    {
        if ( !m || !m->is_valid() || m->is_dead() || !m->is_targetable() ) continue;
        if ( distance_to( m ) > spell_data::e_range ) continue;
        if ( get_rend_stacks( m ) <= 0 ) continue;

        bool is_epic  = m->is_epic_monster();
        bool is_large = !is_epic && m->is_large_monster();
        bool is_small = !is_epic && !is_large;

        bool allowed = ( is_epic  && menu::jungle_clear_e_epic )
                    || ( is_large && menu::jungle_clear_e_large )
                    || ( is_small && menu::jungle_clear_e_small );

        if ( !allowed ) continue;
        if ( get_e_damage( m ) < m->get_hp() ) continue;

        sdk::common->cast_spell( SLOT_E );
        return;
    }
}

// ----------------------------------------------------------------------------
// Jump Trick (Martial Poise distance abuse)
//
//   When AA fires, Kalista hops toward her target. Casting Q + moving in a
//   direction different from that hop extends the dash distance.
// ----------------------------------------------------------------------------
static bool do_jump_trick()
{
    if ( !menu::jump_trick_enabled || !menu::jump_trick_active ) return false;
    if ( !g_sdk->hud_manager ) return false;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p ) return false;

    const auto pos    = p->get_position();
    const auto cursor = g_sdk->hud_manager->get_cursor_position();

    math::vector3 to_cursor{ cursor.x - pos.x, 0.f, cursor.z - pos.z };
    const float len_cur2 = to_cursor.x * to_cursor.x + to_cursor.z * to_cursor.z;
    if ( len_cur2 < 25.f * 25.f ) return false;          // cursor essentially on us
    const float len_cur = std::sqrt( len_cur2 );

    // Require a meaningful angle between the hop and the cursor — otherwise
    // we're casting / clicking in the same direction we hopped and the trick
    // doesn't extend the dash.
    const float len_hop2 = s_last_hop_dir.x * s_last_hop_dir.x
                         + s_last_hop_dir.z * s_last_hop_dir.z;
    if ( len_hop2 > 1.f )
    {
        const float len_hop = std::sqrt( len_hop2 );
        const float dot     = ( to_cursor.x * s_last_hop_dir.x
                              + to_cursor.z * s_last_hop_dir.z )
                            / ( len_cur * len_hop );
        const float angle_deg = std::acos( std::clamp( dot, -1.f, 1.f ) )
                              * ( 180.f / 3.14159265f );
        if ( angle_deg < menu::jump_trick_min_angle ) return false;
    }

    // Q position: cursor, clamped to Q range.
    math::vector3 q_pos = cursor;
    if ( len_cur > spell_data::q_range )
    {
        const float scale = spell_data::q_range / len_cur;
        q_pos.x = pos.x + to_cursor.x * scale;
        q_pos.y = pos.y;
        q_pos.z = pos.z + to_cursor.z * scale;
    }

    if ( is_spell_ready( SLOT_Q ) && has_enough_mana( SLOT_Q ) && sdk::common )
        sdk::common->cast_spell( SLOT_Q, q_pos );

    // Right-click twice — the second order keeps the engine from dropping the
    // first one (matches the manual technique).
    p->issue_order( game_object_order::move_to, cursor );
    p->issue_order( game_object_order::move_to, cursor );
    return true;
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================
bool __stdcall on_before_attack( orb_sdk::event_data* data )
{
    if ( !data || !data->target || !sdk::orbwalker || !sdk::common ) return true;
    if ( !data->target->is_hero() ) return true;

    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || data->target->get_team_id() == p->get_team_id() ) return true;

    // Only swap an AA for an E when we'd actually be securing the kill.
    if ( !is_spell_ready( SLOT_E ) || !has_enough_mana( SLOT_E ) ) return true;
    if ( get_rend_stacks( data->target ) <= 0 ) return true;

    const float dmg = get_e_damage( data->target );
    const float ehp = data->target->get_hp() + data->target->get_all_shield();
    if ( dmg < ehp ) return true;

    sdk::common->cast_spell( SLOT_E );
    data->target = nullptr;
    return false; // cancel this auto-attack
}

bool __stdcall on_after_attack( orb_sdk::event_data* data )
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !sdk::orbwalker || !sdk::common || !sdk::prediction ) return true;

    // Jump Trick wins over After-AA Q when its hotkey is held.
    if ( do_jump_trick() ) return true;

    if ( !sdk::orbwalker->combo() ) return true;
    if ( !data || !data->target || !data->target->is_hero() ) return true;
    if ( data->target->get_team_id() == p->get_team_id() ) return true;

    // Attack-speed lockout.
    if ( ( 1.f / p->get_attack_delay() ) > menu::block_q_as ) return true;

    if ( !is_spell_ready( SLOT_Q ) || !has_enough_mana( SLOT_Q ) ) return true;
    if ( p->get_par() < p->get_spell_mana_cost( SLOT_Q ) + p->get_spell_mana_cost( SLOT_E ) )
        return true;
    if ( !menu::q_mode_after_aa ) return true;

    auto spell = make_q_spell();
    pred_sdk::pred_data pred{};
    try { pred = sdk::prediction->predict( data->target, spell ); }
    catch ( ... ) { pred.is_valid = false; }
    if ( !pred.is_valid || pred.hitchance < pred_sdk::hitchance::medium ) return true;

    sdk::common->cast_spell( SLOT_Q, pred.cast_position );
    return true;
}

void __fastcall on_basic_attack( game_object* sender, game_object* target, spell_cast* /*cast*/ )
{
    if ( !g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade || !sender ) return;
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || sender->get_network_id() != p->get_network_id() ) return;

    s_last_attack_time = g_sdk->clock_facade->get_game_time();

    // Remember which direction we're hopping for the jump trick.
    if ( target && target->is_valid() )
    {
        const auto sp = p->get_position();
        const auto tp = target->get_position();
        s_last_hop_dir.x = tp.x - sp.x;
        s_last_hop_dir.y = 0.f;
        s_last_hop_dir.z = tp.z - sp.z;
    }
}

void __fastcall on_update()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || p->is_dead() ) return;

    if ( menu::auto_save_r && cast_r_save_ally() )
        return;

    if ( menu::killsteal_e && killable_e() )
        return;

    if ( cast_e_before_expire() )
        return;

    if ( !sdk::orbwalker ) return;

    if      ( sdk::orbwalker->combo() ) combo();
    else if ( sdk::orbwalker->clear() || sdk::orbwalker->fast_clear() ) clear();
}

void __fastcall on_draw_world()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || p->is_dead() ) return;

    const auto pos = p->get_position();

    if ( menu::draw_q && is_spell_learned( SLOT_Q ) )
    {
        if ( sdk::shaders )
            sdk::shaders->draw_world_circle( pos, spell_data::q_range, 0.5f, color( menu::draw_q_color ) );
        else if ( g_sdk->renderer )
            g_sdk->renderer->add_circle_3d( pos, spell_data::q_range, 1.f, menu::draw_q_color );
    }

    if ( menu::draw_e && is_spell_learned( SLOT_E ) )
    {
        if ( sdk::shaders )
            sdk::shaders->draw_world_circle( pos, spell_data::e_range, 0.5f, color( menu::draw_e_color ) );
        else if ( g_sdk->renderer )
            g_sdk->renderer->add_circle_3d( pos, spell_data::e_range, 1.f, menu::draw_e_color );
    }
}

void __fastcall on_draw()
{
    auto* p = g_sdk->object_manager->get_local_player();
    if ( !p || !g_sdk->renderer ) return;

    if ( menu::damage_draw_mode != 2 )
    {
        menu::draw_cfg.draw( p, []( game_object* t ) -> float
        {
            if ( !is_spell_learned( SLOT_E ) ) return 0.f;
            if ( get_rend_stacks( t ) <= 0 ) return 0.f;
            return get_e_damage( t );
        } );
    }

    // Health-bar text: spear count + "E KILL" indicator.
    if ( ( !menu::draw_stacks && !menu::draw_kill_text ) || !is_spell_learned( SLOT_E ) )
        return;

    auto draw_for = [&]( game_object* t )
    {
        if ( !t || !t->is_valid() || t->is_dead() || !t->is_visible() ) return;
        if ( t->get_team_id() == p->get_team_id() ) return;

        const int stacks = get_rend_stacks( t );
        if ( stacks <= 0 ) return;

        const float dmg = get_e_damage( t );
        const float ehp = t->get_hp() + t->get_all_shield();
        const bool killable = dmg >= ehp;

        const auto screen = g_sdk->renderer->world_to_screen( t->get_position() );
        if ( screen.x <= 1.f && screen.y <= 1.f ) return;

        // Stack text immediately under the unit; kill text below that.
        const float br      = ( std::max )( t->get_bounding_radius(), 35.f );
        const float base_y  = screen.y + br * 0.35f + 8.f;

        if ( menu::draw_stacks )
        {
            char buf[ 24 ];
            std::snprintf( buf, sizeof( buf ), "%d spears", stacks );
            g_sdk->renderer->add_text( buf, 12.f,
                math::vector2{ screen.x, base_y },
                1, menu::draw_stacks_color );
        }

        if ( menu::draw_kill_text && killable )
        {
            const float kill_y = menu::draw_stacks ? base_y + 16.f : base_y;
            g_sdk->renderer->add_text( "E KILL", 14.f,
                math::vector2{ screen.x, kill_y },
                1, menu::draw_kill_color );
        }
    };

    for ( auto* h : g_sdk->object_manager->get_heroes() )   draw_for( h );
    for ( auto* m : g_sdk->object_manager->get_minions() )  draw_for( m );
    for ( auto* m : g_sdk->object_manager->get_monsters() ) draw_for( m );
}

// ============================================================================
// MENU SETUP
// ============================================================================
static void setup_menu()
{
    s_menu = g_sdk->menu_manager->add_category( "meow_kalista", "MeowAIO - Kalista" );
    if ( !s_menu ) return;

    // -- Passive (Martial Poise) -----------------------------------------------
    auto* pass = s_menu->add_sub_category( "p_passive", "P - Martial Poise" );
    pass->add_checkbox( "orb_minions", "Orbwalk minions if target is out of range",
        menu::orb_minions, []( bool v ) { menu::orb_minions = v; } );
    pass->add_checkbox( "restrict_orb", " ^- Only if jump covers more than walking",
        menu::restrict_orb, []( bool v ) { menu::restrict_orb = v; } );

    // -- Q - Pierce ------------------------------------------------------------
    auto* q = s_menu->add_sub_category( "q_pierce", "Q - Pierce" );
    q->add_checkbox( "combo", "Use Q in Combo", menu::combo_use_q,
        []( bool v ) { menu::combo_use_q = v; } );
    q->add_label( "Usage modes" );
    q->add_checkbox( "q_always", "Always", menu::q_mode_always,
        []( bool v ) { menu::q_mode_always = v; } );
    q->add_checkbox( "q_after_aa", "On / After Attack", menu::q_mode_after_aa,
        []( bool v ) { menu::q_mode_after_aa = v; } );
    q->add_checkbox( "q_oor", "Out of AA Range", menu::q_mode_oor,
        []( bool v ) { menu::q_mode_oor = v; } );
    q->add_slider_float( "block_q", "Block if Attack Speed >=", 1.0f, 5.0f, 0.25f,
        menu::block_q_as, []( float v ) { menu::block_q_as = v; } );
    q->add_separator();
    q->add_label( "Drawings" );
    q->add_checkbox( "draw_q", "Draw Q Range", menu::draw_q,
        []( bool v ) { menu::draw_q = v; } );
    q->add_colorpicker( "draw_q_color", "Q Range Color", menu::draw_q_color,
        []( uint32_t v ) { menu::draw_q_color = v; } );

    // -- Jump Trick (Martial Poise distance abuse) -----------------------------
    auto* jt = q->add_sub_category( "jump_trick", "Jump Trick" );
    jt->add_checkbox( "enabled", "Enable Jump Trick", menu::jump_trick_enabled,
        []( bool v ) { menu::jump_trick_enabled = v; } );
    jt->add_hotkey( "hotkey", "Jump Trick", 'X', false, false,
        []( std::string*, bool v ) { menu::jump_trick_active = v; } );

    // -- E - Rend --------------------------------------------------------------
    auto* e = s_menu->add_sub_category( "e_rend", "E - Rend" );
    e->add_checkbox( "combo", "Use E in Combo", menu::combo_use_e,
        []( bool v ) { menu::combo_use_e = v; } );
    e->add_checkbox( "allow_after_aa", "Allow E After Attack", menu::allow_after_aa_e,
        []( bool v ) { menu::allow_after_aa_e = v; } );
    e->add_checkbox( "slow_e", "Use E to slow when CD will be refunded", menu::slow_e,
        []( bool v ) { menu::slow_e = v; } );
    e->add_checkbox( "killsteal", "Killsteal", menu::killsteal_e,
        []( bool v ) { menu::killsteal_e = v; } );
    e->add_checkbox( "auto_e_expire", "Auto E before stacks expire", menu::auto_e_before_expire,
        []( bool v ) { menu::auto_e_before_expire = v; } );
    e->add_slider_int( "expire_min_stacks", " ^- Minimum stacks to save", 1, 10, 1,
        menu::e_expire_min_stacks, []( int v ) { menu::e_expire_min_stacks = v; } );
    e->add_combo( "dmg_draw_mode", "Damage Draw Mode",
        std::vector< std::string >{ "Damage", "Percentage", "Off" },
        menu::damage_draw_mode, []( int v ) { menu::damage_draw_mode = v; } );
    e->add_separator();
    e->add_label( "Drawings" );
    e->add_checkbox( "draw_e", "Draw E Range", menu::draw_e,
        []( bool v ) { menu::draw_e = v; } );
    e->add_colorpicker( "draw_e_color", "E Range Color", menu::draw_e_color,
        []( uint32_t v ) { menu::draw_e_color = v; } );
    e->add_checkbox( "draw_stacks", "Draw Spear Count", menu::draw_stacks,
        []( bool v ) { menu::draw_stacks = v; } );
    e->add_colorpicker( "draw_stacks_color", "Spear Count Color", menu::draw_stacks_color,
        []( uint32_t v ) { menu::draw_stacks_color = v; } );
    e->add_checkbox( "draw_kill_text", "Draw \"E KILL\" Indicator", menu::draw_kill_text,
        []( bool v ) { menu::draw_kill_text = v; } );
    e->add_colorpicker( "draw_kill_color", "E KILL Color", menu::draw_kill_color,
        []( uint32_t v ) { menu::draw_kill_color = v; } );
    e->add_separator();
    e->add_label( "Clear Settings" );
    e->add_checkbox( "lane_e", "Use E for Lane Minions", menu::lane_clear_e,
        []( bool v ) { menu::lane_clear_e = v; } );
    e->add_slider_int( "lane_e_amount", "Minimum Minions Killable", 1, 7, 1,
        menu::lane_minions_amount, []( int v ) { menu::lane_minions_amount = v; } );
    e->add_label( "Jungle Mobs (E)" );
    e->add_checkbox( "jg_epic",  "Epic",  menu::jungle_clear_e_epic,
        []( bool v ) { menu::jungle_clear_e_epic = v; } );
    e->add_checkbox( "jg_large", "Large", menu::jungle_clear_e_large,
        []( bool v ) { menu::jungle_clear_e_large = v; } );
    e->add_checkbox( "jg_small", "Small", menu::jungle_clear_e_small,
        []( bool v ) { menu::jungle_clear_e_small = v; } );

    // -- R - Fate's Call -------------------------------------------------------
    auto* r = s_menu->add_sub_category( "r_fates_call", "R - Fate's Call" );
    r->add_checkbox( "auto_save", "Auto Save", menu::auto_save_r,
        []( bool v ) { menu::auto_save_r = v; } );
    r->add_slider_int( "save_hp", "Save when Ally HP% <=", 5, 50, 1,
        menu::r_save_hp_pct, []( int v ) { menu::r_save_hp_pct = v; } );
    r->add_checkbox( "save_combo_only", "Only Use Auto Save In Combo", menu::r_save_combo_only,
        []( bool v ) { menu::r_save_combo_only = v; } );

    // -- Drawings (HP overlay) -------------------------------------------------
    auto* d = s_menu->add_sub_category( "drawings", "Drawings" );
    auto* dd = d->add_sub_category( "dmg_draws", "E Damage Overlay" );
    menu::draw_cfg.setup_menu( dd );
    dd->add_separator();
    dd->add_label( "Damage Color  = target has spears but survives E" );
    dd->add_label( "Killable Color = target dies from E" );

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
    g_sdk->event_manager->register_callback( event_manager::event::basic_attack,
        reinterpret_cast< void* >( on_basic_attack ) );

    if ( sdk::orbwalker )
    {
        sdk::orbwalker->register_callback( orb_sdk::event_type::before_attack,
            reinterpret_cast< void* >( on_before_attack ) );
        sdk::orbwalker->register_callback( orb_sdk::event_type::after_attack,
            reinterpret_cast< void* >( on_after_attack ) );
    }

    g_sdk->log_console( "[MeowAIO] Kalista loaded MEOW!" );
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
        g_sdk->event_manager->unregister_callback( event_manager::event::basic_attack,
            reinterpret_cast< void* >( on_basic_attack ) );
    }

    if ( sdk::orbwalker )
    {
        sdk::orbwalker->unregister_callback( orb_sdk::event_type::before_attack,
            reinterpret_cast< void* >( on_before_attack ) );
        sdk::orbwalker->unregister_callback( orb_sdk::event_type::after_attack,
            reinterpret_cast< void* >( on_after_attack ) );
    }

    s_menu             = nullptr;
    s_last_attack_time = -30.f;
    s_last_hop_dir     = { 0.f, 0.f, 0.f };
    menu::jump_trick_active = false;

    if ( g_sdk ) g_sdk->log_console( "[MeowAIO] Kalista unloaded" );
}

} // namespace kalista
