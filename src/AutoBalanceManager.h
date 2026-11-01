/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#ifndef MOD_TWOW_AUTOBALANCE_MANAGER_H
#define MOD_TWOW_AUTOBALANCE_MANAGER_H

#include "AutoBalance.h"
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>

class Map;
class Creature;
class Player;
class Unit;

class AutoBalanceManager
{
public:
    using MapInfoPtr = std::shared_ptr<AutoBalanceMapInfo>;
    using CreatureInfoPtr = std::shared_ptr<AutoBalanceCreatureInfo>;

    static AutoBalanceManager& Instance();

    void Initialize();
    void Reload();
    void ApplyMemoryMapLimits();
    void ScheduleAllMapsRefresh();

    uint32 GetNativeMapCapacity(uint32 mapId) const;

    uint64 GetMapKey(Map* map) const;
    MapInfoPtr GetMapInfo(Map* map);
    MapInfoPtr GetMapInfo(uint64 instanceKey);
    MapInfoPtr GetOrCreateMapInfo(Map* map);

    CreatureInfoPtr GetCreatureInfo(Creature* creature);
    CreatureInfoPtr GetCreatureInfo(uint64 instanceKey, ObjectGuid guid);
    CreatureInfoPtr GetOrCreateCreatureInfo(Creature* creature);

    bool IsMapScaled(Map* map);
    bool IsCreatureScaled(Creature* creature);

    void OnPlayerEnter(Map* map, Player* player);
    void OnPlayerLeave(Map* map, Player* player);
    void HandlePlayerLevelChanged(Map* map, Player* player);

    void OnCreatureAdd(Creature* creature);
    void OnCreatureRemove(Creature* creature);
    void OnCreatureDeath(Creature* creature);
    void OnCreatureRespawn(Creature* creature);

    void OnMapDestroy(Map* map);
    void OnMapUpdate(Map* map, uint32 diff);
    void PerformMapMaintenance(Map* map, AutoBalanceMapInfo& mapInfo);

    void OnUnitEnterCombat(Unit* unit, Unit* victim);
    void OnUnitExitCombat(Unit* unit);

    void UpdateMapPlayerCounts(Map* map, AutoBalanceMapInfo& mapInfo, Player const* excludedPlayer = nullptr);
    void RescaleMapCreatures(Map* map);
    void RestoreMapCreatures(Map* map);

    void SendNotification(Map* map, std::string const& message);

private:
    AutoBalanceManager() = default;

    void RefreshMapSettings(Map* map, AutoBalanceMapInfo& mapInfo);

    std::unordered_map<uint64, MapInfoPtr> m_maps;
    std::unordered_map<AutoBalanceCreatureKey, CreatureInfoPtr, AutoBalanceCreatureKeyHash> m_creatures;
    std::unordered_map<uint32, uint32> m_nativeMapCapacities;
    bool m_nativeCapacitiesCaptured = false;
    mutable std::shared_mutex m_lock;
};

#define sAutoBalanceMgr (AutoBalanceManager::Instance())

#endif // MOD_TWOW_AUTOBALANCE_MANAGER_H
