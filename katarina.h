#pragma once

#include "sdk.hpp"

namespace katarina
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_create_object(game_object* obj);
    void __fastcall on_delete_object(game_object* obj);
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
    bool __stdcall on_before_attack(orb_sdk::event_data* data);
    bool __stdcall on_before_move(orb_sdk::event_data* data);
}
