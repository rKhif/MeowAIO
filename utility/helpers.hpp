#pragma once

#include "sdk.hpp"
#include <string_view>

namespace venplus::utility
{

inline bool is_enemy( game_object* source, game_object* target ) noexcept
{
    if ( !source || !target ) return false;
    return source->get_team_id() != target->get_team_id();
}

inline bool is_ally( game_object* source, game_object* target ) noexcept
{
    if ( !source || !target ) return false;
    return source->get_team_id() == target->get_team_id();
}

inline bool is_enemy( game_object* target ) noexcept
{
    if ( !target || !g_sdk || !g_sdk->object_manager ) return false;
    auto* player = g_sdk->object_manager->get_local_player();
    if ( !player ) return false;
    return player->get_team_id() != target->get_team_id();
}

inline bool is_ally( game_object* target ) noexcept
{
    if ( !target || !g_sdk || !g_sdk->object_manager ) return false;
    auto* player = g_sdk->object_manager->get_local_player();
    if ( !player ) return false;
    return player->get_team_id() == target->get_team_id();
}

inline bool is_valid_target( game_object* obj ) noexcept
{
    return obj && obj->is_valid() && !obj->is_dead() && obj->is_visible() && obj->is_targetable();
}

inline bool is_valid_enemy( game_object* source, game_object* target ) noexcept
{
    if ( !source || !is_valid_target( target ) ) return false;
    return source->get_team_id() != target->get_team_id();
}

inline bool is_valid_enemy( game_object* target ) noexcept
{
    if ( !is_valid_target( target ) ) return false;
    if ( !g_sdk || !g_sdk->object_manager ) return false;
    auto* player = g_sdk->object_manager->get_local_player();
    return player && player->get_team_id() != target->get_team_id();
}

inline bool is_valid_ally( game_object* source, game_object* target ) noexcept
{
    if ( !source || !is_valid_target( target ) ) return false;
    return source->get_team_id() == target->get_team_id() && source != target;
}

inline bool is_valid_ally( game_object* target ) noexcept
{
    if ( !is_valid_target( target ) ) return false;
    if ( !g_sdk || !g_sdk->object_manager ) return false;
    auto* player = g_sdk->object_manager->get_local_player();
    return player && player->get_team_id() == target->get_team_id() && player != target;
}

inline bool is_immobile( game_object* target, float for_duration = 0.f ) noexcept
{
    if ( !target || !g_sdk || !g_sdk->clock_facade ) return false;

    float now = g_sdk->clock_facade->get_game_time();

    for ( auto* b : target->get_buffs() )
    {
        if ( !b || !b->is_active() ) continue;

        auto type = b->get_type();
        bool is_cc = type == buff_type::stun
                  || type == buff_type::snare
                  || type == buff_type::knockup
                  || type == buff_type::suppression
                  || type == buff_type::charm
                  || type == buff_type::taunt
                  || type == buff_type::fear
                  || type == buff_type::flee
                  || type == buff_type::asleep;

        if ( !is_cc ) continue;

        if ( for_duration <= 0.f )
            return true;

        if ( b->get_end_time() - now >= for_duration )
            return true;
    }
    return false;
}

} // namespace venplus::utility
