#pragma once

#include "sdk.hpp"

namespace aurora
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw_world();
    bool __stdcall on_after_attack(orb_sdk::event_data* data);
}
