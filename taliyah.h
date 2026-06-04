#pragma once

#include "sdk.hpp"

namespace taliyah
{
    void load();
    void unload();

    void __fastcall on_update();
    void __fastcall on_draw();
    void __fastcall on_process_cast(game_object* obj, spell_cast* cast);
    void __fastcall on_create_object(game_object* obj);
    void __fastcall on_delete_object(game_object* obj);
}
