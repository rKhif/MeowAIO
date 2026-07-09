#pragma once

#include "sdk.hpp"
#include <cstdint>
#include <array>

namespace venplus::utility
{

namespace defensive_hashes
{
    inline constexpr uint32_t KINDRED_R       = 0xE9EDCF9Eu;
    inline constexpr uint32_t TRYNDAMERE_R    = 0x0u;          // TODO: verify hash in-game
    inline constexpr uint32_t KAYLE_R         = 0x0u;          // TODO: verify hash in-game
    inline constexpr uint32_t TARIC_R         = 0x0u;          // TODO: verify hash in-game
    inline constexpr uint32_t ZILEAN_R        = 0x9A270666u;
}

struct defensive_buff_entry
{
    uint32_t hash;
    bool     enabled;
};

inline constexpr std::array< defensive_buff_entry, 5 > ANTI_BURST_BUFFS = {{
    { defensive_hashes::KINDRED_R,      defensive_hashes::KINDRED_R != 0x0u },
    { defensive_hashes::TRYNDAMERE_R,   defensive_hashes::TRYNDAMERE_R != 0x0u },
    { defensive_hashes::KAYLE_R,        defensive_hashes::KAYLE_R != 0x0u },
    { defensive_hashes::TARIC_R,        defensive_hashes::TARIC_R != 0x0u },
    { defensive_hashes::ZILEAN_R,       defensive_hashes::ZILEAN_R != 0x0u },
}};

inline bool has_anti_burst_buff( game_object* target ) noexcept
{
    if ( !target )
        return false;

    for ( const auto& entry : ANTI_BURST_BUFFS )
    {
        if ( !entry.enabled )
            continue;

        auto* buff = target->get_buff_by_hash( entry.hash );
        if ( buff && buff->is_active() )
            return true;
    }

    return false;
}

inline float get_anti_burst_remaining( game_object* target ) noexcept
{
    if ( !target )
        return 0.f;

    float game_time = g_sdk->clock_facade->get_game_time();
    float longest = 0.f;

    for ( const auto& entry : ANTI_BURST_BUFFS )
    {
        if ( !entry.enabled )
            continue;

        auto* buff = target->get_buff_by_hash( entry.hash );
        if ( buff && buff->is_active() )
        {
            float remaining = buff->get_end_time() - game_time;
            if ( remaining > longest )
                longest = remaining;
        }
    }

    return ( std::max )( 0.f, longest );
}

} // namespace venplus::utility
