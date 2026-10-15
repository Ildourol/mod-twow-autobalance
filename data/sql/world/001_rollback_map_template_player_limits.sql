-- ==============================================================================
-- AutoBalance Migration Rollback: Restore Stock map_template Player Limits
-- Module: mod-twow-autobalance
-- Target: tw_world (Turtle WoW vMaNGOS)
-- Description:
--   Restores original `map_template.player_limit` values for all instances to
--   authentic Vanilla vMaNGOS and Turtle WoW stock defaults.
-- ==============================================================================

-- 1. Classic Vanilla Leveling Dungeons (Stock legacy default: 10)
UPDATE `map_template` SET `player_limit` = 10 WHERE `entry` IN (
    33,  -- Shadowfang Keep
    34,  -- Stormwind Stockade
    36,  -- The Deadmines
    43,  -- Wailing Caverns
    47,  -- Razorfen Kraul
    48,  -- Blackfathom Deeps
    70,  -- Uldaman
    90,  -- Gnomeregan
    109, -- Sunken Temple
    129, -- Razorfen Downs
    189, -- Scarlet Monastery
    209, -- Zul'Farrak
    230, -- Blackrock Depths
    289, -- Scholomance
    329, -- Stratholme
    349, -- Maraudon
    389, -- Ragefire Chasm
    429  -- Dire Maul
);

-- 2. Turtle WoW Custom 5-Man Dungeons (Stock default: 5)
UPDATE `map_template` SET `player_limit` = 5 WHERE `entry` IN (
    35,  -- Stormwind Vault
    269, -- Black Morass
    800, -- Karazhan Crypt
    802, -- Crescent Grove
    806, -- Frostmane Retreat
    808, -- Hateforge Quarry
    815, -- Gilneas City (10 in legacy, 5 standard)
    818  -- Stormwrought Ruins
);

-- 3. 10-Player Instances & Raids (Stock default: 10)
UPDATE `map_template` SET `player_limit` = 10 WHERE `entry` IN (
    229, -- Blackrock Spire (UBRS / LBRS)
    532  -- Lower Karazhan Halls
);

-- 4. 20-Player Raids (Stock default: 20)
UPDATE `map_template` SET `player_limit` = 20 WHERE `entry` IN (
    309, -- Zul'Gurub
    509  -- Ruins of Ahn'Qiraj (AQ20)
);

-- 5. 40-Player Raids (Stock default: 40)
UPDATE `map_template` SET `player_limit` = 40 WHERE `entry` IN (
    44,  -- Old Scarlet Citadel
    45,  -- Scarlet Citadel
    249, -- Onyxia's Lair
    409, -- Molten Core
    469, -- Blackwing Lair
    531, -- Temple of Ahn'Qiraj (AQ40)
    533, -- Naxxramas
    807, -- Emerald Sanctum
    814, -- Tower of Karazhan
    816  -- Dragonmaw Retreat
);
