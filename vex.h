#pragma once

#include "sdk.hpp"

namespace vex
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
    void __fastcall on_new_path(game_object* object, bool is_dash, float dash_speed);
}
