# Object Manager

The Object Manager provides access to all game objects such as champions, minions, turrets, and more.

---

## Getting Local Player

```cpp
game_object* g_sdk->object_manager->get_local_player()
```

ℹ️ **While in replay mode - this function returns the currently selected player; otherwise returns the controlled player**

**Example:**

```cpp
void __fastcall game_update()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player)
    {
        g_sdk->log_console("Player: %s | HP: %.0f", 
            player->get_char_name().c_str(),
            player->get_health());
    }
}
```

---

## Getting Objects by Type

The Object Manager provides convenience methods to get lists of specific object types:

### Turrets

```cpp
std::span<game_object*> g_sdk->object_manager->get_turrets()
```

### Heroes

```cpp
std::span<game_object*> g_sdk->object_manager->get_heroes()
```

### Minions

```cpp
std::span<game_object*> g_sdk->object_manager->get_minions()
```

### Nexuses

```cpp
std::span<game_object*> g_sdk->object_manager->get_nexuses()
```

### Inhibitors

```cpp
std::span<game_object*> g_sdk->object_manager->get_inhibitors()
```

### Monsters (Jungle Camps)

```cpp
std::span<game_object*> g_sdk->object_manager->get_monsters()
```

### Traps

```cpp
std::span<game_object*> g_sdk->object_manager->get_traps()
```

### Wards

```cpp
std::span<game_object*> g_sdk->object_manager->get_wards()
```

### Plants

```cpp
std::span<game_object*> g_sdk->object_manager->get_plants()
```

---

## Getting Object by Network ID

```cpp
game_object* g_sdk->object_manager->get_object_by_network_id(uint32_t network_id)
```

---

## Usage Examples

### Iterating Heroes

```cpp
void __fastcall game_update()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    // Get all heroes
    auto heroes = g_sdk->object_manager->get_heroes();
    
    for (auto hero : heroes)
    {
        if (!hero || hero == player) continue;
        
        // Check if enemy
        if (hero->get_team_id() != player->get_team_id())
        {
            float distance = player->get_position().distance(hero->get_position());
            g_sdk->log_console("Enemy %s at distance %.0f", 
                hero->get_char_name().c_str(), 
                distance);
        }
    }
}
```

### Finding Nearest Enemy

```cpp
game_object* get_nearest_enemy(float max_range)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return nullptr;
    
    game_object* nearest = nullptr;
    float nearest_dist = max_range;
    
    auto heroes = g_sdk->object_manager->get_heroes();
    for (auto hero : heroes)
    {
        if (!hero || hero == player) continue;
        if (hero->get_team_id() == player->get_team_id()) continue;
        if (!hero->is_alive()) continue;
        
        float dist = player->get_position().distance(hero->get_position());
        if (dist < nearest_dist)
        {
            nearest = hero;
            nearest_dist = dist;
        }
    }
    
    return nearest;
}
```

### Counting Minions in Range

```cpp
int count_minions_in_range(math::vector3 position, float range)
{
    int count = 0;
    
    auto minions = g_sdk->object_manager->get_minions();
    for (auto minion : minions)
    {
        if (!minion || !minion->is_alive()) continue;
        
        float dist = position.distance(minion->get_position());
        if (dist <= range)
            count++;
    }
    
    return count;
}
```

### Finding Specific Object by Name

```cpp
game_object* find_object_by_name(const char* name)
{
    auto heroes = g_sdk->object_manager->get_heroes();
    for (auto hero : heroes)
    {
        if (hero && hero->get_char_name() == name)
            return hero;
    }
    return nullptr;
}
```

---

## Common Object Methods

Once you have a `game_object*`, you can call various methods:

### Identification

```cpp
bool is_hero()
bool is_minion()
bool is_turret()
bool is_monster()
bool is_alive()
```

### Position & Movement

```cpp
math::vector3 get_position()
math::vector3 get_server_position()
bool is_moving()
float get_move_speed()
```

### Stats

```cpp
float get_health()
float get_max_health()
float get_mana()
float get_max_mana()
int get_team_id()
```

### Combat

```cpp
float get_attack_range()
float get_attack_damage()
float get_armor()
float get_magic_resist()
float get_bounding_radius()
```

### Information

```cpp
std::string get_name()
std::string get_char_name()
uint32_t get_network_id()
```

---

## Best Practices

### ✅ Do

- Always check for `nullptr` before using objects
- Check `is_alive()` before interacting with units
- Cache objects when iterating multiple times per frame
- Use appropriate object type getter for your needs

### ❌ Don't

- Don't iterate all objects every frame if not needed
- Don't assume objects are always valid
- Don't forget to check team ID for ally/enemy distinction
- Don't hold onto object pointers indefinitely (they can be deleted)

---

## Related

- [Events](../main/events.md) - `create_object`, `delete_object`
- [Renderer](renderer.md) - Drawing around objects
- [Nav Mesh](nav-mesh.md) - Pathfinding for objects
