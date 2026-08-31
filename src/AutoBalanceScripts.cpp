/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1)
 */

#include "AutoBalanceScripts.h"
#include "AutoBalanceManager.h"
#include "AutoBalanceConfig.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include "Maps/Map.h"
#include "Spells/SpellAuras.h"
#include "Chat/Chat.h"
#include "Log.h"

// --- AutoBalanceWorldScript --------------------------------------------------

AutoBalanceWorldScript::AutoBalanceWorldScript()
    : WorldScript("AutoBalanceWorldScript")
{
}

void AutoBalanceWorldScript::OnStartup()
{
    sAutoBalanceMgr.Initialize();
}

void AutoBalanceWorldScript::OnAfterConfigLoad(bool reload)
{
    if (reload)
        sAutoBalanceMgr.Reload();
}

// --- AutoBalancePlayerScript -------------------------------------------------

AutoBalancePlayerScript::AutoBalancePlayerScript()
    : PlayerScript("AutoBalancePlayerScript")
{
}

void AutoBalancePlayerScript::OnLogin(Player* player)
{
    if (!player || !sABConfig.IsAnnouncementEnabled())
        return;

    if (sABConfig.IsGlobalEnabled())
    {
        ChatHandler(player).SendSysMessage("|cff00ccff[AutoBalance]|r This server is running mod-twow-autobalance.");
    }
}

void AutoBalancePlayerScript::OnLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!player || !player->IsInWorld())
        return;

    Map* map = player->GetMap();
    if (map && map->IsDungeon())
        sAutoBalanceMgr.OnPlayerLevelChanged(map, player);
}

void AutoBalancePlayerScript::OnGiveXP(Player* player, uint32& amount, Unit* victim)
{
    if (!player || !victim || amount == 0)
        return;

    if (!victim->IsCreature())
        return;

    Creature* creature = static_cast<Creature*>(victim);
    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature->GetObjectGuid());
    if (cInfo)
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        if (cInfo->isActive && cInfo->xpModifier > 0.0f)
            amount = static_cast<uint32>(std::round(static_cast<float>(amount) * cInfo->xpModifier));
    }
}

// --- AutoBalanceUnitScript ---------------------------------------------------

AutoBalanceUnitScript::AutoBalanceUnitScript()
    : UnitScript("AutoBalanceUnitScript")
{
}

void AutoBalanceUnitScript::ModifyMeleeDamage(Unit* /*target*/, Unit* /*attacker*/, uint32& /*damage*/)
{
    // Physical weapon damage is already scaled directly via SetBaseWeaponDamage()
    // and UpdateDamagePhysical() in ApplyCreatureStats() (identical to core AutoScaler).
    // Multiplying here would result in double (squared) damage reduction.
}

void AutoBalanceUnitScript::ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellEntry const* /*spellInfo*/)
{
    if (!target || !attacker || damage <= 0)
        return;

    if (!attacker->IsCreature())
        return;

    if (!target->IsPlayer() && !(target->IsPet() && target->GetOwner() && target->GetOwner()->IsPlayer()))
        return;

    Creature* creature = static_cast<Creature*>(attacker);
    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature->GetObjectGuid());
    if (cInfo)
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        if (cInfo->isActive && cInfo->scaledDamageMultiplier != 1.0f)
            damage = std::max(1, static_cast<int32>(std::round(static_cast<float>(damage) * cInfo->scaledDamageMultiplier)));
    }
}

void AutoBalanceUnitScript::ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellEntry const* /*spellInfo*/)
{
    if (!target || !healer || heal == 0)
        return;

    // Non-player dungeon source healing dungeon creatures
    if (!target->IsCreature() || healer->IsPlayer())
        return;

    Creature* creature = static_cast<Creature*>(target);
    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature->GetObjectGuid());
    if (cInfo)
    {
        std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
        if (cInfo->isActive && cInfo->scaledHealthMultiplier != 1.0f)
            heal = std::max(1u, static_cast<uint32>(std::round(static_cast<float>(heal) * cInfo->scaledHealthMultiplier)));
    }
}

void AutoBalanceUnitScript::OnAuraApply(Unit* unit, Aura* aura)
{
    if (!unit || !aura)
        return;

    // CC scaling on player from dungeon creature
    if (!unit->IsPlayer())
        return;

    SpellAuraHolder* holder = aura->GetHolder();
    if (!holder)
        return;

    Unit* caster = holder->GetCaster();
    if (!caster || !caster->IsCreature())
        return;

    Creature* creature = static_cast<Creature*>(caster);
    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature->GetObjectGuid());
    if (!cInfo)
        return;

    std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
    if (!cInfo->isActive || cInfo->ccDurationMultiplier == 1.0f)
        return;

    // Check for crowd control mechanics
    bool isCC = holder->HasMechanic(MECHANIC_CHARM) ||
                holder->HasMechanic(MECHANIC_DISORIENTED) ||
                holder->HasMechanic(MECHANIC_FEAR) ||
                holder->HasMechanic(MECHANIC_ROOT) ||
                holder->HasMechanic(MECHANIC_SILENCE) ||
                holder->HasMechanic(MECHANIC_SLEEP) ||
                holder->HasMechanic(MECHANIC_STUN) ||
                holder->HasMechanic(MECHANIC_FREEZE) ||
                holder->HasMechanic(MECHANIC_KNOCKOUT) ||
                holder->HasMechanic(MECHANIC_POLYMORPH) ||
                holder->HasMechanic(MECHANIC_BANISH) ||
                holder->HasMechanic(MECHANIC_SHACKLE) ||
                holder->HasMechanic(MECHANIC_HORROR);

    if (isCC && !holder->IsPermanent())
    {
        int32 curDuration = holder->GetAuraDuration();
        int32 maxDuration = holder->GetAuraMaxDuration();
        if (curDuration > 0)
        {
            int32 newCur = static_cast<int32>(std::round(static_cast<float>(curDuration) * cInfo->ccDurationMultiplier));
            int32 newMax = static_cast<int32>(std::round(static_cast<float>(maxDuration) * cInfo->ccDurationMultiplier));
            holder->SetAuraDuration(std::max(1000, newCur));
            holder->SetAuraMaxDuration(std::max(1000, newMax));
        }
    }
}

void AutoBalanceUnitScript::OnUnitEnterCombat(Unit* unit, Unit* victim)
{
    sAutoBalanceMgr.OnUnitEnterCombat(unit, victim);
}

void AutoBalanceUnitScript::OnUnitExitCombat(Unit* unit)
{
    sAutoBalanceMgr.OnUnitExitCombat(unit);
}

// --- AutoBalanceAllMapScript -------------------------------------------------

AutoBalanceAllMapScript::AutoBalanceAllMapScript()
    : AllMapScript("AutoBalanceAllMapScript")
{
}

void AutoBalanceAllMapScript::OnPlayerEnterAll(Map* map, Player* player)
{
    sAutoBalanceMgr.OnPlayerEnter(map, player);
}

void AutoBalanceAllMapScript::OnPlayerLeaveAll(Map* map, Player* player)
{
    sAutoBalanceMgr.OnPlayerLeave(map, player);
}

void AutoBalanceAllMapScript::OnDestroyMap(Map* map)
{
    sAutoBalanceMgr.OnMapDestroy(map);
}

void AutoBalanceAllMapScript::OnMapUpdate(Map* map, uint32 /*diff*/)
{
    sAutoBalanceMgr.OnMapUpdate(map);
}

// --- AutoBalanceAllCreatureScript --------------------------------------------

AutoBalanceAllCreatureScript::AutoBalanceAllCreatureScript()
    : AllCreatureScript("AutoBalanceAllCreatureScript")
{
}

void AutoBalanceAllCreatureScript::OnCreatureAddWorld(Creature* creature)
{
    sAutoBalanceMgr.OnCreatureAdd(creature);
}

void AutoBalanceAllCreatureScript::OnCreatureRemoveWorld(Creature* creature)
{
    sAutoBalanceMgr.OnCreatureRemove(creature);
}

void AutoBalanceAllCreatureScript::OnCreatureRespawnWorld(Creature* creature)
{
    sAutoBalanceMgr.OnCreatureRespawn(creature);
}

void AutoBalanceAllCreatureScript::OnBeforeCreatureGenerateMoneyLoot(Creature* creature, uint32& minimum, uint32& maximum)
{
    if (!creature || !creature->GetMap() || !creature->GetMap()->IsDungeon())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature->GetObjectGuid());
    if (!cInfo)
        return;

    std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
    if (!cInfo->isActive)
        return;

    minimum = static_cast<uint32>(std::round(static_cast<float>(minimum) * cInfo->moneyModifier));
    maximum = static_cast<uint32>(std::round(static_cast<float>(maximum) * cInfo->moneyModifier));
}
