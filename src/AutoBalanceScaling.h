/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#ifndef MOD_TWOW_AUTOBALANCE_SCALING_H
#define MOD_TWOW_AUTOBALANCE_SCALING_H

#include "AutoBalance.h"
#include "AutoBalanceConfig.h"

class Map;
class Creature;
class Player;

namespace AutoBalanceScaling
{
    bool ShouldMapBeEnabled(Map* map);
    uint32 GetMapMaxPlayers(Map* map);
    bool IsMapRaidActive(Map* map);

    bool IsBoss(Creature* creature);
    bool IsCreatureRelevant(Creature* creature);
    bool IsCreatureFriendlyToAnyPlayer(Creature* creature, Map* map);
    bool IsCreatureWithinPlayerDistance(Creature* creature, Map* map, uint32 distance);

    AutoBalanceInflectionPointSettings GetInflectionPointSettings(Map* map, bool isBoss);
    AutoBalanceStatModifiers GetStatModifiers(Map* map, Creature* creature, bool isBoss);

    float GetDefaultMultiplier(uint32 maxPlayers, float adjustedPlayerCount, AutoBalanceInflectionPointSettings const& settings);

    void LoadCreatureClassLevelStats();
    float GetClassLevelHealth(uint8 unitClass, uint8 level);
    float GetClassLevelDamage(uint8 unitClass, uint8 level);
    float GetClassLevelArmor(uint8 unitClass, uint8 level);
    float GetClassLevelMana(uint8 unitClass, uint8 level);

    uint8 CalculateScaledLevel(Creature* creature, AutoBalanceMapInfo const& mapInfo, uint8 unmodifiedLevel);

    void InitializeCreatureBaseData(Creature* creature, AutoBalanceCreatureInfo& cInfo);
    void CalculateMultipliers(Map* map, Creature* creature, AutoBalanceMapInfo const& mapInfo, AutoBalanceCreatureInfo& cInfo);
    void ApplyCreatureStats(Creature* creature, AutoBalanceCreatureInfo const& cInfo);
    void RestoreCreatureStats(Creature* creature, AutoBalanceCreatureInfo& cInfo);
}

#endif // MOD_TWOW_AUTOBALANCE_SCALING_H
