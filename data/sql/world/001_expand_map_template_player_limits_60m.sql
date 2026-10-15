-- ==============================================================================
-- AutoBalance Migration: Expand Instance Player Limits to 60 Players
-- Module: mod-twow-autobalance
-- Target: tw_world (Turtle WoW vMaNGOS)
-- Description:
--   Updates `map_template.player_limit` for all dungeon and raid instances to allow
--   up to 60 players inside simultaneously.
--
-- Core Engine Behavior:
--   `DungeonMap::CanEnter` enforces `GetPlayersCountExceptGMs() >= maxPlayers`.
--   By updating `player_limit` in `map_template`, the core engine permits up to
--   60 players to zone through the instance portal without core source code edits.
--
-- Group Coordination Note:
--   - In 1.12.1 / Turtle WoW client UI, standard party frames support up to 5 players
--     and raid frames support up to 40 players (8 subgroups of 5).
--   - To bring more than 5 players into a dungeon, form a Raid group (/raid).
--   - To enter raids solo or in normal party without converting to raid, set
--     `Instance.IgnoreRaid = 1` in `mangosd.conf`.
-- ==============================================================================

-- ------------------------------------------------------------------------------
-- 1. Classic & Turtle WoW 5-Man Leveling Dungeons (Expanded to 60)
-- ------------------------------------------------------------------------------
UPDATE `map_template` SET `player_limit` = 60 WHERE `entry` IN (
    33,  -- Shadowfang Keep
    34,  -- Stormwind Stockade
    35,  -- Stormwind Vault (Turtle WoW Custom)
    36,  -- The Deadmines
    43,  -- Wailing Caverns
    47,  -- Razorfen Kraul
    48,  -- Blackfathom Deeps
    70,  -- Uldaman
    90,  -- Gnomeregan
    109, -- Sunken Temple (Temple of Atal'Hakkar)
    129, -- Razorfen Downs
    189, -- Scarlet Monastery
    209, -- Zul'Farrak
    230, -- Blackrock Depths
    269, -- Black Morass / Opening of the Dark Portal
    289, -- Scholomance
    329, -- Stratholme
    349, -- Maraudon
    389, -- Ragefire Chasm
    429, -- Dire Maul
    800, -- Karazhan Crypt (Turtle WoW Custom)
    802, -- Crescent Grove (Turtle WoW Custom)
    806, -- Frostmane Retreat (Turtle WoW Custom)
    808, -- Hateforge Quarry (Turtle WoW Custom)
    815, -- Gilneas City (Turtle WoW Custom)
    818  -- Stormwrought Ruins (Turtle WoW Custom)
);

-- ------------------------------------------------------------------------------
-- 2. 10-Player Instances & Raids (Expanded to 60)
-- ------------------------------------------------------------------------------
UPDATE `map_template` SET `player_limit` = 60 WHERE `entry` IN (
    229, -- Blackrock Spire (UBRS / LBRS)
    532  -- Lower Karazhan Halls (Turtle WoW Custom)
);

-- ------------------------------------------------------------------------------
-- 3. 20-Player Raids (Expanded to 60)
-- ------------------------------------------------------------------------------
UPDATE `map_template` SET `player_limit` = 60 WHERE `entry` IN (
    309, -- Zul'Gurub
    509  -- Ruins of Ahn'Qiraj (AQ20)
);

-- ------------------------------------------------------------------------------
-- 4. 40-Player Raids (Expanded to 60)
-- ------------------------------------------------------------------------------
UPDATE `map_template` SET `player_limit` = 60 WHERE `entry` IN (
    44,  -- Old Scarlet Citadel (Turtle WoW Custom)
    45,  -- Scarlet Citadel (Turtle WoW Custom)
    249, -- Onyxia's Lair
    409, -- Molten Core
    469, -- Blackwing Lair
    531, -- Temple of Ahn'Qiraj (AQ40)
    533, -- Naxxramas
    807, -- Emerald Sanctum (Turtle WoW Custom)
    814, -- Tower of Karazhan / Karazhan Upper (Turtle WoW Custom)
    816  -- Dragonmaw Retreat (Turtle WoW Custom)
);
