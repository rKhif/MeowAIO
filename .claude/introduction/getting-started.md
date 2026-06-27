# Getting Started

Complete guide to setting up your VEN SDK module project.

---

## Requirements

- **Visual Studio 2022**
- **VEN SDK** (downloaded and extracted)

---

## Creating New Project

1. Start **Visual Studio 2022** and press **"Create a new project"**
2. Select **"C++ Empty Project"** and press **"Next"**
3. Change the name and location of the project and press **"Create"**
4. Right click on the created project and press **"Add → New Item"**
5. Enter the desired name of the file that will contain the entry point of the module (e.g., `main.cpp`)

---

## Configuring the Project

### General Settings

1. Right click on the project name and press **"Properties"**
2. Select **"General"** and change the following settings:

| Setting | Value |
|---------|-------|
| **Output Directory** | `$(LOCALAPPDATA)\VEN\League\Modules\` |
| **Configuration Type** | Dynamic Library (.dll) |
| **C++ Language Standard** | ISO C++20 Standard (/std:c++20) |

### C/C++ Settings

1. Select **"C/C++ → All Options"** and change:

| Setting | Value |
|---------|-------|
| **Runtime Library** | Multi-threaded (/MT) |
| **SDL checks** | No (/sdl-) |

### Include Directories

1. Copy the **"sdk"** directory from the downloaded VEN SDK archive to the project root directory
2. Select **"VC++ Directories"**
3. Change **"Include Directories"** to: `$(ProjectDir)sdk;$(IncludePath)`

---

## Module Code

### Module Properties

Export the SDK version so the core can verify compatibility:

```cpp
extern "C" __declspec(dllexport) int SDKVersion = SDK_VERSION;
```

### Load Callback

Register the **PluginLoad** callback that triggers when the core loads your module:

```cpp
extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    g_sdk->log_console("[+] ExampleModule loaded!");

    return true;
}
```

**Important:**
- Return `true` if the module loaded successfully
- Return `false` to prevent the module from loading

### Unload Callback

Register the **PluginUnload** callback that triggers when the core unloads your module:

```cpp
extern "C" __declspec(dllexport) void PluginUnload()
{
    g_sdk->log_console("[-] ExampleModule unloaded!");
}
```

Use this to clean up resources, unregister events, etc.

---

## Complete Example

Here's a complete `main.cpp` file:

```cpp
#include <Windows.h>
#include "sdk.hpp"

// Export SDK version
extern "C" __declspec(dllexport) int SDKVersion = SDK_VERSION;

// Load callback
extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    g_sdk->log_console("[+] ExampleModule loaded!");

    return true;
}

// Unload callback
extern "C" __declspec(dllexport) void PluginUnload()
{
    g_sdk->log_console("[-] ExampleModule unloaded!");
}
```

---

## Special Module Types

If you're creating a specialized module to replace core functionality, add one of these exports:

### Orbwalker Module

```cpp
extern "C" __declspec(dllexport) module_type Type = module_type::orb;
```

### Prediction Module

```cpp
extern "C" __declspec(dllexport) module_type Type = module_type::pred;
```

### Evade Module

```cpp
extern "C" __declspec(dllexport) module_type Type = module_type::evade;
```

These modules will be selectable in the VEN Modules menu to replace the default implementations.

---

## Building and Testing

1. **Build** your project (Ctrl+B)
2. Check the output directory: `%LOCALAPPDATA%\VEN\League\Modules\`
3. Your `.dll` file should be there
4. **Launch League of Legends** with VEN
5. Open the VEN menu and enable your module
6. Check the **console** for your log message

---

## Next Steps

Now that you have a working module:

1. Learn about [Events](../main/events.md) to hook into game events
2. Explore [Object Manager](../classes/object-manager.md) to access game objects
3. Check out [Menu System](../main/menu.md) to add configuration options
4. Study [Orbwalker](../interfaces/orbwalker.md) for advanced automation

---

## Troubleshooting

### Module doesn't load
- Check that SDK version matches
- Verify the DLL is in the correct output directory
- Check VS2022 output window for compilation errors

### Can't see console logs
- Make sure VEN console is visible
- Check that `g_sdk->log_console()` is being called

### Compilation errors
- Verify SDK folder is in the correct location
- Check include directories are set properly
- Ensure C++20 standard is enabled
