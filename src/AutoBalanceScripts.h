/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#ifndef MOD_TWOW_AUTOBALANCE_SCRIPTS_H
#define MOD_TWOW_AUTOBALANCE_SCRIPTS_H

#include "ScriptObjects.h"

class AutoBalanceWorldScript : public WorldScript
{
public:
    AutoBalanceWorldScript();
    void OnStartup() override;
    void OnAfterConfigLoad(bool reload) override;
};

class AutoBalancePlayerScript : public PlayerScript
{
public:
    AutoBalancePlayerScript();
    void OnLogin(Player* player) override;
    void OnLevelChanged(Player* player, uint8 oldLevel) override;
    void OnGiveXP(Player* player, uint32& amount, Unit* victim) override;
};

class AutoBalanceUnitScript : public UnitScript
{
public:
    AutoBalanceUnitScript();
    void ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage) override;
    void ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellEntry const* spellInfo) override;
    void ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellEntry const* spellInfo) override;
    void OnAuraApply(Unit* unit, Aura* aura) override;
    void OnUnitEnterCombat(Unit* unit, Unit* victim) override;
    void OnUnitExitCombat(Unit* unit) override;
};

class AutoBalanceAllMapScript : public AllMapScript
{
public:
    AutoBalanceAllMapScript();
    void OnPlayerEnterAll(Map* map, Player* player) override;
    void OnPlayerLeaveAll(Map* map, Player* player) override;
    void OnDestroyMap(Map* map) override;
    void OnMapUpdate(Map* map, uint32 diff) override;
};

class AutoBalanceAllCreatureScript : public AllCreatureScript
{
public:
    AutoBalanceAllCreatureScript();
    void OnCreatureAddWorld(Creature* creature) override;
    void OnCreatureRemoveWorld(Creature* creature) override;
    void OnCreatureRespawnWorld(Creature* creature) override;
    void OnBeforeCreatureGenerateMoneyLoot(Creature* creature, uint32& minimum, uint32& maximum) override;
};

#endif // MOD_TWOW_AUTOBALANCE_SCRIPTS_H
