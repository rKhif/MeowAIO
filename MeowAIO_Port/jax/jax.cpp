#include "../pch.hpp"
#ifdef MEOWAIO_CHAMP_JAX
#include "db/champions/jax.hpp"

namespace meowaio::champions
{

namespace sc = db::champions::jax_const;

static constexpr float E_HOLD_SAFETY_MARGIN  = 0.15f;
static constexpr float E_LEAVING_THRESHOLD   = 40.f;
static constexpr float WARD_JUMP_TIMEOUT     = 0.6f;
static constexpr float WARD_PLACE_RANGE      = 600.f;
static constexpr float SMART_Q_CLOSING_RATIO = 0.4f;
static constexpr float R_BURST_WINDOW        = 1.5f;
static constexpr float FLEE_MAX_ANGLE        = 90.f;

class jax final : public champion_base
{
public:
    std::string_view get_name() const override { return "Jax"; }

    void on_init() override;
    void on_always_update() override;

    void combo() override;
    void harass() override;
    void clear() override;
    void fast_clear() override;
    void flee() override;

    void on_draw() override;
    void on_draw_world() override;

    bool on_before_attack( orb_sdk::event_data* data ) override;
    void on_execute_cast( game_object* caster, spell_cast* cast ) override;
    void on_basic_attack( game_object* attacker, game_object* target, spell_cast* cast ) override;
    void on_buff_gain( game_object* target, buff_instance* buff ) override;
    void on_buff_loss( game_object* target, buff_instance* buff ) override;
    void on_create_object( game_object* obj ) override;

    float get_combo_damage( game_object* target ) const override;

private:
    void setup_menu();

    bool is_jax_buff( buff_instance* buff, uint32_t hash, const char* name_fragment ) const;
    int  count_enemies_in_range( float range ) const;
    bool is_enemy_closing( game_object* target ) const;
    bool is_in_flee_direction( const ::math::vector3& target_pos ) const;
    bool is_q_jumpable( game_object* obj ) const;

    bool emergency_cast( int slot );
    void r_defensive();
    void e_reactive();
    void e_mitigate();
    void e_recast_logic();
    bool try_q_engage( game_object* target );
    bool try_ward_jump();
    int  find_ward_slot() const;
    void do_clear_lane();
    void do_clear_jungle();

    bool  e_active_          = false;
    float e_start_time_      = 0.f;
    int   e_dodge_count_     = 0;
    bool  w_empowered_       = false;
    bool  r_active_          = false;
    bool  w_pending_reset_   = false;
    bool  ward_jump_pending_ = false;
    float ward_jump_time_    = 0.f;
    int   cached_enemies_    = 0;

    bool  q_combo_           = true;
    bool  q_only_when_e_     = false;
    bool  q_smart_           = true;
    int   q_min_range_       = 175;
    bool  w_combo_           = true;
    int   w_mode_            = 1;
    bool  e_combo_           = true;
    bool  e_recast_          = true;
    int   e_recast_mode_     = 1;
    bool  r_combo_           = true;
    int   r_hp_pct_          = 50;
    bool  r_burst_pred_      = true;
    bool  r_only_nearby_     = true;
    int   r_search_radius_   = 900;

    bool  q_harass_          = false;
    bool  w_harass_          = true;
    bool  e_harass_          = false;
    float harass_mana_pct_   = 0.4f;

    bool  q_lane_            = false;
    bool  w_lane_            = true;
    bool  w_on_turret_       = true;
    bool  e_lane_            = false;
    bool  q_jungle_          = true;
    bool  w_jungle_          = true;
    bool  e_jungle_          = false;
    float clear_mana_pct_    = 0.2f;

    bool  w_fast_            = true;
    bool  e_fast_            = true;

    bool  w_lasthit_         = true;

    bool  q_flee_champ_      = true;
    bool  q_flee_minion_     = true;
    bool  q_flee_ward_       = true;

    bool  e_block_aa_        = true;
    bool  e_interrupter_     = true;
    bool  e_mitigate_dmg_    = true;
    float e_mitigate_thr_    = 0.08f;

    bool     draw_q_         = true;
    bool     draw_e_         = true;
    bool     draw_r_         = false;
    bool     draw_damage_    = true;
    uint32_t q_color_        = 0xFF00FFFF;
    uint32_t e_color_        = 0xFFFFFF00;
    uint32_t r_color_        = 0xFFFF6600;
    uint32_t q_cd_color_     = 0x3200FFFF;
    uint32_t e_cd_color_     = 0x32FFFF00;
    uint32_t r_cd_color_     = 0x32FF6600;
};

void jax::on_init()
{
    setup_spell( Q );
    setup_spell( W );
    setup_spell( E );
    setup_spell( R );
    setup_menu();
}

void jax::setup_menu()
{
    menu_ = g_sdk->menu_manager->add_category( "meowaio_jax", "MeowAIO Jax" );
    if ( !menu_ )
        return;

    auto* c = menu_->add_sub_category( "combo", "Combo" );
    c->add_checkbox( "q", "Use Q", q_combo_, [ this ]( bool v ) { q_combo_ = v; } );

    auto* qcfg = c->add_sub_category( "q_cfg", "Q Config" );
    qcfg->add_checkbox( "q_e_ready", "Q Only When E Ready", q_only_when_e_, [ this ]( bool v ) { q_only_when_e_ = v; } );
    qcfg->add_checkbox( "q_smart", "Smart Q (Don't Q If Enemy Closing)", q_smart_, [ this ]( bool v ) { q_smart_ = v; } );
    qcfg->add_slider_int( "q_min_range", "Q Min Target Range", 0, 700, 25, q_min_range_, [ this ]( int v ) { q_min_range_ = v; } );

    c->add_checkbox( "w", "Use W", w_combo_, [ this ]( bool v ) { w_combo_ = v; } );

    auto* wcfg = c->add_sub_category( "w_cfg", "W Config" );
    wcfg->add_combo( "w_mode", "W Mode", { "Before Attack", "AA Reset" }, w_mode_, [ this ]( int v ) { w_mode_ = v; } );

    c->add_checkbox( "e", "Use E", e_combo_, [ this ]( bool v ) { e_combo_ = v; } );

    auto* ecfg = c->add_sub_category( "e_cfg", "E Config" );
    ecfg->add_checkbox( "e2", "Use E Recast", e_recast_, [ this ]( bool v ) { e_recast_ = v; } );
    ecfg->add_combo( "e2_mode", "E Recast Mode",
        { "Instant Stun", "Enemy Leaving E", "Hold Max Duration", "E>Q Engage" },
        e_recast_mode_, [ this ]( int v ) { e_recast_mode_ = v; } );

    c->add_checkbox( "r", "Use R", r_combo_, [ this ]( bool v ) { r_combo_ = v; } );

    auto* rcfg = c->add_sub_category( "r_cfg", "R Config" );
    rcfg->add_slider_int( "r_hp", "R When HP% Under", 0, 100, 5, r_hp_pct_, [ this ]( int v ) { r_hp_pct_ = v; } );
    rcfg->add_checkbox( "r_burst", "R Burst Prediction", r_burst_pred_, [ this ]( bool v ) { r_burst_pred_ = v; } );
    rcfg->add_checkbox( "r_nearby", "Only When Enemies Nearby", r_only_nearby_, [ this ]( bool v ) { r_only_nearby_ = v; } );
    rcfg->add_slider_int( "r_radius", "Enemies Search Radius", 300, 1600, 50, r_search_radius_, [ this ]( int v ) { r_search_radius_ = v; } );

    auto* h = menu_->add_sub_category( "harass", "Harass" );
    h->add_checkbox( "q", "Use Q", q_harass_, [ this ]( bool v ) { q_harass_ = v; } );
    h->add_checkbox( "w", "Use W", w_harass_, [ this ]( bool v ) { w_harass_ = v; } );
    h->add_checkbox( "e", "Use E", e_harass_, [ this ]( bool v ) { e_harass_ = v; } );
    h->add_slider_float( "mana", "Min Mana %", 0.f, 1.f, 0.05f, harass_mana_pct_,
        [ this ]( float v ) { harass_mana_pct_ = v; } );

    auto* cl = menu_->add_sub_category( "clear", "Clear" );
    cl->add_label( "Lane Clear" );
    cl->add_checkbox( "q_lane", "Use Q", q_lane_, [ this ]( bool v ) { q_lane_ = v; } );
    cl->add_checkbox( "w_lane", "Use W", w_lane_, [ this ]( bool v ) { w_lane_ = v; } );
    cl->add_checkbox( "w_turret", "Use W On Turret", w_on_turret_, [ this ]( bool v ) { w_on_turret_ = v; } );
    cl->add_checkbox( "e_lane", "Use E", e_lane_, [ this ]( bool v ) { e_lane_ = v; } );
    cl->add_separator();
    cl->add_label( "Jungle Clear" );
    cl->add_checkbox( "q_jungle", "Use Q", q_jungle_, [ this ]( bool v ) { q_jungle_ = v; } );
    cl->add_checkbox( "w_jungle", "Use W", w_jungle_, [ this ]( bool v ) { w_jungle_ = v; } );
    cl->add_checkbox( "e_jungle", "Use E", e_jungle_, [ this ]( bool v ) { e_jungle_ = v; } );
    cl->add_separator();
    cl->add_label( "Fast Clear" );
    cl->add_checkbox( "w_fast", "Use W", w_fast_, [ this ]( bool v ) { w_fast_ = v; } );
    cl->add_checkbox( "e_fast", "Use E", e_fast_, [ this ]( bool v ) { e_fast_ = v; } );
    cl->add_separator();
    cl->add_slider_float( "mana", "Min Mana %", 0.f, 1.f, 0.05f, clear_mana_pct_,
        [ this ]( float v ) { clear_mana_pct_ = v; } );

    auto* lh = menu_->add_sub_category( "lasthit", "Last Hit" );
    lh->add_checkbox( "w", "Use W to Secure", w_lasthit_, [ this ]( bool v ) { w_lasthit_ = v; } );

    auto* f = menu_->add_sub_category( "flee", "Flee" );
    f->add_checkbox( "q_champ", "Q to Ally Champions", q_flee_champ_, [ this ]( bool v ) { q_flee_champ_ = v; } );
    f->add_checkbox( "q_minion", "Q to Ally Minions", q_flee_minion_, [ this ]( bool v ) { q_flee_minion_ = v; } );
    f->add_checkbox( "q_ward", "Ward Jump (Place + Q)", q_flee_ward_, [ this ]( bool v ) { q_flee_ward_ = v; } );

    auto* m = menu_->add_sub_category( "misc", "Misc" );
    m->add_checkbox( "e_block", "E to Block AA", e_block_aa_, [ this ]( bool v ) { e_block_aa_ = v; } );
    m->add_checkbox( "e_interrupt", "E Spell Interrupter", e_interrupter_, [ this ]( bool v ) { e_interrupter_ = v; } );
    m->add_checkbox( "e_mitigate", "E Damage Mitigation", e_mitigate_dmg_, [ this ]( bool v ) { e_mitigate_dmg_ = v; } );
    add_target_mode_menu( m );

    auto* d = menu_->add_sub_category( "draw", "Drawings" );
    d->add_checkbox( "q_range", "Q Range", draw_q_, [ this ]( bool v ) { draw_q_ = v; } );
    d->add_colorpicker( "q_color", "Q Color", q_color_, [ this ]( uint32_t v ) { q_color_ = v; } );
    d->add_colorpicker( "q_cd_color", "Q On CD Color", q_cd_color_, [ this ]( uint32_t v ) { q_cd_color_ = v; } );
    d->add_checkbox( "e_range", "E Range", draw_e_, [ this ]( bool v ) { draw_e_ = v; } );
    d->add_colorpicker( "e_color", "E Color", e_color_, [ this ]( uint32_t v ) { e_color_ = v; } );
    d->add_colorpicker( "e_cd_color", "E On CD Color", e_cd_color_, [ this ]( uint32_t v ) { e_cd_color_ = v; } );
    d->add_checkbox( "r_range", "R Range", draw_r_, [ this ]( bool v ) { draw_r_ = v; } );
    d->add_colorpicker( "r_color", "R Color", r_color_, [ this ]( uint32_t v ) { r_color_ = v; } );
    d->add_colorpicker( "r_cd_color", "R On CD Color", r_cd_color_, [ this ]( uint32_t v ) { r_cd_color_ = v; } );
    d->add_checkbox( "damage", "Damage Indicator", draw_damage_, [ this ]( bool v ) { draw_damage_ = v; } );
}

bool jax::is_jax_buff( buff_instance* buff, uint32_t hash, const char* name_fragment ) const
{
    if ( hash != 0 )
        return buff->get_hash() == hash;
    return buff->get_name().find( name_fragment ) != std::string::npos;
}

int jax::count_enemies_in_range( float range ) const
{
    int count = 0;
    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !hero || !hero->is_valid() || hero->is_dead() || !hero->is_visible() || !hero->is_targetable() ) continue;
        if ( hero->get_team_id() == player_->get_team_id() ) continue;
        if ( meowaio::math::dist( pos_, hero->get_position() ) <= range )
            ++count;
    }
    return count;
}

bool jax::is_enemy_closing( game_object* target ) const
{
    if ( !target || !target->is_moving() )
        return false;

    auto path = target->get_path();
    if ( path.empty() )
        return false;

    float current_dist = meowaio::math::dist( pos_, target->get_position() );
    float dest_dist    = meowaio::math::dist( pos_, path.back() );

    return dest_dist < current_dist * ( 1.f - SMART_Q_CLOSING_RATIO );
}

bool jax::is_in_flee_direction( const ::math::vector3& target_pos ) const
{
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    auto flee_dir   = ( cursor_pos - pos_ ).to_2d();
    auto target_dir = ( target_pos - pos_ ).to_2d();

    if ( flee_dir == ::math::vector2{} || target_dir == ::math::vector2{} )
        return true;

    float angle = flee_dir.angle_between( target_dir );
    if ( std::isnan( angle ) )
        return true;

    return angle <= FLEE_MAX_ANGLE;
}

bool jax::is_q_jumpable( game_object* obj ) const
{
    if ( !obj || !obj->is_valid() || obj->is_dead() || obj->is_turret() )
        return false;
    if ( !obj->is_visible() || !obj->is_targetable() )
        return false;
    if ( obj->get_team_id() != player_->get_team_id() )
        return false;
    return meowaio::math::dist( pos_, obj->get_position() ) <= sc::Q_RANGE;
}

bool jax::emergency_cast( int slot )
{
    if ( !spells_[ slot ].is_ready() || !has_mana_for( slot ) )
        return false;

    if ( sdk::evade && sdk::evade->is_evading() && !sdk::evade->can_spell( slot, 0.25f ) )
        return false;

    return sdk::common->cast_spell( slot );
}

void jax::r_defensive()
{
    if ( !r_combo_ || !spells_[ R ].is_ready() || !spells_[ R ].learned || !has_mana_for( R ) || r_active_ )
        return;
    if ( r_only_nearby_ && cached_enemies_ == 0 )
        return;
    if ( count_enemies_in_range( sc::R_RADIUS ) == 0 )
        return;

    float hp_threshold = static_cast< float >( r_hp_pct_ ) / 100.f;

    if ( hp_percent_ < hp_threshold )
    {
        emergency_cast( R );
        return;
    }

    if ( !r_burst_pred_ || !sdk::health_prediction )
        return;

    float predicted = sdk::health_prediction->get_predicted_health( player_, game_time_ + R_BURST_WINDOW );
    float predicted_pct = ( max_hp_ > 0.f ) ? predicted / max_hp_ : 1.f;

    if ( predicted_pct < hp_threshold )
        emergency_cast( R );
}

void jax::e_reactive()
{
    if ( !spells_[ E ].learned || !has_mana_for( E ) )
        return;

    bool can_first = spells_[ E ].is_ready() && !e_active_;

    for ( auto* enemy : g_sdk->object_manager->get_heroes() )
    {
        if ( !enemy || !enemy->is_valid() || enemy->is_dead() || !enemy->is_visible() ) continue;
        if ( enemy->get_team_id() == player_->get_team_id() ) continue;

        if ( e_block_aa_ && can_first )
        {
            auto* active = enemy->get_active_spell_cast();
            if ( active )
            {
                auto* cast = active->get_spell_cast();
                if ( cast && cast->is_basic_attack() && cast->get_target() == player_ )
                {
                    if ( emergency_cast( E ) )
                        return;
                }
            }
        }

        if ( !e_interrupter_ )
            continue;
        if ( meowaio::math::dist( pos_, enemy->get_position() ) > sc::E_RADIUS )
            continue;

        auto* active = enemy->get_active_spell_cast();
        if ( !active ) continue;
        auto* cast = active->get_spell_cast();
        if ( !cast ) continue;
        auto* sd = cast->get_spell_data();
        if ( !sd ) continue;
        auto* ssd = sd->get_static_data();
        if ( !ssd ) continue;

        if ( !ssd->channel_is_interrupted_by_disables() )
            continue;

        if ( e_active_ || can_first )
        {
            if ( emergency_cast( E ) )
                return;
        }
    }
}

void jax::e_mitigate()
{
    if ( !e_mitigate_dmg_ || !spells_[ E ].is_ready() || !spells_[ E ].learned || e_active_ || !has_mana_for( E ) )
        return;
    if ( !sdk::orbwalker || ( !sdk::orbwalker->combo() && !sdk::orbwalker->harass() ) )
        return;
    if ( !sdk::health_prediction )
        return;

    float predicted   = sdk::health_prediction->get_predicted_health( player_, game_time_ + 1.0f );
    float dmg_incoming = hp_ - predicted;

    if ( dmg_incoming >= max_hp_ * e_mitigate_thr_ )
        emergency_cast( E );
}

void jax::e_recast_logic()
{
    if ( !e_recast_ || !e_active_ || !spells_[ E ].is_ready() )
        return;
    if ( game_time_ < e_start_time_ + sc::E_MIN_RECAST_DELAY )
        return;

    game_object* closest = nullptr;
    float closest_dist = FLT_MAX;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !hero || !hero->is_valid() || hero->is_dead() || !hero->is_visible() || !hero->is_targetable() ) continue;
        if ( hero->get_team_id() == player_->get_team_id() ) continue;

        float d = meowaio::math::dist( pos_, hero->get_position() );
        if ( d < closest_dist )
        {
            closest_dist = d;
            closest      = hero;
        }
    }

    if ( !closest )
        return;

    float time_remaining = ( e_start_time_ + sc::E_EVASION_DURATION ) - game_time_;

    switch ( e_recast_mode_ )
    {
    case 0:
        if ( closest_dist <= sc::E_RADIUS )
            emergency_cast( E );
        break;

    case 1:
        if ( closest_dist >= sc::E_RADIUS - E_LEAVING_THRESHOLD && closest_dist <= sc::E_RADIUS + E_LEAVING_THRESHOLD )
            emergency_cast( E );
        else if ( time_remaining <= E_HOLD_SAFETY_MARGIN && closest_dist <= sc::E_RADIUS )
            emergency_cast( E );
        break;

    case 2:
        if ( time_remaining <= E_HOLD_SAFETY_MARGIN && closest_dist <= sc::E_RADIUS )
            emergency_cast( E );
        break;

    case 3:
        if ( closest_dist <= sc::E_RADIUS )
        {
            emergency_cast( E );
            break;
        }
        // Q gap-close to stun if target outside E but within Q range
        if ( closest_dist <= sc::Q_RANGE && spells_[ Q ].is_ready() && has_mana_for( Q ) )
        {
            if ( !sdk::evade || !sdk::evade->is_evading() )
                sdk::common->cast_spell( Q, closest );
            break;
        }
        break;

    default:
        break;
    }
}

bool jax::try_q_engage( game_object* target )
{
    if ( !target || !spells_[ Q ].is_ready() || !spells_[ Q ].learned || !has_mana_for( Q ) )
        return false;

    float dist = meowaio::math::dist( pos_, target->get_position() );
    if ( dist <= static_cast< float >( q_min_range_ ) )
        return false;
    if ( dist > sc::Q_RANGE + target->get_bounding_radius() )
        return false;
    if ( q_only_when_e_ && !spells_[ E ].is_ready() )
        return false;
    if ( q_smart_ && is_enemy_closing( target ) )
        return false;

    if ( sdk::evade && sdk::evade->is_evading() && !sdk::evade->can_spell( Q, 0.f ) )
        return false;

    // Pre-buffer E before Q for guaranteed stun on arrival
    if ( e_recast_mode_ != 3 && e_combo_ && spells_[ E ].is_ready() && !e_active_ && has_mana_for( E ) )
        emergency_cast( E );

    // W before Q empowers leap damage in mode 0
    if ( w_combo_ && w_mode_ == 0 && spells_[ W ].is_ready() && !w_empowered_ && has_mana_for( W ) )
        sdk::common->cast_spell( W );

    return sdk::common->cast_spell( Q, target );
}

int jax::find_ward_slot() const
{
    auto check = [ this ]( int item_id, bool needs_charges ) -> int
    {
        int found = -1;
        if ( !player_->has_item( item_id, &found ) )
            return -1;

        int spell_slot = found + 6;
        auto* entry = player_->get_spell( spell_slot );
        if ( !entry )
            return -1;
        if ( needs_charges && entry->get_charges() <= 0 )
            return -1;
        if ( entry->get_cooldown() > 0.f )
            return -1;

        return spell_slot;
    };

    int s = check( sc::WARD_TRINKET_ID, true );
    if ( s >= 0 ) return s;

    s = check( sc::CONTROL_WARD_ID, false );
    if ( s >= 0 ) return s;

    s = check( sc::FARSIGHT_ID, true );
    return s;
}

bool jax::try_ward_jump()
{
    if ( !q_flee_ward_ || !spells_[ Q ].is_ready() || !spells_[ Q ].learned || !has_mana_for( Q ) )
        return false;

    if ( ward_jump_pending_ && game_time_ > ward_jump_time_ + WARD_JUMP_TIMEOUT )
        ward_jump_pending_ = false;

    if ( ward_jump_pending_ )
        return true;

    for ( auto* ward : g_sdk->object_manager->get_wards() )
    {
        if ( !is_q_jumpable( ward ) ) continue;
        if ( !is_in_flee_direction( ward->get_position() ) ) continue;
        return sdk::common->cast_spell( Q, ward );
    }

    for ( auto* trap : g_sdk->object_manager->get_traps() )
    {
        if ( !trap || !trap->is_valid() || trap->is_dead() ) continue;
        if ( trap->get_team_id() != player_->get_team_id() ) continue;
        if ( meowaio::math::dist( pos_, trap->get_position() ) > sc::Q_RANGE ) continue;
        if ( trap->get_name().find( "JammerDevice" ) == std::string::npos ) continue;
        if ( !is_in_flee_direction( trap->get_position() ) ) continue;
        return sdk::common->cast_spell( Q, trap );
    }

    int ward_slot = find_ward_slot();
    if ( ward_slot < 0 )
        return false;

    auto cursor = g_sdk->hud_manager->get_cursor_position();
    auto place_pos = cursor;
    float dist = meowaio::math::dist( pos_, cursor );
    if ( dist > WARD_PLACE_RANGE )
        place_pos = pos_.extended( cursor, WARD_PLACE_RANGE );

    if ( !sdk::common->cast_spell( ward_slot, place_pos ) )
        return false;

    ward_jump_pending_ = true;
    ward_jump_time_    = game_time_;
    return true;
}

void jax::on_always_update()
{
    if ( !player_ || player_->is_dead() )
        return;

    cached_enemies_ = count_enemies_in_range( static_cast< float >( r_search_radius_ ) );

    r_defensive();
    e_reactive();
    e_mitigate();
    e_recast_logic();
}

void jax::combo()
{
    ward_jump_pending_ = false;

    if ( sdk::orbwalker && sdk::orbwalker->would_cancel_attack() )
        return;

    auto* target = get_target( sc::Q_RANGE );
    if ( !target )
        return;

    float dist = meowaio::math::dist( pos_, target->get_position() );

    if ( e_combo_ && can_cast( E ) && !e_active_ && has_mana_for( E ) )
    {
        if ( e_recast_mode_ != 3 && dist <= sc::E_RADIUS )
            cast_spell_self( E );
    }

    if ( e_recast_mode_ == 3 && e_combo_ && can_cast( E ) && !e_active_ && has_mana_for( E ) )
    {
        if ( q_combo_ && spells_[ Q ].is_ready() && has_mana_for( Q )
            && dist > static_cast< float >( q_min_range_ )
            && dist <= sc::Q_RANGE + target->get_bounding_radius() )
        {
            cast_spell_self( E );
            return;
        }
    }

    if ( q_combo_ )
        try_q_engage( target );
}

void jax::harass()
{
    if ( mana_percent_ < harass_mana_pct_ )
        return;

    ward_jump_pending_ = false;

    if ( sdk::orbwalker && sdk::orbwalker->would_cancel_attack() )
        return;

    auto* target = get_target( sc::Q_RANGE );
    if ( !target )
        return;

    if ( is_under_turret( pos_ ) )
        return;

    float dist = meowaio::math::dist( pos_, target->get_position() );

    if ( e_harass_ && can_cast( E ) && !e_active_ && has_mana_for( E ) && dist <= sc::E_RADIUS )
        cast_spell_self( E );

    if ( q_harass_ )
        try_q_engage( target );
}

void jax::clear()
{
    if ( mana_percent_ < clear_mana_pct_ )
        return;

    ward_jump_pending_ = false;

    if ( sdk::orbwalker && sdk::orbwalker->would_cancel_attack() )
        return;

    do_clear_jungle();
    do_clear_lane();
}

void jax::fast_clear()
{
    if ( mana_percent_ < clear_mana_pct_ )
        return;
    if ( !sdk::orbwalker || sdk::orbwalker->would_cancel_attack() )
        return;

    if ( !e_fast_ || !can_cast( E ) || e_active_ || !has_mana_for( E ) )
        return;

    int count = 0;
    for ( auto* minion : g_sdk->object_manager->get_minions() )
    {
        if ( !minion || !minion->is_valid() || minion->is_dead() ) continue;
        if ( minion->get_team_id() == player_->get_team_id() ) continue;
        if ( !minion->is_visible() ) continue;
        if ( meowaio::math::dist( pos_, minion->get_position() ) <= sc::E_RADIUS && ++count >= 2 )
            break;
    }

    if ( count >= 2 )
        cast_spell_self( E );
}

void jax::do_clear_jungle()
{
    if ( !sdk::target_selector || !sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled() )
        return;

    auto* monster = sdk::target_selector->get_monster_target();
    if ( !monster )
        return;

    float dist = meowaio::math::dist( pos_, monster->get_position() );

    if ( q_jungle_ && can_cast( Q ) && has_mana_for( Q ) && dist <= sc::Q_RANGE && dist > aa_range_ + 50.f )
    {
        cast_spell_target( Q, monster );
        return;
    }

    if ( e_jungle_ && can_cast( E ) && !e_active_ && has_mana_for( E ) && dist <= sc::E_RADIUS )
        cast_spell_self( E );
}

void jax::do_clear_lane()
{
    if ( !sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled() )
        return;

    auto* orb_target = sdk::orbwalker->get_current_target();
    if ( orb_target && orb_target->is_turret() )
        return;

    game_object* best = nullptr;
    float best_dist   = FLT_MAX;

    for ( auto* minion : g_sdk->object_manager->get_minions() )
    {
        if ( !minion || !minion->is_valid() || minion->is_dead() ) continue;
        if ( minion->get_team_id() == player_->get_team_id() ) continue;
        if ( !minion->is_lane_minion() || !minion->is_visible() || !minion->is_targetable() ) continue;

        float d = meowaio::math::dist( pos_, minion->get_position() );
        if ( d < best_dist )
        {
            best_dist = d;
            best      = minion;
        }
    }

    if ( !best )
        return;

    if ( q_lane_ && can_cast( Q ) && has_mana_for( Q ) && best_dist <= sc::Q_RANGE )
    {
        cast_spell_target( Q, best );
        return;
    }

    if ( e_lane_ && can_cast( E ) && !e_active_ && has_mana_for( E ) && best_dist <= sc::E_RADIUS )
        cast_spell_self( E );
}

void jax::flee()
{
    if ( spells_[ E ].is_ready() && !e_active_ && has_mana_for( E ) && count_enemies_in_range( sc::E_RADIUS + 200.f ) > 0 )
        emergency_cast( E );

    if ( !spells_[ Q ].is_ready() || !spells_[ Q ].learned || !has_mana_for( Q ) )
        return;

    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    game_object* best = nullptr;
    float best_cursor_dist = FLT_MAX;

    if ( q_flee_champ_ )
    {
        for ( auto* hero : g_sdk->object_manager->get_heroes() )
        {
            if ( !is_q_jumpable( hero ) || hero == player_ ) continue;
            if ( !is_in_flee_direction( hero->get_position() ) ) continue;

            float cd = meowaio::math::dist( cursor_pos, hero->get_position() );
            if ( cd < best_cursor_dist )
            {
                best_cursor_dist = cd;
                best = hero;
            }
        }
    }

    if ( q_flee_minion_ )
    {
        for ( auto* minion : g_sdk->object_manager->get_minions() )
        {
            if ( !is_q_jumpable( minion ) ) continue;
            if ( !is_in_flee_direction( minion->get_position() ) ) continue;

            float cd = meowaio::math::dist( cursor_pos, minion->get_position() );
            if ( cd < best_cursor_dist )
            {
                best_cursor_dist = cd;
                best = minion;
            }
        }
    }

    if ( best )
    {
        sdk::common->cast_spell( Q, best );
        return;
    }

    try_ward_jump();
}

float jax::get_combo_damage( game_object* target ) const
{
    if ( !target || !player_ )
        return 0.f;

    float dmg = 0.f;

    if ( spells_[ Q ].learned && spells_[ Q ].rank > 0 )
        dmg += get_spell_damage( Q, target );

    if ( spells_[ W ].learned && spells_[ W ].rank > 0 )
        dmg += get_spell_damage( W, target );

    if ( spells_[ E ].learned && spells_[ E ].rank > 0 )
    {
        float e_base = get_spell_damage( E, target );
        if ( e_active_ && e_dodge_count_ > 0 )
        {
            int capped = ( std::min )( e_dodge_count_, sc::E_MAX_DODGE_BONUS_STACKS );
            e_base *= 1.f + sc::E_DAMAGE_INCREASE_PER_DODGE * static_cast< float >( capped );
        }
        dmg += e_base;
    }

    if ( spells_[ R ].learned && spells_[ R ].rank > 0 )
        dmg += get_spell_damage( R, target );

    dmg += get_aa_damage( target ) * 3.f;

    auto* r_passive = db_->find_passive( "JaxRPassive" );
    if ( r_passive && spells_[ R ].learned )
        dmg += get_passive_damage( r_passive, target );

    return dmg;
}

void jax::on_draw_world()
{
    if ( !player_ || player_->is_dead() )
        return;

    if ( draw_q_ && spells_[ Q ].learned )
    {
        uint32_t clr = spells_[ Q ].is_ready() ? q_color_ : q_cd_color_;
        g_sdk->renderer->add_circle_3d( pos_, sc::Q_RANGE, 0.5f, clr );
    }

    if ( draw_e_ && spells_[ E ].learned )
    {
        uint32_t clr = spells_[ E ].is_ready() ? e_color_ : e_cd_color_;
        g_sdk->renderer->add_circle_3d( pos_, sc::E_RADIUS, 0.5f, clr );
    }

    if ( draw_r_ && spells_[ R ].learned )
    {
        uint32_t clr = spells_[ R ].is_ready() ? r_color_ : r_cd_color_;
        g_sdk->renderer->add_circle_3d( pos_, sc::R_RADIUS, 0.5f, clr );
    }
}

void jax::on_draw()
{
    if ( !draw_damage_ || !player_ || player_->is_dead() )
        return;

    for ( auto* hero : g_sdk->object_manager->get_heroes() )
    {
        if ( !hero || !hero->is_valid() || hero->is_dead() || !hero->is_visible() ) continue;
        if ( hero->get_team_id() == player_->get_team_id() ) continue;
        g_sdk->renderer->add_damage_indicator( hero, get_combo_damage( hero ) );
    }
}

void jax::on_buff_gain( game_object* target, buff_instance* buff )
{
    if ( !player_ || target != player_ || !buff )
        return;

    if ( is_jax_buff( buff, sc::BUFF_E_EVASION, sc::BUFF_NAME_E ) )
    {
        e_active_     = true;
        e_start_time_ = game_time_;
        e_dodge_count_ = 0;
    }
    else if ( is_jax_buff( buff, sc::BUFF_W_EMPOWER, sc::BUFF_NAME_W ) )
    {
        w_empowered_ = true;
    }
    else if ( is_jax_buff( buff, sc::BUFF_R_ACTIVE, sc::BUFF_NAME_R ) )
    {
        r_active_ = true;
    }
}

void jax::on_buff_loss( game_object* target, buff_instance* buff )
{
    if ( !player_ || target != player_ || !buff )
        return;

    if ( is_jax_buff( buff, sc::BUFF_E_EVASION, sc::BUFF_NAME_E ) )
    {
        e_active_     = false;
        e_start_time_ = 0.f;
    }
    else if ( is_jax_buff( buff, sc::BUFF_W_EMPOWER, sc::BUFF_NAME_W ) )
    {
        w_empowered_ = false;
    }
    else if ( is_jax_buff( buff, sc::BUFF_R_ACTIVE, sc::BUFF_NAME_R ) )
    {
        r_active_ = false;
    }
}

void jax::on_basic_attack( game_object* attacker, game_object* target, spell_cast* )
{
    if ( target == player_ && e_active_ && attacker && attacker->get_team_id() != player_->get_team_id() )
        ++e_dodge_count_;
}

bool jax::on_before_attack( orb_sdk::event_data* data )
{
    if ( !data || !data->target || !sdk::orbwalker )
        return true;

    auto* target = data->target;

    if ( !spells_[ W ].is_ready() || !spells_[ W ].learned || w_empowered_ || !has_mana_for( W ) )
        return true;

    bool want_w = false;

    if ( target->is_hero() )
    {
        if ( sdk::orbwalker->combo() && w_combo_ )
            want_w = true;
        else if ( sdk::orbwalker->harass() && w_harass_ )
            want_w = true;
    }
    else if ( sdk::orbwalker->clear() )
    {
        if ( target->is_turret() && w_on_turret_ )
            want_w = true;
        else if ( sdk::orbwalker->is_spell_farm_enabled() )
        {
            if ( target->is_lane_minion() && w_lane_ )
                want_w = true;
            else if ( target->is_monster() && w_jungle_ )
                want_w = true;
        }
    }
    else if ( sdk::orbwalker->fast_clear() && w_fast_ )
    {
        want_w = true;
    }
    else if ( sdk::orbwalker->lasthit() && w_lasthit_ && target->is_lane_minion() )
    {
        // Only W to secure kills the AA alone would miss
        float aa_dmg = get_aa_damage( target );
        float w_dmg  = get_spell_damage( W, target );
        float hp     = target->get_hp();

        if ( hp > aa_dmg && hp <= aa_dmg + w_dmg )
            want_w = true;
    }

    if ( !want_w )
        return true;

    if ( w_mode_ == 0 )
        sdk::common->cast_spell( W );
    else
        w_pending_reset_ = true;

    return true;
}

void jax::on_execute_cast( game_object* caster, spell_cast* cast )
{
    if ( caster != player_ || !cast )
        return;
    if ( !cast->is_basic_attack() && !cast->is_special_attack() )
        return;
    if ( !w_pending_reset_ )
        return;

    w_pending_reset_ = false;

    if ( !spells_[ W ].is_ready() || !spells_[ W ].learned || w_empowered_ || !has_mana_for( W ) )
        return;

    sdk::common->cast_spell( W );
}

void jax::on_create_object( game_object* obj )
{
    if ( !ward_jump_pending_ || !obj )
        return;

    if ( !sdk::orbwalker || !sdk::orbwalker->flee() )
    {
        ward_jump_pending_ = false;
        return;
    }

    if ( !obj->is_ward() && !obj->is_trap() )
        return;
    if ( obj->get_team_id() != player_->get_team_id() )
        return;
    if ( obj->is_trap() && obj->get_name().find( "JammerDevice" ) == std::string::npos )
        return;
    if ( meowaio::math::dist( pos_, obj->get_position() ) > sc::Q_RANGE )
        return;

    sdk::common->cast_spell( Q, obj );
    ward_jump_pending_ = false;
}

std::unique_ptr< champion_base > create_jax()
{
    return std::make_unique< jax >();
}

} // namespace meowaio::champions
#endif // MEOWAIO_CHAMP_JAX


