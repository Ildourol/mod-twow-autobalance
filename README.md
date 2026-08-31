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

## 2. Mathematical Methodology

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

## 3. Architecture & TortoiseWoW Seams

| Component | Description |
|---|---|
| `AutoBalance.h` | Core enums, structures, and data models. |
| `AutoBalanceConfig` | Configuration parser for `sConfig` options and per-instance overrides. |
| `AutoBalanceScaling` | Core mathematical engine for sigmoid inflection, level calculations, and base stat application. |
| `AutoBalanceManager` | Central lifecycle manager tracking map states, active creatures, and combat locks without memory leaks. |
| `AutoBalanceScripts` | Native `WorldScript`, `PlayerScript`, `UnitScript`, `AllMapScript`, and `AllCreatureScript` hooks. |
| `AutoBalanceCommands` | Chat command handler implementing `.ab` / `.autobalance` subcommands. |

---

## 4. Chat Commands

All commands can be invoked via `.ab` or `.autobalance`:

- `.ab mapstat` / `.ab map`: Displays diagnostic statistics for the current instance (player counts, min players, combat lock status, level range, active creatures).
- `.ab creaturestat` / `.ab creature`: Displays detailed multiplier and base/scaled stats for the currently targeted creature.
- `.ab getoffset`: Displays the active player-count difficulty offset.
- `.ab setoffset <-N .. +N>`: Adjusts difficulty offset on the fly and rescales live instances (Gamemaster only).
- `.ab reload`: Reloads `mod-twow-autobalance.conf` without restarting the server (Administrator only).
- `.ab info`: Displays module status and active feature toggles.

---

## 5. Coexistence with Core AutoScaler

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

## 6. Configuration Reference

Copy `mod-twow-autobalance.conf.dist` to your `modules/` or server configuration folder as `mod-twow-autobalance.conf`.

Key settings:
- `TwowAutoBalance.Enable`: Master enable/disable (0 = Disabled, 1 = Enabled). `AutoBalance.Enable.Global` is a compatibility alias; enabling either one has the same effect.
- `AutoBalance.MinPlayers`: Minimum player assumption for 5-man dungeons.
- `AutoBalance.MinPlayers.Raid`: Minimum player assumption for raid dungeons.
- `AutoBalance.LevelScaling`: Enable/disable dynamic level scaling (0/1).
- `AutoBalance.PlayerChangeNotify`: In-game chat broadcast when party size changes difficulty.
- `AutoBalance.Coexistence.DisableCoreAutoScaler`: Protects against double-scaling.

---

## 7. License & Credits

- Ported to TortoiseWoW / Turtle WoW by Antigravity (Google DeepMind).
- Original AzerothCore mod-autobalance authors: **VAS**, **CVMagic**, and the **AzerothCore Community**.
- Licensed under the GNU General Public License v2 (GPLv2).
