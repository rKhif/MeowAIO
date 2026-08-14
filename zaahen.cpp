#include "zaahen.h"

#include <cstdio>
#include <cmath>
#include <deque>

#include "evade_sdk.hpp"
#include "damage/items.hpp"

// Shared utilities live under ..\meow_shared\ (added to AdditionalIncludeDirectories).
#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace zaahen
{
    static menu_category* s_menu = nullptr;

    static bool         q_empowered = false;
    static bool         q_recast_ready = false;
    static bool         q_recast_pending = false;
    static float        q1_cast_time = -1.f;
    static game_object* q1_target = nullptr;
    static constexpr uint32_t    BUFF_HASH_Q_EMPOWER  = 0u;
    static constexpr uint32_t    BUFF_HASH_Q_RECAST   = 0u;
    static constexpr uint32_t    BUFF_HASH_PASSIVE    = 0u;
    static constexpr const char* BUFF_NAME_Q_EMPOWER  = "ZaahenQ";
    static constexpr const char* BUFF_NAME_Q_RECAST   = "ZaahenQ2";
    static constexpr const char* BUFF_NAME_PASSIVE    = "ZaahenDetermination";

    static bool         q_pending_reset = false;

    // Passive Determination — 0..12 stacks, doubled bonus AD at max.
    static int          passive_stacks = 0;

    // R wind-up tracking — set when our R is observed, cleared after slam delay.
    static bool         is_r_active = false;
    static float        r_active_until = 0.f;

    static bool         is_dashing_e = false;

    static constexpr float E_SWEETSPOT_IDEAL = 290.f;  // mid of [200, 375] outer ring
    static constexpr float E_SWEETSPOT_MIN   = spell_data::e_inner_radius + 20.f;
    static constexpr float E_SWEETSPOT_MAX   = spell_data::e_outer_radius - 20.f;

    static constexpr float MIN_SPELL_DELAY = 0.1f;
    static float           last_spell_cast_time = 0.f;

    // ============================================================================
    // SPELL VALIDATION HELPERS
    // ============================================================================
    bool is_spell_learned(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto s = p->get_spell(slot);
        return s && s->get_level() > 0;
    }

    bool is_spell_ready(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    bool has_enough_mana(int slot)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    bool is_casting()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto ac = p->get_active_spell_cast();
        if (!ac || !ac->get_spell_cast()) return false;
        return ac->get_cast_end_time() > g_sdk->clock_facade->get_game_time();
    }

    // ============================================================================
    // W CAST LOGGING — outcome telemetry for hitchance / predict tuning
    // ============================================================================
    struct w_cast_record
    {
        float          cast_time      = 0.f;
        uint32_t       target_id      = 0;
        std::string    target_name;
        math::vector3  cast_pos{};
        math::vector3  player_pos{};
        math::vector3  target_pos{};   // current pos at cast time
        math::vector3  predicted_pos{};// pred.predicted_position
        math::vector3  target_vel{};
        int            pred_hitchance = 0;
        int            eff_hitchance  = 0;
        float          dist           = 0.f;
        bool           close_range    = false;
        bool           immobile       = false;
        int            hits           = 0;
        bool           miss_logged    = false;
    };
    static std::deque<w_cast_record> s_w_log;

    #define WLOG(...) do { if (menu::w_logging && g_sdk) g_sdk->log_console("[MeowAIO] " __VA_ARGS__); } while (0)

    // Refuse the cast if evade is actively dodging and committing this spell would
    // disrupt evasion, OR if we're standing inside a dangerous spell that's about
    // to land before our cast resolves. Skipped silently when evade SDK is absent.
    static bool safe_to_cast(int slot, float cast_time)
    {
        if (!sdk::evade) return true;
        if (sdk::evade->is_evading() && !sdk::evade->can_spell(slot, cast_time))
            return false;
        if (sdk::evade->is_player_inside_dangerous_spell())
            return false;
        return true;
    }

    float aa_range_to(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return FLT_MAX;
        return p->get_attack_range() + p->get_bounding_radius() + target->get_bounding_radius();
    }

    // ============================================================================
    // TARGETING
    // ============================================================================
    bool is_valid_target(game_object* unit, float range)
    {
        if (!unit || !unit->is_valid() || unit->is_dead() || !unit->is_targetable())
            return false;
        if (!unit->is_visible())
            return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (unit->get_team_id() == p->get_team_id())
            return false;

        if (range != FLT_MAX) {
            float d = p->get_position().distance(unit->get_position());
            if (d > range) return false;
        }
        return true;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    bool cast_q(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        bool ok = sdk::common->cast_spell(SLOT_Q, target);
        if (ok) {
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            q1_target = target;
        }
        return ok;
    }

    bool cast_q_recast()
    {
        if (!sdk::common) return false;

        // Q2 is a self-cast that uses Q1's stored target context
        bool ok = sdk::common->cast_spell(SLOT_Q);
        if (ok) {
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        }
        return ok;
    }

    bool cast_w(game_object* target, bool from_semi)
    {
        const char* tag = from_semi ? "Semi W" : "W";
        if (!target || !sdk::prediction || !sdk::common) { WLOG("%s refuse: null deps", tag); return false; }
        if (!target->is_valid() || target->is_dead() || !target->is_targetable()) {
            WLOG("%s refuse: target invalid", tag);
            return false;
        }
        if (!is_spell_ready(SLOT_W)) { WLOG("%s refuse: W not ready", tag); return false; }
        if (!has_enough_mana(SLOT_W)) { WLOG("%s refuse: insufficient mana", tag); return false; }
        if (is_r_active) { WLOG("%s refuse: R wind-up active", tag); return false; }
        if (!safe_to_cast(SLOT_W, spell_data::w_delay)) { WLOG("%s refuse: evade unsafe", tag); return false; }

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto  player_pos  = p->get_position();
        const auto  target_pos  = target->get_position();
        const float dist        = player_pos.distance(target_pos);
        const bool  close_range = dist <= spell_data::w_range * 0.5f;
        const bool  immobile    = venplus::utility::is_immobile(target);
        const int   eff_hc      = (close_range || immobile)
                                  ? pred_sdk::hitchance::any
                                  : menu::w_hitchance;

        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range;
        spell.radius             = spell_data::w_radius;
        spell.delay              = spell_data::w_delay;
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = eff_hc;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }

        if (!pred.is_valid) { WLOG("%s refuse: pred invalid (target=%s dist=%.0f)",
            tag, target->get_char_name().c_str(), dist); return false; }
        if (pred.hitchance < eff_hc) {
            WLOG("%s refuse: hc=%d < eff=%d (base=%d, close=%d immobile=%d, target=%s dist=%.0f)",
                tag, pred.hitchance, eff_hc, menu::w_hitchance,
                close_range ? 1 : 0, immobile ? 1 : 0,
                target->get_char_name().c_str(), dist);
            return false;
        }

        bool ok = sdk::common->cast_spell(SLOT_W, pred.cast_position);
        if (!ok) { WLOG("%s refuse: cast_spell rejected", tag); return false; }

        const float now = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = now;

        if (menu::w_logging) {
            const auto vel = target->get_velocity();
            const float vmag = std::sqrt(vel.x * vel.x + vel.z * vel.z);
            WLOG("%s cast: target=%s hc=%d/%d dist=%.0f vel=%.0f close=%d immobile=%d cast=(%.0f,%.0f) tpos=(%.0f,%.0f) ppos=(%.0f,%.0f)",
                tag, target->get_char_name().c_str(),
                pred.hitchance, eff_hc, dist, vmag,
                close_range ? 1 : 0, immobile ? 1 : 0,
                pred.cast_position.x, pred.cast_position.z,
                target_pos.x, target_pos.z,
                player_pos.x, player_pos.z);

            w_cast_record rec;
            rec.cast_time      = now;
            rec.target_id      = target->get_network_id();
            rec.target_name    = target->get_char_name();
            rec.cast_pos       = pred.cast_position;
            rec.player_pos     = player_pos;
            rec.target_pos     = target_pos;
            rec.predicted_pos  = pred.predicted_position;
            rec.target_vel     = vel;
            rec.pred_hitchance = pred.hitchance;
            rec.eff_hitchance  = eff_hc;
            rec.dist           = dist;
            rec.close_range    = close_range;
            rec.immobile       = immobile;
            s_w_log.push_back(rec);
            if (s_w_log.size() > 8) s_w_log.pop_front();
        }
        return ok;
    }

    // Server-tick padding added to E dash time so the predicted target pos
    // accounts for the small lag between issuing the cast and the dash starting.
    static constexpr float E_DASH_TIME_BUFFER = 0.066f;

    // Angle offsets (deg) swept by the sweet-spot solver so we can find a
    // landing when the target is strafing perpendicular to the player↔target axis.
    static constexpr float E_ANGLE_OFFSETS_DEG[] = {
        0.f, 15.f, -15.f, 30.f, -30.f, 45.f, -45.f
    };

    // Rotate a vector around the world Y axis (XZ plane). Preserves length.
    static math::vector3 rotate_xz(const math::vector3& v, float rad)
    {
        const float c = std::cos(rad), s = std::sin(rad);
        math::vector3 out;
        out.x = v.x * c - v.z * s;
        out.y = v.y;
        out.z = v.x * s + v.z * c;
        return out;
    }

    // Predict where `target` will be after `dash_time` seconds (+ tick buffer).
    // Wrapped in try/catch — predict_on_path returns vector3 by value so it's safe
    // from the heap-cross trap, but the SDK may still throw internally on bad
    // target state.
    static math::vector3 predict_e_target_pos_at(game_object* target, float dash_time)
    {
        const auto fallback = target ? target->get_position() : math::vector3{};
        if (!target || !sdk::prediction) return fallback;
        if (dash_time < 0.f) dash_time = 0.f;
        try {
            return sdk::prediction->predict_on_path(target, dash_time + E_DASH_TIME_BUFFER, false);
        } catch (...) {
            return fallback;
        }
    }

    // Back-compat wrapper: full-range dash time (~0.39s).
    static math::vector3 predict_e_target_pos(game_object* target)
    {
        return predict_e_target_pos_at(target, spell_data::e_range / spell_data::e_dash_speed);
    }

    // Sweet-spot landing solver.
    // 1. Iterates dash-time vs. axial dash-distance twice so the predicted
    //    position matches the actually-chosen dash duration (fixes consistent
    //    miss when the chosen dash is much shorter than full range).
    // 2. Sweeps a small set of angle offsets around the player↔predicted axis
    //    so a target strafing perpendicular can still be placed in the ring.
    // 3. Scores each candidate by closeness of |landing - predicted| to IDEAL,
    //    tie-broken on smaller dash magnitude (avoid over-committing position).
    // Falls through to a midpoint-window pick if no scored candidate fits.
    // Returns false if no sweet-spot-compliant landing exists — caller refuses cast.
    static bool solve_e_landing(const math::vector3& player_pos,
                                game_object* target,
                                math::vector3& out_landing)
    {
        if (!target) return false;
        const float er = spell_data::e_range;

        // 1. First-pass + 2-iter dash-time convergence on the axis.
        const float full_t = er / spell_data::e_dash_speed;
        math::vector3 predicted = predict_e_target_pos_at(target, full_t);

        for (int it = 0; it < 2; ++it) {
            const math::vector3 d_vec = predicted - player_pos;
            const float d = d_vec.magnitude();
            if (d <= 0.f) break;
            const float cand_a = d - E_SWEETSPOT_IDEAL;
            const float cand_b = d + E_SWEETSPOT_IDEAL;
            float pick = (std::fabs(cand_a) <= std::fabs(cand_b)) ? cand_a : cand_b;
            if (std::fabs(pick) < 1.f) pick = (pick < 0.f ? -1.f : 1.f);
            if (pick >  er) pick =  er;
            if (pick < -er) pick = -er;
            const float new_t = std::fabs(pick) / spell_data::e_dash_speed;
            predicted = predict_e_target_pos_at(target, new_t);
        }

        const math::vector3 delta = predicted - player_pos;
        const float d_axis = delta.magnitude();
        if (d_axis <= 0.f) return false;
        const math::vector3 axis_dir = delta.normalized();

        // 2. Angle scan.
        bool          best_found = false;
        float         best_score = -FLT_MAX;
        float         best_dash_mag = FLT_MAX;
        math::vector3 best_landing{};

        for (float deg : E_ANGLE_OFFSETS_DEG) {
            const float rad = deg * 3.14159265358979323846f / 180.f;
            const math::vector3 dir = rotate_xz(axis_dir, rad);

            // Axial component of (predicted - player) onto this direction.
            const float d_k = dir.x * delta.x + dir.y * delta.y + dir.z * delta.z;
            const float cands[2] = { d_k - E_SWEETSPOT_IDEAL, d_k + E_SWEETSPOT_IDEAL };

            for (float dash : cands) {
                const float mag = std::fabs(dash);
                if (mag < 1.f || mag > er) continue;

                math::vector3 landing;
                landing.x = player_pos.x + dir.x * dash;
                landing.y = player_pos.y + dir.y * dash;
                landing.z = player_pos.z + dir.z * dash;

                const float to_target = landing.distance(predicted);
                if (to_target < E_SWEETSPOT_MIN || to_target > E_SWEETSPOT_MAX) continue;

                const float score = -std::fabs(to_target - E_SWEETSPOT_IDEAL);
                if (score > best_score
                    || (score == best_score && mag < best_dash_mag)) {
                    best_score = score;
                    best_dash_mag = mag;
                    best_landing = landing;
                    best_found = true;
                }
            }
        }

        if (best_found) { out_landing = best_landing; return true; }

        // 3. Fallback: midpoint of any feasible signed window along the axis.
        auto pick_window = [&](float lo, float hi) -> bool {
            if (lo < -er) lo = -er;
            if (hi >  er) hi =  er;
            if (lo > hi) return false;
            float mid = (lo + hi) * 0.5f;
            if (std::fabs(mid) < 1.f) mid = (mid < 0.f ? -1.f : 1.f);
            const float to_target = std::fabs(mid - d_axis);
            if (to_target < E_SWEETSPOT_MIN || to_target > E_SWEETSPOT_MAX) return false;
            out_landing.x = player_pos.x + axis_dir.x * mid;
            out_landing.y = player_pos.y + axis_dir.y * mid;
            out_landing.z = player_pos.z + axis_dir.z * mid;
            return true;
        };
        if (pick_window(d_axis - E_SWEETSPOT_MAX, d_axis - E_SWEETSPOT_MIN)) return true;
        if (pick_window(d_axis + E_SWEETSPOT_MIN, d_axis + E_SWEETSPOT_MAX)) return true;
        return false;
    }

    bool cast_e(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (is_r_active) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const math::vector3 player_pos = p->get_position();
        math::vector3 cast_pos;

        if (menu::e_sweetspot) {
            // Sweet-spot is mandatory when toggle is on: solver runs dash-time-
            // correct prediction + angle scan, refuses if no landing fits the
            // [E_SWEETSPOT_MIN, E_SWEETSPOT_MAX] outer ring window.
            if (!solve_e_landing(player_pos, target, cast_pos)) return false;
        } else {
            // Non-sweetspot: dash full range toward predicted target pos.
            const math::vector3 target_pos = predict_e_target_pos(target);
            const math::vector3 delta = target_pos - player_pos;
            const float dist = delta.magnitude();
            if (dist <= 0.f) return false;
            const math::vector3 dir = delta.normalized();
            const float dash_dist = spell_data::e_range;
            cast_pos.x = player_pos.x + dir.x * dash_dist;
            cast_pos.y = player_pos.y + dir.y * dash_dist;
            cast_pos.z = player_pos.z + dir.z * dash_dist;
        }

        bool ok = sdk::common->cast_spell(SLOT_E, cast_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // Internal: build the linear-W spell_data shared by combo/farm casts.
    // Crash-safe (no vector / function members set — see veigar.cpp:2019).
    static pred_sdk::spell_data make_w_spell_data(int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_W;
        spell.range              = spell_data::w_range;
        spell.radius             = spell_data::w_radius;
        spell.delay              = spell_data::w_delay;
        spell.projectile_speed   = spell_data::w_speed;
        spell.expected_hitchance = hitchance;
        return spell;
    }

    static bool cast_w_aoe(game_object* primary, bool from_semi = false)
    {
        const char* tag = from_semi ? "Semi W" : "W AOE";
        if (!primary || !sdk::prediction || !sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (is_r_active) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto player_pos = p->get_position();
        const auto heroes = g_sdk->object_manager->get_heroes();

        int           best_hits = 0;
        math::vector3 best_pos{};
        bool          found = false;

        for (auto* h : heroes) {
            if (!is_valid_target(h, spell_data::w_range + spell_data::w_radius)) continue;

            const float d = player_pos.distance(h->get_position());
            const bool  close_range = d <= spell_data::w_range * 0.5f;
            const bool  immobile    = venplus::utility::is_immobile(h);
            const int   eff_hc      = (close_range || immobile)
                                      ? pred_sdk::hitchance::any
                                      : menu::w_hitchance;
            auto spell = make_w_spell_data(eff_hc);

            pred_sdk::pred_data pred{};
            try { pred = sdk::prediction->predict(h, spell); }
            catch (...) { pred.is_valid = false; }
            if (!pred.is_valid || pred.hitchance < eff_hc) continue;

            const auto cast_pos = pred.cast_position;
            const float seg_dx = cast_pos.x - player_pos.x;
            const float seg_dz = cast_pos.z - player_pos.z;
            const float seg_len_sq = seg_dx * seg_dx + seg_dz * seg_dz;
            if (seg_len_sq <= 1.f) continue;

            int hits = 1;  // primary aim hits `h` by construction
            bool includes_ts = (h == primary);
            for (auto* other : heroes) {
                if (other == h || !is_valid_target(other, spell_data::w_range + spell_data::w_radius)) continue;

                const auto opos = other->get_position();
                const float rel_dx = opos.x - player_pos.x;
                const float rel_dz = opos.z - player_pos.z;
                float t = (rel_dx * seg_dx + rel_dz * seg_dz) / seg_len_sq;
                if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;

                const float closest_x = player_pos.x + seg_dx * t;
                const float closest_z = player_pos.z + seg_dz * t;
                const float ddx = opos.x - closest_x;
                const float ddz = opos.z - closest_z;
                const float d = std::sqrt(ddx * ddx + ddz * ddz);
                const float allow = spell_data::w_radius + other->get_bounding_radius();
                if (d <= allow) {
                    ++hits;
                    if (other == primary) includes_ts = true;
                }
            }

            // Respect TS: only accept lines whose hitbox sweep catches the TS-preferred
            // target. If no line can include the priority, callers fall back to single-
            // target cast_w (which already uses the TS pick).
            if (!includes_ts) continue;

            if (hits > best_hits) {
                best_hits = hits;
                best_pos = cast_pos;
                found = true;
            }
        }

        if (!found) { WLOG("%s refuse: no valid line found", tag); return false; }

        bool ok = sdk::common->cast_spell(SLOT_W, best_pos);
        if (!ok) { WLOG("%s refuse: cast_spell rejected", tag); return false; }

        const float now = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = now;

        if (menu::w_logging) {
            WLOG("%s cast: hits=%d cast=(%.0f,%.0f) ppos=(%.0f,%.0f)",
                tag, best_hits, best_pos.x, best_pos.z, player_pos.x, player_pos.z);

            w_cast_record rec;
            rec.cast_time      = now;
            rec.target_id      = primary ? primary->get_network_id() : 0;
            rec.target_name    = primary
                ? std::string(from_semi ? "semi:" : "aoe:") + primary->get_char_name()
                : std::string(from_semi ? "semi" : "aoe");
            rec.cast_pos       = best_pos;
            rec.player_pos     = player_pos;
            rec.dist           = primary ? player_pos.distance(primary->get_position()) : 0.f;
            s_w_log.push_back(rec);
            if (s_w_log.size() > 8) s_w_log.pop_front();
        }
        return ok;
    }

    static bool cast_w_farm(int min_hits, spell_farm_flag flags = spell_farm_flag::none)
    {
        if (!sdk::orbwalker || !sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;

        auto spell = make_w_spell_data(pred_sdk::hitchance::any);

        pred_sdk::pred_data pred{};
        try
        {
            pred = sdk::orbwalker->spell_farm(spell, min_hits, flags,
                                              dmg_sdk::damage_type::physical);
        }
        catch (...)
        {
            pred.is_valid = false;
        }

        if (!pred.is_valid) return false;

        bool ok = sdk::common->cast_spell(SLOT_W, pred.cast_position);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    
    // Sweet-spot-aware E farm: place landing such that mobs cluster in the
   
    static bool cast_e_farm(int min_hits)
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (is_dashing_e || is_r_active) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto  player_pos = p->get_position();
        const float er         = spell_data::e_range;

        int           best_hits = 0;
        math::vector3 best_landing{};

        auto try_with = [&](std::span<game_object*> mobs, bool jungle_only)
        {
            for (auto* anchor : mobs) {
                if (!anchor || !anchor->is_valid() || anchor->is_dead()
                    || !anchor->is_targetable()) continue;
                if (anchor->get_team_id() == p->get_team_id()) continue;
                if (jungle_only ? !anchor->is_monster() : !anchor->is_lane_minion()) continue;

                const auto delta = anchor->get_position() - player_pos;
                const float d = delta.magnitude();
                if (d <= 0.f) continue;

                const auto dir = delta.normalized();

                // Two sweet-spot candidate dash distances (signed, can back-pedal)
                const float candidates[2] = {
                    d - E_SWEETSPOT_IDEAL,
                    d + E_SWEETSPOT_IDEAL,
                };

                for (float dash : candidates) {
                    const float mag = std::fabs(dash);
                    if (mag < 1.f || mag > er) continue;

                    math::vector3 landing;
                    landing.x = player_pos.x + dir.x * dash;
                    landing.y = player_pos.y + dir.y * dash;
                    landing.z = player_pos.z + dir.z * dash;

                    // Count mobs landing in the sweet-spot ring [INNER, OUTER]
                    int hits = 0;
                    for (auto* m : mobs) {
                        if (!m || !m->is_valid() || m->is_dead()) continue;
                        if (m->get_team_id() == p->get_team_id()) continue;
                        if (jungle_only ? !m->is_monster() : !m->is_lane_minion()) continue;
                        const float ld = landing.distance(m->get_position());
                        if (ld >= spell_data::e_inner_radius && ld <= spell_data::e_outer_radius)
                            ++hits;
                    }

                    if (hits > best_hits) {
                        best_hits = hits;
                        best_landing = landing;
                    }
                }
            }
        };

        try_with(g_sdk->object_manager->get_minions(),  /*jungle_only=*/false);
        try_with(g_sdk->object_manager->get_monsters(), /*jungle_only=*/true);

        if (best_hits < min_hits) return false;

        bool ok = sdk::common->cast_spell(SLOT_E, best_landing);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // Multi-hit hero sweet-spot scan for combo (teamfight mode).
    static bool cast_e_combo_aoe(game_object* primary)
    {
        if (!primary || !sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (is_r_active || is_dashing_e) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto  player_pos = p->get_position();
        const float er         = spell_data::e_range;
        const auto  heroes     = g_sdk->object_manager->get_heroes();

        struct Pred { game_object* h; math::vector3 pos; };
        static std::vector<Pred> preds;  // reused across calls — one-time alloc
        preds.clear();
        for (auto* h : heroes) {
            if (!is_valid_target(h, er + spell_data::e_outer_radius)) continue;
            preds.push_back({ h, predict_e_target_pos(h) });
        }
        if (preds.size() < 2) return false;

        int           best_hits = 0;
        float         best_depth = 0.f;   // tie-break: prefer landings deeper in ring
        math::vector3 best_landing{};

        for (const auto& anchor : preds) {
            const math::vector3 anchor_delta = anchor.pos - player_pos;
            const float d_a = anchor_delta.magnitude();
            if (d_a <= 0.f) continue;
            const math::vector3 axis_dir = anchor_delta.normalized();

            for (float deg : E_ANGLE_OFFSETS_DEG) {
                const float rad = deg * 3.14159265358979323846f / 180.f;
                const math::vector3 dir = rotate_xz(axis_dir, rad);

                // Project anchor offset onto this direction so the two signed
                // candidates still place THIS anchor near the ring on the
                // rotated axis.
                const float d_k = dir.x * anchor_delta.x
                                + dir.y * anchor_delta.y
                                + dir.z * anchor_delta.z;
                const float candidates[2] = {
                    d_k - E_SWEETSPOT_IDEAL,
                    d_k + E_SWEETSPOT_IDEAL,
                };

                for (float dash : candidates) {
                    const float mag = std::fabs(dash);
                    if (mag < 1.f || mag > er) continue;

                    math::vector3 landing;
                    landing.x = player_pos.x + dir.x * dash;
                    landing.y = player_pos.y + dir.y * dash;
                    landing.z = player_pos.z + dir.z * dash;

                    int   hits  = 0;
                    float depth = 0.f;
                    for (const auto& q : preds) {
                        const float ld = landing.distance(q.pos);
                        if (ld >= spell_data::e_inner_radius && ld <= spell_data::e_outer_radius) {
                            ++hits;
                            // Closer to IDEAL = deeper in the ring sweet zone.
                            depth += (E_SWEETSPOT_MAX - std::fabs(ld - E_SWEETSPOT_IDEAL));
                        }
                    }

                    if (hits > best_hits
                        || (hits == best_hits && depth > best_depth)) {
                        best_hits = hits;
                        best_depth = depth;
                        best_landing = landing;
                    }
                }
            }
        }

        if (best_hits < 2) return false;

        bool ok = sdk::common->cast_spell(SLOT_E, best_landing);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static float mana_pct()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        float mx = p->get_max_par();
        if (mx <= 0.f) return 0.f;
        return (p->get_par() / mx) * 100.f;
    }

    bool cast_r(game_object* target, bool force)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (is_dashing_e) return false;
        if (!force && !safe_to_cast(SLOT_R, spell_data::r_cast_time)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const float dist = p->get_position().distance(target->get_position());
        if (dist > spell_data::r_range + target->get_bounding_radius()) {
            WLOG("R refuse: out of range (dist=%.0f, max=%.0f)",
                dist, spell_data::r_range + target->get_bounding_radius());
            return false;
        }

        if (!force && menu::r_only_max_stacks &&
            passive_stacks < spell_data::passive_max_stacks) {
            WLOG("R refuse: stacks=%d < %d", passive_stacks, spell_data::passive_max_stacks);
            return false;
        }

        if (!force && menu::r_only_killable && sdk::damage && sdk::health_prediction) {
            if (venplus::utility::has_anti_burst_buff(target)) {
                WLOG("R refuse: anti-burst buff on %s", target->get_char_name().c_str());
                return false;
            }
            if (target->has_buff_of_type(buff_type::spell_shield)) {
                WLOG("R refuse: spell shield on %s", target->get_char_name().c_str());
                return false;
            }

            const float distance = p->get_position().distance(target->get_position());
            const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
            meow::item_damage::ability_proc_context items{};
            items.horizon_focus_active = horizon;
            meow::item_damage::ability_rune_context runes{};
            runes.cast_distance = distance;
            const float dmg = meow::item_damage::apply_physical_damage_modifiers(
                p, target, sdk::damage->get_spell_damage(p, target, SLOT_R), 0, horizon)
                + meow::item_damage::on_ability_damage(p, target, items)
                + meow::item_damage::on_ability_rune_damage(p, target, runes);
            const float hit_time = g_sdk->clock_facade->get_game_time()
                                 + spell_data::r_cast_time + spell_data::r_slam_delay;
            const float hp = sdk::health_prediction->get_predicted_health(target, hit_time)
                           + target->get_all_shield();
            if (hp <= 0.f || hp > dmg) {
                WLOG("R refuse: not killable (pred_hp+shield=%.0f vs dmg=%.0f, target=%s)",
                    hp, dmg, target->get_char_name().c_str());
                return false;
            }
        }

        if (!force && menu::r_require_hits) {
            const float radius = spell_data::r_radius + 25.f;
            const auto land_pos = target->get_position();
            int hits = 0;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                if (h->get_position().distance(land_pos) <= radius)
                    ++hits;
            }
            if (hits < menu::r_min_targets) {
                WLOG("R refuse: hits=%d < min=%d (target=%s)",
                    hits, menu::r_min_targets, target->get_char_name().c_str());
                return false;
            }
        }

        bool ok = sdk::common->cast_spell(SLOT_R, target->get_position());
        if (ok) {
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            WLOG("R cast%s: target=%s dist=%.0f stacks=%d",
                force ? " (semi)" : "",
                target->get_char_name().c_str(), dist, passive_stacks);
        }
        return ok;
    }

    // ============================================================================
    // COMBO
    //   Q1 is now driven by on_before_attack (proper AA-reset path),
    //   so combo() only handles Q2 / R / E / W priority.
    // ============================================================================
    void combo()
    {
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;
        if (is_casting()) return;

        float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!sdk::target_selector) return;

        // Pick a hero target within W range (largest gate)
        auto target = sdk::target_selector->get_hero_target(
            [player](game_object* h) {
                if (!h || !h->is_valid() || h->is_dead()) return false;
                if (!h->is_targetable() || !h->is_visible()) return false;
                float d = player->get_position().distance(h->get_position());
                return d <= spell_data::w_range;
            });
        if (!target) return;

        float dist = player->get_position().distance(target->get_position());

        // Q2 recast (knockup) — burn the window ASAP when target is in melee.
        // Authoritative: buff "ZaahenQ2" is up. Fallback: timestamp window.
        if (menu::use_q_recast_combo) {
            float aa_reach = aa_range_to(target) + spell_data::q_range_bonus;
            bool window_open_by_time =
                q_recast_pending &&
                (now - q1_cast_time) >= spell_data::q_recast_min_delay &&
                (now - q1_cast_time) <= spell_data::q_recast_min_delay + spell_data::q_recast_window;
            if ((q_recast_ready || window_open_by_time) && dist <= aa_reach) {
                if (cast_q_recast()) return;
            }
        }

        // R — engage / kill / multi-hit (gated inside cast_r)
        if (menu::use_r_combo && !is_dashing_e) {
            if (cast_r(target)) return;
        }

        // Mode-aware E/W ordering
        auto try_e = [&]() -> bool {
            if (!menu::use_e_combo || is_dashing_e) return false;
            if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
            if (dist > spell_data::e_range + spell_data::e_outer_radius) return false;
            if (menu::combo_teamfight && cast_e_combo_aoe(target)) return true;
            return cast_e(target);
        };

        auto try_w = [&]() -> bool {
            if (!menu::use_w_combo || is_dashing_e || is_r_active) return false;
            if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
            if (dist > spell_data::w_range) return false;
            // In teamfight mode, prefer the line that catches multiple enemies.
            if (menu::combo_teamfight) {
                if (cast_w_aoe(target)) return true;
            }
            return cast_w(target);
        };

        if (menu::combo_teamfight) {
      
            if (try_e()) return;

            const bool e_usable = !is_dashing_e
                                && is_spell_ready(SLOT_E)
                                && has_enough_mana(SLOT_E);
            if (e_usable) return;          // E ready but out of range — wait, don't fire W

            if (try_w()) return;
        } else {
            // Normal: W first (poke / setup / pull), then E (close gap)
            
            if (dist <= spell_data::e_range) {
                if (try_e()) return;
                if (try_w()) return;
            } else {
                if (try_w()) return;
                if (try_e()) return;
            }
        }
    }
            //Farm
    void clear()
    {
        if (!sdk::orbwalker) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;
        if (!sdk::orbwalker->is_spell_farm_enabled()) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;
        if (is_casting() || is_dashing_e) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (menu::use_w_clear) {
            if (cast_w_farm(menu::clear_min_hits)) return;
        }
        if (menu::use_e_clear) {
            cast_e_farm(menu::e_clear_min_hits);
        }
    }

    void fast_clear()
    {
        if (!sdk::orbwalker) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;
        if (is_casting() || is_dashing_e) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (menu::use_w_fast_clear) {
            if (cast_w_farm(menu::clear_min_hits)) return;
        }
        if (menu::use_e_fast_clear) {
            cast_e_farm(menu::e_clear_min_hits);
        }
    }

   
    // INSTANT Q — fire Q1 / Q2 the moment they're available, regardless of mode.
    bool instant_q()
    {
        if (!sdk::common) return false;
        if (is_casting() || is_dashing_e || is_r_active) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return false;
        if (!is_spell_learned(SLOT_Q)) return false;

        const float aa_reach = p->get_attack_range() + p->get_bounding_radius()
                             + spell_data::q_range_bonus;

        auto find_nearest_hero_in = [&](float reach) -> game_object* {
            game_object* best = nullptr;
            float best_d = FLT_MAX;
            const auto pos = p->get_position();
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                const float d = pos.distance(h->get_position()) - h->get_bounding_radius();
                if (d > reach) continue;
                if (d < best_d) { best_d = d; best = h; }
            }
            return best;
        };

        // Q2 is hero-only: knockup is wasted on a minion. Always look for a hero.
        if (menu::q_mode == 2) {
            const bool window_ok =
                q_recast_ready ||
                (q_recast_pending &&
                 (now - q1_cast_time) >= spell_data::q_recast_min_delay &&
                 (now - q1_cast_time) <= spell_data::q_recast_min_delay + spell_data::q_recast_window);
            if (window_ok) {
                if (auto* h = find_nearest_hero_in(aa_reach)) {
                    (void)h;
                    if (cast_q_recast()) return true;
                }
            }
        }

        // Q1: pick the orbwalker's current target so we follow the same mode rules
        // (heroes in combo/harass, minions in clear, anything in fast_clear). Falls
        // back to nearest-hero scan if orbwalker isn't pointing at anything yet.
        if (menu::q_mode == 2) {
            if (q_empowered || q_recast_ready || q_recast_pending) return false;
            if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

            game_object* target = sdk::orbwalker ? sdk::orbwalker->get_current_target() : nullptr;
            if (target) {
                if (!target->is_valid() || target->is_dead() || !target->is_targetable())
                    target = nullptr;
                else if (target->get_team_id() == p->get_team_id())
                    target = nullptr;
                else {
                    const float d = p->get_position().distance(target->get_position())
                                  - target->get_bounding_radius();
                    if (d > aa_reach) target = nullptr;
                }
            }
            if (!target) target = find_nearest_hero_in(aa_reach);

            if (target && cast_q(target)) return true;
        }

        return false;
    }

    // ============================================================================
    // SEMI-W — manual hold-to-cast: predict W on best hero in range.
    //   Tries the multi-hit AOE line first (catches most enemies); falls back to
    //   single-target predict on the best hero if no AOE candidate is valid.
    // ============================================================================
    void semi_w()
    {
        if (!sdk::common || !sdk::prediction) return;
        if (is_casting() || is_dashing_e || is_r_active) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return;
        if (!sdk::target_selector) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        // Prefer the cursor-hovered enemy hero when valid; fall back to the TS pick.
        game_object* target = nullptr;
        if (g_sdk->hud_manager) {
            auto* h = g_sdk->hud_manager->get_hovered_target();
            if (h && is_valid_target(h, spell_data::w_range) && h->is_hero())
                target = h;
        }
        if (!target) {
            target = sdk::target_selector->get_hero_target(
                [p](game_object* h) {
                    if (!h || !h->is_valid() || h->is_dead()) return false;
                    if (!h->is_targetable() || !h->is_visible()) return false;
                    const float d = p->get_position().distance(h->get_position());
                    return d <= spell_data::w_range;
                });
        }
        if (!target) return;

        if (cast_w_aoe(target, /*from_semi=*/true)) return;
        cast_w(target, /*from_semi=*/true);
    }

    // ============================================================================
    // FLASH + E + W COMBO MACRO
    //   Rising-edge hotkey: press once to Flash forward, sweet-spot E onto best
    //   hero, follow up with W. Three-stage state machine driven by on_update.
    // ============================================================================
    static constexpr float FLASH_RANGE = 425.f;

    static int      s_flash_slot_cache = -1;
    static int      s_fcs_state    = 0;  // 0=idle, 1=post-flash, 2=post-E
    static float    s_fcs_time     = 0.f;
    static uint32_t s_fcs_target_id = 0;

    static int detect_flash_slot()
    {
        if (s_flash_slot_cache >= 0) return s_flash_slot_cache;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return -1;
        for (int slot = 4; slot <= 5; ++slot) {
            auto sp = p->get_spell(slot);
            if (!sp || sp->get_level() <= 0) continue;
            auto sd = sp->get_data();
            if (!sd) continue;
            auto ssd = sd->get_static_data();
            if (!ssd) continue;
            const char* name = ssd->get_name();
            if (name && _stricmp(name, "SummonerFlash") == 0) {
                s_flash_slot_cache = slot;
                return slot;
            }
        }
        return -1;
    }

    static void flash_ew_start()
    {
        if (s_fcs_state != 0) return;
        if (!sdk::common || !sdk::target_selector) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting() || is_dashing_e || is_r_active) return;

        const int flash = detect_flash_slot();
        if (flash < 0) return;
        auto fs = p->get_spell(flash);
        if (!fs || fs->get_cooldown() > 0.f) return;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;

        const float reach_max = FLASH_RANGE + spell_data::e_range + spell_data::e_outer_radius;
        auto* target = sdk::target_selector->get_hero_target(
            [p, reach_max](game_object* h) {
                if (!h || !h->is_valid() || h->is_dead()) return false;
                if (!h->is_targetable() || !h->is_visible()) return false;
                return p->get_position().distance(h->get_position()) <= reach_max;
            });
        if (!target) return;

        const auto pp = p->get_position();
        const auto delta = target->get_position() - pp;
        const float d = delta.magnitude();
        if (d <= 0.f) return;
        const auto dir = delta.normalized();
        const float fdist = d < FLASH_RANGE ? d : FLASH_RANGE;
        math::vector3 flash_pos;
        flash_pos.x = pp.x + dir.x * fdist;
        flash_pos.y = pp.y + dir.y * fdist;
        flash_pos.z = pp.z + dir.z * fdist;

        if (!sdk::common->cast_spell(flash, flash_pos)) return;

        s_fcs_state = 1;
        s_fcs_time  = g_sdk->clock_facade->get_game_time();
        s_fcs_target_id = target->get_network_id();
        last_spell_cast_time = s_fcs_time;
    }

    static void flash_ew_tick()
    {
        if (s_fcs_state == 0) return;

        const float now = g_sdk->clock_facade->get_game_time();
        auto* target = s_fcs_target_id
                       ? g_sdk->object_manager->get_object_by_network_id(s_fcs_target_id)
                       : nullptr;

        // Abort on target loss / timeout
        if (!target || !target->is_valid() || target->is_dead() || (now - s_fcs_time > 2.5f)) {
            s_fcs_state = 0;
            s_fcs_target_id = 0;
            return;
        }

        if (s_fcs_state == 1) {
            // Post-flash: 0.5s gap before E so the engine has fully committed the
            // Flash teleport, the player's network position has updated, and the
            // user can mentally see the reposition before the dash kicks in.
            if (now - s_fcs_time < 0.1f) return;
            if (cast_e(target)) {
                s_fcs_state = 2;
                s_fcs_time  = now;
            } else {
                s_fcs_state = 0;
                s_fcs_target_id = 0;
            }
            return;
        }

        if (s_fcs_state == 2) {
            // Post-E: wait for dash to land, then fire W
            const float dash_time = spell_data::e_range / spell_data::e_dash_speed;
            if (now - s_fcs_time < dash_time + 0.05f) return;
            cast_w(target);
            s_fcs_state = 0;
            s_fcs_target_id = 0;
        }
    }

    // SEMI-R
    void semi_r()
    {
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;
        if (is_casting() || is_dashing_e) return;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return;
        if (!sdk::target_selector) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        const float r_reach = spell_data::r_range;
        game_object* target = nullptr;
        if (g_sdk->hud_manager) {
            auto* h = g_sdk->hud_manager->get_hovered_target();
            if (h && is_valid_target(h, r_reach + h->get_bounding_radius()) && h->is_hero())
                target = h;
        }
        if (!target) {
            target = sdk::target_selector->get_hero_target(
                [player, r_reach](game_object* h) {
                    if (!h || !h->is_valid() || h->is_dead()) return false;
                    if (!h->is_targetable() || !h->is_visible()) return false;
                    float d = player->get_position().distance(h->get_position());
                    return d <= r_reach + h->get_bounding_radius();
                });
        }
        if (!target) return;

        cast_r(target, /*force=*/true);
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        const float now = g_sdk->clock_facade->get_game_time();

        // Refresh Determination stacks each tick — event::buff_gain only fires on
        // initial buff acquisition; subsequent stack increments do NOT re-fire it,
        // so the on_buff_gain path freezes at the starting count.
        {
            std::string name = BUFF_NAME_PASSIVE;
            if (auto* b = player->get_buff_by_name(name); b && b->is_active()) {
                const int s = (int)b->get_stacks();
                passive_stacks = s > 0 ? s : (int)b->get_count();
            } else {
                passive_stacks = 0;
            }
        }

        // Expire stale Q recast pending state if window has passed
        if (q_recast_pending) {
            float expiry = spell_data::q_recast_min_delay + spell_data::q_recast_window + 0.5f;
            if (now - q1_cast_time > expiry) {
                q_recast_pending = false;
                q1_target = nullptr;
            }
        }

        if (is_r_active && now > r_active_until) {
            is_r_active = false;
        }

        // Prune W cast log: any record older than 1.5s with 0 hits is a confirmed miss.
        if (menu::w_logging && !s_w_log.empty()) {
            for (auto& r : s_w_log) {
                if (r.miss_logged || r.hits > 0) continue;
                if (now - r.cast_time < 1.5f) continue;
                r.miss_logged = true;
                WLOG("MISS: target=%s dist=%.0f hc=%d/%d close=%d immobile=%d",
                    r.target_name.c_str(), r.dist,
                    r.pred_hitchance, r.eff_hitchance,
                    r.close_range ? 1 : 0, r.immobile ? 1 : 0);
            }
            while (!s_w_log.empty() && now - s_w_log.front().cast_time > 5.0f)
                s_w_log.pop_front();
        }

        // Live passive Determination stack polling.
        // event::buff_gain only fires on first acquisition — subsequent stack
        // increases never re-fire it. Read the buff each tick for the current count.
        // (Static string reused across calls so we don't reallocate per frame.)
        {
            static std::string s_passive_name_long  = "ZaahenDetermination";
            static std::string s_passive_name_short = "Determination";
            buff_instance* b = player->get_buff_by_name(s_passive_name_long);
            if (!b) b = player->get_buff_by_name(s_passive_name_short);
            if (b && b->is_active()) {
                const int s = (int)b->get_stacks();
                passive_stacks = s > 0 ? s : (int)b->get_count();
            } else {
                passive_stacks = 0;
            }
        }

        // Drive the Flash+E+W macro state machine each tick (no-op when idle).
        flash_ew_tick();

        if (menu::semi_r_active) {
            semi_r();
            return;  // don't fall through into combo/farm on the same tick
        }

        if (menu::semi_w_active) {
            semi_w();
            return;
        }

        // Instant Q runs before mode dispatch — fire Q1/Q2 the moment they're up,
        // regardless of orbwalker state. Returns true if a cast went out.
        if (menu::q_mode == 2 && sdk::orbwalker) {
            const bool mode_ok = sdk::orbwalker->combo()
                              || sdk::orbwalker->fast_clear()
                              || sdk::orbwalker->clear()
                              || sdk::orbwalker->harass();
            if (mode_ok && instant_q()) return;
        }

        if (!sdk::orbwalker) return;

        if (sdk::orbwalker->combo())            combo();
        else if (sdk::orbwalker->fast_clear())  fast_clear();
        else if (sdk::orbwalker->clear())       clear();
    }

    // ============================================================================
    // DRAWINGS
    // ============================================================================
    void __fastcall on_draw()
    {
        if (!g_sdk || !g_sdk->object_manager) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || player->is_dead()) return;

        const auto player_pos = player->get_position();

        auto draw_circle = [&](math::vector3 const& center, float range, uint32_t clr)
            {
                if (sdk::shaders)
                    sdk::shaders->draw_world_circle(center, range, 2.f, clr);
                else if (g_sdk->renderer)
                    g_sdk->renderer->add_circle_3d(center, range, 2.f, clr);
            };

        // Returns clr if spell is up; dimmed clr if on CD; 0 if hidden
        auto get_draw_color = [&](int slot, uint32_t clr) -> uint32_t
            {
                auto sp = player->get_spell(slot);
                if (!sp || sp->get_level() == 0) return 0u;
                if (menu::draw_only_ready && sp->get_cooldown() > 0.f) return 0u;
                return sp->get_cooldown() <= 0.f ? clr : ((clr & 0x00FFFFFFu) | 0x40000000u);
            };

        // Q reach = current AA range + bounding + Q bonus
        if (menu::draw_q_range) {
            const uint32_t c = get_draw_color(SLOT_Q, menu::color_q);
            if (c) {
                float q_reach = player->get_attack_range() + player->get_bounding_radius()
                              + spell_data::q_range_bonus;
                draw_circle(player_pos, q_reach, c);
            }
        }

        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(player_pos, spell_data::w_range, c);
        }

        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(player_pos, spell_data::e_range, c);
        }

        // E sweet-spot rings (inner + outer ring of the slash AOE)
        if (menu::draw_e_sweetspot) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) {
                const uint32_t dim = (c & 0x00FFFFFFu) | 0x40000000u;
                draw_circle(player_pos, spell_data::e_inner_radius, dim);
                draw_circle(player_pos, spell_data::e_outer_radius, dim);
            }
        }

        // E terrain reachability line — toward cursor, length = max pathable dash.
        // Only active when at least one direction within E range hits a wall,
        // so the indicator stays out of the way in open ground.
        if (menu::draw_e_terrain && g_sdk->nav_mesh && g_sdk->hud_manager) {
            bool near_wall = false;
            {
                constexpr int   PROBE_N = 12;
                constexpr float PROBE_R = 50.f; // "very close" — only fire when a wall is right next to us
                for (int i = 0; i < PROBE_N && !near_wall; ++i) {
                    const float a = (float)i * (6.2831853f / (float)PROBE_N);
                    math::vector3 s{};
                    s.x = player_pos.x + std::cos(a) * PROBE_R;
                    s.y = player_pos.y;
                    s.z = player_pos.z + std::sin(a) * PROBE_R;
                    if (!g_sdk->nav_mesh->is_pathable(s)) near_wall = true;
                }
            }

            const auto cursor = g_sdk->hud_manager->get_cursor_position();
            math::vector3 delta = cursor - player_pos;
            const float dlen = std::sqrt(delta.x * delta.x + delta.z * delta.z);
            if (near_wall && dlen > 1.f) {
                const float inv = 1.f / dlen;
                const float dx = delta.x * inv, dz = delta.z * inv;
                const float er = spell_data::e_range;

                // Walk from full range back until we find a pathable landing.
                float max_dash = 0.f;
                for (float d = er; d >= 50.f; d -= 25.f) {
                    math::vector3 cand{};
                    cand.x = player_pos.x + dx * d;
                    cand.y = player_pos.y;
                    cand.z = player_pos.z + dz * d;
                    if (g_sdk->nav_mesh->is_pathable(cand)) { max_dash = d; break; }
                }

                if (max_dash > 0.f) {
                    math::vector3 landing{};
                    landing.x = player_pos.x + dx * max_dash;
                    landing.y = player_pos.y;
                    landing.z = player_pos.z + dz * max_dash;

                    const bool full   = max_dash >= er - 0.5f;
                    const uint32_t clr = full ? 0xFF00FF7Fu : 0xFFFFC020u; // green / amber

                    if (sdk::shaders)
                        sdk::shaders->draw_world_segment(player_pos, landing, 4.f, clr, 0);
                    draw_circle(landing, 28.f, clr);
                }
            }
        }

        if (menu::draw_r_range) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(player_pos, spell_data::r_range, c);
        }

        // Combo mode indicator — text above player's head, color-coded
        if (menu::draw_combo_mode && g_sdk->renderer) {
            auto screen = g_sdk->renderer->world_to_screen(player_pos);
            const char* label = menu::combo_teamfight ? "TEAMFIGHT (E -> W)" : "NORMAL (W -> E)";
            const uint32_t color = menu::combo_teamfight ? 0xFFFF4500u
                                                          : 0xFF00FF7Fu;
            g_sdk->renderer->add_text(label, 16.f,
                { screen.x, screen.y - 80.f }, 1, color);
        }

        if (menu::draw_q_mode && g_sdk->renderer) {
            auto screen = g_sdk->renderer->world_to_screen(player_pos);
            const char* labels[] = { "Q: Before Attack", "Q: AA Reset", "Q: Instant" };
            const uint32_t colors[] = { 0xFFFFD700u, 0xFF87CEEBu, 0xFFFF69B4u };
            const int idx = (menu::q_mode >= 0 && menu::q_mode < 3) ? menu::q_mode : 0;
            g_sdk->renderer->add_text(labels[idx], 14.f,
                { screen.x, screen.y - 62.f }, 1, colors[idx]);
        }

        // R-killable label — floating "R KILL" above any enemy hero R would execute.
        // Mirrors cast_r's actual gates so what the label shows == what would fire.
        if (menu::draw_r_killable && g_sdk->renderer && sdk::damage && sdk::health_prediction) {
            auto r_sp = player->get_spell(SLOT_R);
            const bool max_stacks_gate_ok =
                !menu::r_only_max_stacks || passive_stacks >= spell_data::passive_max_stacks;
            if (max_stacks_gate_ok && r_sp && r_sp->get_level() > 0 && r_sp->get_cooldown() <= 0.f) {
                const float hit_time = g_sdk->clock_facade->get_game_time()
                                     + spell_data::r_cast_time + spell_data::r_slam_delay;
                for (auto* h : g_sdk->object_manager->get_heroes()) {
                    if (!is_valid_target(h, spell_data::r_range + h->get_bounding_radius())) continue;
                    if (venplus::utility::has_anti_burst_buff(h)) continue;
                    if (h->has_buff_of_type(buff_type::spell_shield)) continue;

                    const float distance = player->get_position().distance(h->get_position());
                    const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
                    meow::item_damage::ability_proc_context items{};
                    items.horizon_focus_active = horizon;
                    meow::item_damage::ability_rune_context runes{};
                    runes.cast_distance = distance;
                    const float dmg = meow::item_damage::apply_physical_damage_modifiers(
                        player, h, sdk::damage->get_spell_damage(player, h, SLOT_R), 0, horizon)
                        + meow::item_damage::on_ability_damage(player, h, items)
                        + meow::item_damage::on_ability_rune_damage(player, h, runes);
                    const float hp  = sdk::health_prediction->get_predicted_health(h, hit_time)
                                    + h->get_all_shield();
                    if (hp <= 0.f || hp > dmg) continue;

                    auto screen = g_sdk->renderer->world_to_screen(h->get_position());
                    g_sdk->renderer->add_text("R KILL", 18.f,
                        { screen.x, screen.y - 60.f }, 1 /*center*/, 0xFFFF4500u);
                }
            }
        }

        // Passive Determination stack readout — color shifts amber → red as it caps
        if (menu::draw_passive_stacks && g_sdk->renderer && passive_stacks > 0) {
            auto screen = g_sdk->renderer->world_to_screen(player_pos);
            char text[48];
            std::snprintf(text, sizeof(text), "Determination %d/12", passive_stacks);
            // 0..6 stacks = white-amber, 7..11 = orange, 12 = bright red (max double bonus AD)
            const uint32_t color =
                passive_stacks >= 12 ? 0xFFFF2020u :
                passive_stacks >= 7  ? 0xFFFFA040u :
                                       0xFFFFE0A0u;
            g_sdk->renderer->add_text(text, 14.f,
                { screen.x, screen.y - 60.f }, 1, color);
        }
    }

    // R-only HP-bar overlay. Must run in the present pass — add_rectangle_filled is
    // screen-space and gets clipped if drawn from event::draw_world (mirrors the
    // veigar.cpp / viktor.cpp pattern).
    void __fastcall on_present()
    {
        if (!menu::draw_r_dmg_bar) return;
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->renderer) return;
        if (!sdk::damage) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || !player->is_valid() || player->is_dead()) return;

        auto r_sp = player->get_spell(SLOT_R);
        if (!r_sp || r_sp->get_level() <= 0) return;

        const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
        const float ox = -45.6f * scale, oy = 15.5f * scale;
        const float bw = 106.4f * scale, bh = 11.5f * scale;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead()) continue;
            if (h->get_team_id() == player->get_team_id()) continue;
            if (!h->is_visible()) continue;

            const float distance = player->get_position().distance(h->get_position());
            const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
            meow::item_damage::ability_proc_context items{};
            items.horizon_focus_active = horizon;
            meow::item_damage::ability_rune_context runes{};
            runes.cast_distance = distance;
            const float r_dmg = meow::item_damage::apply_physical_damage_modifiers(
                player, h, sdk::damage->get_spell_damage(player, h, SLOT_R), 0, horizon)
                + meow::item_damage::on_ability_damage(player, h, items)
                + meow::item_damage::on_ability_rune_damage(player, h, runes);
            if (r_dmg <= 0.f) continue;

            const float hp = h->get_hp(), mhp = h->get_max_hp();
            if (mhp <= 0.f || hp <= 0.f) continue;

            const auto hbar = h->get_health_bar_position();
            if (hbar.x < 1.f && hbar.y < 1.f) continue;

            const float r0 = hp / mhp;
            const float r1 = std::max(0.f, hp - r_dmg) / mhp;
            if (r0 <= r1) continue;

            math::rect rc;
            rc.left   = hbar.x + ox + r1 * bw;
            rc.top    = hbar.y + oy;
            rc.right  = hbar.x + ox + r0 * bw;
            rc.bottom = hbar.y + oy + bh;
            g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
        }
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj->get_network_id() != player->get_network_id()) return;
        if (cast->is_basic_attack()) return;

        const int slot = cast->get_spell_slot();

        // E dash started
        if (slot == SLOT_E) {
            is_dashing_e = true;
            return;
        }

        // R cast started — lock out Q/W/E until slam delay expires
        if (slot == SLOT_R) {
            is_r_active = true;
            r_active_until = g_sdk->clock_facade->get_game_time()
                           + spell_data::r_cast_time + spell_data::r_slam_delay + 0.3f;
            return;
        }

        if (slot != SLOT_Q) return;

        float now = g_sdk->clock_facade->get_game_time();
        if (!q_recast_pending) {
            // Q1 just fired — arm the recast window
            q_recast_pending = true;
            q1_cast_time = now;
        } else {
            // Q2 fired — close the window
            q_recast_pending = false;
            q1_target = nullptr;
        }
    }

    void __fastcall on_execute_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj->get_network_id() != player->get_network_id()) return;
        if (!q_pending_reset) return;

        // Mode 1 (AA Reset After): when the AA actually fires, queue Q to reset
        if (cast->is_basic_attack() || cast->is_special_attack()) {
            q_pending_reset = false;
            if (sdk::common &&
                is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q) && !q_recast_pending) {
                sdk::common->cast_spell(SLOT_Q);
            }
        }
    }

    void __fastcall on_new_path(game_object* obj, bool is_dash, float /*dash_speed*/)
    {
        if (!obj) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj->get_network_id() != player->get_network_id()) return;

        // E dash ends when the player picks up a non-dash path
        if (is_dashing_e && !is_dash)
            is_dashing_e = false;
    }

    // Hash-first match with substring-name fallback (Jax/Veigar pattern).
    // hash=0 means "we don't know the hash yet" → use the name fragment match.
    static bool is_zaahen_buff(buff_instance* buff, uint32_t hash, const char* name_frag)
    {
        if (!buff) return false;
        if (hash != 0u) return buff->get_hash() == hash;
        if (!name_frag) return false;
        return buff->get_name().find(name_frag) != std::string::npos;
    }

    void __fastcall on_spell_hit(game_object* obj, spell_cast* cast)
    {
        if (!menu::w_logging || !obj || !cast) return;
        // spell_hit also fires for terrain / structure collisions which logs as
        // garbage "OTHER <localized tower name>" entries. Only count hero impacts.
        if (!obj->is_hero()) return;
        auto src = cast->get_source();
        if (!src) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || src->get_network_id() != player->get_network_id()) return;
        if (cast->get_spell_slot() != SLOT_W) return;

        const float now = g_sdk->clock_facade->get_game_time();
        const auto  hpos = obj->get_position();

        // Most recent matching record wins; W lifetime ≤ ~0.79s (delay + travel)
        for (auto it = s_w_log.rbegin(); it != s_w_log.rend(); ++it) {
            const float age = now - it->cast_time;
            if (age < 0.f || age > 1.5f) continue;
            it->hits++;
            const bool same = (obj->get_network_id() == it->target_id);
            const float dx = hpos.x - it->cast_pos.x;
            const float dz = hpos.z - it->cast_pos.z;
            const float deviation = std::sqrt(dx * dx + dz * dz);
            WLOG("HIT: %s%s age=%.2fs dev=%.0f (intended=%s)",
                same ? "" : "OTHER ", obj->get_name().c_str(),
                age, deviation, it->target_name.c_str());
            return;
        }
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj->get_network_id() != player->get_network_id()) return;

        // Q2 first — "ZaahenQ" is a substring of "ZaahenQ2", check the longer string first
        if (is_zaahen_buff(buff, BUFF_HASH_Q_RECAST, BUFF_NAME_Q_RECAST)) {
            q_recast_ready = true;
        }
        else if (is_zaahen_buff(buff, BUFF_HASH_Q_EMPOWER, BUFF_NAME_Q_EMPOWER)) {
            q_empowered = true;
            if (q1_cast_time < 0.f)
                q1_cast_time = g_sdk->clock_facade->get_game_time();
        }
        else if (is_zaahen_buff(buff, BUFF_HASH_PASSIVE, BUFF_NAME_PASSIVE)) {
            // Determination — refresh stack count
            const int s = (int)buff->get_stacks();
            passive_stacks = s > 0 ? s : (int)buff->get_count();
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj->get_network_id() != player->get_network_id()) return;

        if (is_zaahen_buff(buff, BUFF_HASH_Q_RECAST, BUFF_NAME_Q_RECAST)) {
            q_recast_ready = false;
            q_recast_pending = false;
        }
        else if (is_zaahen_buff(buff, BUFF_HASH_Q_EMPOWER, BUFF_NAME_Q_EMPOWER)) {
            q_empowered = false;
            if (!q_recast_ready) {
                q_recast_pending = false;
                q1_cast_time = -1.f;
            }
        }
        else if (is_zaahen_buff(buff, BUFF_HASH_PASSIVE, BUFF_NAME_PASSIVE)) {
            passive_stacks = 0;
        }
    }

    bool __fastcall on_cast_spell(game_object* obj, int spell_slot,
                                  math::vector3 position, game_object* /*target*/)
    {
        if (!obj) return true;
        if (spell_slot != SLOT_R) return true;
        if (!menu::r_block_manual) return true;
        if (menu::semi_r_active) return true;

        auto player = g_sdk->object_manager->get_local_player();
        if (!player || obj->get_network_id() != player->get_network_id()) return true;

        const float radius = spell_data::r_radius + 25.f;
        int hits = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (h->get_position().distance(position) <= radius) ++hits;
        }

        if (hits < menu::r_block_min_hits) {
            if (g_sdk)
                g_sdk->log_console("[Zaahen] R blocked — %d in slam, need %d",
                    hits, menu::r_block_min_hits);
            return false;
        }
        return true;
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target) return true;
        if (!sdk::orbwalker) return true;
        if (q_empowered || q_recast_ready || q_recast_pending) return true; // Q already in flight
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return true;
        if (!sdk::common) return true;

        auto* target = data->target;
        if (!target->is_valid() || target->is_dead() || !target->is_targetable())
            return true;

        bool want_q = false;

        if (sdk::orbwalker->combo()) {
            // Combo: heroes only
            if (menu::use_q_combo && target->is_hero()) want_q = true;
        }
        else if (sdk::orbwalker->fast_clear()) {
            // Fast clear: any AA target (push-now intent)
            if (menu::use_q_fast_clear) want_q = true;
        }
        else if (sdk::orbwalker->clear() && sdk::orbwalker->is_spell_farm_enabled()) {
            // Clear: minions / monsters only, gated by lasthit-aware spell_farm
            if (menu::use_q_clear &&
                (target->is_lane_minion() || target->is_monster()))
                want_q = true;
        }

        if (!want_q) return true;

        // In farm modes, skip Q if our regular AA would already lasthit the target —
        // saves mana and avoids overkilling minions. Hero targets always go through.
        if (!target->is_hero() && sdk::damage && sdk::health_prediction) {
            auto player = g_sdk->object_manager->get_local_player();
            if (player) {
                const float aa_dmg = sdk::damage->get_aa_damage(player, target, false);
                const float aa_land = g_sdk->clock_facade->get_game_time()
                                    + player->get_attack_cast_delay();
                const float pred_hp = sdk::health_prediction->get_predicted_health(target, aa_land);
                if (pred_hp > 0.f && pred_hp <= aa_dmg) return true; // AA kills — let it fly without Q
            }
        }

        if (menu::q_mode == 0) {
            sdk::common->cast_spell(SLOT_Q);
        }
        else if (menu::q_mode == 1) {
            q_pending_reset = true;
        }
        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("zaahen", "MeowAIO - Zaahen");
        if (!s_menu) {
            g_sdk->log_console("[!] Zaahen: failed to create menu category");
            return;
        }

        auto q_menu = s_menu->add_sub_category("q_darkin_glaive", "Q - The Darkin Glaive");
        q_menu->add_checkbox("combo", "Use in Combo", menu::use_q_combo,
            [](bool v) { menu::use_q_combo = v; });
        q_menu->add_combo("q_mode", "Q Mode",
            { "Before Attack", "AA Reset After", "Instant" }, menu::q_mode,
            [](int v) { menu::q_mode = v; });
        q_menu->add_hotkey("q_mode_cycle", "Cycle Q Mode", 'G', false, false,
            [](std::string* /*hk*/, bool pressed) {
                static bool was_pressed = false;
                if (pressed && !was_pressed) {
                    menu::q_mode = (menu::q_mode + 1) % 3;
                    if (g_sdk) {
                        const char* names[] = { "Before Attack", "AA Reset After", "Instant" };
                        g_sdk->log_console("[+] Zaahen Q mode: %s", names[menu::q_mode]);
                    }
                }
                was_pressed = pressed;
            });
        q_menu->add_checkbox("clear", "Use on Clear", menu::use_q_clear,
            [](bool v) { menu::use_q_clear = v; });
        q_menu->add_checkbox("fast_clear", "Use on Fast Clear", menu::use_q_fast_clear,
            [](bool v) { menu::use_q_fast_clear = v; });
        q_menu->add_checkbox("recast_combo", "Use Q2 in Combo", menu::use_q_recast_combo,
            [](bool v) { menu::use_q_recast_combo = v; });
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        auto w_menu = s_menu->add_sub_category("w_dreaded_return", "W - Dreaded Return");
        w_menu->add_checkbox("combo", "Use in Combo", menu::use_w_combo,
            [](bool v) { menu::use_w_combo = v; });
        w_menu->add_checkbox("clear", "Use on Clear", menu::use_w_clear,
            [](bool v) { menu::use_w_clear = v; });
        w_menu->add_checkbox("fast_clear", "Use on Fast Clear", menu::use_w_fast_clear,
            [](bool v) { menu::use_w_fast_clear = v; });
        w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::w_hitchance,
            [](int v) { menu::w_hitchance = v; });
        w_menu->add_slider_int("min_aoe", "Min AOE Hits (clear)", 1, 6, 1,
            menu::clear_min_hits, [](int v) { menu::clear_min_hits = v; });
        w_menu->add_separator();
        w_menu->add_separator();
        w_menu->add_hotkey("semi_w", "Semi-Manual W", 'Y', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_w_active = pressed;
                if (hk) menu::semi_w_key_str = *hk;
            });
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });

        auto e_menu = s_menu->add_sub_category("e_aureate_rush", "E - Aureate Rush");
        e_menu->add_checkbox("combo", "Use in Combo", menu::use_e_combo,
            [](bool v) { menu::use_e_combo = v; });
        e_menu->add_checkbox("sweetspot", "Sweet-Spot Aim", menu::e_sweetspot,
            [](bool v) { menu::e_sweetspot = v; });
        e_menu->add_checkbox("clear", "Use on Clear", menu::use_e_clear,
            [](bool v) { menu::use_e_clear = v; });
        e_menu->add_checkbox("fast_clear", "Use on Fast Clear", menu::use_e_fast_clear,
            [](bool v) { menu::use_e_fast_clear = v; });
        e_menu->add_slider_int("min_aoe", "Min Sweet-Spot Hits", 1, 6, 1,
            menu::e_clear_min_hits, [](int v) { menu::e_clear_min_hits = v; });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });
        e_menu->add_checkbox("draw_sweetspot", "Draw Sweet-Spot Ring", menu::draw_e_sweetspot,
            [](bool v) { menu::draw_e_sweetspot = v; });
        e_menu->add_checkbox("draw_terrain", "Draw Terrain Reachability", menu::draw_e_terrain,
            [](bool v) { menu::draw_e_terrain = v; });

        auto r_menu = s_menu->add_sub_category("r_grim_deliverance", "R - Grim Deliverance");
        r_menu->add_checkbox("combo", "Use in Combo", menu::use_r_combo,
            [](bool v) { menu::use_r_combo = v; });
        r_menu->add_checkbox("only_kill", "Only When Killable", menu::r_only_killable,
            [](bool v) { menu::r_only_killable = v; });
        r_menu->add_checkbox("require_hits", "Only When Hits Enemies", menu::r_require_hits,
            [](bool v) { menu::r_require_hits = v; });
        r_menu->add_slider_int("min_hits", "Min Hits", 1, 5, 1, menu::r_min_targets,
            [](int v) { menu::r_min_targets = v; });
        r_menu->add_checkbox("max_stacks", "Only at Max Passive Stacks ",
            menu::r_only_max_stacks, [](bool v) { menu::r_only_max_stacks = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("block_manual", "Block Manual R",
            menu::r_block_manual, [](bool v) { menu::r_block_manual = v; });
        r_menu->add_slider_int("block_min", "Block Manual %",
            1, 5, 1, menu::r_block_min_hits, [](int v) { menu::r_block_min_hits = v; });
        r_menu->add_separator();
        r_menu->add_hotkey("semi_r", "Semi-Manual R", VK_LBUTTON, false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_r_active = pressed;
                if (hk) menu::semi_r_key_str = *hk;
            });
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });

        auto mode_menu = s_menu->add_sub_category("combo_mode", "Misc");
        w_menu->add_checkbox("logging", "Debug logs", menu::w_logging,
            [](bool v) { menu::w_logging = v; });
        mode_menu->add_checkbox("teamfight", "Teamfight Mode (E -> W)", menu::combo_teamfight,
            [](bool v) { menu::combo_teamfight = v; });
        mode_menu->add_hotkey("toggle", "Toggle Combo Mode", 'T', false, true,
            [](std::string* /*hk*/, bool active) {
                menu::combo_teamfight = active;
                if (g_sdk)
                    g_sdk->log_console("[+] Zaahen combo: %s",
                        active ? "Teamfight (E -> W)" : "Normal (W -> E)");
            });
        mode_menu->add_separator();
        mode_menu->add_label("Flash + E sweet-spot + W");
        mode_menu->add_hotkey("flash_ew", "Flash + E + W", 'X', false, false,
            [](std::string* hk, bool pressed) {
                static bool was = false;
                if (pressed && !was) flash_ew_start();
                was = pressed;
                if (hk) menu::flash_ew_key_str = *hk;
            });

        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("clear_mana", "Clear Mana %", 0, 100, 5,
            menu::clear_mana_pct, [](int v) { menu::clear_mana_pct = v; });

        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("r_killable", "R Killable Text", menu::draw_r_killable,
            [](bool v) { menu::draw_r_killable = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_checkbox("combo_mode", "Combo Mode Indicator", menu::draw_combo_mode,
            [](bool v) { menu::draw_combo_mode = v; });
        draw_menu->add_checkbox("q_mode", "Q Mode Indicator", menu::draw_q_mode,
            [](bool v) { menu::draw_q_mode = v; });
        draw_menu->add_checkbox("passive_stacks", "Passive Stacks Indicator",
            menu::draw_passive_stacks, [](bool v) { menu::draw_passive_stacks = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("color_w", "W Color", menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("color_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("color_r", "R Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("color_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::execute_cast,
            reinterpret_cast<void*>(on_execute_cast));
        g_sdk->event_manager->register_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->register_callback(event_manager::event::cast_spell,
            reinterpret_cast<void*>(on_cast_spell));
        g_sdk->event_manager->register_callback(event_manager::event::spell_hit,
            reinterpret_cast<void*>(on_spell_hit));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        if (sdk::infotab) {
            infotab_sdk::text_entry title_r{ "Semi R", 0xFFFFFFFFu };
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key_str, title_r,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_r_active ? "ON" : "OFF", clr };
                });

            infotab_sdk::text_entry title_w{ "Semi W", 0xFFFFFFFFu };
            menu::infotab_semi_w = sdk::infotab->add_hotkey_text(
                &menu::semi_w_key_str, title_w,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_w_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_w_active ? "ON" : "OFF", clr };
                });

            infotab_sdk::text_entry title_fe{ "Flash+E+W", 0xFFFFFFFFu };
            menu::infotab_flash_ew = sdk::infotab->add_hotkey_text(
                &menu::flash_ew_key_str, title_fe,
                []() -> infotab_sdk::text_entry {
                    const bool running = (s_fcs_state != 0);
                    const uint32_t clr = running ? 0xFFFFD700u : 0xFFAAAAAAu;
                    return { running ? "FIRING" : "ready", clr };
                });
        }

        g_sdk->log_console("[MeowAIO] Zaahen module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::present,
                reinterpret_cast<void*>(on_present));
            g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
                reinterpret_cast<void*>(on_process_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::execute_cast,
                reinterpret_cast<void*>(on_execute_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::new_path,
                reinterpret_cast<void*>(on_new_path));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
            g_sdk->event_manager->unregister_callback(event_manager::event::cast_spell,
                reinterpret_cast<void*>(on_cast_spell));
            g_sdk->event_manager->unregister_callback(event_manager::event::spell_hit,
                reinterpret_cast<void*>(on_spell_hit));
        }

        if (sdk::infotab) {
            if (menu::infotab_semi_r != 0) {
                sdk::infotab->remove_text(menu::infotab_semi_r);
                menu::infotab_semi_r = 0;
            }
            if (menu::infotab_semi_w != 0) {
                sdk::infotab->remove_text(menu::infotab_semi_w);
                menu::infotab_semi_w = 0;
            }
            if (menu::infotab_flash_ew != 0) {
                sdk::infotab->remove_text(menu::infotab_flash_ew);
                menu::infotab_flash_ew = 0;
            }
        }

        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        q_recast_pending = false;
        q_empowered = false;
        q_recast_ready = false;
        q1_cast_time = -1.f;
        q1_target = nullptr;
        q_pending_reset = false;
        is_dashing_e = false;
        is_r_active = false;
        r_active_until = 0.f;
        passive_stacks = 0;
        menu::semi_r_active = false;
        menu::semi_w_active = false;
        s_fcs_state = 0;
        s_fcs_target_id = 0;
        s_flash_slot_cache = -1;

        if (g_sdk) g_sdk->log_console("[-] Zaahen module unloaded");
    }
}
