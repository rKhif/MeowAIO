# Events

The VEN SDK event system allows you to hook into various game events. Events are callbacks that trigger when specific actions occur in the game.

---

## Event Registration

### Registering an Event

To register a game event, call `g_sdk->event_manager->register_callback` inside `PluginLoad`:

```cpp
void __fastcall present()
{
    g_sdk->log_console("[+] Presenting the frame!");
}

extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    g_sdk->event_manager->register_callback(
        event_manager::event::present, 
        reinterpret_cast<void*>(present)
    );

    g_sdk->log_console("[+] ExampleModule loaded!");
    return true;
}
```

### Unregistering an Event

To unregister from a game event, call `g_sdk->event_manager->unregister_callback` inside `PluginUnload`:

```cpp
extern "C" __declspec(dllexport) void PluginUnload()
{
    g_sdk->event_manager->unregister_callback(
        event_manager::event::present, 
        reinterpret_cast<void*>(present)
    );

    g_sdk->log_console("[-] ExampleModule unloaded!");
}
```

---

## Available Events

### event::present

**When:** Every rendering frame  
**Use For:** UI drawings on top of everything

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

---

### event::draw_environment

**When:** Every rendering frame  
**Use For:** UI drawings under the game environment

**Example:**

```cpp
void __fastcall draw_environment()
{
    auto player = g_sdk->object_manager->get_local_player();
    if(player)
        g_sdk->renderer->add_circle_3d(player->get_position(), 20.f, 1.f, 0xFFFFFFFF);
}
```

---

### event::draw_world

**When:** Every world rendering frame  
**Use For:** World layer drawings (3D circles, indicators)

**Example:**

```cpp
void __fastcall draw_world()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player)
    {
        auto position = player->get_position();
        g_sdk->renderer->add_circle_3d(
            position, 
            player->get_bounding_radius(), 
            1.f, 
            0xFFFFFFFF
        );
    }
}
```

---

### event::wndproc

**When:** Game receives a window message  
**Use For:** Keyboard/mouse input detection

**Signature:**
```cpp
void __fastcall wndproc(uint32_t msg, uint32_t wparam, uint32_t lparam)
```

**Example:**

```cpp
void __fastcall wndproc(uint32_t msg, uint32_t wparam, uint32_t lparam)
{
    if (msg == WM_LBUTTONDOWN)
    {
        g_sdk->log_console("[+] LMB was pressed");
    }
}
```

---

### event::game_update

**When:** Every game frame  
**Use For:** Module logic (NOT for drawing)

**Example:**

```cpp
void __fastcall game_update()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player && player->is_moving())
    {
        g_sdk->log_console("[+] Moving!");
    }
}
```

---

### event::create_object

**When:** An object is created in the game  
**Use For:** Tracking object spawns

**Signature:**
```cpp
void __fastcall create_object(game_object* object)
```

**Example:**

```cpp
void __fastcall create_object(game_object* object)
{
    g_sdk->log_console("[+] Object %s created", object->get_name().c_str());
}
```

---

### event::delete_object

**When:** An object is deleted from the game  
**Use For:** Tracking object deletions

**Signature:**
```cpp
void __fastcall delete_object(game_object* object)
```

**Example:**

```cpp
void __fastcall delete_object(game_object* object)
{
    g_sdk->log_console("[-] Object %s deleted", object->get_name().c_str());
}
```

---

### event::create_missile

**When:** A missile is created  
**Use For:** Tracking missiles/projectiles

**Signature:**
```cpp
void __fastcall create_missile(game_object* missile)
```

**Example:**

```cpp
void __fastcall create_missile(game_object* missile)
{
    const auto spell_cast = missile->get_missile_spell_cast();
    if (spell_cast)
    {
        const auto attack_id = spell_cast->get_attack_id();
        const auto name = spell_cast->get_spell_data()->get_static_data()->get_name();

        g_sdk->log_console("[+] Missile linked to attack id %d - name: %s", attack_id, name);
    }
}
```

---

### event::basic_attack

**When:** An AI object starts a basic attack cast  
**Use For:** Attack tracking, animation cancelling

**Signature:**
```cpp
void __fastcall basic_attack(game_object* object, game_object* target, spell_cast* cast)
```

**Example:**

```cpp
void __fastcall basic_attack(game_object* object, game_object* target, spell_cast* cast)
{
    g_sdk->log_console("[+] %s started attacking %s", 
        object->get_name().c_str(), 
        target->get_name().c_str());
}
```

---

### event::stop_cast

**When:** An AI stops casting  
**Use For:** Tracking cancelled casts

**Signature:**
```cpp
void __fastcall stop_cast(game_object* object, spell_cast* cast, bool was_attack_processed)
```

**Example:**

```cpp
void __fastcall stop_cast(game_object* object, spell_cast* cast, bool was_attack_processed)
{
    const auto name = cast->get_spell_data()->get_static_data()->get_name();

    g_sdk->log_console("[+] %s stopped the cast %s (processed: %d)", 
        object->get_name().c_str(), 
        name, 
        was_attack_processed);
}
```

---

### event::process_cast

**When:** An AI starts casting a spell  
**Use For:** Spell detection and reaction

**Signature:**
```cpp
void __fastcall process_cast(game_object* object, spell_cast* cast)
```

**Example:**

```cpp
void __fastcall process_cast(game_object* object, spell_cast* cast)
{
    const auto name = cast->get_spell_data()->get_static_data()->get_name();

    g_sdk->log_console("[+] %s started casting %s", 
        object->get_name().c_str(), 
        name);
}
```

---

### event::execute_cast

**When:** An AI finishes the active cast  
**Use For:** Spell completion tracking

**Signature:**
```cpp
void __fastcall execute_cast(game_object* object, spell_cast* cast)
```

**Example:**

```cpp
void __fastcall execute_cast(game_object* object, spell_cast* cast)
{
    const auto name = cast->get_spell_data()->get_static_data()->get_name();

    g_sdk->log_console("[+] %s finished casting %s", 
        object->get_name().c_str(), 
        name);
}
```

---

### event::buff_gain

**When:** An AI gains a buff  
**Use For:** Buff tracking

**Signature:**
```cpp
void __fastcall buff_gain(game_object* object, buff_instance* buff)
```

**Example:**

```cpp
void __fastcall buff_gain(game_object* object, buff_instance* buff)
{
    g_sdk->log_console("[+] %s gained buff %s", 
        object->get_name().c_str(), 
        buff->get_name().c_str());
}
```

---

### event::buff_loss

**When:** An AI loses a buff  
**Use For:** Buff expiration tracking

**Signature:**
```cpp
void __fastcall buff_loss(game_object* object, buff_instance* buff)
```

**Example:**

```cpp
void __fastcall buff_loss(game_object* object, buff_instance* buff)
{
    g_sdk->log_console("[+] %s lost buff %s", 
        object->get_name().c_str(), 
        buff->get_name().c_str());
}
```

---

### event::new_path

**When:** An AI changes its path  
**Use For:** Movement and dash detection

**Signature:**
```cpp
void __fastcall new_path(game_object* object, bool is_dash, float dash_speed)
```

**Example:**

```cpp
void __fastcall new_path(game_object* object, bool is_dash, float dash_speed)
{
    if (is_dash)
    {
        g_sdk->log_console("[+] %s has dashed with %.02f speed", 
            object->get_name().c_str(), 
            dash_speed);
    }
}
```

---

### event::neutral_minion_kill

**When:** A jungle camp monster is killed  
**Use For:** Jungle tracking

**Signature:**
```cpp
void __fastcall neutral_minion_kill(game_object* object, game_object* minion, int camp_side_team_id)
```

**Example:**

```cpp
void __fastcall neutral_minion_kill(game_object* object, game_object* minion, int camp_side_team_id)
{
    const auto ally_camp = camp_side_team_id == object->get_team_id();

    g_sdk->log_console("[+] %s killed a monster (%s) from an %s camp", 
        object->get_name().c_str(),
        minion->get_name().c_str(), 
        ally_camp ? "allied" : "enemy");
}
```

---

### event::cast_heal

**When:** An AI object casts heal  
**Use For:** Heal tracking

**Signature:**
```cpp
void __fastcall cast_heal(game_object* object, game_object* target, float amount)
```

**Example:**

```cpp
void __fastcall cast_heal(game_object* object, game_object* target, float amount)
{
    g_sdk->log_console("[+] %s casted heal on %s (amount: %.02f)", 
        object->get_name().c_str(),
        target->get_name().c_str(), 
        amount);
}
```

---

### event::spell_hit

**When:** A spell has been hit  
**Use For:** Spell impact detection

**Signature:**
```cpp
void __fastcall spell_hit(game_object* object, spell_cast* cast)
```

**Example:**

```cpp
void __fastcall spell_hit(game_object* object, spell_cast* cast)
{
    g_sdk->log_console("[+] %s hit spell slot %d", 
        object->get_name().c_str(),
        cast->get_spell_slot());
}
```

---

### event::animation

**When:** An AI plays an animation  
**Use For:** Animation detection

**Signature:**
```cpp
void __fastcall animation(game_object* object, uint32_t animation_hash)
```

**Example:**

```cpp
void __fastcall animation(game_object* object, uint32_t animation_hash)
{
    g_sdk->log_console("[+] %s plays animation 0x%x", 
        object->get_name().c_str(), 
        animation_hash);
}
```

---

## Preventable Events

Some events can be prevented by returning `false`. These events allow you to block certain actions.

### event::issue_order

⚠️ **Does NOT trigger for issue_order called from modules (including Orbwalker)**  
⚠️ **This is a preventable event - return value determines if order is allowed**

**When:** Local player tries to issue an order  
**Use For:** Blocking movement or actions

**Signature:**
```cpp
bool __fastcall issue_order(game_object* object, game_object_order order_type, math::vector3 position, game_object* target, bool move_pet)
```

**Example:**

```cpp
bool __fastcall issue_order(game_object* object, game_object_order order_type, math::vector3 position, game_object* target, bool move_pet)
{
    if (order_type == game_object_order::stop)
    {
        g_sdk->log_console("[+] Stop order prevented!");
        return false; // Block the order
    }

    return true; // Allow the order
}
```

---

### event::cast_spell

⚠️ **Does NOT trigger for cast_spell called from modules**  
⚠️ **This is a preventable event - return value determines if cast is allowed**

**When:** Local player tries to cast a spell  
**Use For:** Blocking spell casts

**Signature:**
```cpp
bool __fastcall cast_spell(game_object* object, int spell_slot, math::vector3 position, game_object* target)
```

**Example:**

```cpp
bool __fastcall cast_spell(game_object* object, int spell_slot, math::vector3 position, game_object* target)
{
    if (spell_slot == 0) // Q spell
    {
        g_sdk->log_console("[+] Prevented the cast of spell 0!");
        return false; // Block the cast
    }

    return true; // Allow the cast
}
```

---

## Developer Events

### event::packet

⚠️ **This event is ONLY enabled on DEVELOPER builds**

**When:** Client receives a network packet  
**Use For:** Packet analysis (advanced)

**Signature:**
```cpp
void __fastcall packet(uint16_t packet_opcode, uint64_t packet_data, uint32_t rpc_network_id)
```

**Example:**

```cpp
void __fastcall packet(uint16_t packet_opcode, uint64_t packet_data, uint32_t rpc_network_id)
{
    g_sdk->log_console("[+] Received packet with opcode 0x%x", packet_opcode);
}
```

---

## Best Practices

### ✅ Do

- Unregister events in `PluginUnload`
- Use `game_update` for logic, `present`/`draw_world` for rendering
- Check for `nullptr` before using object pointers
- Return `true` in preventable events unless you want to block them

### ❌ Don't

- Don't do heavy computation in high-frequency events (`game_update`, `present`)
- Don't log excessively in frequently-called events
- Don't forget to unregister events (can cause crashes)
- Don't mix drawing code in `game_update`

---

## Event Frequency Guide

| Event | Frequency | Use Case |
|-------|-----------|----------|
| `present` | ~60-144 Hz | UI rendering |
| `draw_world` | ~60-144 Hz | 3D rendering |
| `game_update` | ~30 Hz | Game logic |
| `create_object` | On spawn | Object tracking |
| `process_cast` | On cast | Spell detection |

---

## Related

- [Logging](logging.md) - How to log in events
- [Object Manager](../classes/object-manager.md) - Working with game objects
- [Renderer](../classes/renderer.md) - Drawing in render events
