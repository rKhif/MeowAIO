#include "pch.hpp"

#include "champions/aphelios/aphelios.hpp"

#include "db/champions/aphelios.hpp"

#include <string>
#include <string_view>

namespace sora::champions
{

namespace db = sora::db::champions::aphelios;

static constexpr int   SLIDER_AMMO_MIN  = 0;
static constexpr int   SLIDER_AMMO_MAX  = 50;
static constexpr int   SLIDER_AMMO_STEP = 1;

static constexpr uint32_t weapon_color( db::weapon w ) noexcept
{
    switch ( w )
    {
        case db::weapon::calibrum:   { return 0xFF44CCFFu; } // light blue
        case db::weapon::severum:    { return 0xFFFF4444u; } // red
        case db::weapon::gravitum:   { return 0xFFCC66FFu; } // purple
        case db::weapon::infernum:   { return 0xFFFF8800u; } // orange
        case db::weapon::crescendum: { return 0xFFEEEEEEu; } // white
        default:                     { return 0xFF888888u; } // grey
    }
}

static constexpr float    HUD_TEXT_SMALL = 14.f;
static constexpr float    HUD_LINE_H   = 20.f;
static constexpr uint32_t HUD_LABEL_COLOR = 0xFFAAAAAAu;
static constexpr size_t   HUD_BUF_SIZE = 128u;

static void draw_world_circle( const math::vector3& pos, float radius, uint32_t color_value )
{
    if ( sdk::shaders != nullptr )
    {
        sdk::shaders->draw_world_circle( pos, radius, 2.f, color( color_value ) );
    }
    else if ( g_sdk != nullptr && g_sdk->renderer != nullptr )
    {
        g_sdk->renderer->add_circle_3d( pos, radius, 2.f, color_value );
    }
}

static constexpr const char* slot_label( int slot ) noexcept
{
    switch ( slot )
    {
        case 0:  { return "Q"; }
        case 1:  { return "W"; }
        case 2:  { return "E"; }
        case 3:  { return "R"; }
        default: { return "?"; }
    }
}

static constexpr int HC_MIN  = 0;
static constexpr int HC_MAX  = 100;
static constexpr int HC_STEP = 5;

void aphelios::setup_combo_menu( menu_category* parent )
{
    auto* combo = parent->add_sub_category( "combo", "Combo Settings" );
    if ( combo == nullptr ) return;

    auto* cal = combo->add_sub_category( "calibrum", "Calibrum" );
    if ( cal != nullptr )
    {
        cal->add_checkbox( "use_q", "Use Q",
            cal_use_q_master_, [this]( bool v ) { cal_use_q_master_ = v; } );
        cal->add_slider_int( "q_hc", "Hitchance",
            HC_MIN, HC_MAX, HC_STEP, cal_q_hitchance_,
            [this]( int v ) { cal_q_hitchance_ = v; } );
        cal->add_slider_int( "ammo_hold", "Ammo Hold Threshold",
            SLIDER_AMMO_MIN, SLIDER_AMMO_MAX, SLIDER_AMMO_STEP, cal_ammo_hold_,
            [this]( int v ) { cal_ammo_hold_ = v; } );
        cal->add_checkbox( "use_r", "Use R",
            cal_use_r_master_, [this]( bool v ) { cal_use_r_master_ = v; } );
        cal->add_slider_int( "r_hc", "R Hitchance",
            HC_MIN, HC_MAX, HC_STEP, cal_r_hitchance_,
            [this]( int v ) { cal_r_hitchance_ = v; } );
        cal->add_checkbox( "qr_combo", "Q + R Combo",
            cal_qr_combo_, [this]( bool v ) { cal_qr_combo_ = v; } );
        cal->add_checkbox( "cres_mv_opener", "Cal-Cres MV Opener",
            cres_cal_mv_opener_, [this]( bool v ) { cres_cal_mv_opener_ = v; } );
    }

    auto* inf = combo->add_sub_category( "infernum", "Infernum" );
    if ( inf != nullptr )
    {
        inf->add_checkbox( "use_q", "Use Q",
            inf_use_q_master_, [this]( bool v ) { inf_use_q_master_ = v; } );
        inf->add_slider_int( "q_hc", "Hitchance",
            HC_MIN, HC_MAX, HC_STEP, inf_q_hitchance_,
            [this]( int v ) { inf_q_hitchance_ = v; } );
        inf->add_checkbox( "duskwave_solo", "Duskwave Solo",
            inf_duskwave_solo_combo_, [this]( bool v ) { inf_duskwave_solo_combo_ = v; } );
        inf->add_checkbox( "duskwave_multi", "Duskwave Multi",
            inf_duskwave_multi_combo_, [this]( bool v ) { inf_duskwave_multi_combo_ = v; } );
        inf->add_slider_int( "q_min_targets", "Min Targets",
            2, 5, 1, inf_q_min_targets_,
            [this]( int v ) { inf_q_min_targets_ = v; } );
        inf->add_checkbox( "use_r", "Use R",
            inf_use_r_master_, [this]( bool v ) { inf_use_r_master_ = v; } );
        inf->add_slider_int( "r_hc", "R Hitchance",
            HC_MIN, HC_MAX, HC_STEP, inf_r_hitchance_,
            [this]( int v ) { inf_r_hitchance_ = v; } );
        inf->add_checkbox( "r_aoe", "R AoE",
            inf_r_combo_, [this]( bool v ) { inf_r_combo_ = v; } );
        inf->add_checkbox( "r_finisher", "R Finisher",
            inf_r_solo_finisher_combo_, [this]( bool v ) { inf_r_solo_finisher_combo_ = v; } );
        inf->add_slider_int( "r_min_enemies", "R Min Enemies",
            2, 5, 1, inf_r_min_enemies_,
            [this]( int v ) { inf_r_min_enemies_ = v; } );
        inf->add_checkbox( "cal_swap", "Infernum -> Calibrum",
            inf_cal_swap_combo_, [this]( bool v ) { inf_cal_swap_combo_ = v; } );
        inf->add_checkbox( "gra_swap", "Infernum -> Gravitum",
            inf_gra_swap_combo_, [this]( bool v ) { inf_gra_swap_combo_ = v; } );
        inf->add_slider_int( "gra_swap_min_marks", "Inf->Gra Min Marks",
            1, 5, 1, inf_gra_swap_min_marks_,
            [this]( int v ) { inf_gra_swap_min_marks_ = v; } );
        inf->add_checkbox( "anim_cancel", "Infernum R -> Q Cancel",
            inf_anim_cancel_combo_, [this]( bool v ) { inf_anim_cancel_combo_ = v; } );
        inf->add_checkbox( "aa_q_cancel", "Infernum AA -> Q Cancel",
            inf_aa_q_cancel_combo_, [this]( bool v ) { inf_aa_q_cancel_combo_ = v; } );
    }

    auto* sev = combo->add_sub_category( "severum", "Severum" );
    if ( sev != nullptr )
    {
        sev->add_checkbox( "use_q", "Use Q",
            sev_use_q_master_, [this]( bool v ) { sev_use_q_master_ = v; } );
        sev->add_checkbox( "solo_q", "Solo Q",
            sev_solo_q_combo_, [this]( bool v ) { sev_solo_q_combo_ = v; } );
        sev->add_checkbox( "use_r", "Use R",
            sev_use_r_master_, [this]( bool v ) { sev_use_r_master_ = v; } );
        sev->add_slider_int( "r_hc", "R Hitchance",
            HC_MIN, HC_MAX, HC_STEP, sev_r_hitchance_,
            [this]( int v ) { sev_r_hitchance_ = v; } );
        sev->add_checkbox( "cres_swap", "Severum -> Crescendum",
            sev_cres_swap_combo_, [this]( bool v ) { sev_cres_swap_combo_ = v; } );
    }

    auto* cres = combo->add_sub_category( "crescendum", "Crescendum" );
    if ( cres != nullptr )
    {
        cres->add_checkbox( "use_q", "Use Q",
            cres_use_q_master_, [this]( bool v ) { cres_use_q_master_ = v; } );
        cres->add_slider_int( "q_hc", "Hitchance",
            HC_MIN, HC_MAX, HC_STEP, cres_q_hitchance_,
            [this]( int v ) { cres_q_hitchance_ = v; } );
        cres->add_checkbox( "sentry", "Sentry Q",
            cres_sentry_combo_, [this]( bool v ) { cres_sentry_combo_ = v; } );
        cres->add_checkbox( "use_r", "Use R",
            cres_use_r_master_, [this]( bool v ) { cres_use_r_master_ = v; } );
        cres->add_slider_int( "r_hc", "R Hitchance",
            HC_MIN, HC_MAX, HC_STEP, cres_r_hitchance_,
            [this]( int v ) { cres_r_hitchance_ = v; } );
        cres->add_checkbox( "r_aoe", "R AoE",
            cres_r_combo_, [this]( bool v ) { cres_r_combo_ = v; } );
        cres->add_slider_int( "r_min_enemies", "R Min Enemies",
            2, 5, 1, cres_r_min_enemies_,
            [this]( int v ) { cres_r_min_enemies_ = v; } );
    }

    auto* gra = combo->add_sub_category( "gravitum", "Gravitum" );
    if ( gra != nullptr )
    {
        gra->add_checkbox( "use_q", "Use Q",
            gra_use_q_master_, [this]( bool v ) { gra_use_q_master_ = v; } );
        gra->add_checkbox( "be_combo", "Binding Eclipse",
            gra_be_combo_, [this]( bool v ) { gra_be_combo_ = v; } );
        gra->add_checkbox( "be_multi", "Multi Root",
            gra_be_multi_combo_, [this]( bool v ) { gra_be_multi_combo_ = v; } );
        gra->add_slider_int( "be_min_roots", "Min Roots",
            1, 5, 1, gra_be_min_roots_,
            [this]( int v ) { gra_be_min_roots_ = v; } );
        gra->add_checkbox( "use_r", "Use R",
            gra_use_r_master_, [this]( bool v ) { gra_use_r_master_ = v; } );
        gra->add_slider_int( "r_hc", "R Hitchance",
            HC_MIN, HC_MAX, HC_STEP, gra_r_hitchance_,
            [this]( int v ) { gra_r_hitchance_ = v; } );
        gra->add_checkbox( "r_engage", "R Engage",
            gra_r_engage_combo_, [this]( bool v ) { gra_r_engage_combo_ = v; } );
        gra->add_slider_int( "r_min_enemies", "R Min Enemies",
            2, 5, 1, gra_r_min_enemies_,
            [this]( int v ) { gra_r_min_enemies_ = v; } );
        gra->add_slider_int( "r_min_allies", "R Min Allies",
            1, 4, 1, gra_r_min_allies_,
            [this]( int v ) { gra_r_min_allies_ = v; } );
        gra->add_checkbox( "inf_swap", "Gravitum -> Infernum",
            gra_inf_swap_combo_, [this]( bool v ) { gra_inf_swap_combo_ = v; } );
        gra->add_checkbox( "inf_swap_killable", "Gra->Inf Require Killable",
            gra_inf_swap_require_killable_, [this]( bool v ) { gra_inf_swap_require_killable_ = v; } );
    }
}

void aphelios::on_init()
{
    setup_spell( db::SLOT_Q, pred_sdk::spell_type::linear, pred_sdk::targetting_type::edge_to_edge );
    setup_spell( db::SLOT_W, pred_sdk::spell_type::targetted, pred_sdk::targetting_type::center );
    apply_weapon_spell_params( db::weapon::calibrum );

    setup_spell( db::SLOT_R, pred_sdk::spell_type::circular, pred_sdk::targetting_type::edge_to_edge );
    spells_[ db::SLOT_R ].pred.range            = db::R_RANGE;
    spells_[ db::SLOT_R ].pred.radius           = db::R_RADIUS;
    spells_[ db::SLOT_R ].pred.delay            = db::R_DELAY;
    spells_[ db::SLOT_R ].pred.projectile_speed = db::R_SPEED;

    menu_ = g_sdk->menu_manager->add_category( "aphelios", "MeowAIO - Aphelios" );
    if ( menu_ == nullptr ) return;

    setup_combo_menu( menu_ );

    auto* draw_cat = menu_->add_sub_category( "drawing", "Drawing" );
    if ( draw_cat != nullptr )
    {
        draw_cat->add_checkbox( "draw_queue", "Draw Weapon Queue",
            draw_queue_, [this]( bool v ) { draw_queue_ = v; } );
        draw_cat->add_checkbox( "draw_q_range", "Draw Calibrum Q Range",
            draw_q_range_, [this]( bool v ) { draw_q_range_ = v; } );
        draw_cat->add_checkbox( "draw_inf_q_range", "Draw Infernum Q Range",
            draw_inf_q_range_, [this]( bool v ) { draw_inf_q_range_ = v; } );
    }

    auto* debug_cat = menu_->add_sub_category( "debug", "Debug" );
    if ( debug_cat != nullptr )
    {
        debug_cat->add_checkbox( "trace_manual", "Trace Manual Actions",
            trace_manual_, [this]( bool v ) { trace_manual_ = v; } );
        debug_cat->add_checkbox( "trace_wq", "Trace Weapon Queue (1/sec)",
            trace_weapon_queue_, [this]( bool v ) { trace_weapon_queue_ = v; } );
    }
}

void aphelios::on_game_update()
{
    update_player_state();
    update_weapon_state();

    // Cast W once Q Infernum has processed AND its animation ended.
    // is_currently_casting() flickers false between cast_spell_pred(Q)
    // and process_cast(Q); engine drops W silently if issued in that
    // window. Gate on q1_processed_at_ to ensure Q is actually live.
    if ( infernum_step_ == infernum_step::q1_in_flight
      && infernum_q1_processed_at_ > 0.f
      && !is_currently_casting()
      && is_spell_ready( db::SLOT_W )
      && player_ != nullptr )
    {
        player_->cast_spell( db::SLOT_W );
        infernum_step_ = infernum_step::swap_pending;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] in_w_fire t=%.3f via player->cast_spell\n",
                                game_time_ );
        }
    }

    // Safety net for r_pending_: try_finish_qr_combo's timeout only runs
    // when main=Calibrum. If main is anything else and r_pending_ stays
    // armed (e.g. user manually cast R before our auto-fire path), AA
    // would be blocked indefinitely. Force-clear after timeout regardless.
    if ( r_pending_
      && game_time_ - r_pending_started_at_ > db::QR_COMBO_TIMEOUT_SEC )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] r_pending_safety_timeout t=%.3f main=%s\n",
                                game_time_, db::weapon_label( main_weapon_ ) );
        }
        r_pending_       = false;
        q_cast_observed_ = false;
    }

    if ( infernum_step_ != infernum_step::idle
      && game_time_ - infernum_started_at_ > db::INFERNUM_COMBO_TIMEOUT_SEC )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[in_timeout] t=%.3f step=%d\n",
                                game_time_, static_cast< int >( infernum_step_ ) );
        }
        clear_infernum_state();
    }

    // Severum combo: Q finished (rapid-fire done) -> cast W to swap to Crescendum.
    // Severum Q is a sustained ability (~1.5-3s). Wait for process_cast confirmation
    // + minimum 1s elapsed + no active cast = Q ended.
    if ( severum_step_ == severum_step::q_active
      && severum_q_processed_at_ > 0.f
      && game_time_ - severum_q_processed_at_ > 1.0f
      && !is_currently_casting()
      && is_spell_ready( db::SLOT_W )
      && player_ != nullptr )
    {
        player_->cast_spell( db::SLOT_W );
        severum_step_ = severum_step::swap_pending;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] sev_w_fire t=%.3f via player->cast_spell\n",
                                game_time_ );
        }
    }

    if ( severum_step_ != severum_step::idle
      && game_time_ - severum_started_at_ > db::SEVERUM_COMBO_TIMEOUT_SEC )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[sev_timeout] t=%.3f step=%d\n",
                                game_time_, static_cast< int >( severum_step_ ) );
        }
        clear_severum_state();
    }

    // Inf-Gra swap: Q-Inf processed + enough Gra marks accumulated → W swap.
    if ( inf_gra_step_ == inf_gra_step::q_in_flight
      && inf_gra_q_processed_at_ > 0.f
      && !is_currently_casting()
      && is_spell_ready( db::SLOT_W )
      && player_ != nullptr
      && count_rootable_gra_marked() >= inf_gra_swap_min_marks_ )
    {
        player_->cast_spell( db::SLOT_W );
        inf_gra_step_ = inf_gra_step::swap_pending;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] inf_gra_w_fire t=%.3f marks=%d need=%d\n",
                                game_time_,
                                count_rootable_gra_marked(),
                                inf_gra_swap_min_marks_ );
        }
    }

    // Inf-Gra timeout: abort if combo hasn't completed in time.
    if ( inf_gra_step_ != inf_gra_step::idle
      && game_time_ - inf_gra_started_at_ > db::INFERNUM_COMBO_TIMEOUT_SEC )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[inf_gra_timeout] t=%.3f step=%d\n",
                                game_time_, static_cast< int >( inf_gra_step_ ) );
        }
        clear_inf_gra_state();
    }

    // Gra-Inf swap: BE processed + W ready + not casting → fire W.
    if ( gra_inf_step_ == gra_inf_step::be_in_flight
      && gra_inf_be_processed_at_ > 0.f
      && !is_currently_casting()
      && is_spell_ready( db::SLOT_W )
      && player_ != nullptr )
    {
        player_->cast_spell( db::SLOT_W );
        gra_inf_step_ = gra_inf_step::swap_pending;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_inf_w_fire t=%.3f\n", game_time_ );
        }
    }

    // Gra-Inf timeout.
    if ( gra_inf_step_ != gra_inf_step::idle
      && game_time_ - gra_inf_started_at_ > db::INFERNUM_COMBO_TIMEOUT_SEC )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[gra_inf_timeout] t=%.3f step=%d\n",
                                game_time_, static_cast< int >( gra_inf_step_ ) );
        }
        clear_gra_inf_state();
    }

    maybe_force_calibrum_bonus_aa();
    tick_post_r_kick_assist();

    if ( trace_weapon_queue_ && game_time_ - trace_wq_last_at_ >= 1.0f )
    {
        trace_wq_last_at_ = game_time_;
        g_sdk->log_console( "[wq] t=%.3f main=%s off=%s ammo=%d next=%s queue=[%s,%s,%s]\n",
                            game_time_,
                            db::weapon_label( main_weapon_ ),
                            db::weapon_label( offhand_weapon_ ),
                            main_ammo_,
                            queue_initialized_ ? db::weapon_label( weapon_queue_[ 0 ] ) : "?",
                            queue_initialized_ ? db::weapon_label( weapon_queue_[ 0 ] ) : "?",
                            queue_initialized_ ? db::weapon_label( weapon_queue_[ 1 ] ) : "?",
                            queue_initialized_ ? db::weapon_label( weapon_queue_[ 2 ] ) : "?" );
    }

    if ( !chakram_out_ )        return;
    if ( chakram_net_id_ == 0u ) return;

    auto* missile = g_sdk->object_manager->get_object_by_network_id( chakram_net_id_ );
    if ( missile == nullptr || !missile->is_valid() )
    {
        clear_chakram_state();
    }
}

bool aphelios::on_before_attack( orb_sdk::event_data* /*data*/ )
{
    const bool chakram_block = is_chakram_in_flight();
    const bool r_block       = r_pending_;

    if ( chakram_block )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] aa_blocked CHAKRAM t=%.3f main=%s clear_at=%.3f remaining=%.3fs\n",
                                game_time_,
                                db::weapon_label( main_weapon_ ),
                                chakram_clear_at_,
                                chakram_clear_at_ - game_time_ );
        }
        return false;
    }
    if ( r_block )
    {
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] aa_blocked R_PENDING t=%.3f age=%.3fs main=%s q_obs=%d\n",
                                game_time_,
                                game_time_ - r_pending_started_at_,
                                db::weapon_label( main_weapon_ ),
                                static_cast< int >( q_cast_observed_ ) );
        }
        return false;
    }
    return true;
}

void aphelios::on_create_missile( game_object* missile )
{
    if ( player_ == nullptr )   return;
    if ( missile == nullptr )   return;
    if ( !missile->is_valid() ) return;
    if ( missile->get_team_id() != player_->get_team_id() ) return;

    const std::string name = missile->get_name();
    const std::string_view name_view{ name };

    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] create_missile t=%.3f name=%s\n",
                            game_time_, name.c_str() );
    }

    // AA + Q anim cancel: Infernum AA missile just spawned → AA damage
    // is committed. Cast Q-Inf now to cancel the AA recovery animation
    // for the tightest possible AA → Duskwave chain. Anim cancel paths
    // via on_process_cast / on_after_attack fired too early (before AA
    // missile spawn) and would cancel the AA itself; this hook waits
    // for the missile to confirm the AA is locked in.
    if ( db::is_infernum_aa_missile( name_view ) )
    {
        try_inf_aa_q_anim_cancel();
    }

    // Q missile spawning = engine accepted Q + wind-up done. Fire R now.
    if ( r_pending_ && db::is_calibrum_q_missile( name_view ) )
    {
        auto* target = get_hero_target();
        if ( target == nullptr && infernum_target_id_ != 0u )
        {
            target = g_sdk->object_manager->get_object_by_network_id( infernum_target_id_ );
            if ( target != nullptr && !target->is_valid() ) target = nullptr;
        }
        if ( target != nullptr && is_spell_ready( db::SLOT_R ) )
        {
            const bool ok = cast_spell_pred( db::SLOT_R, target, cal_r_hc() );
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] qr_r_on_q_missile t=%.3f result=%d\n",
                                    game_time_, static_cast< int >( ok ) );
            }
            if ( ok )
            {
                r_pending_       = false;
                q_cast_observed_ = false;
            }
        }
    }

    // R missile spawn: post-R force-AA pattern is ONLY meaningful in
    // Calibrum/Infernum QR contexts (consume marks). With Crescendum or
    // Severum or Gravitum as main, R has its own mechanics (e.g. Cres+R
    // spawns mini-orbit chakrams), and issuing attack_unit orders here
    // confuses the orbwalker — it interprets our order as a user override
    // and stalls. Gate strictly.
    if ( db::is_r_missile( name_view ) )
    {
        const bool qr_context = ( main_weapon_ == db::weapon::calibrum )
                             || ( infernum_step_ != infernum_step::idle );

        if ( qr_context )
        {
            qr_force_aa_until_ = game_time_ + 2.0f;

            if ( is_orbwalker_mode_active() && sdk::common != nullptr )
            {
                auto* target = get_hero_target();
                if ( target != nullptr )
                {
                    sdk::common->issue_order( game_object_order::attack_unit, target );
                    if ( trace_manual_ )
                    {
                        g_sdk->log_console( "[trace] qr_force_aa_on_r_missile t=%.3f\n", game_time_ );
                    }
                }
            }
        }
        else if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] r_missile_skip_force_aa t=%.3f main=%s (non-QR context)\n",
                                game_time_, db::weapon_label( main_weapon_ ) );
        }

        // Post-R kick window: R with Crescendum main spawns lots of
        // missiles (OffHandOrbit/MisInMini) that pollute the orbwalker's
        // internal AA tracker, stalling AAs for several seconds. Arm a
        // window where on_game_update will periodically force-attack via
        // sdk::orbwalker->attack() to bypass the stall.
        if ( main_weapon_ == db::weapon::crescendum )
        {
            post_r_kick_until_       = game_time_ + 2.0f;
            post_r_kick_last_at_     = 0.f;
            post_r_kick_fail_streak_ = 0;
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] post_r_kick_armed t=%.3f until=%.3f\n",
                                    game_time_, post_r_kick_until_ );
            }
        }
    }

    // Infernum Q missile spawn marks the "ready for W" check. Actual W
    // cast is polled from on_game_update once !is_currently_casting() so
    // engine doesn't drop it during Q's post-cast animation.

    // Calibrum Q missile spawn during infernum combo: arm force-AA window
    // for the Q-Cal hit mark.
    if ( db::is_calibrum_q_missile( name_view )
      && infernum_step_ == infernum_step::q2_in_flight )
    {
        infernum_force_aa_until_ = game_time_ + 2.0f;

        if ( is_orbwalker_mode_active() && sdk::common != nullptr )
        {
            auto* target = get_hero_target();
            if ( target != nullptr )
            {
                sdk::common->issue_order( game_object_order::attack_unit, target );
                if ( trace_manual_ )
                {
                    g_sdk->log_console( "[trace] in_force_aa_on_q2_missile t=%.3f\n", game_time_ );
                }
            }
        }
    }

    if ( db::is_chakram_out_missile( name_view ) )
    {
        chakram_out_       = true;
        chakram_returning_ = false;
        chakram_net_id_    = missile->get_network_id();

        // Set timer-based AA gate. Robust to weapon swaps and missed
        // tracking events: even if we never observe the IN missile,
        // chakram_clear_at_ expires on its own.
        const math::vector3 start_pos = missile->get_missile_start_pos();
        const math::vector3 end_pos   = missile->get_missile_end_pos();
        const float dist  = start_pos.distance( end_pos );
        const float ping  = ( g_sdk->net_client != nullptr )
                            ? static_cast< float >( g_sdk->net_client->get_ping() ) / 1000.f
                            : 0.f;
        const float aa_delay = ( player_ != nullptr ) ? player_->get_attack_delay() : 0.f;
        const float cycle_time = ( dist / db::CRESCENDUM_MISSILE_SPEED ) * 2.f
                               + ping
                               + db::CRESCENDUM_FRAME_MARGIN
                               + aa_delay;
        chakram_clear_at_ = game_time_ + cycle_time;

        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] chakram_out t=%.3f dist=%.0f cycle=%.3fs clear_at=%.3f\n",
                                game_time_, dist, cycle_time, chakram_clear_at_ );
        }
        return;
    }
    if ( db::is_chakram_in_missile( name_view ) )
    {
        chakram_out_       = true;
        chakram_returning_ = true;
        chakram_net_id_    = missile->get_network_id();
    }
}

void aphelios::on_delete_object( game_object* object )
{
    if ( object == nullptr )             return;

    const uint32_t object_net_id = object->get_network_id();
    if ( !chakram_returning_ )                   return;
    if ( object_net_id != chakram_net_id_ )      return;

    clear_chakram_state();

    // Early-terminate the timer-based gate: chakram physically returned,
    // free to AA immediately (no need to wait out the conservative cycle
    // estimate set when OUT spawned).
    chakram_clear_at_ = 0.f;

    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] chakram_in_returned t=%.3f (timer cleared)\n", game_time_ );
    }

    if ( !is_orbwalker_mode_active() )   return;

    auto* target = get_hero_target();
    if ( target == nullptr )             return;

    sdk::orbwalker->attack( target );
}

void aphelios::on_stop_cast( game_object* caster, spell_cast* cast, bool was_attack_processed )
{
    if ( caster != player_ ) return;
    if ( cast == nullptr )   return;

    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] stop_cast t=%.3f slot=%s is_aa=%d was_processed=%d\n",
                            game_time_,
                            slot_label( cast->get_spell_slot() ),
                            static_cast< int >( cast->is_basic_attack() ),
                            static_cast< int >( was_attack_processed ) );
    }

    if ( was_attack_processed )                       return;
    if ( !cast->is_basic_attack() )                   return;
    if ( main_weapon_ != db::weapon::crescendum )     return;

    clear_chakram_state();
}

void aphelios::on_process_cast( game_object* caster, spell_cast* cast )
{
    if ( caster != player_ ) return;
    if ( cast == nullptr )   return;

    const int slot = cast->get_spell_slot();

    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] process_cast t=%.3f slot=%s is_aa=%d\n",
                            game_time_,
                            slot_label( slot ),
                            static_cast< int >( cast->is_basic_attack() ) );
    }

    if ( slot == db::SLOT_Q
      && infernum_step_ == infernum_step::q1_in_flight
      && main_weapon_ == db::weapon::infernum )
    {
        infernum_q1_processed_at_ = game_time_;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] in_q1_processed t=%.3f\n", game_time_ );
        }
    }

    if ( slot == db::SLOT_Q
      && inf_gra_step_ == inf_gra_step::q_in_flight
      && main_weapon_ == db::weapon::infernum )
    {
        inf_gra_q_processed_at_ = game_time_;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] inf_gra_q_processed t=%.3f\n", game_time_ );
        }
    }

    if ( slot == db::SLOT_Q
      && gra_inf_step_ == gra_inf_step::be_in_flight
      && main_weapon_ == db::weapon::gravitum )
    {
        gra_inf_be_processed_at_ = game_time_;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_inf_be_processed t=%.3f\n", game_time_ );
        }
    }

    if ( slot == db::SLOT_Q
      && severum_step_ == severum_step::q_active
      && main_weapon_ == db::weapon::severum )
    {
        severum_q_processed_at_ = game_time_;
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] sev_q_processed t=%.3f\n", game_time_ );
        }
    }


    if ( !r_pending_ )         return;
    if ( slot != db::SLOT_Q )  return;

    q_cast_observed_ = true;
}

bool aphelios::is_orbwalker_mode_active() const
{
    if ( sdk::orbwalker == nullptr ) return false;
    return sdk::orbwalker->combo()
        || sdk::orbwalker->harass()
        || sdk::orbwalker->clear()
        || sdk::orbwalker->fast_clear();
}

void aphelios::update_weapon_state()
{
    if ( player_ == nullptr ) return;

    const db::weapon prev_main    = main_weapon_;
    const db::weapon prev_offhand = offhand_weapon_;

    const db::offhand_state off = db::detect_offhand_state( player_ );

    main_weapon_    = db::detect_main_weapon( player_ );
    offhand_weapon_ = off.w;
    main_ammo_      = read_charges( db::SLOT_Q );
    offhand_ammo_   = off.ammo;

    // First detection — init queue.
    if ( !queue_initialized_
      && main_weapon_ != db::weapon::none )
    {
        init_weapon_queue();
        if ( trace_weapon_queue_ )
        {
            g_sdk->log_console( "[wq] init t=%.3f queue=[%s, %s, %s]\n",
                                game_time_,
                                db::weapon_label( weapon_queue_[ 0 ] ),
                                db::weapon_label( weapon_queue_[ 1 ] ),
                                db::weapon_label( weapon_queue_[ 2 ] ) );
        }
    }

    if ( main_weapon_ != prev_main )
    {
        clear_chakram_state();
        r_pending_       = false;
        q_cast_observed_ = false;

        apply_weapon_spell_params( main_weapon_ );

        // Queue tracking: distinguish W swap from depletion.
        if ( queue_initialized_ && prev_main != db::weapon::none )
        {
            // W swap = new main was the old offhand. Depletion = new main
            // came from queue (never the old offhand). Single check is
            // robust against the 1-frame offhand=none flicker during swaps.
            const bool is_w_swap = ( main_weapon_ == prev_offhand );

            if ( !is_w_swap )
            {
                // Depletion: prev_main ran out. Queue front became new main.
                // Shift queue left, push depleted to back.
                weapon_queue_[ 0 ] = weapon_queue_[ 1 ];
                weapon_queue_[ 1 ] = weapon_queue_[ 2 ];
                weapon_queue_[ 2 ] = prev_main;

                if ( trace_weapon_queue_ )
                {
                    g_sdk->log_console( "[wq] deplete t=%.3f %s -> queue=[%s, %s, %s]\n",
                                        game_time_,
                                        db::weapon_label( prev_main ),
                                        db::weapon_label( weapon_queue_[ 0 ] ),
                                        db::weapon_label( weapon_queue_[ 1 ] ),
                                        db::weapon_label( weapon_queue_[ 2 ] ) );
                }
            }
            else if ( trace_weapon_queue_ )
            {
                g_sdk->log_console( "[wq] w_swap t=%.3f queue=[%s, %s, %s] (unchanged)\n",
                                    game_time_,
                                    db::weapon_label( weapon_queue_[ 0 ] ),
                                    db::weapon_label( weapon_queue_[ 1 ] ),
                                    db::weapon_label( weapon_queue_[ 2 ] ) );
            }
        }

        if ( infernum_step_ == infernum_step::swap_pending
          && prev_main      == db::weapon::infernum
          && main_weapon_   == db::weapon::calibrum )
        {
            infernum_step_           = infernum_step::consume_q1;
            infernum_force_aa_until_ = game_time_ + 2.0f;
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] in_swap_done t=%.3f -> consume_q1\n", game_time_ );
            }
        }
        else if ( infernum_step_ != infernum_step::idle )
        {
            clear_infernum_state();
        }

        if ( severum_step_ == severum_step::swap_pending
          && prev_main     == db::weapon::severum
          && main_weapon_  == db::weapon::crescendum )
        {
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] sev_swap_done t=%.3f -> idle (Crescendum active)\n",
                                    game_time_ );
            }
            clear_severum_state();
        }
        else if ( severum_step_ != severum_step::idle
               && main_weapon_ != db::weapon::severum )
        {
            // Unexpected weapon change during combo — abort.
            clear_severum_state();
        }

        // Inf-Gra swap landed: Inf -> Gra. Transition to be_pending so
        // combo() finalizes with BE on accumulated Gra marks.
        if ( inf_gra_step_ == inf_gra_step::swap_pending
          && prev_main      == db::weapon::infernum
          && main_weapon_   == db::weapon::gravitum )
        {
            inf_gra_step_ = inf_gra_step::be_pending;
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] inf_gra_swap_done t=%.3f -> be_pending\n",
                                    game_time_ );
            }
        }
        else if ( inf_gra_step_ != inf_gra_step::idle
               && main_weapon_ != db::weapon::infernum
               && main_weapon_ != db::weapon::gravitum )
        {
            // Unexpected weapon change mid Inf->Gra combo (e.g. depletion).
            clear_inf_gra_state();
        }

        // Gra-Inf swap landed: Gra -> Inf. Transition to duskwave_pending
        // so combo() finalizes with Duskwave on the rooted target.
        if ( gra_inf_step_ == gra_inf_step::swap_pending
          && prev_main      == db::weapon::gravitum
          && main_weapon_   == db::weapon::infernum )
        {
            gra_inf_step_ = gra_inf_step::duskwave_pending;
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] gra_inf_swap_done t=%.3f -> duskwave_pending\n",
                                    game_time_ );
            }
        }
        else if ( gra_inf_step_ != gra_inf_step::idle
               && main_weapon_ != db::weapon::gravitum
               && main_weapon_ != db::weapon::infernum )
        {
            clear_gra_inf_state();
        }
    }
}

void aphelios::apply_weapon_spell_params( db::weapon w )
{
    auto& q = spells_[ db::SLOT_Q ].pred;
    switch ( w )
    {
        case db::weapon::calibrum:
        {
            q.spell_type           = pred_sdk::spell_type::linear;
            q.targetting_type      = pred_sdk::targetting_type::edge_to_edge;
            q.range                = db::CALIBRUM_Q_RANGE;
            q.radius               = db::CALIBRUM_Q_WIDTH;
            q.delay                = db::CALIBRUM_Q_DELAY;
            q.projectile_speed     = db::CALIBRUM_Q_SPEED;
            q.forbidden_collisions = {
                pred_sdk::collision_type::unit,
                pred_sdk::collision_type::hero,
            };
            break;
        }
        case db::weapon::infernum:
        {
            q.spell_type           = pred_sdk::spell_type::linear;
            q.targetting_type      = pred_sdk::targetting_type::edge_to_edge;
            q.range                = db::INFERNUM_Q_RANGE;
            q.radius               = db::INFERNUM_Q_RADIUS;
            q.delay                = db::INFERNUM_Q_DELAY;
            q.projectile_speed     = db::INFERNUM_Q_SPEED;
            q.forbidden_collisions = {};
            break;
        }
        default:
        {
            break;
        }
    }
}

int aphelios::read_charges( int slot ) const
{
    if ( player_ == nullptr ) return 0;
    auto* spell = player_->get_spell( slot );
    if ( spell == nullptr ) return 0;
    return static_cast< int >( spell->get_charges() );
}

static constexpr db::weapon WEAPON_CYCLE[] =
{
    db::weapon::calibrum,
    db::weapon::severum,
    db::weapon::gravitum,
    db::weapon::infernum,
    db::weapon::crescendum,
};
static constexpr int WEAPON_CYCLE_LEN = 5;

static int cycle_index_of( db::weapon w ) noexcept
{
    for ( int i = 0; i < WEAPON_CYCLE_LEN; ++i )
    {
        if ( WEAPON_CYCLE[ i ] == w ) return i;
    }
    return -1;
}

void aphelios::init_weapon_queue()
{
    // Best-effort: walk the fixed cycle starting after main, skip offhand.
    // Correct at game start. Self-corrects after first observed depletion.
    const int main_idx = cycle_index_of( main_weapon_ );
    if ( main_idx < 0 ) return;

    size_t q = 0u;
    for ( int step = 1; step < WEAPON_CYCLE_LEN && q < WEAPON_QUEUE_SIZE; ++step )
    {
        const db::weapon w = WEAPON_CYCLE[ ( main_idx + step ) % WEAPON_CYCLE_LEN ];
        if ( w == offhand_weapon_ ) continue;
        weapon_queue_[ q++ ] = w;
    }
    queue_initialized_ = true;
}

db::weapon aphelios::next_weapon() const
{
    if ( !queue_initialized_ ) return db::weapon::none;
    return weapon_queue_[ 0 ];
}

void aphelios::on_draw()
{
    if ( player_ == nullptr || player_->is_dead() ) return;
    if ( sdk::shaders == nullptr && g_sdk->renderer == nullptr ) return;

    auto* r = g_sdk->renderer;

    if ( draw_q_range_ && main_weapon_ == db::weapon::calibrum )
    {
        draw_world_circle( pos_, db::CALIBRUM_Q_RANGE, 0xFF22CCFFu );
    }

    if ( draw_inf_q_range_ && main_weapon_ == db::weapon::infernum )
    {
        draw_world_circle( pos_, db::INFERNUM_Q_RANGE, 0xFFFF8800u );
    }

    if ( r != nullptr && draw_queue_ )
    {
        const auto screen = r->world_to_screen( pos_ );
        float y = screen.y + 42.f;
        const float x = screen.x - 70.f;

        // Line 1: Main [ammo] | Off [ammo]
        {
            char buf[ HUD_BUF_SIZE ];
            std::snprintf( buf, sizeof( buf ), "%s [%d] | %s [%d]",
                           db::weapon_label( main_weapon_ ), main_ammo_,
                           db::weapon_label( offhand_weapon_ ), offhand_ammo_ );
            r->add_text( buf, HUD_TEXT_SMALL, { x, y }, 0, weapon_color( main_weapon_ ) );
        }
        y += HUD_LINE_H;

        // Line 2: Next weapon.
        {
            char buf[ HUD_BUF_SIZE ];
            const db::weapon next = queue_initialized_ ? weapon_queue_[ 0 ] : db::weapon::none;
            std::snprintf( buf, sizeof( buf ), "Next: %s",
                           queue_initialized_ ? db::weapon_label( next ) : "Detecting" );
            r->add_text( buf, HUD_TEXT_SMALL, { x, y }, 0,
                         queue_initialized_ ? weapon_color( next ) : HUD_LABEL_COLOR );
        }
        y += HUD_LINE_H;

        // Line 3: Queue [1] -> [2].
        {
            char buf[ HUD_BUF_SIZE ];
            if ( queue_initialized_ )
            {
                std::snprintf( buf, sizeof( buf ), "%s > %s",
                               db::weapon_label( weapon_queue_[ 1 ] ),
                               db::weapon_label( weapon_queue_[ 2 ] ) );
            }
            else
            {
                std::snprintf( buf, sizeof( buf ), "%s", "Waiting..." );
            }
            r->add_text( buf, HUD_TEXT_SMALL, { x, y }, 0, HUD_LABEL_COLOR );
        }
    }
}

void aphelios::combo()
{
    if ( player_ == nullptr ) return;

    // Active state machines own combo() while in mid-flight phases.
    // be_pending / duskwave_pending finalize states are handled in the
    // per-weapon sections below.
    if ( infernum_step_ != infernum_step::idle ) return;
    if ( severum_step_  != severum_step::idle )  return;
    if ( inf_gra_step_ == inf_gra_step::q_in_flight
      || inf_gra_step_ == inf_gra_step::swap_pending ) return;
    if ( gra_inf_step_ == gra_inf_step::be_in_flight
      || gra_inf_step_ == gra_inf_step::swap_pending ) return;

    // Inf-Cal swap combo runs through on_after_attack — defer.
    if ( main_weapon_    == db::weapon::infernum
      && offhand_weapon_ == db::weapon::calibrum
      && inf_cal_swap_combo_ )
    {
        return;
    }
    // Inf-Gra cross-swap entry is also on_after_attack — defer.
    if ( main_weapon_    == db::weapon::infernum
      && offhand_weapon_ == db::weapon::gravitum
      && inf_gra_swap_combo_ )
    {
        return;
    }
    if ( main_weapon_    == db::weapon::severum
      && offhand_weapon_ == db::weapon::crescendum )
    {
        return;
    }

    // Crescendum main: R generates chakrams for sustained DPS, Sentry
    // deploys turret near target. Orbwalker handles chakram AAs.
    if ( main_weapon_ == db::weapon::crescendum )
    {
        if ( try_crescendum_r() )      return;
        if ( try_crescendum_sentry() ) return;
        return;
    }

    // Gravitum main: R engage (teamfight setup) takes priority over BE.
    // R applies Gra mark via slow → next combo() tick BE roots all marked.
    if ( main_weapon_ == db::weapon::gravitum )
    {
        // Inf-Gra swap landed (be_pending) → finalize: BE on any rootable
        // Gra-marked target, bypass the normal multi-root threshold (the
        // swap was committed precisely to set up this BE).
        if ( inf_gra_step_ == inf_gra_step::be_pending )
        {
            auto* be_target = pick_any_gra_marked_target();
            if ( be_target != nullptr && is_spell_ready( db::SLOT_Q ) )
            {
                const bool ok = cast_spell_target( db::SLOT_Q, be_target );
                if ( trace_manual_ )
                {
                    g_sdk->log_console( "[trace] inf_gra_be_finalize t=%.3f result=%d\n",
                                        game_time_, static_cast< int >( ok ) );
                }
            }
            clear_inf_gra_state();
            return;
        }

        // Gra-Inf cross-swap entry (Gra main + Inf offhand, target Gra-marked).
        // Try BE → swap → Duskwave chain before normal Gra logic.
        if ( offhand_weapon_ == db::weapon::infernum
          && gra_inf_swap_combo_
          && gra_inf_step_ == gra_inf_step::idle )
        {
            if ( try_gra_inf_be() ) return;
        }

        if ( try_gravitum_r() ) return;

        auto* target = get_hero_target();
        if ( target != nullptr )
        {
            try_gravitum_be( target );
        }
        return;
    }

    // Infernum main (non-swap pairings or swap disabled). R for AoE
    // damage (priority), then Duskwave solo if killable / dump-ammo.
    if ( main_weapon_ == db::weapon::infernum )
    {
        // Gra-Inf swap landed (duskwave_pending) → finalize: Duskwave on
        // the rooted target. Tries the original Gra-marked target first;
        // falls back to hero target if original is gone.
        if ( gra_inf_step_ == gra_inf_step::duskwave_pending )
        {
            game_object* dw_target = nullptr;
            if ( gra_inf_target_id_ != 0u )
            {
                dw_target = g_sdk->object_manager->get_object_by_network_id( gra_inf_target_id_ );
                if ( dw_target != nullptr && !dw_target->is_valid() ) dw_target = nullptr;
            }
            if ( dw_target == nullptr ) dw_target = get_hero_target();

            if ( dw_target != nullptr
              && is_spell_ready( db::SLOT_Q )
              && pos_.distance( dw_target->get_position() ) <= db::INFERNUM_Q_RANGE )
            {
                const bool ok = cast_spell_pred( db::SLOT_Q, dw_target, inf_q_hc() );
                if ( trace_manual_ )
                {
                    g_sdk->log_console( "[trace] gra_inf_duskwave_finalize t=%.3f result=%d\n",
                                        game_time_, static_cast< int >( ok ) );
                }
            }
            clear_gra_inf_state();
            return;
        }

        if ( try_infernum_r() ) return;

        auto* target = get_hero_target();
        if ( target != nullptr )
        {
            try_infernum_duskwave_solo( target );
        }
        return;
    }

    if ( main_weapon_ != db::weapon::calibrum ) return;

    if ( try_finish_qr_combo() ) return;
    if ( r_pending_ )            return;

    auto* target = get_hero_target();
    if ( target == nullptr ) return;

    if ( try_qr_combo( target ) )            return;
    if ( try_calibrum_r_standalone( target ) ) return;

    const float dist = pos_.distance( target->get_position() );
    if ( dist <= db::CALIBRUM_AA_RANGE ) return;

    try_calibrum_q( target );
}

void aphelios::on_after_attack( orb_sdk::event_data* /*data*/ )
{
    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] after_attack t=%.3f in_step=%d\n",
                            game_time_, static_cast< int >( infernum_step_ ) );
    }

    // AA fired during the post-R window: re-arm instead of clear. The
    // orbwalker re-stalls per-AA because each Crescendum AA post-R spawns
    // another batch of OffHandOrbit/MisInMini missiles that pollute the
    // tracker. Keep kicking through the entire R effect duration — window
    // naturally expires after 3s of no AAs (orbwalker recovered).
    if ( post_r_kick_until_ > 0.f
      && main_weapon_ == db::weapon::crescendum )
    {
        post_r_kick_until_       = game_time_ + 3.0f;
        post_r_kick_fail_streak_ = 0;  // reset, AA fired = progress
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] post_r_kick_rearmed t=%.3f until=%.3f\n",
                                game_time_, post_r_kick_until_ );
        }
    }

    if ( player_ == nullptr ) return;
    if ( r_pending_ )         return;

    // Swap combos own AA-driven actions while running.
    if ( infernum_step_ != infernum_step::idle ) return;
    if ( severum_step_  != severum_step::idle )  return;

    // Infernum combo entry: weave Q Inf after first orbwalker AA.
    if ( main_weapon_    == db::weapon::infernum
      && offhand_weapon_ == db::weapon::calibrum
      && inf_cal_swap_combo_
      && is_orbwalker_mode_active() )
    {
        try_infernum_q1();
        return;
    }

    // Inf-Gra cross-swap entry: Duskwave applies Gra marks via offhand
    // → wait for marks → W swap → BE roots.
    if ( main_weapon_    == db::weapon::infernum
      && offhand_weapon_ == db::weapon::gravitum
      && inf_gra_swap_combo_
      && is_orbwalker_mode_active()
      && inf_gra_step_ == inf_gra_step::idle )
    {
        try_inf_gra_q1();
        return;
    }


    // Severum combo entry: prefer Crescendum-swap path (builds chakram stacks).
    // Without Crescendum offhand, fall back to solo Q only on kill or
    // dump-ammo exception (gated inside try_severum_solo_q).
    if ( main_weapon_ == db::weapon::severum
      && is_orbwalker_mode_active() )
    {
        if ( offhand_weapon_ == db::weapon::crescendum )
        {
            try_severum_swap_q();
        }
        else
        {
            try_severum_solo_q();
        }
        return;
    }

    if ( main_weapon_ != db::weapon::calibrum )  return;
    if ( !is_orbwalker_mode_active() )           return;

    auto* target = get_hero_target();
    if ( target == nullptr ) return;

    try_calibrum_q( target );
}

void aphelios::on_buff_gain( game_object* target, buff_instance* buff )
{
    if ( target == nullptr || buff == nullptr ) return;

    const uint32_t hash = buff->get_hash();

    // Gravitum mark: track for BE targeting.
    if ( hash == db::BUFF_GRAVITUM_DEBUFF )
    {
        const uint32_t net_id = target->get_network_id();
        gra_mark_add( net_id );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_mark_gain t=%.3f target_id=%u\n",
                                game_time_, net_id );
        }
        return;
    }

    if ( hash != db::BUFF_CALIBRUM_MARK_DEBUFF ) return;

    const uint32_t net_id = target->get_network_id();

    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] mark_gain  t=%.3f target_id=%u\n",
                            game_time_, net_id );
    }

    mark_add( net_id );

    // QR combo: mark inside post-R window forces consume AA.
    if ( game_time_ < qr_force_aa_until_
      && sdk::common != nullptr
      && is_orbwalker_mode_active() )
    {
        sdk::common->issue_order( game_object_order::attack_unit, target );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] qr_force_aa_on_mark t=%.3f\n", game_time_ );
        }
    }
    // General attack-reset abuse: mark-consume AAs bypass normal AA
    // cooldown. When Calibrum is main, target is an enemy hero, orbwalker
    // is in combat mode, AA is ready and we're not mid-cast, force the
    // consume AA immediately to squeeze an extra hit between regular AAs.
    // Skip if QR window is active (already handled above).
    else if ( main_weapon_ == db::weapon::calibrum
           && sdk::common != nullptr
           && sdk::orbwalker != nullptr
           && is_orbwalker_mode_active()
           && sdk::orbwalker->is_attack_ready_in( 0.f )
           && !is_currently_casting()
           && !would_cancel_attack()
           && target->is_hero() )
    {
        sdk::common->issue_order( game_object_order::attack_unit, target );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] cal_attack_reset_on_mark t=%.3f target=%u\n",
                                game_time_, net_id );
        }
    }

    // Infernum combo: mark inside post-swap window OR post-Q2 window
    // triggers the consume AA. Same dual-trigger pattern as QR.
    if ( net_id == infernum_target_id_
      && game_time_ < infernum_force_aa_until_
      && sdk::common != nullptr
      && is_orbwalker_mode_active() )
    {
        sdk::common->issue_order( game_object_order::attack_unit, target );

        if ( infernum_step_ == infernum_step::consume_q1 )
        {
            infernum_step_ = infernum_step::waiting_q2_cast;
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] in_force_aa_on_q1_mark t=%.3f -> waiting_q2_cast\n",
                                    game_time_ );
            }
        }
        else if ( infernum_step_ == infernum_step::q2_in_flight )
        {
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] in_force_aa_on_q2_mark t=%.3f -> idle\n", game_time_ );
            }
            clear_infernum_state();
        }
    }
}

void aphelios::on_buff_loss( game_object* target, buff_instance* buff )
{
    if ( target == nullptr || buff == nullptr ) return;

    const uint32_t hash = buff->get_hash();

    // Gravitum mark expired/consumed.
    if ( hash == db::BUFF_GRAVITUM_DEBUFF )
    {
        const uint32_t net_id = target->get_network_id();
        gra_mark_remove( net_id );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] gra_mark_loss t=%.3f target_id=%u\n",
                                game_time_, net_id );
        }
        return;
    }

    if ( hash != db::BUFF_CALIBRUM_MARK_DEBUFF ) return;

    const uint32_t net_id = target->get_network_id();

    if ( trace_manual_ )
    {
        g_sdk->log_console( "[trace] mark_loss  t=%.3f target_id=%u\n",
                            game_time_, net_id );
    }

    mark_remove( net_id );

    // Infernum combo: AA #2 consumed Q-Inf mark -> cast Q-Cal now (always).
    // Base combo completes naturally: Q-Cal hits, places mark, force-AA via
    // mark gain fires AA#3 which consumes the Q-Cal mark.
    //
    // R extension is conditional on the full chain (Q-Cal + R + AA + AA)
    // killing the target. If yes, arm R now — Q-Cal missile spawn will
    // trigger R via the r_pending_ flow. If no, base combo finishes
    // gracefully at Q-Cal + AA#3, R saved for next time.
    if ( net_id == infernum_target_id_
      && infernum_step_ == infernum_step::waiting_q2_cast
      && main_weapon_ == db::weapon::calibrum
      && cal_use_q_master_ )
    {
        const bool ok = cast_spell_pred( db::SLOT_Q, target, cal_q_hc() );
        if ( trace_manual_ )
        {
            g_sdk->log_console( "[trace] in_q2_fire t=%.3f result=%d\n",
                                game_time_, static_cast< int >( ok ) );
        }
        if ( !ok ) return;

        infernum_step_ = infernum_step::q2_in_flight;

        if ( cal_use_r_master_
          && is_spell_ready( db::SLOT_R )
          && killable_with_qr_aa2( target ) )
        {
            r_pending_            = true;
            r_pending_started_at_ = game_time_;
            q_cast_observed_      = true;
            if ( trace_manual_ )
            {
                g_sdk->log_console( "[trace] in_chain_qr t=%.3f (full extension)\n",
                                    game_time_ );
            }
        }
        else if ( trace_manual_ )
        {
            const char* reason =
                !cal_use_r_master_                  ? "r_master_off" :
                !is_spell_ready( db::SLOT_R )       ? "r_not_ready"  :
                                                      "wont_kill";
            g_sdk->log_console( "[trace] in_chain_qr_skip t=%.3f reason=%s (base combo continues)\n",
                                game_time_, reason );
        }
    }
}

void aphelios::mark_add( uint32_t net_id )
{
    if ( marked_count_ >= marked_targets_.size() ) return;

    for ( size_t index = 0u; index < marked_count_; ++index )
    {
        if ( marked_targets_[ index ] == net_id ) return;
    }

    marked_targets_[ marked_count_ ] = net_id;
    ++marked_count_;
}

void aphelios::mark_remove( uint32_t net_id )
{
    for ( size_t index = 0u; index < marked_count_; ++index )
    {
        if ( marked_targets_[ index ] != net_id ) continue;

        marked_targets_[ index ] = marked_targets_[ marked_count_ - 1u ];
        --marked_count_;
        return;
    }
}

std::unique_ptr< champion_base > create_aphelios( game_object* player )
{
    return std::make_unique< aphelios >( player );
}

}
