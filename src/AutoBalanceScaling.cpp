/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#include "AutoBalanceScaling.h"
#include "AutoBalanceClassStatsData.h"
#include "AutoBalanceManager.h"
#include "Maps/Map.h"
#include "Objects/Creature.h"
#include "Objects/Player.h"
#include "Objects/TemporarySummon.h"
#include "Group/Group.h"
#include "Log.h"
#include <algorithm>
#include <cmath>

namespace AutoBalanceScaling
{

static inline CanonicalClassLevelStats const& GetCanonicalClassStats(uint8 unitClass, uint8 level)
{
    uint8 const lvl = static_cast<uint8>(std::max<uint8>(1, std::min<uint8>(63, level)));
    if (unitClass == 8)
        return g_CanonicalClassMageStats[lvl];
    if (unitClass == 2)
        return g_CanonicalClassPaladinStats[lvl];
    return g_CanonicalClassWarriorStats[lvl];
}

float GetClassLevelHealth(uint8 unitClass, uint8 level)
{
    return static_cast<float>(GetCanonicalClassStats(unitClass, level).health);
}

float GetClassLevelDamage(uint8 unitClass, uint8 level)
{
    return GetCanonicalClassStats(unitClass, level).meleeDamage;
}

float GetClassLevelArmor(uint8 unitClass, uint8 level)
{
    return static_cast<float>(GetCanonicalClassStats(unitClass, level).armor);
}

float GetClassLevelMana(uint8 unitClass, uint8 level)
{
    return static_cast<float>(GetCanonicalClassStats(unitClass, level).mana);
}

bool IsMapRaidActive(Map* map)
{
    if (!map)
        return false;

    if (map->IsRaid())
        return true;

    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            if (player->IsGameMaster() && !sABConfig.IsCountGameMastersEnabled())
                continue;

            Group* group = player->GetGroup();
            if (group && group->isRaidGroup())
                return true;
        }
    }
    return false;
}

uint32 GetMapBaseCapacity(Map* map)
{
    if (!map)
        return 5;

    uint32 const nativeCap = sAutoBalanceMgr.GetNativeMapCapacity(map->GetId());
    if (nativeCap > 0)
        return nativeCap;

    if (map->IsRaid())
        return 40;

    return 5;
}

uint32 GetMapMaxPlayers(Map* map)
{
    if (!map)
        return 5;

    uint32 const baseCapacity = GetMapBaseCapacity(map);

    uint32 activePlayers = 0;
    bool isRaidGroup = false;
    for (const auto& ref : map->GetPlayers())
    {
        if (Player* p = ref.getSource())
        {
            if (p->IsGameMaster() && !sABConfig.IsCountGameMastersEnabled())
                continue;
            ++activePlayers;
            Group* g = p->GetGroup();
            if (g && g->isRaidGroup())
                isRaidGroup = true;
        }
    }

    // Dynamic Raid Scaling for Dungeons
    if (!map->IsRaid())
    {
        if (sABConfig.IsRaidScalingInDungeonsEnabled() && (isRaidGroup || activePlayers > baseCapacity))
        {
            if (activePlayers > 40 && sABConfig.Is60MEnabled())
                return 60;
            if (activePlayers > 25 && sABConfig.Is40MEnabled())
                return 40;
            if (activePlayers > 20 && sABConfig.Is25MEnabled())
                return 25;
            if (activePlayers > 15 && sABConfig.Is20MEnabled())
                return 20;
            if (activePlayers > 10 && sABConfig.Is15MEnabled())
                return 15;
            if (sABConfig.Is10MEnabled())
                return 10;
            return 5;
        }
        return baseCapacity;
    }

    // Actual Raid Instance: scale to 60 if active players exceed 40
    if (activePlayers > baseCapacity && activePlayers > 40 && sABConfig.Is60MEnabled())
        return 60;

    return baseCapacity;
}

bool ShouldMapBeEnabled(Map* map)
{
    if (!map || !map->IsDungeon())
        return false;

    if (!sABConfig.IsGlobalEnabled())
        return false;

    if (sABConfig.IsDungeonDisabled(map->GetId()))
        return false;

    uint32 const maxPlayers = GetMapMaxPlayers(map);
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
        case 60:
            return sABConfig.Is60MEnabled();
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

bool IsCreatureFriendlyToAnyPlayer(Creature* creature, Map* map)
{
    if (!creature || !map)
        return false;

    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            if (player->IsGameMaster())
                continue;
            if (creature->IsFriendlyTo(player))
                return true;
        }
    }
    return false;
}

bool IsCreatureWithinPlayerDistance(Creature* creature, Map* map, uint32 distance)
{
    if (!creature || !map)
        return false;

    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            if (player->IsGameMaster())
                continue;
            if (creature->IsWithinDist(player, static_cast<float>(distance)))
                return true;
        }
    }
    return false;
}

bool IsCreatureRelevant(Creature* creature)
{
    if (!creature)
        return false;

    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return false;

    // Disabled explicitly by creature entry ID
    if (sABConfig.IsCreatureDisabled(creature->GetEntry()))
        return false;

    // Player pets and totems
    if (creature->IsPet() || creature->IsTotem())
        return false;

    if (creature->GetOwner() && creature->GetOwner()->IsPlayer())
        return false;

    CreatureInfo const* cinfo = creature->GetCreatureInfo();
    if (!cinfo)
        return false;

    // Flavor critter check
    if (cinfo->type == CREATURE_TYPE_CRITTER && creature->GetLevel() <= 5 && creature->GetMaxHealth() < 100)
        return false;

    // Temporary summon unwrapping & ownership checks
    if (creature->IsTemporarySummon())
    {
        TemporarySummon* tempSummon = static_cast<TemporarySummon*>(creature);
        Unit* summoner = tempSummon->GetSummoner();
        if (summoner)
        {
            if (summoner->IsPlayer())
            {
                // Player summoned: only scale if hostile to valid party members
                bool hostileToPlayers = false;
                for (const auto& ref : map->GetPlayers())
                {
                    if (Player* player = ref.getSource())
                    {
                        if (!player->IsGameMaster() && player->IsAlive() && creature->IsHostileTo(player))
                        {
                            hostileToPlayers = true;
                            break;
                        }
                    }
                }
                if (!hostileToPlayers)
                    return false;
            }
            else if (summoner->IsCreature())
            {
                Creature* summonerCreature = static_cast<Creature*>(summoner);
                if (IsCreatureFriendlyToAnyPlayer(summonerCreature, map) && !IsBoss(summonerCreature))
                    return false;
            }
        }
    }

    // Multi-wing instance distance check (e.g. Scarlet Monastery Map 189)
    if (sABConfig.HasDistanceCheckOverride(map->GetId()))
    {
        uint32 maxDist = sABConfig.GetDistanceCheckOverride(map->GetId());
        if (!IsCreatureWithinPlayerDistance(creature, map, maxDist))
            return false;
    }

    // Friendly non-boss NPCs (vendors, trainers, questgivers)
    if (!IsBoss(creature))
    {
        if (cinfo->npc_flags & (UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_TRAINER | UNIT_NPC_FLAG_QUESTGIVER | UNIT_NPC_FLAG_REPAIR))
            return false;

        if (IsCreatureFriendlyToAnyPlayer(creature, map))
            return false;
    }

    return true;
}

AutoBalanceInflectionPointSettings GetInflectionPointSettings(Map* map, bool isBoss)
{
    uint32 const mapId = map ? map->GetId() : 0;
    AutoBalanceInflectionPointSettings const* overrideSettings = sABConfig.GetMapInflectionOverride(mapId, isBoss);
    if (overrideSettings)
        return *overrideSettings;

    uint32 const maxPlayers = GetMapMaxPlayers(map);
    bool const isRaid = (map && (map->IsRaid() || maxPlayers > 5 || IsMapRaidActive(map)));
    AutoBalanceInflectionPointSettings settings = isRaid
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

    uint32 const mapId = map ? map->GetId() : 0;
    AutoBalanceStatModifiers const* mOverride = sABConfig.GetMapStatModifierOverride(mapId, isBoss);
    if (mOverride)
        return *mOverride;

    uint32 const maxPlayers = GetMapMaxPlayers(map);
    bool const isRaid = (map && (map->IsRaid() || maxPlayers > 5 || IsMapRaidActive(map)));
    if (isRaid)
        return sABConfig.GetRaidStatModifiers(maxPlayers, isBoss);

    return isBoss ? sABConfig.GetBossStatModifiers() : sABConfig.GetNormalStatModifiers();
}

float GetDefaultMultiplier(uint32 maxPlayers, float adjustedPlayerCount, AutoBalanceInflectionPointSettings const& settings)
{
    if (maxPlayers == 0)
        maxPlayers = 5;

    float const maxP = static_cast<float>(maxPlayers);
    float const inflectionValue = maxP * settings.value;
    float diff = (maxP / 5.0f) * 1.5f;
    if (diff <= 0.0f)
        diff = 1.0f;

    float denom = ((std::tanh((maxP - inflectionValue) / diff) + 1.0f) / 2.0f) *
                  (settings.curveCeiling - settings.curveFloor) + settings.curveFloor;
    if (denom <= 0.0001f)
        denom = 0.0001f;

    float const curveCeilingAdjustment = settings.curveCeiling / denom;

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
    // Hard clamp player level to Vanilla maximum level 60
    int const playerLevel = std::min(60, static_cast<int>(mapInfo.highestPlayerLevel));

    float const dungeonLevel = (mapInfo.avgCreatureLevel > 0.0f) ? mapInfo.avgCreatureLevel :
        ((mapInfo.highestCreatureLevel > 0 && mapInfo.lowestCreatureLevel > 0)
            ? static_cast<float>(mapInfo.highestCreatureLevel + mapInfo.lowestCreatureLevel) / 2.0f
            : static_cast<float>(unmodifiedLevel));

    int const skipHigher = static_cast<int>(mapInfo.levelScalingSkipHigherLevels);
    int const skipLower  = static_cast<int>(mapInfo.levelScalingSkipLowerLevels);

    if (skipHigher > 0 && dungeonLevel <= static_cast<float>(playerLevel + skipHigher) &&
        skipLower > 0 && dungeonLevel >= static_cast<float>(playerLevel - skipLower))
    {
        return unmodifiedLevel;
    }

    if (sABConfig.GetLevelScalingMethod() == AUTOBALANCE_SCALING_FIXED)
        return static_cast<uint8>(playerLevel);

    // Dynamic level scaling
    if (mapInfo.highestCreatureLevel == 0)
        return unmodifiedLevel;

    int const levelOffset = baseLevel - static_cast<int>(mapInfo.highestCreatureLevel);
    int targetLevel = playerLevel + static_cast<int>(mapInfo.levelScalingDynamicCeiling) + levelOffset;

    int const minLevel = playerLevel - static_cast<int>(mapInfo.levelScalingDynamicFloor);
    int const maxLevel = playerLevel + static_cast<int>(mapInfo.levelScalingDynamicCeiling);

    targetLevel = std::max(minLevel, std::min(maxLevel, targetLevel));

    // Player target-level rules capped at 60; bosses with native level > 60 retain boss ceiling up to 63
    int const ceiling = (unmodifiedLevel > 60) ? 63 : 60;
    return static_cast<uint8>(std::max(1, std::min(ceiling, targetLevel)));
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
    cInfo.baseData.baseMinDamage = creature->GetWeaponDamageRange(BASE_ATTACK, MINDAMAGE);
    cInfo.baseData.baseMaxDamage = creature->GetWeaponDamageRange(BASE_ATTACK, MAXDAMAGE);
    if (cInfo.baseData.baseMinDamage <= 0.0f && cinfo->dmg_min > 0.0f)
        cInfo.baseData.baseMinDamage = cinfo->dmg_min;
    if (cInfo.baseData.baseMaxDamage <= 0.0f && cinfo->dmg_max > 0.0f)
        cInfo.baseData.baseMaxDamage = cinfo->dmg_max;

    cInfo.baseData.baseRangedMinDamage = creature->GetWeaponDamageRange(RANGED_ATTACK, MINDAMAGE);
    cInfo.baseData.baseRangedMaxDamage = creature->GetWeaponDamageRange(RANGED_ATTACK, MAXDAMAGE);
    if (cInfo.baseData.baseRangedMinDamage <= 0.0f && cinfo->ranged_dmg_min > 0.0f)
        cInfo.baseData.baseRangedMinDamage = cinfo->ranged_dmg_min;
    if (cInfo.baseData.baseRangedMaxDamage <= 0.0f && cinfo->ranged_dmg_max > 0.0f)
        cInfo.baseData.baseRangedMaxDamage = cinfo->ranged_dmg_max;

    cInfo.baseData.baseAttackPower = cinfo->attack_power;

    // Capture each spawn's native rolled level and base stats.
    // Do not reconstruct an assumed historical level from template bounds.
    cInfo.baseData.baseLevel = creature->GetLevel();
    cInfo.baseData.baseGoldMin = cinfo->gold_min;
    cInfo.baseData.baseGoldMax = cinfo->gold_max;
    cInfo.baseData.initialized = true;

    cInfo.unmodifiedLevel = cInfo.baseData.baseLevel;
    cInfo.selectedLevel = cInfo.baseData.baseLevel;
    cInfo.isBoss = IsBoss(creature);
    cInfo.entry = creature->GetEntry();
}

void CalculateMultipliers(Map* map, Creature* creature, AutoBalanceMapInfo const& mapInfo, AutoBalanceCreatureInfo& cInfo)
{
    if (!creature)
        return;

    InitializeCreatureBaseData(creature, cInfo);

    uint32 const maxPlayers = GetMapMaxPlayers(map);
    int const forcedCount = sABConfig.GetForcedPlayerCount(creature->GetEntry());
    float effectivePlayers = (forcedCount > 0) ? static_cast<float>(forcedCount) : static_cast<float>(mapInfo.adjustedPlayerCount);
    if (effectivePlayers < 1.0f)
        effectivePlayers = 1.0f;

    AutoBalanceInflectionPointSettings const inflection = GetInflectionPointSettings(map, cInfo.isBoss);
    AutoBalanceStatModifiers const statMod = GetStatModifiers(map, creature, cInfo.isBoss);

    float const defaultMultiplier = GetDefaultMultiplier(maxPlayers, effectivePlayers, inflection);

    cInfo.damageMultiplier = std::max(sABConfig.GetMinDamageModifier(), defaultMultiplier * statMod.global * statMod.damage);
    cInfo.healthMultiplier = std::max(sABConfig.GetMinHPModifier(), defaultMultiplier * statMod.global * statMod.health);
    cInfo.manaMultiplier   = std::max(sABConfig.GetMinManaModifier(), defaultMultiplier * statMod.global * statMod.mana);
    cInfo.armorMultiplier  = std::max(0.0f, defaultMultiplier * statMod.global * statMod.armor);
    cInfo.ccDurationMultiplier = std::min(sABConfig.GetMaxCCDurationModifier(),
                                 std::max(sABConfig.GetMinCCDurationModifier(), defaultMultiplier * statMod.ccduration));

    // Level scaling calculations using authentic Vanilla creature class-level stats
    if (mapInfo.isLevelScalingEnabled && !cInfo.neverLevelScale)
    {
        cInfo.selectedLevel = CalculateScaledLevel(creature, mapInfo, cInfo.unmodifiedLevel);
        if (cInfo.unmodifiedLevel > 0 && cInfo.selectedLevel != cInfo.unmodifiedLevel)
        {
            uint8 const origLvl = cInfo.unmodifiedLevel;
            uint8 const newLvl = cInfo.selectedLevel;
            CreatureInfo const* cinfo = creature->GetCreatureInfo();
            uint8 const unitClass = cinfo ? cinfo->unit_class : 1;

            float const origHp = GetClassLevelHealth(unitClass, origLvl);
            float const newHp  = GetClassLevelHealth(unitClass, newLvl);
            float const hpRatio = (origHp > 0.0f) ? (newHp / origHp) : 1.0f;

            float const origDmg = GetClassLevelDamage(unitClass, origLvl);
            float const newDmg  = GetClassLevelDamage(unitClass, newLvl);
            float const dmgRatio = (origDmg > 0.0f) ? (newDmg / origDmg) : 1.0f;

            float const origArmor = GetClassLevelArmor(unitClass, origLvl);
            float const newArmor  = GetClassLevelArmor(unitClass, newLvl);
            float const armorRatio = (origArmor > 0.0f) ? (newArmor / origArmor) : 1.0f;

            float const origMana = GetClassLevelMana(unitClass, origLvl);
            float const newMana  = GetClassLevelMana(unitClass, newLvl);
            float const manaRatio = (origMana > 0.0f) ? (newMana / origMana) : 1.0f;

            cInfo.scaledHealthMultiplier = std::max(sABConfig.GetMinHPModifier(), cInfo.healthMultiplier * hpRatio);
            cInfo.scaledDamageMultiplier = std::max(sABConfig.GetMinDamageModifier(), cInfo.damageMultiplier * dmgRatio);
            cInfo.scaledArmorMultiplier  = std::max(0.0f, cInfo.armorMultiplier * armorRatio);
            cInfo.scaledManaMultiplier   = (cInfo.baseData.baseMana > 0) ? (cInfo.manaMultiplier * manaRatio) : 0.0f;

            if (newLvl > origLvl)
            {
                if (cInfo.scaledHealthMultiplier < cInfo.healthMultiplier)
                    cInfo.scaledHealthMultiplier = cInfo.healthMultiplier;
                if (cInfo.scaledDamageMultiplier < cInfo.damageMultiplier)
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
    }
    else
    {
        cInfo.selectedLevel = cInfo.unmodifiedLevel;
        cInfo.scaledHealthMultiplier = cInfo.healthMultiplier;
        cInfo.scaledManaMultiplier   = cInfo.manaMultiplier;
        cInfo.scaledArmorMultiplier  = cInfo.armorMultiplier;
        cInfo.scaledDamageMultiplier = cInfo.damageMultiplier;
    }

    float const avgHealthDamageMultipliers = (cInfo.scaledHealthMultiplier + cInfo.scaledDamageMultiplier) / 2.0f;
    if (sABConfig.IsRewardScalingXPEnabled())
    {
        if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_DYNAMIC)
            cInfo.xpModifier = avgHealthDamageMultipliers * sABConfig.GetRewardScalingXPModifier();
        else
            cInfo.xpModifier = sABConfig.GetRewardScalingXPModifier();
    }
    else
        cInfo.xpModifier = 1.0f;

    if (sABConfig.IsRewardScalingMoneyEnabled())
    {
        if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_DYNAMIC)
            cInfo.moneyModifier = avgHealthDamageMultipliers * sABConfig.GetRewardScalingMoneyModifier();
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

    bool const wasAlive = creature->IsAlive();
    float const hpPercent = creature->GetHealthPercent() / 100.0f;
    float const manaPercent = creature->GetPowerPercent(POWER_MANA) / 100.0f;

    if (cInfo.selectedLevel != creature->GetLevel())
        creature->SetLevel(cInfo.selectedLevel);

    uint32 const newMaxHealth = std::max(1u, static_cast<uint32>(std::round(static_cast<float>(cInfo.baseData.baseHealth) * cInfo.scaledHealthMultiplier)));
    uint32 const newMaxMana   = static_cast<uint32>(std::round(static_cast<float>(cInfo.baseData.baseMana) * cInfo.scaledManaMultiplier));
    uint32 const newArmor     = static_cast<uint32>(std::round(static_cast<float>(cInfo.baseData.baseArmor) * cInfo.scaledArmorMultiplier));

    float const newMinDamage  = cInfo.baseData.baseMinDamage * cInfo.scaledDamageMultiplier;
    float const newMaxDamage  = cInfo.baseData.baseMaxDamage * cInfo.scaledDamageMultiplier;
    float const newRangedMinDamage = cInfo.baseData.baseRangedMinDamage * cInfo.scaledDamageMultiplier;
    float const newRangedMaxDamage = cInfo.baseData.baseRangedMaxDamage * cInfo.scaledDamageMultiplier;

    creature->SetMaxHealth(newMaxHealth);
    creature->SetHealth(wasAlive ? std::max(1u, static_cast<uint32>(std::round(static_cast<float>(newMaxHealth) * hpPercent))) : 0u);

    if (cInfo.baseData.baseMana > 0)
    {
        creature->SetMaxPower(POWER_MANA, newMaxMana);
        creature->SetPower(POWER_MANA, static_cast<uint32>(std::round(static_cast<float>(newMaxMana) * manaPercent)));
        creature->SetModifierValue(UNIT_MOD_MANA, BASE_VALUE, static_cast<float>(newMaxMana));
    }

    creature->SetModifierValue(UNIT_MOD_ARMOR, BASE_VALUE, static_cast<float>(newArmor));
    creature->UpdateArmor();

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
    creature->UpdateArmor();

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
