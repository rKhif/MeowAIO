#pragma once

#include "sdk.hpp"

class awareness_sdk
{
public:
    virtual float get_spell_cooldown( game_object* hero, int slot ) = 0;
};

namespace sdk
{
    inline awareness_sdk* awareness = nullptr;
}

namespace sdk_init
{
    inline bool awareness()
    {
        if( sdk::awareness )
            return true;

        const std::string module_name = ( "VEN.Awareness" );
        if( !g_sdk->add_dependency( ( "Core/" ) + module_name ) )
            return false;

        sdk::awareness   = reinterpret_cast< awareness_sdk* >( g_sdk->get_custom_sdk( module_name ) );

        return true;
    }
}