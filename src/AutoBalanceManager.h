/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#ifndef MOD_TWOW_AUTOBALANCE_MANAGER_H
#define MOD_TWOW_AUTOBALANCE_MANAGER_H

#include "AutoBalance.h"
#include <memory>
#include <mutex>
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

    MapInfoPtr GetMapInfo(Map* map);
    MapInfoPtr GetOrCreateMapInfo(Map* map);

    CreatureInfoPtr GetCreatureInfo(ObjectGuid guid);
    CreatureInfoPtr GetOrCreateCreatureInfo(Creature* creature);

    bool IsMapScaled(Map* map);
    bool IsCreatureScaled(Creature* creature);

    void OnPlayerEnter(Map* map, Player* player);
    void OnPlayerLeave(Map* map, Player* player);
    void OnPlayerLevelChanged(Map* map, Player* player);

    void OnCreatureAdd(Creature* creature);
    void OnCreatureRemove(Creature* creature);
    void OnCreatureRespawn(Creature* creature);

    void OnMapDestroy(Map* map);
    void OnMapUpdate(Map* map);

    void OnUnitEnterCombat(Unit* unit, Unit* victim);
    void OnUnitExitCombat(Unit* unit);

    void UpdateMapPlayerCounts(Map* map, AutoBalanceMapInfo& mapInfo, Player const* excludedPlayer = nullptr);
    void RescaleMapCreatures(Map* map);
    void RestoreMapCreatures(Map* map);

    void SendNotification(Map* map, std::string const& message);

private:
    AutoBalanceManager() = default;

    uint32 GetMapKey(Map* map) const;
    void RefreshMapSettings(Map* map, AutoBalanceMapInfo& mapInfo);

    std::unordered_map<uint32, MapInfoPtr> m_maps;
    std::unordered_map<ObjectGuid, CreatureInfoPtr> m_creatures;
    std::mutex m_lock;
};

#define sAutoBalanceMgr (AutoBalanceManager::Instance())

#endif // MOD_TWOW_AUTOBALANCE_MANAGER_H
