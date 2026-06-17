#include "soraka.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "hp_sdk.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <unordered_map>

namespace soraka
{
namespace
{
    menu_category* s_menu = nullptr;

    // Per-ally W whitelist (network id -> live menu state).
    struct w_ally_state
    {
        bool enabled = true;
        int  min_hp  = 60;
    };
    std::unordered_map<uint32_t, w_ally_state> s_w_ally;
    std::unordered_map<uint32_t, bool>         s_r_ally; // network id -> enabled

    float s_last_cast_time[4] = { 0.f, 0.f, 0.f, 0.f };

    // Reference-counted because both SorakaQMissile and SorakaQReturnMissile
    // can be alive simultaneously — a single bool would flip false too early.
    int  s_q_in_flight_count = 0;
    bool s_w_toggle_live     = true; // current effective toggle (hotkey state)

    inline bool q_in_flight() { return s_q_in_flight_count > 0; }

    bool is_soraka_q_missile(const std::string& name)
    {
        return name == "SorakaQMissile" || name == "SorakaQReturnMissile";
    }

    // ------------------------------------------------------------------
    // BASIC HELPERS
    // ------------------------------------------------------------------
    float now_time()
    {
        return (g_sdk && g_sdk->clock_facade) ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    game_object* get_player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    int get_spell_level(int slot)
    {
        auto* p = get_player();
        if (!p) return 0;
        auto* sp = p->get_spell(slot);
        return sp ? sp->get_level() : 0;
    }

    bool is_spell_learned(int slot) { return get_spell_level(slot) > 0; }

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
        if (range != FLT_MAX
            && p->get_position().distance(unit->get_position()) > range)
            return false;
        return true;
    }

    bool is_valid_ally(game_object* unit, float range = FLT_MAX, bool include_self = true)
    {
        auto* p = get_player();
        if (!p || !unit_alive(unit)) return false;
        if (!include_self && unit->get_network_id() == p->get_network_id()) return false;
        if (unit->get_team_id() != p->get_team_id()) return false;
        if (range != FLT_MAX
            && p->get_position().distance(unit->get_position()) > range)
            return false;
        return true;
    }

    bool player_has_buff(const char* name)
    {
        auto* p = get_player();
        if (!p || !name) return false;
        std::string key = name;
        auto* b = p->get_buff_by_name(key);
        return b && b->is_active();
    }

    bool is_unstoppable(game_object* t)
    {
        if (!t) return false;
        return t->has_buff_of_type(buff_type::spell_immunity)
            || t->has_buff_of_type(buff_type::invulnerability);
    }

    // ------------------------------------------------------------------
    // MENU HELPERS — per-ally settings
    // ------------------------------------------------------------------
    bool w_ally_enabled(game_object* a)
    {
        if (!a) return false;
        auto it = s_w_ally.find(a->get_network_id());
        return it != s_w_ally.end() && it->second.enabled;
    }

    int w_ally_min_hp(game_object* a)
    {
        if (!a) return 0;
        auto it = s_w_ally.find(a->get_network_id());
        return it == s_w_ally.end() ? 0 : it->second.min_hp;
    }

    bool r_ally_enabled(game_object* a)
    {
        if (!a) return false;
        auto it = s_r_ally.find(a->get_network_id());
        return it != s_r_ally.end() && it->second;
    }

    // ------------------------------------------------------------------
    // PREDICTION HELPERS
    // ------------------------------------------------------------------
    pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.targetting_type    = pred_sdk::targetting_type::center;
        sd.spell_slot         = SLOT_Q;
        sd.range              = spell_data::q_range;
        sd.radius             = spell_data::q_radius;
        sd.delay              = spell_data::q_delay;
        sd.projectile_speed   = spell_data::q_speed;
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

    pred_sdk::pred_data predict_e(game_object* target, int hitchance, float delay)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.targetting_type    = pred_sdk::targetting_type::center;
        sd.spell_slot         = SLOT_E;
        sd.range              = spell_data::e_range;
        sd.radius             = spell_data::e_radius;
        sd.delay              = delay;
        sd.projectile_speed   = spell_data::e_speed;
        sd.expected_hitchance = hitchance;

        pred_sdk::pred_data pred{};
        try { pred = sdk::prediction->predict(target, sd); }
        catch (...) { pred.is_valid = false; }
        return pred;
    }

    // ------------------------------------------------------------------
    // CANCEL HELPER (Equinox interrupt)
    // ------------------------------------------------------------------
    // The `cant_cancel_while_(channeling|winding_up)` flags describe whether
    // the *caster* can self-cancel by issuing other commands — they do NOT
    // prevent external CC like Soraka E's silence, so we ignore them here.
    bool enemy_is_cancelable(game_object* enemy)
    {
        if (!enemy) return false;
        auto* ac = enemy->get_active_spell_cast();
        if (!ac) return false;

        const float t = now_time();
        const bool channeling = ac->get_cast_channeling_end_time() > t + 0.05f;
        const bool casting    = ac->get_cast_end_time()           > t + 0.05f;
        if (!channeling && !casting) return false;

        auto* sc = ac->get_spell_cast();
        if (!sc) return false;
        auto* sd = sc->get_spell_data();
        if (!sd) return false;
        auto* st = sd->get_static_data();
        if (!st) return false;

        // Only worth firing E on long animations (ult channels, recall, etc).
        const float channel_dur = st->get_channel_duration() ? *st->get_channel_duration() : 0.f;
        const float cast_time   = st->get_cast_time();

        if (channeling) return channel_dur >= 0.6f;
        if (casting)    return cast_time   >= 0.6f;
        return false;
    }

    bool try_cancel_e()
    {
        if (!menu::e_auto_cancel) return false;
        if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] <= 0.25f) return false;

        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::e_range + 200.f)) continue;
            if (!enemy_is_cancelable(h)) continue;

            auto pred = predict_e(h, pred_sdk::hitchance::medium, spell_data::e_delay);
            if (!pred.is_valid || pred.hitchance < pred_sdk::hitchance::medium) continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_E, pred.cast_position))
            {
                s_last_cast_time[SLOT_E] = t;
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------
    // SNARE HELPER (guaranteed snare via long re-prediction)
    // ------------------------------------------------------------------
    bool try_e_snare()
    {
        if (!menu::e_auto_snare) return false;
        if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] <= 0.25f) return false;

        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::e_range)) continue;
            if (is_unstoppable(h)) continue;

            // Use a much longer effective delay so only stationary / hard-CC'd
            // targets pass the high hitchance bar.
            auto pred = predict_e(h, pred_sdk::hitchance::very_high, spell_data::e_snare_delay);
            if (!pred.is_valid || pred.hitchance < pred_sdk::hitchance::very_high) continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_E, pred.cast_position))
            {
                s_last_cast_time[SLOT_E] = t;
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------
    // ANTI GAP-CLOSE
    // ------------------------------------------------------------------
    bool try_e_gapclose()
    {
        if (!menu::e_auto_gap) return false;
        if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E)) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] <= 0.25f) return false;

        auto* p = get_player();
        if (!p) return false;
        const auto self_pos = p->get_position();

        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(h, spell_data::e_range + 250.f)) continue;
            if (!h->is_dashing()) continue;

            const auto enemy_pos = h->get_position();
            const auto vel       = h->get_velocity();
            const float dx       = self_pos.x - enemy_pos.x;
            const float dz       = self_pos.z - enemy_pos.z;
            const float dist     = std::sqrt(dx * dx + dz * dz);
            if (dist < 1.f) continue;
            const float approach = (vel.x * dx + vel.z * dz) / dist;
            if (approach < 400.f) continue;

            auto pred = predict_e(h, pred_sdk::hitchance::medium, spell_data::e_delay);
            if (!pred.is_valid) continue;
            if (pred.hitchance < pred_sdk::hitchance::medium
                && pred.state != pred_sdk::prediction_state::dashing_or_casting)
                continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_E, pred.cast_position))
            {
                s_last_cast_time[SLOT_E] = t;
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------
    // POKE / COMBO — Q and E (poke)
    // ------------------------------------------------------------------
    bool try_q_poke()
    {
        if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q)) return false;
        if (mana_pct() < (float)menu::q_min_mana) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_Q] <= 0.3f) return false;

        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        const bool combo  = sdk::orbwalker && sdk::orbwalker->combo();
        const bool harass = sdk::orbwalker && sdk::orbwalker->harass();
        if (!((menu::combo_q && combo) || (menu::harass_q && harass))) return false;

        if (!sdk::target_selector) return false;
        for (auto* target : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(target, spell_data::q_range)) continue;

            auto pred = predict_q(target, menu::q_hitchance);
            if (!pred.is_valid || pred.hitchance < menu::q_hitchance) continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_Q, pred.cast_position))
            {
                s_last_cast_time[SLOT_Q] = t;
                return true;
            }
        }
        return false;
    }

    bool try_e_poke()
    {
        if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E)) return false;
        if (mana_pct() < (float)menu::e_min_mana) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_E] <= 0.25f) return false;

        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        const bool combo  = sdk::orbwalker && sdk::orbwalker->combo();
        const bool harass = sdk::orbwalker && sdk::orbwalker->harass();
        if (!((menu::combo_e && combo) || (menu::harass_e && harass))) return false;

        if (!sdk::target_selector) return false;
        for (auto* target : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(target, spell_data::e_range)) continue;

            auto pred = predict_e(target, pred_sdk::hitchance::high, spell_data::e_delay);
            if (!pred.is_valid || pred.hitchance < pred_sdk::hitchance::high) continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_E, pred.cast_position))
            {
                s_last_cast_time[SLOT_E] = t;
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------------------------
    // W (Astral Infusion) — heal whitelist
    // ------------------------------------------------------------------
    bool w_global_allowed()
    {
        if (!s_w_toggle_live) return false;
        if (!is_spell_ready(SLOT_W) || !has_mana_for(SLOT_W)) return false;
        if (mana_pct() < (float)menu::w_min_mana) return false;
        return true;
    }

    bool try_w_heal()
    {
        if (!w_global_allowed()) return false;

        auto* p = get_player();
        if (!p) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_W] <= 0.25f) return false;

        const bool only_combo = menu::w_only_combo;
        const bool in_combo   = sdk::orbwalker && sdk::orbwalker->combo();

        // Pass the passive proc onto an ally if we already have starcall buff.
        const bool has_passive = menu::w_passive && player_has_buff("sorakaqregen");
        if (!has_passive && only_combo && !in_combo) return false;

        // While Q is in flight, hold W for very-low-HP allies only (mirror of MidAirQ check).
        const float ally_hp_cap = q_in_flight() ? 33.f : FLT_MAX;

        game_object* best = nullptr;
        float worst_hp = FLT_MAX;

        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            // Soraka cannot W herself, so exclude self at the source.
            if (!is_valid_ally(a, spell_data::w_range, /*include_self*/ false)) continue;
            if (!w_ally_enabled(a)) continue;

            const float pct = hp_pct(a);
            if (pct > (float)w_ally_min_hp(a)) continue;
            if (pct > ally_hp_cap) continue;

            if (pct < worst_hp) { worst_hp = pct; best = a; }
        }

        if (best && sdk::common && sdk::common->cast_spell(SLOT_W, best))
        {
            s_last_cast_time[SLOT_W] = t;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------
    // R (Wish) — emergency global heal
    // ------------------------------------------------------------------
    bool try_r()
    {
        if (!menu::r_toggle) return false;
        if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R)) return false;

        auto* p = get_player();
        if (!p) return false;

        const float t = now_time();
        if (t - s_last_cast_time[SLOT_R] <= 0.5f) return false;

        int saving = 0;

        for (auto* a : g_sdk->object_manager->get_heroes())
        {
            // Wish does not heal Soraka herself.
            if (!a || !a->is_valid() || a->is_dead()) continue;
            if (a->get_team_id() != p->get_team_id()) continue;
            if (a->get_network_id() == p->get_network_id()) continue;
            if (!r_ally_enabled(a)) continue;

            float predicted_hp = a->get_hp();
            if (sdk::health_prediction)
            {
                try { predicted_hp = sdk::health_prediction->get_predicted_health(a, 2.f); }
                catch (...) { predicted_hp = a->get_hp(); }
            }
            const float incoming = a->get_hp() - predicted_hp;
            const bool  dying    = predicted_hp <= a->get_max_hp() * 0.10f && incoming > 1.f;
            if (!dying) continue;

            // Require at least one enemy nearby — avoids R for damage-over-time
            // ticks (Ignite/poison) where R won't actually save them.
            bool enemy_near = false;
            for (auto* e : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(e)) continue;
                if (e->get_position().distance(a->get_position()) <= 1500.f) { enemy_near = true; break; }
            }
            if (!enemy_near) continue;

            ++saving;
        }

        if (saving < menu::r_min_allies) return false;

        if (sdk::common && sdk::common->cast_spell(SLOT_R))
        {
            s_last_cast_time[SLOT_R] = t;
            return true;
        }
        return false;
    }

    // ------------------------------------------------------------------
    // EVENTS
    // ------------------------------------------------------------------
    void __fastcall on_update()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;
        if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

        // Order mirrors original CancelHelper -> HealHandler -> SnareHandler -> PokeHandler.
        if (try_cancel_e())   return;
        if (try_w_heal())     return;
        if (try_r())          return;
        if (try_e_snare())    return;
        if (try_e_gapclose()) return;
        if (try_e_poke())     return;
        try_q_poke();
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

        if (menu::draw_q) draw_range(spell_data::q_range, menu::color_q, SLOT_Q);
        if (menu::draw_w && s_w_toggle_live) draw_range(spell_data::w_range, menu::color_w, SLOT_W);
        if (menu::draw_e) draw_range(spell_data::e_range, menu::color_e, SLOT_E);

        // Highlight whitelisted allies that are currently healable with W.
        if (s_w_toggle_live && is_spell_ready(SLOT_W))
        {
            for (auto* a : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_ally(a, spell_data::w_range)) continue;
                if (a->get_network_id() == p->get_network_id()) continue;
                if (!w_ally_enabled(a)) continue;
                if (hp_pct(a) > (float)w_ally_min_hp(a)) continue;

                g_sdk->renderer->add_circle_3d(a->get_position(), 80.f, 2.5f,
                    menu::color_w_heal_outer);
            }
        }
    }

    void __fastcall on_present()
    {
        auto* p = get_player();
        if (!p || p->is_dead()) return;
        if (!g_sdk->renderer) return;

        const auto screen = g_sdk->renderer->world_to_screen(p->get_position());
        if (screen.x <= 0.f && screen.y <= 0.f) return;

        const std::string text  = s_w_toggle_live ? "[W] is Enabled" : "[W] is Disabled";
        const uint32_t    color = s_w_toggle_live ? 0xFF2AAA8Au : 0xFF818589u;

        constexpr float font_size = 14.f;
        // Rough monospace width estimate (good enough for centering this label).
        const float approx_width = static_cast<float>(text.size()) * (font_size * 0.55f);

        math::vector2 pos{
            screen.x - approx_width * 0.5f,
            screen.y + 28.f
        };
        g_sdk->renderer->add_text(text, font_size, pos, 0u, color);
    }

    void __fastcall on_create_missile(game_object* missile)
    {
        if (!missile) return;
        auto* p = get_player();
        if (!p) return;

        auto* sc = missile->get_missile_spell_cast();
        if (!sc) return;
        auto* src = sc->get_source();
        if (!src || src->get_network_id() != p->get_network_id()) return;

        if (is_soraka_q_missile(missile->get_name()))
            ++s_q_in_flight_count;
    }

    void __fastcall on_delete_object(game_object* obj)
    {
        // delete_missile event doesn't exist — game_object deletion fires here
        // and we filter to missiles cheaply before string-comparing the name.
        if (!obj || !obj->is_missile()) return;
        if (is_soraka_q_missile(obj->get_name()) && s_q_in_flight_count > 0)
            --s_q_in_flight_count;
    }

    // ------------------------------------------------------------------
    // MENU
    // ------------------------------------------------------------------
    void build_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_soraka", "MeowAIO - Soraka");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Soraka: failed to create menu category");
            return;
        }

        // Q
        if (auto* q = s_menu->add_sub_category("q_starcall", "Q - Starcall"))
        {
            q->add_checkbox("combo", "Use in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            q->add_checkbox("harass", "Use in Harass", menu::harass_q,
                [](bool v) { menu::harass_q = v; });
            q->add_slider_int("min_mana", "Only if Mana >= %", 0, 100, 5, menu::q_min_mana,
                [](int v) { menu::q_min_mana = v; });
            q->add_slider_int("hitchance", "Hitchance %", 0, 100, 5, menu::q_hitchance,
                [](int v) { menu::q_hitchance = v; });
            q->add_separator();
            q->add_checkbox("draw_range", "Draw Q Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });
        }

        // W
        if (auto* w = s_menu->add_sub_category("w_astral_infusion", "W - Astral Infusion"))
        {
            w->add_hotkey("w_toggle_key", "Toggle W Usage", VK_F1, menu::w_toggle, true,
                [](std::string*, bool v) { s_w_toggle_live = v; });
            w->add_separator();
            w->add_checkbox("only_combo", "Only Use in Combo", menu::w_only_combo,
                [](bool v) { menu::w_only_combo = v; });
            w->add_checkbox("passive", "Auto-Pass Passive when Applicable", menu::w_passive,
                [](bool v) { menu::w_passive = v; });
            w->add_slider_int("min_mana", "Minimum Mana %", 0, 100, 1, menu::w_min_mana,
                [](int v) { menu::w_min_mana = v; });
            w->add_separator();
            w->add_checkbox("draw_range", "Draw W Range", menu::draw_w,
                [](bool v) { menu::draw_w = v; });

            if (auto* wl = w->add_sub_category("whitelist", "Whitelist"))
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
                        const std::string key   = "ally_" + std::to_string(id);
                        const std::string title = a->get_char_name();
                        auto* sub = wl->add_sub_category(key, title);
                        if (!sub) continue;

                        constexpr int default_hp = 60;
                        s_w_ally[id] = w_ally_state{ true, default_hp };

                        sub->add_checkbox("toggle", "Enabled", true,
                            [id](bool v) { s_w_ally[id].enabled = v; });
                        sub->add_slider_int("min_hp", "If HP < %", 0, 100, 5, default_hp,
                            [id](int v) { s_w_ally[id].min_hp = v; });
                    }
                }
            }
        }

        // E
        if (auto* e = s_menu->add_sub_category("e_equinox", "E - Equinox"))
        {
            e->add_checkbox("combo", "Use in Combo", menu::combo_e,
                [](bool v) { menu::combo_e = v; });
            e->add_checkbox("harass", "Use in Harass", menu::harass_e,
                [](bool v) { menu::harass_e = v; });
            e->add_slider_int("min_mana", "Only if Mana >= %", 0, 100, 5, menu::e_min_mana,
                [](int v) { menu::e_min_mana = v; });
            e->add_separator();
            e->add_label("Auto Usages");
            e->add_checkbox("auto_cancel", "Cancel Channels", menu::e_auto_cancel,
                [](bool v) { menu::e_auto_cancel = v; });
            e->add_checkbox("auto_snare",  "Guaranteed Snare", menu::e_auto_snare,
                [](bool v) { menu::e_auto_snare = v; });
            e->add_checkbox("auto_gap",    "Anti Gap Close",   menu::e_auto_gap,
                [](bool v) { menu::e_auto_gap = v; });
            e->add_separator();
            e->add_checkbox("draw_range", "Draw E Range", menu::draw_e,
                [](bool v) { menu::draw_e = v; });
        }

        // R
        if (auto* r = s_menu->add_sub_category("r_wish", "R - Wish"))
        {
            r->add_checkbox("toggle", "Auto-Cast R to Save Allies", menu::r_toggle,
                [](bool v) { menu::r_toggle = v; });
            r->add_slider_int("min_allies", "Minimum Allies to Save", 1, 5, 1, menu::r_min_allies,
                [](int v) { menu::r_min_allies = v; });
            r->add_separator();

            if (auto* wl = r->add_sub_category("whitelist", "Whitelist"))
            {
                auto* p = get_player();
                if (p && g_sdk->object_manager)
                {
                    for (auto* a : g_sdk->object_manager->get_heroes())
                    {
                        if (!a) continue;
                        if (a->get_team_id() != p->get_team_id()) continue;
                        // Wish does not heal Soraka herself.
                        if (a->get_network_id() == p->get_network_id()) continue;

                        const uint32_t    id    = a->get_network_id();
                        const std::string key   = "ally_" + std::to_string(id);
                        const std::string title = a->get_char_name();

                        s_r_ally[id] = true;
                        wl->add_checkbox(key, title, true,
                            [id](bool v) { s_r_ally[id] = v; });
                    }
                }
            }
        }

        // Drawings
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
        }
    }
} // namespace

void load()
{
    auto* p = g_sdk ? g_sdk->object_manager->get_local_player() : nullptr;
    if (!p || p->get_char_name() != "Soraka")
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
        g_sdk->event_manager->register_callback(event_manager::event::create_missile,
            reinterpret_cast<void*>(on_create_missile));
        g_sdk->event_manager->register_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
    }

    g_sdk->log_console("[MeowAIO] Soraka loaded MEOW!");
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
        g_sdk->event_manager->unregister_callback(event_manager::event::create_missile,
            reinterpret_cast<void*>(on_create_missile));
        g_sdk->event_manager->unregister_callback(event_manager::event::delete_object,
            reinterpret_cast<void*>(on_delete_object));
    }

    s_menu = nullptr;
    s_w_ally.clear();
    s_r_ally.clear();
    s_q_in_flight_count = 0;
    for (float& v : s_last_cast_time) v = 0.f;

    if (g_sdk) g_sdk->log_console("[-] Soraka unloaded");
}

} // namespace soraka
