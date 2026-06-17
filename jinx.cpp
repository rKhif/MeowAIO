#include "jinx.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace jinx
{
namespace
{
    // -----------------------------------------------------------------------
    // CONSTANTS
    // -----------------------------------------------------------------------
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    // Q - Switcheroo (self cast, no projectile - swaps weapon)
    constexpr float Q_AOE_RADIUS = 250.f;

    // W - Zap
    constexpr float W_RANGE  = 1450.f;
    constexpr float W_RADIUS = 60.f;
    constexpr float W_SPEED  = 3300.f;
    constexpr float W_DELAY  = 0.6f;

    // E - Flame Chompers
    constexpr float E_RANGE  = 925.f;
    constexpr float E_RADIUS = 100.f;
    constexpr float E_DELAY  = 0.9f;

    // R - Super Mega Death Rocket
    constexpr float R_RADIUS     = 140.f;
    constexpr float R_DELAY      = 0.6f;
    constexpr float R_SPEED_AVG  = 1900.f; // averaged pre/post acceleration projectile speed

    // Damage tables
    constexpr float W_DAMAGE_BASE[7] = { -40.f, 10.f, 60.f, 110.f, 160.f, 210.f, 260.f };
    constexpr float W_AD_RATIO       = 1.6f;

    constexpr float R_MAX_DAMAGE[7] = { 175.f, 325.f, 475.f, 625.f, 775.f, 925.f, 1075.f };
    constexpr float R_PCT_DAMAGE[7] = { 0.2f, 0.25f, 0.3f, 0.35f, 0.4f, 0.45f, 0.5f };
    constexpr float R_BONUS_AD_RATIO = 1.65f;

    // Buff name fragments (lowercase) used to identify the active Q weapon.
    // The VEN SDK exposes buff names as lowercase strings; matching by fragment
    // is more portable across patches than raw hashes.
    constexpr const char* BUFF_Q_ICON      = "jinxqicon"; // PowPow stance icon
    constexpr const char* BUFF_Q_FISHBONES = "jinxq";     // Fishbones stance buff

    // -----------------------------------------------------------------------
    // MENU STATE
    // -----------------------------------------------------------------------
    menu_category* s_menu = nullptr;

    namespace menu
    {
        // Q
        inline bool  q_combo        = true;
        inline bool  q_aoe          = true;
        inline bool  q_farm_pow_pow = true;
        inline bool  draw_q         = true;
        inline uint32_t color_q     = 0xFFEF8354u;

        // W
        inline bool  w_combo            = true;
        inline bool  w_harass           = false;
        inline int   w_auto_cc          = 1;
        inline int   w_auto_kill        = 1;
        inline bool  w_dont_in_aa_range = true;
        inline bool  draw_w             = true;
        inline uint32_t color_w         = 0xFF00B0FFu;

        // E
        inline bool  e_combo           = true;
        inline bool  e_auto_cc         = true;
        inline bool  e_auto_stasis     = true;
        inline bool  e_auto_gap_close  = true;
        inline bool  draw_e            = true;
        inline uint32_t color_e        = 0xFFE0A030u;

        // R
        inline bool  r_kill            = true;
        inline float r_max_range       = 1500.f;
        inline bool  r_use_cursor_rad  = true;
        inline int   r_cursor_radius   = 700;
        inline bool  draw_r            = true;
        inline uint32_t color_r        = 0xFFFF4040u;

        // Drawings
        inline bool  draw_only_ready   = true;
    }

    // -----------------------------------------------------------------------
    // RUNTIME STATE
    // -----------------------------------------------------------------------
    enum class weapon : uint8_t { pow_pow = 0, fishbones = 1 };

    weapon s_active_weapon = weapon::pow_pow;
    float  s_last_q_cast   = 0.f;
    float  s_last_w_cast   = 0.f;
    float  s_last_e_cast   = 0.f;
    float  s_last_r_cast   = 0.f;
    bool   s_semi_r_held   = false;

    // Auto-stasis cache: <network_id, <expire_time, buff_hash>>
    std::unordered_map<uint32_t, std::pair<float, uint32_t>> s_stasis_cache;

    const std::unordered_set<std::string> STASIS_BUFFS = {
        "chronorevive", "bardrstasis", "zhonyasringshield", "lissandrarself",
    };

    // -----------------------------------------------------------------------
    // HELPERS
    // -----------------------------------------------------------------------
    game_object* player()
    {
        return (g_sdk && g_sdk->object_manager) ? g_sdk->object_manager->get_local_player() : nullptr;
    }

    float game_time()
    {
        return (g_sdk && g_sdk->clock_facade) ? g_sdk->clock_facade->get_game_time() : 0.f;
    }

    bool spell_learned(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* sp = p->get_spell(slot);
        return sp && sp->get_level() > 0;
    }

    int spell_level(int slot)
    {
        auto* p = player();
        if (!p) return 0;
        auto* sp = p->get_spell(slot);
        return sp ? sp->get_level() : 0;
    }

    bool spell_ready(int slot)
    {
        auto* p = player();
        if (!p) return false;
        auto* sp = p->get_spell(slot);
        return sp && sp->get_level() > 0 && sp->get_cooldown() <= 0.f;
    }

    bool has_mana(int slot)
    {
        auto* p = player();
        if (!p) return false;
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    std::string lowercase(std::string v)
    {
        std::transform(v.begin(), v.end(), v.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    }

    bool has_buff_fragment(game_object* o, const char* fragment_lower)
    {
        if (!o) return false;
        for (auto* buff : o->get_buffs())
        {
            if (!buff || !buff->is_active()) continue;
            if (lowercase(buff->get_name()).find(fragment_lower) != std::string::npos)
                return true;
        }
        return false;
    }

    // Exact (case-insensitive) buff-name match. Necessary for Jinx because
    // "jinxq" (Fishbones), "jinxqicon" (PowPow) and "jinxqramp" (stacks) all
    // share the same "jinxq" fragment, so substring matching is ambiguous.
    bool has_buff_exact(game_object* o, const char* name_lower)
    {
        if (!o) return false;
        for (auto* buff : o->get_buffs())
        {
            if (!buff || !buff->is_active()) continue;
            if (lowercase(buff->get_name()) == name_lower)
                return true;
        }
        return false;
    }

    bool is_valid_enemy(game_object* o, float range = FLT_MAX)
    {
        auto* p = player();
        if (!p || !o || !o->is_valid() || o->is_dead()) return false;
        if (!o->is_targetable() || !o->is_visible()) return false;
        if (o->get_team_id() == p->get_team_id()) return false;
        if (range < FLT_MAX && p->get_position().distance(o->get_position()) > range)
            return false;
        return true;
    }

    bool has_spell_shield(game_object* o)
    {
        return o && o->has_buff_of_type(buff_type::spell_shield);
    }

    bool is_unstoppable(game_object* o)
    {
        if (!o) return false;
        return o->has_buff_of_type(buff_type::spell_immunity)
            || o->has_buff_of_type(buff_type::invulnerability)
            || o->has_buff_of_type(buff_type::physical_immunity);
    }

    bool is_cc_immobile(game_object* o)
    {
        if (!o) return false;
        return o->has_buff_of_type(buff_type::stun)
            || o->has_buff_of_type(buff_type::snare)
            || o->has_buff_of_type(buff_type::knockup)
            || o->has_buff_of_type(buff_type::knockback)
            || o->has_buff_of_type(buff_type::charm)
            || o->has_buff_of_type(buff_type::fear)
            || o->has_buff_of_type(buff_type::taunt)
            || o->has_buff_of_type(buff_type::suppression)
            || o->has_buff_of_type(buff_type::asleep);
    }

    bool can_cast(float& last_cast, float min_interval = 0.2f)
    {
        const float now = game_time();
        return now <= 0.f || now - last_cast >= min_interval;
    }

    void mark_cast(float& last_cast)
    {
        last_cast = game_time();
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

    // -----------------------------------------------------------------------
    // Q - SWITCHEROO
    // -----------------------------------------------------------------------
    float fishbones_bonus_range()
    {
        // 50 + 30 * level
        return 50.f + 30.f * static_cast<float>(spell_level(SLOT_Q));
    }

    void refresh_active_weapon()
    {
        auto* p = player();
        if (!p) return;

        // Read buffs directly each frame instead of relying solely on events.
        // Order matters: jinxqicon must be checked specifically since it shares
        // the "jinxq" prefix with the Fishbones buff.
        if (has_buff_exact(p, BUFF_Q_ICON))
            s_active_weapon = weapon::pow_pow;
        else if (has_buff_exact(p, BUFF_Q_FISHBONES))
            s_active_weapon = weapon::fishbones;
    }

    float effective_aa_range(game_object* target)
    {
        auto* p = player();
        if (!p) return 0.f;
        const float base = p->get_attack_range() + p->get_bounding_radius()
            + (target ? target->get_bounding_radius() : 0.f);
        const float bonus = (s_active_weapon == weapon::fishbones) ? fishbones_bonus_range() : 0.f;
        return base + bonus;
    }

    bool try_swap_q(const char* /*reason*/)
    {
        if (!sdk::common) return false;
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return false;
        if (!can_cast(s_last_q_cast, 0.18f)) return false;
        if (sdk::common->cast_spell(SLOT_Q))
        {
            mark_cast(s_last_q_cast);
            return true;
        }
        return false;
    }

    bool switcheroo_on_before_attack(game_object* target)
    {
        if (!menu::q_combo && !menu::q_aoe && !menu::q_farm_pow_pow) return false;
        if (!target || !spell_learned(SLOT_Q)) return false;

        auto* p = player();
        if (!p) return false;

        const bool fishbones_active = (s_active_weapon == weapon::fishbones);

        // Don't swap mid-swap window
        if (!has_buff_exact(p, BUFF_Q_ICON) && !has_buff_exact(p, BUFF_Q_FISHBONES))
            return false;

        // If Fishbones is active and target is well inside short-range PowPow distance,
        // swap to PowPow for AAs against non-heroes / for higher DPS.
        if (fishbones_active)
        {
            const float pow_pow_range = p->get_attack_range() + p->get_bounding_radius()
                + target->get_bounding_radius();
            if (p->get_position().distance(target->get_position()) > pow_pow_range - 10.f)
                return false;
        }

        // Non-hero targets (minions / monsters): prefer PowPow unless AoE Fishbones is wanted.
        if (!target->is_hero())
        {
            if (fishbones_active && menu::q_farm_pow_pow)
            {
                return try_swap_q("farm-to-powpow");
            }
            return false;
        }

        // Heroes: AOE Fishbones if multiple allied targets are stacked near target.
        if (menu::q_aoe && !fishbones_active)
        {
            const auto tpos = target->get_position();
            int stack = 0;
            for (auto* h : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(h)) continue;
                if (h->get_position().distance(tpos) <= Q_AOE_RADIUS)
                    ++stack;
            }
            if (stack >= 2)
            {
                return try_swap_q("aoe-swap");
            }
        }

        if (!menu::q_combo) return false;

        // Compute post-mitigation AA damage with each weapon and the resulting
        // attack-time-to-kill. Mirrors the original Lulu/Jinx source's
        // GetPowPowAttackTime vs AttackSpeedFishbones comparison.
        const float raw_aa = p->get_attack_damage();
        if (raw_aa <= 1.f) return false;
        const float fishbones_bonus = 0.1f * raw_aa;

        const float target_hp = target->get_hp() + target->get_all_shield()
            + target->get_physical_shield();
        float aa_dmg_raw = raw_aa;
        if (sdk::damage)
        {
            aa_dmg_raw = sdk::damage->calc_damage(
                dmg_sdk::damage_type::physical, p, target, raw_aa);
        }
        if (aa_dmg_raw <= 1.f) return false;

        const float aa_with_bonus = aa_dmg_raw + fishbones_bonus;
        const float aas_needed_pow_pow   = std::ceil(target_hp / aa_dmg_raw);
        const float aas_needed_fishbones = std::ceil(target_hp / aa_with_bonus);

        // PowPow attack speed approximation
        const float pow_pow_as_mod = p->get_percent_attack_speed_mod();
        const float base_as        = 0.625f;
        const float pow_pow_as     = base_as * (1.f + pow_pow_as_mod);
        // Fishbones is roughly 10% slower attack rate vs PowPow at stack 3
        const float fishbones_as   = pow_pow_as * 0.9f;

        const float time_pow_pow   = aas_needed_pow_pow   / std::max(0.01f, pow_pow_as);
        const float time_fishbones = aas_needed_fishbones / std::max(0.01f, fishbones_as);

        if (fishbones_active)
        {
            // Currently on Fishbones and target is inside PowPow AA range.
            // Swap back to PowPow when its higher attack speed kills sooner.
            if (time_pow_pow < time_fishbones)
                return try_swap_q("kill-time-powpow");
            return false;
        }

        // Currently on PowPow. Only swap to Fishbones for the kill-time reason
        // when Fishbones genuinely finishes faster — in practice this is the
        // narrow band where Fishbones one-shots a target PowPow can't (target
        // HP between one PowPow AA and one Fishbones AA), or where the rocket
        // damage saves the player a full extra AA. For sustained-DPS fights
        // (high-HP targets) PowPow's attack speed wins this comparison and we
        // keep PowPow, which is what the player expects in close range.
        if (aas_needed_pow_pow <= 1.f) return false;        // PowPow one-shots already
        if (time_fishbones >= time_pow_pow) return false;   // PowPow still wins on time
        return try_swap_q("kill-time-fishbones");
    }

    // VEN's orb_sdk only exposes before/after attack and before_move callbacks,
    // so the original "OnOutOfRange" hook is folded into this proactive on_update
    // check. Only range-extension (PowPow -> Fishbones when every valid target
    // is outside PowPow AA range but inside Fishbones AA range) is handled here.
    // AOE / farm / kill-time swaps stay in switcheroo_on_before_attack so they
    // only trigger at the moment of an actual auto-attack, matching the
    // original Lulu/Jinx logic.
    bool try_q_proactive_swap()
    {
        if (!menu::q_combo) return false;
        if (!sdk::orbwalker || !sdk::orbwalker->combo()) return false;
        if (!spell_learned(SLOT_Q) || !spell_ready(SLOT_Q) || !has_mana(SLOT_Q))
            return false;
        if (!can_cast(s_last_q_cast, 0.18f)) return false;
        if (sdk::orbwalker->would_cancel_attack()) return false;
        if (!sdk::target_selector) return false;

        auto* p = player();
        if (!p) return false;

        // Don't try to swap while a swap is already in flight.
        if (!has_buff_exact(p, BUFF_Q_ICON) && !has_buff_exact(p, BUFF_Q_FISHBONES))
            return false;

        if (s_active_weapon == weapon::fishbones) return false;

        const auto self_pos = p->get_position();
        const float pow_pow_base   = p->get_attack_range() + p->get_bounding_radius();
        const float fishbones_base = pow_pow_base + fishbones_bonus_range();

        // We only extend to Fishbones when EVERY valid hero target is outside
        // PowPow AA range. If anyone is in PowPow range, PowPow's higher DPS
        // (and the before_attack damage/AOE heuristic) wins — don't yank the
        // player out of close-quarters combat into Fishbones unsolicited.
        game_object* extend_target = nullptr;
        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h)) continue;
            const float dist = self_pos.distance(h->get_position());
            const float pow_pow_max = pow_pow_base + h->get_bounding_radius();
            if (dist <= pow_pow_max) return false; // someone is in PowPow range — stay on PowPow
            const float fishbones_max = fishbones_base + h->get_bounding_radius();
            if (dist > fishbones_max) continue;    // out of Fishbones too — keep looking
            if (!extend_target) extend_target = h;
        }
        if (!extend_target) return false;
        return try_swap_q("range-extend");
    }

    // -----------------------------------------------------------------------
    // W - ZAP
    // -----------------------------------------------------------------------
    float compute_w_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        const int lvl = std::clamp(spell_level(SLOT_W), 0, 6);
        const float raw = W_DAMAGE_BASE[lvl] + W_AD_RATIO * p->get_attack_damage();
        if (raw <= 0.f) return 0.f;
        if (sdk::damage)
            return sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target, raw);
        return raw;
    }

    pred_sdk::pred_data predict_w(game_object* target, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        sd.spell_slot         = SLOT_W;
        sd.range              = W_RANGE;
        sd.radius             = W_RADIUS;
        sd.delay              = W_DELAY;
        sd.projectile_speed   = W_SPEED;
        sd.expected_hitchance = hitchance;
        sd.forbidden_collisions = {
            pred_sdk::collision_type::hero,
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data result{};
        try { result = sdk::prediction->predict(target, sd); }
        catch (...) { result.is_valid = false; }
        return result;
    }

    bool try_w_on(game_object* target, int min_hit)
    {
        if (!sdk::common || !sdk::prediction) return false;
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        if (!can_cast(s_last_w_cast, 0.3f)) return false;
        if (!is_valid_enemy(target, W_RANGE)) return false;
        if (has_spell_shield(target)) return false;

        auto* p = player();
        if (!p) return false;

        // Don't W if target is already inside AA range and the AA can pressure them.
        if (menu::w_dont_in_aa_range
            && p->get_position().distance(target->get_position()) <= effective_aa_range(target))
        {
            return false;
        }

        auto pred = predict_w(target, min_hit);
        if (!pred.is_valid) return false;
        if (pred.hitchance < min_hit) return false;

        if (sdk::common->cast_spell(SLOT_W, pred.cast_position))
        {
            mark_cast(s_last_w_cast);
            return true;
        }
        return false;
    }

    bool try_w_killable()
    {
        if (!menu::w_auto_kill) return false;
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, W_RANGE)) continue;
            if (has_spell_shield(h)) continue;
            const float dmg = compute_w_damage(h);
            const float ehp = h->get_hp() + h->get_all_shield() + h->get_physical_shield();
            if (dmg < ehp) continue;
            if (try_w_on(h, pred_sdk::hitchance::high)) return true;
        }
        return false;
    }

    bool try_w_cc()
    {
        if (!menu::w_auto_cc) return false;
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, W_RANGE)) continue;
            if (!is_cc_immobile(h)) continue;
            if (has_spell_shield(h)) continue;
            if (try_w_on(h, pred_sdk::hitchance::medium)) return true;
        }
        return false;
    }

    bool try_w_combo()
    {
        if (!menu::w_combo) return false;
        auto* target = pick_hero_target(W_RANGE);
        if (!target) return false;
        return try_w_on(target, pred_sdk::hitchance::high);
    }

    // -----------------------------------------------------------------------
    // E - FLAME CHOMPERS
    // -----------------------------------------------------------------------
    pred_sdk::pred_data predict_e(game_object* target, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::circular;
        sd.targetting_type    = pred_sdk::targetting_type::center;
        sd.spell_slot         = SLOT_E;
        sd.range              = E_RANGE;
        sd.radius             = E_RADIUS;
        sd.delay              = E_DELAY;
        sd.projectile_speed   = FLT_MAX;
        sd.expected_hitchance = hitchance;

        pred_sdk::pred_data result{};
        try { result = sdk::prediction->predict(target, sd); }
        catch (...) { result.is_valid = false; }
        return result;
    }

    bool try_e_at(const math::vector3& pos)
    {
        if (!sdk::common) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        if (!can_cast(s_last_e_cast, 0.4f)) return false;
        if (sdk::common->cast_spell(SLOT_E, pos))
        {
            mark_cast(s_last_e_cast);
            return true;
        }
        return false;
    }

    bool try_e_stasis()
    {
        if (!menu::e_auto_stasis) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        if (s_stasis_cache.empty()) return false;

        auto* p = player();
        if (!p) return false;
        const auto self_pos = p->get_position();
        const float now = game_time();

        for (auto it = s_stasis_cache.begin(); it != s_stasis_cache.end();)
        {
            if (it->second.first <= now) { it = s_stasis_cache.erase(it); continue; }
            auto* target = g_sdk->object_manager->get_object_by_network_id(it->first);
            ++it;
            if (!target || !target->is_valid() || target->is_dead()) continue;
            if (target->get_team_id() == p->get_team_id()) continue;
            if (target->is_targetable()) continue; // not currently in stasis state
            if (self_pos.distance(target->get_position()) > E_RANGE) continue;

            const float expire = s_stasis_cache[target->get_network_id()].first;
            if (now < expire - 0.9f && try_e_at(target->get_position()))
                return true;
        }
        return false;
    }

    bool try_e_cc()
    {
        if (!menu::e_auto_cc) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, E_RANGE)) continue;
            if (!is_cc_immobile(h)) continue;
            const auto pred = predict_e(h, pred_sdk::hitchance::medium);
            if (pred.is_valid && try_e_at(pred.cast_position))
                return true;
        }
        return false;
    }

    bool try_e_dash()
    {
        if (!menu::e_auto_gap_close) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;

        auto* p = player();
        if (!p) return false;
        const auto self_pos = p->get_position();

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, E_RANGE + 200.f)) continue;
            if (!h->is_dashing()) continue;
            if (is_unstoppable(h) || has_spell_shield(h)) continue;
            const auto pred = predict_e(h, pred_sdk::hitchance::low);
            if (!pred.is_valid) continue;
            // Only commit if predicted cast position is inside range.
            if (self_pos.distance(pred.cast_position) > E_RANGE) continue;
            if (try_e_at(pred.cast_position)) return true;
        }
        return false;
    }

    bool try_e_combo()
    {
        if (!menu::e_combo) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        auto* target = pick_hero_target(E_RANGE);
        if (!target) return false;
        // Avoid wasting E on point-blank short-path targets
        auto* p = player();
        if (!p) return false;
        const float dist = p->get_position().distance(target->get_position());
        if (dist < 150.f) return false;

        const auto pred = predict_e(target, pred_sdk::hitchance::high);
        if (!pred.is_valid) return false;
        return try_e_at(pred.cast_position);
    }

    // -----------------------------------------------------------------------
    // R - SUPER MEGA DEATH ROCKET
    // -----------------------------------------------------------------------
    float compute_r_damage(game_object* target, float distance)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        const int lvl = std::clamp(spell_level(SLOT_R), 0, 6);
        // Bonus AD = total - base. native_sdk only exposes total attack damage.
        // Use a simple approximation: bonus_ad ~= 0.7 * attack_damage for mid-late
        // game scaling. Marksman with crit items: this is conservative.
        const float total_ad = p->get_attack_damage();
        const float bonus_ad = std::max(0.f, total_ad - p->get_base_attack_damage());
        const float raw_max  = R_MAX_DAMAGE[lvl] + R_BONUS_AD_RATIO * bonus_ad;

        // Distance damage scaling: 10% base + 6% per 100u, capped at 100%.
        float mod = 0.1f;
        float remaining = distance;
        for (int i = 1; i < 15; ++i)
        {
            remaining -= 100.f;
            if (remaining <= 0.f) break;
            mod += 0.06f;
        }
        mod = std::min(1.f, mod);

        const float distance_dmg = raw_max * mod;
        const float pct_missing  = target->get_max_hp() - target->get_hp();
        const float pct_dmg      = pct_missing * R_PCT_DAMAGE[lvl];

        if (sdk::damage)
            return sdk::damage->calc_damage(dmg_sdk::damage_type::physical, p, target, distance_dmg + pct_dmg);
        return distance_dmg + pct_dmg;
    }

    pred_sdk::pred_data predict_r(game_object* target)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        sd.spell_slot         = SLOT_R;
        sd.range              = menu::r_max_range; // capped by user setting
        sd.radius             = R_RADIUS;
        sd.delay              = R_DELAY;
        sd.projectile_speed   = R_SPEED_AVG;
        sd.expected_hitchance = pred_sdk::hitchance::high;
        sd.forbidden_collisions = {
            pred_sdk::collision_type::hero,
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data result{};
        try { result = sdk::prediction->predict(target, sd); }
        catch (...) { result.is_valid = false; }
        return result;
    }

    bool try_r_killsteal()
    {
        if (!menu::r_kill) return false;
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return false;
        if (!can_cast(s_last_r_cast, 0.5f)) return false;

        auto* p = player();
        if (!p) return false;
        const auto self_pos = p->get_position();

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h, menu::r_max_range + 200.f)) continue;
            if (has_spell_shield(h)) continue;

            const float dist = self_pos.distance(h->get_position());

            // Don't ult inside extended AA range; just AA them.
            if (dist < effective_aa_range(h) * 1.1f) continue;
            if (dist > menu::r_max_range) continue;

            const auto pred = predict_r(h);
            if (!pred.is_valid) continue;
            if (pred.hitchance < pred_sdk::hitchance::high) continue;

            const float dist_to_cast = self_pos.distance(pred.cast_position);
            const float dmg = compute_r_damage(h, dist_to_cast);
            const float ehp = h->get_hp() + h->get_all_shield() + h->get_physical_shield();
            if (dmg < ehp) continue;

            if (sdk::common && sdk::common->cast_spell(SLOT_R, pred.cast_position))
            {
                mark_cast(s_last_r_cast);
                return true;
            }
        }
        return false;
    }

    bool try_r_semi_auto()
    {
        if (!s_semi_r_held) return false;
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return false;
        if (!can_cast(s_last_r_cast, 0.5f)) return false;

        auto* p = player();
        if (!p || !g_sdk || !g_sdk->hud_manager) return false;

        const auto self_pos = p->get_position();
        const auto cursor   = g_sdk->hud_manager->get_cursor_position();
        const float cursor_radius_sqr = static_cast<float>(menu::r_cursor_radius)
            * static_cast<float>(menu::r_cursor_radius);

        game_object* best   = nullptr;
        float        best_d = FLT_MAX;

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!is_valid_enemy(h)) continue;
            const auto hpos = h->get_position();
            const float dist = self_pos.distance(hpos);
            if (dist > menu::r_max_range) continue;
            if (menu::r_use_cursor_rad)
            {
                const float dx = hpos.x - cursor.x;
                const float dz = hpos.z - cursor.z;
                if (dx * dx + dz * dz > cursor_radius_sqr) continue;
            }
            if (dist < best_d) { best_d = dist; best = h; }
        }

        if (!best) return false;

        const auto pred = predict_r(best);
        if (!pred.is_valid) return false;
        if (pred.hitchance < pred_sdk::hitchance::medium) return false;

        if (sdk::common && sdk::common->cast_spell(SLOT_R, pred.cast_position))
        {
            mark_cast(s_last_r_cast);
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // MENU SETUP
    // -----------------------------------------------------------------------
    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_jinx", "MeowAIO - Jinx");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Jinx: failed to create menu category");
            return;
        }

        if (auto* qm = s_menu->add_sub_category("q_switcheroo", "Q - Switcheroo!"))
        {
            qm->add_checkbox("combo",        "Auto Swap in Combo",      menu::q_combo,
                [](bool v) { menu::q_combo = v; });
            qm->add_checkbox("aoe",          "Use Fishbones for AOE",   menu::q_aoe,
                [](bool v) { menu::q_aoe = v; });
            qm->add_checkbox("farmpowpow",   "Force Pow-Pow on Minions", menu::q_farm_pow_pow,
                [](bool v) { menu::q_farm_pow_pow = v; });
            qm->add_separator();
            qm->add_checkbox("draw_range",   "Draw Q Range",             menu::draw_q,
                [](bool v) { menu::draw_q = v; });
            qm->add_colorpicker("color",     "Q Color",                  menu::color_q,
                [](uint32_t c) { menu::color_q = c; });
        }

        if (auto* wm = s_menu->add_sub_category("w_zap", "W - Zap!"))
        {
            wm->add_checkbox("combo",         "Use W in Combo",          menu::w_combo,
                [](bool v) { menu::w_combo = v; });
            wm->add_checkbox("harass",        "Use W in Harass",         menu::w_harass,
                [](bool v) { menu::w_harass = v; });
            wm->add_separator();
            wm->add_label("[Auto] Settings");
            wm->add_checkbox("auto_cc",       "Auto W on CC/Stasis",     menu::w_auto_cc != 0,
                [](bool v) { menu::w_auto_cc = v ? 1 : 0; });
            wm->add_checkbox("auto_kill",     "Auto W on Killable",      menu::w_auto_kill != 0,
                [](bool v) { menu::w_auto_kill = v ? 1 : 0; });
            wm->add_separator();
            wm->add_label("[Advanced] Settings");
            wm->add_checkbox("aa_range",      "Don't W if Enemy in AA Range", menu::w_dont_in_aa_range,
                [](bool v) { menu::w_dont_in_aa_range = v; });
            wm->add_separator();
            wm->add_checkbox("draw_range",    "Draw W Range",            menu::draw_w,
                [](bool v) { menu::draw_w = v; });
            wm->add_colorpicker("color",      "W Color",                 menu::color_w,
                [](uint32_t c) { menu::color_w = c; });
        }

        if (auto* em = s_menu->add_sub_category("e_flame_chompers", "E - Flame Chompers!"))
        {
            em->add_checkbox("combo",         "Use E in Combo",          menu::e_combo,
                [](bool v) { menu::e_combo = v; });
            em->add_separator();
            em->add_label("[Auto] Settings");
            em->add_checkbox("auto_cc",       "On CC",                    menu::e_auto_cc,
                [](bool v) { menu::e_auto_cc = v; });
            em->add_checkbox("auto_stasis",   "On Stasis / Revive",       menu::e_auto_stasis,
                [](bool v) { menu::e_auto_stasis = v; });
            em->add_checkbox("auto_gap",      "On Gap Close",             menu::e_auto_gap_close,
                [](bool v) { menu::e_auto_gap_close = v; });
            em->add_separator();
            em->add_checkbox("draw_range",    "Draw E Range",             menu::draw_e,
                [](bool v) { menu::draw_e = v; });
            em->add_colorpicker("color",      "E Color",                  menu::color_e,
                [](uint32_t c) { menu::color_e = c; });
        }

        if (auto* rm = s_menu->add_sub_category("r_rocket", "R - Super Mega Death Rocket!"))
        {
            rm->add_checkbox("kill",           "Auto Killsteal",          menu::r_kill,
                [](bool v) { menu::r_kill = v; });
            rm->add_hotkey("semi",             "Semi-Auto",               'G', false, false,
                [](std::string*, bool down) { s_semi_r_held = down; });
            rm->add_slider_float("max_range",  "Max Cast Range",          800.f, 4000.f, 50.f,
                menu::r_max_range, [](float v) { menu::r_max_range = v; });
            rm->add_checkbox("use_cursor_rad", "Use Cursor Radius",       menu::r_use_cursor_rad,
                [](bool v) { menu::r_use_cursor_rad = v; });
            rm->add_slider_int("cursor_rad",   "Cursor Radius",           200, 2500, 50,
                menu::r_cursor_radius, [](int v) { menu::r_cursor_radius = v; });
            rm->add_separator();
            rm->add_checkbox("draw_range",     "Draw R Range",            menu::draw_r,
                [](bool v) { menu::draw_r = v; });
            rm->add_colorpicker("color",       "R Color",                 menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
        }

        if (auto* dm = s_menu->add_sub_category("drawings", "Drawings"))
        {
            dm->add_checkbox("only_ready", "Only Draw Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
        }

        s_menu->add_separator();
    }
}

// =========================================================================
// EVENTS
// =========================================================================
void __fastcall on_update()
{
    if (!g_sdk || !g_sdk->object_manager || !sdk::common) return;
    if (g_sdk->is_chat_open() || g_sdk->is_shop_open()) return;

    auto* p = player();
    if (!p || p->is_dead()) return;

    refresh_active_weapon();

    // Reactive autos (do not require combo orbwalker mode).
    if (try_r_semi_auto()) return;
    if (try_r_killsteal()) return;

    if (try_e_stasis())   return;
    if (try_e_cc())       return;
    if (try_e_dash())     return;

    if (try_w_cc())       return;
    if (try_w_killable()) return;

    if (!sdk::orbwalker) return;

    if (sdk::orbwalker->combo())
    {
        if (try_q_proactive_swap()) return;
        if (try_e_combo()) return;
        if (try_w_combo()) return;
    }
    else if (sdk::orbwalker->harass() && menu::w_harass)
    {
        if (try_w_combo()) return;
    }
}

void __fastcall on_draw()
{
    // No 2D HUD elements.
}

void __fastcall on_draw_world()
{
    auto* p = player();
    if (!p || p->is_dead() || !g_sdk || !g_sdk->renderer) return;

    const auto pos = p->get_position();

    auto draw_range = [&](float range, uint32_t color, int slot)
    {
        if (color == 0u || range <= 0.f) return;
        if (!spell_learned(slot)) return;
        if (menu::draw_only_ready && !spell_ready(slot)) return;
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, range, 1.5f, ::color(color));
        else
            g_sdk->renderer->add_circle_3d(pos, range, 1.f, color);
    };

    if (menu::draw_q)
    {
        // Show effective AA range with current weapon
        const float aa = p->get_attack_range()
            + ((s_active_weapon == weapon::fishbones) ? fishbones_bonus_range() : 0.f);
        draw_range(aa, menu::color_q, SLOT_Q);
    }
    if (menu::draw_w) draw_range(W_RANGE, menu::color_w, SLOT_W);
    if (menu::draw_e) draw_range(E_RANGE, menu::color_e, SLOT_E);
    if (menu::draw_r)
    {
        // R has unlimited range; show the user-set cast limit.
        draw_range(menu::r_max_range, menu::color_r, SLOT_R);
    }
}

void __fastcall on_buff_gain(game_object* object, buff_instance* buff)
{
    if (!object || !buff) return;

    auto* p = player();
    if (!p) return;

    // Track active Q weapon from local player buffs.
    // Use exact match: the "jinxq" prefix is shared by jinxq (Fishbones),
    // jinxqicon (PowPow icon) and jinxqramp (PowPow attack speed stacks).
    if (object == p)
    {
        const auto name = lowercase(buff->get_name());
        if (name == BUFF_Q_FISHBONES)
            s_active_weapon = weapon::fishbones;
        else if (name == BUFF_Q_ICON)
            s_active_weapon = weapon::pow_pow;
    }

    // Track enemy stasis / zhonyas / revive for auto-E.
    if (object->is_hero() && object->get_team_id() != p->get_team_id())
    {
        const auto name = lowercase(buff->get_name());
        for (const auto& fragment : STASIS_BUFFS)
        {
            if (name.find(fragment) != std::string::npos)
            {
                s_stasis_cache[object->get_network_id()] =
                    { buff->get_end_time(), buff->get_hash() };
                break;
            }
        }
    }
}

void __fastcall on_buff_loss(game_object* object, buff_instance* buff)
{
    if (!object || !buff) return;
    auto* p = player();
    if (!p) return;

    // Karthus / Sion / Aatrox revive trigger
    if (object->is_hero() && object->get_team_id() != p->get_team_id())
    {
        const auto name = lowercase(buff->get_name());
        if (name.find("willrevive") != std::string::npos)
        {
            s_stasis_cache[object->get_network_id()] = { game_time() + 4.f, 0x1337u };
        }
    }
}

bool __stdcall on_before_attack(orb_sdk::event_data* data)
{
    if (!data || !data->target) return true;

    auto* p = player();
    if (!p) return true;
    if (!sdk::orbwalker || !sdk::orbwalker->combo()) return true;

    switcheroo_on_before_attack(data->target);
    return true;
}

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Jinx")
        return;

    setup_menu();

    if (g_sdk && g_sdk->event_manager)
    {
        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::present,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->register_callback(orb_sdk::event_type::before_attack,
            reinterpret_cast<void*>(on_before_attack));
    }

    if (g_sdk) g_sdk->log_console("[MeowAIO] Jinx loaded MEOW!");
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
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));
    }

    if (sdk::orbwalker)
    {
        sdk::orbwalker->unregister_callback(orb_sdk::event_type::before_attack,
            reinterpret_cast<void*>(on_before_attack));
    }

    s_menu = nullptr;
    s_active_weapon = weapon::pow_pow;
    s_last_q_cast = s_last_w_cast = s_last_e_cast = s_last_r_cast = 0.f;
    s_semi_r_held = false;
    s_stasis_cache.clear();

    if (g_sdk) g_sdk->log_console("[MeowAIO] Jinx unloaded successfully!");
}

} // namespace jinx
