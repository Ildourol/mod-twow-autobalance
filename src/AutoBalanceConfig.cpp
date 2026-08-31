/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#include "AutoBalanceConfig.h"
#include "Config/Config.h"
#include "Log.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace
{
uint8 ClampConfigUint8(int value, int minimum = 0)
{
    return static_cast<uint8>(std::max(minimum, std::min(255, value)));
}

int8 ClampConfigInt8(int value)
{
    return static_cast<int8>(std::max(-128, std::min(127, value)));
}
}

AutoBalanceConfig& AutoBalanceConfig::Instance()
{
    static AutoBalanceConfig instance;
    return instance;
}

std::vector<std::string> AutoBalanceConfig::Tokenize(std::string const& str, char delimiter)
{
    std::vector<std::string> tokens;
    std::stringstream ss(str);
    std::string token;
    while (std::getline(ss, token, delimiter))
    {
        // trim whitespace
        size_t first = token.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        size_t last = token.find_last_not_of(" \t\r\n");
        tokens.push_back(token.substr(first, last - first + 1));
    }
    return tokens;
}

void AutoBalanceConfig::ParseIdList(std::string const& str, std::unordered_set<uint32>& outSet)
{
    outSet.clear();
    if (str.empty())
        return;

    std::vector<std::string> tokens = Tokenize(str, ',');
    for (std::string const& t : tokens)
    {
        if (t.empty())
            continue;
        try
        {
            uint32 id = static_cast<uint32>(std::stoul(t));
            outSet.insert(id);
        }
        catch (...) {}
    }
}

void AutoBalanceConfig::ParseForcedIdList(std::string const& str, int count, std::unordered_map<uint32, int>& outMap)
{
    if (str.empty())
        return;

    std::vector<std::string> tokens = Tokenize(str, ',');
    for (std::string const& t : tokens)
    {
        if (t.empty())
            continue;
        try
        {
            uint32 id = static_cast<uint32>(std::stoul(t));
            outMap[id] = count;
        }
        catch (...) {}
    }
}

void AutoBalanceConfig::ParseMinPlayersList(std::string const& str, std::unordered_map<uint32, uint8>& outMap)
{
    outMap.clear();
    if (str.empty())
        return;

    std::vector<std::string> entries = Tokenize(str, ',');
    for (std::string const& entry : entries)
    {
        std::vector<std::string> parts = Tokenize(entry, ':');
        if (parts.size() == 2)
        {
            try
            {
                uint32 mapId = static_cast<uint32>(std::stoul(parts[0]));
                unsigned long const parsed = std::stoul(parts[1]);
                uint8 minP = static_cast<uint8>(std::max(1ul, std::min(255ul, parsed)));
                outMap[mapId] = minP;
            }
            catch (...) {}
        }
    }
}

void AutoBalanceConfig::ParseInflectionOverrides(std::string const& str, std::unordered_map<uint32, AutoBalanceInflectionPointSettings>& outMap)
{
    outMap.clear();
    if (str.empty())
        return;

    std::vector<std::string> entries = Tokenize(str, ',');
    for (std::string const& entry : entries)
    {
        std::vector<std::string> parts = Tokenize(entry, ':');
        if (parts.size() >= 4)
        {
            try
            {
                uint32 mapId = static_cast<uint32>(std::stoul(parts[0]));
                float val = std::stof(parts[1]);
                float floor = std::stof(parts[2]);
                float ceil = std::stof(parts[3]);
                float bossMod = (parts.size() >= 5) ? std::stof(parts[4]) : 1.0f;
                outMap[mapId] = AutoBalanceInflectionPointSettings(val, floor, ceil, bossMod);
            }
            catch (...) {}
        }
    }
}

void AutoBalanceConfig::ParseStatModifierOverrides(std::string const& str, std::unordered_map<uint32, AutoBalanceStatModifiers>& outMap)
{
    outMap.clear();
    if (str.empty())
        return;

    std::vector<std::string> entries = Tokenize(str, ',');
    for (std::string const& entry : entries)
    {
        std::vector<std::string> parts = Tokenize(entry, ':');
        if (parts.size() >= 7)
        {
            try
            {
                uint32 id = static_cast<uint32>(std::stoul(parts[0]));
                float g = std::stof(parts[1]);
                float h = std::stof(parts[2]);
                float m = std::stof(parts[3]);
                float a = std::stof(parts[4]);
                float d = std::stof(parts[5]);
                float cc = std::stof(parts[6]);
                outMap[id] = AutoBalanceStatModifiers(g, h, m, a, d, cc);
            }
            catch (...) {}
        }
    }
}

void AutoBalanceConfig::ParseDynamicLevelOverrides(std::string const& str, std::unordered_map<uint32, AutoBalanceDynamicLevelSettings>& outMap)
{
    outMap.clear();
    if (str.empty())
        return;

    std::vector<std::string> entries = Tokenize(str, ',');
    for (std::string const& entry : entries)
    {
        std::vector<std::string> parts = Tokenize(entry, ':');
        if (parts.size() >= 5)
        {
            try
            {
                uint32 mapId = static_cast<uint32>(std::stoul(parts[0]));
                int higher = std::stoi(parts[1]);
                int lower = std::stoi(parts[2]);
                int ceil = std::stoi(parts[3]);
                int flr = std::stoi(parts[4]);
                outMap[mapId] = AutoBalanceDynamicLevelSettings(higher, lower, ceil, flr);
            }
            catch (...) {}
        }
    }
}

void AutoBalanceConfig::Load()
{
    // Check TwowAutoBalance.Enable or AutoBalance.Enable.Global
    m_enableGlobal = sConfig.GetBoolDefault("TwowAutoBalance.Enable", false);
    if (!m_enableGlobal)
        m_enableGlobal = sConfig.GetBoolDefault("AutoBalance.Enable.Global", false);

    m_enable5M = sConfig.GetBoolDefault("AutoBalance.Enable.5M", true);
    m_enable10M = sConfig.GetBoolDefault("AutoBalance.Enable.10M", true);
    m_enable15M = sConfig.GetBoolDefault("AutoBalance.Enable.15M", true);
    m_enable20M = sConfig.GetBoolDefault("AutoBalance.Enable.20M", true);
    m_enable25M = sConfig.GetBoolDefault("AutoBalance.Enable.25M", true);
    m_enable40M = sConfig.GetBoolDefault("AutoBalance.Enable.40M", true);
    m_enableOtherNormal = sConfig.GetBoolDefault("AutoBalance.Enable.OtherNormal", true);

    ParseIdList(sConfig.GetStringDefault("AutoBalance.Disable.PerInstance", ""), m_disabledDungeonIds);

    m_minPlayersNormal = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.MinPlayers", 1), 1);
    m_minPlayersRaid = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.MinPlayers.Raid", 1), 1);
    ParseMinPlayersList(sConfig.GetStringDefault("AutoBalance.MinPlayers.PerInstance", ""), m_minPlayersPerDungeon);

    // Inflection points
    m_defaultInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPoint", 0.5f),
        sConfig.GetFloatDefault("AutoBalance.InflectionPoint.CurveFloor", 0.0f),
        sConfig.GetFloatDefault("AutoBalance.InflectionPoint.CurveCeiling", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.InflectionPoint.BossModifier", 1.0f)
    );

    m_raidInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid", m_defaultInflection.value),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid.CurveFloor", m_defaultInflection.curveFloor),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid.CurveCeiling", m_defaultInflection.curveCeiling),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid.BossModifier", m_defaultInflection.bossModifier)
    );

    m_raid10MInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid10M", m_raidInflection.value),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid10M.CurveFloor", m_raidInflection.curveFloor),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid10M.CurveCeiling", m_raidInflection.curveCeiling),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid10M.BossModifier", m_raidInflection.bossModifier)
    );

    m_raid15MInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid15M", m_raidInflection.value),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid15M.CurveFloor", m_raidInflection.curveFloor),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid15M.CurveCeiling", m_raidInflection.curveCeiling),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid15M.BossModifier", m_raidInflection.bossModifier)
    );

    m_raid20MInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid20M", m_raidInflection.value),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid20M.CurveFloor", m_raidInflection.curveFloor),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid20M.CurveCeiling", m_raidInflection.curveCeiling),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid20M.BossModifier", m_raidInflection.bossModifier)
    );

    m_raid25MInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid25M", m_raidInflection.value),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid25M.CurveFloor", m_raidInflection.curveFloor),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid25M.CurveCeiling", m_raidInflection.curveCeiling),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid25M.BossModifier", m_raidInflection.bossModifier)
    );

    m_raid40MInflection = AutoBalanceInflectionPointSettings(
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid40M", m_raidInflection.value),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid40M.CurveFloor", m_raidInflection.curveFloor),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid40M.CurveCeiling", m_raidInflection.curveCeiling),
        sConfig.GetFloatDefault("AutoBalance.InflectionPointRaid40M.BossModifier", m_raidInflection.bossModifier)
    );

    ParseInflectionOverrides(sConfig.GetStringDefault("AutoBalance.InflectionPoint.PerInstance", ""), m_mapInflectionOverrides);
    ParseInflectionOverrides(sConfig.GetStringDefault("AutoBalance.InflectionPoint.Boss.PerInstance", ""), m_bossMapInflectionOverrides);

    // Stat Modifiers
    m_normalStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Global", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Health", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Mana", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Armor", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Damage", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.CCDuration", 1.0f)
    );

    m_bossStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Boss.Global", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Boss.Health", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Boss.Mana", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Boss.Armor", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Boss.Damage", 1.0f),
        sConfig.GetFloatDefault("AutoBalance.StatModifier.Boss.CCDuration", 1.0f)
    );

    m_raidStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Global", m_normalStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Health", m_normalStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Mana", m_normalStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Armor", m_normalStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Damage", m_normalStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.CCDuration", m_normalStatModifiers.ccduration)
    );

    m_raid10MStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Global", m_raidStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Health", m_raidStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Mana", m_raidStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Armor", m_raidStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Damage", m_raidStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.CCDuration", m_raidStatModifiers.ccduration)
    );

    m_raid20MStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Global", m_raidStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Health", m_raidStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Mana", m_raidStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Armor", m_raidStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Damage", m_raidStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.CCDuration", m_raidStatModifiers.ccduration)
    );

    m_raid40MStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Global", m_raidStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Health", m_raidStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Mana", m_raidStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Armor", m_raidStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Damage", m_raidStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.CCDuration", m_raidStatModifiers.ccduration)
    );

    m_raidBossStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Boss.Global", m_bossStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Boss.Health", m_bossStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Boss.Mana", m_bossStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Boss.Armor", m_bossStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Boss.Damage", m_bossStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid.Boss.CCDuration", m_bossStatModifiers.ccduration)
    );

    m_raid10MBossStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Boss.Global", m_raidBossStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Boss.Health", m_raidBossStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Boss.Mana", m_raidBossStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Boss.Armor", m_raidBossStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Boss.Damage", m_raidBossStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid10M.Boss.CCDuration", m_raidBossStatModifiers.ccduration)
    );

    m_raid20MBossStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Boss.Global", m_raidBossStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Boss.Health", m_raidBossStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Boss.Mana", m_raidBossStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Boss.Armor", m_raidBossStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Boss.Damage", m_raidBossStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid20M.Boss.CCDuration", m_raidBossStatModifiers.ccduration)
    );

    m_raid40MBossStatModifiers = AutoBalanceStatModifiers(
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Boss.Global", m_raidBossStatModifiers.global),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Boss.Health", m_raidBossStatModifiers.health),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Boss.Mana", m_raidBossStatModifiers.mana),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Boss.Armor", m_raidBossStatModifiers.armor),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Boss.Damage", m_raidBossStatModifiers.damage),
        sConfig.GetFloatDefault("AutoBalance.StatModifierRaid40M.Boss.CCDuration", m_raidBossStatModifiers.ccduration)
    );

    ParseStatModifierOverrides(sConfig.GetStringDefault("AutoBalance.StatModifier.PerInstance", ""), m_mapStatModifierOverrides);
    ParseStatModifierOverrides(sConfig.GetStringDefault("AutoBalance.StatModifier.Boss.PerInstance", ""), m_mapBossStatModifierOverrides);
    ParseStatModifierOverrides(sConfig.GetStringDefault("AutoBalance.StatModifier.PerCreature", ""), m_creatureStatModifierOverrides);

    // Multiplier Clamp Bounds
    m_minHPModifier = sConfig.GetFloatDefault("AutoBalance.MinHPModifier", 0.01f);
    m_minManaModifier = sConfig.GetFloatDefault("AutoBalance.MinManaModifier", 0.01f);
    m_minDamageModifier = sConfig.GetFloatDefault("AutoBalance.MinDamageModifier", 0.01f);
    m_minCCDurationModifier = sConfig.GetFloatDefault("AutoBalance.MinCCDurationModifier", 0.25f);
    m_maxCCDurationModifier = sConfig.GetFloatDefault("AutoBalance.MaxCCDurationModifier", 1.0f);

    m_playerCountDifficultyOffset = ClampConfigInt8(sConfig.GetIntDefault("AutoBalance.playerCountDifficultyOffset", 0));

    // Forced ID & Disabled Creatures
    m_forcedCreatureCounts.clear();
    ParseForcedIdList(sConfig.GetStringDefault("AutoBalance.ForcedID40", ""), 40, m_forcedCreatureCounts);
    ParseForcedIdList(sConfig.GetStringDefault("AutoBalance.ForcedID20", ""), 20, m_forcedCreatureCounts);
    ParseForcedIdList(sConfig.GetStringDefault("AutoBalance.ForcedID10", ""), 10, m_forcedCreatureCounts);
    ParseForcedIdList(sConfig.GetStringDefault("AutoBalance.ForcedID5", ""), 5, m_forcedCreatureCounts);
    ParseForcedIdList(sConfig.GetStringDefault("AutoBalance.ForcedID2", ""), 2, m_forcedCreatureCounts);

    ParseIdList(sConfig.GetStringDefault("AutoBalance.DisabledID", ""), m_disabledCreatureIds);

    // Level Scaling
    m_levelScaling = sConfig.GetBoolDefault("AutoBalance.LevelScaling", false);
    std::string lMethod = sConfig.GetStringDefault("AutoBalance.LevelScaling.Method", "dynamic");
    std::transform(lMethod.begin(), lMethod.end(), lMethod.begin(), [](unsigned char c){ return std::tolower(c); });
    m_levelScalingMethod = (lMethod == "fixed") ? AUTOBALANCE_SCALING_FIXED : AUTOBALANCE_SCALING_DYNAMIC;

    m_levelScalingSkipHigherLevels = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.LevelScaling.SkipHigherLevels", 3));
    m_levelScalingSkipLowerLevels = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.LevelScaling.SkipLowerLevels", 5));
    m_dynamicCeilingDungeons = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.LevelScaling.DynamicLevel.Ceiling.Dungeons", 1));
    m_dynamicFloorDungeons = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.LevelScaling.DynamicLevel.Floor.Dungeons", 5));
    m_dynamicCeilingRaids = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.LevelScaling.DynamicLevel.Ceiling.Raids", 3));
    m_dynamicFloorRaids = ClampConfigUint8(sConfig.GetIntDefault("AutoBalance.LevelScaling.DynamicLevel.Floor.Raids", 5));
    ParseDynamicLevelOverrides(sConfig.GetStringDefault("AutoBalance.LevelScaling.DynamicLevel.PerInstance", ""), m_dynamicLevelOverrides);

    // Reward Scaling
    std::string rMethod = sConfig.GetStringDefault("AutoBalance.RewardScaling.Method", "dynamic");
    std::transform(rMethod.begin(), rMethod.end(), rMethod.begin(), [](unsigned char c){ return std::tolower(c); });
    m_rewardScalingMethod = (rMethod == "fixed") ? AUTOBALANCE_SCALING_FIXED : AUTOBALANCE_SCALING_DYNAMIC;

    m_rewardScalingXP = sConfig.GetBoolDefault("AutoBalance.RewardScaling.XP", true);
    m_rewardScalingXPModifier = std::max(0.0f, sConfig.GetFloatDefault("AutoBalance.RewardScaling.XP.Modifier", 1.0f));
    m_rewardScalingMoney = sConfig.GetBoolDefault("AutoBalance.RewardScaling.Money", true);
    m_rewardScalingMoneyModifier = std::max(0.0f, sConfig.GetFloatDefault("AutoBalance.RewardScaling.Money.Modifier", 1.0f));

    // Notifications & Logging
    m_playerChangeNotify = sConfig.GetBoolDefault("AutoBalance.PlayerChangeNotify", true);
    m_announcement = sConfig.GetBoolDefault("AutoBalanceAnnounce.enable", true);
    m_debugLogging = sConfig.GetBoolDefault("AutoBalance.DebugLog", false);

    // Coexistence
    m_coexistenceDisableCoreAutoScaler = sConfig.GetBoolDefault("AutoBalance.Coexistence.DisableCoreAutoScaler", true);

    ++m_configTimestamp;
    sLog.outString("[AutoBalance] Configuration loaded (Enabled: %s).", m_enableGlobal ? "true" : "false");
}

void AutoBalanceConfig::Reload()
{
    Load();
}

bool AutoBalanceConfig::IsDungeonDisabled(uint32 mapId) const
{
    return m_disabledDungeonIds.find(mapId) != m_disabledDungeonIds.end();
}

uint8 AutoBalanceConfig::GetMinPlayersForMap(uint32 mapId, bool isRaid) const
{
    auto it = m_minPlayersPerDungeon.find(mapId);
    if (it != m_minPlayersPerDungeon.end())
        return it->second;
    return isRaid ? m_minPlayersRaid : m_minPlayersNormal;
}

AutoBalanceInflectionPointSettings const& AutoBalanceConfig::GetRaidInflectionSettings(uint32 maxPlayers) const
{
    if (maxPlayers <= 10)
        return m_raid10MInflection;
    if (maxPlayers <= 15)
        return m_raid15MInflection;
    if (maxPlayers <= 20)
        return m_raid20MInflection;
    if (maxPlayers <= 25)
        return m_raid25MInflection;
    return m_raid40MInflection;
}

AutoBalanceInflectionPointSettings const* AutoBalanceConfig::GetMapInflectionOverride(uint32 mapId, bool isBoss) const
{
    if (isBoss)
    {
        auto itBoss = m_bossMapInflectionOverrides.find(mapId);
        if (itBoss != m_bossMapInflectionOverrides.end())
            return &itBoss->second;
    }

    auto it = m_mapInflectionOverrides.find(mapId);
    if (it != m_mapInflectionOverrides.end())
        return &it->second;

    return nullptr;
}

AutoBalanceStatModifiers const& AutoBalanceConfig::GetRaidStatModifiers(uint32 maxPlayers, bool isBoss) const
{
    if (isBoss)
    {
        if (maxPlayers <= 10)
            return m_raid10MBossStatModifiers;
        if (maxPlayers <= 20)
            return m_raid20MBossStatModifiers;
        return m_raid40MBossStatModifiers;
    }
    else
    {
        if (maxPlayers <= 10)
            return m_raid10MStatModifiers;
        if (maxPlayers <= 20)
            return m_raid20MStatModifiers;
        return m_raid40MStatModifiers;
    }
}

AutoBalanceStatModifiers const* AutoBalanceConfig::GetMapStatModifierOverride(uint32 mapId, bool isBoss) const
{
    if (isBoss)
    {
        auto itBoss = m_mapBossStatModifierOverrides.find(mapId);
        if (itBoss != m_mapBossStatModifierOverrides.end())
            return &itBoss->second;
    }

    auto it = m_mapStatModifierOverrides.find(mapId);
    if (it != m_mapStatModifierOverrides.end())
        return &it->second;

    return nullptr;
}

AutoBalanceStatModifiers const* AutoBalanceConfig::GetCreatureStatModifierOverride(uint32 entry) const
{
    auto it = m_creatureStatModifierOverrides.find(entry);
    if (it != m_creatureStatModifierOverrides.end())
        return &it->second;
    return nullptr;
}

int AutoBalanceConfig::GetForcedPlayerCount(uint32 entry) const
{
    auto it = m_forcedCreatureCounts.find(entry);
    if (it != m_forcedCreatureCounts.end())
        return it->second;
    return 0;
}

bool AutoBalanceConfig::IsCreatureDisabled(uint32 entry) const
{
    return m_disabledCreatureIds.find(entry) != m_disabledCreatureIds.end();
}

AutoBalanceDynamicLevelSettings const* AutoBalanceConfig::GetDynamicLevelOverride(uint32 mapId) const
{
    auto it = m_dynamicLevelOverrides.find(mapId);
    if (it != m_dynamicLevelOverrides.end())
        return &it->second;
    return nullptr;
}
