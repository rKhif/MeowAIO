# Nav Mesh

The Nav Mesh provides pathfinding and fog of war information.

---

## Checking if Terrain is Pathable

```cpp
bool g_sdk->nav_mesh->is_pathable(math::vector3& position)
```

ℹ️ **This function returns whether or not a terrain position is a valid path target**

**Example:**

```cpp
void __fastcall game_update()
{
    auto cursor_position = g_sdk->hud_manager->get_cursor_position();
    if (!g_sdk->nav_mesh->is_pathable(cursor_position))
    {
        g_sdk->log_console("[+] You cannot path here!");
    }
}
```

---

## Checking if Position is in FOW

```cpp
bool g_sdk->nav_mesh->is_in_fow(math::vector3& position)
```

ℹ️ **This function returns whether or not a world position is in Fog of War**

**Example:**

```cpp
void __fastcall game_update()
{
    auto cursor_position = g_sdk->hud_manager->get_cursor_position();
    if (g_sdk->nav_mesh->is_in_fow(cursor_position))
    {
        g_sdk->log_console("[+] Position is in FOW");
    }
}
```

---

## Checking if Position is in FOW for a Specific Team

```cpp
bool g_sdk->nav_mesh->is_in_fow_for_team(math::vector3 const& position, int team_id)
```

```cpp
bool g_sdk->nav_mesh->is_in_fow_ex(math::vector3 const& position, int team_id, bool skip_minions, bool skip_heroes)
```

**Example:**

```cpp
void __fastcall game_update()
{
    auto cursor_position = g_sdk->hud_manager->get_cursor_position();
    if (g_sdk->nav_mesh->is_in_fow_for_team(cursor_position, 100))
    {
        g_sdk->log_console("[+] Position is in FOW for team 100");
    }
}
```

---

## Usage Examples

### Validate Spell Cast Position

```cpp
bool is_valid_cast_position(math::vector3 position)
{
    // Check if position is pathable (not in wall)
    if (!g_sdk->nav_mesh->is_pathable(position))
    {
        g_sdk->log_console("[!] Cannot cast spell into wall");
        return false;
    }
    
    return true;
}
```

### Check Enemy Vision

```cpp
bool is_position_safe_from_enemies(math::vector3 position)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return false;
    
    int enemy_team = player->get_team_id() == 100 ? 200 : 100;
    
    // Check if enemies can see this position
    if (!g_sdk->nav_mesh->is_in_fow_for_team(position, enemy_team))
    {
        g_sdk->log_console("[?] Enemies have vision of this position!");
        return false;
    }
    
    return true;
}
```

### Smart Ward Placement

```cpp
bool is_good_ward_spot(math::vector3 position)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return false;
    
    // Must be pathable
    if (!g_sdk->nav_mesh->is_pathable(position))
        return false;
    
    int enemy_team = player->get_team_id() == 100 ? 200 : 100;
    
    // Currently in enemy FOW (we don't have vision)
    if (!g_sdk->nav_mesh->is_in_fow_for_team(position, player->get_team_id()))
    {
        g_sdk->log_console("[+] Good ward spot - no allied vision");
        return true;
    }
    
    return false;
}
```

### Dash Safety Check

```cpp
bool is_dash_safe(math::vector3 target_position)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return false;
    
    // Check if target is pathable
    if (!g_sdk->nav_mesh->is_pathable(target_position))
    {
        g_sdk->log_console("[!] Cannot dash into wall");
        return false;
    }
    
    // Check if we have vision
    if (g_sdk->nav_mesh->is_in_fow(target_position))
    {
        g_sdk->log_console("[?] Dashing into fog of war - risky!");
        return false;
    }
    
    return true;
}
```

### Jungle Gank Detection

```cpp
void check_jungle_entrances()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return;
    
    int enemy_team = player->get_team_id() == 100 ? 200 : 100;
    
    // List of jungle entrance positions
    std::vector<math::vector3> jungle_entrances = {
        {/* position 1 */},
        {/* position 2 */},
        // ... more positions
    };
    
    for (const auto& entrance : jungle_entrances)
    {
        // Check if we have vision of entrance
        if (g_sdk->nav_mesh->is_in_fow_for_team(entrance, player->get_team_id()))
        {
            g_sdk->log_console("[?] No vision of jungle entrance!");
        }
    }
}
```

### Escape Path Validation

```cpp
math::vector3 find_safe_escape_position(float distance)
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (!player) return player->get_position();
    
    auto player_pos = player->get_position();
    
    // Try multiple directions
    for (int angle = 0; angle < 360; angle += 30)
    {
        float rad = angle * (3.14159f / 180.f);
        math::vector3 test_pos = player_pos;
        test_pos.x += cos(rad) * distance;
        test_pos.z += sin(rad) * distance;
        
        // Check if position is pathable
        if (g_sdk->nav_mesh->is_pathable(test_pos))
        {
            // Check if position is safer (not in enemy vision)
            int enemy_team = player->get_team_id() == 100 ? 200 : 100;
            if (g_sdk->nav_mesh->is_in_fow_for_team(test_pos, enemy_team))
            {
                return test_pos; // Found safe position
            }
        }
    }
    
    return player_pos; // No safe position found
}
```

---

## Best Practices

### ✅ Do

- Always check if position is pathable before movement/casting
- Use FOW checks for safety/ward placement decisions
- Consider team FOW for stealth operations
- Cache results if checking same position multiple times

### ❌ Don't

- Don't assume all positions are walkable
- Don't forget to check FOW before risky plays
- Don't spam FOW checks (they can be expensive)
- Don't confuse team IDs (100 = blue, 200 = red)

---

## Team IDs

- **100** - Blue team (bottom-left)
- **200** - Red team (top-right)

---

## Related

- [Hud Manager](hud-manager.md) - Get cursor position to check
- [Object Manager](object-manager.md) - Get object positions
- [Evade](../interfaces/evade.md) - Uses pathfinding for dodging
