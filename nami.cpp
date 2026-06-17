#include "nami.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <vector>
#include <functional>

namespace nami
{
namespace
{
    menu_category* s_menu = nullptr;

    // Per-spell pacing so we don't spam casts that just got issued.
    float s_last_cast_time[4] = { 0.f, 0.f, 0.f, 0.f };

    // Deferred E casts (humanizer queue: { fire_at_time, network_id }).
    struct pending_e
    {
        float    fire_at;
        uint32_t target_id;
    };
    std::vector<pending_e> s_e_queue;

    // ----------------------------------------------------------------------
    // PLAYER / SPELL HELPERS
    // ----------------------------------------------------------------------
    game_object* get_player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    float now_time()
    {
        return (g_sdk && g_sdk->clock_facade) ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    int get_spell_level(int slot)
    {
        auto* p = get_player();
        if (!p) return 0;
        auto* sp = p->get_spell(slot);
        return sp ? sp->get_level() : 0;
    }

    bool is_spell_learned(int slot)
    {
        return get_spell_level(slot) > 0;
    }

    bool is_spell_ready(int slot)
    {
        auto* p = get_player();
        if (!p) return false;
        auto* sp = p->get_spell(slot);
        return sp && sp->get_level() > 0 && sp->get_cooldown() <= 0.f;
    }

    bool has_mana_for(int slot)
    {
        auto* p = get_player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    float mana_pct()
    {
        auto* p = get_player();
        if (!p) return 0.f;
        float mx = p->get_max_par();
        return mx > 0.f ? (p->get_par() / mx) * 100.f : 0.f;
    }

    float hp_pct(game_object* obj)
    {
        if (!obj) return 0.f;
        float mx = obj->get_max_hp();
        return mx > 0.f ? (obj->get_hp() / mx) * 100.f : 0.f;
    }

    float distance(game_object* a, game_object* b)
    {
        if (!a || !b) return FLT_MAX;
        return a->get_position().distance(b->get_position());
    }

    // ----------------------------------------------------------------------
    // TARGETING
    // ----------------------------------------------------------------------
    bool is_valid_enemy(game_object* unit, float range = FLT_MAX)
    {
        auto* p = get_player();
        if (!p || !unit || !unit->is_valid() || unit->is_dead()) return false;
        if (!unit->is_targetable() || !unit->is_visible()) return false;
        if (unit->get_team_id() == p->get_team_id()) return false;
        if (range != FLT_MAX && p->get_position().distance(unit->get_position()) > range)
            return false;
        return true;
    }

    bool is_valid_ally(game_object* unit, float range = FLT_MAX, bool include_self = true)
    {
        auto* p = get_player();
        if (!p || !unit || !unit->is_valid() || unit->is_dead()) return false;
        if (!include_self && unit->get_network_id() == p->get_network_id()) return false;
        if (unit->get_team_id() != p->get_team_id()) return false;
        if (range != FLT_MAX && p->get_position().distance(unit->get_position()) > range)
            return false;
        return true;
    }

    game_object* pick_hero_target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target();
            forced && is_valid_enemy(forced, range))
            return forced;
        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (is_valid_enemy(h, range)) return h;
        }
        return nullptr;
    }

    bool has_spell_shield(game_object* t)
    {
        return t && t->has_buff_of_type(buff_type::spell_shield);
    }

    bool is_slowed(game_object* t)
    {
        return t && t->has_buff_of_type(buff_type::slow);
    }

    // Closest approximation of "untargetable / can't be CC'd" in VEN SDK.
    bool is_unstoppable(game_object* t)
    {
        if (!t) return false;
        return t->has_buff_of_type(buff_type::spell_immunity)
            || t->has_buff_of_type(buff_type::invulnerability)
            || t->has_buff_of_type(buff_type::physical_immunity);
    }

    bool is_recalling(game_object* unit)
    {
        if (!unit) return false;
        auto* ac = unit->get_active_spell_cast();
        if (!ac) return false;
        auto* sc = ac->get_spell_cast();
        if (!sc) return false;
        auto* sd = sc->get_spell_data();
        if (!sd) return false;
        auto* st = sd->get_static_data();
        if (!st) return false;
        const char* n = st->get_name();
        if (!n) return false;
        // case-insensitive prefix match on "Recall"
        const char* r = "Recall";
        for (int i = 0; i < 6; ++i)
        {
            char c = n[i];
            if (!c) return false;
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (c != r[i]) return false;
        }
        return true;
    }

    // ----------------------------------------------------------------------
    // HEAL VALUE
    // ----------------------------------------------------------------------
    float compute_w_heal(bool rebound)
    {
        auto* p = get_player();
        if (!p) return 0.f;
        int rank = std::clamp(get_spell_level(SLOT_W), 0, 6);
        const float ap = p->get_ability_power();
        if (rebound)
            return spell_data::heal_min[rank] + spell_data::heal_min_ratio * ap;
        return spell_data::heal_base[rank] + spell_data::heal_ap_ratio * ap;
    }

    // ----------------------------------------------------------------------
    // PREDICTION HELPER
    // ----------------------------------------------------------------------
    pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.targetting_type    = pred_sdk::targetting_type::center;
        sd.spell_slot         = SLOT_Q;
        sd.range              = spell_data::q_range;
        sd.radius             = spell_data::q_radius;
        sd.delay              = spell_data::q_delay;
        sd.proc_delay         = spell_data::q_travel;
        sd.projectile_speed   = FLT_MAX;
        sd.expected_hitchance = hitchance;
        sd.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, sd); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    pred_sdk::pred_data predict_r(game_object* target, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.targetting_type    = pred_sdk::targetting_type::center;
        sd.spell_slot         = SLOT_R;
        sd.range              = spell_data::r_range;
        sd.radius             = spell_data::r_radius;
        sd.delay              = spell_data::r_delay;
        sd.projectile_speed   = spell_data::r_speed;
        sd.expected_hitchance = hitchance;
        sd.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, sd); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ----------------------------------------------------------------------
    // Q LOGIC
    // ----------------------------------------------------------------------
    bool can_cast_q_on(game_object* target)
    {
        if (!target) return false;
        if (has_spell_shield(target)) return false;
        if (is_unstoppable(target))  return false;
        if (menu::combo_q_only_slow && !is_slowed(target))
            return false;
        return true;
    }

    bool try_q_combo()
    {
        if (!menu::combo_use_q || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_Q] <= 0.4f) return false;

        auto* p = get_player();
        if (!p) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto* target = pick_hero_target(spell_data::q_range);
        if (!target || !can_cast_q_on(target)) return false;

        auto pred = predict_q(target, menu::combo_q_hitchance);
        if (!pred.is_valid || pred.hitchance < menu::combo_q_hitchance) return false;

        if (sdk::common->cast_spell(SLOT_Q, pred.cast_position))
        {
            s_last_cast_time[SLOT_Q] = t;
            return true;
        }
        return false;
    }

    // Auto-bubble anyone dashing toward us.
    bool try_q_gapclose()
    {
        if (!menu::combo_q_gapclose || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_Q] <= 0.4f) return false;

        auto* p = get_player();
        if (!p) return false;

        const auto self_pos = p->get_position();
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::q_range + 200.f)) continue;
            if (!h->is_dashing()) continue;
            if (!can_cast_q_on(h)) continue;

            // Only fire if they're closing in on us.
            const auto enemy_pos = h->get_position();
            const auto vel       = h->get_velocity();
            const float dx       = self_pos.x - enemy_pos.x;
            const float dz       = self_pos.z - enemy_pos.z;
            const float dist     = std::sqrt(dx * dx + dz * dz);
            if (dist < 1.f) continue;
            const float approach = (vel.x * dx + vel.z * dz) / dist;
            if (approach < 400.f) continue;

            auto pred = predict_q(h, pred_sdk::hitchance::medium);
            if (!pred.is_valid) continue;
            if (pred.hitchance < pred_sdk::hitchance::medium
                && pred.state != pred_sdk::prediction_state::dashing_or_casting)
                continue;

            if (sdk::common->cast_spell(SLOT_Q, pred.cast_position))
            {
                s_last_cast_time[SLOT_Q] = t;
                return true;
            }
        }
        return false;
    }

    // ----------------------------------------------------------------------
    // W LOGIC
    // ----------------------------------------------------------------------
    bool should_heal_ally(game_object* ally)
    {
        if (!menu::heal_use_w) return false;
        if (!is_valid_ally(ally, spell_data::w_range)) return false;
        if (mana_pct() < (float)menu::heal_mana_pct) return false;
        if (hp_pct(ally) > (float)menu::heal_hp_pct) return false;
        // Don't waste a heal that overheals into max.
        const float incoming = compute_w_heal(false);
        if (ally->get_hp() + incoming > ally->get_max_hp() + 1.f) return false;
        return true;
    }

    // Find a friendly bounce target near `from`.
    game_object* best_bounce_ally(game_object* from)
    {
        auto* p = get_player();
        if (!p || !from) return nullptr;

        game_object* best = nullptr;
        float best_dist = 726.f;
        const auto base_pos = from->get_position();
        const int from_team = from->get_team_id();

        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            if (!a || !a->is_valid() || a->is_dead()) continue;
            if (!a->is_targetable() || !a->is_visible()) continue;
            if (a->get_team_id() == from_team) continue;  // bounce flips teams
            const float d = a->get_position().distance(base_pos);
            if (d > best_dist) continue;
            // Don't pick a target that would be fully overhealed.
            if (a->get_team_id() == p->get_team_id()
                && a->get_hp() + compute_w_heal(true) > a->get_max_hp() + 1.f)
                continue;
            best_dist = d;
            best = a;
        }
        return best;
    }

    bool try_w()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_mana_for(SLOT_W)) return false;

        auto* p = get_player();
        if (!p) return false;
        if (is_recalling(p)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_W] <= 0.3f) return false;

        const bool in_combat = sdk::orbwalker
            && (sdk::orbwalker->combo() || sdk::orbwalker->harass());

        // 1) Bounce W: pick a primary target that bounces to a good secondary.
        // Only run when at least one mode the bounce can serve is enabled.
        if (menu::w_bounce && (menu::combo_use_w || menu::heal_use_w))
        {
            for (auto* primary : g_sdk->object_manager->get_heroes())
            {
                if (!primary || !primary->is_valid() || primary->is_dead()) continue;
                if (!primary->is_targetable()) continue;
                if (distance(p, primary) > spell_data::w_range) continue;

                const bool primary_ally  = primary->get_team_id() == p->get_team_id();
                const bool primary_enemy = !primary_ally;
                if (primary_enemy && !in_combat) continue;
                if (primary_ally && primary->get_hp() + compute_w_heal(false) > primary->get_max_hp() + 1.f)
                    continue;

                auto* second = best_bounce_ally(primary);
                if (!second) continue;

                // If primary is an enemy and second is also an enemy, we need to be in combat.
                const bool second_enemy = second->get_team_id() != p->get_team_id();
                if (second_enemy && !in_combat) continue;

                if (sdk::common->cast_spell(SLOT_W, primary))
                {
                    s_last_cast_time[SLOT_W] = t;
                    return true;
                }
            }
        }

        // 2) Simple heal-W on the worst-off ally.
        game_object* heal_target = nullptr;
        float worst_hp = 100.f;
        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            if (!should_heal_ally(a)) continue;
            const float pct = hp_pct(a);
            if (pct < worst_hp) { worst_hp = pct; heal_target = a; }
        }
        if (heal_target && sdk::common->cast_spell(SLOT_W, heal_target))
        {
            s_last_cast_time[SLOT_W] = t;
            return true;
        }

        // 3) Combo W on enemy hero.
        if (menu::combo_use_w && in_combat)
        {
            auto* target = pick_hero_target(spell_data::w_range);
            if (target && sdk::common->cast_spell(SLOT_W, target))
            {
                s_last_cast_time[SLOT_W] = t;
                return true;
            }
        }
        return false;
    }

    // ----------------------------------------------------------------------
    // E LOGIC
    // ----------------------------------------------------------------------
    bool fire_e_on(uint32_t network_id)
    {
        if (!sdk::common || !g_sdk->object_manager) return false;
        if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] <= 0.2f) return false;

        auto* obj = g_sdk->object_manager->get_object_by_network_id(network_id);
        if (!obj || !is_valid_ally(obj, spell_data::e_range)) return false;

        if (sdk::common->cast_spell(SLOT_E, obj))
        {
            s_last_cast_time[SLOT_E] = t;
            return true;
        }
        return false;
    }

    void schedule_e(uint32_t network_id)
    {
        const bool combo_required = menu::e_only_in_combo;
        const bool in_combo = sdk::orbwalker && sdk::orbwalker->combo();

        const int ms = std::max(0, menu::e_humanizer_ms);
        if (ms <= 1)
        {
            if (combo_required && !in_combo) return;
            fire_e_on(network_id);
            return;
        }
        s_e_queue.push_back({ now_time() + ms / 1000.f, network_id });
    }

    void process_e_queue()
    {
        if (s_e_queue.empty()) return;
        const float t = now_time();
        const bool combo_required = menu::e_only_in_combo;
        const bool in_combo = sdk::orbwalker && sdk::orbwalker->combo();

        s_e_queue.erase(
            std::remove_if(s_e_queue.begin(), s_e_queue.end(),
                [&](const pending_e& e)
                {
                    if (t < e.fire_at) return false;
                    if (combo_required && !in_combo) return true;
                    fire_e_on(e.target_id);
                    return true;
                }),
            s_e_queue.end());
    }

    bool e_wants_slot(int slot)
    {
        switch (slot)
        {
        case SLOT_Q: return menu::e_on_q;
        case SLOT_W: return menu::e_on_w;
        case SLOT_E: return menu::e_on_e;
        case SLOT_R: return menu::e_on_r;
        default: return false;
        }
    }

    // ----------------------------------------------------------------------
    // R LOGIC
    // ----------------------------------------------------------------------
    // Counts heroes the R wave would hit along the chosen direction.
    // The wave travels the full r_range, not just to end_pos, so we
    // project onto the unit direction and clamp to [0, r_range].
    int count_r_hits(const math::vector3& start_pos, const math::vector3& end_pos)
    {
        auto* p = get_player();
        if (!p) return 0;

        const float dx = end_pos.x - start_pos.x;
        const float dz = end_pos.z - start_pos.z;
        const float len2 = dx * dx + dz * dz;
        if (len2 <= 1.f) return 0;

        const float inv_len = 1.f / std::sqrt(len2);
        const float dirx    = dx * inv_len;
        const float dirz    = dz * inv_len;
        const float reach   = spell_data::r_range;

        int hits = 0;
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h)) continue;
            if (is_unstoppable(h)) continue;

            // Predict where the target will be when the wave reaches them.
            const float dist_to_target = start_pos.distance(h->get_position());
            const float t_to_target    = spell_data::r_delay + dist_to_target / spell_data::r_speed;
            math::vector3 predicted = h->get_position();
            if (sdk::prediction)
            {
                try { predicted = sdk::prediction->predict_on_path(h, t_to_target); }
                catch (...) { predicted = h->get_position(); }
            }

            const float rx   = predicted.x - start_pos.x;
            const float rz   = predicted.z - start_pos.z;
            const float proj = rx * dirx + rz * dirz;
            if (proj < 0.f || proj > reach) continue;

            const float cx = start_pos.x + dirx * proj;
            const float cz = start_pos.z + dirz * proj;
            const float ox = predicted.x - cx;
            const float oz = predicted.z - cz;
            const float perp = std::sqrt(ox * ox + oz * oz);
            const float allow = spell_data::r_radius + h->get_bounding_radius();
            if (perp <= allow) ++hits;
        }
        return hits;
    }

    bool try_r_combo()
    {
        if (!menu::combo_use_r || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R)) return false;
        if (!sdk::orbwalker || !sdk::orbwalker->combo()) return false;
        if (menu::r_min_hits <= 0) return false;

        auto* p = get_player();
        if (!p) return false;
        const auto start = p->get_position();

        int   best_hits = 0;
        math::vector3 best_end{};

        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::r_range)) continue;
            if (is_unstoppable(h)) continue;
            if (has_spell_shield(h)) continue;

            auto pred = predict_r(h, menu::r_hitchance);
            if (!pred.is_valid || pred.hitchance < menu::r_hitchance) continue;

            const int hits = count_r_hits(start, pred.cast_position);
            if (hits > best_hits)
            {
                best_hits = hits;
                best_end  = pred.cast_position;
            }
        }

        if (best_hits >= menu::r_min_hits)
        {
            return sdk::common->cast_spell(SLOT_R, best_end);
        }
        return false;
    }

    bool try_semi_r()
    {
        if (!menu::semi_r_active || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R)) return false;

        auto* p = get_player();
        if (!p) return false;

        const auto cursor = g_sdk->hud_manager->get_cursor_position();
        const auto start  = p->get_position();

        game_object* best = nullptr;
        float best_dist = menu::semi_r_radius;
        pred_sdk::pred_data best_pred{};

        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::r_range)) continue;
            const float d = cursor.distance(h->get_position());
            if (d > best_dist) continue;

            auto pred = predict_r(h, menu::r_hitchance);
            if (!pred.is_valid || pred.hitchance < menu::r_hitchance) continue;
            if (has_spell_shield(h)) continue;

            best_dist = d;
            best      = h;
            best_pred = pred;
        }

        if (best)
        {
            return sdk::common->cast_spell(SLOT_R, best_pred.cast_position);
        }
        return false;
    }

    // ----------------------------------------------------------------------
    // EVENTS
    // ----------------------------------------------------------------------
    void __fastcall on_update()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;
        if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

        process_e_queue();

        if (try_semi_r()) return;
        if (try_r_combo()) return;
        if (try_q_gapclose()) return;

        if (sdk::orbwalker && sdk::orbwalker->combo())
        {
            try_q_combo();
        }

        try_w();
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!menu::e_use_on_ally || !obj || !cast) return;
        if (!is_spell_ready(SLOT_E)) return;

        auto* p = get_player();
        if (!p) return;
        if (!obj->is_hero() || obj->get_team_id() != p->get_team_id()) return;
        if (!is_valid_ally(obj, spell_data::e_range)) return;

        const int slot = cast->get_spell_slot();
        if (slot < SLOT_Q || slot > SLOT_R) return;
        if (!e_wants_slot(slot)) return;

        // If the cast has a target, make sure it's a hero (matches Nami source intent).
        if (auto* tgt = cast->get_target(); tgt && !tgt->is_hero())
            return;

        schedule_e(obj->get_network_id());
    }

    void __fastcall on_basic_attack(game_object* obj, game_object* target, spell_cast* /*cast*/)
    {
        if (!menu::e_use_on_ally || !menu::e_on_aa) return;
        if (!obj || !target) return;
        if (!is_spell_ready(SLOT_E)) return;

        auto* p = get_player();
        if (!p) return;
        if (!obj->is_hero() || obj->get_team_id() != p->get_team_id()) return;
        if (!target->is_hero() || target->get_team_id() == p->get_team_id()) return;
        if (!is_valid_ally(obj, spell_data::e_range)) return;

        schedule_e(obj->get_network_id());
    }

    void __fastcall on_draw_world()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;

        const auto pos = p->get_position();
        auto draw_range = [&](float range, uint32_t color, int slot)
        {
            if (range <= 0.f || color == 0u) return;
            if (!is_spell_learned(slot)) return;
            if (menu::draw_only_ready && !is_spell_ready(slot)) return;
            if (sdk::shaders)
                sdk::shaders->draw_world_circle(pos, range, 2.f, color);
            else if (g_sdk->renderer)
                g_sdk->renderer->add_circle_3d(pos, range, 2.f, color);
        };

        if (menu::draw_q_range) draw_range(spell_data::q_range, menu::color_q, SLOT_Q);
        if (menu::draw_w_range) draw_range(spell_data::w_range, menu::color_w, SLOT_W);
        if (menu::draw_e_range) draw_range(spell_data::e_range, menu::color_e, SLOT_E);
        if (menu::draw_r_range) draw_range(spell_data::r_range, menu::color_r, SLOT_R);
    }

    // ----------------------------------------------------------------------
    // MENU
    // ----------------------------------------------------------------------
    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_nami", "MeowAIO - Nami");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Nami: failed to create menu category");
            return;
        }

        // Q
        if (auto* q = s_menu->add_sub_category("q_aqua_prison", "Q - Aqua Prison"))
        {
            q->add_checkbox("combo", "Use Q in Combo", menu::combo_use_q,
                [](bool v) { menu::combo_use_q = v; });
            q->add_checkbox("only_slow", "Only if Target is Slowed", menu::combo_q_only_slow,
                [](bool v) { menu::combo_q_only_slow = v; });
            q->add_checkbox("gapclose", "Anti Gap Closers", menu::combo_q_gapclose,
                [](bool v) { menu::combo_q_gapclose = v; });
            q->add_slider_int("hitchance", "Hitchance %", 0, 100, 5, menu::combo_q_hitchance,
                [](int v) { menu::combo_q_hitchance = v; });
            q->add_separator();
            q->add_checkbox("draw_range", "Draw Q Range", menu::draw_q_range,
                [](bool v) { menu::draw_q_range = v; });
        }

        // W
        if (auto* w = s_menu->add_sub_category("w_ebb_and_flow", "W - Ebb and Flow"))
        {
            w->add_checkbox("combo", "Use W in Combo", menu::combo_use_w,
                [](bool v) { menu::combo_use_w = v; });
            w->add_checkbox("bounce", "Allow Bounce Logic", menu::w_bounce,
                [](bool v) { menu::w_bounce = v; });
            w->add_separator();
            w->add_label("Auto Heal");
            w->add_checkbox("heal", "Enable Heal", menu::heal_use_w,
                [](bool v) { menu::heal_use_w = v; });
            w->add_slider_int("heal_mana", "Only if Mana >= %", 0, 100, 5, menu::heal_mana_pct,
                [](int v) { menu::heal_mana_pct = v; });
            w->add_slider_int("heal_hp", "Only if Ally HP < %", 0, 100, 5, menu::heal_hp_pct,
                [](int v) { menu::heal_hp_pct = v; });
            w->add_separator();
            w->add_checkbox("draw_range", "Draw W Range", menu::draw_w_range,
                [](bool v) { menu::draw_w_range = v; });
        }

        // E
        if (auto* e = s_menu->add_sub_category("e_tidecallers", "E - Tidecaller's Blessing"))
        {
            e->add_checkbox("auto", "Auto-Cast on Allies", menu::e_use_on_ally,
                [](bool v) { menu::e_use_on_ally = v; });
            e->add_checkbox("only_combo", "  - Only While Combo Active", menu::e_only_in_combo,
                [](bool v) { menu::e_only_in_combo = v; });
            e->add_slider_int("humanizer", "Humanizer (ms)", 0, 800, 25, menu::e_humanizer_ms,
                [](int v) { menu::e_humanizer_ms = v; });
            e->add_separator();
            e->add_label("Cast E when ally uses:");
            e->add_checkbox("on_aa", "Basic Attack on hero", menu::e_on_aa,
                [](bool v) { menu::e_on_aa = v; });
            e->add_checkbox("on_q", "Q", menu::e_on_q,
                [](bool v) { menu::e_on_q = v; });
            e->add_checkbox("on_w", "W", menu::e_on_w,
                [](bool v) { menu::e_on_w = v; });
            e->add_checkbox("on_e", "E", menu::e_on_e,
                [](bool v) { menu::e_on_e = v; });
            e->add_checkbox("on_r", "R", menu::e_on_r,
                [](bool v) { menu::e_on_r = v; });
            e->add_separator();
            e->add_checkbox("draw_range", "Draw E Range", menu::draw_e_range,
                [](bool v) { menu::draw_e_range = v; });
        }

        // R
        if (auto* r = s_menu->add_sub_category("r_tidal_wave", "R - Tidal Wave"))
        {
            r->add_checkbox("combo", "Use R in Combo", menu::combo_use_r,
                [](bool v) { menu::combo_use_r = v; });
            r->add_slider_int("min_hits", "Minimum Hits", 0, 5, 1, menu::r_min_hits,
                [](int v) { menu::r_min_hits = v; });
            r->add_slider_int("hitchance", "R Hitchance %", 0, 100, 5, menu::r_hitchance,
                [](int v) { menu::r_hitchance = v; });
            r->add_separator();
            r->add_hotkey("semi_r", "Semi-Manual R", 'T', false, false,
                [](std::string* hk, bool pressed)
                {
                    menu::semi_r_active = pressed;
                    if (hk) menu::semi_r_key = *hk;
                });
            r->add_slider_float("semi_radius", "Semi-R Cursor Radius", 100.f, 1000.f, 25.f,
                menu::semi_r_radius, [](float v) { menu::semi_r_radius = v; });
            r->add_separator();
            r->add_checkbox("draw_range", "Draw R Range", menu::draw_r_range,
                [](bool v) { menu::draw_r_range = v; });
        }

        // Global drawings
        if (auto* d = s_menu->add_sub_category("drawings", "Drawings"))
        {
            d->add_checkbox("only_ready", "Only Draw Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            d->add_separator();
            d->add_label("Range Colors");
            d->add_colorpicker("color_q", "Q Color", menu::color_q,
                [](uint32_t v) { menu::color_q = v; });
            d->add_colorpicker("color_w", "W Color", menu::color_w,
                [](uint32_t v) { menu::color_w = v; });
            d->add_colorpicker("color_e", "E Color", menu::color_e,
                [](uint32_t v) { menu::color_e = v; });
            d->add_colorpicker("color_r", "R Color", menu::color_r,
                [](uint32_t v) { menu::color_r = v; });
        }
    }
} // namespace

void load()
{
    auto* p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->get_char_name() != "Nami")
        return;

    setup_menu();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
    }

    g_sdk->log_console("[MeowAIO] Nami loaded MEOW!");
}

void unload()
{
    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->unregister_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->unregister_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->unregister_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->unregister_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
    }

    s_menu = nullptr;
    s_e_queue.clear();
    for (float& v : s_last_cast_time) v = 0.f;
    menu::semi_r_active = false;

    if (g_sdk) g_sdk->log_console("[-] Nami unloaded");
}

} // namespace nami
