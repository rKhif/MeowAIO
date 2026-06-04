#pragma once

#include "sdk.hpp"

namespace annie
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_draw_world();
}
