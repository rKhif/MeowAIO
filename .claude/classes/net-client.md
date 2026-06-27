# Net Client

The Net Client provides network-related information such as ping.

---

## Getting the Ping

```cpp
int g_sdk->net_client->get_ping()
```

ℹ️ **This function returns the current ping in milliseconds**

**Example:**

```cpp
void __fastcall game_update()
{
    const auto ping = g_sdk->net_client->get_ping();
    g_sdk->log_console("[+] Current ping is %dms", ping);
}
```

---

## Usage Examples

### Display Ping on Screen

```cpp
void __fastcall present()
{
    const auto ping = g_sdk->net_client->get_ping();
    
    char ping_text[64];
    sprintf_s(ping_text, "Ping: %dms", ping);
    
    math::vector2 position{10.f, 10.f};
    uint32_t color = ping < 50 ? 0xFF00FF00 : (ping < 100 ? 0xFFFFFF00 : 0xFFFF0000);
    
    g_sdk->renderer->add_text(ping_text, 14.f, position, 0, color);
}
```

### Adjust Prediction Based on Ping

```cpp
float get_adjusted_cast_delay()
{
    const auto ping = g_sdk->net_client->get_ping();
    const float base_delay = 0.25f; // Base spell delay
    const float ping_compensation = static_cast<float>(ping) / 1000.f; // Convert to seconds
    
    return base_delay + ping_compensation;
}
```

### High Ping Warning

```cpp
void check_connection_quality()
{
    const auto ping = g_sdk->net_client->get_ping();
    
    if (ping > 150)
    {
        g_sdk->log_console("[?] High ping detected: %dms", ping);
    }
    else if (ping > 300)
    {
        g_sdk->log_console("[!] Very high ping: %dms - Performance may be affected", ping);
    }
}
```

### Ping-Based Decision Making

```cpp
bool should_attempt_dodge()
{
    const auto ping = g_sdk->net_client->get_ping();
    
    // Don't attempt fast dodges with high ping
    if (ping > 100)
    {
        g_sdk->log_console("[?] Ping too high for reliable dodge");
        return false;
    }
    
    return true;
}
```

---

## Best Practices

### ✅ Do

- Consider ping when doing time-sensitive operations
- Adjust prediction timings based on ping
- Warn users about high ping
- Use ping to determine if certain features should be disabled

### ❌ Don't

- Don't query ping excessively (cache it per frame if needed)
- Don't assume ping is always stable
- Don't forget ping affects all network operations

---

## Related

- [Prediction](../interfaces/prediction.md) - Adjust delays based on ping
- [Orbwalker](../interfaces/orbwalker.md) - Attack timing affected by ping
