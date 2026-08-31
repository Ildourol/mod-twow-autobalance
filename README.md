# mod-twow-autobalance for TortoiseWoW / Turtle WoW

Native Vanilla 1.12.1 port of the acclaimed AzerothCore **mod-autobalance** module, adapted for **TortoiseWoW** and **Turtle WoW**.

---

## 1. Overview

`mod-twow-autobalance` dynamically adjusts dungeon and raid encounter difficulty based on the number and levels of active players inside an instance. It allows small groups (or even solo players with or without PlayerBots) to experience group content with smoothly scaled health, mana, armor, damage, and crowd-control mechanics while maintaining the intended boss encounter pacing.

### Key Highlights
- **Smooth Hyperbolic Tangent (tanh) Scaling**: Replaces simplistic linear multipliers with smooth sigmoid curves parameterized by inflection points, floor, ceiling, and boss modifiers.
- **Dynamic & Fixed Level Scaling**: Scales creature levels, health, mana, and damage relative to the highest player level in the party.
- **Separate Boss Tuning**: Custom modifiers and curves for dungeon bosses vs trash mobs.
- **Combat Locking**: Prevents player count exploits during active boss encounters.
- **Reward Scaling**: Configurable XP and money drop scaling based on effective group size.
- **PlayerBots Compatibility**: Seamlessly integrates with TortoiseWoW / cmangos PlayerBots.
- **Coexistence Protection**: Zero double-scaling when both core `AutoScaler` and `mod-twow-autobalance` are present.

---

## 2. Installation

The module must be installed inside the TortoiseWoW source tree **before configuring/building the core**.

### 2.1 Clone the module

From the root of your TortoiseWoW source tree:

```bash
cd /path/to/tortoise-wow
mkdir -p modules

git clone https://github.com/Ildourol/mod-twow-autobalance.git modules/mod-twow-autobalance
```

Your source tree should then contain:

```text
tortoise-wow/
├── modules/
│   └── mod-twow-autobalance/
├── src/
└── ...
```

### 2.2 Apply the required vMaNGOS/TortoiseWoW core hooks

`mod-twow-autobalance` requires several small core integration hooks for creature lifecycle handling and money-loot scaling.

From the **TortoiseWoW source-tree root**, first check whether the supplied patch applies cleanly:

```bash
git apply --check modules/mod-twow-autobalance/patches/vmangos-core-hooks.patch
```

If the command prints no errors, apply it:

```bash
git apply modules/mod-twow-autobalance/patches/vmangos-core-hooks.patch
```

You can inspect the resulting core changes with:

```bash
git diff
```

#### Shyalya/tortoise-wow users

The current `Shyalya/tortoise-wow` tree already contains the `OnCreatureAddWorld` and `OnCreatureRemoveWorld` callback wiring, but it may not contain all of the additional hooks required by this module. Because of that, the complete patch can report overlapping hunks.

If `git apply --check` fails, use:

```bash
git apply --reject modules/mod-twow-autobalance/patches/vmangos-core-hooks.patch
```

Then list any rejected hunks:

```bash
find . -name "*.rej"
```

Do **not** add duplicate `OnCreatureAddWorld` or `OnCreatureRemoveWorld` calls if your core already contains them. The important final integration is that the core provides:

- `OnCreatureRemoveWorld` notification;
- `OnCreatureRespawnWorld` notification after the creature's base level/stats are selected;
- `OnBeforeCreatureGenerateMoneyLoot` before the core calls `GenerateMoneyLoot`;
- `AutoScaler::ModifyMoneyLoot(...)`, so the legacy core scaler modifies the money range instead of generating money independently.

These hooks allow `mod-twow-autobalance` and the legacy TortoiseWoW `AutoScaler` to coexist without double-scaling.

### 2.3 Configure the module

Copy the distributed configuration file:

```bash
cp modules/mod-twow-autobalance/mod-twow-autobalance.conf.dist \
   modules/mod-twow-autobalance/mod-twow-autobalance.conf
```

At minimum, enable the module with:

```ini
TwowAutoBalance.Enable = 1
```

`AutoBalance.Enable.Global = 1` is also accepted as a compatibility alias.

### 2.4 Configure and build TortoiseWoW

After the module and core patch are in place, configure and build TortoiseWoW using your normal build procedure. If you already configured CMake before adding the module, re-run CMake so the module is discovered.

For example, if you use an out-of-source build directory:

```bash
cd /path/to/tortoise-wow
mkdir -p build
cd build

cmake .. <your normal TortoiseWoW CMake options>
cmake --build . -j$(nproc)
```

Use the same CMake options and installation paths that you normally use for your TortoiseWoW server.

### 2.5 Verify the installation

After starting the world server, use:

```text
.ab info
```

or:

```text
.autobalance info
```

to verify that `mod-twow-autobalance` is loaded and enabled.

It is recommended to make a Git branch before modifying the core so the integration can be reverted easily:

```bash
git checkout -b twow-autobalance
```

---

## 3. Mathematical Methodology

### Inflection Point & Sigmoid Multiplier Formula

Let:
- $N_{\text{max}}$ be the maximum intended instance capacity (e.g. 5 for normal dungeons, 10/20/40 for raids).
- $P_{\text{eff}}$ be the adjusted effective player count in the instance.
- $I_f$ be the configured inflection factor (default: 0.5).
- $I = N_{\text{max}} \times I_f$ be the resulting player-count inflection point.
- $F$ be the curve floor (default: 0.0).
- $C$ be the curve ceiling (default: 1.0).

$$\text{diff} = \frac{N_{\text{max}}}{5.0} \times 1.5$$

$$\text{denom} = \left( \frac{\tanh\left(\frac{N_{\text{max}} - I}{\text{diff}}\right) + 1}{2} \right) \times (C - F) + F$$

$$\text{ceilingAdjustment} = \frac{C}{\text{denom}}$$

$$\text{multiplier} = \left( \frac{\tanh\left(\frac{P_{\text{eff}} - I}{\text{diff}}\right) + 1}{2} \right) \times (C \times \text{ceilingAdjustment} - F) + F$$

Each creature stat (Health, Mana, Armor, Damage, CC Duration) is computed from its unmodified base baseline:
- $\text{Health} = \max(1, \text{round}(\text{baseHealth} \times \text{multiplier} \times \text{mod}_{\text{global}} \times \text{mod}_{\text{health}} \times \text{ratio}_{\text{level}}))$
- $\text{Damage} = \text{baseDamage} \times \text{multiplier} \times \text{mod}_{\text{global}} \times \text{mod}_{\text{damage}} \times \text{ratio}_{\text{level}}$

---

## 4. Architecture & TortoiseWoW Seams

| Component | Description |
|---|---|
| `AutoBalance.h` | Core enums, structures, and data models. |
| `AutoBalanceConfig` | Configuration parser for `sConfig` options and per-instance overrides. |
| `AutoBalanceScaling` | Core mathematical engine for sigmoid inflection, level calculations, and base stat application. |
| `AutoBalanceManager` | Central lifecycle manager tracking map states, active creatures, and combat locks without memory leaks. |
| `AutoBalanceScripts` | Native `WorldScript`, `PlayerScript`, `UnitScript`, `AllMapScript`, and `AllCreatureScript` hooks. |
| `AutoBalanceCommands` | Chat command handler implementing `.ab` / `.autobalance` subcommands. |

---

## 5. Chat Commands

All commands can be invoked via `.ab` or `.autobalance`:

- `.ab mapstat` / `.ab map`: Displays diagnostic statistics for the current instance (player counts, min players, combat lock status, level range, active creatures).
- `.ab creaturestat` / `.ab creature`: Displays detailed multiplier and base/scaled stats for the currently targeted creature.
- `.ab getoffset`: Displays the active player-count difficulty offset.
- `.ab setoffset <-N .. +N>`: Adjusts difficulty offset on the fly and rescales live instances (Gamemaster only).
- `.ab reload`: Reloads `mod-twow-autobalance.conf` without restarting the server (Administrator only).
- `.ab info`: Displays module status and active feature toggles.

---

## 6. Coexistence with Core AutoScaler

TortoiseWoW includes a legacy `AutoScaler` implementation in `src/game/Autoscaling`.
`mod-twow-autobalance` enforces **strict mutual exclusion**:
- When `TwowAutoBalance.Enable = 1` or `AutoBalance.Enable.Global = 1`, core `AutoScaler` automatically yields execution.
- When `mod-twow-autobalance` is disabled, core `AutoScaler` operates normally according to `AutoScalerEnable`.
- **Zero double-scaling** occurs under any configuration combination.

The vMaNGOS/TortoiseWoW core integration also supplies three lifecycle seams used by this module:

- creature removal notification, so tracked state is released safely;
- creature respawn notification after `SelectLevel`, so base stats are captured again;
- a pre-money-generation hook, so either scaler can modify gold while the core always generates the loot.

For a core tree that does not already contain these seams, apply
`modules/mod-twow-autobalance/patches/vmangos-core-hooks.patch` from the source-tree root before building.

---

## 7. Configuration Reference

Copy `mod-twow-autobalance.conf.dist` to your `modules/` or server configuration folder as `mod-twow-autobalance.conf`.

Key settings:
- `TwowAutoBalance.Enable`: Master enable/disable (0 = Disabled, 1 = Enabled). `AutoBalance.Enable.Global` is a compatibility alias; enabling either one has the same effect.
- `AutoBalance.MinPlayers`: Minimum player assumption for 5-man dungeons.
- `AutoBalance.MinPlayers.Raid`: Minimum player assumption for raid dungeons.
- `AutoBalance.LevelScaling`: Enable/disable dynamic level scaling (0/1).
- `AutoBalance.PlayerChangeNotify`: In-game chat broadcast when party size changes difficulty.
- `AutoBalance.Coexistence.DisableCoreAutoScaler`: Protects against double-scaling.

---

## 8. License & Credits

- Ported to TortoiseWoW / Turtle WoW by Antigravity (Google DeepMind).
- Original AzerothCore mod-autobalance authors: **VAS**, **CVMagic**, and the **AzerothCore Community**.
- Licensed under the GNU General Public License v2 (GPLv2).
