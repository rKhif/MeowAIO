# Game Info

The Game Info class provides information about the current game mode and settings.

---

## Getting the Game Mode

```cpp
char* g_sdk->game_info->get_game_mode()
```

ℹ️ **This function returns the name of the current game mode**

**Example:**

```cpp
void __fastcall game_update()
{
    const char* mode = g_sdk->game_info->get_game_mode();
    g_sdk->log_console("Game mode: %s", mode);
}
```

---

## Usage Examples

### Mode-Specific Behavior

```cpp
bool is_ranked_game()
{
    const char* mode = g_sdk->game_info->get_game_mode();
    return strcmp(mode, "CLASSIC") == 0; // Ranked/Normal
}

bool is_aram()
{
    const char* mode = g_sdk->game_info->get_game_mode();
    return strcmp(mode, "ARAM") == 0;
}

bool is_urf()
{
    const char* mode = g_sdk->game_info->get_game_mode();
    return strcmp(mode, "URF") == 0;
}
```

### Adjust Settings Based on Mode

```cpp
void configure_for_game_mode()
{
    const char* mode = g_sdk->game_info->get_game_mode();
    
    if (strcmp(mode, "ARAM") == 0)
    {
        g_sdk->log_console("[+] ARAM detected - adjusting settings");
        // Enable more aggressive settings for ARAM
    }
    else if (strcmp(mode, "CLASSIC") == 0)
    {
        g_sdk->log_console("[+] Classic mode - using standard settings");
    }
}
```

### Display Game Mode Info

```cpp
void __fastcall present()
{
    const char* mode = g_sdk->game_info->get_game_mode();
    
    char mode_text[128];
    sprintf_s(mode_text, "Mode: %s", mode);
    
    math::vector2 position{10.f, 30.f};
    g_sdk->renderer->add_text(mode_text, 12.f, position, 0, 0xFFFFFFFF);
}
```

---

## Common Game Modes

- **CLASSIC** - Summoner's Rift (Ranked/Normal)
- **ARAM** - All Random All Mid (Howling Abyss)
- **URF** - Ultra Rapid Fire
- **PRACTICETOOL** - Practice Tool
- **TUTORIAL** - Tutorial mode
- **ONEFORALL** - One for All
- **NEXUSBLITZ** - Nexus Blitz

---

## Best Practices

### ✅ Do

- Check game mode during initialization
- Adjust module behavior based on mode
- Cache the game mode if using frequently
- Use string comparison for mode checks

### ❌ Don't

- Don't assume always Summoner's Rift
- Don't query game mode every frame
- Don't forget to handle different modes

---

## Related

- [Clock Facade](clock-facade.md) - Game timing
- [Object Manager](object-manager.md) - Game objects
