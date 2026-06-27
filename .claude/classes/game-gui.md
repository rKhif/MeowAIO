# Game GUI

The Game GUI provides functions for interacting with the game's chat and ping systems.

---

## Sending a Chat Message

```cpp
void g_sdk->game_gui->send_chat(std::string const& message)
```

**Example:**

```cpp
void send_status_message()
{
    g_sdk->game_gui->send_chat("Flash ready!");
}
```

---

## Sending Pings

### Ping Types

```cpp
enum class map_ping_type : uint8_t
{
    ping = 1,
    old_caution = 2,
    enemy_missing = 3,
    on_my_way = 4,
    caution = 5,
    assist_me = 6,
    enemy_has_vision = 7,
    invisible_assist_me_yellow = 8,
    invisible_assist_me_red = 9,
    game_mode_info = 10,
    game_mode_important = 11,
    game_mode_warning = 12,
    enemy_has_no_vision = 13,
    ask_for_vision = 14,
    push_forward = 15,
    all_in = 16,
    reset = 17,
    retreat = 18,
    bait = 19,
    hold_the_area = 20
};
```

### Ping at Position

```cpp
void g_sdk->game_gui->send_map_ping(math::vector3 const& position, map_ping_type type)
```

**Example:**

```cpp
void ping_danger_location(math::vector3 position)
{
    g_sdk->game_gui->send_map_ping(position, map_ping_type::caution);
}
```

### Ping on Target

```cpp
void g_sdk->game_gui->send_map_ping(game_object* target, map_ping_type type)
```

**Example:**

```cpp
void ping_enemy_missing(game_object* enemy)
{
    if (enemy && enemy->is_hero())
    {
        g_sdk->game_gui->send_map_ping(enemy, map_ping_type::enemy_missing);
    }
}
```

---

## Usage Examples

### Auto-Ping Low Health Enemies

```cpp
void auto_ping_low_enemies()
{
    auto heroes = g_sdk->object_manager->get_heroes();
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    for (auto hero : heroes)
    {
        if (!hero || !hero->is_alive()) continue;
        if (hero->get_team_id() == player->get_team_id()) continue;
        
        float hp_percent = hero->get_health() / hero->get_max_health();
        if (hp_percent < 0.3f)
        {
            g_sdk->game_gui->send_map_ping(hero, map_ping_type::assist_me);
            break; // Only ping once
        }
    }
}
```

### Ping Flash Cooldown

```cpp
float last_flash_use_time = 0.f;
const float FLASH_CD = 300.f; // 5 minutes

void check_flash_cooldown()
{
    float current_time = g_sdk->clock_facade->get_game_time();
    float time_since_flash = current_time - last_flash_use_time;
    
    if (time_since_flash >= FLASH_CD)
    {
        g_sdk->game_gui->send_chat("Flash ready");
    }
    else
    {
        int seconds_remaining = static_cast<int>(FLASH_CD - time_since_flash);
        char message[64];
        sprintf_s(message, "Flash: %ds", seconds_remaining);
        g_sdk->game_gui->send_chat(message);
    }
}
```

### Smart Vision Ping

```cpp
void ping_no_vision_areas()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    // Important bush locations
    std::vector<math::vector3> important_bushes = {
        {/* position 1 */},
        {/* position 2 */},
    };
    
    for (const auto& bush : important_bushes)
    {
        if (g_sdk->nav_mesh->is_in_fow(bush))
        {
            g_sdk->game_gui->send_map_ping(bush, map_ping_type::ask_for_vision);
            break; // Ping only once
        }
    }
}
```

### Objective Timer Announcements

```cpp
struct ObjectiveTimer
{
    std::string name;
    float spawn_time;
};

std::vector<ObjectiveTimer> objectives;

void announce_objective_spawns()
{
    float current_time = g_sdk->clock_facade->get_game_time();
    
    for (auto& obj : objectives)
    {
        float time_until_spawn = obj.spawn_time - current_time;
        
        if (time_until_spawn <= 0.f && time_until_spawn > -1.f)
        {
            char message[64];
            sprintf_s(message, "%s spawned!", obj.name.c_str());
            g_sdk->game_gui->send_chat(message);
        }
        else if (time_until_spawn <= 20.f && time_until_spawn > 19.f)
        {
            char message[64];
            sprintf_s(message, "%s in 20s", obj.name.c_str());
            g_sdk->game_gui->send_chat(message);
        }
    }
}
```

### Danger Ping on Enemy

```cpp
void warn_teammates_about_enemy(game_object* enemy)
{
    if (!enemy || !enemy->is_hero()) return;
    
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    // Check if enemy is close to allies
    auto heroes = g_sdk->object_manager->get_heroes();
    for (auto ally : heroes)
    {
        if (!ally || ally == player) continue;
        if (ally->get_team_id() != player->get_team_id()) continue;
        
        float dist = ally->get_position().distance(enemy->get_position());
        if (dist < 2000.f) // Within 2000 units
        {
            g_sdk->game_gui->send_map_ping(enemy, map_ping_type::caution);
            break;
        }
    }
}
```

### Coordinate Team Actions

```cpp
void signal_engage()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    auto cursor_pos = g_sdk->hud_manager->get_cursor_position();
    
    g_sdk->game_gui->send_map_ping(cursor_pos, map_ping_type::all_in);
    g_sdk->game_gui->send_chat("Engaging now!");
}

void signal_retreat()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    g_sdk->game_gui->send_map_ping(player->get_position(), map_ping_type::retreat);
    g_sdk->game_gui->send_chat("Fall back!");
}
```

---

## Best Practices

### ✅ Do

- Use pings sparingly to avoid spam
- Choose appropriate ping types for the situation
- Keep chat messages concise and relevant
- Add cooldowns to prevent spam

### ❌ Don't

- Don't spam pings (can annoy teammates)
- Don't send messages too frequently
- Don't use offensive or toxic messages
- Don't ping excessively during fights

---

## Ping Spam Prevention

```cpp
float last_ping_time = 0.f;
const float PING_COOLDOWN = 3.f; // 3 seconds between pings

bool can_ping()
{
    float current_time = g_sdk->clock_facade->get_game_time();
    if (current_time >= last_ping_time + PING_COOLDOWN)
    {
        last_ping_time = current_time;
        return true;
    }
    return false;
}

void safe_ping(math::vector3 position, map_ping_type type)
{
    if (can_ping())
    {
        g_sdk->game_gui->send_map_ping(position, type);
    }
}
```

---

## Related

- [Object Manager](object-manager.md) - Get targets to ping
- [Hud Manager](hud-manager.md) - Get cursor position for pings
- [Clock Facade](clock-facade.md) - Timer management
