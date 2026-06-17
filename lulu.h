#pragma once

#include "sdk.hpp"

namespace lulu
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_basic_attack(game_object* object, game_object* target, spell_cast* cast);
}
