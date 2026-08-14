#include "annie.h"
#include "damage/items.hpp"

#include "evade_sdk.hpp"
#include "infotab_sdk.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>

namespace annie
{
namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    constexpr float Q_RANGE = 625.f;
    constexpr float W_RANGE = 600.f;
    constexpr float W_HALF_ANGLE_DEG = 25.f;
    constexpr float E_RANGE = 800.f;
    constexpr float R_RANGE = 600.f;
    constexpr float R_RADIUS = 250.f;
    constexpr float FLASH_RANGE = 425.f;

    constexpr uint32_t BUFF_PASSIVE_READY = 0x97E259F8u; // anniepassiveprimed, fallback by name below
    constexpr uint32_t BUFF_PASSIVE_STACK = 0x2F9D4DEBu; // anniepassivestack, fallback by name below

    menu_category* s_menu = nullptr;

    namespace menu
    {
        inline bool combo_q = true;
        inline bool combo_w = true;
        inline bool combo_e = false;
        inline bool combo_r = true;
        inline bool combo_r_stun_only = true;
        inline bool combo_r_killable = false;
        inline int  combo_r_min_hits = 2;

        inline bool harass_q = true;
        inline bool harass_w = false;
        inline int  harass_mana_pct = 45;

        inline bool clear_q = true;
        inline int  clear_mana_pct = 35;

        inline bool ks_q = true;
        inline bool ks_w = true;
        inline bool ks_r = true;
        inline bool tibbers_control = true;

        inline bool semi_r_active = false;
        inline std::string semi_r_key;
        inline uint32_t infotab_semi_r = 0;
        inline bool r_flash_active = false;
        inline std::string r_flash_key;
        inline uint32_t infotab_r_flash = 0;

        inline bool draw_q = true;
        inline bool draw_w = true;
        inline bool draw_e = false;
        inline bool draw_r = true;
        inline bool draw_tibbers_timer = true;
        inline bool draw_only_ready = true;
        inline uint32_t color_q = 0x80FFAA40u;
        inline uint32_t color_w = 0x80FF6040u;
        inline uint32_t color_e = 0x8040C8FFu;
        inline uint32_t color_r = 0x80FF3030u;
    }

    int s_flash_slot_cache = -1;
    int s_r_flash_state = 0;
    float s_r_flash_time = 0.f;
    uint32_t s_r_flash_target_id = 0;
    uint32_t s_tibbers_id = 0;
    float s_tibbers_spawn_time = 0.f;
    float s_last_cast_attempt[6] = {};
    float s_last_tibbers_order_time = 0.f;
    uint32_t s_last_tibbers_order_target_id = 0;

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

    bool spell_learned(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0;
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
        auto* s = p->get_spell(slot);
        return s && s->get_level() > 0 && p->get_par() >= p->get_spell_mana_cost(slot);
    }

    int detect_flash_slot()
    {
        if (s_flash_slot_cache >= 0) return s_flash_slot_cache;

        auto* p = player();
        if (!p) return -1;

        for (int slot = 4; slot <= 5; ++slot)
        {
            auto* spell = p->get_spell(slot);
            if (!spell || spell->get_level() <= 0) continue;
            auto* data = spell->get_data();
            if (!data) continue;
            auto* static_data = data->get_static_data();
            if (!static_data) continue;

            const char* name = static_data->get_name();
            if (name && _stricmp(name, "SummonerFlash") == 0)
            {
                s_flash_slot_cache = slot;
                return slot;
            }
        }

        return -1;
    }

    float mana_pct()
    {
        auto* p = player();
        if (!p || p->get_max_par() <= 0.f) return 0.f;
        return (p->get_par() / p->get_max_par()) * 100.f;
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

    bool is_enemy_minion(game_object* obj)
    {
        return obj && obj->is_lane_minion() && is_valid_enemy(obj);
    }

    std::string lowercase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    bool has_buff_name(game_object* obj, const char* fragment)
    {
        if (!obj || !fragment) return false;
        const std::string needle = lowercase(fragment);
        for (auto* buff : obj->get_buffs())
        {
            if (!buff || !buff->is_active()) continue;
            if (lowercase(buff->get_name()).find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    bool stun_ready()
    {
        auto* p = player();
        if (!p) return false;
        if (auto* buff = p->get_buff_by_hash(BUFF_PASSIVE_READY); buff && buff->is_active())
            return true;
        return has_buff_name(p, "anniepassiveprimed");
    }

    int passive_stacks()
    {
        auto* p = player();
        if (!p) return 0;
        if (auto* buff = p->get_buff_by_hash(BUFF_PASSIVE_STACK); buff && buff->is_active())
            return std::max<int>(1, buff->get_stacks());

        for (auto* buff : p->get_buffs())
        {
            if (!buff || !buff->is_active()) continue;
            if (lowercase(buff->get_name()).find("anniepassivestack") != std::string::npos)
                return std::max<int>(1, buff->get_stacks());
        }
        return 0;
    }

    float spell_damage(int slot, game_object* target, bool include_procs = true)
    {
        auto* p = player();
        if (!p || !target || !sdk::damage) return 0.f;
        const bool horizon = p->get_position().distance(target->get_position())
            >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
        float damage = meow::item_damage::apply_magic_damage_modifiers(
            p, target, sdk::damage->get_spell_damage(p, target, slot), 0, horizon);
        if (!include_procs) return damage;

        meow::item_damage::ability_proc_context items{};
        items.horizon_focus_active = horizon;
        meow::item_damage::ability_rune_context runes{};
        runes.cast_distance = p->get_position().distance(target->get_position());
        return damage + meow::item_damage::on_ability_damage(p, target, items)
            + meow::item_damage::on_ability_rune_damage(p, target, runes);
    }

    float effective_hp(game_object* target)
    {
        if (!target) return FLT_MAX;
        return target->get_hp() + target->get_all_shield() + target->get_magical_shield();
    }

    game_object* target(float range)
    {
        if (!sdk::target_selector) return nullptr;
        if (auto* forced = sdk::target_selector->get_forced_target(); is_valid_enemy(forced, range))
            return forced;

        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (is_valid_enemy(hero, range))
                return hero;
        }
        return nullptr;
    }

    bool in_w_cone(game_object* target_obj)
    {
        auto* p = player();
        if (!p || !target_obj) return false;

        const auto start = p->get_position();
        const auto end = start.extended(target_obj->get_position(), W_RANGE, true);
        if (sdk::common && sdk::common->physics)
            return sdk::common->physics->is_in_cone(
                target_obj->get_position(), start, end, W_HALF_ANGLE_DEG, target_obj->get_bounding_radius());

        auto forward = (end - start).normalized();
        auto to_target = (target_obj->get_position() - start).normalized();
        const float dot = std::clamp(forward.dot(to_target), -1.f, 1.f);
        const float angle = std::acos(dot) * 57.2957795f;
        return angle <= W_HALF_ANGLE_DEG;
    }

    int count_enemies_near(const math::vector3& pos, float radius)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0;
        int hits = 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (is_valid_enemy(hero) && hero->get_position().distance(pos) <= radius)
                ++hits;
        }
        return hits;
    }

    game_object* tibbers()
    {
        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return nullptr;

        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (!minion || !minion->is_valid() || minion->is_dead()) continue;
            if (minion->get_team_id() != p->get_team_id()) continue;
            if (minion->get_owner() != p && minion->get_attacher() != p) continue;

            const auto name = lowercase(minion->get_name() + " " + minion->get_char_name());
            if (name.find("tibbers") != std::string::npos || name.find("annietibbers") != std::string::npos)
            {
                const uint32_t id = minion->get_network_id();
                if (s_tibbers_id != id)
                {
                    s_tibbers_id = id;
                    s_tibbers_spawn_time = g_sdk->clock_facade ? g_sdk->clock_facade->get_game_time() : 0.f;
                }
                return minion;
            }
        }

        s_tibbers_id = 0;
        s_tibbers_spawn_time = 0.f;
        return nullptr;
    }

    bool cast_q(game_object* target_obj, bool bypass_anti_buffering = true)
    {
        auto* p = player();
        if (!p || !target_obj || !spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return false;
        if (!sdk::common && !bypass_anti_buffering) return false;
        if (!is_valid_enemy(target_obj, Q_RANGE + target_obj->get_bounding_radius())) return false;
        if (!can_attempt_cast(SLOT_Q)) return false;

        if (bypass_anti_buffering)
        {
            p->cast_spell(SLOT_Q, target_obj);
            mark_cast_attempt(SLOT_Q);
            return true;
        }

        const bool ok = sdk::common->cast_spell(SLOT_Q, target_obj);
        if (ok) mark_cast_attempt(SLOT_Q);
        return ok;
    }

    bool cast_w(game_object* target_obj)
    {
        if (!target_obj || !spell_ready(SLOT_W) || !has_mana(SLOT_W) || !sdk::common) return false;
        if (!is_valid_enemy(target_obj, W_RANGE + target_obj->get_bounding_radius())) return false;
        if (!in_w_cone(target_obj)) return false;
        if (!can_attempt_cast(SLOT_W)) return false;

        const bool ok = sdk::common->cast_spell(SLOT_W, target_obj->get_position());
        if (ok) mark_cast_attempt(SLOT_W);
        return ok;
    }

    bool cast_e_self(bool bypass_anti_buffering = true)
    {
        auto* p = player();
        if (!p || !spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        if (!sdk::common && !bypass_anti_buffering) return false;
        if (!can_attempt_cast(SLOT_E)) return false;

        if (bypass_anti_buffering)
        {
            p->cast_spell(SLOT_E, p);
            mark_cast_attempt(SLOT_E);
            return true;
        }

        const bool ok = sdk::common->cast_spell(SLOT_E, p);
        if (ok) mark_cast_attempt(SLOT_E);
        return ok;
    }

    bool cast_r(game_object* target_obj)
    {
        if (!target_obj || !spell_ready(SLOT_R) || !has_mana(SLOT_R) || !sdk::common) return false;
        if (!is_valid_enemy(target_obj, R_RANGE + target_obj->get_bounding_radius())) return false;
        if (!can_attempt_cast(SLOT_R, 0.25f)) return false;

        const bool ok = sdk::common->cast_spell(SLOT_R, target_obj->get_position());
        if (ok) mark_cast_attempt(SLOT_R);
        return ok;
    }

    bool cast_r_at_best_position(game_object* target_obj)
    {
        if (!target_obj) return false;
        return cast_r(target_obj);
    }

    float combo_damage(game_object* target_obj)
    {
        if (!target_obj) return 0.f;
        float total = 0.f;
        bool procs_added = false;
        if (spell_ready(SLOT_Q)) { total += spell_damage(SLOT_Q, target_obj, !procs_added); procs_added = true; }
        if (spell_ready(SLOT_W)) { total += spell_damage(SLOT_W, target_obj, !procs_added); procs_added = true; }
        if (spell_ready(SLOT_R)) { total += spell_damage(SLOT_R, target_obj, !procs_added); }
        return total;
    }

    void killsteal()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(hero)) continue;

            const float hp = effective_hp(hero);
            if (menu::ks_q && spell_ready(SLOT_Q) && hero->get_position().distance(player()->get_position()) <= Q_RANGE
                && spell_damage(SLOT_Q, hero) >= hp)
            {
                if (cast_q(hero)) return;
            }

            if (menu::ks_w && spell_ready(SLOT_W) && hero->get_position().distance(player()->get_position()) <= W_RANGE
                && spell_damage(SLOT_W, hero) >= hp)
            {
                if (cast_w(hero)) return;
            }

            if (menu::ks_r && spell_ready(SLOT_R) && hero->get_position().distance(player()->get_position()) <= R_RANGE
                && spell_damage(SLOT_R, hero) >= hp)
            {
                if (cast_r(hero)) return;
            }
        }
    }

    void start_r_flash()
    {
        if (s_r_flash_state != 0) return;
        if (!sdk::common || !g_sdk || !g_sdk->clock_facade) return;

        auto* p = player();
        if (!p || p->is_dead()) return;
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return;

        auto* t = target(R_RANGE + FLASH_RANGE + 150.f);
        if (!t) return;

        const float dist = p->get_position().distance(t->get_position());
        if (dist <= R_RANGE + t->get_bounding_radius())
        {
            cast_r_at_best_position(t);
            return;
        }

        const int flash_slot = detect_flash_slot();
        if (flash_slot < 0) return;

        auto* flash_spell = p->get_spell(flash_slot);
        if (!flash_spell || flash_spell->get_cooldown() > 0.f) return;
        if (!can_attempt_cast(flash_slot, 0.35f)) return;

        const auto pp = p->get_position();
        const auto tp = t->get_position();
        const auto delta = tp - pp;
        const float d = delta.magnitude();
        if (d <= 1.f || d > R_RANGE + FLASH_RANGE + t->get_bounding_radius())
            return;

        const auto dir = delta.normalized();
        const float flash_dist = std::clamp(d - R_RANGE + 75.f, 0.f, FLASH_RANGE);
        if (flash_dist <= 0.f) return;

        const math::vector3 flash_pos{
            pp.x + dir.x * flash_dist,
            pp.y,
            pp.z + dir.z * flash_dist
        };

        if (!sdk::common->cast_spell(flash_slot, flash_pos))
            return;

        mark_cast_attempt(flash_slot);
        s_r_flash_state = 1;
        s_r_flash_time = g_sdk->clock_facade->get_game_time();
        s_r_flash_target_id = t->get_network_id();
    }

    void r_flash_tick()
    {
        if (s_r_flash_state == 0) return;
        if (!g_sdk || !g_sdk->clock_facade || !g_sdk->object_manager) return;

        const float now = g_sdk->clock_facade->get_game_time();
        auto* t = s_r_flash_target_id ? g_sdk->object_manager->get_object_by_network_id(s_r_flash_target_id) : nullptr;
        if (!t || !is_valid_enemy(t) || now - s_r_flash_time > 1.0f)
        {
            s_r_flash_state = 0;
            s_r_flash_target_id = 0;
            return;
        }

        if (now - s_r_flash_time < 0.10f)
            return;

        cast_r_at_best_position(t);
        s_r_flash_state = 0;
        s_r_flash_target_id = 0;
    }


    void combo()
    {
        auto* t = target(std::max({ Q_RANGE, W_RANGE, R_RANGE }));
        if (!t) return;

        const bool has_stun = stun_ready();
        if (menu::combo_r && (!menu::combo_r_stun_only || has_stun))
        {
            const bool enough_hits = count_enemies_near(t->get_position(), R_RADIUS) >= menu::combo_r_min_hits;
            const bool killable = combo_damage(t) >= effective_hp(t);
            if ((enough_hits || (menu::combo_r_killable && killable)) && cast_r(t))
                return;
        }

        if (menu::combo_q && cast_q(t)) return;
        if (menu::combo_w && cast_w(t)) return;

        if (menu::combo_e && !has_stun && passive_stacks() >= 3)
            cast_e_self();
    }

    void harass()
    {
        if (mana_pct() < static_cast<float>(menu::harass_mana_pct)) return;
        auto* t = target(std::max(Q_RANGE, W_RANGE));
        if (!t) return;

        if (menu::harass_q && cast_q(t)) return;
        if (menu::harass_w && cast_w(t)) return;
    }

    void clear()
    {
        if (!menu::clear_q || mana_pct() < static_cast<float>(menu::clear_mana_pct)) return;
        if (!g_sdk || !g_sdk->object_manager || !sdk::damage) return;
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return;

        auto* p = player();
        if (!p) return;
        for (auto* minion : g_sdk->object_manager->get_minions())
        {
            if (!is_enemy_minion(minion)) continue;
            if (p->get_position().distance(minion->get_position()) > Q_RANGE) continue;
            if (spell_damage(SLOT_Q, minion) >= minion->get_hp() + minion->get_all_shield())
            {
                if (cast_q(minion)) return;
            }
        }
    }

    void control_tibbers()
    {
        if (!menu::tibbers_control || !sdk::common) return;
        auto* bear = tibbers();
        if (!bear) return;

        auto* t = target(1800.f);
        if (!t) return;

        const float attack_range = bear->get_attack_range() + bear->get_bounding_radius() + t->get_bounding_radius();
        const float now = game_time();
        const uint32_t target_id = t->get_network_id();
        if (now > 0.f && target_id == s_last_tibbers_order_target_id
            && now - s_last_tibbers_order_time < 0.25f)
        {
            return;
        }

        if (bear->get_position().distance(t->get_position()) <= attack_range)
            sdk::common->issue_order(game_object_order::attack_to, t, true);
        else
            sdk::common->issue_order(game_object_order::move_to, t->get_position(), true);

        s_last_tibbers_order_time = now;
        s_last_tibbers_order_target_id = target_id;
    }

    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("annie", "MeowAIO - Annie");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Annie: failed to create menu category");
            return;
        }

        // Q - Disintegrate
        if (auto* q_menu = s_menu->add_sub_category("q_disintegrate", "Q - Disintegrate"))
        {
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            q_menu->add_checkbox("harass", "Use in Harass", menu::harass_q,
                [](bool v) { menu::harass_q = v; });
            q_menu->add_checkbox("clear", "Use in Clear", menu::clear_q,
                [](bool v) { menu::clear_q = v; });
            q_menu->add_checkbox("ks", "Killsteal", menu::ks_q,
                [](bool v) { menu::ks_q = v; });
            q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });
        }

        // W - Incinerate
        if (auto* w_menu = s_menu->add_sub_category("w_incinerate", "W - Incinerate"))
        {
            w_menu->add_checkbox("combo", "Use in Combo", menu::combo_w,
                [](bool v) { menu::combo_w = v; });
            w_menu->add_checkbox("harass", "Use in Harass", menu::harass_w,
                [](bool v) { menu::harass_w = v; });
            w_menu->add_checkbox("ks", "Killsteal", menu::ks_w,
                [](bool v) { menu::ks_w = v; });
            w_menu->add_checkbox("draw_range", "Draw Range", menu::draw_w,
                [](bool v) { menu::draw_w = v; });
        }

        // E - Molten Shield
        if (auto* e_menu = s_menu->add_sub_category("e_molten_shield", "E - Molten Shield"))
        {
            e_menu->add_checkbox("combo", "Use in Combo to Prep Stun", menu::combo_e,
                [](bool v) { menu::combo_e = v; });
            e_menu->add_separator();
            e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e,
                [](bool v) { menu::draw_e = v; });
        }

        // R - Summon: Tibbers
        if (auto* r_menu = s_menu->add_sub_category("r_tibbers", "R - Summon: Tibbers"))
        {
            r_menu->add_checkbox("combo", "Use in Combo", menu::combo_r,
                [](bool v) { menu::combo_r = v; });
            r_menu->add_checkbox("stun_only", "R Only When Stun", menu::combo_r_stun_only,
                [](bool v) { menu::combo_r_stun_only = v; });
            r_menu->add_checkbox("killable", "Only if Killable", menu::combo_r_killable,
                [](bool v) { menu::combo_r_killable = v; });
            r_menu->add_slider_int("min_hits", "R Min Hits", 1, 5, 1, menu::combo_r_min_hits,
                [](int v) { menu::combo_r_min_hits = v; });
            r_menu->add_checkbox("ks", "Killsteal", menu::ks_r,
                [](bool v) { menu::ks_r = v; });
            r_menu->add_checkbox("control_tibbers", "Auto Move Tibbers", menu::tibbers_control,
                [](bool v) { menu::tibbers_control = v; });
            r_menu->add_separator();
            r_menu->add_hotkey("semi_r", "Semi-Manual R", 'T', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_r_active = pressed;
                    if (key) menu::semi_r_key = *key;
                });
            r_menu->add_hotkey("r_flash", "R + Flash", 'Y', false, false,
                [](std::string* key, bool pressed) {
                    menu::r_flash_active = pressed;
                    if (key) menu::r_flash_key = *key;
                });
            r_menu->add_separator();
            r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
            if (menu::semi_r_key.empty()) menu::semi_r_key = "T";
            if (menu::r_flash_key.empty()) menu::r_flash_key = "Y";
        }

        // Mana
        if (auto* mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds"))
        {
            mana_menu->add_slider_int("harass", "Harass Mana %", 0, 100, 5, menu::harass_mana_pct,
                [](int v) { menu::harass_mana_pct = v; });
            mana_menu->add_slider_int("clear", "Clear Mana %", 0, 100, 5, menu::clear_mana_pct,
                [](int v) { menu::clear_mana_pct = v; });
        }

        // Drawings
        if (auto* draw_menu = s_menu->add_sub_category("drawings", "Drawings"))
        {
            draw_menu->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_menu->add_checkbox("tibbers_timer", "Tibbers Timer", menu::draw_tibbers_timer,
                [](bool v) { menu::draw_tibbers_timer = v; });
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

    void setup_infotab()
    {
        if (!sdk::infotab || menu::semi_r_key.empty()) return;
        if (!menu::semi_r_key.empty())
        {
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key, { "Semi R" }, []() -> infotab_sdk::text_entry {
                    return { menu::semi_r_active ? "ON" : "OFF",
                        menu::semi_r_active ? 0xFF00FF00u : 0xFFFF0000u };
                });
        }
        if (!menu::r_flash_key.empty())
        {
            menu::infotab_r_flash = sdk::infotab->add_hotkey_text(
                &menu::r_flash_key, { "R Flash" }, []() -> infotab_sdk::text_entry {
                    return { menu::r_flash_active ? "ON" : "OFF",
                        menu::r_flash_active ? 0xFF00FF00u : 0xFFFF0000u };
                });
        }
    }
}

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Annie")
        return;

    setup_menu();
    setup_infotab();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
    }

    g_sdk->log_console("[MeowAIO] Annie loaded MEOW!");
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

    if (sdk::infotab && menu::infotab_semi_r)
    {
        sdk::infotab->remove_text(menu::infotab_semi_r);
        menu::infotab_semi_r = 0;
    }
    if (sdk::infotab && menu::infotab_r_flash)
    {
        sdk::infotab->remove_text(menu::infotab_r_flash);
        menu::infotab_r_flash = 0;
    }

    s_menu = nullptr;
    menu::semi_r_active = false;
    menu::semi_r_key.clear();
    menu::r_flash_active = false;
    menu::r_flash_key.clear();
    s_flash_slot_cache = -1;
    s_r_flash_state = 0;
    s_r_flash_target_id = 0;
    s_tibbers_id = 0;
    s_tibbers_spawn_time = 0.f;
    s_last_tibbers_order_time = 0.f;
    s_last_tibbers_order_target_id = 0;
    for (float& last_attempt : s_last_cast_attempt)
        last_attempt = 0.f;
    g_sdk->log_console("[MeowAIO] Annie unloaded successfully!");
}

void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = player();
    if (!p || p->is_dead()) return;

    killsteal();
    control_tibbers();
    r_flash_tick();

    if (menu::r_flash_active)
    {
        start_r_flash();
        return;
    }

    if (menu::semi_r_active)
    {
        if (auto* t = target(R_RANGE); cast_r(t)) return;
    }

    if (!sdk::orbwalker) return;
    if (sdk::orbwalker->combo()) combo();
    else if (sdk::orbwalker->harass()) harass();
    else if (sdk::orbwalker->clear() || sdk::orbwalker->lasthit()) clear();
}

void __fastcall on_draw()
{
    if (!g_sdk || !g_sdk->renderer) return;
    auto* p = player();
    if (!p || p->is_dead()) return;

    const auto screen = g_sdk->renderer->world_to_screen(p->get_position());
    if (screen.x > 1.f || screen.y > 1.f)
    {
        const bool ready = stun_ready();
        const int stacks = passive_stacks();
        const uint32_t color = ready ? 0xFFFF4040u : 0xFFFFD060u;
        char text[64]{};
        sprintf_s(text, "Stun: %s (%d)", ready ? "READY" : "stacking", stacks);
        g_sdk->renderer->add_text(text, 15.f, { screen.x, screen.y + 58.f }, 1, color);
    }
}

void __fastcall on_draw_world()
{
    auto* p = player();
    if (!p || p->is_dead()) return;

    auto draw = [&](float radius, uint32_t color)
    {
        if (!color) return;
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(p->get_position(), radius, 2.f, color);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(p->get_position(), radius, 1.f, color);
    };

    if (menu::draw_q && (!menu::draw_only_ready || spell_ready(SLOT_Q))) draw(Q_RANGE, menu::color_q);
    if (menu::draw_w && (!menu::draw_only_ready || spell_ready(SLOT_W))) draw(W_RANGE, menu::color_w);
    if (menu::draw_e && (!menu::draw_only_ready || spell_ready(SLOT_E))) draw(E_RANGE, menu::color_e);
    if (menu::draw_r && (!menu::draw_only_ready || spell_ready(SLOT_R))) draw(R_RANGE, menu::color_r);

    if (auto* bear = tibbers())
    {
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(bear->get_position(), bear->get_bounding_radius() * 1.5f, 2.f, 0x80FF3030u);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(bear->get_position(), bear->get_bounding_radius() * 1.5f, 1.f, 0x80FF3030u);

        if (menu::draw_tibbers_timer && g_sdk && g_sdk->renderer)
        {
            const float max_lifetime = bear->get_max_lifetime();
            const float lifetime = bear->get_lifetime();
            float remaining = 0.f;
            if (max_lifetime > 0.f && lifetime > 0.f)
            {
                remaining = std::max(0.f, max_lifetime - lifetime);
            }
            else if (g_sdk->clock_facade && s_tibbers_spawn_time > 0.f)
            {
                remaining = std::max(0.f, 45.f - (g_sdk->clock_facade->get_game_time() - s_tibbers_spawn_time));
            }
            else
            {
                remaining = 45.f;
            }

            const auto screen = g_sdk->renderer->world_to_screen(bear->get_position());
            if (screen.x > 1.f || screen.y > 1.f)
            {
                char text[48]{};
                sprintf_s(text, "Tibbers: %.1fs", remaining);
                g_sdk->renderer->add_text(text, 14.f, { screen.x, screen.y - 42.f }, 1, 0xFFFF6040u);
            }
        }
    }
}
} // namespace annie
