# Hud Manager

The HUD Manager provides access to cursor position and hovered target information.

---

## Getting Current Cursor Position

```cpp
math::vector3 g_sdk->hud_manager->get_cursor_position()
```

ℹ️ **This function returns the position in 3D world coordinates**

**Example:**

```cpp
void __fastcall draw_world()
{
    auto cursor_position = g_sdk->hud_manager->get_cursor_position();
    g_sdk->renderer->add_circle_3d(cursor_position, 50.f, 1.f, 0xFFFFFFFF);
}
```

---

## Getting Currently Hovered Target

```cpp
game_object* g_sdk->hud_manager->get_hovered_target()
```

ℹ️ **This function returns the currently hovered target (can be nullptr)**

**Example:**

```cpp
void __fastcall game_update()
{
    auto target = g_sdk->hud_manager->get_hovered_target();
    if (target && target->is_hero())
    {
        g_sdk->log_console("[+] Currently hovering %s", target->get_char_name().c_str());
    }
}
```

---

## Usage Examples

### Display Cursor Position

```cpp
void __fastcall present()
{
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    
    math::vector2 screen_pos = g_sdk->renderer->world_to_screen(cursor_pos);
    
    g_sdk->renderer->add_text(
        "Cursor Position",
        12.f,
        screen_pos,
        0,
        0xFFFFFFFF
    );
}
```

### Cast Spell at Cursor

```cpp
void cast_spell_at_cursor(int spell_slot)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    
    // Cast spell towards cursor
    player->cast_spell(spell_slot, cursor_pos);
}
```

### Smart Target Selection

```cpp
game_object* get_target()
{
    // First, try to get hovered target
    auto hovered = g_sdk->hud_manager->get_hovered_target();
    if (hovered && hovered->is_hero() && hovered->is_alive())
    {
        const auto player = g_sdk->object_manager->get_local_player();
        if (player && hovered->get_team_id() != player->get_team_id())
        {
            return hovered; // Return enemy hovered target
        }
    }
    
    // Fall back to automatic target selection
    return find_best_target();
}
```

### Draw Line to Cursor

```cpp
void __fastcall draw_world()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    auto player_pos = player->get_position();
    
    // Calculate distance
    float distance = player_pos.distance(cursor_pos);
    
    // Draw line on minimap
    math::vector2 minimap_player = g_sdk->renderer->world_to_minimap(player_pos);
    math::vector2 minimap_cursor = g_sdk->renderer->world_to_minimap(cursor_pos);
    
    g_sdk->renderer->add_line_2d(minimap_player, minimap_cursor, 2.f, 0xFFFFFF00);
}
```

### Cursor Distance Check

```cpp
bool is_cursor_in_range(float range)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return false;
    
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    auto player_pos = player->get_position();
    
    float distance = player_pos.distance(cursor_pos);
    return distance <= range;
}
```

### Hover Information Display

```cpp
void __fastcall present()
{
    auto target = g_sdk->hud_manager->get_hovered_target();
    if (!target) return;
    
    // Get screen position for target
    math::vector2 screen_pos = g_sdk->renderer->world_to_screen(target->get_position());
    screen_pos.y -= 50.f; // Offset above target
    
    // Display info
    char info[256];
    sprintf_s(info, "%s\nHP: %.0f/%.0f", 
        target->get_char_name().c_str(),
        target->get_health(),
        target->get_max_health()
    );
    
    g_sdk->renderer->add_text(info, 14.f, screen_pos, 1, 0xFFFFFFFF);
}
```

### Area Cast with Cursor

```cpp
void cast_area_spell(int spell_slot, float radius)
{
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    
    // Count enemies in area
    int enemy_count = 0;
    auto heroes = g_sdk->object_manager->get_heroes();
    
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    for (auto hero : heroes)
    {
        if (!hero || !hero->is_alive()) continue;
        if (hero->get_team_id() == player->get_team_id()) continue;
        
        float dist = cursor_pos.distance(hero->get_position());
        if (dist <= radius)
            enemy_count++;
    }
    
    // Cast if multiple enemies
    if (enemy_count >= 2)
    {
        player->cast_spell(spell_slot, cursor_pos);
        g_sdk->log_console("[+] Cast spell on %d enemies!", enemy_count);
    }
}
```

---

## Best Practices

### ✅ Do

- Check for `nullptr` when getting hovered target
- Use cursor position for manual spell targeting
- Combine with renderer to show cursor-related visuals
- Cache cursor position if using it multiple times per frame

### ❌ Don't

- Don't assume hovered target is always valid
- Don't forget to check team ID on hovered targets
- Don't use cursor position for automatic combo logic (use prediction instead)
- Don't query cursor position excessively (cache it)

---

## Related

- [Renderer](renderer.md) - Draw at cursor position
- [Object Manager](object-manager.md) - Validate hovered targets
- [Prediction](../interfaces/prediction.md) - Better than cursor for automatic spell casting
