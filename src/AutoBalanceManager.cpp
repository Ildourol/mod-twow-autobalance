/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#include "AutoBalanceManager.h"
#include "AutoBalanceConfig.h"
#include "AutoBalanceScaling.h"
#include "Maps/Map.h"
#include "Objects/Creature.h"
#include "Objects/Player.h"
#include "Chat/Chat.h"
#include "Database/SQLStorages.h"
#include "Log.h"
#include "StringFormat.h"
#include "Message.h"
#include <algorithm>
#include <vector>

namespace
{
uint8 ClampConfigByte(int value)
{
    return static_cast<uint8>(std::max(0, std::min(255, value)));
}
}

AutoBalanceManager& AutoBalanceManager::Instance()
{
    static AutoBalanceManager instance;
    return instance;
}

uint32 AutoBalanceManager::GetNativeMapCapacity(uint32 mapId) const
{
    auto it = m_nativeMapCapacities.find(mapId);
    if (it != m_nativeMapCapacities.end())
        return it->second;
    return 0;
}

uint64 AutoBalanceManager::GetMapKey(Map* map) const
{
    if (!map)
        return 0;
    return MakeAutoBalanceInstanceKey(map->GetId(), map->GetInstanceId());
}

void AutoBalanceManager::RefreshMapSettings(Map* map, AutoBalanceMapInfo& mapInfo)
{
    if (!map)
        return;

    mapInfo.enabled = AutoBalanceScaling::ShouldMapBeEnabled(map);
    mapInfo.globalConfigTime = sABConfig.GetConfigTimestamp();
    mapInfo.mapConfigTime = sABConfig.GetConfigTimestamp();
    mapInfo.minPlayers = sABConfig.GetMinPlayersForMap(map->GetId(), map->IsRaid());
    if (map->IsRaid())
        mapInfo.minPlayers = std::max<uint8>(mapInfo.minPlayers, sABConfig.GetRaidPlayerLimitMin());
    else
        mapInfo.minPlayers = std::max<uint8>(mapInfo.minPlayers, sABConfig.GetDungeonPlayerLimitMin());
    mapInfo.isLevelScalingEnabled = sABConfig.IsLevelScalingEnabled();

    if (AutoBalanceDynamicLevelSettings const* settings = sABConfig.GetDynamicLevelOverride(map->GetId()))
    {
        mapInfo.levelScalingSkipHigherLevels = ClampConfigByte(settings->skipHigher);
        mapInfo.levelScalingSkipLowerLevels = ClampConfigByte(settings->skipLower);
        mapInfo.levelScalingDynamicCeiling = ClampConfigByte(settings->ceiling);
        mapInfo.levelScalingDynamicFloor = ClampConfigByte(settings->floor);
    }
    else
    {
        mapInfo.levelScalingSkipHigherLevels = sABConfig.GetLevelScalingSkipHigherLevels();
        mapInfo.levelScalingSkipLowerLevels = sABConfig.GetLevelScalingSkipLowerLevels();
        mapInfo.levelScalingDynamicCeiling = sABConfig.GetDynamicLevelCeiling(map->IsRaid());
        mapInfo.levelScalingDynamicFloor = sABConfig.GetDynamicLevelFloor(map->IsRaid());
    }

    mapInfo.initialized = true;
}

void AutoBalanceManager::ApplyMemoryMapLimits()
{
    uint32 count = 0;
    for (auto itr = sMapStorage.begin<MapEntry>(); itr < sMapStorage.end<MapEntry>(); ++itr)
    {
        MapEntry* entry = const_cast<MapEntry*>(*itr);
        if (!entry || !entry->IsDungeon())
            continue;

        uint32 const mapId = entry->id;
        uint32 nativeCap = 5;
        auto itNative = m_nativeMapCapacities.find(mapId);
        if (itNative != m_nativeMapCapacities.end())
            nativeCap = itNative->second;

        if (sABConfig.IsGlobalEnabled() && !sABConfig.IsDungeonDisabled(mapId))
        {
            if (entry->IsRaid())
                entry->maxPlayers = sABConfig.GetRaidPlayerLimitMax();
            else
                entry->maxPlayers = sABConfig.GetDungeonPlayerLimitMax();
            ++count;
        }
        else
        {
            // When globally disabled or map explicitly excluded, release module-owned entrance override
            entry->maxPlayers = nativeCap;
        }
    }

    if (sABConfig.IsGlobalEnabled())
    {
        sLog.outString("[AutoBalance] Applied in-memory instance entrance limits to %u dungeons/raids (Dungeons Max: %u, Raids Max: %u).",
            count, sABConfig.GetDungeonPlayerLimitMax(), sABConfig.GetRaidPlayerLimitMax());
    }
}

void AutoBalanceManager::Initialize()
{
    // Capture boot's native map capacities once before applying entrance overrides
    if (!m_nativeCapacitiesCaptured)
    {
        for (auto itr = sMapStorage.begin<MapEntry>(); itr < sMapStorage.end<MapEntry>(); ++itr)
        {
            MapEntry* entry = const_cast<MapEntry*>(*itr);
            if (entry && entry->IsDungeon())
                m_nativeMapCapacities[entry->id] = entry->maxPlayers;
        }
        m_nativeCapacitiesCaptured = true;
    }

    sABConfig.Load();
    ApplyMemoryMapLimits();
}

void AutoBalanceManager::Reload()
{
    sABConfig.Reload();
    ApplyMemoryMapLimits();
    ScheduleAllMapsRefresh();
}

void AutoBalanceManager::ScheduleAllMapsRefresh()
{
    std::shared_lock<std::shared_mutex> guard(m_lock);
    for (auto const& pair : m_maps)
    {
        if (pair.second)
        {
            std::lock_guard<std::recursive_mutex> mapGuard(pair.second->stateLock);
            pair.second->configPending = true;
        }
    }
}

AutoBalanceManager::MapInfoPtr AutoBalanceManager::GetMapInfo(Map* map)
{
    if (!map)
        return MapInfoPtr();
    return GetMapInfo(GetMapKey(map));
}

AutoBalanceManager::MapInfoPtr AutoBalanceManager::GetMapInfo(uint64 instanceKey)
{
    std::shared_lock<std::shared_mutex> guard(m_lock);
    auto it = m_maps.find(instanceKey);
    return it != m_maps.end() ? it->second : MapInfoPtr();
}

AutoBalanceManager::MapInfoPtr AutoBalanceManager::GetOrCreateMapInfo(Map* map)
{
    if (!map)
        return MapInfoPtr();

    uint64 const instanceKey = GetMapKey(map);
    if (MapInfoPtr existing = GetMapInfo(instanceKey))
        return existing;

    MapInfoPtr created = std::make_shared<AutoBalanceMapInfo>();
    created->instanceKey = instanceKey;
    RefreshMapSettings(map, *created);

    std::unique_lock<std::shared_mutex> guard(m_lock);
    auto result = m_maps.emplace(instanceKey, created);
    return result.first->second;
}

AutoBalanceManager::CreatureInfoPtr AutoBalanceManager::GetCreatureInfo(Creature* creature)
{
    if (!creature)
        return CreatureInfoPtr();
    return GetCreatureInfo(GetMapKey(creature->GetMap()), creature->GetObjectGuid());
}

AutoBalanceManager::CreatureInfoPtr AutoBalanceManager::GetCreatureInfo(uint64 instanceKey, ObjectGuid guid)
{
    AutoBalanceCreatureKey const key{ instanceKey, guid };
    std::shared_lock<std::shared_mutex> guard(m_lock);
    auto it = m_creatures.find(key);
    return it != m_creatures.end() ? it->second : CreatureInfoPtr();
}

AutoBalanceManager::CreatureInfoPtr AutoBalanceManager::GetOrCreateCreatureInfo(Creature* creature)
{
    if (!creature)
        return CreatureInfoPtr();

    uint64 const instanceKey = GetMapKey(creature->GetMap());
    ObjectGuid const guid = creature->GetObjectGuid();
    AutoBalanceCreatureKey const key{ instanceKey, guid };

    {
        std::shared_lock<std::shared_mutex> guard(m_lock);
        auto it = m_creatures.find(key);
        if (it != m_creatures.end())
        {
            CreatureInfoPtr existing = it->second;
            // Check captured creature identity: if entry doesn't match, GUID was reused for a different creature
            if (existing && existing->entry == creature->GetEntry())
                return existing;
        }
    }

    CreatureInfoPtr created = std::make_shared<AutoBalanceCreatureInfo>();
    created->instanceKey = instanceKey;
    created->entry = creature->GetEntry();

    std::unique_lock<std::shared_mutex> guard(m_lock);
    m_creatures[key] = created;
    return created;
}

bool AutoBalanceManager::IsMapScaled(Map* map)
{
    if (!map || !map->IsDungeon() || !sABConfig.IsGlobalEnabled())
        return false;

    if (MapInfoPtr info = GetMapInfo(map))
    {
        std::lock_guard<std::recursive_mutex> guard(info->stateLock);
        return info->enabled;
    }

    return AutoBalanceScaling::ShouldMapBeEnabled(map);
}

bool AutoBalanceManager::IsCreatureScaled(Creature* creature)
{
    if (!creature || !sABConfig.IsGlobalEnabled())
        return false;

    if (CreatureInfoPtr cInfo = GetCreatureInfo(creature))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        return cInfo->isActive;
    }

    return false;
}

void AutoBalanceManager::UpdateMapPlayerCounts(Map* map, AutoBalanceMapInfo& mapInfo, Player const* excludedPlayer)
{
    if (!map)
        return;

    std::lock_guard<std::recursive_mutex> guard(mapInfo.stateLock);

    float effective = 0.0f;
    uint8 count = 0;
    uint8 lowestLvl = 255;
    uint8 highestLvl = 0;
    mapInfo.players.clear();

    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            if (player == excludedPlayer)
                continue;

            if (player->IsGameMaster() && !sABConfig.IsCountGameMastersEnabled())
                continue;

            ++count;
            mapInfo.players.insert(player->GetObjectGuid());
            uint8 const level = std::min<uint8>(60, player->GetLevel());
            if (level < lowestLvl)
                lowestLvl = level;
            if (level > highestLvl)
                highestLvl = level;

            bool const isBot = (player->GetSession() && player->GetSession()->IsHeadless());
            if (isBot)
            {
                if (sABConfig.IsCountBotsEnabled())
                    effective += sABConfig.GetBotWeight();
            }
            else
            {
                effective += 1.0f;
            }
        }
    }

    if (highestLvl == 0)
    {
        for (const auto& ref : map->GetPlayers())
        {
            if (Player* gm = ref.getSource())
            {
                if (gm != excludedPlayer)
                {
                    uint8 const lvl = std::min<uint8>(60, gm->GetLevel());
                    if (lvl < lowestLvl)
                        lowestLvl = lvl;
                    if (lvl > highestLvl)
                        highestLvl = lvl;
                }
            }
        }
    }

    mapInfo.playerCount = count;
    mapInfo.lowestPlayerLevel = (lowestLvl == 255) ? 0 : lowestLvl;
    mapInfo.highestPlayerLevel = highestLvl;

    if (mapInfo.combatLocked && mapInfo.combatLockMinPlayers > 0)
        effective = std::max(effective, static_cast<float>(mapInfo.combatLockMinPlayers));
    effective = std::max(effective, static_cast<float>(mapInfo.minPlayers));

    float adjusted = effective + static_cast<float>(sABConfig.GetPlayerCountDifficultyOffset());
    uint32 const maxPlayers = std::max<uint32>(1, AutoBalanceScaling::GetMapMaxPlayers(map));
    adjusted = std::max(1.0f, std::min(static_cast<float>(maxPlayers), adjusted));
    mapInfo.adjustedPlayerCount = static_cast<uint8>(std::round(adjusted));

    if (sABConfig.IsDebugLoggingEnabled())
    {
        sLog.outString("[AutoBalance] Map %u (%s): %u active player(s) (levels %u-%u), effective %.2f -> difficulty %u player(s).",
            map->GetId(), map->GetMapName(), mapInfo.playerCount, mapInfo.lowestPlayerLevel,
            mapInfo.highestPlayerLevel, effective, static_cast<uint32>(mapInfo.adjustedPlayerCount));
    }

    AutoBalanceInflectionPointSettings const inflection = AutoBalanceScaling::GetInflectionPointSettings(map, false);
    AutoBalanceStatModifiers const statMod = AutoBalanceScaling::GetStatModifiers(map, nullptr, false);
    float const defaultMult = AutoBalanceScaling::GetDefaultMultiplier(maxPlayers, adjusted, inflection);

    mapInfo.worldDamageMultiplier = std::max(sABConfig.GetMinDamageModifier(), defaultMult * statMod.global * statMod.damage);
    mapInfo.worldHealthMultiplier = std::max(sABConfig.GetMinHPModifier(), defaultMult * statMod.global * statMod.health);

    if (mapInfo.isLevelScalingEnabled && mapInfo.highestPlayerLevel > 0 && mapInfo.avgCreatureLevel > 0.0f)
    {
        uint8 const origLvl = static_cast<uint8>(std::round(mapInfo.avgCreatureLevel));
        uint8 const newLvl = std::min<uint8>(60, mapInfo.highestPlayerLevel);
        if (origLvl > 0 && newLvl != origLvl)
        {
            float const origDmg = AutoBalanceScaling::GetClassLevelDamage(1, origLvl);
            float const newDmg = AutoBalanceScaling::GetClassLevelDamage(1, newLvl);
            float const dmgRatio = (origDmg > 0.0f) ? (newDmg / origDmg) : 1.0f;
            mapInfo.scaledWorldDamageMultiplier = std::max(sABConfig.GetMinDamageModifier(), mapInfo.worldDamageMultiplier * dmgRatio);
        }
        else
        {
            mapInfo.scaledWorldDamageMultiplier = mapInfo.worldDamageMultiplier;
        }
    }
    else
    {
        mapInfo.scaledWorldDamageMultiplier = mapInfo.worldDamageMultiplier;
    }
}

void AutoBalanceManager::SendNotification(Map* map, std::string const& message)
{
    if (!map || message.empty())
        return;

    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            if (player->GetSession() && player->GetSession()->IsHeadless())
                continue;

            ChatHandler(player).SendSysMessage(message.c_str());
        }
    }
}

void AutoBalanceManager::RescaleMapCreatures(Map* map)
{
    if (!map || !map->IsDungeon())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    std::vector<ObjectGuid> allGuids;
    {
        std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
        if (!mapInfo->enabled)
            return;

        allGuids.assign(mapInfo->creatures.begin(), mapInfo->creatures.end());
        allGuids.insert(allGuids.end(), mapInfo->summons.begin(), mapInfo->summons.end());
    }

    uint64 const instanceKey = GetMapKey(map);
    bool deferred = false;
    for (ObjectGuid const& guid : allGuids)
    {
        Creature* creature = map->GetCreature(guid);
        if (!creature || !creature->IsAlive())
            continue;

        if (creature->IsInCombat())
        {
            deferred = true;
            continue;
        }

        CreatureInfoPtr cInfo = GetCreatureInfo(instanceKey, guid);
        if (!AutoBalanceScaling::IsCreatureRelevant(creature))
        {
            if (cInfo)
            {
                std::lock_guard<std::recursive_mutex> creatureGuard(cInfo->stateLock);
                AutoBalanceScaling::RestoreCreatureStats(creature, *cInfo);
            }
            continue;
        }

        if (!cInfo)
            cInfo = GetOrCreateCreatureInfo(creature);
        if (!cInfo)
            continue;

        std::lock_guard<std::recursive_mutex> creatureGuard(cInfo->stateLock);
        AutoBalanceScaling::CalculateMultipliers(map, creature, *mapInfo, *cInfo);
        AutoBalanceScaling::ApplyCreatureStats(creature, *cInfo);
    }

    std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
    mapInfo->pendingRescale = deferred;
}

void AutoBalanceManager::RestoreMapCreatures(Map* map)
{
    if (!map)
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    std::vector<ObjectGuid> allGuids;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        allGuids.assign(mapInfo->creatures.begin(), mapInfo->creatures.end());
        allGuids.insert(allGuids.end(), mapInfo->summons.begin(), mapInfo->summons.end());
    }

    uint64 const instanceKey = GetMapKey(map);
    bool deferred = false;
    for (ObjectGuid const& guid : allGuids)
    {
        Creature* creature = map->GetCreature(guid);
        CreatureInfoPtr cInfo = GetCreatureInfo(instanceKey, guid);
        if (!creature || !cInfo)
            continue;

        if (creature->IsInCombat())
        {
            deferred = true;
            continue;
        }

        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        AutoBalanceScaling::RestoreCreatureStats(creature, *cInfo);
    }

    std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
    mapInfo->pendingRestore = deferred;
    if (!deferred)
    {
        mapInfo->combatLocked = false;
        mapInfo->combatLockTripped = false;
        mapInfo->combatLockMinPlayers = 0;
    }
}

void AutoBalanceManager::OnPlayerEnter(Map* map, Player* player)
{
    if (!map || !map->IsDungeon() || !player)
        return;

    MapInfoPtr mapInfo = GetOrCreateMapInfo(map);
    if (!mapInfo)
        return;

    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->enabled)
            return;
        uint8 const oldCount = mapInfo->playerCount;
        uint8 const oldAdjusted = mapInfo->adjustedPlayerCount;
        UpdateMapPlayerCounts(map, *mapInfo);
        changed = (mapInfo->playerCount != oldCount) || (mapInfo->adjustedPlayerCount != oldAdjusted);
        if (changed)
            mapInfo->mapConfigTime = GetCurrentAutoBalanceConfigTime();
    }

    if (sABConfig.IsDebugLoggingEnabled())
    {
        sLog.outString("[AutoBalance] Player %s%s entered map %u (%s) - player count: %u, adjusted difficulty: %u.",
            player->GetName(), player->IsGameMaster() ? " (GM)" : "", map->GetId(), map->GetMapName(),
            mapInfo->playerCount, static_cast<uint32>(mapInfo->adjustedPlayerCount));
    }

    if (changed)
        RescaleMapCreatures(map);

    if (sABConfig.IsPlayerChangeNotifyEnabled())
    {
        uint8 playerCount;
        uint8 adjustedCount;
        {
            std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
            playerCount = mapInfo->playerCount;
            adjustedCount = mapInfo->adjustedPlayerCount;
        }

        uint32 const maxPlayers = AutoBalanceScaling::GetMapMaxPlayers(map);
        char const* mapName = map->GetMapName();
        char const* difficulty = (map->IsRaid() || maxPlayers > 5 || AutoBalanceScaling::IsMapRaidActive(map)) ? "Raid" : "Normal";

        for (const auto& ref : map->GetPlayers())
        {
            Player* thisPlayer = ref.getSource();
            if (!thisPlayer || (thisPlayer->GetSession() && thisPlayer->GetSession()->IsHeadless()))
                continue;

            LocaleConstant const loc = thisPlayer->GetSession() ? thisPlayer->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;

            if (thisPlayer == player)
            {
                std::string const welcomeTemplate = ABGetLocaleText(loc, "welcome_to_player");
                std::string const welcomeMsg = AutoBalanceFormatting::StringFormat(
                    welcomeTemplate, mapName, maxPlayers, difficulty, playerCount, adjustedCount);
                ChatHandler(thisPlayer).SendSysMessage(welcomeMsg.c_str());

                if (player->IsGameMaster() && !sABConfig.IsCountGameMastersEnabled())
                {
                    std::string const gmMsg = ABGetLocaleText(loc, "welcome_to_gm");
                    ChatHandler(thisPlayer).SendSysMessage(gmMsg.c_str());
                }
            }
            else
            {
                if (!player->IsGameMaster() || sABConfig.IsCountGameMastersEnabled())
                {
                    std::string const announceTemplate = ABGetLocaleText(loc, "announce_non_gm_entering_instance");
                    std::string const announceMsg = AutoBalanceFormatting::StringFormat(
                        announceTemplate, player->GetName(), playerCount, adjustedCount);
                    ChatHandler(thisPlayer).SendSysMessage(announceMsg.c_str());
                }
            }
        }
    }
}

void AutoBalanceManager::OnPlayerLeave(Map* map, Player* player)
{
    if (!map || !map->IsDungeon() || !player)
        return;

    if (player->IsGameMaster() && !sABConfig.IsCountGameMastersEnabled())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    bool changed = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->enabled)
            return;
        uint8 const oldCount = mapInfo->playerCount;
        uint8 const oldAdjusted = mapInfo->adjustedPlayerCount;
        UpdateMapPlayerCounts(map, *mapInfo, player);
        changed = (mapInfo->playerCount != oldCount) || (mapInfo->adjustedPlayerCount != oldAdjusted);
        if (changed)
            mapInfo->mapConfigTime = GetCurrentAutoBalanceConfigTime();
        // Request post-unlink refresh on the map thread
        mapInfo->playerUnlinkPending = true;
    }

    if (sABConfig.IsDebugLoggingEnabled())
    {
        sLog.outString("[AutoBalance] Player %s left map %u (%s) - remaining players: %u, adjusted difficulty: %u.",
            player->GetName(), map->GetId(), map->GetMapName(),
            mapInfo->playerCount, static_cast<uint32>(mapInfo->adjustedPlayerCount));
    }

    if (changed)
        RescaleMapCreatures(map);

    if (sABConfig.IsPlayerChangeNotifyEnabled())
    {
        uint8 playerCount;
        uint8 adjustedCount;
        bool combatLocked;
        {
            std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
            playerCount = mapInfo->playerCount;
            adjustedCount = mapInfo->adjustedPlayerCount;
            combatLocked = mapInfo->combatLocked;
        }

        if (!player->IsGameMaster() && playerCount > 0)
        {
            for (const auto& ref : map->GetPlayers())
            {
                Player* thisPlayer = ref.getSource();
                if (!thisPlayer || thisPlayer == player || (thisPlayer->GetSession() && thisPlayer->GetSession()->IsHeadless()))
                    continue;

                LocaleConstant const loc = thisPlayer->GetSession() ? thisPlayer->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
                std::string leaveMsg;
                if (combatLocked)
                {
                    std::string const leaveTemplate = ABGetLocaleText(loc, "leaving_instance_combat");
                    leaveMsg = AutoBalanceFormatting::StringFormat(leaveTemplate, player->GetName(), adjustedCount);
                }
                else
                {
                    std::string const leaveTemplate = ABGetLocaleText(loc, "leaving_instance");
                    leaveMsg = AutoBalanceFormatting::StringFormat(leaveTemplate, player->GetName(), playerCount, adjustedCount);
                }

                ChatHandler(thisPlayer).SendSysMessage(leaveMsg.c_str());
            }
        }
    }
}

void AutoBalanceManager::HandlePlayerLevelChanged(Map* map, Player* player)
{
    if (!map || !map->IsDungeon() || !player || !sABConfig.IsLevelScalingEnabled())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->enabled)
            return;
        mapInfo->mapConfigTime = GetCurrentAutoBalanceConfigTime();
    }

    UpdateMapPlayerCounts(map, *mapInfo);
    RescaleMapCreatures(map);
}

void AutoBalanceManager::OnCreatureAdd(Creature* creature)
{
    if (!creature || !creature->IsAlive())
        return;

    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon() || !AutoBalanceScaling::IsCreatureRelevant(creature))
        return;

    MapInfoPtr mapInfo = GetOrCreateMapInfo(map);
    if (!mapInfo)
        return;

    {
        std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
        if (!mapInfo->enabled)
            return;

        if (mapInfo->playerCount == 0)
            UpdateMapPlayerCounts(map, *mapInfo);

        CreatureInfoPtr cInfo = GetOrCreateCreatureInfo(creature);
        if (!cInfo)
            return;

        std::lock_guard<std::recursive_mutex> creatureGuard(cInfo->stateLock);
        AutoBalanceScaling::InitializeCreatureBaseData(creature, *cInfo);
        uint8 const creatureLevel = cInfo->unmodifiedLevel;

        bool const isTempSummon = creature->IsTemporarySummon();
        if (!isTempSummon)
        {
            bool creatureBoundsChanged = false;
            if (mapInfo->lowestCreatureLevel == 0 || creatureLevel < mapInfo->lowestCreatureLevel)
            {
                mapInfo->lowestCreatureLevel = creatureLevel;
                creatureBoundsChanged = true;
            }
            if (creatureLevel > mapInfo->highestCreatureLevel)
            {
                mapInfo->highestCreatureLevel = creatureLevel;
                creatureBoundsChanged = true;
            }

            float const oldAvg = mapInfo->avgCreatureLevel;
            size_t const prevCount = mapInfo->creatures.size();
            mapInfo->creatures.insert(creature->GetObjectGuid());
            mapInfo->activeCreatureCount = static_cast<uint32>(mapInfo->creatures.size());
            if (mapInfo->creatures.size() > prevCount)
            {
                float const newCount = static_cast<float>(mapInfo->creatures.size());
                mapInfo->avgCreatureLevel = ((oldAvg * static_cast<float>(prevCount)) + static_cast<float>(creatureLevel)) / newCount;

                if (mapInfo->isLevelScalingEnabled && mapInfo->highestPlayerLevel > 0 && mapInfo->avgCreatureLevel > 0.0f)
                {
                    uint8 const origLvl = static_cast<uint8>(std::round(mapInfo->avgCreatureLevel));
                    uint8 const newLvl = std::min<uint8>(60, mapInfo->highestPlayerLevel);
                    if (origLvl > 0 && newLvl != origLvl)
                    {
                        float const origDmg = AutoBalanceScaling::GetClassLevelDamage(1, origLvl);
                        float const newDmg = AutoBalanceScaling::GetClassLevelDamage(1, newLvl);
                        float const dmgRatio = (origDmg > 0.0f) ? (newDmg / origDmg) : 1.0f;
                        mapInfo->scaledWorldDamageMultiplier = std::max(sABConfig.GetMinDamageModifier(), mapInfo->worldDamageMultiplier * dmgRatio);
                    }
                    else
                    {
                        mapInfo->scaledWorldDamageMultiplier = mapInfo->worldDamageMultiplier;
                    }
                }
            }

            AutoBalanceScaling::CalculateMultipliers(map, creature, *mapInfo, *cInfo);
            AutoBalanceScaling::ApplyCreatureStats(creature, *cInfo);

            if (creatureBoundsChanged && sABConfig.IsLevelScalingEnabled() && mapInfo->creatures.size() > 1)
            {
                mapInfo->mapConfigTime = GetCurrentAutoBalanceConfigTime();
                RescaleMapCreatures(map);
            }
        }
        else
        {
            // Summons scale from their own captured native stats without altering map bounds or averages
            cInfo->isTemporarySummon = true;
            mapInfo->summons.insert(creature->GetObjectGuid());
            AutoBalanceScaling::CalculateMultipliers(map, creature, *mapInfo, *cInfo);
            AutoBalanceScaling::ApplyCreatureStats(creature, *cInfo);
        }
    }
}

void AutoBalanceManager::OnCreatureRemove(Creature* creature)
{
    if (!creature)
        return;

    uint64 const instanceKey = GetMapKey(creature->GetMap());
    ObjectGuid const guid = creature->GetObjectGuid();

    if (CreatureInfoPtr cInfo = GetCreatureInfo(instanceKey, guid))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        AutoBalanceScaling::RestoreCreatureStats(creature, *cInfo);
    }

    if (Map* map = creature->GetMap())
    {
        if (MapInfoPtr mapInfo = GetMapInfo(map))
        {
            std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
            mapInfo->creatures.erase(guid);
            mapInfo->summons.erase(guid);
            mapInfo->activeCreatureCount = static_cast<uint32>(mapInfo->creatures.size());
        }
    }

    AutoBalanceCreatureKey const key{ instanceKey, guid };
    std::unique_lock<std::shared_mutex> guard(m_lock);
    m_creatures.erase(key);
}

void AutoBalanceManager::OnCreatureDeath(Creature* creature)
{
    if (!creature)
        return;

    if (CreatureInfoPtr cInfo = GetCreatureInfo(creature))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        cInfo->wasAliveNowDead = true;
    }
}

void AutoBalanceManager::OnCreatureRespawn(Creature* creature)
{
    if (!creature)
        return;

    if (CreatureInfoPtr cInfo = GetCreatureInfo(creature))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        cInfo->baseData = CreatureBaseData();
        cInfo->isActive = false;
        cInfo->wasAliveNowDead = false;
    }

    OnCreatureAdd(creature);
}

void AutoBalanceManager::OnMapDestroy(Map* map)
{
    if (!map)
        return;

    uint64 const instanceKey = GetMapKey(map);
    MapInfoPtr mapInfo;
    {
        std::unique_lock<std::shared_mutex> guard(m_lock);
        auto it = m_maps.find(instanceKey);
        if (it == m_maps.end())
            return;
        mapInfo = it->second;
        m_maps.erase(it);
    }

    std::vector<ObjectGuid> allGuids;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        allGuids.assign(mapInfo->creatures.begin(), mapInfo->creatures.end());
        allGuids.insert(allGuids.end(), mapInfo->summons.begin(), mapInfo->summons.end());
        mapInfo->creatures.clear();
        mapInfo->summons.clear();
    }

    std::unique_lock<std::shared_mutex> guard(m_lock);
    for (ObjectGuid const& guid : allGuids)
    {
        AutoBalanceCreatureKey const key{ instanceKey, guid };
        m_creatures.erase(key);
    }
}

void AutoBalanceManager::PerformMapMaintenance(Map* map, AutoBalanceMapInfo& mapInfo)
{
    if (!map)
        return;

    std::vector<ObjectGuid> retiredCreatures;
    std::vector<ObjectGuid> retiredSummons;

    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo.stateLock);
        for (ObjectGuid const& guid : mapInfo.creatures)
        {
            Creature* c = map->GetCreature(guid);
            if (!c || !c->IsInWorld())
                retiredCreatures.push_back(guid);
        }
        for (ObjectGuid const& guid : mapInfo.summons)
        {
            Creature* c = map->GetCreature(guid);
            if (!c || !c->IsInWorld())
                retiredSummons.push_back(guid);
        }

        for (ObjectGuid const& guid : retiredCreatures)
            mapInfo.creatures.erase(guid);
        for (ObjectGuid const& guid : retiredSummons)
            mapInfo.summons.erase(guid);

        mapInfo.activeCreatureCount = static_cast<uint32>(mapInfo.creatures.size());

        if (!retiredCreatures.empty())
        {
            // Recompute level bounds and average from remaining active normal creatures
            uint8 lowest = 255;
            uint8 highest = 0;
            double sumLevels = 0.0;
            uint32 counted = 0;

            for (ObjectGuid const& guid : mapInfo.creatures)
            {
                Creature* c = map->GetCreature(guid);
                if (c && c->IsAlive())
                {
                    uint8 const lvl = c->GetLevel();
                    if (lvl < lowest)
                        lowest = lvl;
                    if (lvl > highest)
                        highest = lvl;
                    sumLevels += lvl;
                    ++counted;
                }
            }

            if (counted > 0)
            {
                mapInfo.lowestCreatureLevel = lowest;
                mapInfo.highestCreatureLevel = highest;
                mapInfo.avgCreatureLevel = static_cast<float>(sumLevels / counted);
            }
            else
            {
                mapInfo.lowestCreatureLevel = 0;
                mapInfo.highestCreatureLevel = 0;
                mapInfo.avgCreatureLevel = 0.0f;
            }
        }
    }

    if (!retiredCreatures.empty() || !retiredSummons.empty())
    {
        uint64 const instanceKey = GetMapKey(map);
        std::unique_lock<std::shared_mutex> guard(m_lock);
        for (ObjectGuid const& guid : retiredCreatures)
            m_creatures.erase(AutoBalanceCreatureKey{ instanceKey, guid });
        for (ObjectGuid const& guid : retiredSummons)
            m_creatures.erase(AutoBalanceCreatureKey{ instanceKey, guid });
    }
}

void AutoBalanceManager::OnMapUpdate(Map* map, uint32 diff)
{
    if (!map || !map->IsDungeon())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    bool needsRescale = false;
    bool needsRestore = false;

    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);

        if (mapInfo->playerUnlinkPending)
        {
            mapInfo->playerUnlinkPending = false;
            uint8 const oldCount = mapInfo->playerCount;
            uint8 const oldAdjusted = mapInfo->adjustedPlayerCount;
            UpdateMapPlayerCounts(map, *mapInfo);
            if ((mapInfo->playerCount != oldCount) || (mapInfo->adjustedPlayerCount != oldAdjusted))
            {
                mapInfo->mapConfigTime = GetCurrentAutoBalanceConfigTime();
                needsRescale = true;
            }
        }

        if (mapInfo->configPending)
        {
            RefreshMapSettings(map, *mapInfo);
            mapInfo->configPending = false;
            UpdateMapPlayerCounts(map, *mapInfo);
            if (mapInfo->enabled)
                needsRescale = true;
            else
                needsRestore = true;
        }

        mapInfo->maintenanceTimer += diff;
        if (mapInfo->maintenanceTimer >= 5000)
        {
            mapInfo->maintenanceTimer = 0;
            PerformMapMaintenance(map, *mapInfo);
        }
    }

    if (needsRescale)
        RescaleMapCreatures(map);
    else if (needsRestore)
        RestoreMapCreatures(map);
}

void AutoBalanceManager::OnUnitEnterCombat(Unit* unit, Unit* victim)
{
    if (!unit)
        return;

    Map* map = unit->GetMap();
    if (!map || !map->IsDungeon())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    Creature* creature = unit->IsCreature() ? static_cast<Creature*>(unit) :
        (victim && victim->IsCreature() ? static_cast<Creature*>(victim) : nullptr);
    if (!creature || !AutoBalanceScaling::IsBoss(creature))
        return;

    std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
    if (!mapInfo->enabled || mapInfo->combatLocked)
        return;

    mapInfo->combatLocked = true;
    mapInfo->combatLockMinPlayers = mapInfo->playerCount;
    mapInfo->combatLockTripped = true;

    if (sABConfig.IsDebugLoggingEnabled())
    {
        sLog.outString("[AutoBalance] Combat lock engaged for boss %u (%s) on map %u (locked at %u players).",
            creature->GetEntry(), creature->GetName(), map->GetId(), static_cast<uint32>(mapInfo->combatLockMinPlayers));
    }
}

void AutoBalanceManager::OnUnitExitCombat(Unit* unit)
{
    if (!unit)
        return;

    Map* map = unit->GetMap();
    if (!map || !map->IsDungeon())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    bool anyInCombat = false;
    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            if (player->IsInCombat())
            {
                anyInCombat = true;
                break;
            }
        }
    }

    bool shouldRescale = false;
    bool shouldRestore = false;
    bool lockWasTripped = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->enabled || anyInCombat)
            return;

        lockWasTripped = mapInfo->combatLockTripped;
        shouldRescale = mapInfo->combatLockTripped || mapInfo->pendingRescale;
        shouldRestore = mapInfo->pendingRestore;

        mapInfo->combatLocked = false;
        mapInfo->combatLockMinPlayers = 0;
        mapInfo->combatLockTripped = false;
        mapInfo->pendingRescale = false;
        mapInfo->pendingRestore = false;
        UpdateMapPlayerCounts(map, *mapInfo);

        if (lockWasTripped && sABConfig.IsDebugLoggingEnabled())
        {
            sLog.outString("[AutoBalance] Combat lock released on map %u (%s).",
                map->GetId(), map->GetMapName());
        }
    }

    if (shouldRescale)
        RescaleMapCreatures(map);
    if (shouldRestore)
        RestoreMapCreatures(map);

    if (lockWasTripped && sABConfig.IsPlayerChangeNotifyEnabled())
    {
        for (const auto& ref : map->GetPlayers())
        {
            Player* player = ref.getSource();
            if (!player || (player->GetSession() && player->GetSession()->IsHeadless()))
                continue;

            LocaleConstant const loc = player->GetSession() ? player->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
            std::string const msg = ABGetLocaleText(loc, "leaving_instance_combat_change");
            ChatHandler(player).SendSysMessage(msg.c_str());
        }
    }
}
