#pragma once

#include "../../pch.hpp"

namespace sora::db::champions::aphelios
{
    enum class weapon : uint8_t
    {
        none = 0,
        calibrum,
        severum,
        gravitum,
        infernum,
        crescendum,
    };

    struct offhand_state
    {
        weapon w = weapon::none;
        int ammo = 0;
    };

    inline constexpr int SLOT_Q = 0;
    inline constexpr int SLOT_W = 1;
    inline constexpr int SLOT_R = 3;

    inline constexpr float CALIBRUM_AA_RANGE = 650.f;
    inline constexpr float CALIBRUM_BONUS_RANGE = 1800.f;
    inline constexpr float CALIBRUM_MARK_BONUS_BASE = 15.f;
    inline constexpr float CALIBRUM_MARK_BONUS_BAD = 0.20f;
    inline constexpr float CALIBRUM_Q_DELAY = 0.4f;
    inline constexpr float CALIBRUM_Q_RANGE = 1440.f;
    inline constexpr float CALIBRUM_Q_SPEED = 1850.f;
    inline constexpr float CALIBRUM_Q_WIDTH = 60.f;
    inline constexpr float CALIBRUM_RESERVE_MANA = 60.f;

    inline constexpr float CRESCENDUM_FRAME_MARGIN = 0.25f;
    inline constexpr float CRESCENDUM_MISSILE_SPEED = 5000.f;

    inline constexpr float BE_DELAY = 0.3f;
    inline constexpr float GRA_R_ALLY_RANGE = 1500.f;

    inline constexpr float INFERNUM_AA_RANGE = 550.f;
    inline constexpr float INFERNUM_COMBO_TIMEOUT_SEC = 4.0f;
    inline constexpr float INFERNUM_Q_DELAY = 0.4f;
    inline constexpr float INFERNUM_Q_RADIUS = 200.f;
    inline constexpr float INFERNUM_Q_RANGE = 645.f;
    inline constexpr float INFERNUM_Q_SPEED = FLT_MAX;

    inline constexpr float Q_MANA_COST = 60.f;
    inline constexpr float QR_COMBO_TIMEOUT_SEC = 2.0f;
    inline constexpr float R_DELAY = 0.6f;
    inline constexpr float R_RADIUS = 300.f;
    inline constexpr float R_RANGE = 1250.f;
    inline constexpr float R_RANGE_SAFETY = 25.f;
    inline constexpr float R_SPEED = 1000.f;
    inline constexpr float SENTRY_RANGE = 475.f;
    inline constexpr int SEV_Q_MIN_HITS = 6;
    inline constexpr float SEVERUM_AA_RANGE = 550.f;
    inline constexpr float SEVERUM_COMBO_TIMEOUT_SEC = 4.0f;

    inline constexpr int DEFAULT_AMMO_HOLD_THRESHOLD = 10;
    inline constexpr size_t CALIBRUM_MARK_TRACKER_CAP = 16;
    inline constexpr size_t GRAVITUM_MARK_TRACKER_CAP = 16;

    inline constexpr uint32_t BUFF_CALIBRUM_MARK_DEBUFF = 0u;
    inline constexpr uint32_t BUFF_GRAVITUM_DEBUFF = 0u;

    inline const char* weapon_label(weapon w)
    {
        switch (w)
        {
        case weapon::calibrum: return "Calibrum";
        case weapon::severum: return "Severum";
        case weapon::gravitum: return "Gravitum";
        case weapon::infernum: return "Infernum";
        case weapon::crescendum: return "Crescendum";
        default: return "None";
        }
    }

    inline weapon parse_weapon(std::string_view name)
    {
        if (name.find("Calibrum") != std::string_view::npos) return weapon::calibrum;
        if (name.find("Severum") != std::string_view::npos) return weapon::severum;
        if (name.find("Gravitum") != std::string_view::npos) return weapon::gravitum;
        if (name.find("Infernum") != std::string_view::npos) return weapon::infernum;
        if (name.find("Crescendum") != std::string_view::npos) return weapon::crescendum;
        return weapon::none;
    }

    inline weapon detect_main_weapon(game_object* player)
    {
        if (!player)
            return weapon::none;

        auto* spell = player->get_spell(SLOT_Q);
        if (!spell || !spell->get_data() || !spell->get_data()->get_static_data())
            return weapon::none;

        return parse_weapon(spell->get_data()->get_static_data()->get_name());
    }

    inline offhand_state detect_offhand_state(game_object* player)
    {
        if (!player)
            return {};

        auto* spell = player->get_spell(SLOT_W);
        if (!spell || !spell->get_data() || !spell->get_data()->get_static_data())
            return {};

        return {
            parse_weapon(spell->get_data()->get_static_data()->get_name()),
            static_cast<int>(spell->get_charges()),
        };
    }

    inline bool contains(std::string_view name, std::string_view token)
    {
        return name.find(token) != std::string_view::npos;
    }

    inline bool is_infernum_aa_missile(std::string_view name) { return contains(name, "Infernum") && contains(name, "Attack"); }
    inline bool is_calibrum_q_missile(std::string_view name) { return contains(name, "Calibrum") && contains(name, "Q"); }
    inline bool is_r_missile(std::string_view name) { return contains(name, "Aphelios") && contains(name, "R"); }
    inline bool is_chakram_out_missile(std::string_view name) { return contains(name, "Crescendum") || contains(name, "Chakram"); }
    inline bool is_chakram_in_missile(std::string_view name) { return contains(name, "Return") || contains(name, "In"); }

    inline float cal_q_raw_damage(int level, float bonus_ad, float ap) { return 70.f + 15.f * level + 0.42f * bonus_ad + ap; }
    inline float cal_q_raw_damage(int level, float bonus_ad) { return cal_q_raw_damage(level, bonus_ad, 0.f); }
    inline float cal_mv_mark_bonus(float bonus_ad) { return CALIBRUM_MARK_BONUS_BASE + CALIBRUM_MARK_BONUS_BAD * bonus_ad; }
    inline float cal_mv_mark_bonus(int level) { return level >= 13 ? 110.f : level >= 9 ? 80.f : 50.f; }
    inline float r_base_raw_damage(int rank, float bonus_ad, float ap) { return 125.f + 50.f * rank + 0.20f * bonus_ad + ap; }
    inline float r_base_raw_damage(int level, float bonus_ad) { return r_base_raw_damage(level >= 11 ? 2 : level >= 6 ? 1 : 0, bonus_ad, 0.f); }
    inline float inf_mv_bonus_raw(int rank, float bonus_ad) { return 50.f * rank + 0.25f * bonus_ad; }
    inline float inf_q_raw_damage(int level, float bonus_ad, float ap) { return 20.f + 15.f * level + 0.10f * bonus_ad + 0.70f * ap; }
    inline float inf_q_raw_damage(int level, float bonus_ad) { return inf_q_raw_damage(level, bonus_ad, 0.f); }
    inline float sev_q_per_hit_raw_damage(int level, float bonus_ad) { return 10.f + 0.20f * bonus_ad + static_cast<float>(level); }
    inline float be_raw_damage(int level, float bonus_ad, float ap) { return 50.f + 15.f * level + 0.32f * bonus_ad + 0.70f * ap; }
}
