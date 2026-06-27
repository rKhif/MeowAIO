# VEN SDK – Navigation & Docs Index

**Official Docs:** https://docs.ven.pub/

---

## SDK Headers (Signatures)

### `sdk/native_sdk.hpp` — THE PRIMARY SOURCE
**This is the most important file.** It contains complete signatures for the entire core SDK.
When looking for anything, search keywords directly inside it: `"pred"`, `"object"`, `"menu"`, `"renderer"`, `"orbwalker"`, `"event"`, etc.
Even interfaces like prediction and orbwalker are exposed here — always start here first.

### Interface-specific headers
Interfaces also have their own dedicated headers with extended signatures. Check these after `native_sdk.hpp`:

| Feature | Doc | SDK Header |
|---------|-----|-----------|
| Common utilities, input, key detection | — | `sdk/common_sdk.hpp` |
| Prediction | `interfaces/prediction.md` | `sdk/pred_sdk.hpp` |
| Orbwalker | `interfaces/orbwalker.md` | `sdk/orb_sdk.hpp` |
| Target Selector | `interfaces/target-selector.md` | `sdk/ts_sdk.hpp` |
| Evade | `interfaces/evade.md` | `sdk/evade_sdk.hpp` |
| Damage | `interfaces/damage.md` | `sdk/dmg_sdk.hpp` |
| Spell Manager | `interfaces/spell-manager.md` | `sdk/sm_sdk.hpp` |
| Health Prediction | `interfaces/health-prediction.md` | `sdk/hp_sdk.hpp` |
| Shaders | `interfaces/shaders.md` | `sdk/shader_sdk.hpp` |
| Infotab | `interfaces/infotab.md` | `sdk/infotab_sdk.hpp` |
| Notification | `interfaces/notification.md` | `sdk/notification_sdk.hpp` |
| Benchmark | `interfaces/benchmark.md` | `sdk/benchmark_sdk.hpp` |
| Math helpers | — | `sdk/math.hpp` |
| Color helpers | — | `sdk/color.hpp` |
| Spell type enums | — | `sdk/engine_spell_type.hpp` |
| All SDK includes | — | `sdk/sdk.hpp` |

---

## Full Documentation Index

### Introduction
- [Welcome](introduction/welcome.md) – Overview of VEN SDK
- [Getting Started](introduction/getting-started.md) – Project setup and configuration

### Main *(signatures in `sdk/native_sdk.hpp`)*
- [Logging](main/logging.md) – Console logging system (`sdk::log->...`)
- [Events](main/events.md) – All game event callbacks and hooks (`event_handler`)
- [Menu](main/menu.md) – Creating UI menus, checkboxes, sliders, hotkeys

### Classes *(signatures in `sdk/native_sdk.hpp`)*
- [Object Manager](classes/object-manager.md) – Access heroes, minions, missiles, all game objects
- [Clock Facade](classes/clock-facade.md) – Game time, creating timers, tracking cooldowns
- [Hud Manager](classes/hud-manager.md) – Cursor world position, hovered target
- [Renderer](classes/renderer.md) – Drawing circles, text, lines, 2D/3D elements, minimap
- [Net Client](classes/net-client.md) – Ping and network information
- [Nav Mesh](classes/nav-mesh.md) – Walkability checks, fog of war, pathfinding
- [Game Info](classes/game-info.md) – Game mode, map, match info
- [Game GUI](classes/game-gui.md) – Send chat messages, pings

### Interfaces *(each has its own SDK header — see table above)*
- [Setup](interfaces/setup.md) – Loading SDK interface dependencies
- [Orbwalker](interfaces/orbwalker.md) – Attack automation and movement — `sdk/orb_sdk.hpp`
- [Target Selector](interfaces/target-selector.md) – Target selection system — `sdk/ts_sdk.hpp`
- [Prediction](interfaces/prediction.md) – Skillshot prediction — `sdk/pred_sdk.hpp`
- [Spell Manager](interfaces/spell-manager.md) – Track active enemy spells — `sdk/sm_sdk.hpp`
- [Evade](interfaces/evade.md) – Auto-dodge spells — `sdk/evade_sdk.hpp`
- [Health Prediction](interfaces/health-prediction.md) – Predicted HP under damage — `sdk/hp_sdk.hpp`
- [Damage](interfaces/damage.md) – Damage calculation formulas — `sdk/dmg_sdk.hpp`
- [Infotab](interfaces/infotab.md) – Info display panel — `sdk/infotab_sdk.hpp`
- [Notification](interfaces/notification.md) – In-game notifications — `sdk/notification_sdk.hpp`
- [Shaders](interfaces/shaders.md) – Visual shader effects — `sdk/shader_sdk.hpp`
- [Benchmark](interfaces/benchmark.md) – Performance benchmarking — `sdk/benchmark_sdk.hpp`

---

## I Want To...

### Logging & Debugging
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Print to console | [Logging](main/logging.md) | `native_sdk.hpp` |
| Debug values in-game | [Logging](main/logging.md) | `native_sdk.hpp` |

### Events & Callbacks
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Hook into game update tick | [Events](main/events.md) – `game_update` | `native_sdk.hpp` |
| Detect spell casts | [Events](main/events.md) – `process_cast`, `execute_cast` | `native_sdk.hpp` |
| Track object spawns/deaths | [Events](main/events.md) – `create_object`, `delete_object` | `native_sdk.hpp` |
| Detect basic attacks | [Events](main/events.md) – `basic_attack` | `native_sdk.hpp` |
| Track buffs applied/removed | [Events](main/events.md) – `buff_gain`, `buff_loss` | `native_sdk.hpp` |
| Detect when drawing happens | [Events](main/events.md) – `present` | `native_sdk.hpp` |

### Menu & Configuration
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Create a settings menu | [Menu](main/menu.md) | `native_sdk.hpp` |
| Add checkbox / toggle | [Menu](main/menu.md) | `native_sdk.hpp` |
| Add slider | [Menu](main/menu.md) | `native_sdk.hpp` |
| Add hotkey (combo key) | [Menu](main/menu.md) | `native_sdk.hpp` |
| Add colorpicker | [Menu](main/menu.md) | `native_sdk.hpp` |

### Game Objects
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Get local player | [Object Manager](classes/object-manager.md) | `native_sdk.hpp` |
| Iterate enemies / allies | [Object Manager](classes/object-manager.md) | `native_sdk.hpp` |
| Iterate minions / jungle | [Object Manager](classes/object-manager.md) | `native_sdk.hpp` |
| Get object position, HP, stats | [Object Manager](classes/object-manager.md) | `native_sdk.hpp` |
| Track missiles / projectiles | [Object Manager](classes/object-manager.md) | `native_sdk.hpp` |

### Timing & Cooldowns
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Get current game time | [Clock Facade](classes/clock-facade.md) | `native_sdk.hpp` |
| Track spell cooldowns | [Clock Facade](classes/clock-facade.md) | `native_sdk.hpp` |
| Create a timer / delay logic | [Clock Facade](classes/clock-facade.md) | `native_sdk.hpp` |

### Mouse & HUD
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Get cursor world position | [Hud Manager](classes/hud-manager.md) | `native_sdk.hpp` |
| Get hovered target | [Hud Manager](classes/hud-manager.md) | `native_sdk.hpp` |

### Drawing & Rendering
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Draw circle (spell range) | [Renderer](classes/renderer.md) | `native_sdk.hpp` |
| Draw text on screen | [Renderer](classes/renderer.md) | `native_sdk.hpp` |
| Draw on minimap | [Renderer](classes/renderer.md) | `native_sdk.hpp` |
| Draw 3D shapes / lines | [Renderer](classes/renderer.md) | `native_sdk.hpp` |

### Network & Game Info
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Get ping | [Net Client](classes/net-client.md) | `native_sdk.hpp` |
| Get game mode / map | [Game Info](classes/game-info.md) | `native_sdk.hpp` |
| Send chat message / ping | [Game GUI](classes/game-gui.md) | `native_sdk.hpp` |

### Pathfinding & Vision
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Check if position is walkable | [Nav Mesh](classes/nav-mesh.md) | `native_sdk.hpp` |
| Check if in fog of war | [Nav Mesh](classes/nav-mesh.md) | `native_sdk.hpp` |

### Combat & Advanced
| Goal | Doc | SDK Header |
|------|-----|-----------|
| Auto-attack (orbwalk) | [Orbwalker](interfaces/orbwalker.md) | `sdk/orb_sdk.hpp` |
| Select best target | [Target Selector](interfaces/target-selector.md) | `sdk/ts_sdk.hpp` |
| Predict skillshot landing | [Prediction](interfaces/prediction.md) | `sdk/pred_sdk.hpp` |
| Track active enemy spells | [Spell Manager](interfaces/spell-manager.md) | `sdk/sm_sdk.hpp` |
| Auto-dodge incoming spells | [Evade](interfaces/evade.md) | `sdk/evade_sdk.hpp` |
| Calculate pre/post-mitigation damage | [Damage](interfaces/damage.md) | `sdk/dmg_sdk.hpp` |
| Predict target HP under damage | [Health Prediction](interfaces/health-prediction.md) | `sdk/hp_sdk.hpp` |
| Show in-game notification | [Notification](interfaces/notification.md) | `sdk/notification_sdk.hpp` |
| Apply visual shader effect | [Shaders](interfaces/shaders.md) | `sdk/shader_sdk.hpp` |
| Profile performance | [Benchmark](interfaces/benchmark.md) | `sdk/benchmark_sdk.hpp` |

---

## Common Patterns

### Drawing Spell Ranges
```
1. Menu          → toggle on/off          [Menu](main/menu.md)
2. Events        → hook present event     [Events](main/events.md)
3. Object Mgr    → get player position    [Object Manager](classes/object-manager.md)
4. Renderer      → draw the circle        [Renderer](classes/renderer.md)
```

### Combo Logic
```
1. Events        → game_update tick       [Events](main/events.md)
2. Menu          → hotkey to activate     [Menu](main/menu.md)
3. Orbwalker     → check attack window    [Orbwalker](interfaces/orbwalker.md)  ← sdk/orb_sdk.hpp
4. Prediction    → cast with prediction   [Prediction](interfaces/prediction.md) ← sdk/pred_sdk.hpp
```

### Object Tracking (e.g. enemy missiles)
```
1. Events        → create_object / delete_object   [Events](main/events.md)
2. Object Mgr    → validate / read object data     [Object Manager](classes/object-manager.md)
3. Clock Facade  → timestamp events                [Clock Facade](classes/clock-facade.md)
```

### Last-Hit / Lane Clear
```
1. Events        → game_update tick
2. Orbwalker     → check can attack       [Orbwalker](interfaces/orbwalker.md)  ← sdk/orb_sdk.hpp
3. Health Pred   → predict minion HP      [Health Prediction](interfaces/health-prediction.md) ← sdk/hp_sdk.hpp
4. Object Mgr    → iterate minions
```
