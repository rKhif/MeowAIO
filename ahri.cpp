#include "ahri.h"

#include <cstdio>
#include <cmath>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace ahri
{
    static menu_category* s_menu = nullptr;

    static constexpr float MIN_SPELL_DELAY = 0.05f;
    static float           last_spell_cast_time = 0.f;
    static float           last_q_cast_time     = 0.f;
    static float           last_e_cast_time     = 0.f;

    // ============================================================================
    // SPELL VALIDATION
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
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (m->get_team_id() == p->get_team_id()) return false;
        return m->is_lane_minion();
    }

    static float distance_to(game_object* o)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !o) return FLT_MAX;
        return p->get_position().distance(o->get_position());
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
    // STATE QUERIES
    // ============================================================================
    static bool has_r_active()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        auto* b = p->get_buff_by_hash(spell_data::buff_hash_r_recast);
        return b && b->is_active();
    }

    static bool target_is_charmed(game_object* t)
    {
        if (!t) return false;
        auto* b = t->get_buff_by_hash(spell_data::buff_hash_e_charm);
        return b && b->is_active();
    }

    // Returns the longest remaining duration (s) of any immobilizing CC on `obj`.
    static float immobile_duration(game_object* obj)
    {
        if (!obj) return 0.f;
        const float now = g_sdk->clock_facade->get_game_time();
        const buff_type cc_types[] = {
            buff_type::stun, buff_type::snare, buff_type::taunt, buff_type::fear,
            buff_type::charm, buff_type::suppression, buff_type::knockup, buff_type::knockback,
            buff_type::asleep,
        };

        float best = 0.f;
        for (auto* b : obj->get_buffs()) {
            if (!b || !b->is_active()) continue;
            const buff_type t = b->get_type();
            bool match = false;
            for (auto ct : cc_types) { if (t == ct) { match = true; break; } }
            if (!match) continue;
            const float rem = b->get_end_time() - now;
            if (rem > best) best = rem;
        }
        return best;
    }

    // E should only be cast on CC'd target if the CC outlasts the missile travel time.
    static bool is_e_cc_chain_safe(game_object* tgt)
    {
        if (!tgt) return true;
        const float rem = immobile_duration(tgt);
        if (rem <= 0.05f) return true;
        const float dist = distance_to(tgt);
        const float travel = (spell_data::e_speed > 0.f) ? (dist / spell_data::e_speed) : 0.f;
        return rem >= spell_data::e_cast_time + travel;
    }

    // ============================================================================
    // PREDICTION (crash-safe)
    // ============================================================================
    static pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_Q;
        spell.range              = spell_data::q_range;
        spell.radius             = spell_data::q_radius;
        spell.delay              = spell_data::q_delay;
        spell.projectile_speed   = spell_data::q_speed;
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    static pred_sdk::pred_data predict_e(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_E;
        spell.range              = spell_data::e_range;
        spell.radius             = spell_data::e_radius;
        spell.delay              = spell_data::e_delay;
        spell.projectile_speed   = spell_data::e_speed;
        spell.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, spell); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // Manual minion-collision check between player and the E cast endpoint
    // (per documented heap-cross trap, can't set forbidden_collisions on spell_data).
    static bool e_blocked_by_minion(const math::vector3& cast_pos)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto src = p->get_position();
        const float dx = cast_pos.x - src.x;
        const float dz = cast_pos.z - src.z;
        const float seg_len2 = dx * dx + dz * dz;
        if (seg_len2 <= 1.f) return false;

        const float w_half = spell_data::e_radius;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
            if (m->get_team_id() == p->get_team_id()) continue;
            if (!m->is_lane_minion()) continue;

            const auto mp = m->get_position();
            const float rel_dx = mp.x - src.x;
            const float rel_dz = mp.z - src.z;
            float t = (rel_dx * dx + rel_dz * dz) / seg_len2;
            if (t < 0.f || t > 1.f) continue;
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
    static bool try_q(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto pred = predict_q(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        bool ok = sdk::common->cast_spell(SLOT_Q, pred.cast_position);
        if (ok) {
            const float now = g_sdk->clock_facade->get_game_time();
            last_q_cast_time = now;
            last_spell_cast_time = now;
        }
        return ok;
    }

    static bool try_w(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;

        const float dist = distance_to(target);
        const float reach = (target_is_charmed(target) ? spell_data::w_range_charm
                                                       : spell_data::w_range)
                          + target->get_bounding_radius();
        if (dist > reach) return false;

        bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_e(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (!is_e_cc_chain_safe(target)) return false;

        auto pred = predict_e(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        if (target->is_hero() && e_blocked_by_minion(pred.cast_position))
            return false;

        bool ok = sdk::common->cast_spell(SLOT_E, pred.cast_position);
        if (ok) {
            const float now = g_sdk->clock_facade->get_game_time();
            last_e_cast_time = now;
            last_spell_cast_time = now;
        }
        return ok;
    }

    // R gap-close: dash toward target if outside E range but within E + dash range.
    static bool try_r_gap_close(game_object* target)
    {
        if (!target || !sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        // Only gap-close from the first cast (recast window also works but rarer)
        const float dist = distance_to(target);
        if (dist <= spell_data::e_range) return false;
        if (dist > spell_data::e_range + spell_data::r_dash_range + 100.f) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto pp = p->get_position();
        const auto tp = target->get_position();
        const float dash = std::min(spell_data::r_dash_range, dist - spell_data::e_range + 100.f);
        const auto dash_pos = pp.extended(tp, dash, true);

        bool ok = sdk::common->cast_spell(SLOT_R, dash_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    // ============================================================================
    // KILLSTEAL
    // ============================================================================
    static void killsteal_q()
    {
        if (!menu::ks_use_q || !is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::damage) return;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h, spell_data::q_range)) continue;
            if (venplus::utility::has_anti_burst_buff(h)) continue;
            // Q hits twice (out + return) — assume both connect for KS calc
            const float dmg = sdk::damage->get_spell_damage(p, h, SLOT_Q) * 2.f;
            const float ehp = h->get_hp() + h->get_all_shield();
            if (dmg >= ehp && try_q(h, pred_sdk::hitchance::medium)) return;
        }
    }

    // ============================================================================
    // AUTO Q/E ON CC
    // ============================================================================
    static void handle_auto_cc()
    {
        if (!menu::auto_q_cc && !menu::auto_e_cc) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float remain = immobile_duration(h);
            if (remain <= 0.05f) continue;

            const float dist = distance_to(h);

            if (menu::auto_e_cc && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)
                && dist <= spell_data::e_range) {
                const float travel = dist / spell_data::e_speed;
                if (remain >= spell_data::e_cast_time + travel) {
                    if (try_e(h, pred_sdk::hitchance::low)) return;
                }
            }

            if (menu::auto_q_cc && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)
                && dist <= spell_data::q_range) {
                if (try_q(h, pred_sdk::hitchance::low)) return;
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

        // Search wider when R is available for gap-close
        float search_range = spell_data::e_range;
        if (menu::combo_use_r && (has_r_active() || is_spell_ready(SLOT_R)))
            search_range += spell_data::r_dash_range;

        auto* target = pick_hero_target(search_range);
        if (!target) return;

        // R gap-close if E is on cooldown or out of range
        if (menu::combo_use_r) try_r_gap_close(target);

        if (menu::combo_e_first) {
            // E first mode: land charm, then W/Q follow up
            if (menu::combo_use_e && try_e(target, menu::combo_e_hitchance)) return;
            if (menu::combo_use_w && try_w(target)) return;
            if (menu::combo_use_q && try_q(target, menu::combo_q_hitchance)) return;
            return;
        }

        // Normal mode: E on charmed targets first, then W → Q
        if (menu::combo_use_e && target_is_charmed(target)
            && try_e(target, menu::combo_e_hitchance)) return;
        if (menu::combo_use_w && try_w(target)) return;
        if (menu::combo_use_q && try_q(target, menu::combo_q_hitchance)) return;
        if (menu::combo_use_e && try_e(target, menu::combo_e_hitchance)) return;
    }

    void harass()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (mana_pct() < (float)menu::harass_mana_pct) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::e_range);
        if (!target) return;

        if (menu::harass_use_e && try_e(target, menu::combo_e_hitchance)) return;
        if (menu::harass_use_q && try_q(target, menu::combo_q_hitchance)) return;
    }

    // Counts distinct lane minions whose hitbox intersects the Q line from
    // player toward `aim`, capped at q_range. spell_farm's aoe_hits_needed
    // is unreliable for Ahri (orb's two passes inflate hit counts), so we
    // gate on a manual count instead.
    static int count_minions_on_q_line(const math::vector3& aim)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        const auto src = p->get_position();
        float dx = aim.x - src.x;
        float dz = aim.z - src.z;
        const float seg_len = std::sqrt(dx * dx + dz * dz);
        if (seg_len <= 1.f) return 0;
        // Clamp the line to q_range so we don't count minions past the orb's reach.
        const float reach = std::min(seg_len, spell_data::q_range);
        dx = (dx / seg_len) * reach;
        dz = (dz / seg_len) * reach;
        const float seg_len2 = dx * dx + dz * dz;
        const float w_half = spell_data::q_radius;

        int count = 0;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            const auto mp = m->get_position();
            const float rel_dx = mp.x - src.x;
            const float rel_dz = mp.z - src.z;
            float t = (rel_dx * dx + rel_dz * dz) / seg_len2;
            if (t < 0.f || t > 1.f) continue;
            const float cx = src.x + dx * t;
            const float cz = src.z + dz * t;
            const float ddx = mp.x - cx;
            const float ddz = mp.z - cz;
            if (std::sqrt(ddx * ddx + ddz * ddz) <= w_half + m->get_bounding_radius())
                ++count;
        }
        return count;
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;
        if (!menu::clear_use_q) return;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::linear;
        spell.targetting_type    = pred_sdk::targetting_type::edge_to_edge;
        spell.spell_slot         = SLOT_Q;
        spell.range              = spell_data::q_range;
        spell.radius             = spell_data::q_radius;
        spell.delay              = spell_data::q_delay;
        spell.projectile_speed   = spell_data::q_speed;
        spell.expected_hitchance = pred_sdk::hitchance::any;

        pred_sdk::pred_data pred{};
        try {
            pred = sdk::orbwalker->spell_farm(
                spell, menu::clear_q_min,
                static_cast<spell_farm_flag>(spell_farm_flag::outside_of_attack_range
                                             | spell_farm_flag::when_attack_not_ready),
                dmg_sdk::damage_type::magical);
        } catch (...) { pred.is_valid = false; }

        if (!pred.is_valid || !sdk::common) return;

        // Verify the cast line will hit at least clear_q_min distinct minions.
        if (count_minions_on_q_line(pred.cast_position) < menu::clear_q_min) return;

        sdk::common->cast_spell(SLOT_Q, pred.cast_position);
        last_q_cast_time = g_sdk->clock_facade->get_game_time();
        last_spell_cast_time = last_q_cast_time;
    }

    void flee()
    {
        if (!sdk::common || !g_sdk->hud_manager) return;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const auto cursor = g_sdk->hud_manager->get_cursor_position();
        const auto pp = p->get_position();
        const auto dash_end = pp.extended(cursor, spell_data::r_dash_range, true);

        bool ok = sdk::common->cast_spell(SLOT_R, dash_end);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
    }

    static void semi_r()
    {
        if (!sdk::common || !g_sdk->hud_manager) return;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const auto cursor = g_sdk->hud_manager->get_cursor_position();
        const auto pp = p->get_position();
        const auto dash_end = pp.extended(cursor, spell_data::r_dash_range, true);
        sdk::common->cast_spell(SLOT_R, dash_end);
        last_spell_cast_time = g_sdk->clock_facade->get_game_time();
    }

    // Semi-manual E: while hotkey held, auto-fires charm at the best hero
    // in range using prediction. Skips minion-blocked and CC-chain-unsafe shots.
    static void semi_e()
    {
        if (!sdk::common || !sdk::prediction) return;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::e_range);
        if (!target) return;

        try_e(target, menu::semi_e_hitchance);
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        if (menu::semi_r_active) { semi_r(); return; }
        if (menu::semi_e_active) { semi_e(); return; }

        handle_auto_cc();
        killsteal_q();

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())  combo();
        else if (sdk::orbwalker->harass()) harass();
        else if (sdk::orbwalker->clear())  clear();
        else if (sdk::orbwalker->flee())   flee();
    }

    static float get_combo_damage(game_object* target)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !target || !sdk::damage) return 0.f;

        float dmg = 0.f;
        // Q hits out + return = 2× spell damage approximation
        if (is_spell_ready(SLOT_Q))
            dmg += sdk::damage->get_spell_damage(p, target, SLOT_Q) * 2.f;
        // W fires 3 flames; subsequent flames deal 40% (model as 1.0 + 0.4 + 0.4 = 1.8x)
        if (is_spell_ready(SLOT_W))
            dmg += sdk::damage->get_spell_damage(p, target, SLOT_W)
                 * (1.f + spell_data::w_subseq_dmg_pct * 2.f);
        if (is_spell_ready(SLOT_E))
            dmg += sdk::damage->get_spell_damage(p, target, SLOT_E);
        // R: 3 dashes worth of bolt damage when fully charged
        if (is_spell_ready(SLOT_R))
            dmg += sdk::damage->get_spell_damage(p, target, SLOT_R) * 3.f;
        // 2 AAs worth of follow-up
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        dmg += aa * 2.f;
        return dmg;
    }

    void __fastcall on_draw()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        const auto pos = p->get_position();

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
            if (c) draw_circle(pos, spell_data::q_range, c);
        }
        if (menu::draw_w_range) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(pos, spell_data::w_range, c);
        }
        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(pos, spell_data::e_range, c);
        }
        if (menu::draw_r_range) {
            const uint32_t c = get_draw_color(SLOT_R, menu::color_r);
            if (c) draw_circle(pos, spell_data::r_dash_range, c);
        }

        // Combo mode label above player
        if (menu::draw_combo_mode && g_sdk->renderer) {
            const auto screen = g_sdk->renderer->world_to_screen(pos);
            const char* label = menu::combo_e_first ? "E FIRST" : "NORMAL";
            const uint32_t color = menu::combo_e_first ? 0xFFFF40C0u : 0xFF80FFC0u;
            g_sdk->renderer->add_text(label, 14.f, { screen.x, screen.y - 80.f }, 1, color);
        }

        if ((menu::draw_damage_bar || menu::draw_killable_text || menu::draw_r_dmg_bar)
            && sdk::damage && g_sdk->renderer) {
            const bool r_learned = is_spell_learned(SLOT_R);
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                const float dmg = get_combo_damage(h);

                if (menu::draw_damage_bar && dmg > 0.f)
                    g_sdk->renderer->add_damage_indicator(h, dmg);

                if (menu::draw_killable_text && dmg > 0.f
                    && dmg >= h->get_hp() + h->get_all_shield()) {
                    const auto hbar = h->get_health_bar_position();
                    if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                        g_sdk->renderer->add_text("KILL", 14.f,
                            { hbar.x, hbar.y - 20.f }, 1, 0xFF00E600u);
                    }
                }

                if (menu::draw_r_dmg_bar && r_learned) {
                    const float r_dmg = sdk::damage->get_spell_damage(p, h, SLOT_R);
                    const float hp = h->get_hp(), mhp = h->get_max_hp();
                    if (r_dmg > 0.f && mhp > 0.f && hp > 0.f) {
                        const auto hbar = h->get_health_bar_position();
                        if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                            const float r0 = hp / mhp;
                            const float r1 = std::max(0.f, hp - r_dmg) / mhp;
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

    void __fastcall on_new_path(game_object* obj, bool is_dash, float /*dash_speed*/)
    {
        if (!menu::antigap_e || !obj || !is_dash) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj == p) return;
        if (!obj->is_valid() || obj->is_dead()) return;
        if (obj->get_team_id() == p->get_team_id()) return;
        if (!obj->is_hero()) return;
        if (distance_to(obj) > spell_data::e_range) return;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return;

        try_e(obj, pred_sdk::hitchance::low);
    }

    void __fastcall on_buff_gain(game_object* /*obj*/, buff_instance* /*buff*/) {}
    void __fastcall on_buff_loss(game_object* /*obj*/, buff_instance* /*buff*/) {}

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("ahri", "MeowAIO - Ahri");
        if (!s_menu) {
            g_sdk->log_console("[!] Ahri: failed to create menu category");
            return;
        }

        // Q — Orb of Deception
        auto q_menu = s_menu->add_sub_category("q_orb_of_deception", "Q - Orb of Deception");
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_q,  [](bool v) { menu::clear_use_q = v; });
        q_menu->add_slider_int("c_min", "  Clear Min Hits", 1, 6, 1, menu::clear_q_min,
            [](int v) { menu::clear_q_min = v; });
        q_menu->add_checkbox("ks", "Killsteal", menu::ks_use_q,
            [](bool v) { menu::ks_use_q = v; });
        q_menu->add_separator();
        q_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::combo_q_hitchance,
            [](int v) { menu::combo_q_hitchance = v; });
        q_menu->add_checkbox("auto_cc", "Auto Q on CC", menu::auto_q_cc,
            [](bool v) { menu::auto_q_cc = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Fox-Fire
        auto w_menu = s_menu->add_sub_category("w_fox_fire", "W - Fox-Fire");
        w_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_w,
            [](bool v) { menu::combo_use_w = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });

        // E — Charm
        auto e_menu = s_menu->add_sub_category("e_charm", "E - Charm");
        e_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_e,  [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_e, [](bool v) { menu::harass_use_e = v; });
        e_menu->add_separator();
        e_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 1, menu::combo_e_hitchance,
            [](int v) { menu::combo_e_hitchance = v; });
        e_menu->add_checkbox("auto_cc", "Auto E on CC (chain-safe)", menu::auto_e_cc,
            [](bool v) { menu::auto_e_cc = v; });
        e_menu->add_checkbox("antigap", "Anti-Gap-Closer E", menu::antigap_e,
            [](bool v) { menu::antigap_e = v; });
        e_menu->add_separator();
        e_menu->add_slider_int("semi_hc", "Semi-E Hitchance", 0, 100, 1, menu::semi_e_hitchance,
            [](int v) { menu::semi_e_hitchance = v; });
        e_menu->add_hotkey("semi_e", "Semi-Manual E (best target)", 'G', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_e_active = pressed;
                if (hk) menu::semi_e_key_str = *hk;
            });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // R — Spirit Rush
        auto r_menu = s_menu->add_sub_category("r_spirit_rush", "R - Spirit Rush");
        r_menu->add_checkbox("combo", "Use R to Gap-Close in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_separator();
        r_menu->add_checkbox("draw_range", "Draw Dash Range", menu::draw_r_range,
            [](bool v) { menu::draw_r_range = v; });
        r_menu->add_hotkey("semi_r", "Semi-Manual R (cursor)", 'T', false, false,
            [](std::string* hk, bool pressed) {
                menu::semi_r_active = pressed;
                if (hk) menu::semi_r_key_str = *hk;
            });

        // Combo mode toggle
        auto combo_menu = s_menu->add_sub_category("combo_mode", "Combo Mode");
        combo_menu->add_checkbox("e_first", "E First Mode", menu::combo_e_first,
            [](bool v) { menu::combo_e_first = v; });
        combo_menu->add_hotkey("e_first_toggle", "Toggle E First", 'Y', false, true,
            [](std::string* /*hk*/, bool active) {
                menu::combo_e_first = active;
                if (g_sdk) g_sdk->log_console("[Ahri] Combo mode: %s",
                    active ? "E FIRST" : "NORMAL");
            });

        // Mana
        auto mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds");
        mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
            [](int v) { menu::harass_mana_pct = v; });
        mana_menu->add_slider_int("clear",  "Clear Mana %",  0, 100, 5, menu::clear_mana_pct,
            [](int v) { menu::clear_mana_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("dmg",     "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_checkbox("kill",    "KILL Text on Killable", menu::draw_killable_text,
            [](bool v) { menu::draw_killable_text = v; });
        draw_menu->add_checkbox("mode",    "Combo Mode Indicator", menu::draw_combo_mode,
            [](bool v) { menu::draw_combo_mode = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
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
        g_sdk->event_manager->register_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        if (sdk::infotab) {
            infotab_sdk::text_entry title_r{ "Semi R", 0xFFFFFFFFu };
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key_str, title_r,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_r_active ? "ON" : "OFF", clr };
                });

            infotab_sdk::text_entry title_e{ "Semi E", 0xFFFFFFFFu };
            menu::infotab_semi_e = sdk::infotab->add_hotkey_text(
                &menu::semi_e_key_str, title_e,
                []() -> infotab_sdk::text_entry {
                    const uint32_t clr = menu::semi_e_active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { menu::semi_e_active ? "ON" : "OFF", clr };
                });
        }

        g_sdk->log_console("[MeowAIO] Ahri module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::new_path,
                reinterpret_cast<void*>(on_new_path));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }

        if (sdk::infotab) {
            if (menu::infotab_semi_r != 0) {
                sdk::infotab->remove_text(menu::infotab_semi_r);
                menu::infotab_semi_r = 0;
            }
            if (menu::infotab_semi_e != 0) {
                sdk::infotab->remove_text(menu::infotab_semi_e);
                menu::infotab_semi_e = 0;
            }
        }

        s_menu = nullptr;
        last_spell_cast_time = 0.f;
        last_q_cast_time = 0.f;
        last_e_cast_time = 0.f;
        menu::semi_r_active = false;
        menu::semi_e_active = false;

        if (g_sdk) g_sdk->log_console("[-] Ahri module unloaded");
    }
}
