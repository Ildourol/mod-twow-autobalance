/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#include "AutoBalanceScripts.h"
#include "AutoBalanceCommands.h"

void Addmod_twow_autobalanceScripts()
{
    new AutoBalanceWorldScript();
    new AutoBalancePlayerScript();
    new AutoBalanceUnitScript();
    new AutoBalanceAllMapScript();
    new AutoBalanceCommandScript();
}
