#pragma once

#include "../../sdk.hpp"
#include "../../evade_sdk.hpp"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sora::damage
{
    enum class damage_type
    {
        physical,
        magic,
        true_damage,
    };

    enum class spell_kind
    {
        normal,
        ultimate_aoe,
    };

    inline dmg_sdk::damage_type to_sdk_type(damage_type type)
    {
        switch (type)
        {
        case damage_type::magic: return dmg_sdk::damage_type::magical;
        case damage_type::true_damage: return dmg_sdk::damage_type::truedamage;
        default: return dmg_sdk::damage_type::physical;
        }
    }

    inline float auto_attack_final(game_object* source, game_object* target, float raw_bonus = 0.f)
    {
        if (!source || !target)
            return 0.f;

        const float aa = sdk::damage ? sdk::damage->get_aa_damage(source, target) : source->get_attack_damage();
        const float bonus = sdk::damage ? sdk::damage->calc_damage(dmg_sdk::damage_type::physical, source, target, raw_bonus) : raw_bonus;
        return aa + bonus;
    }

    inline bool will_kill(game_object*, game_object* target, float damage)
    {
        return target && damage >= target->get_hp() + target->get_all_shield();
    }
}
