#include "kaisa.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace kaisa
{
    static menu_category* s_menu = nullptr;

    // --- State flags refreshed each tick from buffs ---
    static bool q_evolved      = false;
    static bool w_evolved      = false;
    static bool e_evolved      = false;
    static bool e_charging     = false;
    static bool r_dashing      = false;
    static bool r_dashing_prev = false;

    static int  cached_q_targets = -1;
    static float last_w_cast_time   = 0.f;
    static float last_rw_buffer     = 0.f;

    static constexpr float MIN_SPELL_DELAY = 0.05f;
    static float last_spell_cast_time = 0.f;

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
        if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (m->get_team_id() == p->get_team_id()) return false;
        return m->is_lane_minion();
    }

    static bool is_valid_monster(game_object* m)
    {
        if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) return false;
        return m->is_visible() && m->is_monster();
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

    static float distance_to(game_object* obj)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !obj) return FLT_MAX;
        return p->get_position().distance(obj->get_position());
    }

    // ============================================================================
    // PASSIVE STATE
    // ============================================================================
    static int get_plasma_stacks(game_object* target)
    {
        if (!target) return 0;
        auto* b = target->get_buff_by_hash(spell_data::buff_hash_plasma);
        if (!b || !b->is_active()) return 0;
        const int s = (int)b->get_stacks();
        const int c = b->get_count();
        return std::max(s, c);
    }

    static bool has_r_marker(game_object* target)
    {
        if (!target) return false;
        auto* b = target->get_buff_by_hash(spell_data::buff_hash_r_marker);
        return b && b->is_active();
    }

    static void refresh_state_flags()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        q_evolved   = p->get_buff_by_hash(spell_data::buff_hash_q_evolved) != nullptr;
        w_evolved   = p->get_buff_by_hash(spell_data::buff_hash_w_evolved) != nullptr;
        e_evolved   = p->get_buff_by_hash(spell_data::buff_hash_e_evolved) != nullptr;
        e_charging  = p->get_buff_by_hash(spell_data::buff_hash_e_charging) != nullptr;

        r_dashing_prev = r_dashing;
        r_dashing      = p->get_buff_by_hash(spell_data::buff_hash_r_dash) != nullptr;
    }

    // Get current R range based on rank (0,1,2 -> 2000,2500,3000)
    static float get_r_range()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        auto sp = p->get_spell(SLOT_R);
        if (!sp || sp->get_level() <= 0) return 0.f;
        const int rank_idx = std::clamp(sp->get_level() - 1, 0, 2);
        return spell_data::r_ranges[rank_idx];
    }

    // ============================================================================
    // Q TARGET COUNT (cached per tick)
    // ============================================================================
    static int count_q_targets()
    {
        if (cached_q_targets >= 0) return cached_q_targets;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        const auto pos = p->get_position();

        int n = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pos.distance(h->get_position()) <= spell_data::q_range) ++n;
        }
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            if (pos.distance(m->get_position()) <= spell_data::q_range) ++n;
        }
        for (auto* m : g_sdk->object_manager->get_monsters()) {
            if (!is_valid_monster(m)) continue;
            if (pos.distance(m->get_position()) <= spell_data::q_range) ++n;
        }
        cached_q_targets = n;
        return n;
    }

    // ============================================================================
    // DAMAGE HELPERS
    // ============================================================================
    static float get_q_damage_split(game_object* target, int q_targets)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage || q_targets <= 0) return 0.f;

        // Per-missile spell damage
        const float base_per_missile = sdk::damage->get_spell_damage(p, target, SLOT_Q);
        if (base_per_missile <= 0.f) return 0.f;

        const int missiles  = q_evolved ? spell_data::q_missiles_evolved : spell_data::q_missiles;
        const int on_target = std::max(1, missiles / q_targets);

        float raw;
        if (target->is_hero() || target->is_monster()) {
            raw = base_per_missile
                + base_per_missile * spell_data::q_subsequent_factor
                  * static_cast<float>(on_target - 1);
        } else {
            raw = base_per_missile * static_cast<float>(on_target);
            if (target->get_hp() < target->get_max_hp() * spell_data::q_minion_low_hp_pct)
                raw *= 2.f;
        }
        return raw;
    }

    static float get_passive_flat(game_object* target, int stacks)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return 0.f;

        const int level = std::max(1, p->get_level());
        const float base    = (level <= 1) ? 5.f : 5.f + 18.f / 17.f * (level - 1);
        const float per_stk = ((level <= 1) ? 1.f : 1.f + 11.f / 17.f * (level - 1))
                            * static_cast<float>(stacks);
        const float ap      = p->get_ability_power();
        const float ap_dmg  = spell_data::passive_ap_base_ratio * ap
                            + spell_data::passive_ap_per_stack_ratio * ap * stacks;
        return base + per_stk + ap_dmg;
    }

    static float get_passive_detonation(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return 0.f;
        const float missing = target->get_max_hp() - target->get_hp();
        if (missing <= 0.f) return 0.f;
        const float ap  = p->get_ability_power();
        const float pct = spell_data::passive_detonation_miss_hp
                        + spell_data::passive_detonation_ap_per100 * (ap / 100.f);
        float raw = missing * pct;
        if (target->is_monster())
            raw = std::min(raw, spell_data::passive_monster_cap);
        return raw;
    }

    static float get_aa_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;
        return sdk::damage->calc_damage(dmg_sdk::damage_type::physical,
                                        p, target, p->get_attack_damage());
    }

    static float get_combo_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;

        float total = 0.f;
        const int q_tgts = count_q_targets();
        const int stacks = get_plasma_stacks(target);

        if (is_spell_learned(SLOT_Q) && q_tgts > 0)
            total += get_q_damage_split(target, q_tgts);

        if (is_spell_learned(SLOT_W))
            total += sdk::damage->get_spell_damage(p, target, SLOT_W);

        const int w_stk_add = w_evolved ? spell_data::w_plasma_stacks_evo
                                        : spell_data::w_plasma_stacks;
        const int after_w   = stacks + w_stk_add;
        const float aa      = get_aa_damage(target);

        // 3 AAs after applying W stacks
        for (int i = 0; i < 3; ++i) {
            total += aa;
            total += get_passive_flat(target, after_w + i);
        }
        if (after_w + 3 >= spell_data::plasma_max_stacks)
            total += get_passive_detonation(target);

        return total;
    }

    // ============================================================================
    // PREDICTION (crash-safe — only safe scalar fields, try/catch wrapped)
    // ============================================================================
    static pred_sdk::pred_data predict_w(game_object* target, int hitchance)
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

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // Manual minion-collision check between player and the W cast endpoint.
    // Per the documented heap-cross trap we cannot use spell_data::forbidden_collisions.
    static bool w_blocked_by_minion(const math::vector3& cast_pos, game_object* skip)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto src = p->get_position();
        const float dx = cast_pos.x - src.x;
        const float dz = cast_pos.z - src.z;
        const float seg_len2 = dx * dx + dz * dz;
        if (seg_len2 <= 1.f) return false;

        const float w_half = spell_data::w_radius;

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (m == skip) continue;
            if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
            if (m->get_team_id() == p->get_team_id()) continue;
            if (!m->is_lane_minion()) continue;

            const auto mp = m->get_position();
            const float rel_dx = mp.x - src.x;
            const float rel_dz = mp.z - src.z;
            float t = (rel_dx * dx + rel_dz * dz) / seg_len2;
            if (t < 0.f || t > 1.f) continue;  // outside the segment
            const float cx = src.x + dx * t;
            const float cz = src.z + dz * t;
            const float ddx = mp.x - cx;
            const float ddz = mp.z - cz;
            if (std::sqrt(ddx * ddx + ddz * ddz) <= w_half + m->get_bounding_radius())
                return true;
        }
        return false;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    bool cast_q()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;
        if (e_charging) return false;

        bool ok = sdk::common->cast_spell(SLOT_Q);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    bool cast_w(game_object* target, float range, int hitchance)
    {
        if (!target || !sdk::prediction || !sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;
        if (e_charging) return false;
        if (distance_to(target) > range) return false;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_w_cast_time < 0.15f) return false;

        auto pred = predict_w(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        if (menu::combo_w_collision_check && target->is_hero()
            && w_blocked_by_minion(pred.cast_position, target))
            return false;

        bool ok = sdk::common->cast_spell(SLOT_W, pred.cast_position);
        if (ok) {
            last_w_cast_time     = now;
            last_spell_cast_time = now;
        }
        return ok;
    }

    bool cast_e()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (e_charging) return false;
        // E is a self-buff — already-AS is fine to refresh, no gate needed
        bool ok = sdk::common->cast_spell(SLOT_E);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // R lands you within R_EFFECT_RADIUS of target. Pick a position on the line
    // between player and target that puts you within AA range of the target.
    static math::vector3 calc_r_landing(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target) return {};

        const auto src = p->get_position();
        const auto tp  = target->get_position();
        const float total = src.distance(tp);

        // Land us so the dash ends within AA range of target (~aa_range - 50)
        const float aa = p->get_attack_range() + p->get_bounding_radius()
                       + target->get_bounding_radius();
        const float ideal_dist_from_target = std::max(50.f, aa - 50.f);

        float dash_dist = std::max(0.f, total - ideal_dist_from_target);
        // R lands you within R_EFFECT_RADIUS of target, so no need to over-shoot
        if (dash_dist > total) dash_dist = total;
        return src.extended(tp, dash_dist, /*allow_over_extension=*/true);
    }

    bool cast_r(game_object* target, bool force)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (!has_r_marker(target)) return false;

        const float r_range = get_r_range();
        if (r_range <= 0.f) return false;
        if (distance_to(target) > r_range) return false;

        if (!force) {
            if (venplus::utility::has_anti_burst_buff(target)) return false;
            if (target->has_buff_of_type(buff_type::spell_shield)) return false;

            // Don't dash into a fight you can't win (limit nearby enemies)
            int nearby = 0;
            const auto tp = target->get_position();
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                if (tp.distance(h->get_position()) <= 800.f) ++nearby;
            }
            if (nearby > menu::combo_r_max_near) return false;

            if (menu::combo_r_only_kill) {
                const float dmg = get_combo_damage(target);
                const float hp  = target->get_hp() + target->get_all_shield();
                if (dmg < hp) return false;
            }
        }

        const auto landing = calc_r_landing(target);
        bool ok = sdk::common->cast_spell(SLOT_R, landing);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // ============================================================================
    // R-W BUFFER (cast W during R dash)
    // ============================================================================
    static void r_w_buffer()
    {
        if (!menu::combo_rw_buffer || !r_dashing) return;
        if (!sdk::orbwalker || !sdk::orbwalker->combo()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_rw_buffer < 0.143f) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W) || e_charging) return;

        const float w_range = static_cast<float>(menu::combo_w_range);
        auto* target = pick_hero_target(w_range);
        if (!target) return;

        if (cast_w(target, w_range, menu::w_hitchance))
            last_rw_buffer = now;
    }

    // ============================================================================
    // SEMI-W (manual hold-to-cast)
    // ============================================================================
    static void semi_w()
    {
        if (!menu::semi_w_active) return;
        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_w_cast_time < 0.25f) return;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W) || e_charging) return;
        const float w_range = static_cast<float>(menu::combo_w_range);
        auto* target = pick_hero_target(w_range);
        if (!target) return;
        cast_w(target, w_range, pred_sdk::hitchance::any); // user-driven, lower bar
    }

    // ============================================================================
    // KILLSTEAL
    // ============================================================================
    static bool target_is_killable_by(game_object* target, float dmg)
    {
        if (!target) return false;
        return dmg >= target->get_hp() + target->get_all_shield();
    }

    static void ks_q()
    {
        if (!menu::ks_use_q || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q))
            return;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return;
        const int q_tgts = count_q_targets();
        if (q_tgts <= 0) return;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h, spell_data::q_range)) continue;
            if (target_is_killable_by(h, get_q_damage_split(h, q_tgts))) {
                cast_q();
                return;
            }
        }
    }

    static void ks_w()
    {
        if (!menu::ks_use_w || !is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W) || e_charging)
            return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::damage) return;

        const float w_range = static_cast<float>(menu::combo_w_range);
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float d = distance_to(h);
            if (d <= spell_data::q_range || d > w_range) continue;
            const float wdmg = sdk::damage->get_spell_damage(p, h, SLOT_W);
            if (target_is_killable_by(h, wdmg)) {
                if (cast_w(h, w_range, menu::w_hitchance)) return;
            }
        }
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

        const float w_range = static_cast<float>(menu::combo_w_range);
        auto* target = pick_hero_target(w_range);
        if (!target) return;

        const float dist = distance_to(target);
        const float aa_dist = p->get_attack_range() + p->get_bounding_radius()
                            + target->get_bounding_radius();

        // R — engage marked enemy out of AA range
        if (menu::combo_use_r && is_spell_learned(SLOT_R) && is_spell_ready(SLOT_R)
            && dist > aa_dist && dist <= get_r_range() && has_r_marker(target)) {
            if (cast_r(target)) return;
        }

        // W — out of AA range, before AAs
        if (menu::combo_use_w && !e_charging && dist > aa_dist && dist <= w_range) {
            if (cast_w(target, w_range, menu::w_hitchance)) return;
        }

        // Q — fire if hits enough enemies (max gate)
        if (menu::combo_use_q) {
            const int n = count_q_targets();
            if (n > 0 && n <= menu::combo_q_max) cast_q();
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

        const float w_range = static_cast<float>(menu::harass_w_range);
        auto* target = pick_hero_target(w_range);
        if (!target) return;

        const float dist = distance_to(target);

        if (menu::harass_use_w && !e_charging && dist <= w_range) {
            if (cast_w(target, w_range, menu::w_hitchance)) return;
        }

        if (menu::harass_use_q) {
            const int n = count_q_targets();
            if (n > 0 && n <= menu::harass_q_max) cast_q();
        }
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::lane_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        // W on cannon-killable lane minion outside Q range
        const float w_range = static_cast<float>(menu::combo_w_range);
        if (menu::lane_use_w && !e_charging && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)
            && sdk::damage) {
            for (auto* m : g_sdk->object_manager->get_minions()) {
                if (!is_valid_enemy_minion(m)) continue;
                const float d = distance_to(m);
                if (d <= spell_data::q_range || d > w_range) continue;
                const float wdmg = sdk::damage->get_spell_damage(p, m, SLOT_W);
                if (wdmg >= m->get_hp()) {
                    if (cast_w(m, w_range, pred_sdk::hitchance::any)) return;
                }
            }
        }

        // Q if it kills at least one minion among a pack
        if (!menu::lane_use_q || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        const int q_tgts = count_q_targets();
        if (q_tgts < menu::lane_q_min) return;

        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            if (distance_to(m) > spell_data::q_range) continue;
            if (get_q_damage_split(m, q_tgts) >= m->get_hp()) {
                cast_q();
                return;
            }
        }
    }

    void fast_clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::jungle_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (!sdk::target_selector) return;
        auto* monster = sdk::target_selector->get_monster_target(
            [](game_object* m) {
                return is_valid_monster(m) && distance_to(m) <= spell_data::q_range;
            });
        if (!monster) {
            // No monster in Q range — try W on a visible epic monster
            if (menu::jungle_use_w && !e_charging && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
                for (auto* m : g_sdk->object_manager->get_monsters()) {
                    if (!is_valid_monster(m) || !m->is_epic_monster()) continue;
                    if (distance_to(m) > 1200.f) continue;
                    cast_w(m, spell_data::w_range, pred_sdk::hitchance::any);
                    break;
                }
            }
            return;
        }

        if (menu::jungle_use_q && !sdk::orbwalker->would_cancel_attack())
            cast_q();

        if (menu::jungle_use_w && monster->is_epic_monster() && !e_charging)
            cast_w(monster, spell_data::w_range, pred_sdk::hitchance::any);
    }

    void lasthit()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (!sdk::orbwalker->is_spell_farm_lasthit_enabled()) return;
        if (mana_pct() < (float)menu::lh_mana_pct) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead() || !sdk::damage) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        // W on cannon
        const float w_range = static_cast<float>(menu::combo_w_range);
        if (menu::lh_w_cannon && !e_charging && is_spell_ready(SLOT_W) && has_enough_mana(SLOT_W)) {
            for (auto* m : g_sdk->object_manager->get_minions()) {
                if (!is_valid_enemy_minion(m)) continue;
                if (!m->is_lane_minion_siege()) continue;
                if (distance_to(m) > w_range) continue;

                const float wdmg = sdk::damage->get_spell_damage(p, m, SLOT_W);
                const float hp = sdk::health_prediction
                    ? sdk::health_prediction->get_predicted_health(m, now + spell_data::w_delay)
                    : m->get_hp();
                if (hp > 0.f && wdmg >= hp) {
                    if (cast_w(m, w_range, pred_sdk::hitchance::any)) return;
                }
            }
        }

        // Q — cast if it secures lh_q_kills lasthits among the in-range pack
        if (!menu::lh_use_q || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        const int q_tgts = count_q_targets();
        if (q_tgts <= 0) return;

        int kills = 0;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            if (distance_to(m) > spell_data::q_range) continue;
            if (get_q_damage_split(m, q_tgts) >= m->get_hp()) ++kills;
        }
        if (kills >= menu::lh_q_kills) cast_q();
    }

    void flee()
    {
        if (!menu::flee_use_e) return;
        cast_e();
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        cached_q_targets = -1; // invalidate per tick
        refresh_state_flags();

        // Always-on hooks
        r_w_buffer();
        if (menu::semi_w_active) { semi_w(); return; }

        ks_q();
        ks_w();

        if (!sdk::orbwalker) return;

        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->clear())      clear();
        else if (sdk::orbwalker->lasthit())    lasthit();
        else if (sdk::orbwalker->flee())       flee();
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
            if (c) draw_circle(player_pos, spell_data::q_range + p->get_bounding_radius(), c);
        }
        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(player_pos, static_cast<float>(menu::combo_w_range), c);
        }
        if (menu::draw_r_range) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(player_pos, get_r_range(), c);
        }

        // Plasma stack count above each enemy hero
        if (menu::draw_plasma_stacks && g_sdk->renderer) {
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                const int s = get_plasma_stacks(h);
                if (s <= 0) continue;
                const auto hbar = h->get_health_bar_position();
                if (hbar.x < 1.f && hbar.y < 1.f) continue;
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%d", s);
                const uint32_t clr = (s >= spell_data::plasma_max_stacks)
                    ? 0xFFFF0000u  // red — ready to detonate
                    : 0xFF00FF80u; // green — building stacks
                g_sdk->renderer->add_text(buf, 16.f, { hbar.x + 20.f, hbar.y - 10.f }, 1, clr);
            }
        }

        // Evolution status above player
        if (menu::draw_evolution_text && g_sdk->renderer && (q_evolved || w_evolved || e_evolved)) {
            const auto screen = g_sdk->renderer->world_to_screen(player_pos);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "EVO: %s%s%s",
                q_evolved ? "Q " : "", w_evolved ? "W " : "", e_evolved ? "E" : "");
            g_sdk->renderer->add_text(buf, 14.f, { screen.x, screen.y - 60.f }, 1, 0xFF00FFAAu);
        }

        // Damage indicator: combo damage on each in-range enemy + R-only HP-bar overlay
        if ((menu::draw_damage_bar || menu::draw_r_dmg_bar) && sdk::damage && g_sdk->renderer) {
            const float w_range = static_cast<float>(menu::combo_w_range);
            const bool r_learned = is_spell_learned(SLOT_R);
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h, w_range)) continue;
                if (menu::draw_damage_bar) {
                    const float dmg = get_combo_damage(h);
                    if (dmg > 0.f) g_sdk->renderer->add_damage_indicator(h, dmg);
                }
                if (menu::draw_r_dmg_bar && r_learned) {
                    const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                    const float hp = h->get_hp(), mhp = h->get_max_hp();
                    if (dmg > 0.f && mhp > 0.f && hp > 0.f) {
                        const auto hbar = h->get_health_bar_position();
                        if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                            const float r0 = hp / mhp;
                            const float r1 = std::max(0.f, hp - dmg) / mhp;
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
    }

    void __fastcall on_buff_gain(game_object* /*obj*/, buff_instance* /*buff*/)
    {
        // Reserved for future hooks (kept symmetrical with other modules)
    }

    void __fastcall on_buff_loss(game_object* /*obj*/, buff_instance* /*buff*/)
    {
        // Reserved for future hooks
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("kaisa", "MeowAIO - Kai'Sa");
        if (!s_menu) {
            g_sdk->log_console("[!] Kai'Sa: failed to create menu category");
            return;
        }

        // Q — Icathian Rain
        auto q_menu = s_menu->add_sub_category("q_icathian_rain", "Q - Icathian Rain");
        q_menu->add_checkbox("combo",   "Use in Combo",  menu::combo_use_q,   [](bool v) { menu::combo_use_q = v; });
        q_menu->add_slider_int("c_max", "  Combo Max Targets", 1, 5, 1, menu::combo_q_max,
            [](int v) { menu::combo_q_max = v; });
        q_menu->add_checkbox("harass",  "Use in Harass", menu::harass_use_q,  [](bool v) { menu::harass_use_q = v; });
        q_menu->add_slider_int("h_max", "  Harass Max Targets", 1, 5, 1, menu::harass_q_max,
            [](int v) { menu::harass_q_max = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("clear",   "Use in Clear",  menu::lane_use_q,    [](bool v) { menu::lane_use_q = v; });
        q_menu->add_slider_int("c_min", "  Clear Min Minions", 1, 7, 1, menu::lane_q_min,
            [](int v) { menu::lane_q_min = v; });
        q_menu->add_checkbox("lasthit", "Lasthit",       menu::lh_use_q,      [](bool v) { menu::lh_use_q = v; });
        q_menu->add_slider_int("lh_kills", "  Lasthit Min Kills", 1, 4, 1, menu::lh_q_kills,
            [](int v) { menu::lh_q_kills = v; });
        q_menu->add_checkbox("jungle",  "Use in Jungle", menu::jungle_use_q,  [](bool v) { menu::jungle_use_q = v; });
        q_menu->add_checkbox("ks",      "Killsteal",     menu::ks_use_q,      [](bool v) { menu::ks_use_q = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Void Seeker
        auto w_menu = s_menu->add_sub_category("w_void_seeker", "W - Void Seeker");
        w_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_w,  [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_w, [](bool v) { menu::harass_use_w = v; });
        w_menu->add_checkbox("clear",  "Use in Clear (Cannon)", menu::lane_use_w,
            [](bool v) { menu::lane_use_w = v; });
        w_menu->add_checkbox("lh_cannon", "Use to Secure Cannon", menu::lh_w_cannon,
            [](bool v) { menu::lh_w_cannon = v; });
        w_menu->add_checkbox("jungle", "Use on Epic Monsters", menu::jungle_use_w,
            [](bool v) { menu::jungle_use_w = v; });
        w_menu->add_checkbox("ks",     "Killsteal",     menu::ks_use_w,     [](bool v) { menu::ks_use_w = v; });
        w_menu->add_separator();
        w_menu->add_slider_int("c_range", "Combo Max Range", 600, 3000, 50, menu::combo_w_range,
            [](int v) { menu::combo_w_range = v; });
        w_menu->add_slider_int("h_range", "Harass Max Range", 600, 3000, 50, menu::harass_w_range,
            [](int v) { menu::harass_w_range = v; });
        w_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::w_hitchance,
            [](int v) { menu::w_hitchance = v; });
        w_menu->add_checkbox("col_check", "Manual Minion Collision Check", menu::combo_w_collision_check,
            [](bool v) { menu::combo_w_collision_check = v; });
        w_menu->add_checkbox("rw_buffer", "Cast W during R Dash", menu::combo_rw_buffer,
            [](bool v) { menu::combo_rw_buffer = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });
        w_menu->add_hotkey("semi_w", "Semi-Manual W", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_w_active = pressed;
                if (hk) menu::semi_w_key_str = *hk;
            });

        // E — Supercharge
        auto e_menu = s_menu->add_sub_category("e_supercharge", "E - Supercharge");
        e_menu->add_checkbox("flee", "Use E in Flee", menu::flee_use_e,
            [](bool v) { menu::flee_use_e = v; });

        // R — Killer Instinct
        auto r_menu = s_menu->add_sub_category("r_killer_instinct", "R - Killer Instinct");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_checkbox("only_kill", "Only if Killable", menu::combo_r_only_kill,
            [](bool v) { menu::combo_r_only_kill = v; });
        r_menu->add_slider_int("max_near", "Max Enemies Near Target", 1, 5, 1, menu::combo_r_max_near,
            [](int v) { menu::combo_r_max_near = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });

        // Mana
        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass_mana", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });
        mana_menu->add_slider_int("clear_mana",  "Clear Mana %",  0, 100, 5, menu::lane_mana_pct,
            [](int v) { menu::lane_mana_pct = v; });
        mana_menu->add_slider_int("lh_mana",     "Lasthit Mana %", 0, 100, 5, menu::lh_mana_pct,
            [](int v) { menu::lh_mana_pct = v; });
        mana_menu->add_slider_int("jungle_mana", "Jungle Mana %", 0, 100, 5, menu::jungle_mana_pct,
            [](int v) { menu::jungle_mana_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("plasma_stacks", "Plasma Stacks on Enemies", menu::draw_plasma_stacks,
            [](bool v) { menu::draw_plasma_stacks = v; });
        draw_menu->add_checkbox("evo_text", "Evolution Status", menu::draw_evolution_text,
            [](bool v) { menu::draw_evolution_text = v; });
        draw_menu->add_checkbox("dmg_bar", "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("color_q", "Q Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("color_w", "W Color", menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("color_r", "R Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("color_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        if (sdk::infotab) {
            infotab_sdk::text_entry title{ "Semi W", 0xFFFFFFFFu };
            menu::infotab_semi_w = sdk::infotab->add_hotkey_text(
                &menu::semi_w_key_str, title,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_w_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_w_active ? "ON" : "OFF", clr };
                });
        }

        g_sdk->log_console("[MeowAIO] Kai'Sa module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }

        if (sdk::infotab && menu::infotab_semi_w != 0) {
            sdk::infotab->remove_text(menu::infotab_semi_w);
            menu::infotab_semi_w = 0;
        }

        s_menu = nullptr;
        q_evolved = w_evolved = e_evolved = false;
        e_charging = false;
        r_dashing = r_dashing_prev = false;
        cached_q_targets = -1;
        last_w_cast_time = 0.f;
        last_rw_buffer = 0.f;
        last_spell_cast_time = 0.f;
        menu::semi_w_active = false;

        if (g_sdk) g_sdk->log_console("[-] Kai'Sa module unloaded");
    }
}
