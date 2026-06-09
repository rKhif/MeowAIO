#pragma once

#include "sdk.hpp"

namespace teemo
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_create_object(game_object* obj);
    void __fastcall on_delete_object(game_object* obj);
    void __fastcall on_new_path(game_object* obj, bool is_dash, float dash_speed);
    void __fastcall on_basic_attack(game_object* attacker, game_object* target, spell_cast* cast);
}
