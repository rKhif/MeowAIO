#include "vex.h"

#include "db/champions/vex.hpp"
#include "evade_sdk.hpp"
#include "infotab_sdk.hpp"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <string>
#include <vector>

namespace vex
{
namespace data = venplus::db::champions::vex_const;

namespace menu
{
    inline bool combo_use_q       = true;
    inline bool combo_use_w       = true;
    inline bool combo_use_e       = true;
    inline bool combo_use_r       = true;
    inline bool combo_eq          = true;
    inline bool combo_aa_weave    = true;
    inline bool combo_hold_doom   = true;
    inline int  combo_q_hitchance = pred_sdk::hitchance::high;
    inline int  combo_e_hitchance = pred_sdk::hitchance::high;
    inline int  combo_r_hitchance = pred_sdk::hitchance::high;
    inline bool r_killable_only   = true;
    inline bool r_auto_recast     = true;
    inline bool r2_killable_only  = true;
    inline bool         semi_q_active  = false;
    inline bool         semi_e_active  = false;
    inline bool         semi_r_active  = false;
    inline std::string  semi_q_key{};
    inline std::string  semi_e_key{};
    inline std::string  semi_r_key{};
    inline uint32_t     infotab_semi_q = 0;
    inline uint32_t     infotab_semi_e = 0;
    inline uint32_t     infotab_semi_r = 0;

    inline bool  harass_use_q       = true;
    inline bool  harass_use_e       = true;
    inline int   harass_q_hitchance = pred_sdk::hitchance::high;
    inline float harass_mana_pct    = 0.40f;

    inline bool  clear_use_q    = true;
    inline bool  clear_use_w    = true;
    inline bool  clear_use_e    = false;
    inline float clear_mana_pct = 0.30f;

    inline bool flee_use_w = true;
    inline bool flee_use_e = true;

    inline bool anti_dash_w   = true;
    inline int  anti_dash_mode = 0; // 0 = Always, 1 = Combo Only

    inline bool     draw_q_range = true;
    inline bool     draw_e_range = true;
    inline bool     draw_r_range = false;
    inline bool     draw_damage  = true;
    inline bool     draw_only_ready = true;
    inline uint32_t color_q      = 0x80FFFFFF;
    inline uint32_t color_e      = 0x8000FF00;
    inline uint32_t color_r      = 0x80FF4400;
    inline bool     draw_r_rgb   = false;
    inline uint32_t color_r_dmg  = 0xFFFF55AAu;
} // namespace menu

namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    menu_category* s_menu = nullptr;

    pred_sdk::spell_data s_q_close_pred{};
    pred_sdk::spell_data s_q_far_pred{};
    pred_sdk::spell_data s_e_pred{};
    pred_sdk::spell_data s_r_pred{};

    game_object* get_player()
    {
        if (!g_sdk || !g_sdk->object_manager) return nullptr;
        return g_sdk->object_manager->get_local_player();
    }

    bool is_spell_learned(int slot)
    {
        auto* player = get_player();
        if (!player) return false;
        auto* entry = player->get_spell(slot);
        return entry && entry->get_level() > 0;
    }

    bool is_spell_ready(int slot)
    {
        auto* player = get_player();
        if (!player) return false;
        auto* entry = player->get_spell(slot);
        return entry && entry->get_level() > 0 && entry->get_cooldown() <= 0.f;
    }

    bool has_mana_for(int slot)
    {
        auto* player = get_player();
        if (!player) return false;
        auto* entry = player->get_spell(slot);
        if (!entry || entry->get_level() <= 0) return false;
        return player->get_par() >= player->get_spell_mana_cost(slot);
    }

    float mana_percent()
    {
        auto* player = get_player();
        if (!player) return 0.f;
        const float max_mp = player->get_max_par();
        if (max_mp <= 0.f) return 0.f;
        return player->get_par() / max_mp;
    }

    bool is_valid_target(game_object* target, float range = FLT_MAX)
    {
        if (!target || !target->is_valid() || target->is_dead()) return false;
        if (!target->is_targetable() || !target->is_visible()) return false;
        auto* player = get_player();
        if (!player) return false;
        if (target->get_team_id() == player->get_team_id()) return false;
        if (range < FLT_MAX)
        {
            const auto pos = player->get_position();
            const auto tpos = target->get_position();
            if (pos.distance(tpos) > range) return false;
        }
        return true;
    }

    bool is_enemy_minion(game_object* obj)
    {
        if (!obj || !obj->is_valid() || obj->is_dead()) return false;
        if (!obj->is_targetable() || !obj->is_visible()) return false;
        if (!obj->is_lane_minion()) return false;
        auto* player = get_player();
        if (!player) return false;
        return obj->get_team_id() != player->get_team_id();
    }

    game_object* get_target(float range)
    {
        if (!sdk::target_selector) return nullptr;

        if (auto* forced = sdk::target_selector->get_forced_target(); forced && is_valid_target(forced, range))
            return forced;

        for (auto* hero : sdk::target_selector->get_sorted_heroes())
        {
            if (is_valid_target(hero, range))
                return hero;
        }
        return nullptr;
    }

    void setup_prediction_templates()
    {
        s_q_close_pred = {};
        s_q_close_pred.spell_slot       = SLOT_Q;
        s_q_close_pred.spell_type       = pred_sdk::spell_type::linear;
        s_q_close_pred.targetting_type  = pred_sdk::targetting_type::edge_to_edge;
        s_q_close_pred.range            = data::Q_PHASE1_RANGE;
        s_q_close_pred.radius           = data::Q_PHASE1_WIDTH * 0.5f;
        s_q_close_pred.delay            = data::Q_CAST_TIME;
        s_q_close_pred.projectile_speed = data::Q_PHASE1_SPEED;
        s_q_close_pred.forbidden_collisions.clear();

        s_q_far_pred = s_q_close_pred;
        s_q_far_pred.range            = data::Q_RANGE;
        s_q_far_pred.radius           = data::Q_PHASE2_WIDTH * 0.5f;
        s_q_far_pred.delay            = data::Q_CAST_TIME + data::Q_PHASE1_RANGE / data::Q_PHASE1_SPEED;
        s_q_far_pred.projectile_speed = data::Q_PHASE2_SPEED;

        s_e_pred = {};
        s_e_pred.spell_slot       = SLOT_E;
        s_e_pred.spell_type       = pred_sdk::spell_type::circular;
        s_e_pred.targetting_type  = pred_sdk::targetting_type::center;
        s_e_pred.range            = data::E_RANGE;
        s_e_pred.radius           = data::E_RADIUS_MAX;
        s_e_pred.delay            = data::E_CAST_TIME;
        s_e_pred.projectile_speed = data::E_SPEED;
        s_e_pred.forbidden_collisions.clear();

        s_r_pred = {};
        s_r_pred.spell_slot       = SLOT_R;
        s_r_pred.spell_type       = pred_sdk::spell_type::linear;
        s_r_pred.targetting_type  = pred_sdk::targetting_type::edge_to_edge;
        s_r_pred.range            = data::R_RANGE[ 0 ];
        s_r_pred.radius           = data::R_WIDTH * 0.5f;
        s_r_pred.delay            = data::R_CAST_TIME;
        s_r_pred.projectile_speed = data::R_MISSILE_SPEED;
        s_r_pred.forbidden_collisions = {
            pred_sdk::collision_type::hero,
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall
        };
    }

    bool has_buff(game_object* obj, uint32_t hash, const char* name)
    {
        if (!obj) return false;
        if (hash != 0u)
        {
            if (auto* buff = obj->get_buff_by_hash(hash); buff && buff->is_active())
                return true;
        }
        if (!name || !*name)
            return false;

        const std::string needle = [&]() {
            std::string lowered{name};
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return lowered;
        }();

        for (auto* buff : obj->get_buffs())
        {
            if (!buff || !buff->is_active())
                continue;

            auto buff_name = buff->get_name();
            if (buff_name.empty())
                continue;

            std::transform(buff_name.begin(), buff_name.end(), buff_name.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });

            if (buff_name.find(needle) != std::string::npos)
                return true;
        }
        return false;
    }

    bool is_doom_ready()
    {
        auto* player = get_player();
        if (!player) return false;
        return has_buff(player, data::BUFF_DOOM_READY, data::BUFF_NAME_DOOM);
    }

    bool has_gloom_mark(game_object* target)
    {
        return has_buff(target, data::BUFF_GLOOM_MARK, data::BUFF_NAME_GLOOM);
    }

    bool has_r_mark(game_object* target)
    {
        return has_buff(target, data::BUFF_R_MARK, data::BUFF_NAME_R_MARK);
    }

    bool has_r_recast()
    {
        auto* player = get_player();
        if (!player) return false;
        return has_buff(player, data::BUFF_R_RESET, data::BUFF_NAME_R_RESET);
    }

    float get_aa_damage(game_object* target)
    {
        auto* player = get_player();
        if (!player || !target || !sdk::damage) return 0.f;
        const float raw = player->get_attack_damage();
        return sdk::damage->calc_damage(dmg_sdk::damage_type::physical, player, target, raw);
    }

    float get_spell_damage(int slot, game_object* target)
    {
        auto* player = get_player();
        if (!player || !target || !sdk::damage) return 0.f;
        return sdk::damage->get_spell_damage(player, target, slot);
    }

    float get_gloom_damage(game_object* target)
    {
        auto* player = get_player();
        if (!player || !target || !sdk::damage) return 0.f;
        const int level = std::max(1, player->get_level());
        const float base = data::GLOOM_BASE_MIN
            + (data::GLOOM_BASE_MAX - data::GLOOM_BASE_MIN) * static_cast<float>(level - 1) / 17.f;
        const float raw = base + data::GLOOM_AP_RATIO * player->get_ability_power();
        return sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, raw);
    }

    int count_enemies_near(const math::vector3& pos, float radius)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0;
        int count = 0;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero))
                continue;
            if (hero->get_position().distance(pos) <= radius)
                ++count;
        }
        return count;
    }

    float get_e_radius(float cast_distance)
    {
        const float clamped = std::clamp(cast_distance, 0.f, data::E_RANGE);
        return data::E_RADIUS_MIN + (data::E_RADIUS_MAX - data::E_RADIUS_MIN) * (clamped / data::E_RANGE);
    }

    float get_arrival_damage(game_object* target, float time_offset)
    {
        auto* player = get_player();
        if (!player || !target || !sdk::damage) return 0.f;

        auto* r_entry = player->get_spell(SLOT_R);
        const int r_level = r_entry ? r_entry->get_level() : 0;
        if (r_level <= 0) return 0.f;

        const int r_idx = std::clamp(r_level - 1, 0, 2);
        const float ap = player->get_ability_power();

        float total = 0.f;
        const float r2_raw = data::R2_BASE[ r_idx ] + data::R2_AP_RATIO * ap;
        total += sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, r2_raw);

        auto will_be_ready = [&](int slot) -> bool
        {
            auto* entry = player->get_spell(slot);
            if (!entry || entry->get_level() <= 0) return false;
            if (!sdk::awareness) return entry->get_cooldown() <= 0.f;
            return sdk::awareness->get_spell_cooldown(player, slot) <= time_offset;
        };

        if (will_be_ready(SLOT_Q)) total += get_spell_damage(SLOT_Q, target);
        if (will_be_ready(SLOT_W)) total += get_spell_damage(SLOT_W, target);
        if (will_be_ready(SLOT_E)) total += get_spell_damage(SLOT_E, target);

        total += get_gloom_damage(target);
        total += get_aa_damage(target);
        return total;
    }

    float get_combo_damage(game_object* target)
    {
        if (!target) return 0.f;
        float dmg = 0.f;

        if (is_spell_learned(SLOT_Q) && is_spell_ready(SLOT_Q))
            dmg += get_spell_damage(SLOT_Q, target);
        if (is_spell_learned(SLOT_W) && is_spell_ready(SLOT_W))
            dmg += get_spell_damage(SLOT_W, target);
        if (is_spell_learned(SLOT_E) && is_spell_ready(SLOT_E))
            dmg += get_spell_damage(SLOT_E, target);

        auto* player = get_player();
        if (player && is_spell_learned(SLOT_R) && is_spell_ready(SLOT_R))
        {
            dmg += get_spell_damage(SLOT_R, target);
            auto* r_entry = player->get_spell(SLOT_R);
            const int r_level = r_entry ? r_entry->get_level() : 0;
            if (r_level > 0)
            {
                const int idx = std::clamp(r_level - 1, 0, 2);
                const float ap = player->get_ability_power();
                const float r2_raw = data::R2_BASE[ idx ] + data::R2_AP_RATIO * ap;
                dmg += sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, r2_raw);
            }
        }

        dmg += get_gloom_damage(target);
        dmg += get_aa_damage(target) * 2.f;
        return dmg;
    }

    float get_r_damage(game_object* target)
    {
        auto* player = get_player();
        if (!player || !target || !sdk::damage) return 0.f;

        auto* r_entry = player->get_spell(SLOT_R);
        if (!r_entry || r_entry->get_level() <= 0) return 0.f;

        const int level_idx = std::clamp(r_entry->get_level() - 1, 0, 2);
        const float ap = player->get_ability_power();
        const float r2_raw = data::R2_BASE[ level_idx ] + data::R2_AP_RATIO * ap;

        float total = 0.f;

        if (has_r_recast())
        {
            total += sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, r2_raw);
        }
        else if (is_spell_ready(SLOT_R))
        {
            total += sdk::damage->get_spell_damage(player, target, SLOT_R);
            total += sdk::damage->calc_damage(dmg_sdk::damage_type::magical, player, target, r2_raw);
        }

        return total;
    }

    bool can_weave_cast()
    {
        if (!sdk::orbwalker) return true;
        return !sdk::orbwalker->would_cancel_attack();
    }

    bool try_q(int hitchance)
    {
        if (!is_spell_ready(SLOT_Q) || !has_mana_for(SLOT_Q)) return false;
        if (!sdk::prediction || !sdk::common) return false;
        auto* player = get_player();
        if (!player) return false;
        if (menu::combo_aa_weave && !can_weave_cast()) return false;

        auto* target = get_target(data::Q_RANGE);
        if (!target) return false;

        const float dist = player->get_position().distance(target->get_position());
        auto spell = (dist <= data::Q_PHASE1_RANGE) ? s_q_close_pred : s_q_far_pred;
        spell.expected_hitchance = hitchance;
        spell.source = player;
        spell.source_position = player->get_position();

        auto pred = sdk::prediction->predict(target, spell);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        return sdk::common->cast_spell(SLOT_Q, pred.cast_position);
    }

    bool try_w()
    {
        if (!is_spell_ready(SLOT_W) || !has_mana_for(SLOT_W)) return false;
        if (!sdk::common) return false;
        auto* player = get_player();
        if (!player) return false;

        if (count_enemies_near(player->get_position(), data::W_RADIUS) <= 0)
            return false;

        if (sdk::evade && sdk::evade->is_evading() && !sdk::evade->can_spell(SLOT_W, data::W_CAST_TIME))
            return false;

        return sdk::common->cast_spell(SLOT_W);
    }

    bool try_e(int hitchance)
    {
        if (!is_spell_ready(SLOT_E) || !has_mana_for(SLOT_E)) return false;
        if (!sdk::prediction || !sdk::common) return false;
        auto* player = get_player();
        if (!player) return false;
        if (menu::combo_aa_weave && !can_weave_cast()) return false;

        auto* target = get_target(data::E_RANGE);
        if (!target) return false;

        const float dist = player->get_position().distance(target->get_position());
        auto spell = s_e_pred;
        spell.radius = get_e_radius(dist);
        spell.expected_hitchance = hitchance;
        spell.source = player;
        spell.source_position = player->get_position();

        auto pred = sdk::prediction->predict(target, spell);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        return sdk::common->cast_spell(SLOT_E, pred.cast_position);
    }

    bool try_r1(int hitchance, bool respect_killable = true)
    {
        if (!is_spell_ready(SLOT_R) || !has_mana_for(SLOT_R)) return false;
        if (!sdk::prediction || !sdk::common) return false;
        if (has_r_recast()) return false;

        auto* player = get_player();
        if (!player) return false;

        auto spell = s_r_pred;
        if (auto* r_entry = player->get_spell(SLOT_R))
        {
            const int r_level = std::clamp(r_entry->get_level(), 1, 3);
            spell.range = data::R_RANGE[ r_level - 1 ];
        }

        auto* target = get_target(spell.range);
        if (!target) return false;

        if (respect_killable && menu::r_killable_only)
        {
            const float dist = player->get_position().distance(target->get_position());
            const float arrival = data::R_CAST_TIME + dist / data::R_MISSILE_SPEED + dist / data::R_DASH_SPEED;
            const float dmg = get_arrival_damage(target, arrival);
            const float hp  = target->get_hp() + target->get_all_shield() + target->get_magical_shield();
            if (dmg < hp) return false;
        }

        spell.expected_hitchance = hitchance;
        spell.source = player;
        spell.source_position = player->get_position();

        auto pred = sdk::prediction->predict(target, spell);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        return sdk::common->cast_spell(SLOT_R, pred.cast_position);
    }

    bool try_r2()
    {
        if (!menu::r_auto_recast) return false;
        if (!has_r_recast()) return false;
        if (!g_sdk || !g_sdk->object_manager || !sdk::common) return false;

        auto* player = get_player();
        if (!player) return false;

        game_object* marked = nullptr;
        for (auto* hero : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_target(hero))
                continue;
            if (has_r_mark(hero))
            {
                marked = hero;
                break;
            }
        }

        if (!marked) return false;

        if (menu::r2_killable_only)
        {
            const float dist = player->get_position().distance(marked->get_position());
            const float arrival = dist / data::R_DASH_SPEED;
            const float dmg = get_arrival_damage(marked, arrival);
            const float hp  = marked->get_hp() + marked->get_all_shield() + marked->get_magical_shield();
            if (dmg < hp) return false;
        }

        return sdk::common->cast_spell(SLOT_R);
    }

    bool emergency_cast_w()
    {
        if (!menu::anti_dash_w) return false;
        if (!is_spell_ready(SLOT_W) || !has_mana_for(SLOT_W)) return false;
        if (!sdk::common) return false;
        if (sdk::evade && sdk::evade->is_evading() && !sdk::evade->can_spell(SLOT_W, data::W_CAST_TIME))
            return false;
        return sdk::common->cast_spell(SLOT_W);
    }

    void combo()
    {
        if (menu::combo_use_r && try_r1(menu::combo_r_hitchance)) return;

        auto* player = get_player();
        if (menu::combo_use_w && player && count_enemies_near(player->get_position(), data::W_RADIUS) > 0)
        {
            if (try_w()) return;
        }

        const bool hold_q = menu::combo_hold_doom && is_doom_ready()
            && (is_spell_ready(SLOT_E) || is_spell_ready(SLOT_W));

        if (menu::combo_eq)
        {
            if (menu::combo_use_e && try_e(menu::combo_e_hitchance)) return;
            if (menu::combo_use_q && !hold_q && try_q(menu::combo_q_hitchance)) return;
        }
        else
        {
            if (menu::combo_use_q && !hold_q && try_q(menu::combo_q_hitchance)) return;
            if (menu::combo_use_e && try_e(menu::combo_e_hitchance)) return;
        }

        if (menu::combo_use_w && try_w()) return;
    }

    void harass()
    {
        if (mana_percent() < menu::harass_mana_pct) return;

        if (menu::harass_use_q && try_q(menu::combo_q_hitchance)) return;
        if (menu::harass_use_e && try_e(menu::combo_e_hitchance)) return;
    }

    void clear()
    {
        if (mana_percent() < menu::clear_mana_pct) return;
        if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;

        auto* player = get_player();
        if (!player) return;

        const auto minions = g_sdk->object_manager->get_minions();

        if (menu::clear_use_w && is_spell_ready(SLOT_W) && has_mana_for(SLOT_W))
        {
            int hits = 0;
            for (auto* m : minions)
            {
                if (!is_enemy_minion(m)) continue;
                if (m->get_position().distance(player->get_position()) <= data::W_RADIUS)
                    ++hits;
            }
            if (hits >= 3 && (!sdk::orbwalker || !sdk::orbwalker->would_cancel_attack()))
            {
                if (sdk::common->cast_spell(SLOT_W))
                    return;
            }
        }

        if (menu::clear_use_q && is_spell_ready(SLOT_Q) && has_mana_for(SLOT_Q))
        {
            if (!sdk::orbwalker || !sdk::orbwalker->would_cancel_attack())
            {
                for (auto* m : minions)
                {
                    if (!is_enemy_minion(m)) continue;
                    if (player->get_position().distance(m->get_position()) > data::Q_RANGE)
                        continue;
                    if (sdk::common->cast_spell(SLOT_Q, m->get_position()))
                        return;
                }
            }
        }

        if (menu::clear_use_e && is_spell_ready(SLOT_E) && has_mana_for(SLOT_E))
        {
            if (!sdk::orbwalker || !sdk::orbwalker->would_cancel_attack())
            {
                for (auto* m : minions)
                {
                    if (!is_enemy_minion(m)) continue;
                    if (player->get_position().distance(m->get_position()) > data::E_RANGE)
                        continue;
                    if (sdk::common->cast_spell(SLOT_E, m->get_position()))
                        return;
                }
            }
        }
    }

    void flee()
    {
        if (!sdk::common) return;
        auto* player = get_player();
        if (!player) return;

        if (menu::flee_use_w && is_spell_ready(SLOT_W) && has_mana_for(SLOT_W))
        {
            if (count_enemies_near(player->get_position(), data::W_RADIUS + 200.f) >= 1)
                sdk::common->cast_spell(SLOT_W);
        }

        if (menu::flee_use_e && is_spell_ready(SLOT_E) && has_mana_for(SLOT_E))
        {
            if (auto* target = get_target(data::E_RANGE))
                try_e(pred_sdk::hitchance::medium);
        }
    }

    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("vex", "MeowAIO - Vex");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Vex: failed to create menu category");
            return;
        }

        if (auto* q_menu = s_menu->add_sub_category("q", "Q - Mistral Bolt"))
        {
            q_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_q,
                [](bool v) { menu::combo_use_q = v; });
            q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q,
                [](bool v) { menu::harass_use_q = v; });
            q_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_q,
                [](bool v) { menu::clear_use_q = v; });
            q_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 5,
                menu::combo_q_hitchance, [](int v) {
                    menu::combo_q_hitchance = v;
                    menu::harass_q_hitchance = v;
                });
            q_menu->add_checkbox("aa_weave", "AA Weave", menu::combo_aa_weave,
                [](bool v) { menu::combo_aa_weave = v; });
            q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
                [](bool v) { menu::draw_q_range = v; });
            q_menu->add_hotkey("semi_q", "Semi-Manual Q", 'G', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_q_active = pressed;
                    if (key) menu::semi_q_key = *key;
                });
            if (menu::semi_q_key.empty()) menu::semi_q_key = "G";
        }

        if (auto* w_menu = s_menu->add_sub_category("w", "W - Personal Space"))
        {
            w_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_w,
                [](bool v) { menu::combo_use_w = v; });
            w_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_w,
                [](bool v) { menu::clear_use_w = v; });
            w_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_w,
                [](bool v) { menu::flee_use_w = v; });
            w_menu->add_checkbox("anti_dash", "Anti-Dash", menu::anti_dash_w,
                [](bool v) { menu::anti_dash_w = v; });
            w_menu->add_combo("anti_dash_mode", "Anti-Dash Mode", { "Always", "Combo Only" },
                menu::anti_dash_mode, [](int v) { menu::anti_dash_mode = v; });
            w_menu->add_checkbox("hold_doom", "Hold Doom for E/W", menu::combo_hold_doom,
                [](bool v) { menu::combo_hold_doom = v; });
        }

        if (auto* e_menu = s_menu->add_sub_category("e", "E - Looming Darkness"))
        {
            e_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_e,
                [](bool v) { menu::combo_use_e = v; });
            e_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_e,
                [](bool v) { menu::harass_use_e = v; });
            e_menu->add_checkbox("clear", "Use in Clear", menu::clear_use_e,
                [](bool v) { menu::clear_use_e = v; });
            e_menu->add_checkbox("flee", "Use in Flee", menu::flee_use_e,
                [](bool v) { menu::flee_use_e = v; });
            e_menu->add_checkbox("prefer_eq", "Prefer E -> Q", menu::combo_eq,
                [](bool v) { menu::combo_eq = v; });
            e_menu->add_slider_int("hitchance", "Hitchance", 0, 100, 5,
                menu::combo_e_hitchance, [](int v) {
                    menu::combo_e_hitchance = v;
                });
            e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
                [](bool v) { menu::draw_e_range = v; });
            e_menu->add_hotkey("semi_e", "Semi-Manual E", 'H', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_e_active = pressed;
                    if (key) menu::semi_e_key = *key;
                });
            if (menu::semi_e_key.empty()) menu::semi_e_key = "H";
        }

        if (auto* r_menu = s_menu->add_sub_category("r", "R - Shadow Surge"))
        {
            r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
                [](bool v) { menu::combo_use_r = v; });
            r_menu->add_slider_int("hc_combo", "Hitchance", 0, 100, 5,
                menu::combo_r_hitchance, [](int v) { menu::combo_r_hitchance = v; });
            r_menu->add_checkbox("killable", "R1 Killable Only", menu::r_killable_only,
                [](bool v) { menu::r_killable_only = v; });
            r_menu->add_checkbox("r2_killable", "R2 Killable Only", menu::r2_killable_only,
                [](bool v) { menu::r2_killable_only = v; });
            r_menu->add_checkbox("auto_recast", "Auto Recast R2", menu::r_auto_recast,
                [](bool v) { menu::r_auto_recast = v; });
            r_menu->add_checkbox("draw_range", "Draw Range", menu::draw_r_range,
                [](bool v) { menu::draw_r_range = v; });
            r_menu->add_checkbox("rainbow", "Rainbow Range", menu::draw_r_rgb,
                [](bool v) { menu::draw_r_rgb = v; });
            r_menu->add_hotkey("semi_r", "Semi-Manual R", 'T', false, false,
                [](std::string* key, bool pressed) {
                    menu::semi_r_active = pressed;
                    if (key) menu::semi_r_key = *key;
                });
            if (menu::semi_r_key.empty()) menu::semi_r_key = "T";
        }

        if (auto* mana_menu = s_menu->add_sub_category("mana", "Mana Thresholds"))
        {
            mana_menu->add_slider_float("harass", "Harass Mana %", 0.f, 1.f, 0.05f,
                menu::harass_mana_pct, [](float v) { menu::harass_mana_pct = v; });
            mana_menu->add_slider_float("clear", "Lane Clear Mana %", 0.f, 1.f, 0.05f,
                menu::clear_mana_pct, [](float v) { menu::clear_mana_pct = v; });
        }

        if (auto* draw_cat = s_menu->add_sub_category("draw", "Drawings"))
        {
            draw_cat->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            draw_cat->add_checkbox("damage", "R Damage Indicator", menu::draw_damage,
                [](bool v) { menu::draw_damage = v; });
            draw_cat->add_colorpicker("color_q", "Q Color", menu::color_q,
                [](uint32_t c) { menu::color_q = c; });
            draw_cat->add_colorpicker("color_e", "E Color", menu::color_e,
                [](uint32_t c) { menu::color_e = c; });
            draw_cat->add_colorpicker("color_r", "R Color", menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
            draw_cat->add_colorpicker("color_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
                [](uint32_t c) { menu::color_r_dmg = c; });
        }
    }

    void setup_infotab()
    {
        if (!sdk::infotab) return;

        if (!menu::semi_q_key.empty())
        {
            menu::infotab_semi_q = sdk::infotab->add_hotkey_text(
                &menu::semi_q_key, { "Semi Q" }, []() -> infotab_sdk::text_entry {
                    const bool active = menu::semi_q_active;
                    const uint32_t color = active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { active ? "ON" : "OFF", color };
                });
        }

        if (!menu::semi_e_key.empty())
        {
            menu::infotab_semi_e = sdk::infotab->add_hotkey_text(
                &menu::semi_e_key, { "Semi E" }, []() -> infotab_sdk::text_entry {
                    const bool active = menu::semi_e_active;
                    const uint32_t color = active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { active ? "ON" : "OFF", color };
                });
        }

        if (!menu::semi_r_key.empty())
        {
            menu::infotab_semi_r = sdk::infotab->add_hotkey_text(
                &menu::semi_r_key, { "Semi R" }, []() -> infotab_sdk::text_entry {
                    const bool active = menu::semi_r_active;
                    const uint32_t color = active ? 0xFF00FF00u : 0xFFFF0000u;
                    return { active ? "ON" : "OFF", color };
                });
        }
    }
} // namespace

void load()
{
    auto* player = get_player();
    if (!player || player->get_char_name() != "Vex")
        return;

    setup_prediction_templates();
    setup_menu();
    setup_infotab();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
    }

    g_sdk->log_console("[MeowAIO] Vex loaded MEOW!");
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
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->unregister_callback(event_manager::event::new_path,
            reinterpret_cast<void*>(on_new_path));
    }

    if (sdk::infotab)
    {
        if (menu::infotab_semi_q)
        {
            sdk::infotab->remove_text(menu::infotab_semi_q);
            menu::infotab_semi_q = 0;
        }
        if (menu::infotab_semi_e)
        {
            sdk::infotab->remove_text(menu::infotab_semi_e);
            menu::infotab_semi_e = 0;
        }
        if (menu::infotab_semi_r)
        {
            sdk::infotab->remove_text(menu::infotab_semi_r);
            menu::infotab_semi_r = 0;
        }
    }

    s_menu = nullptr;
    menu::semi_q_active = false;
    menu::semi_e_active = false;
    menu::semi_r_active = false;
    menu::semi_q_key.clear();
    menu::semi_e_key.clear();
    menu::semi_r_key.clear();
    g_sdk->log_console("[MeowAIO] Vex unloaded successfully!");
}

void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !g_sdk->clock_facade) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* player = get_player();
    if (!player || player->is_dead()) return;

    if (menu::r_auto_recast)
    {
        if (try_r2()) return;
    }

    if (menu::semi_r_active && try_r1(menu::combo_r_hitchance, false))
        return;

    if (menu::semi_e_active && try_e(menu::combo_e_hitchance))
        return;

    if (menu::semi_q_active && try_q(menu::combo_q_hitchance))
        return;

    if (sdk::orbwalker)
    {
        if (sdk::orbwalker->combo())
        {
            combo();
            return;
        }
        if (sdk::orbwalker->harass())
        {
            harass();
            return;
        }
        if (sdk::orbwalker->clear() || sdk::orbwalker->lasthit())
        {
            clear();
            return;
        }
        if (sdk::orbwalker->flee())
        {
            flee();
            return;
        }
    }
}

void __fastcall on_draw()
{
    if (!g_sdk || !g_sdk->renderer) return;
    auto* player = get_player();
    if (!player || player->is_dead()) return;

    if (!g_sdk->object_manager) return;

    const float scale = static_cast<float>(g_sdk->renderer->get_window_height()) / 1080.f;
    const float ox = -45.6f * scale;
    const float oy = 15.5f * scale;
    const float bw = 106.4f * scale;
    const float bh = 11.5f * scale;

    for (auto* hero : g_sdk->object_manager->get_heroes())
    {
        if (!is_valid_target(hero))
            continue;

        if (menu::draw_damage)
        {
            const float dmg = get_r_damage(hero);
            const float hp = hero->get_hp();
            const float max_hp = hero->get_max_hp();
            if (dmg > 0.f && max_hp > 0.f && hp > 0.f)
            {
                const auto hbar = hero->get_health_bar_position();
                if (!(hbar.x < 1.f && hbar.y < 1.f))
                {
                    const float r0 = hp / max_hp;
                    const float r1 = std::max(0.f, hp - dmg) / max_hp;
                    if (r0 > r1)
                    {
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

void __fastcall on_draw_world()
{
    if (!g_sdk) return;
    auto* player = get_player();
    if (!player || player->is_dead()) return;

    const auto pos = player->get_position();

    auto draw_circle = [&](float radius, uint32_t color, bool rainbow = false)
    {
        if (!color) return;
        if (sdk::shaders)
        {
            if (rainbow)
                sdk::shaders->draw_world_circle(pos, radius, 2.f, color, shaders_sdk::world_circle_style::rgb);
            else
                sdk::shaders->draw_world_circle(pos, radius, 2.f, color);
        }
        else if (g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, radius, 0.5f, color);
    };

    if (menu::draw_q_range && (!menu::draw_only_ready || is_spell_ready(SLOT_Q)))
        draw_circle(data::Q_RANGE, menu::color_q);

    if (menu::draw_e_range && (!menu::draw_only_ready || is_spell_ready(SLOT_E)))
        draw_circle(data::E_RANGE, menu::color_e);

    if (menu::draw_r_range && (!menu::draw_only_ready || is_spell_ready(SLOT_R)))
    {
        if (auto* r_entry = player->get_spell(SLOT_R))
        {
            const int level = std::clamp(r_entry->get_level(), 1, 3);
            const float range = data::R_RANGE[ level - 1 ];
            draw_circle(range, menu::color_r, menu::draw_r_rgb);
        }
    }
}

void __fastcall on_new_path(game_object* object, bool is_dash, float /*dash_speed*/)
{
    if (!menu::anti_dash_w || !is_dash || !object) return;
    auto* player = get_player();
    if (!player) return;
    if (object->get_team_id() == player->get_team_id()) return;
    if (!object->is_hero() || object->is_dead()) return;
    if (!is_doom_ready()) return;
    if (menu::anti_dash_mode == 1 && (!sdk::orbwalker || !sdk::orbwalker->combo())) return;

    const auto path = object->get_path();
    if (path.empty()) return;

    const auto dash_end = path.back();
    if (dash_end.distance(player->get_position()) > data::W_RADIUS_DASH)
        return;

    emergency_cast_w();
}

} // namespace vex
