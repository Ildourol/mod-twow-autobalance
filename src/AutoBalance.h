/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Copyright (C) 2008-2010 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2006-2009 ScriptDev2 <https://scriptdev2.svn.sourceforge.net/>
 * Copyright (C) 1985-2010 KalCorp <http://vasserver.dyndns.org/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#ifndef MOD_TWOW_AUTOBALANCE_H
#define MOD_TWOW_AUTOBALANCE_H

#include "Common.h"
#include "ObjectGuid.h"
#include "SharedDefines.h"
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum ScalingMethod
{
    AUTOBALANCE_SCALING_FIXED = 0,
    AUTOBALANCE_SCALING_DYNAMIC = 1
};

enum BaseValueType
{
    AUTOBALANCE_HEALTH = 0,
    AUTOBALANCE_DAMAGE_HEALING = 1
};

enum Relevance
{
    AUTOBALANCE_RELEVANCE_FALSE = 0,
    AUTOBALANCE_RELEVANCE_TRUE = 1,
    AUTOBALANCE_RELEVANCE_UNCHECKED = 2
};

enum Damage_Healing_Debug_Phase
{
    AUTOBALANCE_DAMAGE_HEALING_DEBUG_PHASE_BEFORE = 0,
    AUTOBALANCE_DAMAGE_HEALING_DEBUG_PHASE_AFTER = 1
};

struct World_Multipliers
{
    float scaled   = 1.0f;
    float unscaled = 1.0f;
};

struct AutoBalanceInflectionPointSettings
{
    AutoBalanceInflectionPointSettings() = default;
    AutoBalanceInflectionPointSettings(float val, float floor, float ceiling, float bossMod = 1.0f)
        : value(val), curveFloor(floor), curveCeiling(ceiling), bossModifier(bossMod) {}

    float value        = 0.5f;
    float curveFloor   = 0.0f;
    float curveCeiling = 1.0f;
    float bossModifier = 1.0f;
};

struct AutoBalanceStatModifiers
{
    AutoBalanceStatModifiers() = default;
    AutoBalanceStatModifiers(float g, float h, float m, float a, float d, float cc)
        : global(g), health(h), mana(m), armor(a), damage(d), ccduration(cc) {}

    float global     = 1.0f;
    float health     = 1.0f;
    float mana       = 1.0f;
    float armor      = 1.0f;
    float damage     = 1.0f;
    float ccduration = 1.0f;
};

struct AutoBalanceDynamicLevelSettings
{
    AutoBalanceDynamicLevelSettings() = default;
    AutoBalanceDynamicLevelSettings(int higher, int lower, int ceil, int flr)
        : skipHigher(higher), skipLower(lower), ceiling(ceil), floor(flr) {}

    int skipHigher = 3;
    int skipLower  = 5;
    int ceiling    = 1;
    int floor      = 5;
};

struct CreatureBaseData
{
    uint32 baseHealth = 0;
    uint32 baseMana = 0;
    uint32 baseArmor = 0;
    float  baseMinDamage = 0.0f;
    float  baseMaxDamage = 0.0f;
    float  baseRangedMinDamage = 0.0f;
    float  baseRangedMaxDamage = 0.0f;
    int32  baseAttackPower = 0;
    uint8  baseLevel = 1;
    uint32 baseGoldMin = 0;
    uint32 baseGoldMax = 0;
    bool   initialized = false;
};

struct AutoBalanceCreatureInfo
{
    mutable std::recursive_mutex stateLock;

    uint64_t   mapConfigTime          = 1;
    uint32     instancePlayerCount    = 0;
    uint8      selectedLevel          = 0;
    uint8      unmodifiedLevel        = 0;

    float      damageMultiplier       = 1.0f;
    float      scaledDamageMultiplier = 1.0f;

    float      healthMultiplier       = 1.0f;
    float      scaledHealthMultiplier = 1.0f;

    float      manaMultiplier         = 1.0f;
    float      scaledManaMultiplier   = 1.0f;

    float      armorMultiplier        = 1.0f;
    float      scaledArmorMultiplier  = 1.0f;

    float      ccDurationMultiplier   = 1.0f;
    float      xpModifier             = 1.0f;
    float      moneyModifier          = 1.0f;

    bool       isActive               = false;
    bool       wasAliveNowDead        = false;
    bool       neverLevelScale        = false;
    bool       isBoss                 = false;
    Relevance  relevance              = AUTOBALANCE_RELEVANCE_UNCHECKED;

    CreatureBaseData baseData;
};

struct AutoBalanceMapInfo
{
    mutable std::recursive_mutex stateLock;
    bool       enabled                        = false;
    uint64_t   globalConfigTime               = 1;
    uint64_t   mapConfigTime                  = 1;

    uint8      playerCount                    = 0;
    uint8      adjustedPlayerCount            = 0;
    uint8      minPlayers                     = 1;

    uint8      mapLevel                       = 0;
    uint8      lowestPlayerLevel              = 0;
    uint8      highestPlayerLevel             = 0;

    uint8      lowestCreatureLevel            = 0;
    uint8      highestCreatureLevel           = 0;
    float      avgCreatureLevel               = 0.0f;
    uint32     activeCreatureCount            = 0;

    float      worldDamageMultiplier          = 1.0f;
    float      scaledWorldDamageMultiplier    = 1.0f;
    float      worldHealthMultiplier          = 1.0f;

    bool       combatLocked                   = false;
    bool       combatLockTripped              = false;
    bool       pendingRescale                 = false;
    uint8      combatLockMinPlayers           = 0;

    bool       isLevelScalingEnabled          = false;
    uint8      levelScalingSkipHigherLevels   = 3;
    uint8      levelScalingSkipLowerLevels    = 5;
    uint8      levelScalingDynamicCeiling     = 1;
    uint8      levelScalingDynamicFloor       = 5;

    bool       initialized                    = false;
    bool       configPending                  = false;

    std::set<ObjectGuid> players;
    std::set<ObjectGuid> creatures;
};

#endif // MOD_TWOW_AUTOBALANCE_H
