#include "orianna.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace orianna
{
    static menu_category* s_menu = nullptr;

    // --- Ball state machine ---
    static ball_state    g_ball_state           = ball_state::on_self;
    static game_object*  g_ball_missile         = nullptr;
    static game_object*  g_ball_ally            = nullptr;
    static math::vector3 g_ball_ground_pos      = {};
    static bool          g_ball_ground_valid    = false;
    static math::vector3 g_ball_transit_start   = {};
    static math::vector3 g_ball_transit_dest    = {};
    static float         g_ball_transit_t_start = 0.f;
    static float         g_ball_transit_t_end   = 0.f;

    static constexpr float MIN_SPELL_DELAY = 0.05f;
    static float           last_spell_cast_time = 0.f;

    // E-specific cooldown — prevents respamming E while the previous shield
    // is still active (E shield duration is ~2.5s in the live db).
    static constexpr float E_RECAST_COOLDOWN = 2.5f;
    static float           last_e_cast_time = 0.f;

    static bool e_recently_cast()
    {
        return g_sdk->clock_facade->get_game_time() - last_e_cast_time
             < E_RECAST_COOLDOWN;
    }

    static void stamp_e_cast()
    {
        const float now = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = now;
        last_e_cast_time     = now;
    }

    // --- Hotkey display strings + infotab IDs ---
    static std::string g_semi_r_key;
    static std::string g_auto_q_key;
    static std::string g_auto_r_key;
    static std::string g_e_mode_key;
    static uint32_t    g_infotab_semi_r = 0;
    static uint32_t    g_infotab_auto_q = 0;
    static uint32_t    g_infotab_auto_r = 0;
    static uint32_t    g_infotab_e_mode = 0;

    // --- Per-ally shield enable map (true = shield this ally) ---
    static std::unordered_map<std::string, bool> g_ally_shield_enabled;
    // Names already added to the *current* menu instance. Kept separate from
    // g_ally_shield_enabled (which persists across re-inits) so that after a
    // reload — where the submenu is rebuilt empty — checkboxes are re-added
    // even though the value map still holds the names.
    static std::unordered_set<std::string>       g_ally_menu_added;
    static menu_category* g_ally_shield_menu = nullptr;
    static float          g_ally_menu_last_refresh = 0.f;

    static bool is_ally_shieldable(game_object* h)
    {
        if (!h) return false;
        const std::string name = h->get_char_name();
        auto it = g_ally_shield_enabled.find(name);
        return it != g_ally_shield_enabled.end() ? it->second : true;
    }

    // Append a checkbox for each ally we haven't seen yet. Cheap, throttled
    // to ~2s in on_update. Catches allies that hadn't loaded when init() ran.
    static void refresh_ally_shield_menu()
    {
        if (!g_ally_shield_menu) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h == p) continue;
            if (h->get_team_id() != p->get_team_id()) continue;

            const std::string name = h->get_char_name();
            if (name.empty()) continue;
            if (g_ally_menu_added.count(name)) continue;

            // Preserve any previously-configured value across reloads; default on.
            auto it = g_ally_shield_enabled.find(name);
            const bool initial = (it != g_ally_shield_enabled.end()) ? it->second : true;
            g_ally_shield_enabled[name] = initial;
            g_ally_menu_added.insert(name);
            g_ally_shield_menu->add_checkbox("ally_" + name, name, initial,
                [name](bool v) { g_ally_shield_enabled[name] = v; });
        }
    }

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
        if (!ac) return false;
        auto sc = ac->get_spell_cast();
        if (!sc) return false;
        // Ignore basic-attack windups — they register as active casts but don't
        // block spells. Treating them as "casting" would gate every Q/W/R until
        // the AA finishes, which is the bug we're fixing.
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

    bool is_valid_target(game_object* u, float range)
    {
        if (!u || !u->is_valid() || u->is_dead() || !u->is_targetable()) return false;
        if (!u->is_visible()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (u->get_team_id() == p->get_team_id()) return false;
        if (range != FLT_MAX) {
            const float d = p->get_position().distance(u->get_position());
            if (d > range) return false;
        }
        return true;
    }

    static bool is_valid_enemy_minion(game_object* m)
    {
        if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) return false;
        if (!m->is_visible()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (m->get_team_id() == p->get_team_id()) return false;
        return m->is_lane_minion();
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
    // BALL STATE
    // ============================================================================
    static bool is_ball_in_transit()
    {
        return g_ball_state == ball_state::in_transit_q
            || g_ball_state == ball_state::in_transit_e;
    }

    static bool has_ball_buff_self()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        if (spell_data::buff_hash_ball_on_self != 0u) {
            auto* b = p->get_buff_by_hash(spell_data::buff_hash_ball_on_self);
            return b && b->is_active();
        }
        std::string name{ spell_data::buff_name_ball_self };
        auto* b = p->get_buff_by_name(name);
        return b && b->is_active();
    }

    static bool is_ball_buff_self_match(buff_instance* b)
    {
        if (!b) return false;
        if (spell_data::buff_hash_ball_on_self != 0u)
            return b->get_hash() == spell_data::buff_hash_ball_on_self;
        return b->get_name() == spell_data::buff_name_ball_self;
    }

    static bool is_ball_buff_ally_match(buff_instance* b)
    {
        if (!b) return false;
        if (spell_data::buff_hash_ball_on_ally != 0u)
            return b->get_hash() == spell_data::buff_hash_ball_on_ally;
        return b->get_name() == spell_data::buff_name_ball_ally;
    }

    static math::vector3 get_ball_position()
    {
        auto p = g_sdk->object_manager->get_local_player();
        const auto fallback = p ? p->get_position() : math::vector3{};

        switch (g_ball_state) {
        case ball_state::in_transit_q:
        case ball_state::in_transit_e: {
            if (g_ball_missile && g_ball_missile->is_valid())
                return g_ball_missile->get_position();
            const float total = g_ball_transit_t_end - g_ball_transit_t_start;
            if (total <= 0.f) return g_ball_transit_dest;
            const float now = g_sdk->clock_facade->get_game_time();
            const float t = std::clamp((now - g_ball_transit_t_start) / total, 0.f, 1.f);
            const float full_d = g_ball_transit_start.distance(g_ball_transit_dest);
            return g_ball_transit_start.extended(g_ball_transit_dest, full_d * t, true);
        }
        case ball_state::on_self:   return fallback;
        case ball_state::on_ally:   return (g_ball_ally && g_ball_ally->is_valid())
                                       ? g_ball_ally->get_position() : fallback;
        case ball_state::on_ground: return g_ball_ground_pos;
        }
        return fallback;
    }

    static void complete_q_transit()
    {
        g_ball_ground_pos   = g_ball_transit_dest;
        g_ball_ground_valid = true;
        g_ball_state        = ball_state::on_ground;
        g_ball_missile      = nullptr;
    }

    static void complete_e_transit()
    {
        if (g_ball_ally && g_ball_ally->is_valid())
            g_ball_state = ball_state::on_ally;
        else {
            g_ball_state = ball_state::on_self;
            g_ball_ally  = nullptr;
        }
        g_ball_missile = nullptr;
    }

    static void update_ball_state()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) {
            g_ball_state = ball_state::on_self;
            g_ball_ally = nullptr;
            g_ball_missile = nullptr;
            g_ball_ground_valid = false;
            return;
        }

        const float now = g_sdk->clock_facade->get_game_time();

        // Missile dropped (became invalid) → consider transit complete
        if (g_ball_missile && (!g_ball_missile->is_valid() || g_ball_missile->is_dead())) {
            if (g_ball_state == ball_state::in_transit_q)      complete_q_transit();
            else if (g_ball_state == ball_state::in_transit_e) complete_e_transit();
            else                                                g_ball_missile = nullptr;
        }

        // Time-based transit completion fallback
        if (is_ball_in_transit() && !g_ball_missile && now >= g_ball_transit_t_end) {
            if (g_ball_state == ball_state::in_transit_q)      complete_q_transit();
            else if (g_ball_state == ball_state::in_transit_e) complete_e_transit();
        }

        if (!is_ball_in_transit()) {
            const bool buff_on = has_ball_buff_self();
            if (buff_on && g_ball_state != ball_state::on_self) {
                g_ball_state = ball_state::on_self;
                g_ball_ally  = nullptr;
                // Reaching on_self via the self-buff means the previous ground
                // position is no longer valid (ball returned to us, not dropped).
                g_ball_ground_valid = false;
            } else if (!buff_on && g_ball_state == ball_state::on_self && g_ball_ground_valid) {
                g_ball_state = ball_state::on_ground;
            }
        }

        // Leash check — if ball drifts too far, snap back to self
        const auto ppos = p->get_position();
        if (g_ball_state == ball_state::on_ground
            && ppos.distance(g_ball_ground_pos) > spell_data::ball_leash_range) {
            g_ball_state = ball_state::on_self;
            g_ball_ally = nullptr;
            g_ball_ground_valid = false;
        }
        if (g_ball_state == ball_state::on_ally && g_ball_ally) {
            if (!g_ball_ally->is_valid() || g_ball_ally->is_dead()
                || ppos.distance(g_ball_ally->get_position()) > spell_data::ball_leash_range_ally) {
                g_ball_state = ball_state::on_self;
                g_ball_ally = nullptr;
            }
        }
    }

    // ============================================================================
    // MOVEMENT PREDICTION (lightweight, path-based)
    // ============================================================================
    // Walks the target's path waypoints from its current path index, summing
    // segment distances until `dt * move_speed` units have been consumed, and
    // returns the interpolated world position. Used to estimate where an
    // enemy will be after R's wind-up so we don't burn ult on a stale snapshot.
    static math::vector3 predict_position(game_object* h, float dt)
    {
        if (!h) return {};
        const auto cur_pos = h->get_position();
        if (dt <= 0.f) return cur_pos;

        auto path = h->get_path();
        if (path.empty()) return cur_pos;

        const float ms = h->get_move_speed();
        if (ms <= 0.f) return cur_pos;

        float remaining = ms * dt;
        const uint8_t idx = h->get_current_path_index();
        const size_t start = (idx < path.size()) ? idx : 0;

        math::vector3 cur = cur_pos;
        for (size_t i = start; i < path.size(); ++i) {
            const auto& wp = path[i];
            const float seg = cur.distance(wp);
            if (seg >= remaining) return cur.extended(wp, remaining);
            remaining -= seg;
            cur = wp;
        }
        return cur;  // ran out of path — last waypoint
    }

    // ============================================================================
    // QUERIES
    // ============================================================================
    static int count_enemies_near(const math::vector3& pos, float radius)
    {
        const float r_sq = radius * radius;
        int n = 0;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const auto hp = h->get_position();
            const float dx = hp.x - pos.x;
            const float dz = hp.z - pos.z;
            if (dx * dx + dz * dz <= r_sq) ++n;
        }
        return n;
    }

    static int count_enemies_near_ball(float radius)
    {
        return count_enemies_near(get_ball_position(), radius);
    }

    static int count_minions_near(const math::vector3& pos, float radius)
    {
        const float r_sq = radius * radius;
        int n = 0;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            const float dx = mp.x - pos.x;
            const float dz = mp.z - pos.z;
            if (dx * dx + dz * dz <= r_sq) ++n;
        }
        return n;
    }

    static math::vector3 get_best_minion_cluster(float search_range,
                                                 float cluster_radius,
                                                 int* out_count = nullptr)
    {
        if (out_count) *out_count = 0;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return {};
        const auto pos = p->get_position();
        const float search_sq = search_range * search_range;
        const float cl_sq = cluster_radius * cluster_radius;

        math::vector3 best{};
        int best_n = 0;
        auto minions = g_sdk->object_manager->get_minions();
        for (auto* m : minions) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            const float dx = mp.x - pos.x;
            const float dz = mp.z - pos.z;
            if (dx * dx + dz * dz > search_sq) continue;

            int n = 0;
            for (auto* o : minions) {
                if (!is_valid_enemy_minion(o)) continue;
                const auto op = o->get_position();
                const float ox = op.x - mp.x;
                const float oz = op.z - mp.z;
                if (ox * ox + oz * oz <= cl_sq) ++n;
            }
            if (n > best_n) { best_n = n; best = mp; }
        }
        if (out_count) *out_count = best_n;
        return best;
    }

    static bool is_carry(game_object* h)
    {
        if (!h) return false;
        const char* tags = h->get_search_tags();
        if (!tags) return false;

        // Match comma/space-delimited tokens to avoid false positives.
        auto has_token = [tags](const char* tok) {
            const size_t n = std::strlen(tok);
            for (const char* p = tags; *p; ) {
                while (*p == ' ' || *p == ',') ++p;
                if (!*p) break;
                const char* end = p;
                while (*end && *end != ' ' && *end != ',') ++end;
                if ((size_t)(end - p) == n && std::strncmp(p, tok, n) == 0) return true;
                p = end;
            }
            return false;
        };
        return has_token("marksman") || has_token("mage") || has_token("assassin");
    }

    static bool is_r_killable(game_object* target)
    {
        if (!target || !sdk::damage) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !is_spell_learned(SLOT_R)) return false;
        const float dmg = sdk::damage->get_spell_damage(p, target, SLOT_R);
        return dmg >= target->get_hp() + target->get_all_shield();
    }

    static bool is_q_killable(game_object* target)
    {
        if (!target || !sdk::damage) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !is_spell_learned(SLOT_Q)) return false;
        const float dmg = sdk::damage->get_spell_damage(p, target, SLOT_Q);
        return dmg >= target->get_hp() + target->get_all_shield();
    }

    // R has a ~0.5s wind-up. Use predicted positions when checking who'll
    // actually be inside the AOE when it lands, otherwise we burn ult on
    // enemies who walked out during the cast.
    static constexpr float R_CAST_DELAY = 0.5f;

    static int score_r_cast()
    {
        const auto bp = get_ball_position();
        const float r_sq = spell_data::r_effect_radius * spell_data::r_effect_radius;
        int score = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const auto hp = predict_position(h, R_CAST_DELAY);
            const float dx = hp.x - bp.x;
            const float dz = hp.z - bp.z;
            if (dx * dx + dz * dz > r_sq) continue;

            if (is_r_killable(h)) { score += 4; continue; }
            const float pct = h->get_max_hp() > 0.f ? h->get_hp() / h->get_max_hp() : 1.f;
            if (pct < 0.3f)         { score += 3; continue; }
            if (is_carry(h))         { score += 2; continue; }
            score += 1;
        }
        return score;
    }

    // ============================================================================
    // PREDICTION (crash-safe)
    // ============================================================================
    static pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::circular;
        // center_to_edge — the AOE has a 175u effect radius, so targets near
        // the edge of Q range are still reachable without the predictor
        // pushing the cast point past 825u (which we'd then clamp and miss).
        spell.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        spell.spell_slot         = SLOT_Q;
        spell.range              = spell_data::q_range;
        spell.expected_hitchance = hitchance;

        // Effective hit radius = full detonation AOE + target's bounding radius.
        // Using the conservative q_radius (87.5) under-reported reachable edge
        // targets; q_effect_radius (175) is the actual hit circle radius.
        const float bounding = target ? target->get_bounding_radius() : 0.f;
        spell.radius = spell_data::q_effect_radius + bounding;

        // Total time before the AOE detonates:
        //   windup delay (0.25s) + ball travel time (ball_pos → cast point at 1400 u/s)
        // On-self: SDK derives travel time from player→cast_point / speed.
        // Off-self: ball launches from a non-player origin so we bake total
        //   flight time into delay (FLT_MAX speed = instantaneous from SDK's view).
        //   One refinement pass corrects for enemy movement during the windup +
        //   rough travel window — crude estimate → predict enemy pos → recalculate.
        constexpr float Q_CAST_TIME = 0.25f;
        if (g_ball_state == ball_state::on_self) {
            spell.delay            = Q_CAST_TIME;
            spell.projectile_speed = spell_data::ball_q_missile_speed;
        } else if (target) {
            const auto  bp           = get_ball_position();
            const float rough_travel = bp.distance(target->get_position())
                                     / spell_data::ball_q_missile_speed;
            const auto  refined_pos  = predict_position(target, Q_CAST_TIME + rough_travel);
            const float final_travel = bp.distance(refined_pos)
                                     / spell_data::ball_q_missile_speed;
            spell.delay            = Q_CAST_TIME + final_travel;
            spell.projectile_speed = FLT_MAX;
        } else {
            spell.delay            = Q_CAST_TIME;
            spell.projectile_speed = spell_data::ball_q_missile_speed;
        }

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool cast_q_at(const math::vector3& pos)
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (g_ball_state == ball_state::in_transit_e) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        math::vector3 cast_pos = pos;
        const auto ppos = p->get_position();
        if (ppos.distance(pos) > spell_data::q_range)
            cast_pos = ppos.extended(pos, spell_data::q_range);

        bool ok = sdk::common->cast_spell(SLOT_Q, cast_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_q(game_object* target, int hitchance)
    {
        if (!target || !sdk::prediction || !sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (g_ball_state == ball_state::in_transit_e) return false;
        // Only block if a real AA windup is in progress. The earlier can_spell
        // check was firing on any enemy in AA range, which gated Q to AA range
        // and prevented casts at full Q range (825).
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto pred = predict_q(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        return cast_q_at(pred.cast_position);
    }

    static bool try_w()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (is_ball_in_transit()) return false;
        if (count_enemies_near_ball(spell_data::w_effect_radius) <= 0) return false;

        bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_r()
    {
        constexpr int R_MIN_SCORE = 3;

        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (is_ball_in_transit()) return false;

        const int score = score_r_cast();
        if (score >= R_MIN_SCORE) {
            bool ok = sdk::common->cast_spell(SLOT_R);
            if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            return ok;
        }

        if (menu::combo_r_killable && score >= 1) {
            const auto bp = get_ball_position();
            const float r_sq = spell_data::r_effect_radius * spell_data::r_effect_radius;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                const auto hp = predict_position(h, R_CAST_DELAY);
                const float dx = hp.x - bp.x;
                const float dz = hp.z - bp.z;
                if (dx * dx + dz * dz > r_sq) continue;
                if (is_r_killable(h)) {
                    bool ok = sdk::common->cast_spell(SLOT_R);
                    if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
                    return ok;
                }
            }
        }
        return false;
    }

    static bool try_semi_r()
    {
        if (!menu::semi_r_pressed) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (is_ball_in_transit()) return false;
        if (is_casting()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        const int hits = count_enemies_near_ball(spell_data::r_effect_radius);
        if (hits < menu::semi_r_min_targets) return false;

        if (!sdk::common->cast_spell(SLOT_R)) return false;
        last_spell_cast_time = now;
        return true;
    }

    static bool try_auto_r()
    {
        if (!menu::auto_r_enabled) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (is_ball_in_transit()) return false;
        if (is_casting()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        if (count_enemies_near_ball(spell_data::r_effect_radius) < menu::auto_r_min_targets)
            return false;

        if (!sdk::common->cast_spell(SLOT_R)) return false;
        last_spell_cast_time = now;
        return true;
    }

    // Killsteal — runs in any orbwalker mode. R only checks enemies inside the
    // ball's R radius (no repositioning). Q runs full prediction at q_range.
    static bool try_killsteal_r()
    {
        if (!menu::ks_use_r) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (is_ball_in_transit() || is_casting()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        const auto bp = get_ball_position();
        const float r_sq = spell_data::r_effect_radius * spell_data::r_effect_radius;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const auto hp = predict_position(h, R_CAST_DELAY);
            const float dx = hp.x - bp.x;
            const float dz = hp.z - bp.z;
            if (dx * dx + dz * dz > r_sq) continue;
            if (!is_r_killable(h)) continue;

            if (!sdk::common->cast_spell(SLOT_R)) return false;
            last_spell_cast_time = now;
            return true;
        }
        return false;
    }

    static bool try_killsteal_q()
    {
        if (!menu::ks_use_q) return false;
        if (!sdk::prediction || !sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (g_ball_state == ball_state::in_transit_e) return false;
        if (is_casting()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h, spell_data::q_range)) continue;
            if (!is_q_killable(h)) continue;

            // KS targets are dying — use medium hitchance to avoid wasted casts
            // while still firing on retreating low-HP enemies.
            constexpr int ks_hc = pred_sdk::hitchance::medium;
            auto pred = predict_q(h, ks_hc);
            if (!pred.is_valid || pred.hitchance < ks_hc) continue;

            if (!cast_q_at(pred.cast_position)) continue;
            return true;
        }
        return false;
    }

    static bool try_auto_q()
    {
        if (!menu::auto_q_enabled) return false;
        if (!sdk::target_selector) return false;
        if (is_casting()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        // Don't auto Q while standing inside an enemy turret's aggro range — Q
        // would damage an enemy hero and pull tower aggro onto us. Manual scan
        // (the SDK is_in_turret_range was unreliable in testing).
        auto p = g_sdk->object_manager->get_local_player();
        if (p) {
            constexpr float TURRET_AGGRO_RANGE = 775.f;
            const float r_sq = TURRET_AGGRO_RANGE * TURRET_AGGRO_RANGE;
            const auto ppos = p->get_position();
            for (auto* t : g_sdk->object_manager->get_turrets()) {
                if (!t || !t->is_valid() || t->is_dead()) continue;
                if (t->get_team_id() == p->get_team_id()) continue;
                const auto tp = t->get_position();
                const float dx = tp.x - ppos.x;
                const float dz = tp.z - ppos.z;
                if (dx * dx + dz * dz <= r_sq) return false;
            }
        }

        auto* target = pick_hero_target(spell_data::q_range);
        if (!target) return false;

        return try_q(target, menu::auto_q_hitchance);
    }

    static bool try_auto_w()
    {
        if (!menu::auto_w_enabled) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (is_ball_in_transit() || is_casting()) return false;

        // Don't pre-empt flee — flee wants to E recall first and only W after
        // the ball is back on self. Letting auto-W fire here on a Q-grounded
        // ball would burn W on the enemy and skip the recall entirely.
        if (sdk::orbwalker && sdk::orbwalker->flee()) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return false;

        if (count_enemies_near_ball(spell_data::w_effect_radius) < 1)
            return false;

        if (!sdk::common->cast_spell(SLOT_W)) return false;
        last_spell_cast_time = now;
        return true;
    }

    static bool try_self_shield()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (is_ball_in_transit()) return false;
        if (e_recently_cast()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const bool ok = sdk::common->cast_spell(SLOT_E, p);
        if (ok) stamp_e_cast();
        return ok;
    }

    static bool try_shield_ally_cast(game_object* target)
    {
        if (!sdk::common || !target) return false;
        if (e_recently_cast()) return false;
        const bool ok = sdk::common->cast_spell(SLOT_E, target);
        if (ok) stamp_e_cast();
        return ok;
    }

    // 2D point-to-segment distance on the xz plane.
    static float dist_point_to_segment_xz(const math::vector3& p,
                                          const math::vector3& a,
                                          const math::vector3& b)
    {
        const float vx = b.x - a.x, vz = b.z - a.z;
        const float wx = p.x - a.x, wz = p.z - a.z;
        const float c1 = vx * wx + vz * wz;
        if (c1 <= 0.f) {
            const float dx = p.x - a.x, dz = p.z - a.z;
            return std::sqrt(dx * dx + dz * dz);
        }
        const float c2 = vx * vx + vz * vz;
        if (c2 <= c1) {
            const float dx = p.x - b.x, dz = p.z - b.z;
            return std::sqrt(dx * dx + dz * dz);
        }
        const float t = c1 / c2;
        const float px = a.x + t * vx, pz = a.z + t * vz;
        const float dx = p.x - px, dz = p.z - pz;
        return std::sqrt(dx * dx + dz * dz);
    }

    static bool e_path_clips(const math::vector3& dest, game_object* enemy)
    {
        if (!enemy) return false;
        const auto bp = get_ball_position();
        const float thresh = spell_data::e_width * 0.5f + enemy->get_bounding_radius();
        return dist_point_to_segment_xz(enemy->get_position(), bp, dest) <= thresh;
    }

    // Aggressive E in combo — fire E if its travel line will clip the combo
    // target. Self destination preferred (free shield), then any ally that
    // hasn't been opted out of the per-ally shield list.
    // Combo-only engage: send the ball to an ally where R will hit a cluster.
    // Caller responsible for ensuring orbwalker is in combo (we run from combo()).
    // True if any non-self, non-dead, visible teammate is within E range.
    static bool has_ally_in_e_range()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto ppos = p->get_position();
        const float r_sq = spell_data::e_range * spell_data::e_range;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead() || !h->is_visible()) continue;
            if (h == p || h->get_team_id() != p->get_team_id()) continue;
            const auto hp = h->get_position();
            const float dx = hp.x - ppos.x;
            const float dz = hp.z - ppos.z;
            if (dx * dx + dz * dz <= r_sq) return true;
        }
        return false;
    }

    static bool try_combo_engage_e()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (!is_spell_ready(SLOT_R)) return false;
        if (is_ball_in_transit() || is_casting()) return false;
        if (e_recently_cast()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        game_object* best = nullptr;
        int best_hits = 0;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead() || !h->is_visible()) continue;
            if (h == p || h->get_team_id() != p->get_team_id()) continue;
            if (p->get_position().distance(h->get_position()) > spell_data::e_range) continue;
            if (!is_ally_shieldable(h)) continue;

            // Count enemies likely to still be near this ally after E travels
            // and R winds up. We don't know E travel time precisely; use the
            // R wind-up alone as a conservative estimate.
            const auto ally_pos = h->get_position();
            const float r_sq = spell_data::r_effect_radius * spell_data::r_effect_radius;
            int hits = 0;
            for (auto* e : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(e)) continue;
                const auto ep = predict_position(e, R_CAST_DELAY);
                const float dx = ep.x - ally_pos.x;
                const float dz = ep.z - ally_pos.z;
                if (dx * dx + dz * dz <= r_sq) ++hits;
            }
            if (hits > best_hits) { best_hits = hits; best = h; }
        }

        if (!best || best_hits < menu::combo_engage_e_min_enemies) return false;
        return try_shield_ally_cast(best);
    }

    static bool try_combo_aggressive_e(game_object* combo_target)
    {
        if (!combo_target) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (is_ball_in_transit() || is_casting()) return false;
        if (e_recently_cast()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        // E from on_self moves nothing meaningful — line collapses to a point.
        if (g_ball_state == ball_state::on_self) return false;

        // Don't pre-empt Q. If Q is up and the combo target is in Q range, let
        // the Q→W flow play out first — E only earns its slot when Q can't fire
        // or won't connect.
        const bool q_available = is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q);
        if (q_available && sdk::prediction
            && p->get_position().distance(combo_target->get_position())
               <= spell_data::q_range) {
            auto pred = predict_q(combo_target, menu::combo_q_hitchance);
            if (pred.is_valid && pred.hitchance >= menu::combo_q_hitchance)
                return false;
        }

        // Score candidates (self + allies) by how much impact E will produce:
        //   +2 if the E ball line clips the combo target (or any enemy)
        //   +1 if the destination lands the ball within W radius of an enemy
        // Self is a valid destination — recalling the ball back through the
        // enemy is the classic Orianna pattern, and lets W fire on the player
        // position immediately after E lands.
        const float w_r = spell_data::w_effect_radius;
        const float w_r_sq = w_r * w_r;

        game_object* best = nullptr;
        int best_score = 0;
        float best_dist = FLT_MAX;

        auto score_dest = [&](game_object* dest, const math::vector3& dest_pos) {
            int score = 0;

            // Line clip — prefer the combo target, but any enemy hero counts.
            if (e_path_clips(dest_pos, combo_target)) {
                score += 2;
            } else {
                for (auto* e : g_sdk->object_manager->get_heroes()) {
                    if (!is_valid_target(e)) continue;
                    if (e_path_clips(dest_pos, e)) { score += 2; break; }
                }
            }

            // W follow-up: any enemy hero within W radius of the landing point?
            for (auto* e : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(e)) continue;
                const auto ep = e->get_position();
                const float dx = ep.x - dest_pos.x;
                const float dz = ep.z - dest_pos.z;
                if (dx * dx + dz * dz <= w_r_sq) { score += 1; break; }
            }
            return score;
        };

        auto consider = [&](game_object* dest, const math::vector3& dest_pos) {
            const int score = score_dest(dest, dest_pos);
            if (score <= 0) return;
            const float d = dest_pos.distance(combo_target->get_position());
            if (score > best_score || (score == best_score && d < best_dist)) {
                best_score = score;
                best_dist  = d;
                best       = dest;
            }
        };

        // Self as a candidate (recall) — only meaningful when ball is elsewhere,
        // which we already enforced above.
        consider(p, p->get_position());

        // Allies as candidates.
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead() || !h->is_visible()) continue;
            if (h == p || h->get_team_id() != p->get_team_id()) continue;
            if (p->get_position().distance(h->get_position()) > spell_data::e_range) continue;
            if (!is_ally_shieldable(h)) continue;
            consider(h, h->get_position());
        }

        if (!best) return false;
        return try_shield_ally_cast(best);
    }

    static bool try_shield_ally()
    {
        if (!menu::shield_allies) return false;
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (is_ball_in_transit()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const float threshold = (float)menu::shield_hp_pct / 100.f;

        game_object* best = nullptr;
        float best_pct = 1.f;

        // Only shield allies that actually have an enemy hero threatening
        // them — otherwise E gets burned on chip-damage allies (DoTs, jungle
        // camps) who aren't in any real danger.
        constexpr float ALLY_THREAT_RANGE = 900.f;
        const float thr_sq = ALLY_THREAT_RANGE * ALLY_THREAT_RANGE;

        auto has_enemy_near = [&](const math::vector3& pos) {
            for (auto* e : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(e)) continue;
                const auto ep = e->get_position();
                const float dx = ep.x - pos.x;
                const float dz = ep.z - pos.z;
                if (dx * dx + dz * dz <= thr_sq) return true;
            }
            return false;
        };

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead() || !h->is_visible()) continue;
            if (h == p || h->get_team_id() != p->get_team_id()) continue;
            if (p->get_position().distance(h->get_position()) > spell_data::e_range) continue;
            if (!is_ally_shieldable(h)) continue;

            const float pct = h->get_max_hp() > 0.f ? h->get_hp() / h->get_max_hp() : 1.f;
            if (pct >= threshold) continue;
            if (!has_enemy_near(h->get_position())) continue;
            if (pct < best_pct) { best_pct = pct; best = h; }
        }

        if (!best) return false;
        return try_shield_ally_cast(best);
    }

    // ============================================================================
    // LASTHIT
    // ============================================================================
    static bool try_lasthit_q()
    {
        if (!menu::lasthit_use_q) return false;
        if (!sdk::common || !sdk::damage) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (g_ball_state == ball_state::in_transit_e) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto ppos = p->get_position();
        const float r_sq = spell_data::q_range * spell_data::q_range;

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            const float dx = mp.x - ppos.x;
            const float dz = mp.z - ppos.z;
            if (dx * dx + dz * dz > r_sq) continue;

            const float dmg = sdk::damage->get_spell_damage(p, m, SLOT_Q);
            if (dmg < m->get_hp()) continue;

            // Skip minions an auto-attack will already kill — saves mana.
            const float aa = sdk::damage->get_aa_damage(p, m);
            if (aa >= m->get_hp() && sdk::orbwalker
                && sdk::orbwalker->is_in_auto_attack_range(p, m)) continue;

            return cast_q_at(mp);
        }
        return false;
    }

    static bool try_lasthit_w()
    {
        if (!menu::lasthit_use_w) return false;
        if (!sdk::common || !sdk::damage) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (is_ball_in_transit()) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        const auto bp = get_ball_position();
        const float r_sq = spell_data::w_effect_radius * spell_data::w_effect_radius;

        bool any_killable = false;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            const float dx = mp.x - bp.x;
            const float dz = mp.z - bp.z;
            if (dx * dx + dz * dz > r_sq) continue;

            const float dmg = sdk::damage->get_spell_damage(p, m, SLOT_W);
            if (dmg >= m->get_hp()) { any_killable = true; break; }
        }
        if (!any_killable) return false;

        if (!sdk::common->cast_spell(SLOT_W)) return false;
        last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return true;
    }

    static void lasthit_mode()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        // W first (AOE) so a single cast can secure multiple minions.
        if (try_lasthit_w()) return;
        if (try_lasthit_q()) return;
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

        if (menu::combo_use_r && !is_ball_in_transit() && try_r())
            return;

        auto* target = pick_hero_target(spell_data::q_range);

        // Q first — places the ball on the enemy and does poke damage.
        if (menu::combo_use_q && target) {
            if (try_q(target, menu::combo_q_hitchance)) return;
        }

        // W second — fires at the ball position (after Q, ball is on enemy).
        if (menu::combo_use_w && try_w()) return;

        // E last (Q → W → E). The "Use in Combo" master toggle gates the
        // entire E-in-combo block — E Mode then decides the flavour:
        //   0 Off        → threat-gated self-shield
        //   1 Aggressive → recall ball through enemies / E-to-ally for impact
        //   2 Engage     → send ball to ally with most enemies near them
        //   3 Smart      → engage if ally in range, else aggressive
        if (menu::shield_combo_self) {
            const int em = menu::combo_e_mode;
            if (em == 2) {
                if (try_combo_engage_e()) return;
            }
            else if (em == 3) {
                if (has_ally_in_e_range()) {
                    if (try_combo_engage_e()) return;
                }
                if (target && try_combo_aggressive_e(target)) return;
            }
            else if (em == 1) {
                if (target && try_combo_aggressive_e(target)) return;
            }
            else /* em == 0 */ {
                // Threat-gated self-shield — only when an enemy is close
                // enough to actually threaten us.
                constexpr float SELF_SHIELD_THREAT_RANGE = 700.f;
                const float thr_sq = SELF_SHIELD_THREAT_RANGE * SELF_SHIELD_THREAT_RANGE;
                const auto ppos = p->get_position();
                for (auto* h : g_sdk->object_manager->get_heroes()) {
                    if (!is_valid_target(h)) continue;
                    const auto hp = h->get_position();
                    const float dx = hp.x - ppos.x;
                    const float dz = hp.z - ppos.z;
                    if (dx * dx + dz * dz <= thr_sq) { try_self_shield(); break; }
                }
            }
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

        auto* target = pick_hero_target(spell_data::q_range);
        if (menu::harass_use_q && target) {
            if (try_q(target, menu::harass_q_hitchance)) return;
        }

        if (menu::harass_use_w) try_w();
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

        // Save mana for R if it's up — keep enough for R + at least one Q follow-up.
        if (menu::clear_save_for_r && is_spell_ready(SLOT_R)) {
            const float reserve = p->get_spell_mana_cost(SLOT_R)
                                + p->get_spell_mana_cost(SLOT_Q);
            if (p->get_par() < reserve) return;
        }

        // W on minion cluster around ball
        if (menu::clear_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
            && !is_ball_in_transit()) {
            if (count_minions_near(get_ball_position(), spell_data::w_effect_radius)
                >= menu::clear_w_min) {
                if (sdk::common->cast_spell(SLOT_W)) {
                    last_spell_cast_time = now;
                    return;
                }
            }
        }

        if (!menu::clear_use_q || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        int q_hits = 0;
        const auto cluster = get_best_minion_cluster(spell_data::q_range,
                                                    spell_data::q_effect_radius,
                                                    &q_hits);
        if (cluster == math::vector3{} || q_hits < menu::clear_q_min) return;
        cast_q_at(cluster);
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

        const int min_w = std::max(1, menu::clear_w_min - 1);
        if (menu::clear_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
            && !is_ball_in_transit()) {
            if (count_minions_near(get_ball_position(), spell_data::w_effect_radius) >= min_w) {
                if (sdk::common->cast_spell(SLOT_W)) {
                    last_spell_cast_time = now;
                    return;
                }
            }
        }

        if (!menu::clear_use_q || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        // Fast clear: relax the Q min by 1 (at least 1).
        const int min_q = std::max(1, menu::clear_q_min - 1);
        int q_hits = 0;
        const auto cluster = get_best_minion_cluster(spell_data::q_range,
                                                    spell_data::q_effect_radius,
                                                    &q_hits);
        if (cluster == math::vector3{} || q_hits < min_q) return;
        cast_q_at(cluster);
    }

    void flee()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        // E first — recall the ball to self so W's MS field lands on us, not
        // wherever the ball was sitting (otherwise W's haste is wasted).
        const bool e_ready = menu::flee_use_e
                          && is_spell_ready(SLOT_E)
                          && has_enough_mana(SLOT_E)
                          && !e_recently_cast();
        const bool ball_recallable = g_ball_state != ball_state::on_self
                                  && !is_ball_in_transit();
        if (e_ready && ball_recallable) {
            if (sdk::common->cast_spell(SLOT_E, p)) {
                stamp_e_cast();
                return;
            }
        }

        // W only when the ball is on us (or arriving) — otherwise the haste
        // field lands at the ball, not at the player, and gets wasted.
        if (menu::flee_use_w && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
            && !is_ball_in_transit()
            && g_ball_state == ball_state::on_self) {
            if (sdk::common->cast_spell(SLOT_W))
                last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        }
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        update_ball_state();

        // Lazy per-ally menu refresh — throttled, cheap when nothing to do.
        if (g_ally_shield_menu) {
            const float now = g_sdk->clock_facade->get_game_time();
            if (now - g_ally_menu_last_refresh >= 2.0f) {
                g_ally_menu_last_refresh = now;
                refresh_ally_shield_menu();
            }
        }

        // Semi-manual R runs independent of orbwalker mode.
        if (try_semi_r()) return;
        if (try_auto_r()) return;
        // KS Q first — cheaper than burning R for the same kill.
        if (try_killsteal_q()) return;
        if (try_killsteal_r()) return;
        if (try_auto_q()) return;
        if (try_auto_w()) return;

        if (sdk::orbwalker && !sdk::orbwalker->combo()) {
            try_shield_ally();
        }

        if (!sdk::orbwalker) return;
        // Check lasthit BEFORE clear/fast_clear — VEN's clear() returns true
        // whenever any farming mode is active (including lasthit), so it would
        // otherwise swallow the lasthit case and lasthit_mode() never runs.
        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->lasthit())    lasthit_mode();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->clear())      clear();
        else if (sdk::orbwalker->flee())       flee();
    }

    void __fastcall on_draw()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        const auto ppos = p->get_position();
        const auto bp = get_ball_position();

        auto draw_circle = [&](math::vector3 const& c, float r, uint32_t clr) {
            if (sdk::shaders) sdk::shaders->draw_world_circle(c, r, 2.f, clr);
            else if (g_sdk->renderer) g_sdk->renderer->add_circle_3d(c, r, 2.f, clr);
        };

        auto draw_circle_rgb = [&](math::vector3 const& c, float r, uint32_t clr) {
            if (sdk::shaders) {
                sdk::shaders->draw_world_circle(c, r, 2.f, clr,
                    shaders_sdk::world_circle_style::rgb);
            } else {
                draw_circle(c, r, clr);
            }
        };

        auto get_draw_color = [&](int slot, uint32_t clr) -> uint32_t {
            auto sp = p->get_spell(slot);
            if (!sp || sp->get_level() == 0) return 0u;
            if (menu::draw_only_ready && sp->get_cooldown() > 0.f) return 0u;
            return sp->get_cooldown() <= 0.f ? clr : ((clr & 0x00FFFFFFu) | 0x40000000u);
        };

        if (menu::draw_q_range) {
            const uint32_t c = get_draw_color(SLOT_Q, menu::color_q);
            if (c) draw_circle(ppos, spell_data::q_range, c);
        }
        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(ppos, spell_data::e_range, c);
        }
        if (menu::draw_w_range) {
            const uint32_t cw = get_draw_color(SLOT_W, menu::color_w);
            if (cw) {
                // Draw W where it would actually land:
                //   ball on self  → at the player
                //   ball mid-flight from Q / on ground / on ally → at the ball
                const auto& wpos = (g_ball_state == ball_state::on_self) ? ppos : bp;
                if (menu::draw_w_rgb) draw_circle_rgb(wpos, spell_data::w_effect_radius, cw);
                else                  draw_circle    (wpos, spell_data::w_effect_radius, cw);
            }
        }
        if (menu::draw_r_range) {
            const uint32_t cr = get_draw_color(SLOT_R, menu::color_r);
            if (cr) {
                if (menu::draw_r_rgb) draw_circle_rgb(bp, spell_data::r_effect_radius, cr);
                else                  draw_circle    (bp, spell_data::r_effect_radius, cr);
            }
        }

        // Auto Q / Auto R / E Mode indicators stacked below the player
        if (g_sdk->renderer) {
            const auto base = g_sdk->renderer->world_to_screen(ppos);
            float y = base.y + 30.f;  // start below the champion model
            if (menu::auto_q_enabled) {
                math::vector2 pos{ base.x, y };
                g_sdk->renderer->add_text("AUTO Q", 14.f, pos, 1, 0xFF00FF80u);
                y += 16.f;
            }
            if (menu::auto_r_enabled) {
                math::vector2 pos{ base.x, y };
                g_sdk->renderer->add_text("AUTO R", 14.f, pos, 1, 0xFFFF4040u);
                y += 16.f;
            }
            if (menu::combo_e_mode > 0 && menu::combo_e_mode < 4) {
                static constexpr const char* labels[] = {
                    "", "E: AGGR", "E: ENGAGE", "E: SMART"
                };
                static constexpr uint32_t colors[] = {
                    0u, 0xFFFFAA40u, 0xFF40C0FFu, 0xFF00FF80u
                };
                math::vector2 pos{ base.x, y };
                g_sdk->renderer->add_text(labels[menu::combo_e_mode],
                                          14.f, pos, 1,
                                          colors[menu::combo_e_mode]);
            }
        }
    }

    void __fastcall on_present()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !p->is_valid() || p->is_dead()) return;
        if (!menu::draw_r_dmg_bar) return;
        if (!sdk::damage || !g_sdk->renderer) return;

        auto r_sp = p->get_spell(SLOT_R);
        if (!r_sp || r_sp->get_level() == 0) return;

        const float scale = static_cast<float>(g_sdk->renderer->get_window_height()) / 1080.f;
        constexpr float HERO_OX = -45.6f, HERO_OY = 15.5f;
        constexpr float HERO_BW = 106.4f, HERO_BH = 11.5f;
        const float ox = HERO_OX * scale, oy = HERO_OY * scale;
        const float bw = HERO_BW * scale, bh = HERO_BH * scale;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead()) continue;
            if (h->get_team_id() == p->get_team_id()) continue;
            if (!h->is_visible()) continue;

            const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
            if (dmg <= 0.f) continue;

            const float hp  = h->get_hp();
            const float mhp = h->get_max_hp();
            if (mhp <= 0.f || hp <= 0.f) continue;

            const auto hbar = h->get_health_bar_position();
            if (hbar.x < 1.f && hbar.y < 1.f) continue;

            const float r0 = hp / mhp;
            const float r1 = std::max(0.f, hp - dmg) / mhp;
            if (r0 <= r1) continue;

            const float bl = hbar.x + ox;
            const float bt = hbar.y + oy;
            math::rect rc;
            rc.left   = bl + r1 * bw;
            rc.top    = bt;
            rc.right  = bl + r0 * bw;
            rc.bottom = bt + bh;

            g_sdk->renderer->add_rectangle_filled(rc, menu::color_r_dmg);
        }
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        const int slot = cast->get_spell_slot();
        const float now = g_sdk->clock_facade->get_game_time();

        if (slot == SLOT_Q) {
            const auto start = get_ball_position();
            auto dest = cast->get_end_pos();
            const auto ppos = p->get_position();
            if (ppos.distance(dest) > spell_data::q_range)
                dest = ppos.extended(dest, spell_data::q_range);

            const float dist = start.distance(dest);
            const float travel = dist / spell_data::ball_q_missile_speed;

            g_ball_transit_start   = start;
            g_ball_transit_dest    = dest;
            g_ball_transit_t_start = now;
            g_ball_transit_t_end   = now + travel;
            g_ball_missile         = nullptr;
            g_ball_state           = ball_state::in_transit_q;
        }
        else if (slot == SLOT_E) {
            const auto start = get_ball_position();
            auto* target = cast->get_target();
            math::vector3 dest;
            if (target && target != p && target->is_hero()
                && target->get_team_id() == p->get_team_id()) {
                g_ball_ally = target;
                dest = target->get_position();
            } else {
                g_ball_ally = nullptr;
                dest = p->get_position();
            }
            const float dist = start.distance(dest);
            const float travel = dist / spell_data::ball_e_missile_speed;

            g_ball_transit_start   = start;
            g_ball_transit_dest    = dest;
            g_ball_transit_t_start = now;
            g_ball_transit_t_end   = now + travel;
            g_ball_missile         = nullptr;
            g_ball_state           = ball_state::in_transit_e;
        }
    }

    void __fastcall on_create_missile(game_object* missile)
    {
        if (!missile || !missile->is_valid()) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        auto* msc = missile->get_missile_spell_cast();
        if (!msc) return;
        auto* src = msc->get_source();
        if (!src || src != p) return;

        auto* sd = msc->get_spell_data();
        if (!sd) return;
        auto* st = sd->get_static_data();
        if (!st) return;

        const char* name = st->get_name();
        if (!name) return;

        if (std::strcmp(name, spell_data::q_missile_name) == 0
            || std::strcmp(name, spell_data::e_missile_name) == 0) {
            g_ball_missile = missile;
        }
    }

    void __fastcall on_delete_object(game_object* obj)
    {
        if (!obj) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        // Lost the ball object → ball returns to self
        if (obj->get_name() == spell_data::ball_object_name
            && obj->get_team_id() == p->get_team_id()) {
            if (!is_ball_in_transit() && g_ball_state != ball_state::on_self) {
                g_ball_state = ball_state::on_self;
                g_ball_ally = nullptr;
                g_ball_ground_valid = false;
            }
        }

        if (g_ball_missile && obj == g_ball_missile) {
            if (g_ball_state == ball_state::in_transit_q)      complete_q_transit();
            else if (g_ball_state == ball_state::in_transit_e) complete_e_transit();
            else                                                g_ball_missile = nullptr;
        }
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        if (obj == p && is_ball_buff_self_match(buff) && !is_ball_in_transit()) {
            g_ball_state = ball_state::on_self;
            g_ball_ally = nullptr;
            g_ball_missile = nullptr;
            g_ball_ground_valid = false;
            return;
        }

        if (obj != p && obj->is_hero() && obj->get_team_id() == p->get_team_id()
            && is_ball_buff_ally_match(buff) && !is_ball_in_transit()) {
            g_ball_state = ball_state::on_ally;
            g_ball_ally = obj;
            g_ball_missile = nullptr;
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        if (obj == p && is_ball_buff_self_match(buff)) {
            if (g_ball_state == ball_state::on_self && !is_ball_in_transit() && g_ball_ground_valid)
                g_ball_state = ball_state::on_ground;
            return;
        }

        if (obj == g_ball_ally && is_ball_buff_ally_match(buff)) {
            if (g_ball_state == ball_state::on_ally && !is_ball_in_transit()) {
                g_ball_ground_pos = obj->get_position();
                g_ball_ground_valid = true;
                g_ball_state = ball_state::on_ground;
                g_ball_ally = nullptr;
            }
        }
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("orianna", "MeowAIO - Orianna");
        if (!s_menu) {
            g_sdk->log_console("[!] Orianna: failed to create menu category");
            return;
        }

        // Q — Command: Attack
        auto q_menu = s_menu->add_sub_category("q_command_attack", "Q - Command: Attack");
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("ks_q", "Killsteal", menu::ks_use_q,
            [](bool v) { menu::ks_use_q = v; });
        q_menu->add_checkbox("lasthit", "Use in Lasthit", menu::lasthit_use_q,
            [](bool v) { menu::lasthit_use_q = v; });
        q_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_q,  [](bool v) { menu::clear_use_q = v; });
        q_menu->add_slider_int("clr_min", "  Clear Min Minions", 1, 6, 1, menu::clear_q_min,
            [](int v) { menu::clear_q_min = v; });
        q_menu->add_slider_int("c_hc", "Hitchance",  0, 100, 5, menu::combo_q_hitchance,
            [](int v) { menu::combo_q_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_hotkey("auto_q_toggle", "Auto Q", 'K', false, true,
            [](std::string* key, bool state) {
                menu::auto_q_enabled = state;
                static bool captured = false;
                if (key && !captured) { g_auto_q_key = *key; captured = true; }
            });
        q_menu->add_slider_int("auto_q_hc", "  Auto Q Hitchance", 0, 100, 5,
            menu::auto_q_hitchance, [](int v) { menu::auto_q_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Command: Dissonance
        auto w_menu = s_menu->add_sub_category("w_command_dissonance", "W - Command: Dissonance");
        w_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_w,  [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w, [](bool v) { menu::harass_use_w = v; });
        w_menu->add_checkbox("auto_w", "Auto W", menu::auto_w_enabled,
            [](bool v) { menu::auto_w_enabled = v; });
        w_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_w,
            [](bool v) { menu::flee_use_w = v; });
        w_menu->add_checkbox("lasthit", "Use in Lasthit", menu::lasthit_use_w,
            [](bool v) { menu::lasthit_use_w = v; });
        w_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_w,  [](bool v) { menu::clear_use_w = v; });
        w_menu->add_slider_int("c_min", "  Clear Min Minions", 1, 7, 1, menu::clear_w_min,
            [](int v) { menu::clear_w_min = v; });
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });
        w_menu->add_checkbox("draw_rgb", "RGB Draw", menu::draw_w_rgb,
            [](bool v) { menu::draw_w_rgb = v; });

        // E — Command: Protect
        auto e_menu = s_menu->add_sub_category("e_command_protect", "E - Command: Protect");
        e_menu->add_checkbox("shield_combo_self", "Use in Combo",
            menu::shield_combo_self, [](bool v) { menu::shield_combo_self = v; });
        e_menu->add_combo("e_mode", "E Mode",
            { "Off", "Aggressive", "Engage", "Smart" }, menu::combo_e_mode,
            [](int v) { menu::combo_e_mode = v; });
        e_menu->add_hotkey("e_mode_cycle", "  Cycle E Mode", 'J', false, false,
            [](std::string* key, bool pressed) {
                static bool was_pressed = false;
                static bool key_captured = false;
                // Capture display key once so the infotab has it; avoid per-tick
                // string copies while the key is held (causes movement stutter).
                if (key && !key_captured) {
                    g_e_mode_key = *key;
                    key_captured = true;
                }
                if (pressed && !was_pressed) {
                    menu::combo_e_mode = (menu::combo_e_mode + 1) % 4;
                    if (g_sdk) {
                        const char* names[] = { "Off", "Aggressive", "Engage", "Smart" };
                        g_sdk->log_console("[+] Orianna E mode: %s",
                                           names[menu::combo_e_mode]);
                    }
                }
                was_pressed = pressed;
            });
        e_menu->add_slider_int("engage_e_min", "  Engage Min Enemies in R",
            2, 5, 1, menu::combo_engage_e_min_enemies,
            [](int v) { menu::combo_engage_e_min_enemies = v; });
        e_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_e,
            [](bool v) { menu::flee_use_e = v; });
        e_menu->add_checkbox("shield_allies", "Auto Shield Allies",
            menu::shield_allies, [](bool v) { menu::shield_allies = v; });
        e_menu->add_slider_int("hp", "  HP Threshold %", 10, 80, 5, menu::shield_hp_pct,
            [](int v) { menu::shield_hp_pct = v; });
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // Per-ally shield toggles — built lazily on_update so late-joining
        // allies still get menu entries. The submenu handle is kept so we
        // can append checkboxes after init.
        g_ally_shield_menu = e_menu->add_sub_category("shield_allies_list", "Shield Per Ally");

        // R — Command: Shockwave
        auto r_menu = s_menu->add_sub_category("r_command_shockwave", "R - Command: Shockwave");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_checkbox("ks_r", "Killsteal",
            menu::ks_use_r, [](bool v) { menu::ks_use_r = v; });
        r_menu->add_checkbox("killable", "Only if killable", menu::combo_r_killable,
            [](bool v) { menu::combo_r_killable = v; });
        r_menu->add_separator();
        r_menu->add_hotkey("semi_r", "Semi R", 'T', false, false,
            [](std::string* key, bool pressed) {
                menu::semi_r_pressed = pressed;
                static bool captured = false;
                if (key && !captured) { g_semi_r_key = *key; captured = true; }
            });
        r_menu->add_slider_int("semi_min", "  Semi R Min %",
            1, 5, 1, menu::semi_r_min_targets,
            [](int v) { menu::semi_r_min_targets = v; });
        r_menu->add_separator();
        r_menu->add_hotkey("auto_r_toggle", "Auto R", 'L', false, true,
            [](std::string* key, bool state) {
                menu::auto_r_enabled = state;
                static bool captured = false;
                if (key && !captured) { g_auto_r_key = *key; captured = true; }
            });
        r_menu->add_slider_int("auto_min", "  Auto R Min Enemies",
            1, 5, 1, menu::auto_r_min_targets,
            [](int v) { menu::auto_r_min_targets = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });
        r_menu->add_checkbox("draw_rgb", "RGB Draw", menu::draw_r_rgb,
            [](bool v) { menu::draw_r_rgb = v; });

        // Mana
        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });
        mana_menu->add_slider_int("clear",  "Clear Mana %",  0, 100, 5, menu::clear_mana_pct,
            [](int v) { menu::clear_mana_pct = v; });
        mana_menu->add_checkbox("save_r", "Save for R", menu::clear_save_for_r,
            [](bool v) { menu::clear_save_for_r = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_q",  "Q Color",   menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("col_w", "W Color", menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("col_r", "R Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::create_missile,
            reinterpret_cast<void*>(on_create_missile));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_present));

        // Infotab entries — show key + on/off state for each toggle hotkey.
        if (sdk::infotab) {
            constexpr uint32_t COL_ON  = 0xFF00FF80u;  // green
            constexpr uint32_t COL_OFF = 0xFFFF4040u;  // red

            g_infotab_auto_q = sdk::infotab->add_hotkey_text(
                &g_auto_q_key,
                infotab_sdk::text_entry{ "Auto Q", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    return { menu::auto_q_enabled ? "ON" : "OFF",
                             menu::auto_q_enabled ? COL_ON : COL_OFF };
                });

            g_infotab_auto_r = sdk::infotab->add_hotkey_text(
                &g_auto_r_key,
                infotab_sdk::text_entry{ "Auto R", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    return { menu::auto_r_enabled ? "ON" : "OFF",
                             menu::auto_r_enabled ? COL_ON : COL_OFF };
                });

            g_infotab_semi_r = sdk::infotab->add_hotkey_text(
                &g_semi_r_key,
                infotab_sdk::text_entry{ "Semi R", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    return { menu::semi_r_pressed ? "HOLD" : "",
                             menu::semi_r_pressed ? COL_ON : COL_OFF };
                });

            g_infotab_e_mode = sdk::infotab->add_hotkey_text(
                &g_e_mode_key,
                infotab_sdk::text_entry{ "E Mode", 0xFFFFFFFFu },
                []() -> infotab_sdk::text_entry {
                    static constexpr const char* names[] = {
                        "Off", "Aggressive", "Engage", "Smart"
                    };
                    static constexpr uint32_t colors[] = {
                        0xFF888888u, 0xFFFFAA40u, 0xFF40C0FFu, 0xFF00FF80u
                    };
                    const int m = (menu::combo_e_mode >= 0 && menu::combo_e_mode < 4)
                                ? menu::combo_e_mode : 0;
                    return { names[m], colors[m] };
                });
        }

        g_sdk->log_console("[MeowAIO] Orianna module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
                reinterpret_cast<void*>(on_process_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::create_missile,
                reinterpret_cast<void*>(on_create_missile));
            g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
                reinterpret_cast<void*>(on_delete_object));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
            g_sdk->event_manager->unregister_callback(event_manager::event::present,
                reinterpret_cast<void*>(on_present));
        }

        if (sdk::infotab) {
            if (g_infotab_auto_q) sdk::infotab->remove_text(g_infotab_auto_q);
            if (g_infotab_auto_r) sdk::infotab->remove_text(g_infotab_auto_r);
            if (g_infotab_semi_r) sdk::infotab->remove_text(g_infotab_semi_r);
            if (g_infotab_e_mode) sdk::infotab->remove_text(g_infotab_e_mode);
        }
        g_infotab_auto_q = g_infotab_auto_r = g_infotab_semi_r = g_infotab_e_mode = 0;

        s_menu = nullptr;
        // Keep g_ally_shield_enabled across re-inits within the session so
        // user-configured per-ally toggles aren't lost between games. The
        // menu instance, however, is rebuilt on the next load, so clear the
        // "added to menu" tracking to force checkboxes to be re-created.
        g_ally_menu_added.clear();
        g_ally_shield_menu = nullptr;
        g_ally_menu_last_refresh = 0.f;
        g_ball_state = ball_state::on_self;
        g_ball_missile = nullptr;
        g_ball_ally = nullptr;
        g_ball_ground_valid = false;
        last_spell_cast_time = 0.f;
        last_e_cast_time = 0.f;

        if (g_sdk) g_sdk->log_console("[-] Orianna module unloaded");
    }
}
