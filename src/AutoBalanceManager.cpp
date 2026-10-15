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
    if (!sABConfig.IsGlobalEnabled())
        return;

    uint32 count = 0;
    for (auto itr = sMapStorage.begin<MapEntry>(); itr < sMapStorage.end<MapEntry>(); ++itr)
    {
        MapEntry* entry = const_cast<MapEntry*>(*itr);
        if (!entry)
            continue;

        if (entry->IsDungeon())
        {
            if (entry->IsRaid())
                entry->maxPlayers = sABConfig.GetRaidPlayerLimitMax();
            else
                entry->maxPlayers = sABConfig.GetDungeonPlayerLimitMax();
            ++count;
        }
    }

    sLog.outString("[AutoBalance] Updated %u instance player limits in memory (Dungeons Max: %u, Raids Max: %u).",
        count, sABConfig.GetDungeonPlayerLimitMax(), sABConfig.GetRaidPlayerLimitMax());
}

void AutoBalanceManager::Initialize()
{
    sABConfig.Load();
    AutoBalanceScaling::LoadCreatureClassLevelStats();
    ApplyMemoryMapLimits();
}

void AutoBalanceManager::Reload()
{
    sABConfig.Reload();
    AutoBalanceScaling::LoadCreatureClassLevelStats();
    ApplyMemoryMapLimits();

    std::vector<MapInfoPtr> maps;
    {
        std::shared_lock<std::shared_mutex> guard(m_lock);
        maps.reserve(m_maps.size());
        for (auto const& pair : m_maps)
            maps.push_back(pair.second);
    }

    // Actual map traversal is deferred to OnMapUpdate so it runs on the
    // owning map-update thread rather than the world/config thread.
    for (MapInfoPtr const& mapInfo : maps)
    {
        if (mapInfo)
        {
            std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
            mapInfo->configPending = true;
        }
    }
}

AutoBalanceManager::MapInfoPtr AutoBalanceManager::GetMapInfo(Map* map)
{
    if (!map)
        return MapInfoPtr();

    std::shared_lock<std::shared_mutex> guard(m_lock);
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

    std::unique_lock<std::shared_mutex> guard(m_lock);
    auto result = m_maps.emplace(GetMapKey(map), created);
    return result.first->second;
}

AutoBalanceManager::CreatureInfoPtr AutoBalanceManager::GetCreatureInfo(ObjectGuid guid)
{
    std::shared_lock<std::shared_mutex> guard(m_lock);
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
    std::unique_lock<std::shared_mutex> guard(m_lock);
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
            // vMaNGOS fires OnPlayerLeaveAll before unlinking this player.
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

            // PlayerBots (TortoiseBots) weight adjustment
            bool isBot = (player->GetSession() && player->GetSession()->IsHeadless());
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

    // Upstream parity: if all present players are GMs (count == 0), derive level baseline from
    // present players so instance level scaling does not collapse to 0.
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

    // Compute fallback world multipliers for environmental effects, traps, and non-tracked spawns
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
            // Suppress chat system messages for headless PlayerBots sessions
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

    std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
    if (!mapInfo->enabled)
        return;

    std::vector<ObjectGuid> creatureGuids(mapInfo->creatures.begin(), mapInfo->creatures.end());
    if (sABConfig.IsDebugLoggingEnabled())
    {
        sLog.outString("[AutoBalance] Rescaling %u creatures on map %u (%s) for %u player(s).",
            static_cast<uint32>(creatureGuids.size()), map->GetId(), map->GetMapName(),
            static_cast<uint32>(mapInfo->adjustedPlayerCount));
    }

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
            if (!thisPlayer)
                continue;

            // Suppress chat system messages for headless PlayerBots sessions
            if (thisPlayer->GetSession() && thisPlayer->GetSession()->IsHeadless())
                continue;

            LocaleConstant loc = thisPlayer->GetSession() ? thisPlayer->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;

            if (thisPlayer == player) // This is the player that entered
            {
                std::string const welcomeTemplate = ABGetLocaleText(loc, "welcome_to_player");
                std::string const welcomeMsg = AutoBalanceFormatting::StringFormat(
                    welcomeTemplate, mapName, maxPlayers, difficulty, playerCount, adjustedCount);
                ChatHandler(thisPlayer).SendSysMessage(welcomeMsg.c_str());

                // Notify GMs that they won't be accounted for
                if (player->IsGameMaster() && !sABConfig.IsCountGameMastersEnabled())
                {
                    std::string const gmMsg = ABGetLocaleText(loc, "welcome_to_gm");
                    ChatHandler(thisPlayer).SendSysMessage(gmMsg.c_str());
                }
            }
            else // Existing player in dungeon
            {
                // Announce non-GMs entering the instance only
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
    {
        if (sABConfig.IsDebugLoggingEnabled())
        {
            sLog.outString("[AutoBalance] Player %s (GM) left map %u (%s).",
                player->GetName(), map->GetId(), map->GetMapName());
        }
        return;
    }

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

        // Notify remaining players in the instance that a player left
        if (!player->IsGameMaster() && playerCount > 0)
        {
            for (const auto& ref : map->GetPlayers())
            {
                Player* thisPlayer = ref.getSource();
                if (!thisPlayer || thisPlayer == player)
                    continue;

                // Suppress chat system messages for headless PlayerBots sessions
                if (thisPlayer->GetSession() && thisPlayer->GetSession()->IsHeadless())
                    continue;

                LocaleConstant loc = thisPlayer->GetSession() ? thisPlayer->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
                std::string leaveMsg;
                if (combatLocked)
                {
                    std::string const leaveTemplate = ABGetLocaleText(loc, "leaving_instance_combat");
                    leaveMsg = AutoBalanceFormatting::StringFormat(
                        leaveTemplate, player->GetName(), adjustedCount);
                }
                else
                {
                    std::string const leaveTemplate = ABGetLocaleText(loc, "leaving_instance");
                    leaveMsg = AutoBalanceFormatting::StringFormat(
                        leaveTemplate, player->GetName(), playerCount, adjustedCount);
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

            if (sABConfig.IsDebugLoggingEnabled())
            {
                sLog.outString("[AutoBalance] Scaled creature %u (%s) on map %u for %u player(s): HP x%.3f, Dmg x%.3f, Lvl %u.",
                    creature->GetEntry(), creature->GetName(), map->GetId(), static_cast<uint32>(mapInfo->adjustedPlayerCount),
                    cInfo->scaledHealthMultiplier, cInfo->scaledDamageMultiplier, creature->GetLevel());
            }

            if (creatureBoundsChanged && sABConfig.IsLevelScalingEnabled() && mapInfo->creatures.size() > 1)
            {
                mapInfo->mapConfigTime = GetCurrentAutoBalanceConfigTime();
                RescaleMapCreatures(map);
            }
        }
        else
        {
            // Upstream parity: temporary summons scale with their summoner/instance stats,
            // but must not contaminate map creature bounds, averages, or trigger instance rescale storms.
            AutoBalanceScaling::CalculateMultipliers(map, creature, *mapInfo, *cInfo);
            AutoBalanceScaling::ApplyCreatureStats(creature, *cInfo);

            if (sABConfig.IsDebugLoggingEnabled())
            {
                sLog.outString("[AutoBalance] Scaled temporary summon %u (%s) on map %u for %u player(s): HP x%.3f, Dmg x%.3f, Lvl %u.",
                    creature->GetEntry(), creature->GetName(), map->GetId(), static_cast<uint32>(mapInfo->adjustedPlayerCount),
                    cInfo->scaledHealthMultiplier, cInfo->scaledDamageMultiplier, creature->GetLevel());
            }
        }
    }
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

    std::unique_lock<std::shared_mutex> guard(m_lock);
    m_creatures.erase(guid);
}

void AutoBalanceManager::OnCreatureDeath(Creature* creature)
{
    if (!creature)
        return;

    ObjectGuid const guid = creature->GetObjectGuid();
    if (CreatureInfoPtr cInfo = GetCreatureInfo(guid))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        cInfo->wasAliveNowDead = true;
    }
}

void AutoBalanceManager::OnCreatureRespawn(Creature* creature)
{
    if (!creature)
        return;

    // SelectLevel has restored a fresh base roll. Discard old capture.
    if (CreatureInfoPtr cInfo = GetCreatureInfo(creature->GetObjectGuid()))
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        cInfo->baseData = CreatureBaseData();
        cInfo->isActive = false;
        cInfo->wasAliveNowDead = false;
    }

    if (sABConfig.IsDebugLoggingEnabled())
    {
        sLog.outString("[AutoBalance] Respawned creature %u (%s) on map %u - re-applying scaled stats.",
            creature->GetEntry(), creature->GetName(), creature->GetMapId());
    }

    OnCreatureAdd(creature);
}

void AutoBalanceManager::OnMapDestroy(Map* map)
{
    if (!map)
        return;

    MapInfoPtr mapInfo;
    {
        std::unique_lock<std::shared_mutex> guard(m_lock);
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

    std::unique_lock<std::shared_mutex> guard(m_lock);
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
    bool lockWasTripped = false;
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (!mapInfo->enabled || anyInCombat)
            return;

        lockWasTripped = mapInfo->combatLockTripped;
        shouldRescale = mapInfo->combatLockTripped || mapInfo->pendingRescale;
        mapInfo->combatLocked = false;
        mapInfo->combatLockMinPlayers = 0;
        mapInfo->combatLockTripped = false;
        mapInfo->pendingRescale = false;
        UpdateMapPlayerCounts(map, *mapInfo);

        if (lockWasTripped && sABConfig.IsDebugLoggingEnabled())
        {
            sLog.outString("[AutoBalance] Combat lock released on map %u (%s).",
                map->GetId(), map->GetMapName());
        }
    }

    if (shouldRescale)
        RescaleMapCreatures(map);

    if (lockWasTripped && sABConfig.IsPlayerChangeNotifyEnabled())
    {
        for (const auto& ref : map->GetPlayers())
        {
            Player* player = ref.getSource();
            if (!player || (player->GetSession() && player->GetSession()->IsHeadless()))
                continue;

            LocaleConstant loc = player->GetSession() ? player->GetSession()->GetSessionDbcLocale() : LOCALE_enUS;
            std::string const msg = ABGetLocaleText(loc, "leaving_instance_combat_change");
            ChatHandler(player).SendSysMessage(msg.c_str());
        }
    }
}
