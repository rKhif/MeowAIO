#pragma once

#include "sdk.hpp"

namespace jinx
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_buff_gain(game_object* object, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* object, buff_instance* buff);

    // Orbwalker callbacks
    bool __stdcall on_before_attack(orb_sdk::event_data* data);
}
