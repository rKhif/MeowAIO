#include "aphelios.h"

#include "ap/pch.hpp"
#include "ap/aphelios.hpp"
#include "ap/aphelios.cpp"
#include "ap/aphelios_calibrum.cpp"
#include "ap/aphelios_crescendum.cpp"
#include "ap/aphelios_gravitum.cpp"
#include "ap/aphelios_infernum.cpp"
#include "ap/aphelios_severum.cpp"

namespace aphelios
{
    static std::unique_ptr<sora::champions::champion_base> g_instance{};
    static bool g_loaded = false;

    static sora::champions::champion_base* instance()
    {
        return g_instance.get();
    }

    void __fastcall on_game_update()
    {
        if (!instance())
            return;

        instance()->on_game_update();

        if (!sdk::orbwalker)
            return;

        if (sdk::orbwalker->combo())
            instance()->combo();
    }

    void __fastcall on_present()
    {
        if (instance())
            instance()->on_draw();
    }

    void __fastcall on_draw_world()
    {
    }

    void __fastcall on_process_cast(game_object* caster, spell_cast* cast)
    {
        if (instance())
            instance()->on_process_cast(caster, cast);
    }

    void __fastcall on_execute_cast(game_object* caster, spell_cast* cast)
    {
        (void)caster;
        (void)cast;
    }

    void __fastcall on_basic_attack(game_object* attacker, game_object* target, spell_cast* cast)
    {
        (void)attacker;
        (void)target;
        (void)cast;
    }

    void __fastcall on_create_object(game_object* obj)
    {
        (void)obj;
    }

    void __fastcall on_delete_object(game_object* obj)
    {
        if (instance())
            instance()->on_delete_object(obj);
    }

    void __fastcall on_create_missile(game_object* missile)
    {
        if (instance())
            instance()->on_create_missile(missile);
    }

    void __fastcall on_buff_gain(game_object* target, buff_instance* buff)
    {
        if (instance())
            instance()->on_buff_gain(target, buff);
    }

    void __fastcall on_buff_loss(game_object* target, buff_instance* buff)
    {
        if (instance())
            instance()->on_buff_loss(target, buff);
    }

    void __fastcall on_new_path(game_object* object, bool is_dash, float dash_speed)
    {
        (void)object;
        (void)is_dash;
        (void)dash_speed;
    }

    bool __stdcall on_before_attack(orb_sdk::event_data* data)
    {
        return instance() ? instance()->on_before_attack(data) : true;
    }

    bool __stdcall on_before_move(orb_sdk::event_data* data)
    {
        (void)data;
        return true;
    }

    void __fastcall on_after_attack(orb_sdk::event_data* data)
    {
        if (instance())
            instance()->on_after_attack(data);
    }

    void load()
    {
        if (g_loaded)
            return;

        auto* player = g_sdk->object_manager->get_local_player();
        if (!player)
            throw std::runtime_error("Aphelios: local player unavailable");

        g_instance = sora::champions::create_aphelios(player);
        if (!g_instance)
            throw std::runtime_error("Aphelios: create_aphelios failed");

        g_instance->on_init();

        g_sdk->event_manager->register_callback(event_manager::game_update, reinterpret_cast<void*>(on_game_update));
        g_sdk->event_manager->register_callback(event_manager::present, reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->register_callback(event_manager::draw_environment, reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->register_callback(event_manager::process_cast, reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->register_callback(event_manager::execute_cast, reinterpret_cast<void*>(on_execute_cast));
        g_sdk->event_manager->register_callback(event_manager::basic_attack, reinterpret_cast<void*>(on_basic_attack));
        g_sdk->event_manager->register_callback(event_manager::create_object, reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->register_callback(event_manager::delete_object, reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->register_callback(event_manager::create_missile, reinterpret_cast<void*>(on_create_missile));
        g_sdk->event_manager->register_callback(event_manager::buff_gain, reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->register_callback(event_manager::buff_loss, reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->register_callback(event_manager::new_path, reinterpret_cast<void*>(on_new_path));

        if (sdk::orbwalker)
        {
            sdk::orbwalker->register_callback(orb_sdk::before_attack, reinterpret_cast<void*>(on_before_attack));
            sdk::orbwalker->register_callback(orb_sdk::before_move, reinterpret_cast<void*>(on_before_move));
            sdk::orbwalker->register_callback(orb_sdk::after_attack, reinterpret_cast<void*>(on_after_attack));
        }

        g_loaded = true;
    }

    void unload()
    {
        if (!g_loaded)
            return;

        g_sdk->event_manager->unregister_callback(event_manager::game_update, reinterpret_cast<void*>(on_game_update));
        g_sdk->event_manager->unregister_callback(event_manager::present, reinterpret_cast<void*>(on_present));
        g_sdk->event_manager->unregister_callback(event_manager::draw_environment, reinterpret_cast<void*>(on_draw_world));
        g_sdk->event_manager->unregister_callback(event_manager::process_cast, reinterpret_cast<void*>(on_process_cast));
        g_sdk->event_manager->unregister_callback(event_manager::execute_cast, reinterpret_cast<void*>(on_execute_cast));
        g_sdk->event_manager->unregister_callback(event_manager::basic_attack, reinterpret_cast<void*>(on_basic_attack));
        g_sdk->event_manager->unregister_callback(event_manager::create_object, reinterpret_cast<void*>(on_create_object));
        g_sdk->event_manager->unregister_callback(event_manager::delete_object, reinterpret_cast<void*>(on_delete_object));
        g_sdk->event_manager->unregister_callback(event_manager::create_missile, reinterpret_cast<void*>(on_create_missile));
        g_sdk->event_manager->unregister_callback(event_manager::buff_gain, reinterpret_cast<void*>(on_buff_gain));
        g_sdk->event_manager->unregister_callback(event_manager::buff_loss, reinterpret_cast<void*>(on_buff_loss));
        g_sdk->event_manager->unregister_callback(event_manager::new_path, reinterpret_cast<void*>(on_new_path));

        if (sdk::orbwalker)
        {
            sdk::orbwalker->unregister_callback(orb_sdk::before_attack, reinterpret_cast<void*>(on_before_attack));
            sdk::orbwalker->unregister_callback(orb_sdk::before_move, reinterpret_cast<void*>(on_before_move));
            sdk::orbwalker->unregister_callback(orb_sdk::after_attack, reinterpret_cast<void*>(on_after_attack));
        }

        g_instance.reset();
        g_loaded = false;
    }
}
