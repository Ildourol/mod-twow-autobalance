/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#include "AutoBalanceManager.h"
#include "AutoBalanceConfig.h"
#include "AutoBalanceScaling.h"
#include "Maps/Map.h"
#include "Objects/Creature.h"
#include "Objects/Player.h"
#include "Chat/Chat.h"
#include "Log.h"
#include "StringFormat.h"
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

uint32 AutoBalanceManager::GetMapKey(Map* map) const
{
    if (!map)
        return 0;
    return map->IsDungeon() ? map->GetInstanceId() : map->GetId();
}

void AutoBalanceManager::RefreshMapSettings(Map* map, AutoBalanceMapInfo& mapInfo)
{
    if (!map)
        return;

    mapInfo.enabled = AutoBalanceScaling::ShouldMapBeEnabled(map);
    mapInfo.globalConfigTime = sABConfig.GetConfigTimestamp();
    mapInfo.mapConfigTime = sABConfig.GetConfigTimestamp();
    mapInfo.minPlayers = sABConfig.GetMinPlayersForMap(map->GetId(), map->IsRaid());
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

void AutoBalanceManager::Initialize()
{
    sABConfig.Load();
}

void AutoBalanceManager::Reload()
{
    // This is called after vMaNGOS has reloaded the main and module files.
    sABConfig.Reload();

    std::vector<MapInfoPtr> maps;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        maps.reserve(m_maps.size());
        for (auto const& pair : m_maps)
            maps.push_back(pair.second);
    }

    // Actual map traversal is deferred to OnMapUpdate so it runs on the
    // owning map-update thread rather than the world/config thread.
    for (MapInfoPtr const& mapInfo : maps)
        if (mapInfo)
        {
            std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
            mapInfo->configPending = true;
        }
}

AutoBalanceManager::MapInfoPtr AutoBalanceManager::GetMapInfo(Map* map)
{
    if (!map)
        return MapInfoPtr();

    std::lock_guard<std::mutex> guard(m_lock);
    auto it = m_maps.find(GetMapKey(map));
    return it != m_maps.end() ? it->second : MapInfoPtr();
}

AutoBalanceManager::MapInfoPtr AutoBalanceManager::GetOrCreateMapInfo(Map* map)
{
    if (!map)
        return MapInfoPtr();

    if (MapInfoPtr existing = GetMapInfo(map))
        return existing;

    MapInfoPtr created = std::make_shared<AutoBalanceMapInfo>();
    RefreshMapSettings(map, *created);

    std::lock_guard<std::mutex> guard(m_lock);
    auto result = m_maps.emplace(GetMapKey(map), created);
    return result.first->second;
}

AutoBalanceManager::CreatureInfoPtr AutoBalanceManager::GetCreatureInfo(ObjectGuid guid)
{
    std::lock_guard<std::mutex> guard(m_lock);
    auto it = m_creatures.find(guid);
    return it != m_creatures.end() ? it->second : CreatureInfoPtr();
}

AutoBalanceManager::CreatureInfoPtr AutoBalanceManager::GetOrCreateCreatureInfo(Creature* creature)
{
    if (!creature)
        return CreatureInfoPtr();

    ObjectGuid const guid = creature->GetObjectGuid();
    if (CreatureInfoPtr existing = GetCreatureInfo(guid))
        return existing;

    CreatureInfoPtr created = std::make_shared<AutoBalanceCreatureInfo>();
    std::lock_guard<std::mutex> guard(m_lock);
    auto result = m_creatures.emplace(guid, created);
    return result.first->second;
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

    if (CreatureInfoPtr cInfo = GetCreatureInfo(creature->GetObjectGuid()))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        if (cInfo->isActive)
            return true;
    }

    return IsMapScaled(creature->GetMap());
}

void AutoBalanceManager::UpdateMapPlayerCounts(Map* map, AutoBalanceMapInfo& mapInfo, Player const* excludedPlayer)
{
    if (!map)
        return;

    std::lock_guard<std::recursive_mutex> guard(mapInfo.stateLock);

    uint8 count = 0;
    uint8 lowestLvl = 255;
    uint8 highestLvl = 0;
    mapInfo.players.clear();

    for (const auto& ref : map->GetPlayers())
    {
        if (Player* player = ref.getSource())
        {
            // vMaNGOS fires OnPlayerLeaveAll before unlinking this player.
            if (player == excludedPlayer || player->IsGameMaster())
                continue;

            ++count;
            mapInfo.players.insert(player->GetObjectGuid());
            uint8 const level = player->GetLevel();
            if (level < lowestLvl)
                lowestLvl = level;
            if (level > highestLvl)
                highestLvl = level;
        }
    }

    mapInfo.playerCount = count;
    mapInfo.lowestPlayerLevel = lowestLvl == 255 ? 0 : lowestLvl;
    mapInfo.highestPlayerLevel = highestLvl;

    uint8 effective = count;
    if (mapInfo.combatLocked && mapInfo.combatLockMinPlayers > 0)
        effective = std::max(effective, mapInfo.combatLockMinPlayers);
    effective = std::max(effective, mapInfo.minPlayers);

    int adjusted = static_cast<int>(effective) + sABConfig.GetPlayerCountDifficultyOffset();
    uint32 const maxPlayers = std::max<uint32>(1, AutoBalanceScaling::GetMapMaxPlayers(map));
    adjusted = std::max(1, std::min(static_cast<int>(maxPlayers), adjusted));
    mapInfo.adjustedPlayerCount = static_cast<uint8>(adjusted);
}

void AutoBalanceManager::SendNotification(Map* map, std::string const& message)
{
    if (!map || message.empty())
        return;

    for (const auto& ref : map->GetPlayers())
        if (Player* player = ref.getSource())
            ChatHandler(player).SendSysMessage(message.c_str());
}

void AutoBalanceManager::RescaleMapCreatures(Map* map)
{
    if (!map || !map->IsDungeon())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
    if (!mapInfo->enabled)
        return;

    std::vector<ObjectGuid> creatureGuids(mapInfo->creatures.begin(), mapInfo->creatures.end());
    bool deferred = false;
    for (ObjectGuid const& guid : creatureGuids)
    {
        Creature* creature = map->GetCreature(guid);
        if (!creature || !creature->IsAlive())
            continue;
        if (creature->IsInCombat())
        {
            deferred = true;
            continue;
        }

        CreatureInfoPtr cInfo = GetCreatureInfo(guid);
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
    mapInfo->pendingRescale = deferred;
}

void AutoBalanceManager::RestoreMapCreatures(Map* map)
{
    if (!map)
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    std::vector<ObjectGuid> creatureGuids;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        creatureGuids.assign(mapInfo->creatures.begin(), mapInfo->creatures.end());
    }

    for (ObjectGuid const& guid : creatureGuids)
    {
        Creature* creature = map->GetCreature(guid);
        CreatureInfoPtr cInfo = GetCreatureInfo(guid);
        if (!creature || !cInfo)
            continue;

        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        AutoBalanceScaling::RestoreCreatureStats(creature, *cInfo);
    }
}

void AutoBalanceManager::OnPlayerEnter(Map* map, Player* player)
{
    if (!map || !map->IsDungeon() || !player || player->IsGameMaster())
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
        UpdateMapPlayerCounts(map, *mapInfo);
        changed = mapInfo->playerCount != oldCount;
    }

    if (!changed)
        return;

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
        std::string const message = AutoBalanceFormatting::StringFormat(
            "[AutoBalance] Player entered. Dungeon difficulty scaled for {}/{} player(s) (effective: {}).",
            playerCount, AutoBalanceScaling::GetMapMaxPlayers(map), adjustedCount);
        SendNotification(map, message);
    }
}

void AutoBalanceManager::OnPlayerLeave(Map* map, Player* player)
{
    if (!map || !map->IsDungeon() || !player || player->IsGameMaster())
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
        UpdateMapPlayerCounts(map, *mapInfo, player);
        changed = mapInfo->playerCount != oldCount;
    }

    if (!changed)
        return;

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
        if (playerCount > 0)
        {
            std::string const message = AutoBalanceFormatting::StringFormat(
                "[AutoBalance] Player left. Dungeon difficulty scaled for {}/{} player(s) (effective: {}).",
                playerCount, AutoBalanceScaling::GetMapMaxPlayers(map), adjustedCount);
            SendNotification(map, message);
        }
    }
}

void AutoBalanceManager::OnPlayerLevelChanged(Map* map, Player* player)
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

    bool creatureBoundsChanged = false;
    {
        std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
        if (!mapInfo->enabled)
            return;

        if (mapInfo->playerCount == 0)
            UpdateMapPlayerCounts(map, *mapInfo);

        uint8 const creatureLevel = creature->GetLevel();
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

        mapInfo->creatures.insert(creature->GetObjectGuid());
        mapInfo->activeCreatureCount = static_cast<uint32>(mapInfo->creatures.size());

        CreatureInfoPtr cInfo = GetOrCreateCreatureInfo(creature);
        if (!cInfo)
            return;

        std::lock_guard<std::recursive_mutex> creatureGuard(cInfo->stateLock);
        AutoBalanceScaling::CalculateMultipliers(map, creature, *mapInfo, *cInfo);
        AutoBalanceScaling::ApplyCreatureStats(creature, *cInfo);

        if (sABConfig.IsDebugLoggingEnabled())
        {
            sLog.outDebug("[AutoBalance] Scaled creature %u on map %u for %u effective player(s): health %.3f, damage %.3f.",
                creature->GetEntry(), map->GetId(), static_cast<uint32>(mapInfo->adjustedPlayerCount),
                cInfo->scaledHealthMultiplier, cInfo->scaledDamageMultiplier);
        }
    }

    // Dynamic level offsets depend on the map's highest native creature level.
    // Re-evaluate earlier spawns whenever that bound changes.
    if (creatureBoundsChanged && sABConfig.IsLevelScalingEnabled())
        RescaleMapCreatures(map);
}

void AutoBalanceManager::OnCreatureRemove(Creature* creature)
{
    if (!creature)
        return;

    ObjectGuid const guid = creature->GetObjectGuid();
    if (CreatureInfoPtr cInfo = GetCreatureInfo(guid))
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
            mapInfo->activeCreatureCount = static_cast<uint32>(mapInfo->creatures.size());
        }
    }

    std::lock_guard<std::mutex> guard(m_lock);
    m_creatures.erase(guid);
}

void AutoBalanceManager::OnCreatureRespawn(Creature* creature)
{
    if (!creature)
        return;

    // SelectLevel has just restored a fresh vMaNGOS base roll. Discard the
    // previous spawn's capture so scaling never compounds across respawns.
    if (CreatureInfoPtr cInfo = GetCreatureInfo(creature->GetObjectGuid()))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        cInfo->baseData = CreatureBaseData();
        cInfo->isActive = false;
    }

    OnCreatureAdd(creature);
}

void AutoBalanceManager::OnMapDestroy(Map* map)
{
    if (!map)
        return;

    MapInfoPtr mapInfo;
    {
        std::lock_guard<std::mutex> guard(m_lock);
        auto it = m_maps.find(GetMapKey(map));
        if (it == m_maps.end())
            return;
        mapInfo = it->second;
        m_maps.erase(it);
    }

    std::vector<ObjectGuid> creatureGuids;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        creatureGuids.assign(mapInfo->creatures.begin(), mapInfo->creatures.end());
        mapInfo->creatures.clear();
    }

    std::lock_guard<std::mutex> guard(m_lock);
    for (ObjectGuid const& guid : creatureGuids)
        m_creatures.erase(guid);
}

void AutoBalanceManager::OnMapUpdate(Map* map)
{
    if (!map || !map->IsDungeon())
        return;

    MapInfoPtr mapInfo = GetMapInfo(map);
    if (!mapInfo)
        return;

    bool enabled = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->configPending)
            return;

        RefreshMapSettings(map, *mapInfo);
        mapInfo->configPending = false;
        enabled = mapInfo->enabled;
    }

    UpdateMapPlayerCounts(map, *mapInfo);
    if (enabled)
        RescaleMapCreatures(map);
    else
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
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->enabled || anyInCombat)
            return;

        shouldRescale = mapInfo->combatLockTripped || mapInfo->pendingRescale;
        mapInfo->combatLocked = false;
        mapInfo->combatLockMinPlayers = 0;
        mapInfo->combatLockTripped = false;
        mapInfo->pendingRescale = false;
        UpdateMapPlayerCounts(map, *mapInfo);
    }

    if (shouldRescale)
        RescaleMapCreatures(map);
}
