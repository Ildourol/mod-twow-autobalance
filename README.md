# mod-twow-autobalance for TortoiseWoW / Turtle WoW

![mod-twow-autobalance](mod-twow-autobalance.jpg)

Vanilla 1.12.1 port of the AzerothCore **mod-autobalance** module, adapted for **TortoiseWoW** and **Turtle WoW** (client 1.18.1.7272).

---

## 1. Overview

`mod-twow-autobalance` dynamically adjusts dungeon and raid encounter difficulty based on the number and levels of active players inside an instance. It allows small groups (or solo players with or without PlayerBots) to experience group content with smoothly scaled health, mana, armor, damage, and crowd-control mechanics while maintaining the intended encounter pacing.

### Key Highlights
- **Non-Intrusive Integration**: Zero core modifications or source code patches required for `tortoise-wow` or `TortoiseBots`. All hooks use native virtual script hooks (`UnitScript`, `PlayerScript`, `AllMapScript`).
- **Clean Installation & Zero Database Mitigation**: Pure in-memory operational model without database migrations, historical SQL tables, or schema repair.
- **Boot Native Capacity Baseline**: Captures authentic map capacities once at boot before modifying entrance limits; cleanly releases overrides if disabled.
- **Instance State Isolation**: 64-bit packed keys `(mapId << 32) | instanceId` paired with object GUIDs isolate creature state across all dungeon instances.
- **Separate Summons Lifecycle**: Temporary summons are tracked independently to prevent skewing native level averages while fully participating in scaling and cleanup.
- **Smooth Hyperbolic Tangent (tanh) Scaling**: Smooth sigmoid curves parameterized by inflection points, floor, ceiling, and boss modifiers (1:1 parity with upstream WotLK).
- **Dynamic & Fixed Level Scaling**: Scales creature levels, health, mana, and damage relative to the highest player level in the party, supporting native boss curves through level 63.
- **Native Armor Recalculation**: Explicitly invokes `creature->UpdateArmor()` upon applying or restoring base armor modifiers.
- **Combat Safety & Deferred Work**: Protects active encounters with combat locking and safely defers pending rescale/restore operations until combat concludes.
- **Selective Hook Subscriptions**: Registers only implemented hook selectors, ensuring zero overhead on unhandled core events.
- **PlayerBots (`TortoiseBots`) Compatibility**: Configurable bot difficulty weighting (`AutoBalance.CountBots`, `AutoBalance.BotWeight`) and automatic suppression of chat sysmessages to headless bot sessions.

---

## 2. Installation

These steps assume you have a working [TortoiseWoW](https://github.com/Penqle/tortoise-wow) source checkout, initialized core databases, and the server data files for your realm. Use a core revision compatible with this module. Run all build and SQL commands from the core source root, the directory containing the core's `CMakeLists.txt`.


### 1. Add the module and build on Linux

Use Git, CMake, a C++17 compiler, and the core's build dependencies. On Debian or Ubuntu, a typical starting point is:

```bash
sudo apt update
sudo apt install git cmake build-essential libace-dev default-libmysqlclient-dev libssl-dev zlib1g-dev
```

Use your distribution's `mysql` or `mariadb` command-line client if importing SQL manually. Keep any additional dependency and CMake settings required by your core and other enabled modules.

```bash
cd /path/to/tortoise-wow
git clone https://github.com/Ildourol/mod-twow-autobalance.git modules/mod-twow-autobalance
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PWD/server-stage" -DMODULE_MOD_TWOW_AUTOBALANCE=static
cmake --build build --parallel
cmake --install build
```

### 2. Add the module and build on Windows

Install Git for Windows, CMake 3.21 or later, and Visual Studio 2022 or its Build Tools with **Desktop development with C++** and a Windows SDK. Open PowerShell with Git and CMake on `PATH`. Use an x64 build and the dependency setup of your working core.

```powershell
Set-Location 'C:\src\tortoise-wow'
git clone https://github.com/Ildourol/mod-twow-autobalance.git modules/mod-twow-autobalance
cmake -S . -B build-vs -G "Visual Studio 17 2022" -A x64 "-DCMAKE_INSTALL_PREFIX=$PWD/server-stage" -DMODULE_MOD_TWOW_AUTOBALANCE=static
cmake --build build-vs --config Release --parallel
cmake --install build-vs --config Release
```

Check each command for errors before continuing. Both builds include `mangosd`. If the configure output reports an unused module switch, verify the source placement before building. The two build sections are platform alternatives. If you already have a module checkout, use it in place of the clone. A full directory copy, a Linux symlink, or a Windows directory junction works as long as `modules/mod-twow-autobalance/src/` is directly below the module directory. The module directory name determines the CMake switch and loader symbol. If reusing a build directory, keep its generator and architecture and preserve the switches for your other modules. Reconfigure after adding the module and verify CMake lists it as static.

The staged world server is normally `server-stage/bin/mangosd` on Linux and `server-stage/mangosd.exe` on Windows. Stop the running world server before deploying the rebuilt executable and any required runtime libraries or module assets. Preserve the live configuration and data files. The paths below assume the standard staged layout. For an existing deployment, use the directory containing the active `mangosd.conf`, including a custom path passed via `-c`.

### 3. Set up the database

This module ships no SQL migrations. The initialized core databases are sufficient.

### 4. Deploy and enable the configuration

Create `mod-twow-autobalance.conf` inside the `modules/` subdirectory beside the active `mangosd.conf`. For a first install using the example staging directory, the following commands copy the shipped template. If an active module config already exists, retain it and merge new settings from the template.

**Linux**

```bash
mkdir -p server-stage/etc/modules
cp -n "modules/mod-twow-autobalance/conf/mod-twow-autobalance.conf.dist" "server-stage/etc/modules/mod-twow-autobalance.conf"
```

**Windows (PowerShell)**

```powershell
New-Item -ItemType Directory -Force 'server-stage/modules' | Out-Null
if (-not (Test-Path 'server-stage/modules/mod-twow-autobalance.conf')) {
    Copy-Item 'modules/mod-twow-autobalance/conf/mod-twow-autobalance.conf.dist' 'server-stage/modules/mod-twow-autobalance.conf'
}
```

Keep the template's section header and confirm `AutoBalance.Enable.Global = 1` in the active file. Start the rebuilt server with your normal launcher and active `mangosd.conf`. Check the startup log for module configuration, database, and dependency errors.

### Verify the installation

Enter a supported instance and run `.ab info` and `.ab mapstat` with an account that has permission. If the status reports disabled, check the active config path and keep the `[AutoBalance]` section.

## 3. Mathematical Methodology

### Inflection Point & Sigmoid Multiplier Formula

Let:
- $N_{\text{max}}$ be the maximum intended instance capacity (e.g. 5 for normal dungeons, 10/20/40 for raids).
- $P_{\text{eff}}$ be the adjusted effective player count in the instance.
- $I_f$ be the configured inflection factor (default: 0.5).
- $I = N_{\text{max}} \times I_f$ be the resulting player-count inflection point.
- $F$ be the curve floor.
- $C$ be the curve ceiling (default: 1.0).

$$\text{diff} = \frac{N_{\text{max}}}{5.0} \times 1.5$$

$$\text{denom} = \left( \frac{\tanh\left(\frac{N_{\text{max}} - I}{\text{diff}}\right) + 1}{2} \right) \times (C - F) + F$$

$$\text{ceilingAdjustment} = \frac{C}{\text{denom}}$$

$$\text{multiplier} = \left( \frac{\tanh\left(\frac{P_{\text{eff}} - I}{\text{diff}}\right) + 1}{2} \right) \times (C \times \text{ceilingAdjustment} - F) + F$$

Each creature stat (Health, Mana, Armor, Damage, CC Duration) is computed from its unmodified base baseline:
- $\text{Health} = \max(1, \text{round}(\text{baseHealth} \times \text{multiplier} \times \text{mod}_{\text{global}} \times \text{mod}_{\text{health}} \times \text{ratio}_{\text{level}}))$
- $\text{Damage} = \text{baseDamage} \times \text{multiplier} \times \text{mod}_{\text{global}} \times \text{mod}_{\text{damage}} \times \text{ratio}_{\text{level}}$

---

## 4. Architecture & Non-Intrusive Hooks

| Component | Role | Hook Technique |
|---|---|---|
| `AutoBalance.h` | Core data models & structs | Instance-aware keys `(mapId << 32) | instanceId`, separate summons tracking |
| `AutoBalanceConfig` | Configuration parser | Native `sConfig` reader, whitespace format, structural size guards |
| `AutoBalanceScaling` | Mathematical scaling engine | Sigmoid inflection, immutable built-in Vanilla curves (lvl 1-63), `UpdateArmor()` |
| `AutoBalanceManager` | Central state registry | Instance isolation, boot capacity capture, 5-second maintenance cadence |
| `AutoBalanceScripts` | Engine integration | Filtered `UnitScript`, `PlayerScript`, `AllMapScript`, and `WorldScript` hooks |
| `AutoBalanceCommands` | GM commands | `.ab` / `.autobalance` subcommands |

---

## 5. Chat Commands

All commands can be invoked via `.ab` or `.autobalance`:

- `.ab mapstat` / `.ab map`: Displays diagnostic statistics for the current instance (player counts, min players, combat lock status, level range, active creatures).
- `.ab creaturestat` / `.ab creature`: Displays detailed multiplier and base/scaled stats for the currently targeted creature.
- `.ab getoffset`: Displays the active player-count difficulty offset.
- `.ab setoffset <-N .. +N>`: Adjusts global difficulty offset on the fly and schedules refreshes for all tracked instances (Developer security rank `SEC_DEVELOPER` or console).
- `.ab reload`: Requests native full configuration reload without restarting the server (Administrator rank `SEC_ADMINISTRATOR` or console).
- `.ab info`: Displays module status and active feature toggles.

---

## 6. PlayerBots (`TortoiseBots`) Interoperability

When running with `TortoiseBots`:
- Bots with headless sessions (`session->IsHeadless()`) are recognized automatically.
- Server operators can configure `AutoBalance.CountBots = 1` and `AutoBalance.BotWeight = 1.0` (or fractional values such as `0.8`).
- Headless bot sessions are automatically filtered out from chat announcements, avoiding packet allocation overhead.

---

## 7. Credits

- Based on the original AzerothCore `mod-autobalance` module by **VAS**, **CVMagic**, and the **AzerothCore Community**.
- Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272).
