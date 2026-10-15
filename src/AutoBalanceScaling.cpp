/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#include "AutoBalanceScaling.h"
#include "AutoBalanceClassStatsData.h"
#include "Database/DatabaseEnv.h"
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

static CanonicalClassLevelStats s_ClassLevelStats[10][64];
static bool s_ClassLevelStatsLoaded = false;

static uint8 NormalizeUnitClass(uint8 unitClass)
{
    if (unitClass == 8)
        return 8; // Mage
    if (unitClass == 2)
        return 2; // Paladin
    return 1; // Warrior (default for 1, 4, etc.)
}

void LoadCreatureClassLevelStats()
{
    // 1. Initialize fallback defaults from canonical data
    for (uint8 lvl = 1; lvl <= 63; ++lvl)
    {
        s_ClassLevelStats[1][lvl] = g_CanonicalClassWarriorStats[lvl];
        s_ClassLevelStats[2][lvl] = g_CanonicalClassPaladinStats[lvl];
        s_ClassLevelStats[8][lvl] = g_CanonicalClassMageStats[lvl];
    }
    s_ClassLevelStatsLoaded = true;

    // 2. Safely check if creature_classlevelstats exists before querying
    std::unique_ptr<QueryResult> tableCheck(WorldDatabase.Query("SHOW TABLES LIKE 'creature_classlevelstats'"));
    if (tableCheck)
    {
        QueryResult* result = WorldDatabase.Query("SELECT class, level, melee_damage, health, mana, armor FROM creature_classlevelstats WHERE class IN (1, 2, 8) AND level <= 63");
        if (result)
        {
            uint32 count = 0;
            do
            {
                Field* fields = result->Fetch();
                uint8 const cls = fields[0].GetUInt8();
                uint8 const lvl = fields[1].GetUInt8();
                if ((cls == 1 || cls == 2 || cls == 8) && lvl >= 1 && lvl <= 63)
                {
                    s_ClassLevelStats[cls][lvl].meleeDamage = fields[2].GetFloat();
                    s_ClassLevelStats[cls][lvl].health      = fields[3].GetUInt32();
                    s_ClassLevelStats[cls][lvl].mana        = fields[4].GetUInt32();
                    s_ClassLevelStats[cls][lvl].armor       = fields[5].GetUInt32();
                    ++count;
                }
            } while (result->NextRow());

            delete result;
            sLog.outString("[AutoBalance] Loaded %u creature class-level stats from database table creature_classlevelstats.", count);
        }
    }
    else
    {
        sLog.outString("[AutoBalance] Database table creature_classlevelstats not available; using built-in canonical Vanilla 1.12.1 stats curve.");
    }
}

static void EnsureClassLevelStatsLoaded()
{
    if (!s_ClassLevelStatsLoaded)
        LoadCreatureClassLevelStats();
}

float GetClassLevelHealth(uint8 unitClass, uint8 level)
{
    EnsureClassLevelStatsLoaded();
    uint8 const cls = NormalizeUnitClass(unitClass);
    uint8 const lvl = static_cast<uint8>(std::max<uint8>(1, std::min<uint8>(60, level)));
    return static_cast<float>(s_ClassLevelStats[cls][lvl].health);
}

float GetClassLevelDamage(uint8 unitClass, uint8 level)
{
    EnsureClassLevelStatsLoaded();
    uint8 const cls = NormalizeUnitClass(unitClass);
    uint8 const lvl = static_cast<uint8>(std::max<uint8>(1, std::min<uint8>(60, level)));
    return s_ClassLevelStats[cls][lvl].meleeDamage;
}

float GetClassLevelArmor(uint8 unitClass, uint8 level)
{
    EnsureClassLevelStatsLoaded();
    uint8 const cls = NormalizeUnitClass(unitClass);
    uint8 const lvl = static_cast<uint8>(std::max<uint8>(1, std::min<uint8>(60, level)));
    return static_cast<float>(s_ClassLevelStats[cls][lvl].armor);
}

float GetClassLevelMana(uint8 unitClass, uint8 level)
{
    EnsureClassLevelStatsLoaded();
    uint8 const cls = NormalizeUnitClass(unitClass);
    uint8 const lvl = static_cast<uint8>(std::max<uint8>(1, std::min<uint8>(60, level)));
    return static_cast<float>(s_ClassLevelStats[cls][lvl].mana);
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

    uint32 const mapId = map->GetId();

    // Canonical Vanilla and Turtle WoW instance capacities:
    // vMaNGOS map_template.player_limit contains legacy Patch 1.0-1.2 values (10 for most
    // 5-man dungeons). This canonical table overrides legacy values to guarantee 1:1
    // parity with authentic Vanilla and Turtle WoW (turtlecraft.gg) standards.
    switch (mapId)
    {
        // 40-Player Raids
        case 44:  // Old Scarlet Citadel (TWoW 40-man raid)
        case 45:  // Scarlet Citadel (TWoW 40-man raid)
        case 249: // Onyxia's Lair
        case 409: // Molten Core
        case 469: // Blackwing Lair
        case 531: // Temple of Ahn'Qiraj (AQ40)
        case 533: // Naxxramas
        case 807: // Emerald Sanctum (TWoW 40-man raid)
        case 814: // Tower of Karazhan / Karazhan Upper (TWoW 40-man raid)
            return 40;

        // 20-Player Raids
        case 309: // Zul'Gurub
        case 509: // Ruins of Ahn'Qiraj (AQ20)
            return 20;

        // 10-Player Instances & Raids
        case 229: // Blackrock Spire (Vanilla 10-man instance / UBRS)
        case 532: // Lower Karazhan Halls (TWoW 10-man raid)
            return 10;

        // 5-Player Dungeons (All Classic & Turtle WoW leveling dungeons)
        case 33:  // Shadowfang Keep
        case 34:  // Stormwind Stockade
        case 35:  // Stormwind Vault (TWoW 5-man dungeon)
        case 36:  // Deadmines
        case 43:  // Wailing Caverns
        case 47:  // Razorfen Kraul
        case 48:  // Blackfathom Deeps
        case 70:  // Uldaman
        case 90:  // Gnomeregan
        case 109: // Sunken Temple
        case 129: // Razorfen Downs
        case 189: // Scarlet Monastery
        case 209: // Zul'Farrak
        case 230: // Blackrock Depths
        case 269: // Caverns of Time: Black Morass (TWoW 5-man dungeon)
        case 289: // Scholomance
        case 329: // Stratholme
        case 349: // Maraudon
        case 389: // Ragefire Chasm
        case 429: // Dire Maul
        case 800: // Karazhan Crypt (TWoW 5-man dungeon)
        case 802: // The Crescent Grove (TWoW 5-man dungeon)
        case 806: // Frostmane Retreat (TWoW 5-man dungeon)
        case 808: // Hateforge Quarry (TWoW 5-man dungeon)
        case 815: // Gilneas City (TWoW 5-man dungeon)
            return 5;

        default:
            break;
    }

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
    uint32 mapId = map ? map->GetId() : 0;
    AutoBalanceInflectionPointSettings const* overrideSettings = sABConfig.GetMapInflectionOverride(mapId, isBoss);
    if (overrideSettings)
        return *overrideSettings;

    uint32 maxPlayers = GetMapMaxPlayers(map);
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

    uint32 mapId = map ? map->GetId() : 0;
    AutoBalanceStatModifiers const* mOverride = sABConfig.GetMapStatModifierOverride(mapId, isBoss);
    if (mOverride)
        return *mOverride;

    uint32 maxPlayers = GetMapMaxPlayers(map);
    bool const isRaid = (map && (map->IsRaid() || maxPlayers > 5 || IsMapRaidActive(map)));
    if (isRaid)
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
    // Hard clamp player level to Vanilla maximum level 60
    int const playerLevel = std::min(60, static_cast<int>(mapInfo.highestPlayerLevel));

    // Upstream parity: evaluate skip range against the dungeon's average creature level.
    // If the dungeon's overall difficulty is already within the skip range of the group,
    // preserve the authentic dungeon level spread for all creatures.
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
    // Clamped strictly to Vanilla maximum level 60
    return static_cast<uint8>(std::max(1, std::min(60, targetLevel)));
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

    // Determine authentic base level:
    // If the creature's current level is outside template bounds [minlevel, maxlevel],
    // it was previously modified or scaled. Never inherit a post-scaled level!
    uint8 baseLvl = creature->GetLevel();
    if (cinfo->level_min > 0 && cinfo->level_max > 0)
    {
        if (baseLvl < cinfo->level_min || baseLvl > cinfo->level_max)
            baseLvl = static_cast<uint8>(cinfo->level_max);
    }
    cInfo.baseData.baseLevel = baseLvl;
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

    // If temporary summon of another creature, inherit unmodified level unless not a clone
    if (creature->IsTemporarySummon())
    {
        TemporarySummon* tempSummon = static_cast<TemporarySummon*>(creature);
        Unit* summoner = tempSummon->GetSummoner();
        if (summoner && summoner->IsCreature() && !sABConfig.IsNotAClone(creature->GetEntry()))
        {
            cInfo.unmodifiedLevel = summoner->GetLevel();
        }
    }

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

            // When scaling UP in level, guarantee that scaled multipliers never degrade below
            // the creature's unscaled baseline player-multiplier (prevents Level 60 mobs from
            // ending up with lower stats than lower-level creatures due to compounding formulas).
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

    // Reward scaling calculations matching upstream mod-autobalance-wotlk:
    // Uses average of scaled health & damage multipliers so rewards account for both
    // group player count and level scaling difficulty!
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
