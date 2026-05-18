#include "mel.h"
#include "mel_spell_db.hpp"

#include <cstdio>
#include <cmath>
#include <algorithm>
#include <array>
#include <unordered_map>

#include "utility/defensive_buffs.hpp"
#include "utility/helpers.hpp"

namespace mel
{
    static menu_category* s_menu = nullptr;

    // --- Brilliance stack tracking (self) ---
    static int   brilliance_stacks = 0;
    static float brilliance_expire = 0.f;

    // --- E→Q chain pending state ---
    static bool  eq_pending     = false;
    static float eq_root_expire = 0.f;

    static constexpr float MIN_SPELL_DELAY = 0.05f;
    static float           last_spell_cast_time = 0.f;

    // --- W reflect: per-enemy [Q,W,E,R] enable flags, populated lazily once heroes load ---
    static menu_category*                                       s_w_reflect_menu = nullptr;
    static std::unordered_map<uint32_t, std::array<bool, 4>>    s_w_reflect_map;
    static bool                                                 s_w_reflect_built = false;

    // --- W whitelist: per-enemy [Q,W,E,R,AA] "ignore by smart modes" flags ---
    static menu_category*                                       s_w_skip_menu  = nullptr;
    static std::unordered_map<uint32_t, std::array<bool, 5>>    s_w_skip_map;
    static bool                                                 s_w_skip_built = false;
    static constexpr int SKIP_IDX_AA = 4;

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
        // Ignore basic-attack windups
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

    static float distance_to(game_object* o)
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || !o) return FLT_MAX;
        return p->get_position().distance(o->get_position());
    }

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
    // OVERWHELM stack tracking (read directly from buff)
    // ============================================================================
    static int get_overwhelm_stacks(game_object* target)
    {
        if (!target) return 0;
        std::string name{ spell_data::buff_name_overwhelm };
        auto* b = target->get_buff_by_name(name);
        if (!b || !b->is_active()) return 0;
        return std::max((int)b->get_stacks(), b->get_count());
    }

    static int total_overwhelm_champions()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return 0;
        int n = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || h->is_dead()) continue;
            if (h->get_team_id() == p->get_team_id()) continue;
            if (get_overwhelm_stacks(h) > 0) ++n;
        }
        return n;
    }

    // ============================================================================
    // R DAMAGE — Golden Eclipse: base + AP + per-stack bonus from Overwhelm.
    //   Base:       125 / 200 / 275 + 30% AP
    //   Per stack:  (4 / 7 / 10) + 4% AP, multiplied by current Overwhelm stacks
    // SDK is preferred for the base (handles per-target modifiers); stack bonus
    // is always computed manually since the SDK damage table doesn't know stacks.
    // Both buckets are mitigated as magical.
    // ============================================================================
    static float compute_r_damage(game_object* src, game_object* tgt)
    {
        if (!src || !tgt || !sdk::damage) return 0.f;

        auto* r = src->get_spell(SLOT_R);
        if (!r) return 0.f;
        const int lvl = r->get_level();
        if (lvl <= 0) return 0.f;

        const int   idx = std::clamp(lvl - 1, 0, 2);
        const float ap  = src->get_ability_power();

        // Base damage — prefer SDK (post-mitigation), else compute & mitigate manually.
        float base_dmg = sdk::damage->get_spell_damage(src, tgt, SLOT_R);
        if (base_dmg <= 0.f) {
            static constexpr float r_base[3] = { 125.f, 200.f, 275.f };
            const float raw = r_base[idx] + 0.30f * ap;
            base_dmg = sdk::damage->calc_damage(dmg_sdk::damage_type::magical, src, tgt, raw);
        }

        // Per-stack bonus — Overwhelm stacks on this target.
        const int stacks = get_overwhelm_stacks(tgt);
        if (stacks <= 0) return base_dmg;

        static constexpr float r_stack_base[3] = { 4.f, 7.f, 10.f };
        static constexpr float r_stack_ap      = 0.04f;
        const float stack_raw = (r_stack_base[idx] + r_stack_ap * ap)
                              * static_cast<float>(stacks);
        const float stack_dmg = sdk::damage->calc_damage(
            dmg_sdk::damage_type::magical, src, tgt, stack_raw);

        return base_dmg + stack_dmg;
    }

    // ============================================================================
    // PREDICTION (crash-safe)
    // ============================================================================
    static pred_sdk::pred_data predict_q(game_object* target, int hitchance)
    {
        pred_sdk::spell_data spell{};
        spell.spell_type         = pred_sdk::spell_type::circular;
        spell.targetting_type    = pred_sdk::targetting_type::center;
        spell.spell_slot         = SLOT_Q;
        spell.range              = spell_data::q_range;
        spell.radius             = spell_data::q_radius;
        spell.delay              = spell_data::q_cast_time;
        spell.projectile_speed   = FLT_MAX;
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

    // ============================================================================
    // SPELL CASTS
    // ============================================================================
    static bool cast_q_at(const math::vector3& pos)
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        math::vector3 cast_pos = pos;
        const auto pp = p->get_position();
        if (pp.distance(pos) > spell_data::q_range)
            cast_pos = pp.extended(pos, spell_data::q_range);
        bool ok = sdk::common->cast_spell(SLOT_Q, cast_pos);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_q(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;
        if (distance_to(target) > spell_data::q_range) return false;

        auto pred = predict_q(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;
        return cast_q_at(pred.cast_position);
    }

    static bool try_e(game_object* target, int hitchance)
    {
        if (!target || !sdk::common || !sdk::prediction) return false;
        if (!is_spell_ready(SLOT_E) || !has_enough_mana(SLOT_E)) return false;
        if (distance_to(target) > spell_data::e_range) return false;

        auto pred = predict_e(target, hitchance);
        if (!pred.is_valid || pred.hitchance < hitchance) return false;

        bool ok = sdk::common->cast_spell(SLOT_E, pred.cast_position);
        if (ok) {
            last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            // Arm E→Q chain — root lasts ~1.5s after the missile lands
            if (menu::combo_eq_chain) {
                eq_pending = true;
                eq_root_expire = g_sdk->clock_facade->get_game_time()
                               + (spell_data::e_range / spell_data::e_speed)
                               + spell_data::e_cast_time
                               + spell_data::e_root_duration;
            }
        }
        return ok;
    }

    // ============================================================================
    // W REFLECT — per-enemy spell config built lazily once heroes are loaded
    // ============================================================================

    // Defaults for known champions — [Q, W, E, R]. Curated to flag projectile /
    // targeted burst ults that Mel's W can meaningfully reflect. Self-buffs,
    // dashes, and pure ground-AOEs are intentionally left OFF.
    static std::array<bool, 4> reflect_defaults_for_champ(const std::string& name)
    {
        static const std::unordered_map<std::string, std::array<bool, 4>> table = {
            { "Riven",       { false, false, false, true  } }, // R: Wind Slash
            { "Karthus",     { false, false, false, true  } }, // R: Requiem
            { "Veigar",      { false, false, false, true  } }, // R: Primordial Burst
            { "Lux",         { false, false, false, true  } }, // R: Final Spark
            { "Brand",       { false, false, false, true  } }, // R: Pyroclasm
            { "Syndra",      { false, false, false, true  } }, // R: Unleashed Power
            { "Xerath",      { false, false, false, true  } }, // R: Rite of the Arcane
            { "Ezreal",      { false, false, false, true  } }, // R: Trueshot Barrage
            { "MissFortune", { false, false, false, true  } }, // R: Bullet Time
            { "Lissandra",   { false, false, false, true  } }, // R: Frozen Tomb
            { "Vex",         { false, false, false, true  } }, // R: Shadow Surge
            { "Caitlyn",     { false, false, false, true  } }, // R: Ace in the Hole
            { "Jhin",        { false, false, false, true  } }, // R: Curtain Call
            { "Jinx",        { false, false, false, true  } }, // R: Super Mega Death Rocket
            { "Vladimir",    { false, false, false, true  } }, // R: Hemoplague
            { "Velkoz",      { false, false, false, true  } }, // R: Life Form Disintegration Ray
            { "Ziggs",       { false, false, false, true  } }, // R: Mega Inferno Bomb
            { "Varus",       { false, false, false, true  } }, // R: Chain of Corruption
            { "Ashe",        { false, false, false, true  } }, // R: Enchanted Crystal Arrow
            { "Draven",      { false, false, false, true  } }, // R: Whirling Death
            { "Sona",        { false, false, false, true  } }, // R: Crescendo
            { "Hwei",        { false, false, false, true  } }, // R: Spiraling Despair
        };
        auto it = table.find(name);
        return it != table.end() ? it->second : std::array<bool, 4>{ false, false, false, false };
    }

    static void build_reflect_menu_if_needed()
    {
        if (s_w_reflect_built || !s_w_reflect_menu) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        bool added_any = false;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || !h->is_hero()) continue;
            if (h == p) continue;
            if (h->get_team_id() == p->get_team_id()) continue;

            const std::string char_name = h->get_char_name();
            if (char_name.empty()) continue;

            const uint32_t nid = h->get_network_id();
            if (s_w_reflect_map.find(nid) != s_w_reflect_map.end()) continue;

            const auto defaults = reflect_defaults_for_champ(char_name);
            s_w_reflect_map[nid] = defaults;

            // Unique element key per net-id; display the champion name
            const std::string key = "e_" + std::to_string(nid);
            auto* sub = s_w_reflect_menu->add_sub_category(key, char_name);
            if (!sub) continue;

            static const char* slot_names[4] = { "Q", "W", "E", "R" };
            for (int i = 0; i < 4; ++i) {
                sub->add_checkbox(
                    std::string("s") + slot_names[i],
                    std::string("Reflect ") + slot_names[i],
                    defaults[i],
                    [nid, i](bool v) {
                        auto it = s_w_reflect_map.find(nid);
                        if (it != s_w_reflect_map.end()) it->second[i] = v;
                    });
            }
            added_any = true;
        }
        if (added_any) s_w_reflect_built = true;
    }

    // Whitelist (per-enemy) — slots a user wants Smart Modes to NEVER react to.
    // Built lazily once heroes are present. Does not affect the per-enemy
    // force-on override (s_w_reflect_map) — those still fire if user enabled them.
    static void build_skip_menu_if_needed()
    {
        if (s_w_skip_built || !s_w_skip_menu) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        bool added_any = false;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!h || !h->is_valid() || !h->is_hero()) continue;
            if (h == p) continue;
            if (h->get_team_id() == p->get_team_id()) continue;

            const std::string char_name = h->get_char_name();
            if (char_name.empty()) continue;

            const uint32_t nid = h->get_network_id();
            if (s_w_skip_map.find(nid) != s_w_skip_map.end()) continue;
            s_w_skip_map[nid] = { false, false, false, false, false };

            const std::string key = "sk_" + std::to_string(nid);
            auto* sub = s_w_skip_menu->add_sub_category(key, char_name);
            if (!sub) continue;

            static const char* labels[5] = { "Q", "W", "E", "R", "Auto Attack" };
            for (int i = 0; i < 5; ++i) {
                sub->add_checkbox(
                    std::string("s") + std::to_string(i),
                    std::string("Ignore ") + labels[i],
                    false,
                    [nid, i](bool v) {
                        auto it = s_w_skip_map.find(nid);
                        if (it != s_w_skip_map.end()) it->second[i] = v;
                    });
            }
            added_any = true;
        }
        if (added_any) s_w_skip_built = true;
    }

    // Perpendicular distance from point P to segment [A,B] in the XZ plane.
    // League skillshots are effectively 2D for collision so we drop Y.
    static float distance_point_to_segment_2d(const math::vector3& p,
                                              const math::vector3& a,
                                              const math::vector3& b)
    {
        const float dx = b.x - a.x;
        const float dz = b.z - a.z;
        const float len_sq = dx * dx + dz * dz;
        if (len_sq <= 1e-3f) {
            const float px = p.x - a.x, pz = p.z - a.z;
            return std::sqrt(px * px + pz * pz);
        }
        float t = ((p.x - a.x) * dx + (p.z - a.z) * dz) / len_sq;
        t = std::clamp(t, 0.f, 1.f);
        const float cx = a.x + t * dx;
        const float cz = a.z + t * dz;
        const float ex = p.x - cx, ez = p.z - cz;
        return std::sqrt(ex * ex + ez * ez);
    }

    // Does this cast's geometry actually hit us? Targeted on us → yes;
    // skillshot whose line passes within margin of us → yes.
    static bool cast_hits_player(game_object* caster, spell_cast* cast, game_object* p)
    {
        if (auto* tgt = cast->get_target()) {
            if (tgt->get_network_id() == p->get_network_id()) return true;
        }
        // Skillshot path test — same heuristic as before.
        constexpr float path_width = 150.f;
        const auto start  = caster->get_position();
        const auto end    = cast->get_end_pos();
        const float margin = p->get_bounding_radius() + path_width;
        const float d = distance_point_to_segment_2d(p->get_position(), start, end);
        return d <= margin;
    }

    // Estimate the incoming damage this cast will deal to us.
    //   Basic AA:   base AD through calc_damage (mitigation).
    //   Special AA: try SDK first via the cast's spell slot (headshots & friends
    //               often have a synthetic slot in the damage table); fall back
    //               to (base AD * 2.5) so headshot-style amps still trigger
    //               lethal blocks when the SDK is silent.
    //   Spell:      direct sdk::damage->get_spell_damage.
    static float estimate_incoming_damage(game_object* caster, spell_cast* cast, game_object* p)
    {
        if (!sdk::damage || !caster || !cast || !p) return 0.f;

        if (cast->is_basic_attack()) {
            const float raw = caster->get_attack_damage();
            return sdk::damage->calc_damage(dmg_sdk::damage_type::physical, caster, p, raw);
        }

        if (cast->is_special_attack()) {
            const int sslot = cast->get_spell_slot();
            float dmg = sdk::damage->get_spell_damage(caster, p, sslot);
            if (dmg > 0.f) return dmg;
            constexpr float special_aa_safety_multiplier = 2.5f;
            const float raw = caster->get_attack_damage() * special_aa_safety_multiplier;
            return sdk::damage->calc_damage(dmg_sdk::damage_type::physical, caster, p, raw);
        }

        const int slot = cast->get_spell_slot();
        if (slot < SLOT_Q || slot > SLOT_R) return 0.f;
        return sdk::damage->get_spell_damage(caster, p, slot);
    }

    static bool cast_w_now()
    {
        if (!sdk::common) return false;
        bool ok = sdk::common->cast_spell(SLOT_W);
        if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
        return ok;
    }

    static bool try_w_reflect(game_object* caster, spell_cast* cast)
    {
        if (!menu::w_reflect_enabled) return false;
        if (!caster || !cast || !sdk::common) return false;
        if (!is_spell_ready(SLOT_W) || !has_enough_mana(SLOT_W)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return false;
        if (caster == p) return false;
        if (caster->get_team_id() == p->get_team_id()) return false;

        const bool is_aa = cast->is_basic_attack() || cast->is_special_attack();
        const int  slot  = cast->get_spell_slot();
        if (!is_aa && (slot < SLOT_Q || slot > SLOT_R)) return false;

        // --- Per-enemy force-on override (legacy path). Only applies to
        //     Q/W/E/R; AAs are not covered by the slot-keyed map.
        if (!is_aa) {
            auto it = s_w_reflect_map.find(caster->get_network_id());
            if (it != s_w_reflect_map.end() && it->second[slot]) {
                if (auto* tgt = cast->get_target()) {
                    if (tgt->get_network_id() == p->get_network_id()) return cast_w_now();
                }
                if (!menu::w_reflect_targeted_only && cast_hits_player(caster, cast, p)) {
                    return cast_w_now();
                }
            }
        }

        // --- Whitelist: user said "ignore this spell from auto." Bail before
        //     Smart Modes run. Force-on overrides above are unaffected.
        {
            auto sit = s_w_skip_map.find(caster->get_network_id());
            if (sit != s_w_skip_map.end()) {
                const int idx = is_aa ? SKIP_IDX_AA : slot;
                if (sit->second[idx]) return false;
            }
        }

        // --- Smart Mode 1: Block targeted spells/AAs aimed at us.
        //     Targeted casts always hit, so no geometry check needed.
        auto* tgt = cast->get_target();
        const bool targets_us = tgt && tgt->get_network_id() == p->get_network_id();
        if (menu::w_mode_block_targeted && targets_us) return cast_w_now();

        // --- Smart Mode 2: Block lethal — predicted HP after damage falls
        //     to or below the configured threshold (% of max HP).
        if (menu::w_mode_block_lethal && cast_hits_player(caster, cast, p)) {
            const float dmg = estimate_incoming_damage(caster, cast, p);
            if (dmg > 0.f) {
                const float threshold = p->get_max_hp()
                    * (static_cast<float>(menu::w_lethal_hp_pct) / 100.f);
                const float post = (p->get_hp() + p->get_all_shield()) - dmg;
                if (post <= threshold) return cast_w_now();
            }
        }

        return false;
    }

    // R is global and hits any Overwhelmed enemy regardless of vision/targetability.
    // Use a relaxed filter that only excludes dead / same-team heroes.
    static bool is_r_eligible(game_object* h)
    {
        if (!h || !h->is_valid() || h->is_dead() || !h->is_hero()) return false;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;
        if (h->get_team_id() == p->get_team_id()) return false;
        return true;
    }

    static bool try_r()
    {
        if (!sdk::common) return false;
        if (!is_spell_ready(SLOT_R) || !has_enough_mana(SLOT_R)) return false;
        if (total_overwhelm_champions() < 1) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        // Killable check — execute the lowest-EHP killable target. Walking the full
        // list and picking the lowest avoids being dependent on get_heroes() ordering
        // when multiple enemies are simultaneously killable.
        // Kill confidence: require R damage >= EHP * (min_kill_pct/100). Defaults to
        // 100% (exact lethal); raise the menu value for a safety margin against
        // mid-cast heals / shields.
        if (menu::combo_r_killable && sdk::damage) {
            const float kill_ratio = static_cast<float>(menu::combo_r_min_kill_pct) / 100.f;
            game_object* best_kill = nullptr;
            float        best_ehp  = FLT_MAX;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_r_eligible(h)) continue;
                const int stacks = get_overwhelm_stacks(h);
                if (stacks <= 0) continue;
                if (venplus::utility::has_anti_burst_buff(h)) continue;
                const float dmg = compute_r_damage(p, h);
                const float ehp = h->get_hp() + h->get_all_shield();
                if (dmg >= ehp * kill_ratio && ehp < best_ehp) {
                    best_kill = h;
                    best_ehp  = ehp;
                }
            }
            if (best_kill) {
                bool ok = sdk::common->cast_spell(SLOT_R);
                if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
                return ok;
            }
        }

        // Stack-threshold gate: best non-immune enemy must have >= min stacks.
        // Skipping anti-burst-buffed targets here too — a Zhonya'd carrier with
        // 10 stacks shouldn't make us blow R.
        int best = 0;
        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_r_eligible(h)) continue;
            if (venplus::utility::has_anti_burst_buff(h)) continue;
            best = std::max(best, get_overwhelm_stacks(h));
        }
        if (best >= menu::combo_r_min_stacks) {
            bool ok = sdk::common->cast_spell(SLOT_R);
            if (ok) last_spell_cast_time = g_sdk->clock_facade->get_game_time();
            return ok;
        }
        return false;
    }

    // ============================================================================
    // E→Q CHAIN — fire Q on a freshly rooted enemy
    // ============================================================================
    static bool handle_eq_chain()
    {
        if (!eq_pending) return false;
        const float now = g_sdk->clock_facade->get_game_time();
        if (now > eq_root_expire) { eq_pending = false; return false; }
        if (!is_spell_ready(SLOT_Q) || !has_enough_mana(SLOT_Q)) return false;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return false;

        for (auto* h : g_sdk->object_manager->get_heroes()) {
            if (!is_valid_target(h, spell_data::q_range)) continue;
            if (!h->has_buff_of_type(buff_type::snare)) continue;
            // Aim Q directly on top of the rooted target
            if (cast_q_at(h->get_position())) {
                eq_pending = false;
                return true;
            }
        }
        return false;
    }

    // ============================================================================
    // GAME MODES
    // ============================================================================
    void combo()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        // R is global — try before the in-range target gate so we can still
        // execute a stacked enemy who's far away or in fog.
        if (menu::combo_use_r && try_r()) return;

        auto* target = pick_hero_target(spell_data::e_range);
        if (!target) return;

        if (handle_eq_chain()) return;

        if (menu::combo_use_e && try_e(target, menu::combo_e_hitchance)) return;
        if (menu::combo_use_q && try_q(target, menu::combo_q_hitchance)) return;
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

        if (menu::harass_use_e && try_e(target, menu::harass_e_hitchance)) return;
        if (menu::harass_use_q && try_q(target, menu::harass_q_hitchance)) return;
    }

    void clear()
    {
        if (!sdk::orbwalker || !sdk::orbwalker->is_spell_farm_enabled()) return;
        if (mana_pct() < (float)menu::clear_mana_pct) return;
        if (sdk::orbwalker->would_cancel_attack()) return;

        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;
        if (is_casting()) return;

        const float now = g_sdk->clock_facade->get_game_time();
        if (now - last_spell_cast_time < MIN_SPELL_DELAY) return;

        if (menu::clear_use_q && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)) {
            const auto pp = p->get_position();
            int best_count = 0;
            math::vector3 best_pos{};

            auto minions = g_sdk->object_manager->get_minions();
            for (auto* m : minions) {
                if (!is_valid_enemy_minion(m)) continue;
                const auto mp = m->get_position();
                if (pp.distance(mp) > spell_data::q_range) continue;

                int count = 0;
                for (auto* o : minions) {
                    if (!is_valid_enemy_minion(o)) continue;
                    if (mp.distance(o->get_position()) <= spell_data::q_total_radius) ++count;
                }
                if (count > best_count) {
                    best_count = count;
                    best_pos = mp;
                }
            }
            if (best_count >= menu::clear_q_min) {
                cast_q_at(best_pos);
                return;
            }
        }
    }

    void flee()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        if (menu::flee_use_e && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)) {
            // Root the closest pursuing enemy
            auto* target = pick_hero_target(spell_data::e_range);
            if (target) try_e(target, pred_sdk::hitchance::medium);
        }
    }

    // ============================================================================
    // EVENT CALLBACKS
    // ============================================================================
    void __fastcall on_update()
    {
        auto p = g_sdk->object_manager->get_local_player();
        if (!p || p->is_dead()) return;

        build_reflect_menu_if_needed();
        build_skip_menu_if_needed();

        // Expire brilliance stacks if past the duration
        const float now = g_sdk->clock_facade->get_game_time();
        if (brilliance_expire > 0.f && now > brilliance_expire) {
            brilliance_stacks = 0;
            brilliance_expire = 0.f;
        }

        // Semi E (hotkey hold) — root the best target in range, any orbwalker mode
        if (menu::semi_e_pressed && is_spell_ready(SLOT_E) && has_enough_mana(SLOT_E)) {
            if (auto* t = pick_hero_target(spell_data::e_range)) {
                if (try_e(t, pred_sdk::hitchance::low)) return;
            }
        }

        // Auto Q (toggle) — continuously throw Q at best target when off-cooldown
        if (menu::auto_q_enabled && !is_casting()
            && (now - last_spell_cast_time >= MIN_SPELL_DELAY)
            && is_spell_ready(SLOT_Q) && has_enough_mana(SLOT_Q)) {
            if (auto* t = pick_hero_target(spell_data::q_range)) {
                if (try_q(t, menu::auto_q_hitchance)) return;
            }
        }

        if (!sdk::orbwalker) return;
        if      (sdk::orbwalker->combo())  combo();
        else if (sdk::orbwalker->harass()) harass();
        else if (sdk::orbwalker->clear())  clear();
        else if (sdk::orbwalker->flee())   flee();
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
        if (menu::draw_e_range) {
            const uint32_t c = get_draw_color(SLOT_E, menu::color_e);
            if (c) draw_circle(pos, spell_data::e_range, c);
        }

        if (menu::draw_r_dmg_bar && sdk::damage && g_sdk->renderer) {
            const bool r_learned = is_spell_learned(SLOT_R);
            const float scale = (float)g_sdk->renderer->get_window_height() / 1080.f;
            const float ox = -45.6f * scale, oy = 15.5f * scale;
            const float bw = 106.4f * scale, bh = 11.5f * scale;
            for (auto* h : g_sdk->object_manager->get_heroes()) {
                if (!is_valid_target(h)) continue;

                // R EXECUTE indicator — would R kill this target now? Matches the
                // try_r() kill-confidence threshold so the indicator and cast agree.
                if (is_spell_ready(SLOT_R) && get_overwhelm_stacks(h) > 0) {
                    const float dmg = compute_r_damage(p, h);
                    const float ehp = h->get_hp() + h->get_all_shield();
                    const float kill_ratio = static_cast<float>(menu::combo_r_min_kill_pct) / 100.f;
                    if (dmg >= ehp * kill_ratio) {
                        const auto hbar = h->get_health_bar_position();
                        if (!(hbar.x < 1.f && hbar.y < 1.f)) {
                            g_sdk->renderer->add_text("R EXECUTE", 13.f,
                                { hbar.x, hbar.y - 20.f }, 1, menu::color_r);
                        }
                    }
                }

                if (menu::draw_r_dmg_bar && r_learned) {
                    const float r_dmg = compute_r_damage(p, h);
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

    void __fastcall on_basic_attack(game_object* obj, game_object* /*target*/, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;
        if (obj->get_network_id() == p->get_network_id()) return;
        if (obj->get_team_id() == p->get_team_id()) return;
        try_w_reflect(obj, cast);
    }

    void __fastcall on_process_cast(game_object* obj, spell_cast* cast)
    {
        if (!obj || !cast) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        // Enemy spell — try to reflect with W if configured
        if (obj->get_network_id() != p->get_network_id()) {
            try_w_reflect(obj, cast);
            return;
        }

        const int slot = cast->get_spell_slot();
        if (slot < SLOT_Q || slot > SLOT_R) return;

        // Each Q/W/E/R cast adds 3 brilliance stacks (max 9)
        brilliance_stacks = std::min(brilliance_stacks + spell_data::brilliance_stacks_per_cast,
                                     spell_data::brilliance_max_stacks);
        brilliance_expire = g_sdk->clock_facade->get_game_time() + spell_data::brilliance_duration;

        // Arm E→Q chain when E starts (in addition to the arm in try_e for cases
        // where the user manually casts E)
        if (slot == SLOT_E && menu::combo_eq_chain && !eq_pending) {
            eq_pending = true;
            eq_root_expire = g_sdk->clock_facade->get_game_time()
                           + (spell_data::e_range / spell_data::e_speed)
                           + spell_data::e_cast_time
                           + spell_data::e_root_duration;
        }
    }

    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        if (obj == p) {
            // Searing Brilliance buff on self — sync stacks
            if (buff->get_name().find(spell_data::buff_name_brilliance) != std::string::npos) {
                brilliance_stacks = std::max((int)buff->get_stacks(), buff->get_count());
                brilliance_expire = buff->get_end_time();
            }
        }
    }

    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff)
    {
        if (!obj || !buff) return;
        auto p = g_sdk->object_manager->get_local_player();
        if (!p) return;

        if (obj == p) {
            if (buff->get_name().find(spell_data::buff_name_brilliance) != std::string::npos) {
                brilliance_stacks = 0;
                brilliance_expire = 0.f;
            }
        }
    }

    // ============================================================================
    // LIFECYCLE
    // ============================================================================
    void load()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("mel", "MeowAIO - Mel");
        if (!s_menu) {
            g_sdk->log_console("[!] Mel: failed to create menu category");
            return;
        }

        // Q — Radiant Volley
        auto q_menu = s_menu->add_sub_category("q_radiant_volley", "Q - Radiant Volley");
        q_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_q,  [](bool v) { menu::combo_use_q = v; });
        q_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_q, [](bool v) { menu::harass_use_q = v; });
        q_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_q,  [](bool v) { menu::clear_use_q = v; });
        q_menu->add_slider_int("c_min", "  Clear Min Minions", 1, 7, 1, menu::clear_q_min,
            [](int v) { menu::clear_q_min = v; });
        q_menu->add_separator();
        q_menu->add_slider_int("c_hc", "Hitchance",  0, 100, 5, menu::combo_q_hitchance,
            [](int v) { menu::combo_q_hitchance = v; });
        q_menu->add_slider_int("h_hc", "Harass Hitchance", 0, 100, 5, menu::harass_q_hitchance,
            [](int v) { menu::harass_q_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_hotkey("auto_q", "Auto Q (TOGGLE)", 'M', false, true,
            [](std::string*, bool state) { menu::auto_q_enabled = state; });
        q_menu->add_slider_int("auto_hc", "  Auto Q Hitchance", 0, 100, 5, menu::auto_q_hitchance,
            [](int v) { menu::auto_q_hitchance = v; });
        q_menu->add_separator();
        q_menu->add_checkbox("draw_range", "Draw Range", menu::draw_q_range,
            [](bool v) { menu::draw_q_range = v; });

        // W — Rebuttal
        auto w_menu = s_menu->add_sub_category("w_rebuttal", "W - Rebuttal");

        w_menu->add_checkbox("on", "Enable Reflect", menu::w_reflect_enabled,
            [](bool v) { menu::w_reflect_enabled = v; });
        w_menu->add_checkbox("targeted_only", "Force-on Overrides: Targeted Only",
            menu::w_reflect_targeted_only,
            [](bool v) { menu::w_reflect_targeted_only = v; });
        w_menu->add_separator();
        w_menu->add_checkbox("blk_targeted", "Block Targeted Spells",
            menu::w_mode_block_targeted,
            [](bool v) { menu::w_mode_block_targeted = v; });
        w_menu->add_checkbox("blk_lethal", "Block Lethal",
            menu::w_mode_block_lethal,
            [](bool v) { menu::w_mode_block_lethal = v; });
        w_menu->add_slider_int("lethal_hp", "  Lethal Health %", 0, 50, 1,
            menu::w_lethal_hp_pct,
            [](int v) { menu::w_lethal_hp_pct = v; });
        w_menu->add_separator();

        // Per-enemy override list — force W on for specific slots regardless of Smart Modes.
        s_w_reflect_menu = w_menu->add_sub_category("reflect", "Force Reflect (per enemy)");

        // Per-enemy whitelist — make Smart Modes ignore specific slots/AA per enemy.
        s_w_skip_menu = w_menu->add_sub_category("whitelist", "Whitelist (ignore per enemy)");

        // E — Solar Snare
        auto e_menu = s_menu->add_sub_category("e_solar_snare", "E - Solar Snare");
        e_menu->add_checkbox("combo",  "Use in Combo",  menu::combo_use_e,  [](bool v) { menu::combo_use_e = v; });
        e_menu->add_checkbox("harass", "Use in Harass", menu::harass_use_e, [](bool v) { menu::harass_use_e = v; });
        e_menu->add_checkbox("clear",  "Use in Clear",  menu::clear_use_e,  [](bool v) { menu::clear_use_e = v; });
        e_menu->add_checkbox("flee",   "Use in Flee (root pursuer)", menu::flee_use_e,
            [](bool v) { menu::flee_use_e = v; });
        e_menu->add_separator();
        e_menu->add_slider_int("c_hc", "Combo Hitchance",  0, 100, 5, menu::combo_e_hitchance,
            [](int v) { menu::combo_e_hitchance = v; });
        e_menu->add_slider_int("h_hc", "Harass Hitchance", 0, 100, 5, menu::harass_e_hitchance,
            [](int v) { menu::harass_e_hitchance = v; });
        e_menu->add_checkbox("eq_chain", "Auto E→Q on Rooted", menu::combo_eq_chain,
            [](bool v) { menu::combo_eq_chain = v; });
        e_menu->add_separator();
        e_menu->add_hotkey("semi_e", "Semi E", 'T', false, false,
            [](std::string*, bool pressed) { menu::semi_e_pressed = pressed; });
        e_menu->add_separator();
        e_menu->add_checkbox("draw_range", "Draw Range", menu::draw_e_range,
            [](bool v) { menu::draw_e_range = v; });

        // R — Ascension
        auto r_menu = s_menu->add_sub_category("r_ascension", "R - Ascension");
        r_menu->add_checkbox("combo", "Use in Combo", menu::combo_use_r,
            [](bool v) { menu::combo_use_r = v; });
        r_menu->add_slider_int("min", "Min Overwhelm Stacks on Target", 1, 20, 1, menu::combo_r_min_stacks,
            [](int v) { menu::combo_r_min_stacks = v; });
        r_menu->add_checkbox("kill", "R if Killable", menu::combo_r_killable,
            [](bool v) { menu::combo_r_killable = v; });
        r_menu->add_slider_int("kill_pct", "  Min Kill %", 100, 200, 5, menu::combo_r_min_kill_pct,
            [](int v) { menu::combo_r_min_kill_pct = v; });

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
        draw_menu->add_checkbox("r_dmg_bar", "R Damage Indicator", menu::draw_r_dmg_bar,
            [](bool v) { menu::draw_r_dmg_bar = v; });
        draw_menu->add_separator();
        draw_menu->add_colorpicker("col_q", "Q Color", menu::color_q,
            [](uint32_t c) { menu::color_q = c; });
        draw_menu->add_colorpicker("col_e", "E Color", menu::color_e,
            [](uint32_t c) { menu::color_e = c; });
        draw_menu->add_colorpicker("col_ovw", "Overwhelm Color", menu::color_overwhelm,
            [](uint32_t c) { menu::color_overwhelm = c; });
        draw_menu->add_colorpicker("col_r", "R EXECUTE Color", menu::color_r,
            [](uint32_t c) { menu::color_r = c; });
        draw_menu->add_colorpicker("col_r_dmg", "R Damage Bar Color", menu::color_r_dmg,
            [](uint32_t c) { menu::color_r_dmg = c; });

        g_sdk->event_manager->register_callback(event_manager::event::game_update,
            reinterpret_cast<void*>(on_update));
        g_sdk->event_manager->register_callback(event_manager::event::draw_world,
            reinterpret_cast<void*>(on_draw));
        g_sdk->event_manager->register_callback(event_manager::event::process_cast,
            reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
        g_sdk->event_manager->register_callback(event_manager::event::buff_gain,
            reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::event::buff_loss,
            reinterpret_cast<void*>(on_buff_loss));

        g_sdk->log_console("[MeowAIO] Mel module loaded");
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
            g_sdk->event_manager->unregister_callback(event_manager::event::basic_attack,
                reinterpret_cast<void*>(on_basic_attack));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_gain,
                reinterpret_cast<void*>(on_buff_gain));
            g_sdk->event_manager->unregister_callback(event_manager::event::buff_loss,
                reinterpret_cast<void*>(on_buff_loss));
        }

        s_menu = nullptr;
        s_w_reflect_menu = nullptr;
        s_w_reflect_map.clear();
        s_w_reflect_built = false;
        s_w_skip_menu = nullptr;
        s_w_skip_map.clear();
        s_w_skip_built = false;
        brilliance_stacks = 0;
        brilliance_expire = 0.f;
        eq_pending = false;
        eq_root_expire = 0.f;
        last_spell_cast_time = 0.f;

        if (g_sdk) g_sdk->log_console("[-] Mel module unloaded");
    }
}
