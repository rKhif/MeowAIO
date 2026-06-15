#include "senna.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace senna
{
namespace
{
    // ============================================================================
    // SPELL SLOTS
    // ============================================================================
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // ============================================================================
    // SPELL DATA — Senna
    // ============================================================================
    // Q - Piercing Darkness: targeted unit-cast that fires a line from Senna
    //                        through the casted target. Beam continues to a
    //                        max distance of 1300 from Senna (so you can Q an
    //                        ally / minion / monster / plant / ward / soul to
    //                        chain damage onto an enemy that's out of the
    //                        600u cast range).
    constexpr float Q_CAST_RANGE   = 600.f;   // unit must be within this to be cast on
    constexpr float Q_BEAM_RANGE   = 1300.f;  // beam total range from Senna
    constexpr float Q_RADIUS       = 100.f;   // beam half-width (hit radius)

    // W - Last Embrace: linear skillshot, roots if it hits after a short delay.
    // Windup = cast animation before projectile fires (the "delay" in pred-math).
    // The 1s root delay after impact is irrelevant for hit prediction.
    constexpr float W_RANGE        = 1300.f;
    constexpr float W_RADIUS       = 80.f;
    constexpr float W_WINDUP       = 0.25f;
    constexpr float W_SPEED        = 1100.f;

    // E - Curse of the Black Mist: self-cast, AoE camo around Senna.
    constexpr float E_RADIUS       = 400.f;

    // R - Dawning Shadow: global linear skillshot (damages enemies / shields allies).
    constexpr float R_RANGE        = 20000.f;
    constexpr float R_RADIUS       = 160.f;
    constexpr float R_DELAY        = 1.0f;
    constexpr float R_SPEED        = 20000.f;

    // ============================================================================
    // STATE
    // ============================================================================
    menu_category* s_menu = nullptr;
    float           s_last_cast_attempt[6] = {};

    // ============================================================================
    // MENU
    // ============================================================================
    namespace menu
    {
        // Q
        inline bool combo_q       = true;
        // W
        inline bool combo_w       = true;
        inline int  combo_w_hitchance = pred_sdk::hitchance::high;
        // E
        inline bool combo_e       = false;
        // R
        inline bool combo_r       = false;
        inline int  combo_r_hitchance = pred_sdk::hitchance::very_high;

        // Drawings
        inline bool draw_q        = true;
        inline bool draw_w        = true;
        inline bool draw_e        = false;
        inline bool draw_r        = false;
        inline bool draw_only_ready = true;

        inline uint32_t color_q  = 0x80FF80E0u; // pink
        inline uint32_t color_w  = 0x80B070FFu; // purple
        inline uint32_t color_e  = 0x808080FFu; // light blue
        inline uint32_t color_r  = 0x80FFFFFFu; // white
    }

    // ============================================================================
    // HELPERS
    // ============================================================================
    game_object* player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    float game_time()
    {
        if (!g_sdk || !g_sdk->clock_facade) return 0.f;
        return g_sdk->clock_facade->get_game_time();
    }

    bool can_attempt_cast(int slot, float delay = 0.18f)
    {
        if (slot < 0 || slot >= 6) return true;
        const float now = game_time();
        if (now <= 0.f) return true;
        if (now - s_last_cast_attempt[slot] < delay) return false;
        return true;
    }

    void mark_cast_attempt(int slot)
    {
        if (slot < 0 || slot >= 6) return;
        s_last_cast_attempt[slot] = game_time();
    }

    bool spell_ready(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0 && s->get_cooldown() <= 0.f;
    }

    bool has_mana(int slot)
    {
        auto* p = player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    bool is_casting()
    {
        auto* p = player();
        if (!p) return false;
        auto ac = p->get_active_spell_cast();
        if (!ac) return false;
        auto sc = ac->get_spell_cast();
        if (!sc) return false;
        if (sc->is_basic_attack() || sc->is_special_attack()) return false;
        return ac->get_cast_end_time() > game_time();
    }

    bool is_valid_enemy(game_object* obj, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !obj || !obj->is_valid() || obj->is_dead()) return false;
        if (!obj->is_targetable() || !obj->is_visible()) return false;
        if (obj->get_team_id() == p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(obj->get_position()) > range)
            return false;
        return true;
    }

    game_object* pick_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_enemy(forced, range))
            return forced;
        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (is_valid_enemy(h, range)) return h;
        }
        return nullptr;
    }

    // ============================================================================
    // Q CHAIN TARGETING
    // ============================================================================
    // Senna Q is unit-targeted but extends a beam from Senna in the direction
    // of that unit for a total of Q_BEAM_RANGE units. We can therefore Q *any*
    // valid unit (ally, minion, monster, plant, ward, soul) within 600u to
    // damage an enemy hero standing somewhere along the beam path.
    //
    // The math:
    //   dir = (chain_unit - Senna).normalized()
    //   beam segment = [Senna, Senna + dir * Q_BEAM_RANGE]
    //   hero is hit if projection-along-dir > 0
    //                 and projection-along-dir <= Q_BEAM_RANGE
    //                 and perpendicular distance <= Q_RADIUS + hero_bounding_radius

    bool beam_hits_hero(const math::vector3& senna_pos,
                        const math::vector3& chain_pos,
                        game_object* hero)
    {
        if (!hero) return false;

        const float dx = chain_pos.x - senna_pos.x;
        const float dz = chain_pos.z - senna_pos.z;
        const float chain_dist = std::sqrt(dx * dx + dz * dz);
        if (chain_dist < 1.f) return false;

        // Beam direction (XZ plane).
        const float ux = dx / chain_dist;
        const float uz = dz / chain_dist;

        const float hx = hero->get_position().x - senna_pos.x;
        const float hz = hero->get_position().z - senna_pos.z;

        const float along = hx * ux + hz * uz;                  // projection on beam
        if (along <= 0.f || along > Q_BEAM_RANGE) return false; // behind Senna or past max range

        const float perp_sq = (hx * hx + hz * hz) - along * along;
        const float window  = Q_RADIUS + hero->get_bounding_radius();
        return perp_sq <= window * window;
    }

    // Find a valid unit to Q-cast on such that the beam hits `hero`.
    // Tries every object class: ally heroes, minions (incl. plants/wards/souls),
    // monsters (jungle). Returns nullptr if no chain is possible.
    game_object* find_q_chain_target(game_object* hero)
    {
        auto* p = player();
        if (!p || !hero || !g_sdk || !g_sdk->object_manager) return nullptr;

        const auto senna_pos = p->get_position();
        const float cast_range = Q_CAST_RANGE + 25.f; // tiny slop for moving units

        auto consider = [&](game_object* unit) -> game_object*
        {
            if (!unit || !unit->is_valid() || unit->is_dead()) return nullptr;
            if (!unit->is_targetable() || !unit->is_visible()) return nullptr;
            if (unit->get_network_id() == p->get_network_id()) return nullptr;

            const float d = senna_pos.distance(unit->get_position());
            if (d > cast_range + unit->get_bounding_radius()) return nullptr;

            return beam_hits_hero(senna_pos, unit->get_position(), hero) ? unit : nullptr;
        };

        game_object* best   = nullptr;
        float        best_d = FLT_MAX;

        auto try_pick = [&](game_object* unit)
        {
            auto* ok = consider(unit);
            if (!ok) return;
            // Prefer the closest chain unit — keeps the beam direction tight.
            const float d = senna_pos.distance(ok->get_position());
            if (d < best_d)
            {
                best   = ok;
                best_d = d;
            }
        };

        // Heroes (allies only — direct-Q on the enemy hero is handled separately)
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!h || h == hero) continue;
            if (h->get_team_id() != p->get_team_id()) continue;
            try_pick(h);
        }

        // Minions: lane minions (both teams), plants, wards, Senna souls — all
        // live in the minions collection.
        for (auto* m : g_sdk->object_manager->get_minions())
            try_pick(m);

        // Jungle monsters
        for (auto* m : g_sdk->object_manager->get_monsters())
            try_pick(m);

        return best;
    }

    // ============================================================================
    // PREDICTION
    // ============================================================================
    // Manual W intercept solver.
    //
    // We solve for the projectile travel time `dt` that intercepts a target moving
    // with constant velocity V from its current position T, given player at P.
    //
    // Timeline:
    //   t = 0       : spell is requested
    //   t = w       : windup ends, projectile spawns at P, target now at T + V*w
    //   t = w + dt  : projectile travels `c*dt`, target now at T + V*(w + dt)
    //
    // Intercept condition (vectors):  P + dir * c * dt  =  T + V * (w + dt)
    // Let R = (T - P) + V*w  (target position when projectile spawns, relative to P)
    // Then:                            dir * c * dt    =  R + V * dt
    // Taking magnitudes:               (c*dt)^2        =  |R + V*dt|^2
    //
    // Expanding:
    //   c^2 dt^2 = |R|^2 + 2 (R . V) dt + |V|^2 dt^2
    //   (|V|^2 - c^2) dt^2 + 2 (R . V) dt + |R|^2 = 0
    //
    // Quadratic in dt: a*dt^2 + b*dt + d = 0, choose smallest positive root.
    //
    // The spell radius + target bounding radius give us hit-tolerance, which we
    // bake in by relaxing the required hit distance from 0 to (radius + hitbox).
    struct w_solution
    {
        bool          valid;
        math::vector3 cast_position;
        float         time_to_hit;
        float         travel_distance;
    };

    w_solution solve_w_intercept(game_object* target)
    {
        w_solution out{};
        out.valid = false;

        auto* p = player();
        if (!p || !target) return out;

        const auto P  = p->get_position();
        const auto T  = target->get_position();
        const auto V  = target->get_velocity();
        const float c = W_SPEED;
        const float w = W_WINDUP;

        // R = target position at projectile spawn time, relative to player
        const math::vector3 R{
            (T.x - P.x) + V.x * w,
            0.f,
            (T.z - P.z) + V.z * w
        };

        // Planar dot products / squared magnitudes (XZ plane; League uses Y for height)
        const float V2  = V.x * V.x + V.z * V.z;
        const float R2  = R.x * R.x + R.z * R.z;
        const float RV  = R.x * V.x + R.z * V.z;

        const float a   = V2 - c * c;
        const float b   = 2.f * RV;
        const float d   = R2;

        float dt = -1.f;

        if (std::fabs(a) < 1e-3f)
        {
            // Degenerate (target as fast as spell along this axis) — linear fallback
            if (std::fabs(b) > 1e-3f) dt = -d / b;
        }
        else
        {
            const float disc = b * b - 4.f * a * d;
            if (disc < 0.f) return out; // no intercept
            const float sq = std::sqrt(disc);
            const float t1 = (-b + sq) / (2.f * a);
            const float t2 = (-b - sq) / (2.f * a);
            // pick smallest positive root
            if (t1 > 0.f && (t2 <= 0.f || t1 < t2)) dt = t1;
            else if (t2 > 0.f)                     dt = t2;
        }

        if (dt <= 0.f) return out;

        // Intercept point in world coords (target's position when projectile arrives)
        const math::vector3 intercept{
            T.x + V.x * (w + dt),
            T.y,
            T.z + V.z * (w + dt)
        };

        const float dist_from_player = std::sqrt(
            (intercept.x - P.x) * (intercept.x - P.x) +
            (intercept.z - P.z) * (intercept.z - P.z));

        // Range check: projectile only travels c*dt total — but spell radius and
        // target hitbox forgive small overshoots at the edge of W's range.
        const float hit_tolerance = W_RADIUS + target->get_bounding_radius();
        const float max_reach     = std::min(W_RANGE, c * dt) + hit_tolerance;
        if (dist_from_player > max_reach) return out;

        out.valid           = true;
        out.cast_position   = intercept;
        out.time_to_hit     = w + dt;
        out.travel_distance = dist_from_player;
        return out;
    }

    // Hitchance estimate from the geometry: tighter cone vs target's velocity = higher chance.
    int estimate_w_hitchance(game_object* target, const w_solution& sol)
    {
        if (!target || !sol.valid) return 0;
        if (!target->is_moving())  return pred_sdk::hitchance::very_high;
        if (target->is_dashing())  return pred_sdk::hitchance::high;

        const auto V = target->get_velocity();
        const float vmag = std::sqrt(V.x * V.x + V.z * V.z);
        if (vmag < 50.f) return pred_sdk::hitchance::very_high;

        // Wider hit window (radius + hitbox) relative to how far the target can drift
        // during travel = more forgiving = higher confidence.
        const float drift = vmag * sol.time_to_hit;
        const float window = W_RADIUS + target->get_bounding_radius();
        const float ratio  = window / std::max(drift, 1.f);

        if (ratio >= 1.5f) return pred_sdk::hitchance::very_high;
        if (ratio >= 0.9f) return pred_sdk::hitchance::high;
        if (ratio >= 0.5f) return pred_sdk::hitchance::medium;
        return pred_sdk::hitchance::low;
    }

    pred_sdk::pred_data predict_r(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_R;
        spell.range              = R_RANGE;
        spell.radius             = R_RADIUS;
        spell.delay              = R_DELAY;
        spell.projectile_speed   = R_SPEED;
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    // NOTE: Q is a unit-targeted spell. The orbwalker fix in MeowV1/Senna/
    // (baseSenna.h / Senna.h) — soul tracking + IgnoreCanAttack /
    // ShouldIgnoreCancelDetection — will be integrated here once it's ported
    // to the current SDK's orb_sdk callbacks.
    //
    // Tries direct Q first; if the hero is outside the 600u cast range,
    // looks for any intermediate unit (ally / minion / plant / ward / soul /
    // monster) that lets the beam pass through the hero.
    bool cast_q(game_object* hero)
    {
        auto* p = player();
        if (!p || !hero || !sdk::common) return false;
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return false;
        if (!can_attempt_cast(SLOT_Q)) return false;

        const float hero_dist = p->get_position().distance(hero->get_position());
        if (hero_dist > Q_BEAM_RANGE + hero->get_bounding_radius()) return false;

        // Direct cast when the hero is in cast range.
        game_object* cast_on = nullptr;
        if (is_valid_enemy(hero, Q_CAST_RANGE + hero->get_bounding_radius()))
        {
            cast_on = hero;
        }
        else
        {
            // Hero is outside the 600u cast range but inside the 1300u beam range —
            // find a chain target so the beam pierces through.
            cast_on = find_q_chain_target(hero);
        }
        if (!cast_on) return false;

        const bool ok = sdk::common->cast_spell(SLOT_Q, cast_on);
        if (ok) mark_cast_attempt(SLOT_Q);
        return ok;
    }

    bool cast_w(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        if (!can_attempt_cast(SLOT_W)) return false;

        const auto sol = solve_w_intercept(target);
        if (!sol.valid) return false;

        const int hc = estimate_w_hitchance(target, sol);
        if (hc < menu::combo_w_hitchance) return false;

        const bool ok = sdk::common->cast_spell(SLOT_W, sol.cast_position);
        if (ok) mark_cast_attempt(SLOT_W);
        return ok;
    }

    bool cast_e_self()
    {
        auto* p = player();
        if (!p || !sdk::common) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        if (!can_attempt_cast(SLOT_E)) return false;

        const bool ok = sdk::common->cast_spell(SLOT_E, p);
        if (ok) mark_cast_attempt(SLOT_E);
        return ok;
    }

    bool cast_r(game_object* target)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return false;
        if (!can_attempt_cast(SLOT_R, 0.25f)) return false;

        auto pred = predict_r(target, menu::combo_r_hitchance);
        if (!pred.is_valid || pred.hitchance < menu::combo_r_hitchance) return false;

        const bool ok = sdk::common->cast_spell(SLOT_R, pred.cast_position);
        if (ok) mark_cast_attempt(SLOT_R);
        return ok;
    }

    // ============================================================================
    // MODES
    // ============================================================================
    void combo()
    {
        auto* p = player();
        if (!p || p->is_dead() || is_casting()) return;

        auto* t = pick_target(std::max({ Q_BEAM_RANGE, W_RANGE }));
        if (!t) return;

        if (menu::combo_r && spell_ready(SLOT_R))
        {
            if (auto* r_target = pick_target(R_RANGE); r_target && cast_r(r_target))
                return;
        }

        if (menu::combo_w && cast_w(t)) return;
        if (menu::combo_q && cast_q(t)) return;

        if (menu::combo_e) cast_e_self();
    }

    // ============================================================================
    // MENU SETUP
    // ============================================================================
    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("senna", "MeowAIO - Senna");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Senna: failed to create menu category");
            return;
        }

        // Q - Piercing Darkness
        if (auto* q_menu = s_menu->add_sub_category("q_piercing_darkness", "Q - Piercing Darkness"))
        {
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            q_menu->add_separator();
            q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });
        }

        // W - Last Embrace
        if (auto* w_menu = s_menu->add_sub_category("w_last_embrace", "W - Last Embrace"))
        {
            w_menu->add_checkbox("combo", "Use in Combo", menu::combo_w,
                [](bool v) { menu::combo_w = v; });
            w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 5, menu::combo_w_hitchance,
                [](int v) { menu::combo_w_hitchance = v; });
            w_menu->add_separator();
            w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w,
                [](bool v) { menu::draw_w = v; });
        }

        // E - Curse of the Black Mist
        if (auto* e_menu = s_menu->add_sub_category("e_black_mist", "E - Curse of the Black Mist"))
        {
            e_menu->add_checkbox("combo", "Use in Combo", menu::combo_e,
                [](bool v) { menu::combo_e = v; });
            e_menu->add_separator();
            e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e,
                [](bool v) { menu::draw_e = v; });
        }

        // R - Dawning Shadow
        if (auto* r_menu = s_menu->add_sub_category("r_dawning_shadow", "R - Dawning Shadow"))
        {
            r_menu->add_checkbox("combo", "Use in Combo", menu::combo_r,
                [](bool v) { menu::combo_r = v; });
            r_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 5, menu::combo_r_hitchance,
                [](int v) { menu::combo_r_hitchance = v; });
            r_menu->add_separator();
            r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
        }

        // Drawings
        if (auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings"))
        {
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_separator();
            draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
                [](uint32_t c) { menu::color_q = c; });
            draw_menu->add_colorpicker("col_w", "W Color", menu::color_w,
                [](uint32_t c) { menu::color_w = c; });
            draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
                [](uint32_t c) { menu::color_e = c; });
            draw_menu->add_colorpicker("col_r", "R Color", menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
        }
    }
} // anonymous namespace

// ============================================================================
// LIFECYCLE
// ============================================================================
void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Senna")
        return;

    setup_menu();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
    }

    g_sdk->log_console("[MeowAIO] Senna loaded MEOW!");
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
    }

    s_menu = nullptr;
    for (float& v : s_last_cast_attempt) v = 0.f;

    if (g_sdk) g_sdk->log_console("[-] Senna module unloaded");
}

// ============================================================================
// EVENT CALLBACKS
// ============================================================================
void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = player();
    if (!p || p->is_dead()) return;

    if (!sdk::orbwalker) return;
    if (sdk::orbwalker->combo()) combo();
}

void __fastcall on_draw()
{
    // reserved for HUD-style overlays (passive stacks, soul counter, etc.)
}

void __fastcall on_draw_world()
{
    auto* p = player();
    if (!p || p->is_dead()) return;

    auto draw_circle = [&](float radius, uint32_t color, int slot)
    {
        if (!color) return;
        if (menu::draw_only_ready && slot >= 0)
        {
            auto* s = p->get_spell(slot);
            if (!s || s->get_level() == 0 || s->get_cooldown() > 0.f) return;
        }
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(p->get_position(), radius, 2.f, color);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(p->get_position(), radius, 1.f, color);
    };

    if (menu::draw_q) draw_circle(Q_BEAM_RANGE, menu::color_q, SLOT_Q);
    if (menu::draw_w) draw_circle(W_RANGE, menu::color_w, SLOT_W);
    if (menu::draw_e) draw_circle(E_RADIUS, menu::color_e, SLOT_E);
    // R is global — skip the giant ring; "Draw Range" toggle is exposed but unused.
    if (menu::draw_r) { /* intentionally no draw — global skillshot */ }
}

} // namespace senna
