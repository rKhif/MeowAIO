#pragma once

#include <sdk.hpp>

#include "framework/champion_base.hpp"

#include "db/champions/aphelios.hpp"

#include <array>
#include <cstdint>
#include <memory>

namespace sora::champions
{
    class aphelios final : public champion_base
    {
    public:
        explicit aphelios( game_object* player ) : champion_base{ player } {}

        void on_init() override;
        void on_game_update() override;
        void on_draw() override;

        void combo() override;
        void on_after_attack( orb_sdk::event_data* data ) override;

        bool on_before_attack( orb_sdk::event_data* data ) override;
        void on_create_missile( game_object* missile ) override;
        void on_delete_object( game_object* object ) override;
        void on_buff_gain( game_object* target, buff_instance* buff ) override;
        void on_buff_loss( game_object* target, buff_instance* buff ) override;
        void on_stop_cast( game_object* caster, spell_cast* cast, bool was_attack_processed ) override;
        void on_process_cast( game_object* caster, spell_cast* cast ) override;

    private:
        enum class infernum_step : uint8_t
        {
            idle = 0,
            q1_in_flight,    // Q Infernum cast, waiting for missile spawn
            swap_pending,    // W cast, waiting for weapon flip
            consume_q1,      // weapon flipped, waiting for mark from Q Inf hit
            waiting_q2_cast, // AA #2 fired (force), waiting for mark_loss to fire Q Cal
            q2_in_flight,    // Q Calibrum cast, waiting for mark from Q Cal hit
        };

        enum class severum_step : uint8_t
        {
            idle = 0,
            q_active,      // Q Severum cast, rapid-firing (builds chakram stacks)
            swap_pending,  // Q ended, W cast, waiting for weapon flip to Crescendum
        };

        enum class inf_gra_step : uint8_t
        {
            idle = 0,
            q_in_flight,   // Q-Inf cast, waiting for offhand Gra AAs to mark cone hits
            swap_pending,  // W cast, waiting for weapon flip to Gravitum
            be_pending,    // Gra main now, ready to cast BE on accumulated marks
        };

        enum class gra_inf_step : uint8_t
        {
            idle = 0,
            be_in_flight,      // BE cast on Gra-marked target, waiting for root + sync
            swap_pending,      // W cast, waiting for weapon flip to Infernum
            duskwave_pending,  // Inf main now, ready to fire Duskwave on rooted target
        };

        void setup_combo_menu( menu_category* parent );

        void clear_chakram_state();
        bool is_chakram_in_flight() const;
        void tick_post_r_kick_assist();
        bool try_crescendum_sentry();
        bool try_crescendum_r();
        bool is_orbwalker_mode_active() const;

        void update_weapon_state();
        void apply_weapon_spell_params( sora::db::champions::aphelios::weapon w );
        int  read_charges( int slot ) const;

        void init_weapon_queue();
        sora::db::champions::aphelios::weapon next_weapon() const;

        bool try_calibrum_q( game_object* target );
        bool passes_ammo_check( game_object* target ) const;
        bool passes_calibrum_reserve() const;
        int  cal_q_hc() const  { return cal_q_hitchance_; }
        int  cal_r_hc() const  { return cal_r_hitchance_; }
        int  inf_q_hc() const  { return inf_q_hitchance_; }
        int  inf_r_hc() const  { return inf_r_hitchance_; }
        int  sev_r_hc() const  { return sev_r_hitchance_; }
        int  gra_r_hc() const  { return gra_r_hitchance_; }
        int  cres_q_hc() const { return cres_q_hitchance_; }
        int  cres_r_hc() const { return cres_r_hitchance_; }

        bool  try_qr_combo( game_object* target );
        bool  try_finish_qr_combo();
        bool  try_calibrum_r_standalone( game_object* target );
        float calibrum_mark_bonus_per_aa( game_object* target ) const;
        bool  killable_with_qr_aa2( game_object* target ) const;
        bool  killable_with_q_aa1( game_object* target ) const;
        bool  killable_with_r_aa1( game_object* target ) const;
        void  maybe_force_calibrum_bonus_aa();
        bool  is_marked( uint32_t net_id ) const;

        bool try_infernum_q1();
        bool try_infernum_duskwave_solo( game_object* target );
        bool try_infernum_r();
        bool try_inf_aa_q_anim_cancel();
        bool try_inf_gra_q1();
        bool killable_with_duskwave_aa( game_object* target ) const;
        bool killable_with_r_inf( game_object* target ) const;
        int  count_enemies_near_duskwave_target( game_object* primary ) const;
        void clear_infernum_state();
        void clear_inf_gra_state();

        bool try_severum_swap_q();
        bool try_severum_solo_q();
        bool killable_with_severum_q( game_object* target ) const;
        void clear_severum_state();

        bool try_gravitum_be( game_object* target );
        bool try_gravitum_r();
        bool try_gra_inf_be();
        bool killable_with_be_aa( game_object* target ) const;
        bool killable_with_be_duskwave_aa( game_object* target ) const;
        bool target_be_castable( game_object* target ) const;
        void clear_gra_inf_state();
        void gra_mark_add( uint32_t net_id );
        void gra_mark_remove( uint32_t net_id );
        bool is_gra_marked( uint32_t net_id ) const;
        int  count_allies_near( const math::vector3& pos, float range ) const;
        int  count_rootable_gra_marked() const;
        game_object* pick_any_gra_marked_target() const;

        void mark_add( uint32_t net_id );
        void mark_remove( uint32_t net_id );

        bool     chakram_out_{ false };
        bool     chakram_returning_{ false };
        uint32_t chakram_net_id_{ 0u };
        float    chakram_clear_at_{ 0.f };  // game-time deadline; AA blocked while game_time_ < this

        sora::db::champions::aphelios::weapon main_weapon_   { sora::db::champions::aphelios::weapon::none };
        sora::db::champions::aphelios::weapon offhand_weapon_{ sora::db::champions::aphelios::weapon::none };
        int  main_ammo_   { 0 };
        int  offhand_ammo_{ 0 };

        static constexpr size_t WEAPON_QUEUE_SIZE = 3u;
        std::array< sora::db::champions::aphelios::weapon, WEAPON_QUEUE_SIZE > weapon_queue_{};
        bool queue_initialized_{ false };

        std::array< uint32_t, sora::db::champions::aphelios::CALIBRUM_MARK_TRACKER_CAP > marked_targets_{};
        size_t marked_count_{ 0u };

        std::array< uint32_t, sora::db::champions::aphelios::GRAVITUM_MARK_TRACKER_CAP > gra_marked_targets_{};
        size_t gra_marked_count_{ 0u };

        bool  r_pending_{ false };
        float r_pending_started_at_{ 0.f };
        bool  q_cast_observed_{ false };
        float qr_force_aa_until_{ 0.f };

        // Post-R "kick" window: when R is cast with Crescendum main, the
        // R spawns many missiles (OffHandOrbit, MisInMini, LineAttack) that
        // confuse the orbwalker's internal "AA ready" tracker — it stops
        // calling on_before_attack for several seconds. Workaround:
        // periodically issue sdk::orbwalker->attack(target) to bypass.
        // After N consecutive failures from attack(), fall back to direct
        // sdk::common->issue_order(attack_unit) (bypasses orbwalker entirely).
        float post_r_kick_until_{ 0.f };
        float post_r_kick_last_at_{ 0.f };
        int   post_r_kick_fail_streak_{ 0 };

        // Per-weapon master toggles + combo-specific toggles + hitchance.
        // Master Q/R disables ALL usage of that ability on that weapon
        // (harass, combos, extensions). Combo toggles are additional gates
        // below masters. Hitchance values are SDK percentage (0=any,
        // 30=low, 50=medium, 70=high, 85=very_high, 100=guaranteed).

        // Calibrum
        bool  cal_use_q_master_   { true };
        bool  cal_use_r_master_   { true };
        int   cal_q_hitchance_    { 70 };  // high
        int   cal_r_hitchance_    { 70 };
        int   cal_ammo_hold_      { sora::db::champions::aphelios::DEFAULT_AMMO_HOLD_THRESHOLD };
        bool  cal_qr_combo_       { true };

        // Infernum
        bool  inf_use_q_master_         { true };
        bool  inf_use_r_master_         { true };
        int   inf_q_hitchance_          { 70 };
        int   inf_r_hitchance_          { 70 };
        bool  inf_cal_swap_combo_       { true };
        bool  inf_gra_swap_combo_       { true };
        bool  inf_duskwave_solo_combo_  { true };
        bool  inf_duskwave_multi_combo_ { true };
        bool  inf_r_combo_              { true };
        bool  inf_r_solo_finisher_combo_{ true };
        bool  inf_anim_cancel_combo_    { true };
        bool  inf_aa_q_cancel_combo_    { true };
        int   inf_r_min_enemies_        { 2 };  // slider 2-5
        int   inf_q_min_targets_        { 2 };  // slider 2-5
        int   inf_gra_swap_min_marks_   { 2 };  // slider 1-5

        // Severum
        bool  sev_use_q_master_   { true };
        bool  sev_use_r_master_   { true };
        int   sev_r_hitchance_    { 70 };
        bool  sev_cres_swap_combo_{ true };
        bool  sev_solo_q_combo_   { true };

        // Crescendum
        bool  cres_use_q_master_   { true };
        bool  cres_use_r_master_   { true };
        int   cres_q_hitchance_    { 70 };
        int   cres_r_hitchance_    { 70 };
        bool  cres_sentry_combo_   { true };
        bool  cres_r_combo_        { true };
        int   cres_r_min_enemies_  { 2 };  // slider 2-5
        bool  cres_cal_mv_opener_  { true };  // Cal-Cres pairing: R for chakram gen

        // Gravitum
        bool  gra_use_q_master_            { true };
        bool  gra_use_r_master_            { true };
        int   gra_r_hitchance_             { 70 };
        bool  gra_be_combo_                { true };
        bool  gra_be_multi_combo_          { true };
        int   gra_be_min_roots_            { 2 };  // slider 1-5
        bool  gra_r_engage_combo_          { true };
        int   gra_r_min_enemies_           { 2 };  // slider 2-5
        int   gra_r_min_allies_            { 2 };  // slider 1-4
        bool  gra_inf_swap_combo_          { true };
        bool  gra_inf_swap_require_killable_ { true };

        infernum_step infernum_step_         { infernum_step::idle };
        float         infernum_started_at_   { 0.f };
        uint32_t      infernum_target_id_    { 0u };
        float         infernum_force_aa_until_{ 0.f };
        float         infernum_q1_processed_at_{ 0.f };

        severum_step  severum_step_          { severum_step::idle };
        float         severum_started_at_    { 0.f };
        float         severum_q_processed_at_{ 0.f };

        inf_gra_step  inf_gra_step_           { inf_gra_step::idle };
        float         inf_gra_started_at_     { 0.f };
        float         inf_gra_q_processed_at_ { 0.f };

        gra_inf_step  gra_inf_step_            { gra_inf_step::idle };
        float         gra_inf_started_at_      { 0.f };
        float         gra_inf_be_processed_at_ { 0.f };
        uint32_t      gra_inf_target_id_       { 0u };

        bool  draw_queue_         { true };

        bool  trace_manual_       { false };
        bool  trace_weapon_queue_ { false };
        float trace_wq_last_at_  { 0.f };
        uint32_t qr_gate_last_     { 0u };
    };

    std::unique_ptr< champion_base > create_aphelios( game_object* player );
}
