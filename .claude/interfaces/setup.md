# Setup

Before using any SDK interface, you must initialize it properly.

---

## Loading the Dependencies

For every interface you want to use, you need to instantiate it inside `PluginLoad` using `sdk_init::`.

```cpp
extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    if (!sdk_init::orbwalker())
        return false;

    g_sdk->log_console("[+] ExampleModule loaded!");

    return true;
}
```

This example instantiates the pointer `sdk::orbwalker`.

ℹ️ **Every `sdk_init::` function returns a bool - the function will return false if the dependency failed to load**

---

## Available Interfaces

### sdk_init::orbwalker()

Initializes the Orbwalker interface for attack and movement automation.

```cpp
if (!sdk_init::orbwalker())
{
    g_sdk->log_console("[!] Failed to initialize Orbwalker");
    return false;
}
```

**Access via:** `sdk::orbwalker`

**Documentation:** [Orbwalker](orbwalker.md)

---

### sdk_init::target_selector()

Initializes the Target Selector interface for target selection.

```cpp
if (!sdk_init::target_selector())
{
    g_sdk->log_console("[!] Failed to initialize Target Selector");
    return false;
}
```

**Access via:** `sdk::target_selector`

**Documentation:** [Target Selector](target-selector.md)

---

### sdk_init::prediction()

Initializes the Prediction interface for skillshot prediction.

```cpp
if (!sdk_init::prediction())
{
    g_sdk->log_console("[!] Failed to initialize Prediction");
    return false;
}
```

**Access via:** `sdk::prediction`

**Documentation:** [Prediction](prediction.md)

---

### sdk_init::spell_manager()

Initializes the Spell Manager interface for tracking active spells.

```cpp
if (!sdk_init::spell_manager())
{
    g_sdk->log_console("[!] Failed to initialize Spell Manager");
    return false;
}
```

**Access via:** `sdk::spell_manager`

**Documentation:** [Spell Manager](spell-manager.md)

---

### sdk_init::evade()

Initializes the Evade interface for spell dodging.

```cpp
if (!sdk_init::evade())
{
    g_sdk->log_console("[!] Failed to initialize Evade");
    return false;
}
```

**Access via:** `sdk::evade`

**Documentation:** [Evade](evade.md)

---

### sdk_init::health_prediction()

Initializes the Health Prediction interface.

```cpp
if (!sdk_init::health_prediction())
{
    g_sdk->log_console("[!] Failed to initialize Health Prediction");
    return false;
}
```

**Access via:** `sdk::health_prediction`

**Documentation:** [Health Prediction](health-prediction.md)

---

### sdk_init::damage()

Initializes the Damage interface for damage calculations.

```cpp
if (!sdk_init::damage())
{
    g_sdk->log_console("[!] Failed to initialize Damage");
    return false;
}
```

**Access via:** `sdk::damage`

**Documentation:** [Damage](damage.md)

---

### sdk_init::notification()

Initializes the Notification interface for displaying notifications.

```cpp
if (!sdk_init::notification())
{
    g_sdk->log_console("[!] Failed to initialize Notification");
    return false;
}
```

**Access via:** `sdk::notification`

**Documentation:** [Notification](notification.md)

---

### sdk_init::benchmark()

Initializes the Benchmark interface for performance testing.

```cpp
if (!sdk_init::benchmark())
{
    g_sdk->log_console("[!] Failed to initialize Benchmark");
    return false;
}
```

**Access via:** `sdk::benchmark`

**Documentation:** [Benchmark](benchmark.md)

---

## Complete Example

```cpp
#include <Windows.h>
#include "sdk.hpp"

extern "C" __declspec(dllexport) int SDKVersion = SDK_VERSION;

extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    // Initialize required interfaces
    if (!sdk_init::orbwalker())
    {
        g_sdk->log_console("[!] Failed to initialize Orbwalker");
        return false;
    }
    
    if (!sdk_init::prediction())
    {
        g_sdk->log_console("[!] Failed to initialize Prediction");
        return false;
    }
    
    if (!sdk_init::target_selector())
    {
        g_sdk->log_console("[!] Failed to initialize Target Selector");
        return false;
    }
    
    if (!sdk_init::damage())
    {
        g_sdk->log_console("[!] Failed to initialize Damage");
        return false;
    }
    
    g_sdk->log_console("[+] All interfaces initialized successfully!");

    return true;
}

extern "C" __declspec(dllexport) void PluginUnload()
{
    g_sdk->log_console("[-] Module unloaded!");
}
```

---

## Best Practices

### ✅ Do

- Initialize all required interfaces in `PluginLoad`
- Check return values and handle failures
- Log initialization status
- Return `false` from `PluginLoad` if critical interface fails

### ❌ Don't

- Don't use interfaces without initializing them first
- Don't ignore initialization failures
- Don't initialize interfaces you don't use
- Don't assume initialization always succeeds

---

## Initialization Order

The order doesn't usually matter, but generally:
1. Orbwalker (often needed by other modules)
2. Target Selector
3. Prediction
4. Other interfaces as needed

---

## Related

- [Getting Started](../introduction/getting-started.md) - Project setup
- [Orbwalker](orbwalker.md) - Attack automation
- [Prediction](prediction.md) - Skillshot prediction
