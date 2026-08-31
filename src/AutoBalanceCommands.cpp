/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#include "AutoBalanceCommands.h"
#include "AutoBalanceManager.h"
#include "AutoBalanceConfig.h"
#include "AutoBalanceScaling.h"
#include "Chat/Chat.h"
#include "Maps/Map.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include "WorldSession.h"
#include "World.h"
#include "StringFormat.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <sstream>

AutoBalanceCommandScript::AutoBalanceCommandScript()
    : AllCommandScript("AutoBalanceCommandScript")
{
}

bool AutoBalanceCommandScript::CanExecuteCommand(ChatHandler* handler, char const* cmd, char const* argsIn)
{
    if (!cmd)
        return true;

    if (strcmp(cmd, "ab") != 0 && strcmp(cmd, "autobalance") != 0)
        return true;

    std::string rest = argsIn ? argsIn : "";
    auto const takeWord = [&rest]() -> std::string
    {
        size_t const b = rest.find_first_not_of(' ');
        if (b == std::string::npos) { rest.clear(); return std::string(); }
        size_t const e = rest.find(' ', b);
        std::string word = rest.substr(b, e == std::string::npos ? std::string::npos : e - b);
        rest = e == std::string::npos ? std::string() : rest.substr(e + 1);
        return word;
    };

    std::string const sub = takeWord();

    if (sub == "mapstat" || sub == "map")
        HandleMapStat(handler);
    else if (sub == "creaturestat" || sub == "creature")
        HandleCreatureStat(handler);
    else if (sub == "getoffset")
        HandleGetOffset(handler);
    else if (sub == "setoffset")
        HandleSetOffset(handler, rest);
    else if (sub == "reload")
        HandleReload(handler);
    else if (sub == "info" || sub == "status")
        HandleInfo(handler);
    else
    {
        handler->SendSysMessage(".ab mapstat | creaturestat | getoffset | setoffset <N> | reload | info");
    }

    return false; // Handled / claimed
}

bool AutoBalanceCommandScript::HandleMapStat(ChatHandler* handler)
{
    Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!player)
    {
        handler->SendSysMessage("This command must be used in-game.");
        return true;
    }

    Map* map = player->GetMap();
    if (!map || !map->IsDungeon())
    {
        handler->SendSysMessage("You are not currently in a dungeon or raid instance.");
        return true;
    }

    AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
    uint32 maxPlayers = AutoBalanceScaling::GetMapMaxPlayers(map);

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("=== AutoBalance Map Stats: {} (Map ID: {}, Instance: {}) ===",
        map->GetMapName(), map->GetId(), map->GetInstanceId()).c_str());

    if (!mapInfo)
    {
        handler->SendSysMessage("AutoBalance is DISABLED for this map.");
        return true;
    }

    std::lock_guard<std::recursive_mutex> mapGuard(mapInfo->stateLock);
    if (!mapInfo->enabled)
    {
        handler->SendSysMessage("AutoBalance is DISABLED for this map.");
        return true;
    }

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Status: ENABLED | Combat Locked: {}",
        mapInfo->combatLocked ? "YES" : "NO").c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Players: {}/{} (Min: {}, Adjusted/Effective: {})",
        mapInfo->playerCount, maxPlayers, mapInfo->minPlayers, mapInfo->adjustedPlayerCount).c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Player Levels: [{} - {}] | Creature Levels: [{} - {}]",
        mapInfo->lowestPlayerLevel, mapInfo->highestPlayerLevel,
        mapInfo->lowestCreatureLevel, mapInfo->highestCreatureLevel).c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Level Scaling: {} | Tracked Creatures: {}",
        mapInfo->isLevelScalingEnabled ? "ON" : "OFF", mapInfo->creatures.size()).c_str());

    return true;
}

bool AutoBalanceCommandScript::HandleCreatureStat(ChatHandler* handler)
{
    Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!player)
    {
        handler->SendSysMessage("This command must be used in-game.");
        return true;
    }

    Creature* target = player->GetMap() ? player->GetMap()->GetAnyTypeCreature(player->GetSelectionGuid()) : nullptr;
    if (!target)
    {
        handler->SendSysMessage("You must select a creature first.");
        return true;
    }

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(target->GetObjectGuid());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("=== AutoBalance Creature Stats: {} (Entry: {}, GUID: {}) ===",
        target->GetName(), target->GetEntry(), target->GetGUIDLow()).c_str());

    if (!cInfo)
    {
        handler->SendSysMessage("This creature is not currently managed/scaled by AutoBalance.");
        return true;
    }

    std::lock_guard<std::recursive_mutex> creatureGuard(cInfo->stateLock);
    if (!cInfo->isActive)
    {
        handler->SendSysMessage("This creature is not currently managed/scaled by AutoBalance.");
        return true;
    }

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Boss: {} | Level: {} (Base: {})",
        cInfo->isBoss ? "YES" : "NO", target->GetLevel(), cInfo->unmodifiedLevel).c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Health: {}/{} (Base: {}, Mult: {:.3f}, Scaled: {:.3f})",
        target->GetHealth(), target->GetMaxHealth(), cInfo->baseData.baseHealth,
        cInfo->healthMultiplier, cInfo->scaledHealthMultiplier).c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Mana: {}/{} (Base: {}, Mult: {:.3f}, Scaled: {:.3f})",
        target->GetPower(POWER_MANA), target->GetMaxPower(POWER_MANA), cInfo->baseData.baseMana,
        cInfo->manaMultiplier, cInfo->scaledManaMultiplier).c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Damage Mult: {:.3f} (Scaled: {:.3f}) | Armor Mult: {:.3f} (Scaled: {:.3f})",
        cInfo->damageMultiplier, cInfo->scaledDamageMultiplier,
        cInfo->armorMultiplier, cInfo->scaledArmorMultiplier).c_str());

    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("CC Duration Mult: {:.3f} | XP Mod: {:.3f} | Money Mod: {:.3f}",
        cInfo->ccDurationMultiplier, cInfo->xpModifier, cInfo->moneyModifier).c_str());

    return true;
}

bool AutoBalanceCommandScript::HandleGetOffset(ChatHandler* handler)
{
    int8 offset = sABConfig.GetPlayerCountDifficultyOffset();
    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("[AutoBalance] Current PlayerCountDifficultyOffset: {}", static_cast<int>(offset)).c_str());
    return true;
}

bool AutoBalanceCommandScript::HandleSetOffset(ChatHandler* handler, std::string const& args)
{
    if (handler->GetSession() && handler->GetSession()->GetSecurity() < SEC_GAMEMASTER)
    {
        handler->SendSysMessage("You do not have permission to execute this command.");
        return true;
    }

    if (args.empty())
    {
        handler->SendSysMessage("Usage: .ab setoffset <-N .. +N>");
        return true;
    }

    errno = 0;
    char* end = nullptr;
    long const parsedOffset = std::strtol(args.c_str(), &end, 10);
    while (end && *end == ' ')
        ++end;
    if (errno != 0 || end == args.c_str() || (end && *end != '\0'))
    {
        handler->SendSysMessage("Invalid offset. Enter a whole number from -128 to 127.");
        return true;
    }

    int offset = static_cast<int>(std::max(-128L, std::min(127L, parsedOffset)));
    sABConfig.SetPlayerCountDifficultyOffset(static_cast<int8>(offset));
    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("[AutoBalance] PlayerCountDifficultyOffset set to {}.", offset).c_str());

    // Rescale current map if in dungeon
    if (Player* player = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr)
    {
        Map* map = player->GetMap();
        if (map && map->IsDungeon())
        {
            AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
            if (mapInfo)
            {
                bool enabled = false;
                {
                    std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
                    enabled = mapInfo->enabled;
                }
                if (enabled)
                {
                    sAutoBalanceMgr.UpdateMapPlayerCounts(map, *mapInfo);
                    sAutoBalanceMgr.RescaleMapCreatures(map);
                    handler->SendSysMessage("[AutoBalance] Current instance creatures rescaled.");
                }
            }
        }
    }

    return true;
}

bool AutoBalanceCommandScript::HandleReload(ChatHandler* handler)
{
    if (handler->GetSession() && handler->GetSession()->GetSecurity() < SEC_ADMINISTRATOR)
    {
        handler->SendSysMessage("You do not have permission to execute this command.");
        return true;
    }

    sWorld.LoadConfigSettings(true);
    handler->SendSysMessage("[AutoBalance] Configuration reloaded successfully.");
    return true;
}

bool AutoBalanceCommandScript::HandleInfo(ChatHandler* handler)
{
    handler->SendSysMessage("=== mod-twow-autobalance for TortoiseWoW / Turtle WoW ===");
    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Global State: {}", sABConfig.IsGlobalEnabled() ? "ENABLED" : "DISABLED").c_str());
    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Level Scaling: {} | Reward Scaling (XP/Money): {}/{}",
        sABConfig.IsLevelScalingEnabled() ? "ON" : "OFF",
        sABConfig.IsRewardScalingXPEnabled() ? "ON" : "OFF",
        sABConfig.IsRewardScalingMoneyEnabled() ? "ON" : "OFF").c_str());
    handler->SendSysMessage(AutoBalanceFormatting::StringFormat("Core AutoScaler Coexistence Protection: {}",
        sABConfig.IsCoexistenceDisableCoreAutoScaler() ? "ACTIVE" : "OFF").c_str());
    return true;
}
