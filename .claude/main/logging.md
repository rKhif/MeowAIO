# Logging

VEN SDK provides a simple but effective console logging system with color-coded message types.

---

## Log Types

The logging system uses prefixes to determine the color and type of log message:

| Prefix | Type | Color | Usage |
|--------|------|-------|-------|
| *ANY* | Info | Default | General information |
| `[!]` | Error | Red | Errors and critical issues |
| `[?]` | Warning | Yellow | Warnings and cautions |

---

## Usage

### Info Messages

Standard informational messages:

```cpp
g_sdk->log_console("Hello from logger!");
g_sdk->log_console("Player position: %.2f, %.2f", x, y);
```

### Error Messages

For errors and critical issues - prefix with `[!]`:

```cpp
g_sdk->log_console("[!] Failed to initialize!");
g_sdk->log_console("[!] Target is nullptr");
```

### Warning Messages

For warnings and cautions - prefix with `[?]`:

```cpp
g_sdk->log_console("[?] Attack might be cancelled");
g_sdk->log_console("[?] Low health detected");
```

---

## Examples

### Basic Logging

```cpp
void __fastcall game_update()
{
    const auto player = g_sdk->object_manager->get_local_player();
    if (player)
    {
        g_sdk->log_console("Player health: %.0f/%.0f", 
            player->get_health(), 
            player->get_max_health());
    }
}
```

### Conditional Logging

```cpp
void __fastcall create_object(game_object* object)
{
    if (object->is_hero())
    {
        g_sdk->log_console("[+] Hero created: %s", object->get_char_name().c_str());
    }
    else if (object->is_minion())
    {
        g_sdk->log_console("Minion spawned");
    }
}
```

### Error Handling

```cpp
extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    if (!sdk_init::orbwalker())
    {
        g_sdk->log_console("[!] Failed to initialize Orbwalker SDK");
        return false;
    }
    
    g_sdk->log_console("[+] Module loaded successfully!");
    return true;
}
```

---

## Best Practices

### ✅ Do

- Use appropriate prefixes (`[!]` for errors, `[?]` for warnings)
- Keep messages concise and informative
- Use format specifiers for dynamic values
- Log important state changes and errors

### ❌ Don't

- Don't spam logs in high-frequency events (like `game_update`)
- Don't log sensitive information
- Don't use logs for debugging in production

---

## Format Specifiers

Common format specifiers for `log_console()`:

| Specifier | Type | Example |
|-----------|------|---------|
| `%d` | Integer | `%d` → 42 |
| `%f` | Float | `%f` → 3.141592 |
| `%.2f` | Float (2 decimals) | `%.2f` → 3.14 |
| `%s` | String (C-style) | `%s` → "text" |
| `%x` | Hex | `%x` → 2a |
| `%p` | Pointer | `%p` → 0x7ffe1234 |

---

## Related

- [Events](events.md) - Where to add logging
- [Getting Started](../introduction/getting-started.md) - Basic module setup
