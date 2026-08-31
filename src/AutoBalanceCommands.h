/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#ifndef MOD_TWOW_AUTOBALANCE_COMMANDS_H
#define MOD_TWOW_AUTOBALANCE_COMMANDS_H

#include "ScriptObjects.h"

class AutoBalanceCommandScript : public AllCommandScript
{
public:
    AutoBalanceCommandScript();
    bool CanExecuteCommand(ChatHandler* handler, char const* command, char const* args) override;

private:
    static bool HandleMapStat(ChatHandler* handler);
    static bool HandleCreatureStat(ChatHandler* handler);
    static bool HandleGetOffset(ChatHandler* handler);
    static bool HandleSetOffset(ChatHandler* handler, std::string const& args);
    static bool HandleReload(ChatHandler* handler);
    static bool HandleInfo(ChatHandler* handler);
};

#endif // MOD_TWOW_AUTOBALANCE_COMMANDS_H
