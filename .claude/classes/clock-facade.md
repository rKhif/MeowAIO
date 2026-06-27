# Clock Facade

The Clock Facade provides access to game time information.

---

## Getting Current Game Time

```cpp
float g_sdk->clock_facade->get_game_time()
```

ℹ️ **This function returns the game time in seconds (not real-world time)**

---

## Usage

### Basic Example

```cpp
void __fastcall game_update()
{
    float game_time = g_sdk->clock_facade->get_game_time();
    g_sdk->log_console("Game time: %.2f seconds", game_time);
}
```

### Cooldown Tracking

```cpp
float last_spell_cast_time = 0.f;
const float spell_cooldown = 10.f; // 10 seconds

void cast_spell_if_ready()
{
    float current_time = g_sdk->clock_facade->get_game_time();
    
    if (current_time >= last_spell_cast_time + spell_cooldown)
    {
        // Spell is ready
        cast_spell();
        last_spell_cast_time = current_time;
    }
}
```

### Timer System

```cpp
struct Timer
{
    float start_time;
    float duration;
    
    Timer(float dur) : duration(dur)
    {
        start_time = g_sdk->clock_facade->get_game_time();
    }
    
    bool is_expired() const
    {
        return g_sdk->clock_facade->get_game_time() >= start_time + duration;
    }
    
    float remaining() const
    {
        float elapsed = g_sdk->clock_facade->get_game_time() - start_time;
        return std::max(0.f, duration - elapsed);
    }
};

// Usage
Timer flash_timer(300.f); // 5 minute flash cooldown

if (flash_timer.is_expired())
{
    g_sdk->log_console("Flash is ready!");
}
else
{
    g_sdk->log_console("Flash ready in: %.0f seconds", flash_timer.remaining());
}
```

### Time-Based Actions

```cpp
void check_game_phase()
{
    float game_time = g_sdk->clock_facade->get_game_time();
    
    if (game_time < 90.f) // First 1:30
    {
        g_sdk->log_console("Early game - playing safe");
    }
    else if (game_time < 900.f) // Until 15:00
    {
        g_sdk->log_console("Mid game - farm and objectives");
    }
    else
    {
        g_sdk->log_console("Late game - team fighting");
    }
}
```

### Delayed Actions

```cpp
float action_delay_time = 0.f;
bool action_pending = false;

void schedule_action(float delay)
{
    action_delay_time = g_sdk->clock_facade->get_game_time() + delay;
    action_pending = true;
}

void check_delayed_action()
{
    if (action_pending && g_sdk->clock_facade->get_game_time() >= action_delay_time)
    {
        // Execute delayed action
        perform_action();
        action_pending = false;
    }
}
```

---

## Time Calculations

### Convert Seconds to Minutes

```cpp
void display_game_time()
{
    float game_time = g_sdk->clock_facade->get_game_time();
    int minutes = static_cast<int>(game_time / 60.f);
    int seconds = static_cast<int>(game_time) % 60;
    
    g_sdk->log_console("Game time: %d:%02d", minutes, seconds);
}
```

### Time Since Event

```cpp
float last_death_time = 0.f;

void on_player_death()
{
    last_death_time = g_sdk->clock_facade->get_game_time();
}

float get_time_since_death()
{
    return g_sdk->clock_facade->get_game_time() - last_death_time;
}
```

---

## Best Practices

### ✅ Do

- Use game time for all game-related timers
- Cache the current time if using it multiple times in one frame
- Use time comparisons for cooldowns and delays
- Store timestamps as floats

### ❌ Don't

- Don't use system time (like `std::chrono`) for game logic
- Don't assume time is always increasing (replay/pause can affect it)
- Don't compare times with exact equality (use `>=` or `<=`)
- Don't forget that time is in seconds, not milliseconds

---

## Related

- [Orbwalker](../interfaces/orbwalker.md) - Uses time for attack timing
- [Events](../main/events.md) - Events occur at specific game times
- [Spell Manager](../interfaces/spell-manager.md) - Tracks spell cast times
