#include "thresh.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "ts_sdk.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>
#include <span>

namespace thresh
{
namespace
{
    menu_category* s_menu = nullptr;

    // Per-enemy Q whitelist (network id -> enabled).
    std::unordered_map<uint32_t, bool> s_q_enemies;
    // Per-ally W whitelist (network id -> enabled).
    std::unordered_map<uint32_t, bool> s_w_allies;

    float s_last_cast_time[4] = { 0.f, 0.f, 0.f, 0.f };

    math::vector3 s_lantern_pos        = { 0.f, 0.f, 0.f };
    float         s_lantern_spawn_time = -10.f;

    // Live E mode toggle: false = pull, true = push.
    bool s_e_mode_push = false;

    // Semi-manual E hotkey states (true while held).
    bool s_semi_e_push = false;
    bool s_semi_e_pull = false;

    // Force-EQ window: a successful E cast opens a brief window where Q fires
    // with a relaxed hitchance threshold so the flick chains into a hook.
    constexpr float k_force_eq_window = 0.5f;
    float s_force_eq_until = 0.f;

    // For flashing the max range briefly when its slider changes.
    float s_last_max_range_change = -10.f;

    // Hard CC buff types Thresh cares about for lantern logic.
    const buff_type k_hard_cc[] = {
        buff_type::asleep,
        buff_type::berserk,
        buff_type::charm,
        buff_type::drowsy,
        buff_type::fear,
        buff_type::flee,
        buff_type::knockup,
        buff_type::stun,
        buff_type::suppression,
        buff_type::taunt,
    };

    // ---------------------------------------------------------------
    // BASIC HELPERS
    // ---------------------------------------------------------------
    float now_time()
    {
        return (g_sdk && g_sdk->clock_facade) ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    game_object* get_player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    bool is_spell_ready(int slot)
    {
        auto* p = get_player();
        if (!p) return false;
        auto* sp = p->get_spell(slot);
        return sp && sp->get_level() > 0 && sp->get_cooldown() <= 0.f;
    }

    bool is_spell_learned(int slot)
    {
        auto* p = get_player();
        if (!p) return false;
        auto* sp = p->get_spell(slot);
        return sp && sp->get_level() > 0;
    }

    float dist2d(const math::vector3& a, const math::vector3& b)
    {
        const float dx = a.x - b.x;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dz * dz);
    }

    math::vector3 extend2d(const math::vector3& from, const math::vector3& to, float dist)
    {
        const float d = dist2d(from, to);
        if (d < 0.001f) return from;
        const float ratio = dist / d;
        return {
            from.x + (to.x - from.x) * ratio,
            from.y + (to.y - from.y) * ratio,
            from.z + (to.z - from.z) * ratio
        };
    }

    bool unit_alive(game_object* o)
    {
        return o && o->is_valid() && !o->is_dead();
    }

    bool is_valid_enemy(game_object* unit, float range = FLT_MAX)
    {
        auto* p = get_player();
        if (!p || !unit_alive(unit)) return false;
        if (!unit->is_targetable() || !unit->is_visible()) return false;
        if (unit->get_team_id() == p->get_team_id()) return false;
        if (range != FLT_MAX && dist2d(p->get_position(), unit->get_position()) > range)
            return false;
        return true;
    }

    bool is_valid_ally(game_object* unit, float range = FLT_MAX, bool include_self = false)
    {
        auto* p = get_player();
        if (!p || !unit_alive(unit)) return false;
        if (!include_self && unit->get_network_id() == p->get_network_id()) return false;
        if (unit->get_team_id() != p->get_team_id()) return false;
        if (range != FLT_MAX && dist2d(p->get_position(), unit->get_position()) > range)
            return false;
        return true;
    }

    bool is_unstoppable(game_object* t)
    {
        if (!t) return false;
        return t->has_buff_of_type(buff_type::spell_immunity)
            || t->has_buff_of_type(buff_type::invulnerability)
            || t->has_buff_of_type(buff_type::unkillable);
    }

    bool has_buff(game_object* obj, const char* name)
    {
        if (!obj || !name) return false;
        std::string key = name;
        auto* b = obj->get_buff_by_name(key);
        return b && b->is_active();
    }

    // Q2 (Deathly Leap) is active when the Q spell entry name changes.
    bool is_q2()
    {
        auto* p = get_player();
        if (!p) return false;
        auto* sp = p->get_spell(SLOT_Q);
        if (!sp) return false;
        auto* sd = sp->get_data();
        if (!sd) return false;
        auto* st = sd->get_static_data();
        if (!st) return false;
        const char* name = st->get_name();
        return name && std::strstr(name, "ThreshQLeap") != nullptr;
    }

    // Q hooked an enemy when these buffs are present. Buff name casing is not
    // guaranteed across patches, so we scan with a case-insensitive substring.
    bool buff_contains_ci(game_object* obj, const char* needle)
    {
        if (!obj || !needle) return false;
        const size_t nlen = std::strlen(needle);
        for (auto* b : obj->get_buffs())
        {
            if (!b || !b->is_active()) continue;
            std::string name = b->get_name();
            if (name.size() < nlen) continue;
            // Tolower compare — needle is expected lowercase.
            for (size_t i = 0; i + nlen <= name.size(); ++i)
            {
                bool match = true;
                for (size_t j = 0; j < nlen; ++j)
                {
                    char c = name[i + j];
                    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
                    if (c != needle[j]) { match = false; break; }
                }
                if (match) return true;
            }
        }
        return false;
    }

    bool is_q_successful()
    {
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto* p = get_player();
        if (!p) return false;
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!h || h->is_dead()) continue;
            if (h->get_team_id() == p->get_team_id()) continue;
            if (buff_contains_ci(h, "threshq")) return true;
        }
        return false;
    }

    // ---------------------------------------------------------------
    // PREDICTION HELPERS
    // ---------------------------------------------------------------
    // Snap the slider's raw % to the nearest pred_sdk hitchance bucket. The
    // predictor only returns 30/50/70/85/100 internally, so any value between
    // those buckets behaves like the higher one — snap to make the slider feel
    // intuitive instead of having dead zones.
    int q_hitchance_value()
    {
        const int v = menu::q_hitchance;
        if (v < 40)  return pred_sdk::hitchance::low;            // 30
        if (v < 60)  return pred_sdk::hitchance::medium;         // 50
        if (v < 78)  return pred_sdk::hitchance::high;           // 70
        if (v < 93)  return pred_sdk::hitchance::very_high;      // 85
        return pred_sdk::hitchance::guaranteed_hit;              // 100
    }

    pred_sdk::pred_data predict_q(game_object* target)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type       = pred_sdk::spell_type::linear;
        sd.targetting_type  = pred_sdk::targetting_type::center;
        sd.spell_slot       = SLOT_Q;
        sd.range            = static_cast<float>(menu::q_max_range);
        sd.radius           = spell_data::q_radius;
        sd.delay            = spell_data::q_delay;
        sd.projectile_speed = spell_data::q_speed;
        sd.expected_hitchance = q_hitchance_value();
        sd.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, sd); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // pull = true  → instant/near-instant projectile (hook pull)
    // pull = false → push (2000 speed linear)
    pred_sdk::pred_data predict_e(game_object* target, bool pull, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        sd.spell_slot         = SLOT_E;
        sd.range              = spell_data::e_range;
        sd.radius             = spell_data::e_radius;
        sd.delay              = 0.f;
        sd.projectile_speed   = pull ? FLT_MAX : spell_data::e_speed;
        sd.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, sd); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ---------------------------------------------------------------
    // LANTERN HELPERS
    // ---------------------------------------------------------------
    bool cast_lantern_to(game_object* ally)
    {
        if (!ally || !is_spell_ready(SLOT_W)) return false;
        auto* p = get_player();
        if (!p) return false;
        const auto ppos = p->get_position();
        const auto tpos = ally->get_position();
        const float d   = dist2d(ppos, tpos);
        // Clamp the cast distance: never negative, never beyond W range.
        const float reach = std::clamp(d - 30.f, 50.f, spell_data::w_range);
        const auto  cast_pos = extend2d(ppos, tpos, reach);
        return sdk::common && sdk::common->cast_spell(SLOT_W, cast_pos);
    }

    bool cast_lantern_pos(const math::vector3& pos)
    {
        if (!is_spell_ready(SLOT_W)) return false;
        auto* p = get_player();
        if (!p) return false;
        const auto ppos = p->get_position();
        const float d   = dist2d(ppos, pos);
        const float reach = std::clamp(d - 30.f, 50.f, spell_data::w_range);
        const auto  cast_pos = extend2d(ppos, pos, reach);
        return sdk::common && sdk::common->cast_spell(SLOT_W, cast_pos);
    }

    void iterate_lantern_logic()
    {
        if (!is_spell_ready(SLOT_W)) return;
        if (!g_sdk || !g_sdk->object_manager) return;
        auto* p = get_player();
        if (!p) return;

        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_ally(a, spell_data::w_range + 200.f)) continue;
            const auto it = s_w_allies.find(a->get_network_id());
            if (it == s_w_allies.end() || !it->second) continue;

            // Q hooked → immediately throw lantern for follow-up leap.
            if (menu::w_lantern_q_hit && is_q_successful())
            {
                if (cast_lantern_to(a)) return;
            }

            // Ally under hard CC.
            if (menu::w_lantern_cc)
            {
                for (auto cc_type : k_hard_cc)
                {
                    if (a->has_buff_of_type(cc_type) && !a->has_buff_of_type(buff_type::knockback))
                    {
                        if (cast_lantern_to(a)) return;
                    }
                }
            }

            // Ally is being displaced — cast toward their dash end position.
            if (menu::w_lantern_displace && a->is_dashing())
            {
                auto path = a->get_path();
                if (!path.empty())
                {
                    if (cast_lantern_pos(path.back())) return;
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // THROW LANTERN (hotkey) — nearest whitelisted ally
    // ---------------------------------------------------------------
    void throw_lantern_nearest()
    {
        if (!is_spell_ready(SLOT_W)) return;
        auto* p = get_player();
        if (!p || !g_sdk->object_manager) return;

        const auto ppos    = p->get_position();
        game_object* best  = nullptr;
        float        best_d = FLT_MAX;

        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_ally(a, spell_data::w_range + 175.f)) continue;
            const auto it = s_w_allies.find(a->get_network_id());
            if (it == s_w_allies.end() || !it->second) continue;
            const float d = dist2d(ppos, a->get_position());
            if (d < best_d) { best_d = d; best = a; }
        }

        if (best) cast_lantern_to(best);
    }

    // ---------------------------------------------------------------
    // E CAST — shared helper for combo + semi-manual.
    // pull = true  → flick mirrored across player (drag enemy toward us)
    // pull = false → flick toward target (push enemy away)
    // skip_q_chain = true honors the "don't flay during Q chain" guard
    // ---------------------------------------------------------------
    bool cast_e_with_direction(bool pull, bool skip_q_chain)
    {
        if (!is_spell_ready(SLOT_E)) return false;
        auto* p = get_player();
        if (!p) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] < 0.2f) return false;

        if (skip_q_chain)
        {
            if (is_q_successful()) return false;
            const float q_flight = spell_data::q_range / spell_data::q_speed + spell_data::q_delay;
            if (t - s_last_cast_time[SLOT_Q] < q_flight) return false;
        }

        if (!sdk::target_selector) return false;
        const auto ppos = p->get_position();

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, spell_data::e_range)) continue;
            if (is_unstoppable(h)) continue;

            auto pred = predict_e(h, pull, pred_sdk::hitchance::medium);
            if (!pred.is_valid || pred.hitchance < pred_sdk::hitchance::medium) continue;

            math::vector3 cast_pos = pred.cast_position;
            if (pull)
            {
                const float d = dist2d(ppos, cast_pos);
                cast_pos = extend2d(cast_pos, ppos, d * 2.f);
            }

            if (sdk::common && sdk::common->cast_spell(SLOT_E, cast_pos))
            {
                s_last_cast_time[SLOT_E] = t;
                if (menu::e_force_into_q)
                    s_force_eq_until = t + k_force_eq_window;
                return true;
            }
        }
        return false;
    }

    // ---------------------------------------------------------------
    // COMBO — E (Flay)
    // ---------------------------------------------------------------
    void try_combo_e()
    {
        if (!menu::combo_e) return;
        cast_e_with_direction(/*pull=*/!s_e_mode_push, /*skip_q_chain=*/true);
    }

    // ---------------------------------------------------------------
    // SEMI-MANUAL — E push / pull keys
    // ---------------------------------------------------------------
    void try_semi_e()
    {
        // Pull takes priority if both keys are held.
        if (s_semi_e_pull)
            cast_e_with_direction(/*pull=*/true,  /*skip_q_chain=*/false);
        else if (s_semi_e_push)
            cast_e_with_direction(/*pull=*/false, /*skip_q_chain=*/false);
    }

    // ---------------------------------------------------------------
    // COMBO — Q (Death Sentence)
    // ---------------------------------------------------------------
    void try_combo_q()
    {
        if (!menu::combo_q) return;
        if (!is_spell_ready(SLOT_Q)) return;
        if (is_q2()) return; // already hooked, waiting for Q2

        auto* p = get_player();
        if (!p) return;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_Q] < 0.15f) return;

        if (!sdk::target_selector) return;

        // Outer cull only — generous so prediction can still lead long shots.
        const float max_check = static_cast<float>(menu::q_max_range) + 200.f;

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, max_check)) continue;
            if (is_unstoppable(h)) continue;

            const auto it = s_q_enemies.find(h->get_network_id());
            if (it == s_q_enemies.end() || !it->second) continue;

            // Upper bound is enforced inside the prediction call (sd.range).
            auto pred = predict_q(h);
            if (!pred.is_valid) continue;

            // During the post-E "force into Q" window, drop the hitchance gate
            // to medium so the flick reliably chains into a hook.
            const bool in_eq_window = menu::e_force_into_q && t < s_force_eq_until;
            const int  required = in_eq_window
                ? std::min(q_hitchance_value(), (int)pred_sdk::hitchance::medium)
                : q_hitchance_value();
            if (pred.hitchance < required) continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_Q, pred.cast_position))
            {
                s_last_cast_time[SLOT_Q] = t;
                return;
            }
        }
    }

    // ---------------------------------------------------------------
    // COMBO — R (The Box)
    // ---------------------------------------------------------------
    void try_combo_r()
    {
        if (!menu::combo_r) return;
        if (!is_spell_ready(SLOT_R)) return;

        auto* p = get_player();
        if (!p) return;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_R] < 0.5f) return;

        const auto  ppos = p->get_position();
        int in_range = 0;
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::r_range)) continue;
            ++in_range;
        }

        if (in_range < menu::r_min_enemies) return;

        if (sdk::common && sdk::common->cast_spell(SLOT_R))
            s_last_cast_time[SLOT_R] = t;
    }

    // ---------------------------------------------------------------
    // ANTI GAP-CLOSE — tick fallback for ongoing dashes
    // ---------------------------------------------------------------
    void try_anti_gap_tick()
    {
        if (!menu::e_anti_gap) return;
        if (!is_spell_ready(SLOT_E)) return;

        auto* p = get_player();
        if (!p) return;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] < 0.2f) return;

        const auto ppos = p->get_position();

        if (!sdk::target_selector) return;
        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, spell_data::e_range)) continue;
            if (!h->is_dashing()) continue;

            auto path = h->get_path();
            if (path.empty()) continue;

            const auto end_pos  = path.back();
            const float d_dash  = dist2d(h->get_position(), end_pos);
            if (d_dash < 100.f) continue;

            const bool dashing_away = dist2d(ppos, end_pos) > dist2d(ppos, h->get_position());
            if (dashing_away)
            {
                auto pred = predict_e(h, /*pull*/ true, pred_sdk::hitchance::medium);
                if (!pred.is_valid) continue;
                const float pd = dist2d(ppos, pred.cast_position);
                const auto cast_pos = extend2d(pred.cast_position, ppos, pd * 2.f);
                if (sdk::common && sdk::common->cast_spell(SLOT_E, cast_pos))
                {
                    s_last_cast_time[SLOT_E] = t;
                    return;
                }
            }
            else
            {
                auto pred = predict_e(h, /*push*/ false, pred_sdk::hitchance::medium);
                if (!pred.is_valid) continue;
                if (sdk::common && sdk::common->cast_spell(SLOT_E, pred.cast_position))
                {
                    s_last_cast_time[SLOT_E] = t;
                    return;
                }
            }
        }
    }

    // ---------------------------------------------------------------
    // EVENTS
    // ---------------------------------------------------------------
    void __fastcall on_update()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;
        if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

        // Lantern expiry — only act while we have a position.
        if (s_lantern_pos.x > 1.f
            && now_time() - s_lantern_spawn_time >= spell_data::lantern_lifetime)
        {
            s_lantern_pos = { 0.f, 0.f, 0.f };
        }

        iterate_lantern_logic();
        try_anti_gap_tick();

        // Semi-manual keys work outside of combo mode.
        try_semi_e();

        if (!sdk::orbwalker) return;
        if (sdk::orbwalker->combo())
        {
            try_combo_r();
            try_combo_e();
            try_combo_q();
        }
    }

    void __fastcall on_draw_world()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;

        const auto pos = p->get_position();

        auto draw_circle = [&](float range, uint32_t color, int slot)
        {
            if (range <= 0.f || color == 0u) return;
            if (!is_spell_learned(slot)) return;
            if (menu::draw_only_ready && !is_spell_ready(slot)) return;
            if (sdk::shaders)
                sdk::shaders->draw_world_circle(pos, range, 2.f, color);
            else if (g_sdk->renderer)
                g_sdk->renderer->add_circle_3d(pos, range, 2.f, color);
        };

        if (menu::draw_q) draw_circle(static_cast<float>(menu::q_max_range), menu::color_q, SLOT_Q);
        if (menu::draw_e) draw_circle(spell_data::e_range, menu::color_e, SLOT_E);

        // Flash the max range briefly when its slider changes.
        const float t = now_time();
        if (t - s_last_max_range_change < 1.5f && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, static_cast<float>(menu::q_max_range), 2.f, 0xFFD3D3D3);

        // Show lantern position with a ring while it's alive.
        if (s_lantern_pos.x > 1.f && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(s_lantern_pos, 80.f, 3.f, menu::color_lantern);
    }

    void __fastcall on_present()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;
        if (!g_sdk->renderer) return;
        if (!menu::draw_e_mode) return;

        const auto screen = g_sdk->renderer->world_to_screen(p->get_position());
        if (screen.x <= 0.f && screen.y <= 0.f) return;

        const std::string text  = s_e_mode_push ? "[E] Push" : "[E] Pull";
        constexpr uint32_t color = 0xFF478778;
        constexpr float font_size = 14.f;
        const float approx_width = static_cast<float>(text.size()) * (font_size * 0.55f);

        math::vector2 pos{ screen.x - approx_width * 0.5f, screen.y + 28.f };
        g_sdk->renderer->add_text(text, font_size, pos, 0u, color);
    }

    void __fastcall on_create_object(game_object* obj)
    {
        if (!obj) return;
        auto* p = get_player();
        if (!p) return;
        // Only track our own lantern (other Thresh on the team also creates one).
        if (obj->get_team_id() != p->get_team_id()) return;
        if (obj->get_name() == "ThreshLantern")
        {
            s_lantern_pos        = obj->get_position();
            s_lantern_spawn_time = now_time();
        }
    }

    void __fastcall on_delete_object(game_object* obj)
    {
        if (!obj || s_lantern_pos.x <= 1.f) return;
        auto* p = get_player();
        if (!p || obj->get_team_id() != p->get_team_id()) return;
        const std::string name = obj->get_name();
        if (name.find("_Lantern_cas_green") != std::string::npos
         || name == "ThreshLantern")
        {
            s_lantern_pos = { 0.f, 0.f, 0.f };
        }
    }

    // ---------------------------------------------------------------
    // ORBWALKER — block AA when E is up or Q is mid-pull.
    // ---------------------------------------------------------------
    bool __stdcall on_before_attack(orb_sdk::event_data* /*data*/)
    {
        auto* p = get_player();
        if (!p) return true;

        // While Q is pulling, AA cancels the lantern leap setup.
        if (buff_contains_ci(p, "threshqpullmissile")) return false;

        // While E is ready in combo, hold AA so the flay doesn't get cancelled.
        if (sdk::orbwalker && sdk::orbwalker->combo()
            && menu::combo_e && is_spell_ready(SLOT_E))
        {
            return false;
        }

        return true;
    }

    void __fastcall on_new_path(game_object* obj, bool is_dash, float /*dash_speed*/)
    {
        if (!is_dash || !obj) return;
        auto* p = get_player();
        if (!p) return;
        if (obj->get_team_id() == p->get_team_id()) return;

        auto path = obj->get_path();
        if (path.size() < 2) return;

        const auto start_pos = path.front();
        const auto end_pos   = path.back();
        const float dash_dist = dist2d(start_pos, end_pos);

        if (dash_dist < 250.f) return;

        const float t = now_time();
        const auto  ppos = p->get_position();

        // Anti gap-close: E into the dashing enemy.
        if (is_spell_ready(SLOT_E) && t - s_last_cast_time[SLOT_E] >= 0.2f)
        {
            const bool dashing_away = dist2d(ppos, end_pos) > dist2d(ppos, start_pos);
            if (dashing_away)
            {
                auto pred = predict_e(obj, /*pull*/ true, pred_sdk::hitchance::medium);
                if (pred.is_valid && pred.hitchance >= pred_sdk::hitchance::medium)
                {
                    const float d = dist2d(ppos, pred.cast_position);
                    const auto cast_pos = extend2d(pred.cast_position, ppos, d * 2.f);
                    if (sdk::common && sdk::common->cast_spell(SLOT_E, cast_pos))
                        s_last_cast_time[SLOT_E] = t;
                }
            }
            else
            {
                auto pred = predict_e(obj, /*push*/ false, pred_sdk::hitchance::medium);
                if (pred.is_valid && pred.hitchance >= pred_sdk::hitchance::medium)
                {
                    if (sdk::common && sdk::common->cast_spell(SLOT_E, pred.cast_position))
                        s_last_cast_time[SLOT_E] = t;
                }
            }
        }

        // Lantern on gap-close toward a whitelisted ally.
        if (!menu::w_lantern_gapclose || !is_spell_ready(SLOT_W)) return;
        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_ally(a, spell_data::w_range + 300.f)) continue;
            if (dist2d(ppos, a->get_position()) < 200.f) continue;
            const auto it = s_w_allies.find(a->get_network_id());
            if (it == s_w_allies.end() || !it->second) continue;
            if (dist2d(end_pos, a->get_position()) <= 250.f && dash_dist >= 300.f)
            {
                cast_lantern_to(a);
                return;
            }
        }
    }

    // ---------------------------------------------------------------
    // MENU
    // ---------------------------------------------------------------
    void build_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_thresh", "MeowAIO - Thresh");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Thresh: failed to create menu category");
            return;
        }

        // --- Q / Death Sentence ---
        if (auto* q = s_menu->add_sub_category("q_death_sentence", "Q - Death Sentence"))
        {
            q->add_checkbox("combo_q", "Use in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            q->add_slider_int("hitchance_q", "Hitchance %", 0, 100, 5, menu::q_hitchance,
                [](int v) { menu::q_hitchance = v; });
            q->add_slider_int("max_range_q", "Maximum Range", 450, 1100, 50, menu::q_max_range,
                [](int v)
                {
                    menu::q_max_range = v;
                    s_last_max_range_change = (g_sdk && g_sdk->clock_facade)
                        ? g_sdk->clock_facade->get_game_time() : 0.f;
                });
            q->add_separator();
            q->add_checkbox("draw_q", "Draw Q Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });

            if (auto* wl = q->add_sub_category("q_wl", "Use Q on:"))
            {
                auto* p = get_player();
                if (p && g_sdk->object_manager)
                {
                    for (auto* h : g_sdk->object_manager->get_heroes())
                    {
                        if (!h) continue;
                        if (h->get_team_id() == p->get_team_id()) continue;
                        const uint32_t    id    = h->get_network_id();
                        const std::string key   = "qe_" + std::to_string(id);
                        const std::string title = h->get_char_name();
                        s_q_enemies[id] = true;
                        wl->add_checkbox(key, title, true,
                            [id](bool v) { s_q_enemies[id] = v; });
                    }
                }
            }
        }

        // --- W / Dark Passage ---
        if (auto* w = s_menu->add_sub_category("w_dark_passage", "W - Dark Passage"))
        {
            w->add_hotkey("throw_lantern", "Throw Lantern (nearest ally)", 'G',
                false, false,
                [](std::string*, bool pressed) { if (pressed) throw_lantern_nearest(); });

            w->add_separator();
            w->add_label("Auto-throw Lantern");
            w->add_checkbox("lantern_q_hit", "Q hits an enemy",
                menu::w_lantern_q_hit,
                [](bool v) { menu::w_lantern_q_hit = v; });
            w->add_checkbox("lantern_displace", "Ally is displaced (knockback)",
                menu::w_lantern_displace,
                [](bool v) { menu::w_lantern_displace = v; });
            w->add_checkbox("lantern_gapclose", "Enemy gap-closes toward ally",
                menu::w_lantern_gapclose,
                [](bool v) { menu::w_lantern_gapclose = v; });
            w->add_checkbox("lantern_cc", "Ally is under hard CC",
                menu::w_lantern_cc,
                [](bool v) { menu::w_lantern_cc = v; });

            if (auto* wl = w->add_sub_category("w_wl", "Throw W to:"))
            {
                auto* p = get_player();
                if (p && g_sdk->object_manager)
                {
                    for (auto* a : g_sdk->object_manager->get_heroes())
                    {
                        if (!a) continue;
                        if (a->get_team_id() != p->get_team_id()) continue;
                        if (a->get_network_id() == p->get_network_id()) continue;
                        const uint32_t    id    = a->get_network_id();
                        const std::string key   = "wa_" + std::to_string(id);
                        const std::string title = a->get_char_name();
                        s_w_allies[id] = true;
                        wl->add_checkbox(key, title, true,
                            [id](bool v) { s_w_allies[id] = v; });
                    }
                }
            }
        }

        // --- E / Flay ---
        if (auto* e = s_menu->add_sub_category("e_flay", "E - Flay"))
        {
            e->add_checkbox("combo_e", "Use in Combo", menu::combo_e,
                [](bool v) { menu::combo_e = v; });
            e->add_combo("e_mode", "E Direction", { "Pull", "Push" }, menu::e_mode,
                [](int v) { menu::e_mode = v; s_e_mode_push = (v == 1); });
            // Momentary key press flips the live mode; combo and hotkey stay
            // unified through s_e_mode_push.
            e->add_hotkey("toggle_e", "Flip E Direction (press)", 'T', false, false,
                [](std::string*, bool pressed)
                {
                    if (pressed) s_e_mode_push = !s_e_mode_push;
                });
            e->add_checkbox("e_anti_gap", "Anti Gap-Close", menu::e_anti_gap,
                [](bool v) { menu::e_anti_gap = v; });
            e->add_checkbox("e_force_eq", "Force E into Q (relax Q hitchance after E)",
                menu::e_force_into_q,
                [](bool v) { menu::e_force_into_q = v; });
            e->add_separator();
            e->add_hotkey("semi_e_push", "E Push", 'X', false, false,
                [](std::string*, bool held) { s_semi_e_push = held; });
            e->add_hotkey("semi_e_pull", "E Pull", 'Z', false, false,
                [](std::string*, bool held) { s_semi_e_pull = held; });
            e->add_separator();
            e->add_checkbox("draw_e_mode", "Show E Mode on Screen", menu::draw_e_mode,
                [](bool v) { menu::draw_e_mode = v; });
            e->add_checkbox("draw_e", "Draw E Range", menu::draw_e,
                [](bool v) { menu::draw_e = v; });
        }

        // --- R / The Box ---
        if (auto* r = s_menu->add_sub_category("r_the_box", "R - The Box"))
        {
            r->add_checkbox("combo_r", "Use in Combo", menu::combo_r,
                [](bool v) { menu::combo_r = v; });
            r->add_slider_int("r_min_enemies", "Only if Enemies in Range",
                1, 5, 1, menu::r_min_enemies,
                [](int v) { menu::r_min_enemies = v; });
        }

        // --- Drawings ---
        if (auto* d = s_menu->add_sub_category("drawings", "Drawings"))
        {
            d->add_checkbox("only_ready", "Only Draw Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            d->add_separator();
            d->add_colorpicker("color_q", "Q Color", menu::color_q,
                [](uint32_t v) { menu::color_q = v; });
            d->add_colorpicker("color_e", "E Color", menu::color_e,
                [](uint32_t v) { menu::color_e = v; });
            d->add_colorpicker("color_lantern", "Lantern Color", menu::color_lantern,
                [](uint32_t v) { menu::color_lantern = v; });
        }
    }

} // anonymous namespace

// ---------------------------------------------------------------
// PUBLIC INTERFACE
// ---------------------------------------------------------------
void load()
{
    auto* p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->get_char_name() != "Thresh")
        return;

    build_menu();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->register_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
            reinterpret_cast<void*>(on_before_attack));
    }

    g_sdk->log_console("[MeowAIO] Thresh loaded MEOW!");
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->unregister_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->unregister_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
            reinterpret_cast<void*>(on_before_attack));
    }

    s_menu     = nullptr;
    s_q_enemies.clear();
    s_w_allies.clear();
    s_lantern_pos    = { 0.f, 0.f, 0.f };
    s_e_mode_push    = false;
    s_semi_e_push    = false;
    s_semi_e_pull    = false;
    s_force_eq_until = 0.f;
    for (float& v : s_last_cast_time) v = 0.f;

    if (g_sdk) g_sdk->log_console("[-] Thresh unloaded");
}

} // namespace thresh
