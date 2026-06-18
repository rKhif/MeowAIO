#pragma once

#include "../../pch.hpp"

// Real-world LoL Zyra reference values. Tweak as needed; the implementation in
// zyra.cpp is parameterised on these constants so the bot can be re-tuned without
// touching the logic. Values follow standard public datasheets and are reasonable
// defaults for a Sora-style framework.

namespace sora::db::champions::zyra
{
    // ---- Q ("Deadly Spines") -------------------------------------------------
    inline constexpr float Q_RANGE              = 800.f;   // cast range
    inline constexpr float Q_CAST_RANGE_BUFFER  = 25.f;    // tolerance for prediction overshoot
    inline constexpr float Q_RADIUS             = 175.f;   // spine zone radius
    inline constexpr float Q_DELAY              = 0.25f;   // cast delay
    inline constexpr float Q_PLANT_RANGE        = 525.f;   // distance a seed can be triggered by Q
    inline constexpr float Q_SEED_TRIGGER_RADIUS= 175.f;   // seed must be within this of Q hit to plant

    // ---- W ("Rampant Growth") -----------------------------------------------
    inline constexpr float W_RANGE              = 825.f;   // seed placement range
    inline constexpr float W_SEED_SAFE_MARGIN   = 25.f;    // padding so seeds always fall inside trigger
    inline constexpr float W_PENDING_EXPIRE     = 1.25f;   // how long a pending W stays valid (seconds)
    inline constexpr float W_RETRY_DELAY        = 0.12f;   // min spacing between W casts
    inline constexpr float W_SECOND_SEED_OFFSET = 110.f;   // sideways offset for the second seed of a pair

    // ---- E ("Grasping Roots") -----------------------------------------------
    inline constexpr float E_RANGE              = 1150.f;
    inline constexpr float E_RADIUS             = 70.f;
    inline constexpr float E_DELAY              = 0.25f;
    inline constexpr float E_SPEED              = 1150.f;
    inline constexpr float E_PLANT_RANGE        = 525.f;
    inline constexpr float E_SEED_TRIGGER_RADIUS= 175.f;

    // ---- R ("Stranglethorns") -----------------------------------------------
    inline constexpr float R_RANGE              = 700.f;
    inline constexpr float R_RADIUS             = 500.f;
    inline constexpr float R_DELAY              = 2.0f;

    // ---- Combo damage hint multipliers --------------------------------------
    inline constexpr float PLANT_EXPECTED_HITS_COMBO = 2.5f; // plants typically hit a few times in a combo
    inline constexpr float PLANT_EXPECTED_HITS_R     = 5.0f; // R empowers plants; expect more ticks

    // ---- Mana costs ---------------------------------------------------------
    inline float q_mana_cost(int rank)
    {
        static constexpr float costs[] = { 0.f, 75.f, 80.f, 85.f, 90.f, 95.f };
        if (rank <= 0) return costs[1];
        if (rank > 5)  return costs[5];
        return costs[rank];
    }

    inline float e_mana_cost(int) { return 70.f; }
    inline float r_mana_cost(int) { return 100.f; }

    // ---- Raw damage (pre-mitigation) ----------------------------------------
    // Q: 60/95/130/165/200 (+65% AP)
    inline float q_raw_damage(int rank, game_object* player)
    {
        static constexpr float base[] = { 0.f, 60.f, 95.f, 130.f, 165.f, 200.f };
        const int r = std::clamp(rank, 1, 5);
        const float ap = player ? player->get_ability_power() : 0.f;
        return base[r] + 0.65f * ap;
    }

    // E: 60/105/150/195/240 (+50% AP)
    inline float e_raw_damage(int rank, game_object* player)
    {
        static constexpr float base[] = { 0.f, 60.f, 105.f, 150.f, 195.f, 240.f };
        const int r = std::clamp(rank, 1, 5);
        const float ap = player ? player->get_ability_power() : 0.f;
        return base[r] + 0.50f * ap;
    }

    // R: 180/265/350 (+70% AP)
    inline float r_raw_damage(int rank, game_object* player)
    {
        static constexpr float base[] = { 0.f, 180.f, 265.f, 350.f };
        const int r = std::clamp(rank, 1, 3);
        const float ap = player ? player->get_ability_power() : 0.f;
        return base[r] + 0.70f * ap;
    }

    // Plant single-hit damage (lvl-scaled base + 0.2 AP).
    inline float plant_raw_damage(int level, game_object* player)
    {
        const int l = std::clamp(level, 1, 18);
        const float base = 24.f + (l - 1) * 5.4f;          // ~24 -> ~115 from lvl 1 to 18
        const float ap   = player ? player->get_ability_power() : 0.f;
        return base + 0.20f * ap;
    }
}
