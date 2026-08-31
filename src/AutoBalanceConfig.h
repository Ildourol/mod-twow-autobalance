/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#ifndef MOD_TWOW_AUTOBALANCE_CONFIG_H
#define MOD_TWOW_AUTOBALANCE_CONFIG_H

#include "AutoBalance.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class AutoBalanceConfig
{
public:
    static AutoBalanceConfig& Instance();

    void Load();
    void Reload();

    // Enable / General
    bool IsGlobalEnabled() const { return m_enableGlobal; }
    bool Is5MEnabled() const { return m_enable5M; }
    bool Is10MEnabled() const { return m_enable10M; }
    bool Is15MEnabled() const { return m_enable15M; }
    bool Is20MEnabled() const { return m_enable20M; }
    bool Is25MEnabled() const { return m_enable25M; }
    bool Is40MEnabled() const { return m_enable40M; }
    bool IsOtherNormalEnabled() const { return m_enableOtherNormal; }

    bool IsDungeonDisabled(uint32 mapId) const;

    // Minimum Players
    uint8 GetMinPlayersNormal() const { return m_minPlayersNormal; }
    uint8 GetMinPlayersRaid() const { return m_minPlayersRaid; }
    uint8 GetMinPlayersForMap(uint32 mapId, bool isRaid) const;

    // Inflection Point Settings
    AutoBalanceInflectionPointSettings const& GetDefaultInflectionSettings() const { return m_defaultInflection; }
    AutoBalanceInflectionPointSettings const& GetRaidInflectionSettings(uint32 maxPlayers = 40) const;
    AutoBalanceInflectionPointSettings const* GetMapInflectionOverride(uint32 mapId, bool isBoss) const;

    // Stat Modifiers
    AutoBalanceStatModifiers const& GetNormalStatModifiers() const { return m_normalStatModifiers; }
    AutoBalanceStatModifiers const& GetBossStatModifiers() const { return m_bossStatModifiers; }
    AutoBalanceStatModifiers const& GetRaidStatModifiers(uint32 maxPlayers, bool isBoss) const;
    AutoBalanceStatModifiers const* GetMapStatModifierOverride(uint32 mapId, bool isBoss) const;
    AutoBalanceStatModifiers const* GetCreatureStatModifierOverride(uint32 entry) const;

    // Multiplier Minimums / Maximums
    float GetMinHPModifier() const { return m_minHPModifier; }
    float GetMinManaModifier() const { return m_minManaModifier; }
    float GetMinDamageModifier() const { return m_minDamageModifier; }
    float GetMinCCDurationModifier() const { return m_minCCDurationModifier; }
    float GetMaxCCDurationModifier() const { return m_maxCCDurationModifier; }

    // Difficulty Offset
    int8 GetPlayerCountDifficultyOffset() const { return m_playerCountDifficultyOffset; }
    void SetPlayerCountDifficultyOffset(int8 offset) { m_playerCountDifficultyOffset = offset; }

    // Forced ID & Disabled Creatures
    int GetForcedPlayerCount(uint32 entry) const;
    bool IsCreatureDisabled(uint32 entry) const;

    // Level Scaling
    bool IsLevelScalingEnabled() const { return m_levelScaling; }
    ScalingMethod GetLevelScalingMethod() const { return m_levelScalingMethod; }
    uint8 GetLevelScalingSkipHigherLevels() const { return m_levelScalingSkipHigherLevels; }
    uint8 GetLevelScalingSkipLowerLevels() const { return m_levelScalingSkipLowerLevels; }
    uint8 GetDynamicLevelCeiling(bool isRaid) const { return isRaid ? m_dynamicCeilingRaids : m_dynamicCeilingDungeons; }
    uint8 GetDynamicLevelFloor(bool isRaid) const { return isRaid ? m_dynamicFloorRaids : m_dynamicFloorDungeons; }
    AutoBalanceDynamicLevelSettings const* GetDynamicLevelOverride(uint32 mapId) const;

    // Reward Scaling
    ScalingMethod GetRewardScalingMethod() const { return m_rewardScalingMethod; }
    bool IsRewardScalingXPEnabled() const { return m_rewardScalingXP; }
    float GetRewardScalingXPModifier() const { return m_rewardScalingXPModifier; }
    bool IsRewardScalingMoneyEnabled() const { return m_rewardScalingMoney; }
    float GetRewardScalingMoneyModifier() const { return m_rewardScalingMoneyModifier; }

    // Notification & Announcements
    bool IsPlayerChangeNotifyEnabled() const { return m_playerChangeNotify; }
    bool IsAnnouncementEnabled() const { return m_announcement; }
    bool IsDebugLoggingEnabled() const { return m_debugLogging; }

    // Coexistence
    bool IsCoexistenceDisableCoreAutoScaler() const { return m_coexistenceDisableCoreAutoScaler; }

    uint64_t GetConfigTimestamp() const { return m_configTimestamp; }

private:
    AutoBalanceConfig() = default;

    static std::vector<std::string> Tokenize(std::string const& str, char delimiter);
    static void ParseIdList(std::string const& str, std::unordered_set<uint32>& outSet);
    static void ParseForcedIdList(std::string const& str, int count, std::unordered_map<uint32, int>& outMap);
    static void ParseMinPlayersList(std::string const& str, std::unordered_map<uint32, uint8>& outMap);
    static void ParseInflectionOverrides(std::string const& str, std::unordered_map<uint32, AutoBalanceInflectionPointSettings>& outMap);
    static void ParseStatModifierOverrides(std::string const& str, std::unordered_map<uint32, AutoBalanceStatModifiers>& outMap);
    static void ParseDynamicLevelOverrides(std::string const& str, std::unordered_map<uint32, AutoBalanceDynamicLevelSettings>& outMap);

    bool m_enableGlobal = false;
    bool m_enable5M = true;
    bool m_enable10M = true;
    bool m_enable15M = true;
    bool m_enable20M = true;
    bool m_enable25M = true;
    bool m_enable40M = true;
    bool m_enableOtherNormal = true;

    std::unordered_set<uint32> m_disabledDungeonIds;

    uint8 m_minPlayersNormal = 1;
    uint8 m_minPlayersRaid = 1;
    std::unordered_map<uint32, uint8> m_minPlayersPerDungeon;

    AutoBalanceInflectionPointSettings m_defaultInflection;
    AutoBalanceInflectionPointSettings m_raidInflection;
    AutoBalanceInflectionPointSettings m_raid10MInflection;
    AutoBalanceInflectionPointSettings m_raid15MInflection;
    AutoBalanceInflectionPointSettings m_raid20MInflection;
    AutoBalanceInflectionPointSettings m_raid25MInflection;
    AutoBalanceInflectionPointSettings m_raid40MInflection;
    std::unordered_map<uint32, AutoBalanceInflectionPointSettings> m_mapInflectionOverrides;
    std::unordered_map<uint32, AutoBalanceInflectionPointSettings> m_bossMapInflectionOverrides;

    AutoBalanceStatModifiers m_normalStatModifiers;
    AutoBalanceStatModifiers m_bossStatModifiers;
    AutoBalanceStatModifiers m_raidStatModifiers;
    AutoBalanceStatModifiers m_raid10MStatModifiers;
    AutoBalanceStatModifiers m_raid20MStatModifiers;
    AutoBalanceStatModifiers m_raid40MStatModifiers;
    AutoBalanceStatModifiers m_raidBossStatModifiers;
    AutoBalanceStatModifiers m_raid10MBossStatModifiers;
    AutoBalanceStatModifiers m_raid20MBossStatModifiers;
    AutoBalanceStatModifiers m_raid40MBossStatModifiers;
    std::unordered_map<uint32, AutoBalanceStatModifiers> m_mapStatModifierOverrides;
    std::unordered_map<uint32, AutoBalanceStatModifiers> m_mapBossStatModifierOverrides;
    std::unordered_map<uint32, AutoBalanceStatModifiers> m_creatureStatModifierOverrides;

    float m_minHPModifier = 0.01f;
    float m_minManaModifier = 0.01f;
    float m_minDamageModifier = 0.01f;
    float m_minCCDurationModifier = 0.25f;
    float m_maxCCDurationModifier = 1.0f;

    int8 m_playerCountDifficultyOffset = 0;

    std::unordered_map<uint32, int> m_forcedCreatureCounts;
    std::unordered_set<uint32> m_disabledCreatureIds;

    bool m_levelScaling = false;
    ScalingMethod m_levelScalingMethod = AUTOBALANCE_SCALING_DYNAMIC;
    uint8 m_levelScalingSkipHigherLevels = 3;
    uint8 m_levelScalingSkipLowerLevels = 5;
    uint8 m_dynamicCeilingDungeons = 1;
    uint8 m_dynamicFloorDungeons = 5;
    uint8 m_dynamicCeilingRaids = 3;
    uint8 m_dynamicFloorRaids = 5;
    std::unordered_map<uint32, AutoBalanceDynamicLevelSettings> m_dynamicLevelOverrides;

    ScalingMethod m_rewardScalingMethod = AUTOBALANCE_SCALING_DYNAMIC;
    bool m_rewardScalingXP = true;
    float m_rewardScalingXPModifier = 1.0f;
    bool m_rewardScalingMoney = true;
    float m_rewardScalingMoneyModifier = 1.0f;

    bool m_playerChangeNotify = true;
    bool m_announcement = true;
    bool m_debugLogging = false;

    bool m_coexistenceDisableCoreAutoScaler = true;

    uint64_t m_configTimestamp = 1;
};

#define sABConfig (AutoBalanceConfig::Instance())

#endif // MOD_TWOW_AUTOBALANCE_CONFIG_H
