#include "lulu.h"

#include "orb_sdk.hpp"
#include "pred_sdk.hpp"
#include "dmg_sdk.hpp"
#include "damage/items.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace lulu
{
namespace
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    constexpr float Q_RANGE  = 950.f;
    constexpr float Q_RADIUS = 60.f;
    constexpr float Q_DELAY  = 0.25f;
    constexpr float Q_SPEED  = 1450.f;

    constexpr float W_RANGE  = 650.f;
    constexpr float W_DELAY  = 0.2419f;

    constexpr float E_RANGE  = 650.f;

    constexpr float R_RANGE  = 950.f;
    constexpr float R_RADIUS = 400.f;

    constexpr float E_SHIELD_BASE[7]   = { 0.f, 75.f, 115.f, 155.f, 195.f, 235.f, 235.f };
    constexpr float E_SHIELD_RATIO     = 0.55f;
    constexpr float E_DAMAGE_BASE[7]   = { 0.f, 80.f, 120.f, 160.f, 200.f, 240.f, 280.f };
    constexpr float E_DAMAGE_RATIO     = 0.5f;

    // Tracked AD carry buffs (champion name -> { buff fragment, display, default mask }).
    // Mask bit 0 -> use W on this buff, bit 1 -> use E on this buff.
    struct tracked_buff
    {
        const char* champion;
        const char* buff_fragment;
        const char* display;
        int default_mask;
    };

    constexpr std::array<tracked_buff, 10> TRACKED_BUFFS = { {
        { "Ashe",   "asheqattack",                "Ashe [Q] Buff",       0b10 },
        { "Twitch", "twitchhideinshadowsbuff",    "Twitch [Q] AS Buff",  0b10 },
        { "Twitch", "twitchfullautomatic",        "Twitch [R]",          0b11 },
        { "Jinx",   "jinxpassivekill",            "Jinx [P]",            0b11 },
        { "Zeri",   "zerir",                      "Zeri [R]",            0b01 },
        { "KogMaw", "kogmawbioarcanebarrage",     "Kog'Maw [W]",         0b11 },
        { "Vayne",  "vayneinquisiton",            "Vayne [R]",           0b10 },
        { "Samira", "samirar",                    "Samira [R]",          0b00 },
        { "Xayah",  "xayahw",                     "Xayah [W]",           0b10 },
        { "Kaisa",  "kaisaeattackspeed",          "Kai'Sa [E] AS Buff",  0b11 },
    } };

    // Champion role lists (simplified static classification).
    const std::unordered_set<std::string>& marksman_set()
    {
        static const std::unordered_set<std::string> s = {
            "Aphelios","Ashe","Caitlyn","Corki","Draven","Ezreal","Jhin","Jinx",
            "Kaisa","Kalista","KogMaw","Lucian","MissFortune","Nilah","Quinn",
            "Samira","Senna","Sivir","Smolder","Tristana","Twitch","Varus",
            "Vayne","Xayah","Zeri",
        };
        return s;
    }

    const std::unordered_set<std::string>& fighter_set()
    {
        static const std::unordered_set<std::string> s = {
            "Aatrox","Belveth","Camille","Darius","Diana","Fiora","Gangplank",
            "Garen","Gnar","Gragas","Graves","Hecarim","Illaoi","Irelia","Jax",
            "JarvanIV","Jayce","Kayn","Kled","LeeSin","MonkeyKing","Mordekaiser",
            "Nasus","Olaf","Pantheon","Rengar","Renekton","Riven","Sett","Shyvana",
            "Skarner","Trundle","Tryndamere","Udyr","Urgot","Vi","Viego","Volibear",
            "Warwick","XinZhao","Yasuo","Yone","Yorick",
        };
        return s;
    }

    const std::unordered_set<std::string>& burst_set()
    {
        static const std::unordered_set<std::string> s = {
            "Ahri","Akali","Akshan","Annie","Brand","Diana","Ekko","Evelynn",
            "Fizz","Hwei","Kassadin","Katarina","KhaZix","LeBlanc","Lissandra",
            "Lux","Mel","Neeko","Nidalee","Pyke","Qiyana","Rumble","Shaco",
            "Syndra","Talon","Veigar","Vex","Viktor","Xerath","Yasuo","Yone",
            "Zed","Ziggs","Zoe",
        };
        return s;
    }

    const std::unordered_set<std::string>& support_set()
    {
        static const std::unordered_set<std::string> s = {
            "Alistar","Bard","Blitzcrank","Braum","Janna","Karma","Leona",
            "Lulu","Milio","Morgana","Nami","Nautilus","Pyke","Rakan","Rell",
            "Renata","Senna","Seraphine","Sona","Soraka","TahmKench","Taric",
            "Thresh","Yuumi","Zilean","Zyra",
        };
        return s;
    }

    const std::unordered_set<std::string>& mage_set()
    {
        static const std::unordered_set<std::string> s = {
            "Anivia","Annie","AurelionSol","Brand","Cassiopeia","Ekko","Heimerdinger",
            "Karthus","Lissandra","Lux","Malzahar","Mel","Morgana","Neeko","Orianna",
            "Ryze","Seraphine","Swain","Syndra","Taliyah","TwistedFate","Veigar",
            "Velkoz","Vex","Viktor","Vladimir","Xerath","Zoe","Ziggs",
        };
        return s;
    }

    bool is_marksman(game_object* o)  { return o && marksman_set().count(o->get_char_name()) > 0; }
    bool is_fighter(game_object* o)   { return o && fighter_set().count(o->get_char_name()) > 0; }
    bool is_burst(game_object* o)     { return o && burst_set().count(o->get_char_name()) > 0; }
    bool is_support(game_object* o)   { return o && support_set().count(o->get_char_name()) > 0; }
    bool is_mage(game_object* o)      { return o && mage_set().count(o->get_char_name()) > 0; }

    // -----------------------------------------------------------------------
    // STATE
    // -----------------------------------------------------------------------
    menu_category* s_menu = nullptr;
    game_object*   s_pix  = nullptr;

    struct aa_timer
    {
        float    time = 0.f;
        uint32_t target_id = 0;
    };

    // Two most recent AAs per ally hero (network id keyed) for chase / buff logic.
    std::unordered_map<uint32_t, std::array<aa_timer, 2>> s_last_aa_on_hero;
    float s_last_cast[4] = { 0.f, 0.f, 0.f, 0.f };

    // -----------------------------------------------------------------------
    // MENU STATE
    // -----------------------------------------------------------------------
    namespace menu
    {
        // Q
        inline bool combo_q          = true;
        inline bool combo_q_from_pix = true;
        inline int  combo_q_hitchance = pred_sdk::hitchance::high;

        // W
        inline bool combo_w_speed     = true;
        inline bool combo_w_poly      = true;
        inline bool chase_logic       = true;
        inline bool poly_superiority  = true;
        inline bool poly_marksman     = true;
        inline bool poly_bruiser      = false;
        inline bool poly_burst        = true;
        inline bool poly_melee        = true;
        inline bool antigap_w         = true;
        inline int  cancel_w_min_imp  = 1;  // reserved (no spell_manager wiring yet)

        // E
        inline bool combo_e_shield    = true;
        inline bool combo_e_damage    = true;
        inline bool block_damage      = true;
        inline int  helia_stacks      = 2;
        inline std::unordered_map<uint32_t, bool> shield_on_ally;
        // tracked buff bitmask per (champion, fragment) key.
        struct buff_key
        {
            std::string champion;
            std::string fragment;
            bool operator==(const buff_key& o) const
            {
                return champion == o.champion && fragment == o.fragment;
            }
        };
        struct buff_key_hash
        {
            size_t operator()(const buff_key& k) const noexcept
            {
                return std::hash<std::string>()(k.champion) ^ (std::hash<std::string>()(k.fragment) << 1);
            }
        };
        inline std::unordered_map<buff_key, int, buff_key_hash> buff_mask;

        // R
        inline bool combo_r            = true;
        inline bool r_save_ally        = true;
        inline bool r_antigap          = true;
        inline int  r_aoe_hits         = 3;
        inline std::unordered_map<uint32_t, bool> r_on_ally;

        // Drawings
        inline bool draw_q          = true;
        inline bool draw_w          = false;
        inline bool draw_e          = false;
        inline bool draw_r          = false;
        inline bool draw_only_ready = true;
        inline uint32_t color_q     = 0x80B8C8FFu;
        inline uint32_t color_w     = 0x8090FFC0u;
        inline uint32_t color_e     = 0x80C8A0FFu;
        inline uint32_t color_r     = 0x80FFB060u;
    }

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
        return p->get_par() >= p->get_spell_mana_cost(slot);
    }

    int spell_level(int slot)
    {
        auto* p = player();
        if (!p) return 0;
        auto* s = p->get_spell(slot);
        return s ? s->get_level() : 0;
    }

    bool can_cast(int slot, float min_interval = 0.18f)
    {
        if (slot < 0 || slot >= 4) return false;
        const float now = game_time();
        return now <= 0.f || now - s_last_cast[slot] >= min_interval;
    }

    void mark_cast(int slot)
    {
        if (slot < 0 || slot >= 4) return;
        s_last_cast[slot] = game_time();
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

    bool is_valid_ally(game_object* o, float range = FLT_MAX, bool include_self = true)
    {
        auto* p = player();
        if (!p || !o || !o->is_valid() || o->is_dead()) return false;
        if (o->get_team_id() != p->get_team_id()) return false;
        if (!include_self && o->get_network_id() == p->get_network_id()) return false;
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

    std::string lowercase(std::string v)
    {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return v;
    }

    bool has_buff_fragment(game_object* o, const std::string& fragment_lower)
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

    int enemies_in_range(const math::vector3& pos, float range)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0;
        int count = 0;
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (is_valid_enemy(h) && h->get_position().distance(pos) <= range)
                ++count;
        }
        return count;
    }

    int allies_in_range(const math::vector3& pos, float range, bool include_self = true)
    {
        if (!g_sdk || !g_sdk->object_manager) return 0;
        auto* p = player();
        int count = 0;
        for (auto* h : g_sdk->object_manager->get_heroes())
        {
            if (!h || !h->is_valid() || h->is_dead()) continue;
            if (p && h->get_team_id() != p->get_team_id()) continue;
            if (!include_self && p && h->get_network_id() == p->get_network_id()) continue;
            if (h->get_position().distance(pos) <= range) ++count;
        }
        return count;
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

    void find_pix()
    {
        if (s_pix && s_pix->is_valid() && !s_pix->is_dead()) return;
        s_pix = nullptr;

        auto* p = player();
        if (!p || !g_sdk || !g_sdk->object_manager) return;

        for (auto* m : g_sdk->object_manager->get_minions())
        {
            if (!m || !m->is_valid() || m->is_dead()) continue;
            if (m->get_team_id() != p->get_team_id()) continue;
            const std::string name = lowercase(m->get_name());
            if (name.find("robotbuddy") == std::string::npos) continue;
            if (m->get_owner() == p || m->get_attacher() == p)
            {
                s_pix = m;
                return;
            }
        }
    }

    bool ally_has_aa_within(uint32_t network_id, float seconds, uint32_t* out_target = nullptr)
    {
        auto it = s_last_aa_on_hero.find(network_id);
        if (it == s_last_aa_on_hero.end()) return false;
        const auto& [t1, t2] = it->second;
        const auto& most_recent = (t1.time > t2.time) ? t1 : t2;
        if (game_time() - most_recent.time > seconds) return false;
        if (out_target) *out_target = most_recent.target_id;
        return true;
    }

    // -----------------------------------------------------------------------
    // SPELL DAMAGE / SHIELD CALCS
    // -----------------------------------------------------------------------
    float compute_e_shield_raw()
    {
        auto* p = player();
        if (!p) return 0.f;
        const int lvl = std::clamp(spell_level(SLOT_E), 0, 6);
        const float base = E_SHIELD_BASE[lvl] + E_SHIELD_RATIO * p->get_ability_power();
        // heal/shield power isn't exposed in native_sdk; approximate to 1.0x.
        return base;
    }

    float compute_e_damage(game_object* target)
    {
        auto* p = player();
        if (!p || !target) return 0.f;
        const int lvl = std::clamp(spell_level(SLOT_E), 0, 6);
        const float raw = E_DAMAGE_BASE[lvl] + E_DAMAGE_RATIO * p->get_ability_power();
        if (sdk::damage)
        {
            const float distance = p->get_position().distance(target->get_position());
            const bool horizon = distance >= meow::item_damage::horizon_focus::HYPERSHOT_RANGE;
            meow::item_damage::ability_proc_context items{};
            items.horizon_focus_active = horizon;
            meow::item_damage::ability_rune_context runes{};
            runes.cast_distance = distance;
            const float base = sdk::damage->calc_damage(dmg_sdk::damage_type::magical, p, target, raw);
            return meow::item_damage::apply_magic_damage_modifiers(p, target, base, 0, horizon)
                + meow::item_damage::on_ability_damage(p, target, items)
                + meow::item_damage::on_ability_rune_damage(p, target, runes);
        }
        return raw;
    }

    float effective_hp(game_object* o)
    {
        if (!o) return FLT_MAX;
        return o->get_hp() + o->get_all_shield() + o->get_magical_shield();
    }

    // -----------------------------------------------------------------------
    // Q - GLITTERLANCE
    // -----------------------------------------------------------------------
    pred_sdk::pred_data predict_q_from(const math::vector3& origin, game_object* target, int hitchance)
    {
        pred_sdk::spell_data sd{};
        sd.spell_type         = pred_sdk::spell_type::linear;
        sd.targetting_type    = pred_sdk::targetting_type::center_to_edge;
        sd.spell_slot         = SLOT_Q;
        sd.range              = Q_RANGE;
        sd.radius             = Q_RADIUS;
        sd.delay              = Q_DELAY;
        sd.projectile_speed   = Q_SPEED;
        sd.expected_hitchance = hitchance;
        sd.source_position    = origin;
        sd.forbidden_collisions = {
            pred_sdk::collision_type::yasuo_wall,
            pred_sdk::collision_type::braum_wall,
        };

        pred_sdk::pred_data result{};
        try { result = sdk::prediction->predict(target, sd); }
        catch (...) { result.is_valid = false; }
        return result;
    }

    bool try_q()
    {
        if (!menu::combo_q || !sdk::common || !sdk::prediction) return false;
        if (!spell_ready(SLOT_Q) || !has_mana(SLOT_Q)) return false;
        if (!can_cast(SLOT_Q)) return false;
        if (sdk::orbwalker && sdk::orbwalker->would_cancel_attack()) return false;

        auto* p = player();
        if (!p) return false;

        const auto self_pos = p->get_position();
        auto* target_self = pick_hero_target(Q_RANGE);

        pred_sdk::pred_data best{};
        best.is_valid = false;

        if (target_self)
        {
            auto pred = predict_q_from(self_pos, target_self, menu::combo_q_hitchance);
            if (pred.is_valid && pred.hitchance >= menu::combo_q_hitchance
                && (!best.is_valid || pred.hitchance > best.hitchance))
            {
                best = pred;
            }
        }

        if (menu::combo_q_from_pix && s_pix && s_pix->is_valid() && !s_pix->is_dead())
        {
            const auto pix_pos = s_pix->get_position();
            // Pick a target reachable from Pix (within Pix-centered Q_RANGE around self isn't ideal,
            // but enemies within Q_RANGE from Pix can be hit by a Pix-origin Q).
            for (auto* h : sdk::target_selector->get_sorted_heroes())
            {
                if (!is_valid_enemy(h)) continue;
                if (pix_pos.distance(h->get_position()) > Q_RANGE) continue;
                auto pred = predict_q_from(pix_pos, h, menu::combo_q_hitchance);
                if (pred.is_valid && pred.hitchance >= menu::combo_q_hitchance
                    && (!best.is_valid || pred.hitchance > best.hitchance))
                {
                    best = pred;
                }
            }
        }

        if (!best.is_valid) return false;

        if (sdk::common->cast_spell(SLOT_Q, best.cast_position))
        {
            mark_cast(SLOT_Q);
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // W - WHIMSY
    // -----------------------------------------------------------------------
    bool can_polymorph(game_object* client)
    {
        if (!menu::combo_w_poly) return false;
        if (!client || !is_valid_enemy(client, W_RANGE)) return false;
        if (is_unstoppable(client) || has_spell_shield(client)) return false;
        if (client->has_buff_of_type(buff_type::polymorph)) return false;

        auto* p = player();
        if (!p) return false;
        const auto self_pos = p->get_position();

        if (menu::poly_superiority)
        {
            if (allies_in_range(self_pos, 1250.f, true) > enemies_in_range(self_pos, 1250.f))
                return true;
        }

        if (menu::poly_melee && client->is_melee())
        {
            const float range = client->get_attack_range() + client->get_bounding_radius() + 100.f;
            if (allies_in_range(client->get_position(), range, false) > 0)
                return true;
        }

        if (is_marksman(client) || is_fighter(client))
        {
            uint32_t last_target = 0;
            if (!ally_has_aa_within(client->get_network_id(), 1.0f, &last_target))
                return false;
            return is_marksman(client) ? menu::poly_marksman : menu::poly_bruiser;
        }

        if (is_burst(client))
            return menu::poly_burst;

        return false;
    }

    bool try_w_polymorph()
    {
        if (!menu::combo_w_poly || !sdk::common) return false;
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        if (!can_cast(SLOT_W)) return false;

        for (auto* h : sdk::target_selector->get_sorted_heroes())
        {
            if (!can_polymorph(h)) continue;
            if (sdk::common->cast_spell(SLOT_W, h))
            {
                mark_cast(SLOT_W);
                return true;
            }
        }
        return false;
    }

    bool try_w_carry_buff(game_object* ally)
    {
        if (!menu::combo_w_speed) return false;
        if (!ally || !spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        if (!is_valid_ally(ally, W_RANGE, false)) return false;
        if (!ally_has_aa_within(ally->get_network_id(), 1.0f)) return false;
        if (is_support(ally) || is_mage(ally)) return false;

        for (const auto& tb : TRACKED_BUFFS)
        {
            if (tb.champion != ally->get_char_name()) continue;
            const menu::buff_key key{ tb.champion, tb.buff_fragment };
            auto it = menu::buff_mask.find(key);
            const int mask = (it == menu::buff_mask.end()) ? tb.default_mask : it->second;
            if (!(mask & 0b01)) continue;
            if (has_buff_fragment(ally, tb.buff_fragment) && sdk::common->cast_spell(SLOT_W, ally))
            {
                mark_cast(SLOT_W);
                return true;
            }
        }
        return false;
    }

    bool try_w_antigap(game_object* enemy)
    {
        if (!menu::antigap_w || !sdk::common) return false;
        if (!enemy || !spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        if (!is_valid_enemy(enemy, W_RANGE)) return false;
        if (!enemy->is_dashing()) return false;
        if (is_unstoppable(enemy) || has_spell_shield(enemy)) return false;
        // Kalista / Yasuo dash exclusions in source.
        const std::string name = enemy->get_char_name();
        if (name == "Kalista" || name == "Yasuo") return false;

        auto* p = player();
        if (!p) return false;
        if (p->get_position().distance(enemy->get_position()) > W_RANGE) return false;

        if (sdk::common->cast_spell(SLOT_W, enemy))
        {
            mark_cast(SLOT_W);
            return true;
        }
        return false;
    }

    bool try_w_chase(game_object* ally)
    {
        if (!menu::chase_logic || !menu::combo_w_speed || !sdk::common) return false;
        if (!spell_ready(SLOT_W) || !has_mana(SLOT_W)) return false;
        if (!is_valid_ally(ally, W_RANGE, false)) return false;
        if (is_support(ally) || is_mage(ally)) return false;

        // Did this ally AA an enemy hero recently?
        uint32_t target_id = 0;
        if (!ally_has_aa_within(ally->get_network_id(), 1.0f, &target_id) || target_id == 0)
            return false;
        if (!g_sdk || !g_sdk->object_manager) return false;
        auto* enemy = g_sdk->object_manager->get_object_by_network_id(target_id);
        if (!enemy || enemy->is_dead() || !enemy->is_targetable() || !enemy->is_hero())
            return false;

        const float aa_range = ally->get_attack_range() + ally->get_bounding_radius();
        const float dist = ally->get_position().distance(enemy->get_position());
        if (dist <= aa_range + 30.f) return false;
        if (dist > aa_range + 600.f) return false;

        if (sdk::common->cast_spell(SLOT_W, ally))
        {
            mark_cast(SLOT_W);
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // E - HELP, PIX!
    // -----------------------------------------------------------------------
    bool can_shield_ally(game_object* ally)
    {
        if (!menu::combo_e_shield) return false;
        if (!ally || !is_valid_ally(ally, E_RANGE)) return false;
        auto it = menu::shield_on_ally.find(ally->get_network_id());
        return it == menu::shield_on_ally.end() || it->second;
    }

    bool try_e_shield(game_object* ally)
    {
        if (!sdk::common) return false;
        if (!can_shield_ally(ally)) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        if (!can_cast(SLOT_E)) return false;

        // Carry buff scenario - shield carries with their key buff active.
        if (ally_has_aa_within(ally->get_network_id(), 1.0f))
        {
            for (const auto& tb : TRACKED_BUFFS)
            {
                if (tb.champion != ally->get_char_name()) continue;
                const menu::buff_key key{ tb.champion, tb.buff_fragment };
                auto it = menu::buff_mask.find(key);
                const int mask = (it == menu::buff_mask.end()) ? tb.default_mask : it->second;
                if (!(mask & 0b10)) continue;
                if (has_buff_fragment(ally, tb.buff_fragment) && sdk::common->cast_spell(SLOT_E, ally))
                {
                    mark_cast(SLOT_E);
                    return true;
                }
            }
        }

        // Block damage: if any enemy AA / spell can kill or chunk the ally.
        if (menu::block_damage)
        {
            const float shield = compute_e_shield_raw();
            const float hp     = ally->get_hp();
            // Approximate incoming danger: enemy hero in AA-range of ally,
            // count their AA + at least one ready spell.
            float danger = 0.f;
            for (auto* enemy : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(enemy)) continue;
                const float ed = enemy->get_position().distance(ally->get_position());
                const float threat_range = enemy->get_attack_range() + enemy->get_bounding_radius() + ally->get_bounding_radius() + 50.f;
                if (ed > threat_range) continue;
                danger += enemy->get_attack_damage();
                if (sdk::damage)
                    danger += sdk::damage->get_spell_damage(enemy, ally, SLOT_Q);
            }
            if (danger >= hp || shield * 0.5f <= danger)
            {
                if (sdk::common->cast_spell(SLOT_E, ally))
                {
                    mark_cast(SLOT_E);
                    return true;
                }
            }
        }

        // Echoes of Helia: shield to proc stacks (no direct buff hash exposed; skip if no info).
        return false;
    }

    bool try_e_damage(game_object* enemy)
    {
        if (!menu::combo_e_damage || !sdk::common) return false;
        if (!enemy || !is_valid_enemy(enemy, E_RANGE)) return false;
        if (!spell_ready(SLOT_E) || !has_mana(SLOT_E)) return false;
        if (!can_cast(SLOT_E)) return false;
        if (sdk::orbwalker && !sdk::orbwalker->combo()) return false;

        const float dmg = compute_e_damage(enemy);
        const float shield_value = compute_e_shield_raw();

        // Compare E's damage on the enemy vs. its shield value on the most
        // valuable nearby ally (use player as a fallback proxy for the ally pool).
        auto* p = player();
        game_object* best_ally = p;
        if (g_sdk && g_sdk->object_manager && p)
        {
            for (auto* ally : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_ally(ally, E_RANGE)) continue;
                if (!(is_marksman(ally) || is_burst(ally))) continue;
                best_ally = ally;
                break;
            }
        }

        const float dmg_ratio    = dmg          / std::max(1.f, enemy->get_max_hp());
        const float shield_ratio = best_ally ? (shield_value / std::max(1.f, best_ally->get_max_hp())) : 0.f;
        if (shield_ratio > dmg_ratio)
            return false;

        if (sdk::common->cast_spell(SLOT_E, enemy))
        {
            mark_cast(SLOT_E);
            return true;
        }
        return false;
    }

    // -----------------------------------------------------------------------
    // R - WILD GROWTH
    // -----------------------------------------------------------------------
    bool can_ult_on(game_object* ally)
    {
        if (!menu::combo_r) return false;
        if (!ally || !is_valid_ally(ally, R_RANGE)) return false;
        auto it = menu::r_on_ally.find(ally->get_network_id());
        return it == menu::r_on_ally.end() || it->second;
    }

    bool try_r_on_ally(game_object* ally)
    {
        if (!sdk::common) return false;
        if (!can_ult_on(ally)) return false;
        if (!spell_ready(SLOT_R) || !has_mana(SLOT_R)) return false;
        if (!can_cast(SLOT_R, 0.25f)) return false;

        const auto ally_pos = ally->get_position();

        // Save ally on low HP and enemy threats nearby.
        if (menu::r_save_ally)
        {
            const bool in_combat = sdk::orbwalker && sdk::orbwalker->combo();
            const int enemies_nearby = enemies_in_range(ally_pos, 800.f);
            if (in_combat && ally->get_hp() <= ally->get_max_hp() * 0.25f && enemies_nearby > 0)
            {
                if (sdk::common->cast_spell(SLOT_R, ally))
                {
                    mark_cast(SLOT_R);
                    return true;
                }
            }
        }

        // AOE R: knock up X enemies near the ally.
        if (enemies_in_range(ally_pos, R_RADIUS) >= menu::r_aoe_hits)
        {
            if (sdk::common->cast_spell(SLOT_R, ally))
            {
                mark_cast(SLOT_R);
                return true;
            }
        }

        // Anti gap-close: R to ally if a dashing enemy is on top of them.
        if (menu::r_antigap)
        {
            for (auto* enemy : g_sdk->object_manager->get_heroes())
            {
                if (!is_valid_enemy(enemy)) continue;
                if (!enemy->is_dashing()) continue;
                if (is_unstoppable(enemy) || has_spell_shield(enemy)) continue;
                if (enemy->get_position().distance(ally_pos) > 400.f) continue;
                if (sdk::common->cast_spell(SLOT_R, ally))
                {
                    mark_cast(SLOT_R);
                    return true;
                }
            }
        }

        return false;
    }

    // -----------------------------------------------------------------------
    // FRAME PROCESSING
    // -----------------------------------------------------------------------
    void process_allies()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        for (auto* ally : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_ally(ally, R_RANGE, false)) continue;
            try_r_on_ally(ally);
            try_e_shield(ally);
            try_w_carry_buff(ally);
            if (sdk::orbwalker && sdk::orbwalker->combo())
                try_w_chase(ally);
        }
    }

    void process_enemies()
    {
        if (!g_sdk || !g_sdk->object_manager) return;
        auto* p = player();
        if (!p) return;
        const auto self_pos = p->get_position();
        for (auto* enemy : g_sdk->object_manager->get_heroes())
        {
            if (!is_valid_enemy(enemy)) continue;
            if (enemy->get_position().distance(self_pos) > W_RANGE + 200.f) continue;
            try_w_antigap(enemy);
            try_e_damage(enemy);
        }
    }

    // -----------------------------------------------------------------------
    // MENU SETUP
    // -----------------------------------------------------------------------
    void setup_menu()
    {
        if (!g_sdk || !g_sdk->menu_manager) return;

        s_menu = g_sdk->menu_manager->add_category("meow_lulu", "MeowAIO - Lulu");
        if (!s_menu)
        {
            g_sdk->log_console("[!] Lulu: failed to create menu category");
            return;
        }

        if (auto* qm = s_menu->add_sub_category("q_glitterlance", "Q - Glitterlance"))
        {
            qm->add_checkbox("combo", "Use Q in Combo", menu::combo_q,
                [](bool v) { menu::combo_q = v; });
            qm->add_checkbox("from_pix", "Allow Q from Pix", menu::combo_q_from_pix,
                [](bool v) { menu::combo_q_from_pix = v; });
            qm->add_slider_int("hitchance", "Hitchance %", 0, 100, 5, menu::combo_q_hitchance,
                [](int v) { menu::combo_q_hitchance = v; });
            qm->add_separator();
            qm->add_checkbox("draw_range", "Draw Q Range", menu::draw_q,
                [](bool v) { menu::draw_q = v; });
        }

        if (auto* wm = s_menu->add_sub_category("w_whimsy", "W - Whimsy"))
        {
            wm->add_checkbox("speed", "Use W (Speed Boost)", menu::combo_w_speed,
                [](bool v) { menu::combo_w_speed = v; });
            wm->add_checkbox("poly", "Use W (Polymorph)", menu::combo_w_poly,
                [](bool v) { menu::combo_w_poly = v; });
            wm->add_checkbox("chase", "Use W for Ally to Catch Up", menu::chase_logic,
                [](bool v) { menu::chase_logic = v; });
            wm->add_separator();
            wm->add_label("Polymorph Targets");
            wm->add_checkbox("superiority", "Numerical Superiority", menu::poly_superiority,
                [](bool v) { menu::poly_superiority = v; });
            wm->add_checkbox("marksman", "Polymorph ADCs", menu::poly_marksman,
                [](bool v) { menu::poly_marksman = v; });
            wm->add_checkbox("bruiser", "Polymorph Bruisers", menu::poly_bruiser,
                [](bool v) { menu::poly_bruiser = v; });
            wm->add_checkbox("burst", "Polymorph Burst / Assassins", menu::poly_burst,
                [](bool v) { menu::poly_burst = v; });
            wm->add_checkbox("melee", "Polymorph Melees Near Ally", menu::poly_melee,
                [](bool v) { menu::poly_melee = v; });
            wm->add_separator();
            wm->add_checkbox("antigap", "W to Anti-Gap Close", menu::antigap_w,
                [](bool v) { menu::antigap_w = v; });
            wm->add_separator();
            wm->add_checkbox("draw_range", "Draw W Range", menu::draw_w,
                [](bool v) { menu::draw_w = v; });
        }

        if (auto* em = s_menu->add_sub_category("e_help_pix", "E - Help, Pix!"))
        {
            em->add_checkbox("shield", "Use E (Shield Allies)", menu::combo_e_shield,
                [](bool v) { menu::combo_e_shield = v; });
            em->add_checkbox("damage", "Use E (Damage Enemies)", menu::combo_e_damage,
                [](bool v) { menu::combo_e_damage = v; });
            em->add_checkbox("block_damage", "Shield Allies Taking Damage", menu::block_damage,
                [](bool v) { menu::block_damage = v; });
            em->add_slider_int("helia", "Shield on Helia Stacks >=", 1, 3, 1, menu::helia_stacks,
                [](int v) { menu::helia_stacks = v; });
            em->add_separator();
            if (auto* sm = em->add_sub_category("shield_on", "Shield Allies"))
            {
                auto* p = player();
                if (p && g_sdk->object_manager)
                {
                    for (auto* h : g_sdk->object_manager->get_heroes())
                    {
                        if (!h || !h->is_valid() || h->get_team_id() != p->get_team_id()) continue;
                        const uint32_t id = h->get_network_id();
                        menu::shield_on_ally[id] = true;
                        sm->add_checkbox(
                            "shield_" + std::to_string(id),
                            h->get_char_name(),
                            true,
                            [id](bool v) { menu::shield_on_ally[id] = v; });
                    }
                }
            }
            em->add_separator();
            em->add_checkbox("draw_range", "Draw E Range", menu::draw_e,
                [](bool v) { menu::draw_e = v; });
        }

        if (auto* rm = s_menu->add_sub_category("r_wild_growth", "R - Wild Growth"))
        {
            rm->add_checkbox("combo", "Allow R Usage", menu::combo_r,
                [](bool v) { menu::combo_r = v; });
            rm->add_checkbox("save", "R to Save Ally", menu::r_save_ally,
                [](bool v) { menu::r_save_ally = v; });
            rm->add_checkbox("antigap", "R to Anti-Gap Close", menu::r_antigap,
                [](bool v) { menu::r_antigap = v; });
            rm->add_slider_int("aoe", "R if hits >= #", 0, 5, 1, menu::r_aoe_hits,
                [](int v) { menu::r_aoe_hits = v; });
            rm->add_separator();
            if (auto* sm = rm->add_sub_category("r_on", "R On Allies"))
            {
                auto* p = player();
                if (p && g_sdk->object_manager)
                {
                    for (auto* h : g_sdk->object_manager->get_heroes())
                    {
                        if (!h || !h->is_valid() || h->get_team_id() != p->get_team_id()) continue;
                        const uint32_t id = h->get_network_id();
                        menu::r_on_ally[id] = true;
                        sm->add_checkbox(
                            "r_" + std::to_string(id),
                            h->get_char_name(),
                            true,
                            [id](bool v) { menu::r_on_ally[id] = v; });
                    }
                }
            }
            rm->add_separator();
            rm->add_checkbox("draw_range", "Draw R Range", menu::draw_r,
                [](bool v) { menu::draw_r = v; });
        }

        if (auto* bm = s_menu->add_sub_category("buff_tracking", "Buff Tracking (W/E on Carry Buffs)"))
        {
            // Only build entries for buffs whose champion is actually in the game.
            if (g_sdk->object_manager)
            {
                std::unordered_set<std::string> champs_in_game;
                for (auto* h : g_sdk->object_manager->get_heroes())
                {
                    if (h && h->is_valid()) champs_in_game.insert(h->get_char_name());
                }
                for (const auto& tb : TRACKED_BUFFS)
                {
                    if (!champs_in_game.count(tb.champion)) continue;
                    const menu::buff_key key{ tb.champion, tb.buff_fragment };
                    menu::buff_mask[key] = tb.default_mask;
                    const std::string id = std::string("buff_") + tb.champion + "_" + tb.buff_fragment;
                    bm->add_checkbox(
                        id + "_w",
                        std::string(tb.display) + " - Use W",
                        (tb.default_mask & 0b01) != 0,
                        [key](bool v) {
                            auto& m = menu::buff_mask[key];
                            m = v ? (m | 0b01) : (m & ~0b01);
                        });
                    bm->add_checkbox(
                        id + "_e",
                        std::string(tb.display) + " - Use E",
                        (tb.default_mask & 0b10) != 0,
                        [key](bool v) {
                            auto& m = menu::buff_mask[key];
                            m = v ? (m | 0b10) : (m & ~0b10);
                        });
                }
            }
        }

        if (auto* dm = s_menu->add_sub_category("drawings", "Drawings"))
        {
            dm->add_checkbox("only_ready", "Only Ready Spells", menu::draw_only_ready,
                [](bool v) { menu::draw_only_ready = v; });
            dm->add_separator();
            dm->add_colorpicker("col_q", "Q Color", menu::color_q,
                [](uint32_t c) { menu::color_q = c; });
            dm->add_colorpicker("col_w", "W Color", menu::color_w,
                [](uint32_t c) { menu::color_w = c; });
            dm->add_colorpicker("col_e", "E Color", menu::color_e,
                [](uint32_t c) { menu::color_e = c; });
            dm->add_colorpicker("col_r", "R Color", menu::color_r,
                [](uint32_t c) { menu::color_r = c; });
        }
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

    find_pix();

    // Always-on ally support actions.
    process_allies();

    // Reactive enemy actions (anti-gap, e damage).
    process_enemies();

    // Combo-only offensive actions.
    if (sdk::orbwalker && sdk::orbwalker->combo())
    {
        try_w_polymorph();
        try_q();
    }
}

void __fastcall on_draw()
{
    // No special HUD pieces beyond range circles.
}

void __fastcall on_draw_world()
{
    auto* p = player();
    if (!p || p->is_dead()) return;

    const auto pos = p->get_position();
    auto draw = [&](float range, uint32_t color, int slot)
    {
        if (color == 0u || range <= 0.f) return;
        if (!spell_learned(slot)) return;
        if (menu::draw_only_ready && !spell_ready(slot)) return;
        if (sdk::shaders)
            sdk::shaders->draw_world_circle(pos, range, 2.f, color);
        else if (g_sdk && g_sdk->renderer)
            g_sdk->renderer->add_circle_3d(pos, range, 1.f, color);
    };

    if (menu::draw_q) draw(Q_RANGE, menu::color_q, SLOT_Q);
    if (menu::draw_w) draw(W_RANGE, menu::color_w, SLOT_W);
    if (menu::draw_e) draw(E_RANGE, menu::color_e, SLOT_E);
    if (menu::draw_r) draw(R_RANGE, menu::color_r, SLOT_R);
}

void __fastcall on_basic_attack(game_object* object, game_object* target, spell_cast* /*cast*/)
{
    if (!object || !target) return;
    if (!object->is_hero() || !target->is_hero()) return;

    auto* p = player();
    if (!p) return;

    // Only track allied attacks on enemy heroes; don't track our own.
    if (object->get_team_id() != p->get_team_id()) return;
    if (object->get_network_id() == p->get_network_id()) return;
    if (target->get_team_id() == p->get_team_id()) return;

    const uint32_t ally_id = object->get_network_id();
    auto& timers = s_last_aa_on_hero[ally_id];

    // Replace the older slot.
    auto& slot = (timers[0].time <= timers[1].time) ? timers[0] : timers[1];
    slot.time = game_time();
    slot.target_id = target->get_network_id();
}

void load()
{
    auto* p = player();
    if (!p || p->get_char_name() != "Lulu")
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
        g_sdk->event_manager->register_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
    }

    g_sdk->log_console("[MeowAIO] Lulu loaded MEOW!");
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
        g_sdk->event_manager->unregister_callback(event_manager::event::basic_attack,
            reinterpret_cast<void*>(on_basic_attack));
    }

    s_menu = nullptr;
    s_pix = nullptr;
    s_last_aa_on_hero.clear();
    for (float& v : s_last_cast) v = 0.f;
    menu::shield_on_ally.clear();
    menu::r_on_ally.clear();
    menu::buff_mask.clear();

    if (g_sdk) g_sdk->log_console("[MeowAIO] Lulu unloaded successfully!");
}

} // namespace lulu
