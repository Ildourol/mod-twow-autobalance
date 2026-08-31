/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#include "AutoBalanceScaling.h"
#include "Maps/Map.h"
#include "Objects/Creature.h"
#include "Objects/Player.h"
#include "Log.h"
#include <algorithm>
#include <cmath>

namespace AutoBalanceScaling
{

uint32 GetMapMaxPlayers(Map* map)
{
    if (!map)
        return 5;

    if (map->IsDungeon())
    {
        DungeonMap* dMap = reinterpret_cast<DungeonMap*>(map);
        uint32 maxP = dMap->GetMaxPlayers();
        if (maxP > 0)
            return maxP;
    }

    return map->IsRaid() ? 40 : 5;
}

bool ShouldMapBeEnabled(Map* map)
{
    if (!map || !map->IsDungeon())
        return false;

    if (!sABConfig.IsGlobalEnabled())
        return false;

    if (sABConfig.IsDungeonDisabled(map->GetId()))
        return false;

    uint32 maxPlayers = GetMapMaxPlayers(map);
    switch (maxPlayers)
    {
        case 5:
            return sABConfig.Is5MEnabled();
        case 10:
            return sABConfig.Is10MEnabled();
        case 15:
            return sABConfig.Is15MEnabled();
        case 20:
            return sABConfig.Is20MEnabled();
        case 25:
            return sABConfig.Is25MEnabled();
        case 40:
            return sABConfig.Is40MEnabled();
        default:
            return sABConfig.IsOtherNormalEnabled();
    }
}

bool IsBoss(Creature* creature)
{
    if (!creature)
        return false;

    CreatureInfo const* cinfo = creature->GetCreatureInfo();
    if (!cinfo)
        return false;

    if (cinfo->rank == CREATURE_ELITE_WORLDBOSS || cinfo->rank == CREATURE_ELITE_RAREELITE)
        return true;

    if (creature->HasExtraFlag(CREATURE_FLAG_EXTRA_INSTANCE_BIND))
        return true;

    return false;
}

bool IsCreatureRelevant(Creature* creature)
{
    if (!creature)
        return false;

    if (creature->IsPet() || creature->IsTotem())
        return false;

    if (creature->GetOwner() && creature->GetOwner()->IsPlayer())
        return false;

    CreatureInfo const* cinfo = creature->GetCreatureInfo();
    if (!cinfo)
        return false;

    if (cinfo->type == CREATURE_TYPE_CRITTER)
        return false;

    if (sABConfig.IsCreatureDisabled(creature->GetEntry()))
        return false;

    return true;
}

AutoBalanceInflectionPointSettings GetInflectionPointSettings(Map* map, bool isBoss)
{
    uint32 mapId = map ? map->GetId() : 0;
    AutoBalanceInflectionPointSettings const* overrideSettings = sABConfig.GetMapInflectionOverride(mapId, isBoss);
    if (overrideSettings)
        return *overrideSettings;

    uint32 maxPlayers = GetMapMaxPlayers(map);
    AutoBalanceInflectionPointSettings settings = (map && map->IsRaid())
        ? sABConfig.GetRaidInflectionSettings(maxPlayers)
        : sABConfig.GetDefaultInflectionSettings();

    if (isBoss)
        settings.value *= settings.bossModifier;

    return settings;
}

AutoBalanceStatModifiers GetStatModifiers(Map* map, Creature* creature, bool isBoss)
{
    if (creature)
    {
        AutoBalanceStatModifiers const* cOverride = sABConfig.GetCreatureStatModifierOverride(creature->GetEntry());
        if (cOverride)
            return *cOverride;
    }

    uint32 mapId = map ? map->GetId() : 0;
    AutoBalanceStatModifiers const* mOverride = sABConfig.GetMapStatModifierOverride(mapId, isBoss);
    if (mOverride)
        return *mOverride;

    uint32 maxPlayers = GetMapMaxPlayers(map);
    if (map && map->IsRaid())
        return sABConfig.GetRaidStatModifiers(maxPlayers, isBoss);

    return isBoss ? sABConfig.GetBossStatModifiers() : sABConfig.GetNormalStatModifiers();
}

float GetDefaultMultiplier(uint32 maxPlayers, float adjustedPlayerCount, AutoBalanceInflectionPointSettings const& settings)
{
    if (maxPlayers == 0)
        maxPlayers = 5;

    float maxP = static_cast<float>(maxPlayers);
    // AutoBalance inflection values are fractions of the instance capacity
    // (0.5 means half full), not absolute player counts.
    float inflectionValue = maxP * settings.value;
    float diff = (maxP / 5.0f) * 1.5f;
    if (diff <= 0.0f)
        diff = 1.0f;

    float denom = ((std::tanh((maxP - inflectionValue) / diff) + 1.0f) / 2.0f) *
                  (settings.curveCeiling - settings.curveFloor) + settings.curveFloor;
    if (denom <= 0.0001f)
        denom = 0.0001f;

    float curveCeilingAdjustment = settings.curveCeiling / denom;

    float defaultMultiplier =
        ((std::tanh((adjustedPlayerCount - inflectionValue) / diff) + 1.0f) / 2.0f) *
        (settings.curveCeiling * curveCeilingAdjustment - settings.curveFloor) + settings.curveFloor;

    if (defaultMultiplier < 0.0001f)
        defaultMultiplier = 0.0001f;

    return defaultMultiplier;
}

uint8 CalculateScaledLevel(Creature* creature, AutoBalanceMapInfo const& mapInfo, uint8 unmodifiedLevel)
{
    if (!creature || !mapInfo.isLevelScalingEnabled)
        return unmodifiedLevel;

    if (mapInfo.highestPlayerLevel == 0)
        return unmodifiedLevel;

    int const baseLevel = static_cast<int>(unmodifiedLevel);
    int const playerLevel = static_cast<int>(mapInfo.highestPlayerLevel);
    int const delta = baseLevel - playerLevel;

    // Creatures already near the group's level stay at their native level.
    if (delta <= static_cast<int>(mapInfo.levelScalingSkipHigherLevels) &&
        delta >= -static_cast<int>(mapInfo.levelScalingSkipLowerLevels))
        return unmodifiedLevel;

    if (sABConfig.GetLevelScalingMethod() == AUTOBALANCE_SCALING_FIXED)
        return mapInfo.highestPlayerLevel;

    // Dynamic level scaling
    if (mapInfo.highestCreatureLevel == 0)
        return unmodifiedLevel;

    int const levelOffset = baseLevel - static_cast<int>(mapInfo.highestCreatureLevel);
    int targetLevel = playerLevel + static_cast<int>(mapInfo.levelScalingDynamicCeiling) + levelOffset;

    int const minLevel = playerLevel - static_cast<int>(mapInfo.levelScalingDynamicFloor);
    int const maxLevel = playerLevel + static_cast<int>(mapInfo.levelScalingDynamicCeiling);

    targetLevel = std::max(minLevel, std::min(maxLevel, targetLevel));
    return static_cast<uint8>(std::max(1, std::min(63, targetLevel)));
}

void InitializeCreatureBaseData(Creature* creature, AutoBalanceCreatureInfo& cInfo)
{
    if (cInfo.baseData.initialized || !creature)
        return;

    CreatureInfo const* cinfo = creature->GetCreatureInfo();
    if (!cinfo)
        return;

    cInfo.baseData.baseHealth = creature->GetCreateHealth();
    if (cInfo.baseData.baseHealth == 0)
        cInfo.baseData.baseHealth = cinfo->health_min;

    cInfo.baseData.baseMana = creature->GetCreateMana();
    if (cInfo.baseData.baseMana == 0)
        cInfo.baseData.baseMana = cinfo->mana_min;

    cInfo.baseData.baseArmor = cinfo->armor;
    // Capture vMaNGOS' initialized weapon ranges. Raw template values do not
    // include the configured elite/world-boss rank damage modifier.
    cInfo.baseData.baseMinDamage = creature->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    cInfo.baseData.baseMaxDamage = creature->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);
    cInfo.baseData.baseRangedMinDamage = creature->GetWeaponDamageRange(RANGED_ATTACK, MINDAMAGE);
    cInfo.baseData.baseRangedMaxDamage = creature->GetWeaponDamageRange(RANGED_ATTACK, MAXDAMAGE);
    cInfo.baseData.baseAttackPower = cinfo->attack_power;
    cInfo.baseData.baseLevel = creature->GetLevel();
    cInfo.baseData.baseGoldMin = cinfo->gold_min;
    cInfo.baseData.baseGoldMax = cinfo->gold_max;
    cInfo.baseData.initialized = true;

    cInfo.unmodifiedLevel = cInfo.baseData.baseLevel;
    cInfo.selectedLevel = cInfo.baseData.baseLevel;
    cInfo.isBoss = IsBoss(creature);
}

void CalculateMultipliers(Map* map, Creature* creature, AutoBalanceMapInfo const& mapInfo, AutoBalanceCreatureInfo& cInfo)
{
    if (!creature)
        return;

    InitializeCreatureBaseData(creature, cInfo);

    uint32 maxPlayers = GetMapMaxPlayers(map);
    int forcedCount = sABConfig.GetForcedPlayerCount(creature->GetEntry());
    float effectivePlayers = (forcedCount > 0) ? static_cast<float>(forcedCount) : static_cast<float>(mapInfo.adjustedPlayerCount);
    if (effectivePlayers < 1.0f)
        effectivePlayers = 1.0f;

    AutoBalanceInflectionPointSettings inflection = GetInflectionPointSettings(map, cInfo.isBoss);
    AutoBalanceStatModifiers statMod = GetStatModifiers(map, creature, cInfo.isBoss);

    float defaultMultiplier = GetDefaultMultiplier(maxPlayers, effectivePlayers, inflection);

    cInfo.damageMultiplier = std::max(sABConfig.GetMinDamageModifier(), defaultMultiplier * statMod.global * statMod.damage);
    cInfo.healthMultiplier = std::max(sABConfig.GetMinHPModifier(), defaultMultiplier * statMod.global * statMod.health);
    cInfo.manaMultiplier   = std::max(sABConfig.GetMinManaModifier(), defaultMultiplier * statMod.global * statMod.mana);
    cInfo.armorMultiplier  = std::max(0.0f, defaultMultiplier * statMod.global * statMod.armor);
    cInfo.ccDurationMultiplier = std::min(sABConfig.GetMaxCCDurationModifier(),
                                 std::max(sABConfig.GetMinCCDurationModifier(), defaultMultiplier * statMod.ccduration));

    // Level scaling calculations
    if (mapInfo.isLevelScalingEnabled && !cInfo.neverLevelScale)
    {
        cInfo.selectedLevel = CalculateScaledLevel(creature, mapInfo, cInfo.unmodifiedLevel);
        if (cInfo.unmodifiedLevel > 0 && cInfo.selectedLevel != cInfo.unmodifiedLevel)
        {
            float levelRatio = static_cast<float>(cInfo.selectedLevel) / static_cast<float>(cInfo.unmodifiedLevel);
            cInfo.scaledHealthMultiplier = cInfo.healthMultiplier * levelRatio;
            cInfo.scaledManaMultiplier   = cInfo.manaMultiplier * levelRatio;
            cInfo.scaledArmorMultiplier  = cInfo.armorMultiplier * levelRatio;
            cInfo.scaledDamageMultiplier = cInfo.damageMultiplier * levelRatio;
        }
        else
        {
            cInfo.scaledHealthMultiplier = cInfo.healthMultiplier;
            cInfo.scaledManaMultiplier   = cInfo.manaMultiplier;
            cInfo.scaledArmorMultiplier  = cInfo.armorMultiplier;
            cInfo.scaledDamageMultiplier = cInfo.damageMultiplier;
        }
    }
    else
    {
        cInfo.selectedLevel = cInfo.unmodifiedLevel;
        cInfo.scaledHealthMultiplier = cInfo.healthMultiplier;
        cInfo.scaledManaMultiplier   = cInfo.manaMultiplier;
        cInfo.scaledArmorMultiplier  = cInfo.armorMultiplier;
        cInfo.scaledDamageMultiplier = cInfo.damageMultiplier;
    }

    // Reward scaling calculations
    if (sABConfig.IsRewardScalingXPEnabled())
    {
        if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_DYNAMIC)
            cInfo.xpModifier = defaultMultiplier * sABConfig.GetRewardScalingXPModifier();
        else
            cInfo.xpModifier = sABConfig.GetRewardScalingXPModifier();
    }
    else
        cInfo.xpModifier = 1.0f;

    if (sABConfig.IsRewardScalingMoneyEnabled())
    {
        if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_DYNAMIC)
            cInfo.moneyModifier = defaultMultiplier * sABConfig.GetRewardScalingMoneyModifier();
        else
            cInfo.moneyModifier = sABConfig.GetRewardScalingMoneyModifier();
    }
    else
        cInfo.moneyModifier = 1.0f;

    cInfo.instancePlayerCount = mapInfo.playerCount;
    cInfo.mapConfigTime = mapInfo.mapConfigTime;
    cInfo.isActive = true;
}

void ApplyCreatureStats(Creature* creature, AutoBalanceCreatureInfo const& cInfo)
{
    if (!creature || !cInfo.baseData.initialized)
        return;

    // Preserve HP/Mana percent and never resurrect a dead creature while a
    // configuration or player-count update is being applied.
    bool const wasAlive = creature->IsAlive();
    float hpPercent = creature->GetHealthPercent() / 100.0f;
    float manaPercent = creature->GetPowerPercent(POWER_MANA) / 100.0f;

    if (cInfo.selectedLevel != creature->GetLevel())
        creature->SetLevel(cInfo.selectedLevel);

    uint32 newMaxHealth = std::max(1u, static_cast<uint32>(std::round(static_cast<float>(cInfo.baseData.baseHealth) * cInfo.scaledHealthMultiplier)));
    uint32 newMaxMana   = static_cast<uint32>(std::round(static_cast<float>(cInfo.baseData.baseMana) * cInfo.scaledManaMultiplier));
    uint32 newArmor     = static_cast<uint32>(std::round(static_cast<float>(cInfo.baseData.baseArmor) * cInfo.scaledArmorMultiplier));

    float newMinDamage  = cInfo.baseData.baseMinDamage * cInfo.scaledDamageMultiplier;
    float newMaxDamage  = cInfo.baseData.baseMaxDamage * cInfo.scaledDamageMultiplier;
    float newRangedMinDamage = cInfo.baseData.baseRangedMinDamage * cInfo.scaledDamageMultiplier;
    float newRangedMaxDamage = cInfo.baseData.baseRangedMaxDamage * cInfo.scaledDamageMultiplier;

    creature->SetMaxHealth(newMaxHealth);
    creature->SetHealth(wasAlive ? std::max(1u, static_cast<uint32>(std::round(static_cast<float>(newMaxHealth) * hpPercent))) : 0u);

    if (cInfo.baseData.baseMana > 0)
    {
        creature->SetMaxPower(POWER_MANA, newMaxMana);
        creature->SetPower(POWER_MANA, static_cast<uint32>(std::round(static_cast<float>(newMaxMana) * manaPercent)));
        creature->SetModifierValue(UNIT_MOD_MANA, BASE_VALUE, static_cast<float>(newMaxMana));
    }

    creature->SetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE, static_cast<float>(newArmor));
    creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, static_cast<float>(newMaxHealth));

    creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, newMinDamage);
    creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, newMaxDamage);

    creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, newMinDamage);
    creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, newMaxDamage);

    creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, newRangedMinDamage);
    creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, newRangedMaxDamage);

    creature->UpdateDamagePhysical(BASE_ATTACK);
    creature->UpdateDamagePhysical(OFF_ATTACK);
    creature->UpdateDamagePhysical(RANGED_ATTACK);
}

void RestoreCreatureStats(Creature* creature, AutoBalanceCreatureInfo& cInfo)
{
    if (!creature || !cInfo.baseData.initialized)
        return;

    bool const wasAlive = creature->IsAlive();
    float const hpPercent = creature->GetHealthPercent() / 100.0f;
    float const manaPercent = creature->GetPowerPercent(POWER_MANA) / 100.0f;

    creature->SetLevel(cInfo.baseData.baseLevel);
    creature->SetMaxHealth(std::max(1u, cInfo.baseData.baseHealth));
    creature->SetHealth(wasAlive ? std::max(1u, static_cast<uint32>(std::round(cInfo.baseData.baseHealth * hpPercent))) : 0u);
    creature->SetModifierValue(UNIT_MOD_HEALTH, BASE_VALUE, static_cast<float>(cInfo.baseData.baseHealth));

    if (cInfo.baseData.baseMana > 0)
    {
        creature->SetMaxPower(POWER_MANA, cInfo.baseData.baseMana);
        creature->SetPower(POWER_MANA, static_cast<uint32>(std::round(cInfo.baseData.baseMana * manaPercent)));
        creature->SetModifierValue(UNIT_MOD_MANA, BASE_VALUE, static_cast<float>(cInfo.baseData.baseMana));
    }

    creature->SetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE, static_cast<float>(cInfo.baseData.baseArmor));
    creature->SetBaseWeaponDamage(BASE_ATTACK, MINDAMAGE, cInfo.baseData.baseMinDamage);
    creature->SetBaseWeaponDamage(BASE_ATTACK, MAXDAMAGE, cInfo.baseData.baseMaxDamage);
    creature->SetBaseWeaponDamage(OFF_ATTACK, MINDAMAGE, cInfo.baseData.baseMinDamage);
    creature->SetBaseWeaponDamage(OFF_ATTACK, MAXDAMAGE, cInfo.baseData.baseMaxDamage);
    creature->SetBaseWeaponDamage(RANGED_ATTACK, MINDAMAGE, cInfo.baseData.baseRangedMinDamage);
    creature->SetBaseWeaponDamage(RANGED_ATTACK, MAXDAMAGE, cInfo.baseData.baseRangedMaxDamage);
    creature->UpdateDamagePhysical(BASE_ATTACK);
    creature->UpdateDamagePhysical(OFF_ATTACK);
    creature->UpdateDamagePhysical(RANGED_ATTACK);

    cInfo.selectedLevel = cInfo.unmodifiedLevel;
    cInfo.damageMultiplier = 1.0f;
    cInfo.scaledDamageMultiplier = 1.0f;
    cInfo.healthMultiplier = 1.0f;
    cInfo.scaledHealthMultiplier = 1.0f;
    cInfo.manaMultiplier = 1.0f;
    cInfo.scaledManaMultiplier = 1.0f;
    cInfo.armorMultiplier = 1.0f;
    cInfo.scaledArmorMultiplier = 1.0f;
    cInfo.ccDurationMultiplier = 1.0f;
    cInfo.xpModifier = 1.0f;
    cInfo.moneyModifier = 1.0f;
    cInfo.isActive = false;
}

} // namespace AutoBalanceScaling
