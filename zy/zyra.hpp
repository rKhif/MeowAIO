#pragma once

#include <sdk.hpp>

#include "framework/champion_base.hpp"

#include <cstdint>
#include <memory>

namespace sora::champions
{
    class zyra final : public champion_base
    {
    public:
        explicit zyra( game_object* player ) : champion_base{ player } {}

        void on_init() override;
        void on_game_update() override;
        void on_draw() override;
        void on_draw_world() override;

        void combo() override;
        void harass() override;
        void clear() override;
        void fast_clear() override;
        void flee() override;

        float get_combo_damage( game_object* target ) const override;

    private:
        struct seed_plan
        {
            bool          valid{ false };
            math::vector3 pos{};
            int           score{ 0 };
        };

        struct pending_seed
        {
            bool          active{ false };
            math::vector3 pos{};
            float         expire_time{ 0.f };
            bool          critical{ false };
        };

        void setup_menu();

        bool is_valid_enemy( game_object* target ) const;
        bool is_valid_farm_target( game_object* target ) const;
        bool has_mana_for( float cost ) const;
        bool can_use_q( float min_mana_pct = 0.f ) const;
        bool can_use_w() const;
        bool can_use_e( float min_mana_pct = 0.f ) const;
        bool can_use_r() const;
        bool is_combo_active() const;
        bool is_sane_cast_pos( const math::vector3& pos, float max_range ) const;
        bool enemy_in_e_threat() const;

        int  effective_role() const;
        int  w_charges() const;
        bool can_spend_seed( bool critical ) const;
        bool should_double_seed( game_object* target, bool critical ) const;
        bool should_use_spell_for_harass() const;
        bool harass_q_shoves_wave( game_object* target ) const;

        seed_plan plan_seed_for_q( game_object* target, const math::vector3& q_pos, bool critical ) const;
        seed_plan plan_seed_for_e( game_object* target, const math::vector3& e_pos, bool critical ) const;
        seed_plan plan_seed_for_r( const math::vector3& r_pos, bool critical ) const;
        math::vector3 robust_seed_on_line( const math::vector3& spell_pos, float trigger_radius, float plant_range ) const;
        math::vector3 robust_seed_near_target( game_object* target, const math::vector3& spell_pos, float trigger_radius, float plant_range ) const;
        math::vector3 clamp_seed_pos( const math::vector3& pos ) const;

        void schedule_seed( const math::vector3& pos, bool critical );
        void process_pending_seeds();
        bool cast_pending_seed( pending_seed& seed );

        bool try_q( game_object* target, int hitchance, bool critical );
        bool try_e( game_object* target, int hitchance, bool critical );
        bool try_r_smart( game_object* target, bool critical, bool force = false );
        bool try_r_on_best_cluster( bool critical );
        bool try_r_semi();
        bool try_e_semi();
        bool try_prepare_zone();
        bool try_q_execute();
        bool try_e_execute();
        bool try_jungle_spell_cycle();

        int count_enemies_in_radius( const math::vector3& pos, float radius ) const;
        int count_minions_in_radius( const math::vector3& pos, float radius, bool monsters ) const;
        int count_minions_on_line( const math::vector3& end, float width, bool monsters ) const;

        float health_with_shields( game_object* target ) const;
        float calc_q( game_object* target ) const;
        float calc_e( game_object* target ) const;
        float calc_r( game_object* target ) const;
        float calc_plant( game_object* target, float expected_hits ) const;

        static float segment_distance( const math::vector3& p, const math::vector3& a, const math::vector3& b );
        static math::vector3 offset_from_dir( const math::vector3& pos, const math::vector3& dir, float amount );

        bool  q_enabled_{ true };
        bool  w_enabled_{ true };
        bool  e_enabled_{ true };
        bool  r_enabled_{ true };

        int   role_mode_{ 0 };
        int   playstyle_{ 0 };
        int   plant_policy_{ 0 };
        int   r_policy_{ 1 };
        float min_mana_pct_{ 0.15f };
        int   combo_hitchance_{ pred_sdk::hitchance::high };
        int   harass_hitchance_{ pred_sdk::hitchance::high };
        int   e_hitchance_{ pred_sdk::hitchance::high };

        bool  plant_on_q_{ true };
        bool  plant_on_e_{ true };
        bool  plant_on_r_{ true };
        bool  smart_conserve_last_{ true };
        bool  smart_double_on_kill_{ true };
        bool  smart_lead_{ true };

        int   min_r_targets_{ 2 };
        int   min_r_cluster_score_{ 70 };
        bool  auto_q_ks_{ true };
        bool  auto_e_ks_{ true };
        bool  anti_engage_e_{ false };
        bool  anti_engage_r_{ false };
        bool  semi_r_{ false };
        bool  semi_e_{ false };
        bool  prepare_zone_{ false };
        bool  harass_use_e_{ false };
        bool  harass_respect_wave_{ true };
        bool  support_respect_wave_{ true };
        int   lane_q_min_hit_{ 3 };
        int   jungle_q_min_hit_{ 1 };
        int   jungle_e_min_hit_{ 1 };

        bool     draw_q_{ true };
        bool     draw_e_{ true };
        bool     draw_r_{ true };
        bool     draw_w_seed_{ true };
        uint32_t draw_q_color_{ 0xFF55DD88u };
        uint32_t draw_e_color_{ 0xFFAA55DDu };
        uint32_t draw_r_color_{ 0xFFFF5555u };
        uint32_t draw_w_color_{ 0xFF66DDFFu };

        pending_seed pending_seeds_[ 2 ]{};
        float last_w_cast_time_{ 0.f };
    };

    std::unique_ptr< champion_base > create_zyra( game_object* player );
}
