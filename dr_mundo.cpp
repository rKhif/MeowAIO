#include "dr_mundo.h"

#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace dr_mundo
{
    static menu_category* s_menu = nullptr;

    // --- State refreshed each tick ---
    static bool          w_active             = false;
    static float         w_start_time         = 0.f;
    static bool          e_empowered          = false;
    static bool          e_pending_reset      = false;
    static bool          r_active             = false;

    // --- Passive canister tracking ---
    static math::vector3 canister_pos         = {};
    static float         canister_time        = 0.f;
    static bool          has_canister         = false;

    // --- Cast pacing ---
    static constexpr float MIN_SPELL_DELAY = 0.05f;
    static float           last_spell_cast_time = 0.f;

    static constexpr float R_BURST_WINDOW          = 1.5f;
    static constexpr float W_AUTO_RECAST_BUFFER    = 0.2f;
    static constexpr float W_NO_COMBAT_RECAST_TIME = 1.0f;

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

    // Mundo costs HP — use HP percentage as the "mana" gate.
    float hp_pct()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const float mx = p->get_max_hp();
        if (mx <= 0.f) return 0.f;
        return (p->get_hp() / mx) * 100.f;
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
    // BUFF HELPERS — hash-first match with substring-name fallback
    // ============================================================================
    static bool is_mundo_buff(buff_instance* b, uint32_t hash, const char* frag)
    {
        if (!b) return false;
        if (hash != 0u) return b->get_hash() == hash;
        if (!frag) return false;
        return b->get_name().find(frag) != std::string::npos;
    }

    static int count_enemies_in_range(float range)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        const auto pos = p->get_position();
        int n = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            if (pos.distance(h->get_position()) <= range) ++n;
        }
        return n;
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

    // Manual minion-collision check between player and Q cast endpoint
    // (since forbidden_collisions on spell_data is the documented heap-cross trap)
    static bool q_blocked_by_minion(const math::vector3& cast_pos, game_object* skip)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        const auto src = p->get_position();
        const float dx = cast_pos.x - src.x;
        const float dz = cast_pos.z - src.z;
        const float seg_len2 = dx * dx + dz * dz;
        if (seg_len2 <= 1.f) return false;

        const float w_half = spell_data::q_radius;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (m == skip || !m || !m->is_valid() || m->is_dead() || !m->is_targetable()) continue;
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
    // DAMAGE HELPERS
    // ============================================================================
    static int q_rank()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        auto s = p->get_spell(SLOT_Q);
        return s ? s->get_level() : 0;
    }
    static int w_rank()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        auto s = p->get_spell(SLOT_W);
        return s ? s->get_level() : 0;
    }
    static int e_rank()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        auto s = p->get_spell(SLOT_E);
        return s ? s->get_level() : 0;
    }

    static float get_q_damage(game_object* target)
    {
        if (!target) return 0.f;
        const int rank = q_rank();
        if (rank <= 0) return 0.f;
        const float pct_dmg = spell_data::q_curr_hp_pct[rank - 1] * target->get_hp();
        float raw = std::max(pct_dmg, spell_data::q_min_damage[rank - 1]);
        if (target->is_monster())
            raw = std::min(raw, spell_data::q_monster_cap[rank - 1]);
        return raw;
    }

    static float get_e_damage(game_object* target)
    {
        if (!target) return 0.f;
        const int rank = e_rank();
        if (rank <= 0) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;

        const float bonus_hp = std::max(0.f, p->get_max_hp()
                                         - p->get_flat_base_hp_pool_mod()
                                         - 640.f); // approx base, full bonus calc isn't critical
        const float base = spell_data::e_base_damage[rank - 1]
                         + spell_data::e_bonus_hp_ratio * bonus_hp;

        const float hp_p = p->get_max_hp() > 0.f ? p->get_hp() / p->get_max_hp() : 1.f;
        const float missing = 1.f - hp_p;
        const float amp = spell_data::e_missing_hp_max_amp
                        * std::min(missing / spell_data::e_missing_hp_cap, 1.f);
        float raw = base * (1.f + amp);

        if (target->is_monster())          raw *= spell_data::e_monster_mult;
        else if (target->is_lane_minion()) raw *= spell_data::e_minion_mult;
        return raw;
    }

    static float get_w_recast_damage(game_object* target)
    {
        if (!target) return 0.f;
        const int rank = w_rank();
        if (rank <= 0) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0.f;
        const float bonus_hp = std::max(0.f, p->get_max_hp() - 640.f);
        return spell_data::w_base_damage[rank - 1]
             + spell_data::w_recast_bonus_hp * bonus_hp;
    }

    static float get_combo_damage(game_object* target)
    {
        if (!target) return 0.f;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !sdk::damage) return 0.f;

        float dmg = 0.f;
        if (is_spell_learned(SLOT_Q)) dmg += get_q_damage(target);
        if (is_spell_learned(SLOT_W)) dmg += get_w_recast_damage(target);
        if (is_spell_learned(SLOT_E)) dmg += get_e_damage(target);
        // 3 AAs of follow-up
        const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                  p->get_attack_damage());
        dmg += aa * 3.f;
        return dmg;
    }

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool try_q(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_Q)) return false;

        auto pred = predict_q(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        if (target->is_hero() && q_blocked_by_minion(pred.cast_position, target))
            return false;

        bool ok = sdk::common->cast_spell(SLOT_Q, pred.cast_position);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_w_activate()
    {
        if (w_active) return false;
        if (!is_spell_ready(SLOT_W) || !is_spell_learned(SLOT_W)) return false;
        if (count_enemies_in_range(spell_data::w_radius + 100.f) == 0) return false;
        if (!sdk::common) return false;

        bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static void w_recast_logic()
    {
        if (!w_active || !is_spell_ready(SLOT_W)) return;
        if (!sdk::common) return;

        const float now = g_sdk->clock_facade->get_game_time();
        const float elapsed = now - w_start_time;
        if (elapsed < spell_data::w_recast_delay) return;

        // Auto-recast right before expiry to avoid losing the heal
        if (elapsed >= spell_data::w_duration - W_AUTO_RECAST_BUFFER) {
            sdk::common->cast_spell(SLOT_W);
            last_spell_cast_time = now;
            return;
        }

        const bool enemies_in = count_enemies_in_range(spell_data::w_radius) > 0;

        // Past high-storage window with enemies nearby — recast for full heal
        if (elapsed >= spell_data::w_high_storage_window && enemies_in) {
            sdk::common->cast_spell(SLOT_W);
            last_spell_cast_time = now;
            return;
        }

        // No enemies nearby — recast after brief window for partial heal
        if (!enemies_in && elapsed >= W_NO_COMBAT_RECAST_TIME) {
            sdk::common->cast_spell(SLOT_W);
            last_spell_cast_time = now;
        }
    }

    static void r_defensive()
    {
        if (!menu::combo_use_r) return;
        if (!is_spell_ready(SLOT_R) || !is_spell_learned(SLOT_R) || r_active) return;
        if (!sdk::common) return;

        if (menu::r_only_nearby
            && count_enemies_in_range(static_cast<float>(menu::r_search_radius)) == 0)
            return;

        const float threshold = (float)menu::r_hp_pct;
        if (hp_pct() < threshold) {
            sdk::common->cast_spell(SLOT_R);
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            return;
        }

        if (!menu::r_burst_pred || !sdk::health_prediction) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const float now = g_sdk->clock_facade->get_game_time();
        const float predicted = sdk::health_prediction->get_predicted_health(p, now + R_BURST_WINDOW);
        const float predicted_pct = (p->get_max_hp() > 0.f) ? (predicted / p->get_max_hp()) * 100.f : 100.f;
        if (predicted_pct < threshold) {
            sdk::common->cast_spell(SLOT_R);
            last_spell_cast_time = now;
        }
    }

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo()
    {
        if (!sdk::orbwalker) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::q_range);
        if (!target) return;

        if (menu::combo_use_w) try_w_activate();
        if (menu::combo_use_q) try_q(target, menu::combo_q_hitchance);
        // E is fired via on_before_attack + on_execute_cast (AA-reset path)
    }

    void harass()
    {
        if (!sdk::orbwalker) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;
        if (hp_pct() < (float)menu::harass_hp_pct) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        auto* target = pick_hero_target(spell_data::q_range);
        if (!target) return;

        if (menu::harass_use_q) try_q(target, menu::harass_q_hitchance);
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (hp_pct() < (float)menu::clear_hp_pct) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        // Jungle priority
        if (sdk::target_selector) {
            auto* monster = sdk::target_selector->get_monster_target(
                [](game_object* m) {
                    return m && m->is_valid() && m->is_visible() && m->is_targetable()
                        && distance_to(m) <= spell_data::q_range;
                });
            if (monster) {
                if (menu::clear_use_q && is_spell_ready(SLOT_Q))
                    try_q(monster, pred_sdk::hitchance::low);
                if (menu::clear_use_w && !w_active && is_spell_ready(SLOT_W) && is_spell_learned(SLOT_W)
                    && distance_to(monster) <= spell_data::w_radius && sdk::common) {
                    sdk::common->cast_spell(SLOT_W);
                    last_spell_cast_time = now;
                }
                return;
            }
        }

        // Lane: Q on the lowest-HP minion in range if it would kill
        if (!menu::clear_use_q || !is_spell_ready(SLOT_Q) || !sdk::common) return;

        game_object* best = nullptr;
        float best_hp = FLT_MAX;
        for (auto* m : g_sdk->object_manager->get_minions()) {
            if (!is_valid_enemy_minion(m)) continue;
            if (distance_to(m) > spell_data::q_range) continue;
            const float h = m->get_hp();
            if (h < best_hp) { best_hp = h; best = m; }
        }
        if (best && get_q_damage(best) >= best->get_hp())
            try_q(best, pred_sdk::hitchance::low);
    }

    void fast_clear()
    {
        if (!sdk::orbwalker) return;
        if (hp_pct() < (float)menu::clear_hp_pct) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        // W if enough minions/monsters in W radius
        if (menu::fast_use_w && !w_active && is_spell_ready(SLOT_W) && is_spell_learned(SLOT_W)) {
            int count = 0;
            const auto pos = p->get_position();
            for (auto* m : g_sdk->object_manager->get_minions()) {
                if (!m || !m->is_valid() || m->is_dead() || !m->is_visible()) continue;
                if (m->get_team_id() == p->get_team_id()) continue;
                if (pos.distance(m->get_position()) <= spell_data::w_radius && ++count >= 2) break;
            }
            if (count >= 2 && sdk::common) {
                sdk::common->cast_spell(SLOT_W);
                last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            }
        }
    }

    void flee()
    {
        if (!menu::flee_use_q || !is_spell_ready(SLOT_Q)) return;

        // Q nearest chasing enemy hero to slow them
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        const auto pos = p->get_position();

        game_object* closest = nullptr;
        float closest_d = spell_data::q_range;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h)) continue;
            const float d = pos.distance(h->get_position());
            if (d < closest_d) { closest_d = d; closest = h; }
        }
        if (closest) try_q(closest, pred_sdk::hitchance::low);
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        // Expire passive canister tracking
        const float now = g_sdk->clock_facade->get_game_time();
        if (has_canister && now > canister_time + spell_data::passive_canister_time)
            has_canister = false;

        r_defensive();
        w_recast_logic();

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())      combo();
        else if (sdk::orbwalker->harass())     harass();
        else if (sdk::orbwalker->fast_clear()) fast_clear();
        else if (sdk::orbwalker->clear())      clear();
        else if (sdk::orbwalker->flee())       flee();
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
        if (menu::draw_w_range && w_active) {
            const uint32_t c = get_draw_color(SLOT_W, menu::color_w);
            if (c) draw_circle(pos, spell_data::w_radius, c);
        }
        if (menu::draw_canister && has_canister) {
            draw_circle(canister_pos, 80.f, menu::color_canister);
        }

        // R: Maximum Dosage active indicator above player
        if (menu::draw_r_active && r_active && g_sdk->renderer) {
            const auto screen = g_sdk->renderer->world_to_screen(pos);
            g_sdk->renderer->add_text("R ACTIVE", 14.f,
                { screen.x, screen.y - 60.f }, 1, menu::color_r);
        }

        if ((menu::draw_damage_bar || menu::draw_r_dmg_bar) && sdk::damage && g_sdk->renderer) {
            const bool r_learned = is_spell_learned(SLOT_R);
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;
                if (menu::draw_damage_bar) {
                    const float dmg = get_combo_damage(h);
                    if (dmg > 0.f) g_sdk->renderer->add_damage_indicator(h, dmg);
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

    void __fastcall on_execute_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;
        if (!cast->is_basic_attack() && !cast->is_special_attack()) return;
        if (!e_pending_reset) return;

        e_pending_reset = false;
        if (!is_spell_ready(SLOT_E) || !is_spell_learned(SLOT_E) || e_empowered) return;
        if (sdk::common) sdk::common->cast_spell(SLOT_E);
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        if (is_mundo_buff(buff, spell_data::buff_hash_w_active, spell_data::buff_name_w)) {
            w_active = true;
            w_start_time = g_sdk->clock_facade->get_game_time();
        }
        else if (is_mundo_buff(buff, spell_data::buff_hash_e_empower, spell_data::buff_name_e)) {
            e_empowered = true;
        }
        else if (is_mundo_buff(buff, spell_data::buff_hash_r_active, spell_data::buff_name_r)) {
            r_active = true;
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || obj->get_network_id() != p->get_network_id()) return;

        if (is_mundo_buff(buff, spell_data::buff_hash_w_active, spell_data::buff_name_w)) {
            w_active = false;
            w_start_time = 0.f;
        }
        else if (is_mundo_buff(buff, spell_data::buff_hash_e_empower, spell_data::buff_name_e)) {
            e_empowered = false;
        }
        else if (is_mundo_buff(buff, spell_data::buff_hash_r_active, spell_data::buff_name_r)) {
            r_active = false;
        }
    }

    void __fastcall on_create_object(game_object* obj)
    {
        if (!obj) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        if (obj->get_team_id() != p->get_team_id()) return;
        if (obj->get_name().find(spell_data::canister_name) == std::string::npos) return;

        has_canister = true;
        canister_pos = obj->get_position();
        canister_time = g_sdk->clock_facade->get_game_time();
    }

    void __fastcall on_delete_object(game_object* obj)
    {
        if (!obj || !has_canister) return;
        if (obj->get_name().find(spell_data::canister_name) == std::string::npos) return;
        has_canister = false;
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        if (!data || !data->target || !sdk::orbwalker) return true;
        if (!is_spell_ready(SLOT_E) || !is_spell_learned(SLOT_E) || e_empowered) return true;

        auto* target = data->target;
        bool want_e = false;

        if (target->is_hero()) {
            if (sdk::orbwalker->combo() && menu::combo_use_e) {
                want_e = true;
            } else if (sdk::orbwalker->harass() && menu::harass_use_e
                       && hp_pct() >= (float)menu::harass_hp_pct) {
                want_e = true;
            }
        }
        else if (sdk::orbwalker->clear() && sdk::orbwalker->is_spell_farm_enabled()) {
            if ((target->is_monster() || target->is_lane_minion()) && menu::clear_use_e)
                want_e = true;
        }
        else if (sdk::orbwalker->fast_clear() && menu::fast_use_e) {
            want_e = true;
        }
        else if (sdk::orbwalker->lasthit() && menu::lh_use_e && target->is_lane_minion()) {
            // E secures a kill an AA alone wouldn't
            auto p = g_sdk->object_manager->get_local_player();
            if (p && sdk::damage) {
                const float aa = sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target,
                                                          p->get_attack_damage());
                const float hp = target->get_hp();
                const float ed = get_e_damage(target);
                want_e = (hp > aa && hp <= aa + ed);
            }
        }

        if (want_e) e_pending_reset = true;
        return true;
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("dr_mundo", "MeowAIO - Dr. Mundo");
        if (!s_menu) {
            g_sdk->log_console("[!] Dr. Mundo: failed to create menu category");
            return;
        }

        // Q — Infected Bonesaw
        auto q_menu = s_menu->add_sub_category("q_infected_bonesaw", "Q - Infected Bonesaw");
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear",  "Use in Clear (lasthit lane / jungle)", menu::clear_use_q,
            [](bool v) { menu::clear_use_q = v; });
        q_menu->add_checkbox("flee",   "Use in Flee (slow chaser)", menu::flee_use_q,
            [](bool v) { menu::flee_use_q = v; });
        q_menu->add_separator();
        q_menu->add_slider_int("c_hc", "Combo Hitchance",  0, 100, 5, menu::combo_q_hitchance,
            [](int v) { menu::combo_q_hitchance = v; });
        q_menu->add_slider_int("h_hc", "Harass Hitchance", 0, 100, 5, menu::harass_q_hitchance,
            [](int v) { menu::harass_q_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Heart Zapper
        auto w_menu = s_menu->add_sub_category("w_heart_zapper", "W - Heart Zapper");
        w_menu->add_checkbox("combo", "Use in Combo (auto-activate + recast)", menu::combo_use_w,
            [](bool v) { menu::combo_use_w = v; });
        w_menu->add_checkbox("clear", "Use in Clear (jungle)", menu::clear_use_w,
            [](bool v) { menu::clear_use_w = v; });
        w_menu->add_checkbox("fast_clear", "Use in Fast Clear (2+ minions)", menu::fast_use_w,
            [](bool v) { menu::fast_use_w = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("draw_range", "Draw Range (when active)", menu::draw_w_range,
            [](bool v) { menu::draw_w_range = v; });

        // E — Blunt Force Trauma
        auto e_menu = s_menu->add_sub_category("e_blunt_force_trauma", "E - Blunt Force Trauma");
        e_menu->add_checkbox("combo",   "Use in Combo (AA reset)",  menu::combo_use_e,  [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("harass",  "Use in Harass",            menu::harass_use_e, [](bool v) { menu::harass_use_e = v; });
        e_menu->add_checkbox("clear",   "Use in Clear",             menu::clear_use_e,  [](bool v) { menu::clear_use_e = v; });
        e_menu->add_checkbox("fast_cl", "Use in Fast Clear",        menu::fast_use_e,   [](bool v) { menu::fast_use_e = v; });
        e_menu->add_checkbox("lasthit", "Use to Secure Lasthits",   menu::lh_use_e,     [](bool v) { menu::lh_use_e = v; });

        // R — Maximum Dosage
        auto r_menu = s_menu->add_sub_category("r_maximum_dosage", "R - Maximum Dosage");
        r_menu->add_checkbox("combo", "Auto R (defensive)", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_slider_int("hp", "R when HP%% under", 0, 100, 5, menu::r_hp_pct,
            [](int v) { menu::r_hp_pct = v; });
        r_menu->add_checkbox("burst", "R Burst Prediction (1.5s lookahead)", menu::r_burst_pred,
            [](bool v) { menu::r_burst_pred = v; });
        r_menu->add_checkbox("nearby", "Only When Enemies Nearby", menu::r_only_nearby,
            [](bool v) { menu::r_only_nearby = v; });
        r_menu->add_slider_int("rad", "Enemy Search Radius", 300, 1600, 50, menu::r_search_radius,
            [](int v) { menu::r_search_radius = v; });

        // Health thresholds (Mundo costs HP, not mana)
        auto hp_menu = s_menu->add_sub_category("hp_thresholds", "HP Thresholds");
        hp_menu->add_slider_int("harass", "Harass Min HP %", 0, 100, 5, menu::harass_hp_pct,
            [](int v) { menu::harass_hp_pct = v; });
        hp_menu->add_slider_int("clear",  "Clear Min HP %",  0, 100, 5, menu::clear_hp_pct,
            [](int v) { menu::clear_hp_pct = v; });

        // Drawings
        auto draw_menu = s_menu->add_sub_category("drawings", "Drawings");
        draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
            [](bool v) { menu::draw_only_ready = v; });
        draw_menu->add_checkbox("canister", "Passive Canister Marker", menu::draw_canister,
            [](bool v) { menu::draw_canister = v; });
        draw_menu->add_checkbox("r_active", "R Active Indicator", menu::draw_r_active,
            [](bool v) { menu::draw_r_active = v; });
        draw_menu->add_checkbox("dmg", "Combo Damage Indicator", menu::draw_damage_bar,
            [](bool v) { menu::draw_damage_bar = v; });
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator (HP-bar overlay)", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_q",   "Q Color",         menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("col_w",   "W Color",         menu::color_w,
            [](uint32_t c) { menu::color_w = c; });
        draw_menu->add_colorpicker("col_can", "Canister Color",  menu::color_canister,
            [](uint32_t c) { menu::color_canister = c; });
        draw_menu->add_colorpicker("col_r",   "R Active Color",  menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::execute_cast,
            reinterpret_cast<void*>(on_execute_cast));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->register_callback(event_manager::event::create_object,
            reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));

        if (sdk::orbwalker) {
            sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        g_sdk->log_console("[MeowAIO] Dr. Mundo module loaded");
    }

    void unload()
    {
        if (g_sdk && g_sdk->event_manager) {
            g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
                reinterpret_cast<void*>(on_update));
            g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
                reinterpret_cast<void*>(on_draw));
            g_sdk->event_manager->unregister_callback(event_manager::event::execute_cast,
                reinterpret_cast<void*>(on_execute_cast));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
            g_sdk->event_manager->unregister_callback(event_manager::event::create_object,
                reinterpret_cast<void*>(on_create_object));
            g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
                reinterpret_cast<void*>(on_delete_object));
        }

        if (sdk::orbwalker) {
            sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
                reinterpret_cast<void*>(on_before_attack));
        }

        s_menu = nullptr;
        w_active = false;
        w_start_time = 0.f;
        e_empowered = false;
        e_pending_reset = false;
        r_active = false;
        has_canister = false;
        canister_time = 0.f;
        last_spell_cast_time = 0.f;

        if (g_sdk) g_sdk->log_console("[-] Dr. Mundo module unloaded");
    }
}
