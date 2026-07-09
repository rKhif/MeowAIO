
#include <Windows.h>
#include <unordered_map>
#include <unordered_set>
#include "sdk.hpp"
#include "evade_sdk.hpp"
#include "MeowV1/viktor.h"
#include "MeowV1/veigar.h"
#include "MeowV1/zaahen.h"
#include "MeowV1/syndra.h"
#include "MeowV1/orianna.h"
#include "MeowV1/shyvana.h"
#include "MeowV1/vex.h"
#include "MeowV1/annie.h"
#include "MeowV1/katarina.h"
#include "MeowV1/hwei.h"
#include "MeowV1/twitch.h"
#include "MeowV1/soraka.h"
#include "MeowV1/thresh.h"
#include "MeowV1/ziggs.h"


extern "C" __declspec(dllexport) int SDKVersion = SDK_VERSION;

static std::string s_loaded_champion;


static const std::unordered_set<std::string> g_locked_champions = {
    "Shyvana"
};

static const std::unordered_map<std::string, std::unordered_set<std::string>> g_user_allowlist = {
    // Dev
    { "khif", { "*" } },
    { "losszx", { "*" } },
    { "gaspack", { "*" } },
    { "trixsterfaze", { "*" } },
    { "xxtimexx", { "*" } },
    { "banshee", { "*" } },



    // Per-champion grants (examples — fill in real VEN usernames):
    // { "friend_one", { "Syndra", "Ahri" } },
    // { "friend_two", { "Orianna" } },
};

static bool is_user_allowed(const std::string& username, const std::string& champion)
{
    // Open champion — anyone can use
    if (g_locked_champions.find(champion) == g_locked_champions.end())
        return true;

    auto it = g_user_allowlist.find(username);
    if (it == g_user_allowlist.end())
        return false;

    // "*" wildcard grants all locked champions
    if (it->second.count("*"))
        return true;

    return it->second.count(champion) > 0;
}

bool init_sdk_component(const char* name, bool (*init_func)(), const char* fallback_msg = nullptr)
{
    if (init_func())
    {
        g_sdk->log_console("[+] %s initialized!", name);
        return true;
    }
    else
    {
        g_sdk->log_console("[!] %s not available%s", name, fallback_msg ? fallback_msg : "");
        return false;
    }
}

extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;

    // Initialize SDK
    init_sdk_component("Orbwalker", sdk_init::orbwalker, " (will use VEN's built-in)");
    init_sdk_component("Target Selector", sdk_init::target_selector);
    init_sdk_component("Prediction", sdk_init::prediction);
    init_sdk_component("Damage Calculator", sdk_init::damage);
    init_sdk_component("Common SDK", sdk_init::common);
    init_sdk_component("Infotab", sdk_init::infotab);
    init_sdk_component("Health Prediction", sdk_init::health_prediction);
    init_sdk_component("Shaders", sdk_init::shaders);
    init_sdk_component("Awareness", sdk_init::awareness);
    init_sdk_component("Spell Manager", sdk_init::spell_manager);
    init_sdk_component("Evade", sdk_init::evade);

    g_sdk->log_console("[+] Module loaded successfully!");


    auto player = g_sdk->object_manager->get_local_player();
    if (!player)
    {
        g_sdk->log_console("[!] No local player found!");
        return false;
    }

    std::string champion_name = player->get_char_name();
    s_loaded_champion = champion_name;

    // Username-based access control (locked champs only)
    std::string username = g_sdk->get_username();
    if (!is_user_allowed(username, champion_name))
    {
        g_sdk->log_console("[MeowAIO] '%s' is locked >.<'' (user: %s)",
            champion_name.c_str(), username.c_str());
        s_loaded_champion.clear();
        return false;
    }

    if (champion_name == "Viktor")
    {
        try
        {
            viktor::load();
            g_sdk->log_console("[MeowAIO] MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Viktor MEOW!");
            return false;
        }
    }
    else if (champion_name == "Veigar")
    {
        try
        {
            veigar::load();
            g_sdk->log_console("[MeowAIO] Veigar loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Veigar MEOW!");
            return false;
        }
    }
    else if (champion_name == "Zaahen")
    {
        try
        {
            zaahen::load();
            g_sdk->log_console("[MeowAIO] Zaahen loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Zaahen MEOW!");
            return false;
        }
    }
    else if (champion_name == "Syndra")
    {
        try
        {
            syndra::load();
            g_sdk->log_console("[MeowAIO] Syndra loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Syndra MEOW!");
            return false;
        }
    }
    else if (champion_name == "Orianna")
    {
        try
        {
            orianna::load();
            g_sdk->log_console("[MeowAIO] Orianna loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Orianna MEOW!");
            return false;
        }
    }
    else if (champion_name == "Shyvana")
    {
        try
        {
            shyvana::load();
            g_sdk->log_console("[MeowAIO] Shyvana loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Shyvana MEOW!");
            return false;
        }
    }
    else if (champion_name == "Vex")
    {
        try
        {
            vex::load();
            g_sdk->log_console("[MeowAIO] Vex loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Vex MEOW!");
            return false;
        }
    }
    else if (champion_name == "Annie")
    {
        try
        {
            annie::load();
            g_sdk->log_console("[MeowAIO] Annie loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Annie MEOW!");
            return false;
        }
    }
    else if (champion_name == "Katarina")
    {
        try
        {
            katarina::load();
            g_sdk->log_console("[MeowAIO] Katarina loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Katarina MEOW!");
            return false;
        }
    }
    else if (champion_name == "Hwei")
    {
        try
        {
            hwei::load();
            g_sdk->log_console("[MeowAIO] Hwei loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Hwei MEOW!");
            return false;
        }
    }
    else if (champion_name == "Twitch")
    {
        try
        {
            twitch::load();
            g_sdk->log_console("[MeowAIO] Twitch loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Twitch MEOW!");
            return false;
        }
    }
    else if (champion_name == "Soraka")
    {
        try
        {
            soraka::load();
            g_sdk->log_console("[MeowAIO] Soraka loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Soraka MEOW!");
            return false;
        }
    }
    else if (champion_name == "Thresh")
    {
        try
        {
            thresh::load();
            g_sdk->log_console("[MeowAIO] Thresh loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Thresh MEOW!");
            return false;
        }
    }
    else if (champion_name == "Ziggs")
    {
        try
        {
            ziggs::load();
            g_sdk->log_console("[MeowAIO] Ziggs loaded MEOW!");
        }
        catch (...)
        {
            g_sdk->log_console("[MeowAIO] Failed to load Ziggs MEOW!");
            return false;
        }
    }
    else
    {
        g_sdk->log_console("[MeowAIO] Champion not supported >.<'' (Current: %s)", champion_name.c_str());
        g_sdk->log_console("[MeowAIO] Supported champions: Viktor, Veigar, Zaahen, Syndra, Orianna, Shyvana, Vex, Annie, Katarina, Hwei, Twitch, Soraka, Thresh, Ziggs");
        return false;
    }

    return true;
}

extern "C" __declspec(dllexport) void PluginUnload()
{

    if (s_loaded_champion == "Viktor")
    {
        viktor::unload();
        g_sdk->log_console("[MeowAIO] Viktor unloaded successfully!");
    }
    else if (s_loaded_champion == "Veigar")
    {
        veigar::unload();
        g_sdk->log_console("[MeowAIO] Veigar unloaded successfully!");
    }
    else if (s_loaded_champion == "Zaahen")
    {
        zaahen::unload();
        g_sdk->log_console("[MeowAIO] Zaahen unloaded successfully!");
    }
    else if (s_loaded_champion == "Syndra")
    {
        syndra::unload();
        g_sdk->log_console("[MeowAIO] Syndra unloaded successfully!");
    }
    else if (s_loaded_champion == "Orianna")
    {
        orianna::unload();
        g_sdk->log_console("[MeowAIO] Orianna unloaded successfully!");
    }
    else if (s_loaded_champion == "Shyvana")
    {
        shyvana::unload();
        g_sdk->log_console("[MeowAIO] Shyvana unloaded successfully!");
    }
    else if (s_loaded_champion == "Vex")
    {
        vex::unload();
        g_sdk->log_console("[MeowAIO] Vex unloaded successfully!");
    }
    else if (s_loaded_champion == "Annie")
    {
        annie::unload();
        g_sdk->log_console("[MeowAIO] Annie unloaded successfully!");
    }
    else if (s_loaded_champion == "Katarina")
    {
        katarina::unload();
        g_sdk->log_console("[MeowAIO] Katarina unloaded successfully!");
    }
    else if (s_loaded_champion == "Hwei")
    {
        hwei::unload();
        g_sdk->log_console("[MeowAIO] Hwei unloaded successfully!");
    }
    else if (s_loaded_champion == "Twitch")
    {
        twitch::unload();
        g_sdk->log_console("[MeowAIO] Twitch unloaded successfully!");
    }
    else if (s_loaded_champion == "Soraka")
    {
        soraka::unload();
        g_sdk->log_console("[MeowAIO] Soraka unloaded successfully!");
    }
    else if (s_loaded_champion == "Thresh")
    {
        thresh::unload();
        g_sdk->log_console("[MeowAIO] Thresh unloaded successfully!");
    }
    else if (s_loaded_champion == "Ziggs")
    {
        ziggs::unload();
        g_sdk->log_console("[MeowAIO] Ziggs unloaded successfully!");
    }

    sdk::orbwalker = nullptr;
    sdk::target_selector = nullptr;
    sdk::prediction = nullptr;
    sdk::damage = nullptr;
    sdk::common = nullptr;
    sdk::infotab = nullptr;
    sdk::health_prediction = nullptr;
    sdk::shaders = nullptr;
    sdk::awareness = nullptr;
    sdk::spell_manager = nullptr;
    sdk::evade = nullptr;

    s_loaded_champion.clear();
    g_sdk->log_console("[-] Module unloaded!");
}
