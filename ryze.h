#pragma once

#include "sdk.hpp"
#include "orb_sdk.hpp"

namespace ryze
{
    constexpr int SLOT_Q = 0;
    constexpr int SLOT_W = 1;
    constexpr int SLOT_E = 2;
    constexpr int SLOT_R = 3;

    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw_world();
    void __fastcall on_present();
    void __fastcall on_buff_gain(game_object* obj, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* obj, buff_instance* buff);
    bool __stdcall on_before_attack(orb_sdk::event_data* data);
}
