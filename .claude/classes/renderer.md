# Renderer

The Renderer provides functions for drawing 2D and 3D elements on screen.

---

## 2D Shapes

### Adding a 2D Circle

```cpp
void g_sdk->renderer->add_circle_2d(math::vector2& position, float radius, float thickness, uint32_t color)
```

**Example:**

```cpp
void __fastcall present()
{
    const auto width = g_sdk->renderer->get_window_width();
    const auto height = g_sdk->renderer->get_window_height();
    math::vector2 screen_center{
        static_cast<float>(width) * 0.5f, 
        static_cast<float>(height) * 0.5f
    };

    g_sdk->renderer->add_circle_2d(screen_center, 20.f, 1.f, 0xFFFFFFFF);
}
```

### Adding a Filled 2D Circle

```cpp
void g_sdk->renderer->add_circle_filled_2d(math::vector2& position, float radius, uint32_t color)
```

**Example:**

```cpp
void __fastcall present()
{
    const auto width = g_sdk->renderer->get_window_width();
    const auto height = g_sdk->renderer->get_window_height();
    math::vector2 screen_center{
        static_cast<float>(width) * 0.5f, 
        static_cast<float>(height) * 0.5f
    };

    g_sdk->renderer->add_circle_filled_2d(screen_center, 20.f, 0xFFFF0000);
}
```

---

## 3D Shapes

### Adding a 3D Circle

```cpp
void g_sdk->renderer->add_circle_3d(math::vector3& position, float radius, float thickness, uint32_t color)
```

**Example:**

```cpp
void __fastcall draw_world()
{
    auto cursor_position = g_sdk->hud_manager->get_cursor_position();
    g_sdk->renderer->add_circle_3d(cursor_position, 50.f, 1.f, 0xFFFFFFFF);
}
```

---

## Rectangles

### Adding a Rectangle

```cpp
void g_sdk->renderer->add_rectangle(math::rect const& rectangle, uint32_t color, float thickness)
```

### Adding a Filled Rectangle

```cpp
void g_sdk->renderer->add_rectangle_filled(math::rect const& rectangle, uint32_t color)
```

**Example:**

```cpp
void __fastcall present()
{
    math::rect box{100.f, 100.f, 200.f, 50.f}; // x, y, width, height
    
    // Filled background
    g_sdk->renderer->add_rectangle_filled(box, 0x80000000); // Semi-transparent black
    
    // Border
    g_sdk->renderer->add_rectangle(box, 0xFFFFFFFF, 2.f);
}
```

---

## Text

### Adding Text

```cpp
void g_sdk->renderer->add_text(std::string const& text, float size, math::vector2 position, uint32_t flags, uint32_t color)
```

**Flags:**
- `0` - Left aligned
- `1` - Centered

**Example:**

```cpp
void __fastcall present()
{
    const auto width = g_sdk->renderer->get_window_width();
    const auto height = g_sdk->renderer->get_window_height();
    math::vector2 screen_center{
        static_cast<float>(width) * 0.5f, 
        static_cast<float>(height) * 0.5f
    };

    g_sdk->renderer->add_text("Hello!", 12.f, screen_center, 1, 0xFFFFFFFF);
}
```

---

## Lines

### Adding a 2D Line

```cpp
void g_sdk->renderer->add_line_2d(math::vector2& point1, math::vector2& point2, float thickness, uint32_t color)
```

**Example:**

```cpp
void __fastcall present()
{
    math::vector2 start{100.f, 100.f};
    math::vector2 end{200.f, 200.f};
    
    g_sdk->renderer->add_line_2d(start, end, 2.f, 0xFFFF0000);
}
```

---

## Sprites

### Adding a Sprite

```cpp
void g_sdk->renderer->add_sprite(void* sprite, math::vector2 const& position, uint32_t color, math::vector2 const& scale = {1.f, 1.f})
```

ℹ️ **Color should be 0xFFFFFFFF for the sprite to maintain the original color**

**Example:**

```cpp
void __fastcall present()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player)
    {
        const auto icon = player->get_icon_square();
        if (icon)
        {
            const auto width = g_sdk->renderer->get_window_width();
            const auto height = g_sdk->renderer->get_window_height();
            const math::vector2 screen_center{
                static_cast<float>(width) * 0.5f, 
                static_cast<float>(height) * 0.5f
            };

            g_sdk->renderer->add_sprite(icon, screen_center, 0xFFFFFFFF);
        }
    }
}
```

---

## Minimap

### Adding a Minimap Circle

```cpp
void g_sdk->renderer->add_circle_minimap(math::vector3 const& position, float radius, float thickness, uint32_t color)
```

**Example:**

```cpp
void __fastcall present()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player)
    {
        g_sdk->renderer->add_circle_minimap(player->get_position(), 30.f, 1.f, 0xFFFFFFFF);
    }
}
```

---

## Position Conversion

### World to Screen

```cpp
math::vector2 g_sdk->renderer->world_to_screen(math::vector3 world_position)
```

**Example:**

```cpp
void __fastcall present()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player)
    {
        math::vector2 screen_pos = g_sdk->renderer->world_to_screen(player->get_position());
        g_sdk->renderer->add_text("You", 14.f, screen_pos, 1, 0xFFFFFFFF);
    }
}
```

### World to Minimap

```cpp
math::vector2 g_sdk->renderer->world_to_minimap(math::vector3 world_position)
```

**Example:**

```cpp
void __fastcall present()
{
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    math::vector2 minimap_pos = g_sdk->renderer->world_to_minimap(cursor_pos);
    
    g_sdk->renderer->add_circle_2d(minimap_pos, 5.f, 1.f, 0xFFFFFF00);
}
```

---

## Indicators

### Damage/Heal/Shield Indicators

```cpp
void g_sdk->renderer->add_damage_indicator(game_object* object, float damage);
void g_sdk->renderer->add_heal_indicator(game_object* object, float heal);
void g_sdk->renderer->add_shield_indicator(game_object* object, float shield);
```

ℹ️ **The argument must be the actual damage/heal/shield value**

**Example:**

```cpp
void __fastcall present()
{
    auto heroes = g_sdk->object_manager->get_heroes();
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    for (auto hero : heroes)
    {
        if (!hero || !hero->is_alive()) continue;
        if (hero->get_team_id() == player->get_team_id()) continue;
        
        // Calculate Q damage
        float q_damage = calculate_q_damage(player, hero);
        
        // Show damage indicator
        g_sdk->renderer->add_damage_indicator(hero, q_damage);
    }
}
```

---

## Window Information

### Get Window Dimensions

```cpp
int g_sdk->renderer->get_window_width()
int g_sdk->renderer->get_window_height()
```

**Example:**

```cpp
void __fastcall present()
{
    int width = g_sdk->renderer->get_window_width();
    int height = g_sdk->renderer->get_window_height();
    
    g_sdk->log_console("Window: %dx%d", width, height);
}
```

---

## Color Format

Colors use the format: `0xAARRGGBB`
- **AA** - Alpha (transparency): `00` = fully transparent, `FF` = fully opaque
- **RR** - Red component
- **GG** - Green component
- **BB** - Blue component

### Common Colors

```cpp
0xFFFFFFFF  // White
0xFF000000  // Black
0xFFFF0000  // Red
0xFF00FF00  // Green
0xFF0000FF  // Blue
0xFFFFFF00  // Yellow
0xFF00FFFF  // Cyan
0xFFFF00FF  // Magenta

// Semi-transparent
0x80FFFFFF  // 50% transparent white
0x80FF0000  // 50% transparent red
```

---

## Complete Example: Spell Range Display

```cpp
menu_category* g_menu = nullptr;

void __fastcall draw_world()
{
    if (!g_menu->get_bool("draw.enabled")) return;
    
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    auto player_pos = player->get_position();
    
    // Q range
    if (g_menu->get_bool("draw.q_range"))
    {
        uint32_t q_color = g_menu->get_int("draw.q_color");
        float q_range = g_menu->get_int("spells.q_range");
        g_sdk->renderer->add_circle_3d(player_pos, q_range, 2.f, q_color);
    }
    
    // W range
    if (g_menu->get_bool("draw.w_range"))
    {
        uint32_t w_color = g_menu->get_int("draw.w_color");
        float w_range = g_menu->get_int("spells.w_range");
        g_sdk->renderer->add_circle_3d(player_pos, w_range, 2.f, w_color);
    }
}
```

---

## Best Practices

### ✅ Do

- Use `present` event for 2D UI elements
- Use `draw_world` event for 3D world elements
- Cache color values from menu
- Use appropriate alpha for overlays
- Convert world positions to screen for text display

### ❌ Don't

- Don't draw in `game_update` (use `present` or `draw_world`)
- Don't draw excessively (performance impact)
- Don't forget alpha channel in colors
- Don't use world coordinates for 2D elements

---

## Related

- [Events](../main/events.md) - `present`, `draw_world`, `draw_environment`
- [Object Manager](object-manager.md) - Get objects to draw around
- [Hud Manager](hud-manager.md) - Get cursor position for drawing
