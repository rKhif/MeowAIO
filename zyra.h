#pragma once

#include "sdk.hpp"

namespace zyra
{
    void load();
    void unload();

    void __fastcall on_game_update();
    void __fastcall on_present();
    void __fastcall on_draw_world();
    void __fastcall on_process_cast(game_object* caster, spell_cast* cast);
    void __fastcall on_create_missile(game_object* missile);
    void __fastcall on_delete_object(game_object* obj);
    void __fastcall on_buff_gain(game_object* target, buff_instance* buff);
    void __fastcall on_buff_loss(game_object* target, buff_instance* buff);
    bool __stdcall on_before_attack(orb_sdk::event_data* data);
    void __fastcall on_after_attack(orb_sdk::event_data* data);
}
