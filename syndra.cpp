#include "syndra.h"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"
#include "damage/calc.hpp"
#include "db/champions/syndra.hpp"

namespace syndra
{
    static menu_category* s_menu = nullptr;

    // --- Passive splinter count (0..100) ---
    static int  passive_stacks = 0;
    static bool r_execute_buff_active = false;

    // --- Sphere tracking ---
    static constexpr int MAX_TRACKED_SPHERES = 10;
    struct tracked_sphere
    {
        game_object*  obj           = nullptr;
        float         creation_time = 0.f;
        float         hold_start    = 0.f;
        bool          is_w_held     = false;
        bool          active        = false;

        bool is_alive() const noexcept
        {
            return active && obj && obj->is_valid() && !obj->is_dead();
        }

        math::vector3 position() const noexcept
        {
            return obj ? obj->get_position() : math::vector3{};
        }

        float elapsed(float now) const noexcept
        {
            return is_w_held ? (hold_start - creation_time) : (now - creation_time);
        }

        float remaining(float now) const noexcept
        {
            return spell_data::q_sphere_duration - elapsed(now);
        }
    };
    static tracked_sphere spheres[MAX_TRACKED_SPHERES] = {};
    static int            ground_sphere_count = 0;
    static int            r_total_spheres     = spell_data::r_base_spheres;

    // --- W state ---
    static bool          w_holding        = false;
    static float         w_grab_time      = 0.f;
    static math::vector3 w_grab_pos       = {};
    static bool          w_sphere_pending = false;
    static float         last_w_cast_time = 0.f;

    // --- Cast pacing ---
    static constexpr float MIN_SPELL_DELAY = 0.05f;
    static float           last_spell_cast_time = 0.f;

    // ============================================================================
    // FORWARD DECLARATIONS — helpers used before their definition site
    // ============================================================================
    static void scan_antigap_qe();

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
        if (!s || s->get_level() <= 0) return false;
        if (slot == SLOT_Q && s->get_charges() > 0) return true;
        return s->get_cooldown() <= 0.f;
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
        if (!ac) return false;
        auto sc = ac->get_spell_cast();
        if (!sc) return false;
        // Ignore basic-attack windups — they register as active casts but don't
        // block spells. Treating them as "casting" gates every spell until the
        // AA finishes, making the champ feel locked to AA timing.
        if (sc->is_basic_attack() || sc->is_special_attack()) return false;
        return ac->get_cast_end_time() > g_sdk->clock_facade->get_game_time();
    }

    float mana_pct()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const float mx = p->get_max_par();
        if (mx <= 0.f) return 0.f;
        return (p->get_par() / mx) * 100.f;
    }

    // ============================================================================
    // PASSIVE THRESHOLD HELPERS
    // ============================================================================
    static bool has_w_true_dmg()  { return passive_stacks >= spell_data::splinter_w_true_dmg; }
    static bool has_e_wide_cone() { return passive_stacks >= spell_data::splinter_e_wide_cone; }
    static bool has_r_execute()
    {
        return r_execute_buff_active || passive_stacks >= spell_data::splinter_r_execute;
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
            const float d = p->get_position().distance(unit->get_position());
            if (d > range) return false;
        }
        return true;
    }

    static bool is_valid_enemy_minion(game_object* m)
    {
        if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable())
            return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (m->get_team_id() == p->get_team_id()) return false;
        return m->is_lane_minion();
    }

    static bool is_enemy_hero_near(float range)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pos = p->get_position();
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pos.distance(h->get_position()) <= range) return true;
        }
        return false;
    }

    // Walks TS's priority-sorted heroes and returns the first one within `range`.
    // Bypasses get_hero_target's internal AA-range cap so longer-range spells
    // can pick targets at their actual range.
    static game_object* pick_hero_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target()) {
            if (is_valid_target(forced, range)) return forced;
        }
        for (auto* h : sdk::target_selector->get_sorted_heroes()) {
            if (is_valid_target(h, range)) return h;
        }
        return nullptr;
    }

    // ============================================================================
    // SPHERE TRACKING
    // ============================================================================
    static void add_sphere(game_object* obj)
    {
        if (!obj) return;
        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            if (spheres[i].active && spheres[i].obj == obj) return;
        }
        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            if (!spheres[i].active) {
                spheres[i] = {};
                spheres[i].obj           = obj;
                spheres[i].creation_time = g_sdk->clock_facade->get_game_time();
                spheres[i].active        = true;
                return;
            }
        }
    }

    static void remove_sphere(game_object* obj)
    {
        if (!obj) return;
        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            if (spheres[i].active && spheres[i].obj == obj) {
                if (spheres[i].is_w_held) {
                    w_holding   = false;
                    w_grab_time = 0.f;
                }
                spheres[i] = {};
                return;
            }
        }
    }

    static void cleanup_spheres()
    {
        const float now = g_sdk->clock_facade->get_game_time();
        ground_sphere_count = 0;

        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            auto& s = spheres[i];
            if (!s.active) continue;

            if (!s.is_alive() || s.remaining(now) <= 0.f) {
                if (s.is_w_held) {
                    w_holding   = false;
                    w_grab_time = 0.f;
                }
                s = {};
                continue;
            }

            if (!s.is_w_held) ++ground_sphere_count;
        }

        const int capped = ground_sphere_count > spell_data::r_max_grab_spheres
                         ? spell_data::r_max_grab_spheres : ground_sphere_count;
        r_total_spheres = capped + spell_data::r_base_spheres;
    }

    static tracked_sphere* find_nearest_ground_sphere(const math::vector3& pos, float max_range)
    {
        tracked_sphere* best = nullptr;
        float best_d = FLT_MAX;
        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            auto& s = spheres[i];
            if (!s.is_alive() || s.is_w_held) continue;
            const float d = pos.distance(s.position());
            if (d <= max_range && d < best_d) {
                best_d = d;
                best = &s;
            }
        }
        return best;
    }

    // ============================================================================
    // W THROW DETECTION (sync sphere state when W actually fires)
    // ============================================================================
    static void on_w_throw_observed()
    {
        const float now = g_sdk->clock_facade->get_game_time();
        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            auto& s = spheres[i];
            if (!s.active || !s.is_w_held) continue;
            // Time spent held doesn't count against sphere lifetime
            s.creation_time += now - s.hold_start;
            s.is_w_held      = false;
            s.hold_start     = 0.f;
        }
        w_holding        = false;
        w_grab_time      = 0.f;
        w_sphere_pending = false;
        w_grab_pos       = {};
    }

    // ============================================================================
    // DAMAGE HELPERS
    // ============================================================================
    static venplus::damage::caster_stats make_caster(game_object* p)
    {
        return {
            .ap       = p->get_ability_power(),
            .bonus_ad = p->get_attack_damage() - p->get_base_attack_damage(),
            .total_ad = p->get_attack_damage(),
            .level    = static_cast<float>(p->get_level()),
        };
    }
    static venplus::damage::target_info make_target(game_object* t)
    {
        return {
            .max_hp      = t->get_max_hp(),
            .curr_hp     = t->get_hp(),
            .armor       = t->get_armor(),
            .bonus_armor = t->get_bonus_armor(),
            .mr          = t->get_magic_resist(),
            .bonus_mr    = t->get_bonus_magic_resist(),
        };
    }
    static venplus::damage::pen_stats make_magic_pen(game_object* p)
    {
        return {
            .pct_bonus_pen = p->get_percent_bonus_magic_penetration(),
            .pct_total_pen = p->get_percent_magic_penetration(),
            .flat_pen      = p->get_flat_magic_penetration(),
        };
    }

    // Returns the post-mitigation damage of one R sphere.
    static float r_damage_per_sphere(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return 0.f;
        auto* r_sp = p->get_spell(SLOT_R);
        const int rank = r_sp ? r_sp->get_level() : 0;
        if (rank <= 0) return 0.f;

        const auto& dmg_entry = venplus::db::champions::SYNDRA.spells[SLOT_R].damage;
        venplus::damage::pen_stats armor_pen{};
        return venplus::damage::final_spell_damage(
            dmg_entry, rank - 1,
            make_caster(p), make_target(target),
            armor_pen, make_magic_pen(p));
    }

    static float get_w_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return 0.f;
        auto* w_sp = p->get_spell(SLOT_W);
        const int rank = w_sp ? w_sp->get_level() : 0;
        if (rank <= 0) return 0.f;

        const auto& dmg_entry = venplus::db::champions::SYNDRA.spells[SLOT_W].damage;
        venplus::damage::pen_stats armor_pen{};
        float dmg = venplus::damage::final_spell_damage(
            dmg_entry, rank - 1,
            make_caster(p), make_target(target),
            armor_pen, make_magic_pen(p));

        if (has_w_true_dmg()) {
            const float ap    = p->get_ability_power();
            const float scale = spell_data::w_true_dmg_base
                              + spell_data::w_true_dmg_scale * (ap / 100.f);
            dmg += dmg * scale;
        }
        return dmg;
    }

    static float get_r_damage(game_object* target, int total_spheres)
    {
        return r_damage_per_sphere(target) * static_cast<float>(total_spheres);
    }

    static bool can_r_kill(game_object* target, int total_spheres)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return false;

        const float dmg = get_r_damage(target, total_spheres);
        if (dmg <= 0.f) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        float hp = target->get_hp() + target->get_all_shield();
        if (sdk::health_prediction)
            hp = sdk::health_prediction->get_predicted_health(target, now + spell_data::r_delay)
               + target->get_all_shield();
        if (hp <= 0.f) return false;

        if (has_r_execute()) {
            const float threshold = target->get_max_hp() * spell_data::r_execute_pct;
            const float required  = hp - threshold;
            return required <= 0.f || dmg >= required;
        }
        return dmg >= hp * spell_data::r_overkill_factor;
    }

    static int count_ground_spheres() { return ground_sphere_count; }
    static int count_r_spheres()      { return r_total_spheres; }

    // ============================================================================
    // PREDICTION (crash-safe — only safe scalar fields, try/catch wrapped)
    // ============================================================================
    static pred_sdk::pred_data predict_safe(game_object* target,
                                            pred_sdk::spell_type type,
                                            int slot,
                                            float range,
                                            float radius,
                                            float delay,
                                            float speed,
                                            int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = type;
        spell.targetting_type    = pred_sdk::targetting_type::center;
        spell.spell_slot         = slot;
        spell.range              = range;
        spell.radius             = radius;
        spell.delay              = delay;
        spell.projectile_speed   = speed;
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    bool cast_q_at(const math::vector3& pos)
    {
        if (!sdk::common) return false;
        // Never drop a Q sphere while W is mid-grab: it stalls the throw and the
        // fresh Seed can be mis-tagged as the held sphere in on_create_object.
        if (w_holding) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        math::vector3 cast_pos = pos;
        const auto player_pos = p->get_position();
        if (player_pos.distance(pos) > spell_data::q_range)
            cast_pos = player_pos.extended(pos, spell_data::q_range);

        bool ok = sdk::common->cast_spell(SLOT_Q, cast_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    bool cast_q_on(game_object* target, int hitchance)
    {
        if (!target || !sdk::prediction || !sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (p->get_position().distance(target->get_position()) > spell_data::q_effective_range)
            return false;

        auto pred = predict_safe(target, pred_sdk::spell_type::circular, SLOT_Q,
                                 spell_data::q_effective_range, spell_data::q_radius,
                                 spell_data::q_proc_delay, FLT_MAX, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        return cast_q_at(pred.cast_position);
    }

    static bool issue_w_cast(const math::vector3& pos)
    {
        if (!sdk::common) return false;
        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_w_cast_time < spell_data::w_cast_cooldown) return false;
        bool ok = sdk::common->cast_spell(SLOT_W, pos);
        if (ok) {
            last_w_cast_time     = now;
            last_spell_cast_time = now;
        }
        return ok;
    }

    bool cast_w_throw(game_object* target, int hitchance, bool aggressive)
    {
        if (!target || !w_holding) return false;
        if (!target->is_hero()) return false;

        const float now  = g_sdk->clock_facade->get_game_time();
        const float held = now - w_grab_time;
        if (held < spell_data::w_min_hold_time) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const float dist          = p->get_position().distance(target->get_position());
        const float w_range       = spell_data::w_throw_range;
        const float min_pred_dist = target->get_bounding_radius() * 3.f;
        const bool  held_too_long = held > spell_data::w_force_throw_time;

        if (dist > w_range) {
            // Force-throw if we've held too long
            if (held_too_long)
                return issue_w_cast(target->get_server_position());
            return false;
        }

        // Point-blank: prediction adds no value, just cast at the target.
        if (dist <= min_pred_dist || !sdk::prediction)
            return issue_w_cast(target->get_server_position());

        auto pred = predict_safe(target, pred_sdk::spell_type::circular, SLOT_W,
                                 w_range, spell_data::w_throw_radius,
                                 spell_data::w_throw_delay, spell_data::w_throw_speed,
                                 hitchance);
        if (pred.is_valid && pred.hitchance >= hitchance)
            return issue_w_cast(pred.cast_position);

        // Target is in range but prediction is below the requested hitchance.
        // In aggressive (combo) mode we throw anyway instead of sitting on the
        // sphere — W is a fast, wide missile and holding stalls the whole combo.
        // Outside aggressive mode we only dump it once we've held too long.
        if (aggressive || held_too_long) {
            if (pred.is_valid) return issue_w_cast(pred.cast_position);
            return issue_w_cast(target->get_server_position());
        }
        return false;
    }

    bool cast_w_grab()
    {
        if (w_holding || !is_spell_ready(SLOT_W)) return false;
        if (!has_enough_mana(SLOT_W)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto player_pos = p->get_position();

        // Prefer one of our spheres
        if (auto* sphere = find_nearest_ground_sphere(player_pos, spell_data::w_grab_range)) {
            return issue_w_cast(sphere->position());
        }

        // Otherwise grab a unit (heavy minions / monsters preferred)
        game_object* best_unit = nullptr;
        float        best_d    = FLT_MAX;
        int          best_pri  = -1;

        auto evaluate = [&](game_object* u, int pri) {
            if (!u || !u->is_valid() || u->is_dead() || !u->is_targetable()) return;
            if (u->get_team_id() == p->get_team_id()) return;
            const float d = player_pos.distance(u->get_position());
            if (d > spell_data::w_grab_range) return;
            if (pri > best_pri || (pri == best_pri && d < best_d)) {
                best_pri = pri; best_d = d; best_unit = u;
            }
        };

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!m || !m->is_lane_minion()) continue;
            int pri = 2;
            if (m->is_lane_minion_siege() || m->is_lane_minion_super()) pri = 3;
            evaluate(m, pri);
        }
        for (auto* m : g_sdk->object_manager->get_monsters()) {
            evaluate(m, 0);
        }

        if (!best_unit) return false;
        return issue_w_cast(best_unit->get_position());
    }

    bool cast_e_cone(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (w_holding) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const float dist = p->get_position().distance(target->get_position());
        if (dist > spell_data::e_cone_range) return false;

        // Cone modeled as linear with pseudo-radius = tan(half_angle) * range
        const float half_angle = (has_e_wide_cone() ? spell_data::e_transcendent_angle
                                                    : spell_data::e_base_angle) * 0.5f;
        const float pseudo_radius = std::tan(deg2rad(half_angle)) * spell_data::e_cone_range;

        auto pred = predict_safe(target, pred_sdk::spell_type::linear, SLOT_E,
                                 spell_data::e_cone_range, pseudo_radius,
                                 spell_data::e_cast_delay, spell_data::e_cone_speed,
                                 menu::combo_hitchance);
        if (!pred.is_valid) return false;

        bool ok = sdk::common->cast_spell(SLOT_E, pred.cast_position);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // Find a ground sphere whose push line through `target` would actually catch them.
    bool cast_e_push(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (w_holding) return false;
        if (count_ground_spheres() <= 0) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto player_pos = p->get_position();
        const auto target_pos = target->get_server_position();
        const float hit_width = spell_data::e_push_radius + target->get_bounding_radius();

        int   best_idx  = -1;
        float best_miss = FLT_MAX;

        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
            const auto& s = spheres[i];
            if (!s.is_alive() || s.is_w_held) continue;

            const auto sp = s.position();
            const float sphere_dist = player_pos.distance(sp);
            if (sphere_dist < 50.f || sphere_dist > spell_data::e_sphere_range + 50.f) continue;

            const auto dir = (sp - player_pos).normalized();
            const float push_max = spell_data::e_max_push_range - sphere_dist;
            if (push_max <= 0.f) continue;

            const float rel_x = target_pos.x - sp.x;
            const float rel_z = target_pos.z - sp.z;
            const float proj  = rel_x * dir.x + rel_z * dir.z;
            if (proj < -hit_width || proj > push_max + hit_width) continue;

            const float clamped = std::max(0.f, std::min(proj, push_max));
            const float closest_x = sp.x + dir.x * clamped;
            const float closest_z = sp.z + dir.z * clamped;
            const float dx = target_pos.x - closest_x;
            const float dz = target_pos.z - closest_z;
            const float miss = std::sqrt(dx * dx + dz * dz);

            if (miss <= hit_width && miss < best_miss) {
                best_miss = miss;
                best_idx  = i;
            }
        }

        if (best_idx < 0) return false;

        bool ok = sdk::common->cast_spell(SLOT_E, spheres[best_idx].position());
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // QE chain: place Q sphere on the line to target, then E it for stun + push.
    bool cast_qe_combo(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (w_holding) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto player_pos = p->get_position();
        const auto target_pos = target->get_server_position();
        const float dist = player_pos.distance(target_pos);
        if (dist > spell_data::e_max_push_range) return false;

        const float cast_dist = std::min(dist, spell_data::q_range);
        const auto sphere_pos = player_pos.extended(target_pos, cast_dist);

        if (!sdk::common->cast_spell(SLOT_Q, sphere_pos)) return false;
        sdk::common->cast_spell(SLOT_E, sphere_pos);
        last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return true;
    }

    // Auto QE chain for combo: predicts the Q landing on the target, drops the
    // sphere there and fires E at it on the same tick. The cone arrives ~0.4s
    // later just as the sphere settles, so the target gets stunned + pushed in
    // one motion instead of waiting a full Q-then-E cycle.
    static bool try_combo_fast_qe(game_object* target)
    {
        if (!menu::combo_fast_qe) return false;
        if (!menu::combo_use_q || !menu::combo_use_e) return false;
        if (!target || !sdk::prediction || !sdk::common) return false;
        if (w_holding) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto player_pos = p->get_position();
        if (player_pos.distance(target->get_position()) > spell_data::e_max_push_range)
            return false;

        // Predict where the target will be when Q lands.
        auto pred = predict_safe(target, pred_sdk::spell_type::circular, SLOT_Q,
                                 spell_data::q_effective_range, spell_data::q_radius,
                                 spell_data::q_proc_delay, FLT_MAX, menu::combo_hitchance);
        if (!pred.is_valid || pred.hitchance < menu::combo_hitchance) return false;

        math::vector3 sphere_pos = pred.cast_position;
        if (player_pos.distance(sphere_pos) > spell_data::q_range)
            sphere_pos = player_pos.extended(sphere_pos, spell_data::q_range);

        if (!sdk::common->cast_spell(SLOT_Q, sphere_pos)) return false;
        sdk::common->cast_spell(SLOT_E, sphere_pos);
        last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return true;
    }

    bool cast_r(game_object* target, bool force)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const float dist = p->get_position().distance(target->get_position());
        if (dist > spell_data::r_range + target->get_bounding_radius()) return false;

        if (!force) {
            if (venplus::utility::has_anti_burst_buff(target)) return false;
            if (target->has_buff_of_type(buff_type::spell_shield)) return false;

            if (menu::combo_r_only_kill && !can_r_kill(target, count_r_spheres()))
                return false;
        }

        bool ok = sdk::common->cast_spell(SLOT_R, target);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // ============================================================================
    // FARM HELPERS
    // ============================================================================
    static bool has_nearby_lane_minions(float range)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pos = p->get_position();
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            if (pos.distance(m->get_position()) <= range) return true;
        }
        return false;
    }

    static int count_minions_in_aoe(const math::vector3& center, float radius)
    {
        int n = 0;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const float allow = radius + m->get_bounding_radius();
            const float d = center.distance(m->get_position());
            if (d <= allow) ++n;
        }
        return n;
    }

    static bool find_best_q_farm(int min_hits, math::vector3& out_pos)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto player_pos = p->get_position();

        int best_hits = 0;
        math::vector3 best_pos{};
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            if (player_pos.distance(mp) > spell_data::q_range) continue;
            const int hits = count_minions_in_aoe(mp, spell_data::q_radius);
            if (hits > best_hits) { best_hits = hits; best_pos = mp; }
        }
        if (best_hits >= min_hits) { out_pos = best_pos; return true; }
        return false;
    }

    static void try_q_lasthit()
    {
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::health_prediction || !sdk::damage) return;

        const auto player_pos = p->get_position();
        const float now = g_sdk->clock_facade->get_game_time();

        game_object* best = nullptr;
        bool         best_cannon = false;

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const float d = player_pos.distance(m->get_position());
            if (d > spell_data::q_effective_range) continue;

            const float hp  = sdk::health_prediction->get_predicted_health(m, now + spell_data::q_proc_delay);
            const float dmg = sdk::damage->get_spell_damage(p, m, SLOT_Q);
            if (hp <= 0.f || dmg < hp) continue;

            const bool is_cannon = m->is_lane_minion_siege() || m->is_lane_minion_super();
            if (is_cannon) { best = m; best_cannon = true; break; }

            if (!best && !best_cannon) {
                const bool aa_not_ready = sdk::orbwalker && !sdk::orbwalker->is_attack_ready_in(0.f);
                const bool out_of_aa    = sdk::orbwalker && !sdk::orbwalker->is_in_auto_attack_range(p, m);
                if (aa_not_ready || out_of_aa) best = m;
            }
        }

        if (best) cast_q_at(best->get_position());
    }

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo()
    {
        if (!sdk::target_selector) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::e_stun_range);
        if (!target) return;

        const float dist = p->get_position().distance(target->get_position());

        // 1) R kill if available — gated by Save-R toggle
        if (menu::combo_use_r && !menu::save_r_active
            && is_spell_ready(SLOT_R) && has_enough_mana(SLOT_R)
            && dist <= spell_data::r_range) {
            if (cast_r(target)) return;
        }

        // 2) W throw if currently holding.
        if (menu::combo_use_w && w_holding) {
            if (cast_w_throw(target, menu::combo_hitchance, /*aggressive=*/true)) return;
        }

        // 3) E sphere push — when Q is on CD but a ground sphere is up,
        //    this is the fallback stun. Runs whenever a sphere is on the
        //    ground and can hit the target.
        if (menu::combo_use_e && !w_holding && count_ground_spheres() > 0) {
            if (cast_e_push(target)) return;
        }

        // 4) Fast QE chain — drop a fresh sphere + E it in the same tick.
        if (try_combo_fast_qe(target)) return;

        // 6) E direct cone (in close)
        if (menu::combo_use_e && !w_holding && dist <= spell_data::e_cone_range) {
            if (cast_e_cone(target)) return;
        }

        // 6) Q at hero (fallback: Q only, when E is on cd / push not viable)
        if (menu::combo_use_q) {
            if (cast_q_on(target, menu::combo_hitchance)) return;
        }

        // 7) W grab — pull a minion / sphere within range
        if (menu::combo_use_w && !w_holding) {
            cast_w_grab();
        }
    }

    void harass()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!sdk::target_selector) return;

        float max_range = spell_data::q_effective_range;
        if (menu::harass_use_w) max_range = std::max(max_range, spell_data::w_throw_range);
        if (menu::harass_use_e) max_range = std::max(max_range, spell_data::e_stun_range);

        auto* target = pick_hero_target(max_range);
        if (!target) return;

        if (menu::harass_use_w && w_holding) {
            if (cast_w_throw(target, menu::harass_hitchance)) return;
        }
        if (menu::harass_use_e && !w_holding && count_ground_spheres() > 0) {
            if (cast_e_push(target)) return;
        }
        if (menu::harass_use_q) {
            if (cast_q_on(target, menu::harass_hitchance)) return;
        }
        if (menu::harass_use_w && !w_holding) {
            cast_w_grab();
        }
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!has_nearby_lane_minions(spell_data::q_range + 200.f)) return;

        // W AOE throw onto minion stack (only when held and safe)
        if (menu::clear_use_w && w_holding
            && !is_enemy_hero_near(spell_data::w_throw_range + 300.f)
            && (now - w_grab_time) >= spell_data::w_min_hold_time) {

            const auto player_pos = p->get_position();
            int best_hits = 0;
            math::vector3 best_pos{};
            for (auto* m : g_sdk->object_manager->get_minions()) {
                if (!is_valid_enemy_minion(m)) continue;
                const auto mp = m->get_position();
                if (player_pos.distance(mp) > spell_data::w_throw_range) continue;
                const int hits = count_minions_in_aoe(mp, spell_data::w_throw_radius);
                if (hits > best_hits) { best_hits = hits; best_pos = mp; }
            }
            if (best_hits >= 3) {
                if (issue_w_cast(best_pos)) return;
            }
        }

        // Q AOE on minion stack
        if (menu::clear_use_q && !w_holding && is_spell_ready(SLOT_Q)
            && (!sdk::orbwalker->would_cancel_attack())) {
            math::vector3 pos{};
            if (find_best_q_farm(menu::clear_q_min_hits, pos)) {
                if (cast_q_at(pos)) return;
            }
        }

        // No sphere up — grab one for next cycle
        if (menu::clear_use_w && !w_holding && is_spell_ready(SLOT_W)) {
            cast_w_grab();
        }
    }

    void fast_clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!has_nearby_lane_minions(spell_data::e_max_push_range + 200.f)) return;

        // QE corridor on a minion line
        if (menu::fast_clear_use_q && menu::fast_clear_use_e && !w_holding
            && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)
            && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)) {

            const auto player_pos = p->get_position();
            const float push_max  = spell_data::e_stun_range;
            const float push_w    = spell_data::e_push_radius;

            int best_hits = 0;
            math::vector3 best_pos{};
            for (auto* anchor : g_sdk->object_manager->get_minions()) {
                if (!is_valid_enemy_minion(anchor)) continue;
                const auto ap = anchor->get_position();
                const float ad = player_pos.distance(ap);
                if (ad < 50.f || ad > push_max) continue;

                const float cast_dist = std::min(ad, spell_data::q_range);
                const auto q_pos    = player_pos.extended(ap, cast_dist);
                const auto push_end = player_pos.extended(ap, push_max, true);

                int hits = 0;
                for (auto* m : g_sdk->object_manager->get_minions()) {
                    if (!is_valid_enemy_minion(m)) continue;
                    const auto mp = m->get_position();
                    const float br = m->get_bounding_radius();
                    if (q_pos.distance(mp) <= spell_data::q_radius + br) { ++hits; continue; }

                    // Point-to-segment 2D distance for push corridor
                    const float seg_dx = push_end.x - q_pos.x;
                    const float seg_dz = push_end.z - q_pos.z;
                    const float seg_len2 = seg_dx * seg_dx + seg_dz * seg_dz;
                    if (seg_len2 <= 1.f) continue;
                    const float rel_dx = mp.x - q_pos.x;
                    const float rel_dz = mp.z - q_pos.z;
                    float t = (rel_dx * seg_dx + rel_dz * seg_dz) / seg_len2;
                    if (t < 0.f) t = 0.f; else if (t > 1.f) t = 1.f;
                    const float cx = q_pos.x + seg_dx * t;
                    const float cz = q_pos.z + seg_dz * t;
                    const float ddx = mp.x - cx;
                    const float ddz = mp.z - cz;
                    if (std::sqrt(ddx * ddx + ddz * ddz) <= push_w + br) ++hits;
                }
                if (hits > best_hits) { best_hits = hits; best_pos = q_pos; }
            }
            if (best_hits >= 3) {
                sdk::common->cast_spell(SLOT_Q, best_pos);
                sdk::common->cast_spell(SLOT_E, best_pos);
                last_spell_cast_time = now;
                return;
            }
        }

        // Plain Q for push
        if (menu::fast_clear_use_q && !w_holding && is_spell_ready(SLOT_Q)
            && (!sdk::orbwalker->would_cancel_attack())) {
            math::vector3 pos{};
            if (find_best_q_farm(2, pos)) {
                if (cast_q_at(pos)) return;
            }
        }

        if (menu::fast_clear_use_w && !w_holding && is_spell_ready(SLOT_W)) {
            cast_w_grab();
        }
    }

    void lasthit()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!sdk::orbwalker->is_spell_farm_lasthit_enabled()) return;
        if (mana_pct() < (float)menu::lasthit_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (menu::lasthit_use_q) try_q_lasthit();
    }

    // SEMI-R (manual hold-to-cast)
    static void semi_r()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return;
        if (!sdk::target_selector) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::r_range + 200.f);
        if (!target) return;

        cast_r(target, /*force=*/true);
    }

    // AUTO Q — passive poke when not actively combo'ing / harassing / clearing.
    static void auto_q()
    {
        if (!menu::auto_q_active) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;
        if (!sdk::target_selector) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::q_effective_range);
        if (!target) return;

        cast_q_on(target, menu::auto_q_hitchance);
    }

    // SEMI-QE (manual hold-to-cast QE chain)
    static void semi_qe()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (w_holding) return;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;
        if (!sdk::target_selector) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::e_max_push_range);
        if (!target) return;

        cast_qe_combo(target);
    }

    // ============================================================================
    // FLASH + Q + E MACRO
    //   Rising-edge bind: Flash toward best in-range hero, then drop Q sphere
    //   on top of them and E it for an unflashable stun + push.
    // ============================================================================
    static constexpr float FLASH_RANGE = 425.f;

    static int      s_flash_slot_cache = -1;
    static int      s_fqe_state        = 0;  // 0=idle, 1=post-flash
    static float    s_fqe_time         = 0.f;
    static uint32_t s_fqe_target_id    = 0;

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

    static void flash_qe_start()
    {
        if (s_fqe_state != 0) return;
        if (!sdk::common || !sdk::target_selector) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting() || w_holding) return;

        const int flash = detect_flash_slot();
        if (flash < 0) return;
        auto fs = p->get_spell(flash);
        if (!fs || fs->get_cooldown() > 0.f) return;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;

        // Reach: flash + QE max push range. Beyond this we can't even chain.
        const float reach_max = FLASH_RANGE + spell_data::e_max_push_range;
        auto* target = pick_hero_target(reach_max);
        if (!target) return;

        const auto pp    = p->get_position();
        const auto delta = target->get_position() - pp;
        const float d    = delta.magnitude();
        if (d <= 0.f) return;

        const auto  dir   = delta.normalized();
        const float fdist = d < FLASH_RANGE ? d : FLASH_RANGE;
        math::vector3 flash_pos;
        flash_pos.x = pp.x + dir.x * fdist;
        flash_pos.y = pp.y + dir.y * fdist;
        flash_pos.z = pp.z + dir.z * fdist;

        if (!sdk::common->cast_spell(flash, flash_pos)) return;

        s_fqe_state     = 1;
        s_fqe_time      = g_sdk->clock_facade->get_game_time();
        s_fqe_target_id = target->get_network_id();
        last_spell_cast_time = s_fqe_time;
    }

    static void flash_qe_tick()
    {
        if (s_fqe_state == 0) return;

        const float now = g_sdk->clock_facade->get_game_time();
        auto* target = s_fqe_target_id
                       ? g_sdk->object_manager->get_object_by_network_id(s_fqe_target_id)
                       : nullptr;

        // Abort on target loss / timeout
        if (!target || !target->is_valid() || target->is_dead() || (now - s_fqe_time > 1.5f)) {
            s_fqe_state = 0;
            s_fqe_target_id = 0;
            return;
        }

        // Wait for Flash teleport to commit before reading our new position.
        if (now - s_fqe_time < 0.1f) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) { s_fqe_state = 0; s_fqe_target_id = 0; return; }

        // Drop sphere directly on target (within Q range), then E it.
        const auto player_pos = p->get_position();
        const auto target_pos = target->get_server_position();
        const float dist = player_pos.distance(target_pos);

        const float cast_dist = std::min(dist, spell_data::q_range);
        const auto sphere_pos = player_pos.extended(target_pos, cast_dist);

        if (sdk::common->cast_spell(SLOT_Q, sphere_pos))
            sdk::common->cast_spell(SLOT_E, sphere_pos);

        last_spell_cast_time = now;
        s_fqe_state     = 0;
        s_fqe_target_id = 0;
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        cleanup_spheres();

        // Drive Flash+Q+E macro state machine each tick (no-op when idle).
        flash_qe_tick();

        // Velocity-based anti-gap scanner — runs each tick when the toggle is
        // on. Internal cooldown prevents multi-fire.
        scan_antigap_qe();


        // Refresh passive stack count + R-execute buff (some buff systems don't push every change)
        {
            std::string name{ spell_data::buff_name_passive_stacks };
            auto* b = p->get_buff_by_hash(spell_data::buff_hash_passive_stacks);
            if (!b) b = p->get_buff_by_name(name);
            if (b && b->is_active()) {
                const int c = b->get_count();
                passive_stacks = c > 0 ? c : (int)b->get_stacks();
            }

            std::string r_up{ spell_data::buff_name_r_upgrade };
            auto* b2 = p->get_buff_by_hash(spell_data::buff_hash_r_upgrade);
            if (!b2) b2 = p->get_buff_by_name(r_up);
            r_execute_buff_active = b2 && b2->is_active();
        }

        // Force-throw a stuck W after timeout
        if (w_holding) {
            const float now = g_sdk->clock_facade->get_game_time();
            if ((now - w_grab_time) > spell_data::w_force_throw_time + 0.5f) {
                on_w_throw_observed();
            }
        }

        if (menu::semi_r_active) {
            semi_r();
            return;
        }

        if (menu::semi_qe_active) {
            semi_qe();
            return;
        }

        if (!sdk::orbwalker) return;

        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->clear())      clear();
        else if (sdk::orbwalker->lasthit())    lasthit();
        else                                   auto_q();
    }

    void __fastcall on_draw()
    {
        if (!g_sdk || !g_sdk->object_manager) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        const auto player_pos = p->get_position();

        auto draw_circle = [&](math::vector3 const& c, float r, uint32_t clr) {
            if (sdk::shaders) sdk::shaders->draw_world_circle(c, r, 2.f, clr);
            else if (g_sdk->renderer) g_sdk->renderer->add_circle_3d(c, r, 2.f, clr);
        };

        auto get_draw_color = [&](int slot, uint32_t clr) -> uint32_t {
            auto sp = p->get_spell(slot);
            if (!sp || sp->get_level() == 0) return 0u;
            if (menu::draw_only_ready && sp->get_cooldown() > 0.f) return 0u;
            return sp->get_cooldown() <= 0.f ? clr : ((clr & 0x00FFFFFFu) | 0x40000000u);
        };

        if (menu::draw_q_range) {
            const uint32_t c = get_draw_color(SLOT_Q, menu::color_q);
            if (c) draw_circle(player_pos, spell_data::q_range, c);
        }
        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) {
                const float r = w_holding ? spell_data::w_throw_range : spell_data::w_grab_range;
                draw_circle(player_pos, r, c);
            }
        }
        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) {
                draw_circle(player_pos, spell_data::e_cone_range, c);
                const uint32_t dim = (c & 0x00FFFFFFu) | 0x40000000u;
                draw_circle(player_pos, spell_data::e_sphere_range, dim);
            }
        }
        // QE chain: dim while either spell is unavailable.
        if (menu::draw_qe_range) {
            auto q_sp = p->get_spell(SLOT_Q);
            auto e_sp = p->get_spell(SLOT_E);
            const bool learned = q_sp && q_sp->get_level() > 0
                              && e_sp && e_sp->get_level() > 0;
            const bool ready   = is_spell_ready(SLOT_Q) && is_spell_ready(SLOT_E);
            if (learned && (ready || !menu::draw_only_ready)) {
                const uint32_t c = ready ? menu::color_qe
                                         : ((menu::color_qe & 0x00FFFFFFu) | 0x40000000u);
                if (menu::draw_qe_rgb && sdk::shaders) {
                    sdk::shaders->draw_world_circle(player_pos,
                        spell_data::e_max_push_range, 2.f, c,
                        shaders_sdk::world_circle_style::rgb);
                } else {
                    draw_circle(player_pos, spell_data::e_max_push_range, c);
                }
            }
        }
        if (menu::draw_r_range) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(player_pos, spell_data::r_range, c);
        }

        // "SAVE R" status indicator above player — only when R is learned
        {
            auto r_sp = p->get_spell(SLOT_R);
            if (g_sdk->renderer && r_sp && r_sp->get_level() > 0) {
                const auto screen = g_sdk->renderer->world_to_screen(player_pos);
                if (menu::save_r_active)
                    g_sdk->renderer->add_text("SAVE R: ON",  16.f,
                        { screen.x - 38.f, screen.y - 85.f }, 0, 0xFF00FF00u);
                else
                    g_sdk->renderer->add_text("SAVE R: OFF", 16.f,
                        { screen.x - 40.f, screen.y - 85.f }, 0, 0xFFFF0000u);
            }
        }

        // "AUTO Q" status indicator below player — only while Auto Q is on
        if (menu::auto_q_active && g_sdk->renderer) {
            const auto screen = g_sdk->renderer->world_to_screen(player_pos);
            g_sdk->renderer->add_text("AUTO Q", 16.f,
                { screen.x - 30.f, screen.y + 20.f }, 0, 0xFF00FF00u);
        }

        // Sphere markers + remaining time
        if (menu::draw_spheres && g_sdk->renderer) {
            const float now = g_sdk->clock_facade->get_game_time();
            for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
                const auto& s = spheres[i];
                if (!s.is_alive()) continue;
                const auto sp = s.position();
                const float remaining = s.remaining(now);
                const uint32_t color = s.is_w_held ? 0xFF00FFFFu : 0xFFB066FFu;
                draw_circle(sp, 50.f, color);

                const auto screen = g_sdk->renderer->world_to_screen(sp);
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%.1fs", remaining);
                g_sdk->renderer->add_text(buf, 14.f, screen, 1, color);
            }
        }

        // R killable text — Viktor-style (world_to_screen, centered above hero)
        if (menu::draw_killable_txt && g_sdk->renderer) {
            auto r_sp = p->get_spell(SLOT_R);
            if (r_sp && r_sp->get_level() > 0 && r_sp->get_cooldown() <= 0.f) {
                const int cur_total = count_r_spheres();
                for (auto* h : g_sdk->object_manager->get_heroes()) {
                    if (!is_valid_target(h)) continue;
                    const float dmg = get_r_damage(h, cur_total);
                    if (h->get_hp() > 0.f && h->get_hp() <= dmg) {
                        auto screen = g_sdk->renderer->world_to_screen(h->get_position());
                        g_sdk->renderer->add_text("R KILLABLE", 18.f,
                            { screen.x - 35.f, screen.y - 60.f }, 0, 0xFFFF4500u);
                    }
                }
            }
        }

    }

    // HP-bar overlays must run on event::present, not draw_world,
    // or get_health_bar_position-anchored shapes won't render.
    void __fastcall on_present()
    {
        if (!g_sdk || !g_sdk->object_manager || !g_sdk->renderer) return;
        if (!sdk::damage) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        if (!menu::draw_r_dmg_bar) return;

        // Hide overlays while R is unavailable — drains/text are misleading otherwise.
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return;

        const int  cur_total  = count_r_spheres();

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;

            // get_health_bar_position returns (0,0) for off-screen heroes,
            // so it's the only range gate we need — show on every visible enemy.
            const auto hbar = h->get_health_bar_position();
            if (hbar.x < 1.f && hbar.y < 1.f) continue;

            // R-only HP-bar overlay — mirrors can_r_kill: predicted HP at impact
            // (incl. shields), and shows full bar drain when execute applies.
            if (menu::draw_r_dmg_bar) {
                const float dmg = get_r_damage(h, cur_total);
                const float mhp = h->get_max_hp();
                if (dmg > 0.f && mhp > 0.f) {
                    const float now = g_sdk->clock_facade->get_game_time();
                    float hp = h->get_hp() + h->get_all_shield();
                    if (sdk::health_prediction) {
                        hp = sdk::health_prediction->get_predicted_health(h, now + spell_data::r_delay)
                           + h->get_all_shield();
                    }
                    if (hp > 0.f) {
                        // Execute: bar drains fully past the threshold.
                        float effective_dmg = dmg;
                        if (has_r_execute()) {
                            const float threshold = mhp * spell_data::r_execute_pct;
                            if (hp <= threshold || dmg >= (hp - threshold))
                                effective_dmg = hp;
                        }

                        const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
                        const float ox = -45.6f * scale, oy = 15.5f * scale;
                        const float bw = 106.4f * scale, bh = 11.5f * scale;
                        const float r0 = std::min(1.f, hp / mhp);
                        const float r1 = std::max(0.f, hp - effective_dmg) / mhp;
                        if (r0 > r1) {
                            math::rect rc;
                            rc.left   = hbar.x + ox + r1 * bw;
                            rc.top    = hbar.y + oy;
                            rc.right  = hbar.x + ox + r0 * bw;
                            rc.bottom = hbar.y + oy + bh;
                            g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
                        }
                    }
                }
            }

        }
    }

    // R-tier dash / leap / charge spell names (case-insensitive). Used by
    // anti-gapcloser QE to recognize uncancellable engages as they start.
    static const char* const R_DASH_SPELL_NAMES[] = {
        "UFSlash",                  // Malphite R
        "HecarimUlt",               // Hecarim R
        "JarvanIVCataclysm",        // J4 R
        "AkaliR",                   // Akali R
        "ZedR",                     // Zed R
        "DianaR",                   // Diana R
        "VolibearR",                // Volibear R
        "InfiniteDuress",           // Warwick R
        "RekSaiR",                  // Rek'Sai R
        "ViR",                      // Vi R
        "GalioR",                   // Galio R
        "PantheonRJump",            // Pantheon R landing
        "PantheonR",                // Pantheon R cast
        "NocturneParanoia",         // Nocturne R
        "NocturneParanoia2",        // Nocturne R dash phase
        "ShyvanaTransformLeap",     // Shyvana R cast leap
        "OlafRagnarok",             // Olaf R unstoppable charge
        "MalphiteR",                // Malphite R (alt name)
        "SejuaniGlacialPrison",     // Sejuani R
        "KhazixRLong",              // Kha'Zix evolved R chain
    };

    static bool is_r_dash_spell(const char* name)
    {
        if (!name) return false;
        for (const char* known : R_DASH_SPELL_NAMES) {
            if (_stricmp(name, known) == 0) return true;
        }
        return false;
    }

    // Approach-speed threshold (units/sec) to flag an enemy as gap-closing even
    // if they're not in a hard dash — covers Ghost, sprints, MS buffs while
    // running at us. A walking enemy approaches at ~340; a dasher at 800+.
    static constexpr float ANTIGAP_MIN_APPROACH = 700.f;
    static constexpr float ANTIGAP_COOLDOWN     = 0.8f;
    static float           last_antigap_time    = 0.f;

    static bool try_antigap_qe(game_object* enemy)
    {
        if (!menu::auto_qe_antigap) return false;
        if (!enemy || !sdk::common) return false;
        if (w_holding) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return false;
        if (is_casting()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_antigap_time < ANTIGAP_COOLDOWN) return false;
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        const auto player_pos = p->get_position();
        const auto enemy_pos  = enemy->get_server_position();
        const float dist = player_pos.distance(enemy_pos);
        if (dist > spell_data::e_max_push_range) return false;

        // Drop sphere on the line between us and the dasher (clamped to Q range)
        // and immediately E it. The cone catches them mid-leap for stun + push.
        const float cast_dist = std::min(dist, spell_data::q_range);
        const auto sphere_pos = player_pos.extended(enemy_pos, cast_dist);

        if (!sdk::common->cast_spell(SLOT_Q, sphere_pos)) return false;
        sdk::common->cast_spell(SLOT_E, sphere_pos);
        last_spell_cast_time = now;
        last_antigap_time    = now;
        return true;
    }

    // Per-tick scanner — catches any enemy hero dashing / sprinting toward us
    // even when no recognized R-name fires (Flash, Ghost, Yasuo E, Riven Q,
    // Akali E, etc.). on_process_cast still handles named R-dashes as a fast
    // first-frame path; this is the velocity-based safety net.
    static void scan_antigap_qe()
    {
        if (!menu::auto_qe_antigap) return;
        if (w_holding) return;
        if (!is_spell_ready(SLOT_Q) || !is_spell_ready(SLOT_E)) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        const auto player_pos = p->get_position();
        const float inv_eps   = 0.001f;

        game_object* best       = nullptr;
        float        best_score = 0.f;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead()) continue;
            if (!h->is_visible() || !h->is_targetable()) continue;
            if (h->get_team_id() == p->get_team_id()) continue;

            const auto enemy_pos = h->get_position();
            const float dist = player_pos.distance(enemy_pos);
            if (dist > spell_data::e_max_push_range) continue;
            if (dist < 50.f) continue; // already on top of us — Q sphere can't form between

            // Approach speed: velocity projected onto enemy→player direction.
            const auto  vel = h->get_velocity();
            const float dx  = player_pos.x - enemy_pos.x;
            const float dz  = player_pos.z - enemy_pos.z;
            const float inv_dist = 1.f / (dist + inv_eps);
            const float approach = (vel.x * dx + vel.z * dz) * inv_dist;

            const bool dashing = h->is_dashing();
            if (!dashing && approach < ANTIGAP_MIN_APPROACH) continue;

            // Rank by threat: hard dash beats anything else; otherwise faster
            // approach + closer = higher priority.
            const float score = (dashing ? 5000.f : approach)
                              + (spell_data::e_max_push_range - dist) * 0.5f;
            if (score > best_score) {
                best_score = score;
                best       = h;
            }
        }

        if (best) try_antigap_qe(best);
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        const char* spell_name = nullptr;
        if (auto* sd = cast->get_spell_data()) {
            if (auto* st = sd->get_static_data()) spell_name = st->get_name();
        }

        // 1) Our own W: track grab/throw state.
        if (obj == p) {
            if (cast->get_spell_slot() != SLOT_W) return;

            const float now = g_sdk->clock_facade->get_game_time();
            last_w_cast_time = now;
            if (!spell_name) return;

            if (_stricmp(spell_name, spell_data::spell_name_w_grab) == 0) {
                w_holding        = true;
                w_grab_time      = now;
                w_grab_pos       = cast->get_end_pos();
                w_sphere_pending = true;

                if (auto* s = find_nearest_ground_sphere(w_grab_pos, spell_data::sphere_match_radius)) {
                    s->is_w_held  = true;
                    s->hold_start = now;
                    w_sphere_pending = false;
                }
            }
            else if (_stricmp(spell_name, spell_data::spell_name_w_cast) == 0) {
                on_w_throw_observed();
            }
            return;
        }

        // 2) Enemy hero R-tier dash: anti-gapcloser QE.
        if (!menu::auto_qe_antigap) return;
        if (!obj->is_hero() || obj->get_team_id() == p->get_team_id()) return;
        if (!is_r_dash_spell(spell_name)) return;
        try_antigap_qe(obj);
    }

    void __fastcall on_create_object(game_object* obj)
    {
        if (!obj || !obj->is_valid()) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        // Name check first (matches venp pattern) — avoids calling get_owner
        // on every object that spawns in the game.
        if (obj->get_name() != spell_data::sphere_object_name) return;
        if (obj->get_owner() != p) return;

        add_sphere(obj);

        // If a W grab is pending and this sphere matches the grab pos, attach it
        if (w_sphere_pending) {
            const float d = obj->get_position().distance(w_grab_pos);
            if (d <= spell_data::sphere_match_radius) {
                for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) {
                    if (spheres[i].active && spheres[i].obj == obj) {
                        spheres[i].is_w_held  = true;
                        spheres[i].hold_start = g_sdk->clock_facade->get_game_time();
                        w_sphere_pending = false;
                        break;
                    }
                }
            }
        }
    }

    void __fastcall on_delete_object(game_object* obj)
    {
        if (!obj) return;
        remove_sphere(obj);
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        const uint32_t hash = buff->get_hash();
        if (hash == spell_data::buff_hash_passive_stacks) {
            const int c = buff->get_count();
            passive_stacks = c > 0 ? c : (int)buff->get_stacks();
        } else if (hash == spell_data::buff_hash_r_upgrade) {
            r_execute_buff_active = true;
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        const uint32_t hash = buff->get_hash();
        if (hash == spell_data::buff_hash_passive_stacks) {
            passive_stacks = 0;
        } else if (hash == spell_data::buff_hash_r_upgrade) {
            r_execute_buff_active = false;
        }
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        // AA-weave Q on hero AAs — respects the per-mode Q toggles, mana floor,
        // and hitchance so it never overrides the user's menu settings.
        if (!data || !data->target || !sdk::orbwalker) return true;
        if (!data->target->is_hero()) return true;
        if (w_holding) return true;
        if (!sdk::common) return true;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return true;

        int hitchance;
        if (sdk::orbwalker->combo()) {
            if (!menu::combo_use_q) return true;
            hitchance = menu::combo_hitchance;
        }
        else if (sdk::orbwalker->harass()) {
            if (!menu::harass_use_q) return true;
            if (mana_pct() < (float)menu::harass_mana_pct) return true;
            hitchance = menu::harass_hitchance;
        }
        else {
            return true;
        }

        cast_q_on(data->target, hitchance);
        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("syndra", "MeowAIO - Syndra");
        if (!s_menu) {
            g_sdk->log_console("[!] Syndra: failed to create menu category");
            return;
        }

        // Q — Dark Sphere
        auto q_menu = s_menu->add_sub_category("q_dark_sphere", "Q - Dark Sphere");
        q_menu->add_checkbox("combo",   "Use in Combo",  menu::combo_use_q,    [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass",  "Use in Harass", menu::harass_use_q,   [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("lasthit", "Lasthit",       menu::lasthit_use_q,  [](bool v) { menu::lasthit_use_q = v; });
        q_menu->add_checkbox("clear",   "Use in Clear",  menu::clear_use_q,    [](bool v) { menu::clear_use_q = v; });
        q_menu->add_slider_int("clear_min", "  Clear Min Hits", 1, 5, 1, menu::clear_q_min_hits,
            [](int v) { menu::clear_q_min_hits = v; });
        q_menu->add_checkbox("fast_clear", "Use in Fast Clear", menu::fast_clear_use_q,
            [](bool v) { menu::fast_clear_use_q = v; });
        q_menu->add_separator();
        q_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::combo_hitchance,
            [](int v) { menu::combo_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("auto_q_enabled", "Auto Q", menu::auto_q_active,
            [](bool v) { menu::auto_q_active = v; });
        q_menu->add_slider_int("auto_q_hc", "  Auto Q Hitchance", 0, 100, 1, menu::auto_q_hitchance,
            [](int v) { menu::auto_q_hitchance = v; });
        q_menu->add_hotkey("auto_q_key", "  Auto Q Toggle", 'J', false, false,
            [](std::string* hk, bool pressed) {
                static bool was = false;
                if (pressed && !was) menu::auto_q_active = !menu::auto_q_active;
                was = pressed;
                if (hk) menu::auto_q_key_str = *hk;
            });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Force of Will
        auto w_menu = s_menu->add_sub_category("w_force_of_will", "W - Force of Will");
        w_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_w,  [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w, [](bool v) { menu::harass_use_w = v; });
        w_menu->add_checkbox("clear",  "Use in Clear", menu::clear_use_w,
            [](bool v) { menu::clear_use_w = v; });
        w_menu->add_checkbox("fast_clear", "Use in Fast Clear", menu::fast_clear_use_w,
            [](bool v) { menu::fast_clear_use_w = v; });
        w_menu->add_separator();
        w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::harass_hitchance,
            [](int v) { menu::harass_hitchance = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });

        // E — Scatter the Weak
        auto e_menu = s_menu->add_sub_category("e_scatter_the_weak", "E - Scatter the Weak");
        e_menu->add_checkbox("combo",  "Use in Combo Q+E", menu::combo_use_e,
            [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("harass", "Use in Harass ", menu::harass_use_e,
            [](bool v) { menu::harass_use_e = v; });
        e_menu->add_checkbox("fast_clear", "Use in Fast Clear Q+E", menu::fast_clear_use_e,
            [](bool v) { menu::fast_clear_use_e = v; });
        e_menu->add_checkbox("fast_qe", "Fast QE Push", menu::combo_fast_qe,
            [](bool v) { menu::combo_fast_qe = v; });
        e_menu->add_checkbox("antigap_qe", "Auto QE Anti-Gap", menu::auto_qe_antigap,
            [](bool v) { menu::auto_qe_antigap = v; });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw E Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });
        e_menu->add_checkbox("draw_qe_range", "Draw QE Range", menu::draw_qe_range,
            [](bool v) { menu::draw_qe_range = v; });
        e_menu->add_checkbox("draw_qe_rgb", "Draw QE Range RGB", menu::draw_qe_rgb,
            [](bool v) { menu::draw_qe_rgb = v; });
        e_menu->add_hotkey("semi_qe", "Semi-Manual QE", 'G', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_qe_active = pressed;
                if (hk) menu::semi_qe_key_str = *hk;
            });
        e_menu->add_hotkey("flash_qe", "Flash + Q + E", 'V', false, false,
            [](std::string* hk, bool pressed) {
                static bool was = false;
                if (pressed && !was) flash_qe_start();
                was = pressed;
                if (hk) menu::flash_qe_key_str = *hk;
            });

        // R — Unleashed Power
        auto r_menu = s_menu->add_sub_category("r_unleashed_power", "R - Unleashed Power");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_checkbox("only_kill", "Only if Killable", menu::combo_r_only_kill,
            [](bool v) { menu::combo_r_only_kill = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });
        r_menu->add_hotkey("semi_r", "Semi-Manual R", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_r_active = pressed;
                if (hk) menu::semi_r_key_str = *hk;
            });
        r_menu->add_hotkey("save_r", "Save R", 'N', false, false,
            [](std::string* hk, bool pressed) {
                static bool was = false;
                if (pressed && !was) menu::save_r_active = !menu::save_r_active;
                was = pressed;
                if (hk) menu::save_r_key_str = *hk;
            });

        // Mana
        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass_mana", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });
        mana_menu->add_slider_int("clear_mana", "Clear Mana %", 0, 100, 5, menu::clear_mana_pct,
            [](int v) { menu::clear_mana_pct = v; });
        mana_menu->add_slider_int("lasthit_mana", "Lasthit Mana %", 0, 100, 5, menu::lasthit_mana_pct,
            [](int v) { menu::lasthit_mana_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("spheres", "Sphere Timer", menu::draw_spheres,
            [](bool v) { menu::draw_spheres = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_checkbox("r_killable", "R Killable Text", menu::draw_killable_txt,
            [](bool v) { menu::draw_killable_txt = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("color_w", "W Color", menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("color_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("color_qe", "QE Chain Color", menu::color_qe,
            [](uint32_t c) { menu::color_qe = c; });
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
        g_sdk->event_manager->register_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        if (sdk::infotab) {
            infotab_sdk::text_entry title{ "Semi R", 0xFFFFFFFFu };
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key_str, title,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_r_active ? "ON" : "OFF", clr };
                });

            infotab_sdk::text_entry qe_title{ "Semi QE", 0xFFFFFFFFu };
            menu::infotab_semi_qe = sdk::infotab->add_hotkey_text(
                &menu::semi_qe_key_str, qe_title,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_qe_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_qe_active ? "ON" : "OFF", clr };
                });

            infotab_sdk::text_entry fqe_title{ "Flash QE", 0xFFFFFFFFu };
            menu::infotab_flash_qe = sdk::infotab->add_hotkey_text(
                &menu::flash_qe_key_str, fqe_title,
                []() -> infotab_sdk::text_entry {
                    const bool ready = s_fqe_state == 0;
                    return { ready ? "MEOW" : "MEOW",
                             ready ? 0xFF00FF00u : 0xFFFFA040u };
                });

            infotab_sdk::text_entry sr_title{ "Save R", 0xFFFFFFFFu };
            menu::infotab_save_r = sdk::infotab->add_hotkey_text(
                &menu::save_r_key_str, sr_title,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::save_r_active ? 0xFFFF4040u : 0xFF00FF00u;
                    return { menu::save_r_active ? "OFF" : "ON", clr };
                });

            infotab_sdk::text_entry aq_title{ "Auto Q", 0xFFFFFFFFu };
            menu::infotab_auto_q = sdk::infotab->add_hotkey_text(
                &menu::auto_q_key_str, aq_title,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::auto_q_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::auto_q_active ? "ON" : "OFF", clr };
                });
        }

        g_sdk->log_console("[MeowAIO] Syndra module loaded");
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
            g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
                reinterpret_cast<void*>(on_create_object));
            g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
                reinterpret_cast<void*>(on_delete_object));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }

        if (sdk::infotab && menu::infotab_semi_r != 0) {
            sdk::infotab->remove_text(menu::infotab_semi_r);
            menu::infotab_semi_r = 0;
        }

        if (sdk::infotab && menu::infotab_semi_qe != 0) {
            sdk::infotab->remove_text(menu::infotab_semi_qe);
            menu::infotab_semi_qe = 0;
        }

        if (sdk::infotab && menu::infotab_flash_qe != 0) {
            sdk::infotab->remove_text(menu::infotab_flash_qe);
            menu::infotab_flash_qe = 0;
        }

        if (sdk::infotab && menu::infotab_save_r != 0) {
            sdk::infotab->remove_text(menu::infotab_save_r);
            menu::infotab_save_r = 0;
        }

        if (sdk::infotab && menu::infotab_auto_q != 0) {
            sdk::infotab->remove_text(menu::infotab_auto_q);
            menu::infotab_auto_q = 0;
        }

        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        passive_stacks = 0;
        r_execute_buff_active = false;
        ground_sphere_count = 0;
        r_total_spheres = spell_data::r_base_spheres;
        for (int i = 0; i < MAX_TRACKED_SPHERES; ++i) spheres[i] = {};
        w_holding = false;
        w_grab_time = 0.f;
        w_sphere_pending = false;
        w_grab_pos = {};
        last_w_cast_time = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_r_active = false;
        menu::semi_qe_active = false;
        menu::save_r_active = false;
        menu::auto_q_active = false;
        s_flash_slot_cache = -1;
        s_fqe_state = 0;
        s_fqe_target_id = 0;
        last_antigap_time = 0.f;

        if (g_sdk) g_sdk->log_console("[-] Syndra module unloaded");
    }
}
