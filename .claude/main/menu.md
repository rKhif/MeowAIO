# Menu System

The VEN SDK menu system provides an easy way to create configuration UIs for your modules.

ℹ️ **Callbacks passed to menu elements are called when their value changes AND when they are created (for initialization)**

---

## Creating a Menu

### Add a Category

First, create a category to hold your menu elements:

```cpp
menu_category* g_sdk->menu_manager->add_category(
    std::string const& name,              // Unique identifier
    std::string const& display_name       // Displayed name
)
```

- `name` - Must be unique to avoid config conflicts
- `display_name` - What users see in the menu

**Example:**

```cpp
extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    const auto category = g_sdk->menu_manager->add_category("example_module", "ExampleModule");

    g_sdk->log_console("[+] ExampleModule loaded!");
    return true;
}
```

---

## Menu Elements

### Label

Add text labels for organization:

```cpp
void menu_category::add_label(std::string const& text)
```

**Example:**

```cpp
category->add_label("Combat Settings");
```

---

### Checkbox

Add a boolean toggle:

```cpp
void menu_category::add_checkbox(
    std::string const& element_name,
    std::string const& element_display_name,
    bool default_value,
    std::function<void(bool)> const& callback
)
```

**Example:**

```cpp
category->add_checkbox("use_combo", "Use Combo", true, [](bool value)
{
    if (value)
        g_sdk->log_console("[+] Combo enabled");
    else
        g_sdk->log_console("[+] Combo disabled");
});
```

---

### Hotkey

Add a key binding:

```cpp
void menu_category::add_hotkey(
    std::string const& element_name,
    std::string const& element_display_name,
    unsigned char default_key,
    bool default_value = false,
    bool toggle = false,
    std::function<void(std::string*, bool)> const& callback = nullptr
)
```

**Parameters:**
- `default_key` - Virtual key code (e.g., `VK_SPACE`, `VK_F5`)
- `default_value` - Initial state
- `toggle` - If `true`, key toggles; if `false`, key must be held
- `callback` - Called when key state changes

**Example:**

```cpp
category->add_hotkey("combo_key", "Combo Key", VK_SPACE, false, false, 
    [](std::string*, bool value)
{
    if (value)
        g_sdk->log_console("[+] Combo key pressed!");
});
```

**Common Virtual Key Codes:**
```cpp
VK_SPACE    // Spacebar
VK_F1 - VK_F12  // F keys
VK_LBUTTON  // Left mouse button
VK_RBUTTON  // Right mouse button
VK_CONTROL  // Ctrl
VK_SHIFT    // Shift
VK_MENU     // Alt
```

---

### Separator

Add a visual separator:

```cpp
void menu_category::add_separator()
```

**Example:**

```cpp
category->add_separator();
```

---

### Slider (Integer)

Add an integer slider:

```cpp
void menu_category::add_slider_int(
    std::string const& element_name,
    std::string const& element_display_name,
    int min,
    int max,
    int step,
    int default_value,
    std::function<void(int)> const& callback = nullptr
)
```

**Example:**

```cpp
category->add_slider_int("combo_range", "Combo Range", 0, 1000, 50, 600, 
    [](int value)
{
    g_sdk->log_console("[+] Combo range set to %d", value);
});
```

---

### Slider (Float)

Add a float slider:

```cpp
void menu_category::add_slider_float(
    std::string const& element_name,
    std::string const& element_display_name,
    float min,
    float max,
    float step,
    float default_value,
    std::function<void(float)> const& callback = nullptr
)
```

**Example:**

```cpp
category->add_slider_float("hitchance", "Min Hitchance", 0.f, 1.f, 0.05f, 0.7f, 
    [](float value)
{
    g_sdk->log_console("[+] Hitchance set to %.2f", value);
});
```

---

### Combo Box

Add a dropdown selection:

```cpp
void menu_category::add_combo(
    std::string const& element_name,
    std::string const& element_display_name,
    std::vector<std::string> const& items,
    int default_value,
    const std::function<void(int)>& callback = nullptr
)
```

**Example:**

```cpp
category->add_combo("log_level", "Log Level", {"Info", "Warning", "Error"}, 0, 
    [](int value)
{
    switch(value)
    {
        case 0: g_sdk->log_console("[+] Logging: Info"); break;
        case 1: g_sdk->log_console("[?] Logging: Warning"); break;
        case 2: g_sdk->log_console("[!] Logging: Error"); break;
    }
});
```

---

### Color Picker

Add a color selection:

```cpp
void menu_category::add_colorpicker(
    std::string const& element_name,
    std::string const& element_display_name,
    uint32_t default_color,
    std::function<void(uint32_t)> const& callback = nullptr
)
```

**Color Format:** `0xAARRGGBB` (Alpha, Red, Green, Blue)

**Example:**

```cpp
category->add_colorpicker("draw_color", "Draw Color", 0xFFFFFFFF, 
    [](uint32_t color)
{
    g_sdk->log_console("[+] Color changed to 0x%X", color);
});
```

**Common Colors:**
```cpp
0xFFFFFFFF  // White
0xFF000000  // Black
0xFFFF0000  // Red
0xFF00FF00  // Green
0xFF0000FF  // Blue
0xFFFFFF00  // Yellow
0xFF00FFFF  // Cyan
0xFFFF00FF  // Magenta
```

---

### Sub-category

Create nested menus:

```cpp
menu_category* menu_category::add_sub_category(
    std::string const& element_name,
    std::string const& element_display_name
)
```

**Example:**

```cpp
const auto spells = category->add_sub_category("spells", "Spell Settings");
spells->add_checkbox("use_q", "Use Q", true, nullptr);
spells->add_checkbox("use_w", "Use W", true, nullptr);
spells->add_checkbox("use_e", "Use E", true, nullptr);
spells->add_checkbox("use_r", "Use R", false, nullptr);
```

---

## Reading Menu Values

To read menu values in your code, use the category pointer:

```cpp
// Checkbox
if (category->get_bool("element_name"))
{
    // Checkbox is enabled
}

// Hotkey
if (category->get_bool("hotkey_name"))
{
    // Key is pressed/active
}

// Slider Int
int value = category->get_int("slider_name");

// Slider Float
float value = category->get_float("slider_name");

// Combo
int selected = category->get_int("combo_name");

// Colorpicker
uint32_t color = category->get_int("colorpicker_name");
```

---

## Complete Example

```cpp
menu_category* g_menu = nullptr;

extern "C" __declspec(dllexport) bool PluginLoad(core_sdk* sdk, void** custom_sdk)
{
    g_sdk = sdk;
    
    // Create main category
    g_menu = g_sdk->menu_manager->add_category("my_champion", "My Champion");
    
    // Add label
    g_menu->add_label("Combat Settings");
    
    // Add hotkeys
    g_menu->add_hotkey("combo_key", "Combo", VK_SPACE, false, false, nullptr);
    g_menu->add_hotkey("harass_key", "Harass", 'C', false, false, nullptr);
    g_menu->add_hotkey("clear_key", "Clear", 'V', false, false, nullptr);
    
    g_menu->add_separator();
    
    // Add spell settings sub-category
    const auto spells = g_menu->add_sub_category("spells", "Spells");
    
    // Q settings
    spells->add_checkbox("use_q", "Use Q", true, nullptr);
    spells->add_slider_int("q_range", "Q Range", 0, 1200, 50, 1000, nullptr);
    
    // W settings
    spells->add_checkbox("use_w", "Use W", true, nullptr);
    spells->add_combo("w_mode", "W Mode", {"Always", "If Killable", "Never"}, 0, nullptr);
    
    // Add drawing settings
    const auto draw = g_menu->add_sub_category("draw", "Drawing");
    draw->add_checkbox("draw_q", "Draw Q Range", true, nullptr);
    draw->add_colorpicker("q_color", "Q Color", 0xFF00FF00, nullptr);
    
    g_sdk->log_console("[+] Module loaded!");
    return true;
}

void __fastcall game_update()
{
    // Check if combo key is pressed
    if (g_menu->get_bool("combo_key"))
    {
        // Check if we should use Q
        if (g_menu->get_bool("spells.use_q"))
        {
            int q_range = g_menu->get_int("spells.q_range");
            // Use Q spell with specified range
        }
    }
}

void __fastcall draw_world()
{
    // Check if we should draw Q range
    if (g_menu->get_bool("draw.draw_q"))
    {
        uint32_t color = g_menu->get_int("draw.q_color");
        const auto player = g_sdk->object_manager->get_local_player();
        if (player)
        {
            int q_range = g_menu->get_int("spells.q_range");
            g_sdk->renderer->add_circle_3d(player->get_position(), q_range, 2.f, color);
        }
    }
}
```

---

## Best Practices

### ✅ Do

- Use clear, descriptive names for menu elements
- Group related settings in sub-categories
- Add labels and separators for organization
- Use appropriate default values
- Store menu category pointer globally for easy access

### ❌ Don't

- Don't use generic names like "option1", "option2"
- Don't create flat menus with 50+ options
- Don't forget to use separators for visual grouping
- Don't read menu values excessively in hot paths (cache when possible)

---

## Menu Access Pattern

```cpp
// Store globally
menu_category* g_menu = nullptr;

// Initialize in PluginLoad
g_menu = g_sdk->menu_manager->add_category("module", "Module Name");

// Access anywhere
if (g_menu->get_bool("setting_name"))
{
    // Use setting
}
```

---

## Related

- [Events](events.md) - Where to read menu values
- [Getting Started](../introduction/getting-started.md) - Module setup
