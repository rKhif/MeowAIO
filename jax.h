#pragma once

#include "sdk.hpp"

namespace jax
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
}

