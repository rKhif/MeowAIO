// =====================================================================
// Zeri (Blue / PaidScripts source) -> VEN SDK port
//
// The original implementation lived in `champs/ZeriPS.cpp` and was built
// against the PaidScripts SDK (BaseSpell, ObjectManager, Prediction,
// Graphics, etc).  This file ports the same gameplay logic onto the VEN
// SDK primitives (g_sdk + sdk::*).  It intentionally mirrors the source
// layout (helpers / chain logic / orbwalker submodes / events / menu)
// so the two implementations can be diffed side by side.
// =====================================================================

#include "zeri_ps.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <vector>

namespace zeri_ps
{
    static menu_category* s_menu = nullptr;

    // =================================================================
    // Cached spell state (refreshed each tick from refresh_state())
    // =================================================================
    static float c_q_range        = spell_data::q_base_range;
    static float c_q_speed        = spell_data::q_speed_normal;
    static float c_q_cast_delay   = 0.f;     // half of AA cast delay
    static float c_w_cast_delay   = 0.55f;
    static float c_aa_range       = 500.f;
    static float c_attack_speed   = 0.658f;

    static bool  s_laser_ready    = false;   // ZeriQPassiveReady
    static bool  s_lightning      = false;   // ZeriESpecialRounds
    static bool  s_r_active       = false;   // ZeriR (overcharged)
    static bool  s_collision_off  = false;   // Q ignores units while E/R buffs

    // Per-mode last cast pacing (LastCastTime[ Slot ])
    static float last_cast_q      = 0.f;
    static float last_cast_w      = 0.f;
    static float last_cast_e      = 0.f;
    static float last_cast_r      = 0.f;
    static constexpr float MIN_CAST_GAP = 0.10f; // matches the 0.1s clamp in source

    // For the orbwalker AA-replacement Q gating
    static uint32_t s_ignore_target_id = 0;

    // =================================================================
    // Wall-jump table (ported from BlueZeri::InitializeWallJumps)
    // =================================================================
    struct wall_jump_t
    {
        math::vector3 position;
        math::vector3 cast_position;
    };

    static const wall_jump_t WALL_JUMPS[] = {
        {{6543.56f, 48.53f, 4787.35f}, {6978.00f, 52.42f, 6238.00f}},
        {{6974.34f, 52.28f, 6278.47f}, {6548.00f, 48.53f, 4800.00f}},
        {{9522.00f, 64.96f, 3570.00f}, {8924.00f, -71.24f, 5478.00f}},
        {{8917.89f, -71.24f, 5511.04f}, {9522.00f, 64.96f, 3570.00f}},
        {{758.71f, 52.84f, 10776.69f}, {774.00f, 56.84f, 13306.00f}},
        {{8010.97f, 52.84f, 14177.41f}, {10572.00f, 91.43f, 14356.00f}},
        {{10738.70f, 52.31f, 9860.43f}, {12058.00f, 52.37f, 8152.00f}},
        {{12103.97f, 52.40f, 8136.85f}, {10736.00f, 52.45f, 9846.00f}},
        {{14141.02f, 52.31f, 7396.93f}, {14222.00f, 91.43f, 10506.00f}},
        {{13172.59f, 55.42f, 6133.08f}, {12140.00f, 52.08f, 7266.00f}},
        {{12109.34f, 52.08f, 7299.52f}, {13172.00f, 55.41f, 6108.00f}},
        {{14144.00f, 53.65f, 4394.00f}, {13888.00f, 48.21f, 1836.00f}},
        {{10666.00f, -70.32f, 5714.00f}, {10534.00f, 51.74f, 7456.00f}},
        {{10523.52f, 51.71f, 7502.02f}, {10666.00f, -70.32f, 5714.00f}},
        {{5252.25f, 56.82f, 11399.10f}, {5998.00f, -61.38f, 9414.00f}},
        {{10409.76f, 51.34f, 793.02f}, {13272.00f, 40.04f, 1058.00f}},
        {{5998.00f, -61.38f, 9414.00f}, {5250.00f, 56.81f, 11376.00f}},
        {{3918.00f, 52.84f, 14080.00f}, {1374.00f, 58.32f, 13906.00f}},
        {{7681.81f, 49.44f, 748.44f}, {4624.00f, 93.38f, 558.00f}},
        {{614.32f, 95.75f, 4501.11f}, {700.00f, 52.84f, 7556.00f}},
        {{709.20f, 52.84f, 7587.31f}, {624.00f, 95.75f, 4508.00f}},
        {{1656.46f, 52.84f, 8869.72f}, {2650.00f, 50.74f, 7524.00f}},
        {{2645.28f, 50.43f, 7488.25f}, {1672.00f, 52.81f, 8852.00f}},
        {{2314.00f, 48.63f, 11748.00f}, {3448.00f, 22.20f, 9354.00f}},
        {{3448.00f, 22.20f, 9354.00f}, {2314.00f, 48.63f, 11748.00f}},
        {{8221.36f, 49.55f, 10142.62f}, {7998.00f, 52.47f, 8686.00f}},
        {{7993.60f, 52.46f, 8672.59f}, {8214.00f, 49.50f, 10120.00f}},
        {{4209.63f, 50.62f, 7404.97f}, {4024.00f, -68.92f, 9432.00f}},
        {{4023.32f, -69.42f, 9464.10f}, {4216.00f, 50.63f, 7440.00f}},
        {{2777.15f, 56.12f, 6728.51f}, {4144.00f, 50.26f, 5064.00f}},
        {{4144.00f, 50.26f, 5064.00f}, {2770.00f, 56.29f, 6718.00f}},
        {{666.f, 182.f, 160.f}, {3874.f, 96.f, 562.f}},
        {{14445.f, 171.f, 14078.f}, {14306.f, 91.f, 11025.f}},
        {{242.773f, 184.f, 761.57f}, {524.f, 95.7f, 3758.f}},
        {{14022.f, 171.f, 14506.f}, {10514.f, 91.f, 14362.f}},
        {{13246.f, 41.f, 1048.f}, {10375.f, 51.f, 782.f}},
        {{13872.f, 45.f, 1758.f}, {14258.f, 53.f, 5537.f}},
        {{1374.f, 58.f, 13906.f}, {4354.f, 52.f, 14106.f}},
        {{824.f, 57.f, 13356.f}, {740.f, 52.f, 10053.f}},
        {{3874.f, 95.f, 558.f}, {1290.f, 133.f, 243.f}},
        {{524.f, 95.f, 3958.f}, {146.f, 178.f, 786.f}},
        {{11029.f, 91.f, 14381.f}, {13062.f, 93.f, 14582.f}},
        {{14272.f, 91.f, 11156.f}, {14632.f, 171.f, 14543.f}},
        {{6560.f, 52.f, 7257.f}, {5643.f, 51.f, 7908.f}},
        {{4972.f, 23.f, 8386.f}, {5712.f, 51.f, 7879.f}},
        {{7522.f, 52.f, 8056.f}, {7001.f, -71.f, 8542.f}},
        {{6374.f, -59.f, 9106.f}, {7326.f, 19.f, 8328.f}},
        {{8322.f, 52.f, 7358.f}, {9974.f, 43.f, 6558.f}},
        {{9883.f, 27.f, 6496.f}, {8978.f, 53.f, 7015.f}},
        {{7237.f, 52.f, 6675.f}, {8474.f, -49.f, 5741.f}},
        {{8488.f, -55.f, 5753.f}, {7842.f, -73.f, 6223.f}},
        {{4724.f, 107.f, 608.f}, {10631.f, 49.f, 853.f}},
        {{14222.f, 91.f, 10456.f}, {14158.f, 52.f, 7817.f}},
        {{10472.f, 91.f, 14306.f}, {7705.f, 52.f, 14162.f}},
        {{12496.f, 51.f, 3226.f}, {11396.f, 7.8f, 5470.f}},
        {{11396.f, 7.8f, 5470.f}, {12462.f, 51.f, 3266.f}},
        {{7220.f, 56.47f, 11806.f}, {4538.f, 56.47f, 12124.f}},
        {{4538.f, 56.47f, 12124.f}, {7224.f, 56.4f, 11856.f}},
        {{10322.f, 49.2f, 2758.f}, {7504.f, 52.57f, 3140.f}},
        {{7504.f, 52.57f, 3140.f}, {10376.f, 49.22f, 2802.f}},
    };
    static constexpr size_t WALL_JUMP_COUNT = sizeof(WALL_JUMPS) / sizeof(WALL_JUMPS[0]);

    // =================================================================
    // VALIDATION
    // =================================================================
    static bool is_valid_target(game_object* u, float range = FLT_MAX)
    {
        if (!u || !u->is_valid() || u->is_dead() || !u->is_targetable()) return false;
        if (!u->is_visible()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (u->get_team_id() == p->get_team_id()) return false;
        if (range != FLT_MAX) {
            if (p->get_position().distance(u->get_position()) > range) return false;
        }
        return true;
    }

    static bool spell_learned(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto s = p->get_spell(slot);
        return s && s->get_level() > 0;
    }

    static bool spell_ready(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    static float spell_cd(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return FLT_MAX;
        auto s = p->get_spell(slot);
        return s ? s->get_cooldown() : FLT_MAX;
    }

    static bool enough_mana(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    static float game_time()
    {
        return g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    static bool can_action()
    {
        if (!sdk::orbwalker) return true;
        return !sdk::orbwalker->would_cancel_attack();
    }

    // =================================================================
    // STATE REFRESH (BlueZeri::UpdateSpells)
    // =================================================================
    static void refresh_state()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        c_attack_speed = std::max(p->get_attack_speed_mod(), 0.001f);
        c_aa_range     = p->get_attack_range();

        // 750 + (AA range - 500) bonus
        c_q_range = spell_data::q_base_range + std::max(0.f, c_aa_range - 500.f);

        // Half the AA cast delay
        c_q_cast_delay = p->get_attack_cast_delay() / 2.f;

        // W cast time scales with bonus AS (cap at +2.778 like source)
        const float as_ratio = std::min(c_attack_speed - 1.f, 2.778f);
        const float w_scaled = -0.09f * std::max(0.f, as_ratio) + 0.55f;
        c_w_cast_delay = std::max(0.30f, std::min(w_scaled, 0.55f));

        s_laser_ready = p->get_buff_by_hash(spell_data::buff_q_passive_ready) != nullptr;
        s_lightning   = p->get_buff_by_hash(spell_data::buff_e_lightning) != nullptr;
        s_r_active    = p->get_buff_by_hash(spell_data::buff_r_active) != nullptr;

        // Q goes through units while E (lightning rounds) is up
        s_collision_off = s_lightning;

        c_q_speed = s_r_active ? spell_data::q_speed_r : spell_data::q_speed_normal;
    }

    // =================================================================
    // PREDICTION HELPERS
    // =================================================================
    static int map_hitchance(int ui_value)
    {
        // pred_sdk::hitchance numeric scale: any=0, low=30, medium=50,
        // high=70, very_high=85, guaranteed_hit=100. The VEN SDK has no
        // dedicated "dashing" tier so we map it to very_high (the source
        // used "DashingMidAir" as a hardest-to-hit gate; very_high gives
        // the same effect via percent-confidence floor).
        switch (ui_value) {
        case HC_VERY_LOW:  return pred_sdk::hitchance::any;
        case HC_LOW:       return pred_sdk::hitchance::low;
        case HC_MEDIUM:    return pred_sdk::hitchance::medium;
        case HC_HIGH:      return pred_sdk::hitchance::high;
        case HC_VERY_HIGH: return pred_sdk::hitchance::very_high;
        case HC_DASH:      return pred_sdk::hitchance::very_high;
        default:           return pred_sdk::hitchance::medium;
        }
    }

    static pred_sdk::pred_data predict_q(game_object* target, int hc, bool force_no_col = false)
    {
        pred_sdk::spell_data sp{};
        sp.spell_type          = pred_sdk::spell_type::linear;
        sp.targetting_type     = pred_sdk::targetting_type::edge_to_edge;
        sp.spell_slot          = SLOT_Q;
        sp.range               = c_q_range;
        sp.radius              = spell_data::q_radius;
        sp.delay               = c_q_cast_delay;
        sp.projectile_speed    = c_q_speed;
        sp.expected_hitchance  = hc;
        if (!force_no_col && !s_collision_off) {
            sp.forbidden_collisions = { pred_sdk::collision_type::unit };
        }

        pred_sdk::pred_data pd{};
        try { pd = sdk::prediction->predict(target, sp); }
        catch (...) { pd.is_valid = false; }
        return pd;
    }

    static pred_sdk::pred_data predict_w(game_object* target, int hc)
    {
        pred_sdk::spell_data sp{};
        sp.spell_type          = pred_sdk::spell_type::linear;
        sp.targetting_type     = pred_sdk::targetting_type::center_to_edge;
        sp.spell_slot          = SLOT_W;
        sp.range               = spell_data::w_range;
        sp.radius              = spell_data::w_radius + spell_data::w_extra_collision_radius;
        sp.delay               = c_w_cast_delay;
        sp.projectile_speed    = spell_data::w_speed;
        sp.expected_hitchance  = hc;
        sp.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data pd{};
        try { pd = sdk::prediction->predict(target, sp); }
        catch (...) { pd.is_valid = false; }
        return pd;
    }

    // =================================================================
    // CHAIN LIGHTNING HELPERS (ported from BlueZeri::FindChainStart)
    // =================================================================
    // While R (Lightning Crash) is active, Q bolts arc to a nearby
    // visible client within 650u of the impact.  We find a candidate
    // Q-target that, after up to 4 chained hops, reaches our desired
    // hero.  Returns Vector3::zero on failure.
    static game_object* get_next_chain_target(const math::vector3& from_pos,
                                              const std::vector<uint32_t>& blacklist)
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return nullptr;

        game_object* best = nullptr;
        float best_d = spell_data::q_chain_range;

        auto consider = [&](game_object* c) {
            if (!c || !c->is_valid() || c->is_dead() || !c->is_targetable()) return;
            if (!c->is_visible() || c->get_team_id() == p->get_team_id()) return;
            for (uint32_t id : blacklist) if (id == c->get_network_id()) return;
            const float d = c->get_position().distance(from_pos);
            if (d < best_d) { best_d = d; best = c; }
        };

        for (auto* h : g_sdk->object_manager->get_heroes())   consider(h);
        for (auto* m : g_sdk->object_manager->get_minions())  consider(m);
        for (auto* m : g_sdk->object_manager->get_monsters()) consider(m);
        return best;
    }

    static math::vector3 find_chain_start(game_object* desired)
    {
        const math::vector3 zero{ 0.f, 0.f, 0.f };
        if (!desired || !sdk::prediction) return zero;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return zero;
        const auto player_pos = p->get_position();

        auto try_seed = [&](game_object* seed) -> math::vector3 {
            if (!is_valid_target(seed, c_q_range)) return zero;

            auto pd = predict_q(seed, pred_sdk::hitchance::low);
            if (!pd.is_valid) return zero;

            game_object* cur = seed;
            std::vector<uint32_t> chain = { cur->get_network_id() };

            for (int i = 0; i < 4; ++i) {
                cur = get_next_chain_target(cur->get_position(), chain);
                if (!cur) break;
                chain.push_back(cur->get_network_id());
                if (cur->get_network_id() == desired->get_network_id()) {
                    return pd.cast_position;
                }
            }
            return zero;
        };

        // Seed candidates: any valid AI base in Q range.  Source iterates
        // every AIBase; we cover heroes + minions + monsters since those
        // are the bouncable categories.
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            const auto cp = try_seed(h);
            if (cp.x != 0.f || cp.z != 0.f) return cp;
        }
        for (auto* m : g_sdk->object_manager->get_minions()) {
            const auto cp = try_seed(m);
            if (cp.x != 0.f || cp.z != 0.f) return cp;
        }
        for (auto* m : g_sdk->object_manager->get_monsters()) {
            const auto cp = try_seed(m);
            if (cp.x != 0.f || cp.z != 0.f) return cp;
        }
        return zero;
    }

    // =================================================================
    // CAST HELPERS
    // =================================================================
    static bool cast_q(const math::vector3& cast_pos)
    {
        if (!sdk::common) return false;
        if (!sdk::common->cast_spell(SLOT_Q, cast_pos)) return false;
        last_cast_q = game_time();
        return true;
    }

    static bool cast_w(const math::vector3& cast_pos)
    {
        if (!sdk::common) return false;
        if (!sdk::common->cast_spell(SLOT_W, cast_pos)) return false;
        last_cast_w = game_time();
        return true;
    }

    static bool cast_e(const math::vector3& cast_pos)
    {
        if (!sdk::common) return false;
        if (!sdk::common->cast_spell(SLOT_E, cast_pos)) return false;
        last_cast_e = game_time();
        return true;
    }

    static bool cast_r()
    {
        if (!sdk::common) return false;
        sdk::common->cast_spell(SLOT_R);
        last_cast_r = game_time();
        return true;
    }

    // =================================================================
    // DAMAGE HELPERS  (ported from BlueZeri::GetQDamage/GetWDamage/GetRDamage)
    // =================================================================
    static float q_damage(game_object* target)
    {
        if (!target || !sdk::damage) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        return sdk::damage->get_spell_damage(p, target, SLOT_Q);
    }

    static float w_damage(game_object* target)
    {
        if (!target || !sdk::damage) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        return sdk::damage->get_spell_damage(p, target, SLOT_W);
    }

    static float r_damage(game_object* target)
    {
        if (!target || !sdk::damage) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        return sdk::damage->get_spell_damage(p, target, SLOT_R);
    }

    // =================================================================
    // COMBO
    // =================================================================
    // BlueZeri::Combo: iterate ordered targets, take first whose
    // prediction passes.  We follow the same priority.  Chain-bounce
    // logic kicks in while R is active.  `enabled` is the per-mode
    // gate (menu::combo_q for combo, menu::harass_q for harass).
    static bool combo_q(bool enabled)
    {
        if (!enabled || !spell_ready(SLOT_Q) || !can_action()) return false;
        if (game_time() - last_cast_q < MIN_CAST_GAP) return false;
        if (!sdk::target_selector) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto player_pos = p->get_position();

        for (auto* target : sdk::target_selector->get_sorted_heroes()) {
            if (!is_valid_target(target)) continue;

            // Direct prediction first
            const int hc = pred_sdk::hitchance::low;
            auto pd = predict_q(target, hc);

            const bool in_range = player_pos.distance(target->get_position()) <= c_q_range;

            // Source: Status==Collision + IgnoreCollision -> cast; or
            // Status==Success -> cast; or Status==OutOfRange but target
            // actually in range (target moving in) -> cast anyway.
            if (pd.is_valid && pd.hitchance >= hc) {
                if (cast_q(pd.cast_position)) { s_ignore_target_id = target->get_network_id(); return true; }
            } else if (menu::combo_q_ignore_col && in_range) {
                // Forbid collision was the only failure -- retry without
                // and cast at the resulting position.
                auto pd2 = predict_q(target, hc, /*force_no_col=*/true);
                if (pd2.is_valid && pd2.hitchance >= hc) {
                    if (cast_q(pd2.cast_position)) { s_ignore_target_id = target->get_network_id(); return true; }
                }
            }

            // Chain bounce logic during R
            if (menu::chain_logic && s_r_active) {
                auto* r_buff = p->get_buff_by_hash(spell_data::buff_r_active);
                if (!r_buff || !r_buff->is_active()) continue;

                const auto chain_pos = find_chain_start(target);
                if (chain_pos.x == 0.f && chain_pos.z == 0.f) continue;

                const float time_to_hit = player_pos.distance(chain_pos) / c_q_speed + 0.25f;
                const float r_remaining = std::max(0.f, r_buff->get_end_time() - game_time());
                if (menu::chain_only_keep_alive && r_remaining + 0.2f > time_to_hit)
                    continue;

                if (cast_q(chain_pos)) { s_ignore_target_id = target->get_network_id(); return true; }
            }
        }
        return false;
    }

    // ---- W (missile + wall beam) -------------------------------------
    static bool try_w_missile(game_object* target, int hc, bool only_kill)
    {
        if (!target) return false;
        if (only_kill && target->get_hp() + target->get_all_shield() > w_damage(target))
            return false;

        auto pd = predict_w(target, hc);
        if (!pd.is_valid || pd.hitchance < hc) return false;
        return cast_w(pd.cast_position);
    }

    // Simplified wall-beam logic: if the cast direction toward the
    // target threads through a wall (any unpathable cell in the first
    // 800u), allow casting at the prediction even if the missile
    // forbidden_collisions would normally reject it.  The original
    // BlueZeri code computes the post-wall beam range and recomputes
    // prediction; the VEN prediction engine handles wall passthrough
    // implicitly so we just need to confirm there is a wall in the path.
    static bool try_w_beam(game_object* target, int hc, bool only_kill)
    {
        if (!target || !g_sdk->nav_mesh) return false;
        if (only_kill && target->get_hp() + target->get_all_shield() > w_damage(target) * 1.75f)
            return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto from = p->get_position();
        const auto to   = target->get_position();
        const float dx = to.x - from.x, dz = to.z - from.z;
        const float len = std::sqrt(dx * dx + dz * dz);
        if (len < 1.f) return false;
        const float nx = dx / len, nz = dz / len;

        // Walk forward up to min(800u, distance) probing the navmesh
        const float scan = std::min(len, 800.f);
        bool wall_hit = false;
        for (float d = 50.f; d <= scan; d += 25.f) {
            math::vector3 pt{ from.x + nx * d, from.y, from.z + nz * d };
            pt.y = g_sdk->nav_mesh->get_terrain_height(pt.x, pt.z);
            if (!g_sdk->nav_mesh->is_pathable(pt)) { wall_hit = true; break; }
        }
        if (!wall_hit) return false;

        // Re-predict without wall collision rejection -- because we just
        // confirmed there's a wall between us and the target.
        pred_sdk::spell_data sp{};
        sp.spell_type         = pred_sdk::spell_type::linear;
        sp.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        sp.spell_slot         = SLOT_W;
        sp.range              = spell_data::wbeam_range;
        sp.radius             = spell_data::wbeam_radius;
        sp.delay              = c_w_cast_delay;
        sp.projectile_speed   = spell_data::w_speed;
        sp.expected_hitchance = hc;

        pred_sdk::pred_data pd{};
        try { pd = sdk::prediction->predict(target, sp); }
        catch (...) { return false; }
        if (!pd.is_valid || pd.hitchance < hc) return false;
        return cast_w(pd.cast_position);
    }

    static bool combo_w(bool check_mana, bool missile, bool beam)
    {
        if (!spell_ready(SLOT_W) || !can_action()) return false;
        if (game_time() - last_cast_w < MIN_CAST_GAP) return false;
        if (check_mana && !enough_mana(SLOT_W)) return false;
        if (!sdk::target_selector) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto player_pos = p->get_position();
        const float q_rng_sq = c_q_range * c_q_range;

        for (auto* target : sdk::target_selector->get_sorted_heroes()) {
            if (!is_valid_target(target, spell_data::w_range + 100.f)) continue;

            // Block-if-in-Q-range option (unless we're in a wall ourselves)
            math::vector3 here = player_pos;
            const float d2 = (target->get_position().x - player_pos.x) * (target->get_position().x - player_pos.x)
                           + (target->get_position().z - player_pos.z) * (target->get_position().z - player_pos.z);
            const bool in_wall = g_sdk->nav_mesh && !g_sdk->nav_mesh->is_pathable(here);
            if (menu::only_out_of_range_w && d2 < q_rng_sq && !in_wall) continue;

            if (beam    && try_w_beam(target,    map_hitchance(menu::hitchance_beam), false)) return true;
            if (missile && try_w_missile(target, map_hitchance(menu::hitchance_w),    false)) return true;
        }
        return false;
    }

    static bool killsteal_w()
    {
        if (!menu::ks_w) return false;
        if (!spell_ready(SLOT_W) || !enough_mana(SLOT_W) || !can_action()) return false;
        if (!sdk::target_selector) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto player_pos = p->get_position();
        const float q_rng_sq = c_q_range * c_q_range;

        for (auto* target : sdk::target_selector->get_sorted_heroes()) {
            if (!is_valid_target(target, spell_data::w_range + 100.f)) continue;

            const float dx = target->get_position().x - player_pos.x;
            const float dz = target->get_position().z - player_pos.z;
            if (dx * dx + dz * dz < q_rng_sq) continue;       // close enough that Q + AA handles it

            if (try_w_beam(target,    map_hitchance(menu::hitchance_beam), true)) return true;
            if (try_w_missile(target, map_hitchance(menu::hitchance_w),    true)) return true;
        }
        return false;
    }

    // ---- E gap close / Q-E-Q reset -----------------------------------
    static int enemy_count_near(const math::vector3& pos, float r)
    {
        int n = 0;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        const float r2 = r * r;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float dx = h->get_position().x - pos.x;
            const float dz = h->get_position().z - pos.z;
            if (dx * dx + dz * dz <= r2) ++n;
        }
        return n;
    }

    static bool combo_e(bool reset = false)
    {
        if (!spell_ready(SLOT_E) || (!can_action() && !reset)) return false;
        if (game_time() - last_cast_e < MIN_CAST_GAP) return false;
        if (!enough_mana(SLOT_E)) return false;
        if (!sdk::target_selector) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto player_pos = p->get_position();
        const bool should_cast = reset || !menu::only_reset;

        for (auto* target : sdk::target_selector->get_sorted_heroes()) {
            if (!is_valid_target(target)) continue;

            // Need Q to land on target somewhere
            auto pd = predict_q(target, pred_sdk::hitchance::low);
            if (!pd.is_valid) continue;

            math::vector3 cast_pos = pd.cast_position;
            const bool is_collision = !pd.collided_units.empty();

            // Resolve E destination: Cursor or Safe
            if (menu::e_mode == 1) {
                // Safe -- pick the E landing that minimises enemies in 450u
                // (simplified version of BlueZeri::GetEPositionScore).
                // Project forward toward target, then sample two short
                // offsets perpendicular and choose lowest enemy density.
                const auto tp = target->get_position();
                const float dx = tp.x - player_pos.x, dz = tp.z - player_pos.z;
                const float len = std::sqrt(dx * dx + dz * dz);
                if (len < 1.f) continue;
                const float nx = dx / len, nz = dz / len;

                math::vector3 candidates[3] = {
                    { player_pos.x + nx * spell_data::e_range, player_pos.y, player_pos.z + nz * spell_data::e_range },
                    { player_pos.x + nx * spell_data::e_range + (-nz) * 100.f, player_pos.y, player_pos.z + nz * spell_data::e_range + nx * 100.f },
                    { player_pos.x + nx * spell_data::e_range - (-nz) * 100.f, player_pos.y, player_pos.z + nz * spell_data::e_range - nx * 100.f },
                };

                int best_n = INT_MAX;
                math::vector3 best = candidates[0];
                for (auto& c : candidates) {
                    const int n = enemy_count_near(c, 450.f);
                    if (n < best_n) { best_n = n; best = c; }
                }
                if (best_n > menu::enemy_limit_e) continue;
                cast_pos = best;
            } else {
                // Cursor mode -- dash where the player is aiming
                if (g_sdk->hud_manager) {
                    cast_pos = g_sdk->hud_manager->get_cursor_position();
                }
            }

            // Collision branch: only fire when "Collision" submode allowed
            if (is_collision && menu::combo_e_collision) {
                if (should_cast || menu::collision_ignore_reset) {
                    return cast_e(cast_pos);
                }
            }

            // Killable branch: Q + E will kill (3-4 follow-up Qs accounted)
            if (menu::combo_e_killable && should_cast) {
                const float reset_count = reset ? 4.f : 3.f;
                const float qe_dmg = q_damage(target) * reset_count;
                const float q_dmg  = is_collision ? 0.f : q_damage(target) * 3.f;
                const float ehp    = target->get_hp() + target->get_all_shield();
                if (q_dmg < ehp && qe_dmg >= ehp) {
                    return cast_e(cast_pos);
                }
            }
        }
        return false;
    }

    // ---- R: count + killable -----------------------------------------
    static bool combo_r()
    {
        if (s_r_active) return false;
        if (!spell_ready(SLOT_R) || !can_action() || !enough_mana(SLOT_R)) return false;
        if (game_time() - last_cast_r < MIN_CAST_GAP) return false;
        if (!sdk::target_selector) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        if (menu::save_mana_e) {
            // Save enough mana to follow up with E
            if (p->get_par() - p->get_spell_mana_cost(SLOT_R) <= p->get_spell_mana_cost(SLOT_E))
                return false;
        }

        const auto player_pos = p->get_position();
        int count = 0;

        for (auto* target : sdk::target_selector->get_sorted_heroes()) {
            if (!is_valid_target(target)) continue;
            if (player_pos.distance(target->get_position()) > spell_data::r_range) continue;
            ++count;

            // Killable mode -- fire if R lethal and target survives the
            // burst-without-R (so we don't waste R on someone dying anyway)
            if (menu::combo_r_killable) {
                const float r_dmg = r_damage(target);
                const float q_dmg = q_damage(target);
                const float w_dmg = spell_ready(SLOT_W) ? w_damage(target) : 0.f;
                const float aa    = sdk::damage ? sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target, p->get_attack_damage()) : 0.f;

                const float magic_shield = target->get_magical_shield();
                const float phys_shield  = target->get_physical_shield();
                const float hp = target->get_hp();

                const float aa_after = std::max(aa - magic_shield, 0.f);
                if (hp + magic_shield < r_dmg && hp + phys_shield > q_dmg + w_dmg + aa_after) {
                    return cast_r();
                }
            }
        }

        if (count >= menu::r_count && menu::combo_r_count) {
            return cast_r();
        }
        return false;
    }

    // Forward decls (defined further down in the laneclear section)
    static bool q_lethal_minion();

    static void combo()
    {
        if (combo_q(menu::combo_q)) return;
        combo_r();
        combo_e();
        combo_w(true, menu::combo_w_missile, menu::combo_w_beam);
    }

    static void harass()
    {
        // BlueZeri::OnGameUpdate harass path: Lasthit() first (Q snipes
        // killable minions), then Combo() on heroes, then W.  Killable
        // minions take priority because the empowered shot is wasted on
        // a chip-damage hero shot.
        if (menu::harass_q && q_lethal_minion()) return;
        if (combo_q(menu::harass_q)) return;
        if (combo_w(true, menu::harass_w_missile, menu::harass_w_beam)) return;
    }

    // =================================================================
    // LANECLEAR / JUNGLE / LASTHIT  (simplified port -- the source uses
    // a bespoke last-hit scheduler that we don't have a direct VEN
    // equivalent for; we approximate with damage-based last-hit + a
    // single nearest minion fallback for push.)
    // =================================================================
    static bool q_lethal_minion()
    {
        if (!spell_ready(SLOT_Q) || !can_action() || !sdk::damage) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        game_object* best = nullptr;
        float best_hp = FLT_MAX;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_target(m, c_q_range)) continue;
            if (!m->is_lane_minion()) continue;
            const float hp = m->get_hp();
            const float dmg = sdk::damage->get_spell_damage(p, m, SLOT_Q);
            if (dmg >= hp && hp < best_hp) { best_hp = hp; best = m; }
        }
        if (!best) return false;
        return cast_q(best->get_position());
    }

    static bool q_plant()
    {
        if (!menu::lane_q_plant || !spell_ready(SLOT_Q) || !can_action()) return false;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_target(m, c_q_range)) continue;
            if (!m->is_plant()) continue;
            return cast_q(m->get_position());
        }
        return false;
    }

    static bool q_jungle()
    {
        if (!menu::jungle_q || !spell_ready(SLOT_Q) || !can_action()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        game_object* best = nullptr;
        float best_mhp = 0.f;
        for (auto* m : g_sdk->object_manager->get_monsters()) {
            if (!is_valid_target(m, c_q_range)) continue;
            if (m->get_max_hp() > best_mhp) { best_mhp = m->get_max_hp(); best = m; }
        }
        if (!best) return false;
        auto pd = predict_q(best, pred_sdk::hitchance::any, /*force_no_col=*/true);
        if (pd.is_valid) return cast_q(pd.cast_position);
        return cast_q(best->get_position());
    }

    static bool q_wave_push()
    {
        if (!menu::lane_q || !spell_ready(SLOT_Q) || !can_action()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto player_pos = p->get_position();

        // Pick the lane minion farthest along but still in Q range
        game_object* best = nullptr;
        float best_d = -1.f;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_target(m, c_q_range)) continue;
            if (!m->is_lane_minion()) continue;
            if (m->get_position().distance(player_pos) <= c_aa_range) continue;
            const float d = m->get_position().distance(player_pos);
            if (d > best_d) { best_d = d; best = m; }
        }
        if (!best) return false;
        return cast_q(best->get_position());
    }

    static void laneclear()
    {
        if (q_plant())        return;
        if (q_lethal_minion()) return;
        if (q_jungle())       return;
        if (menu::lane_q) q_wave_push();
    }

    static void lasthit()
    {
        if (game_time() - last_cast_q < MIN_CAST_GAP) return;
        if (!menu::lasthit_q) return;
        q_lethal_minion();
    }

    // =================================================================
    // WALL JUMP HOTKEY (BlueZeri::HandleJump)
    // =================================================================
    static void handle_jump()
    {
        if (!menu::jump_key_active || !spell_ready(SLOT_E)) return;
        if (!sdk::common || !g_sdk->hud_manager) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const auto player_pos = p->get_position();
        const auto cursor = g_sdk->hud_manager->get_cursor_position();

        const wall_jump_t* best = nullptr;
        float min_cursor = 1000.f;
        float dist_to_point = FLT_MAX;

        for (size_t i = 0; i < WALL_JUMP_COUNT; ++i) {
            const auto& j = WALL_JUMPS[i];
            const float cd = cursor.distance(j.position);
            if (cd < min_cursor) {
                min_cursor = cd;
                dist_to_point = player_pos.distance(j.position);
                best = &j;
            }
        }
        if (!best) return;

        if (dist_to_point < 15.f) {
            cast_e(best->cast_position);
            return;
        }

        // Walk toward the entry point if we're not pathing there already
        auto path = p->get_path();
        bool path_ok = !path.empty() && path.back().distance(best->position) <= 15.f;
        if (!path_ok) {
            sdk::common->issue_order(game_object_order::move_to, best->position, false);
        }
    }

    // =================================================================
    // EVENT CALLBACKS
    // =================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
        if (!p || p->is_dead()) return;

        refresh_state();
        handle_jump();

        // Killsteal is always running (matches BlueZeri::OnGameUpdate)
        if (killsteal_w()) return;

        // Semi-auto W
        if (menu::semi_auto_w_active) {
            combo_w(false, true, true);
        }

        if (!sdk::orbwalker) return;

        if (sdk::orbwalker->combo()) {
            combo();
        } else if (sdk::orbwalker->harass()) {
            harass();
        } else if (sdk::orbwalker->clear() || sdk::orbwalker->fast_clear()) {
            laneclear();
        } else if (sdk::orbwalker->lasthit()) {
            lasthit();
        }
    }

    // Q-reset path: when our own Q lands, try to fire E for a reset.
    static bool __fastcall on_cast_spell(game_object* obj, int slot, math::vector3 /*pos*/, game_object* /*target*/)
    {
        if (!obj) return true;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return true;

        if (slot != SLOT_R) {
            // Block R when no targets within R (BlueZeri::OnCastSpell)
            return true;
        }
        if (!menu::block_r) return true;
        if (s_r_active) return true;

        const auto pos = p->get_position();
        const float r2 = spell_data::r_range * spell_data::r_range;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float dx = h->get_position().x - pos.x;
            const float dz = h->get_position().z - pos.z;
            if (dx * dx + dz * dz <= r2) return true;
        }
        return false;
    }

    // Fire E reset after our own Q completes
    static void __fastcall on_execute_cast(game_object* obj, spell_cast* spell)
    {
        if (!obj || !spell) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (!sdk::orbwalker || !sdk::orbwalker->combo()) return;
        if (spell->get_spell_slot() != SLOT_Q) return;
        combo_e(true);
    }

    // Orbwalker AA gating -- swallow attacks on the unit we just Q'd to
    // avoid double-tapping.
    static bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target) return true;
        if (s_ignore_target_id != 0 && data->target->get_network_id() == s_ignore_target_id) {
            // BlueZeri::OnOrbwalkerAction sets Process=false in this case
            return false;
        }
        return true;
    }

    // Clear ignore when a Q missile finishes flying
    static void __fastcall on_delete(game_object* obj)
    {
        if (!obj || !obj->is_missile()) return;
        const auto name = obj->get_name();
        if (name == "ZeriQMis" || name == "ZeriQMisEmpowered") {
            s_ignore_target_id = 0;
        }
    }

    void __fastcall on_draw()
    {
        auto p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
        if (!p || p->is_dead()) return;
        const auto pos = p->get_position();

        auto circle = [&](const math::vector3& c, float r, uint32_t clr) {
            if (sdk::shaders) sdk::shaders->draw_world_circle(c, r, 2.f, clr);
            else if (g_sdk->renderer) g_sdk->renderer->add_circle_3d(c, r, 2.f, clr);
        };

        if (menu::draw_q && spell_learned(SLOT_Q)) circle(pos, c_q_range, menu::color_q);
        if (menu::draw_w && spell_learned(SLOT_W)) circle(pos, spell_data::w_range,  0xFFF7E1D7u);
        if (menu::draw_e && spell_learned(SLOT_E)) circle(pos, spell_data::e_range,  0xFFDEDBD2u);
        if (menu::draw_r && spell_learned(SLOT_R)) circle(pos, spell_data::r_range,  0xFFEDAFB8u);

        // Wall jump spots
        if (menu::draw_wall_jumps && g_sdk->hud_manager) {
            const auto cursor = g_sdk->hud_manager->get_cursor_position();
            for (size_t i = 0; i < WALL_JUMP_COUNT; ++i) {
                const auto& j = WALL_JUMPS[i];
                if (menu::draw_jumps_only_close && cursor.distance(j.position) > 2000.f) continue;
                circle(j.position, 30.f, 0xFFFFFFFFu);
            }
        }

        // Chain lightning visualisation while R is active
        if (menu::draw_chain_lightning && s_r_active && g_sdk->renderer && sdk::target_selector) {
            for (auto* desired : sdk::target_selector->get_sorted_heroes()) {
                if (!is_valid_target(desired)) continue;

                // Best-effort: find any unit in Q-range that chains to desired
                for (auto* c : g_sdk->object_manager->get_heroes()) {
                    if (!c || c == desired || !c->is_valid() || c->get_team_id() == p->get_team_id()) continue;
                    if (c->get_position().distance(pos) > c_q_range) continue;
                    if (c->get_position().distance(desired->get_position()) > spell_data::q_chain_range) continue;
                    const auto s1 = g_sdk->renderer->world_to_screen(pos);
                    const auto s2 = g_sdk->renderer->world_to_screen(c->get_position());
                    const auto s3 = g_sdk->renderer->world_to_screen(desired->get_position());
                    if (s1.x > 1.f && s2.x > 1.f) g_sdk->renderer->add_line_2d(s1, s2, 2.f, 0xFFFFFF00u);
                    if (s2.x > 1.f && s3.x > 1.f) g_sdk->renderer->add_line_2d(s2, s3, 2.f, 0xFFFFFF00u);
                    break;
                }
            }
        }
    }

    // =================================================================
    // LIFECYCLE
    // =================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("zeri_ps", "MeowAIO - Zeri PS Test meow?");
        if (!s_menu) {
            g_sdk->log_console("[!] zeri_ps: failed to create menu category");
            return;
        }

        // ---- Combo ---------------------------------------------------
        auto m_combo = s_menu->add_sub_category("combo", "Combo");
        m_combo->add_checkbox("q",           "Use Q",            menu::combo_q,
            [](bool v) { menu::combo_q = v; });
        m_combo->add_checkbox("q_ignore_col","Q Ignore Collision", menu::combo_q_ignore_col,
            [](bool v) { menu::combo_q_ignore_col = v; });
        m_combo->add_checkbox("chain",       "Chain Logic (R)",  menu::chain_logic,
            [](bool v) { menu::chain_logic = v; });
        m_combo->add_checkbox("chain_keep",  "  Only to keep R alive", menu::chain_only_keep_alive,
            [](bool v) { menu::chain_only_keep_alive = v; });
        m_combo->add_separator();
        m_combo->add_checkbox("w_missile",   "Use W (Missile)",  menu::combo_w_missile,
            [](bool v) { menu::combo_w_missile = v; });
        m_combo->add_checkbox("w_beam",      "Use W (Beam)",     menu::combo_w_beam,
            [](bool v) { menu::combo_w_beam = v; });
        m_combo->add_checkbox("oorw",        "Only out of Q range", menu::only_out_of_range_w,
            [](bool v) { menu::only_out_of_range_w = v; });
        m_combo->add_separator();
        m_combo->add_checkbox("e_kill",      "E - Killable",      menu::combo_e_killable,
            [](bool v) { menu::combo_e_killable = v; });
        m_combo->add_checkbox("e_col",       "E - Collision",     menu::combo_e_collision,
            [](bool v) { menu::combo_e_collision = v; });
        m_combo->add_combo("e_mode", "E Mode", {"Cursor", "Safe"}, menu::e_mode,
            [](int v) { menu::e_mode = v; });
        m_combo->add_checkbox("only_reset",  "Only Q>E>Q reset",  menu::only_reset,
            [](bool v) { menu::only_reset = v; });
        m_combo->add_checkbox("col_ignore_reset", "  Unless collision", menu::collision_ignore_reset,
            [](bool v) { menu::collision_ignore_reset = v; });
        m_combo->add_slider_int("e_max_near", "Max enemies for E", 1, 5, 1, menu::enemy_limit_e,
            [](int v) { menu::enemy_limit_e = v; });
        m_combo->add_separator();
        m_combo->add_checkbox("r_kill",      "R - Killable",      menu::combo_r_killable,
            [](bool v) { menu::combo_r_killable = v; });
        m_combo->add_checkbox("r_count",     "R - Count",         menu::combo_r_count,
            [](bool v) { menu::combo_r_count = v; });
        m_combo->add_slider_int("r_min",     "  Min Enemies",     1, 5, 1, menu::r_count,
            [](int v) { menu::r_count = v; });

        // ---- Harass --------------------------------------------------
        auto m_harass = s_menu->add_sub_category("harass", "Harass");
        m_harass->add_checkbox("q",         "Use Q",            menu::harass_q,
            [](bool v) { menu::harass_q = v; });
        m_harass->add_checkbox("w_missile", "Use W (Missile)",  menu::harass_w_missile,
            [](bool v) { menu::harass_w_missile = v; });
        m_harass->add_checkbox("w_beam",    "Use W (Beam)",     menu::harass_w_beam,
            [](bool v) { menu::harass_w_beam = v; });

        // ---- Lane/lasthit/jungle ------------------------------------
        auto m_lane = s_menu->add_sub_category("lane", "Lane / Lasthit / Jungle");
        m_lane->add_checkbox("lh_q",      "Lasthit Q",       menu::lasthit_q,
            [](bool v) { menu::lasthit_q = v; });
        m_lane->add_checkbox("lane_q",    "Lane Q (push)",   menu::lane_q,
            [](bool v) { menu::lane_q = v; });
        m_lane->add_checkbox("plant_q",   "Q plants",        menu::lane_q_plant,
            [](bool v) { menu::lane_q_plant = v; });
        m_lane->add_checkbox("jungle_q",  "Jungle Q",        menu::jungle_q,
            [](bool v) { menu::jungle_q = v; });

        // ---- Misc ----------------------------------------------------
        auto m_misc = s_menu->add_sub_category("misc", "Misc");
        m_misc->add_checkbox("ks_w", "Killsteal W", menu::ks_w,
            [](bool v) { menu::ks_w = v; });
        m_misc->add_hotkey("semi_w", "Semi-Auto W", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_auto_w_active = pressed;
                if (hk) menu::semi_auto_w_key = *hk;
            });
        m_misc->add_checkbox("block_r", "Block R if no targets", menu::block_r,
            [](bool v) { menu::block_r = v; });
        m_misc->add_checkbox("save_mana_e", "Save mana for E (R)", menu::save_mana_e,
            [](bool v) { menu::save_mana_e = v; });
        m_misc->add_hotkey("jump_e", "Wall Jump", 'G', false, false,
            [](std::string* hk, bool pressed) {
                menu::jump_key_active = pressed;
                if (hk) menu::jump_key = *hk;
            });

        // ---- Hitchance ----------------------------------------------
        auto m_hc = s_menu->add_sub_category("hc", "Hitchance");
        m_hc->add_combo("hc_w",    "W Missile",
            {"Very Low","Low","Medium","High","Very High","Dashing"}, menu::hitchance_w,
            [](int v) { menu::hitchance_w = v; });
        m_hc->add_combo("hc_beam", "W Beam",
            {"Very Low","Low","Medium","High","Very High","Dashing"}, menu::hitchance_beam,
            [](int v) { menu::hitchance_beam = v; });

        // ---- Drawings -----------------------------------------------
        auto m_draw = s_menu->add_sub_category("draw", "Drawings");
        m_draw->add_checkbox("q", "Draw Q range", menu::draw_q,
            [](bool v) { menu::draw_q = v; });
        m_draw->add_checkbox("w", "Draw W range", menu::draw_w,
            [](bool v) { menu::draw_w = v; });
        m_draw->add_checkbox("e", "Draw E range", menu::draw_e,
            [](bool v) { menu::draw_e = v; });
        m_draw->add_checkbox("r", "Draw R range", menu::draw_r,
            [](bool v) { menu::draw_r = v; });
        m_draw->add_checkbox("chain", "Draw chain lightning", menu::draw_chain_lightning,
            [](bool v) { menu::draw_chain_lightning = v; });
        m_draw->add_checkbox("jumps", "Draw wall jump spots", menu::draw_wall_jumps,
            [](bool v) { menu::draw_wall_jumps = v; });
        m_draw->add_checkbox("jumps_close", "  Only if near cursor", menu::draw_jumps_only_close,
            [](bool v) { menu::draw_jumps_only_close = v; });
        m_draw->add_colorpicker("color_q", "Q color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });

        // ---- Event hooks --------------------------------------------
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::cast_spell,
            reinterpret_cast<void*>(on_cast_spell));
        g_sdk->event_manager->register_callback(event_manager::event::execute_cast,
            reinterpret_cast<void*>(on_execute_cast));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        g_sdk->log_console("[!] [MeowAIO] Zeri (PS port) loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::cast_spell,
                reinterpret_cast<void*>(on_cast_spell));
            g_sdk->event_manager->unregister_callback(event_manager::event::execute_cast,
                reinterpret_cast<void*>(on_execute_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
                reinterpret_cast<void*>(on_delete));
        }

        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        s_laser_ready = s_lightning = s_r_active = s_collision_off = false;
        last_cast_q = last_cast_w = last_cast_e = last_cast_r = 0.f;
        s_ignore_target_id = 0;
        menu::jump_key_active = false;
        menu::semi_auto_w_active = false;

        if (g_sdk) g_sdk->log_console("[!] [MeowAIO] Zeri (PS port) unloaded");
    }
}
