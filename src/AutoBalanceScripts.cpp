/*
 * Copyright (C) 2018 AzerothCore <http://www.azerothcore.org>
 * Copyright (C) 2012 CVMagic <http://www.trinitycore.org/f/topic/6551-vas-autobalance/>
 * Adapted for TortoiseWoW / Turtle WoW (Vanilla 1.12.1 / client 1.18.1.7272)
 */

#include "AutoBalanceScripts.h"
#include "AutoBalanceManager.h"
#include "AutoBalanceConfig.h"
#include "AutoBalanceScaling.h"
#include "Objects/Player.h"
#include "Objects/Creature.h"
#include "Maps/Map.h"
#include "Spells/SpellAuras.h"
#include "Chat/Chat.h"
#include "Log.h"
#include <cmath>

// --- AutoBalanceWorldScript --------------------------------------------------

AutoBalanceWorldScript::AutoBalanceWorldScript()
    : WorldScript("AutoBalanceWorldScript", {
        WORLDHOOK_ON_STARTUP,
        WORLDHOOK_ON_AFTER_CONFIG_LOAD
    })
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
    : PlayerScript("AutoBalancePlayerScript", {
        PLAYERHOOK_ON_LOGIN,
        PLAYERHOOK_ON_LEVEL_CHANGED,
        PLAYERHOOK_ON_GIVE_EXP,
        PLAYERHOOK_ON_MONEY_CHANGED,
        PLAYERHOOK_ON_CREATURE_KILL
    })
{
}

void AutoBalancePlayerScript::OnLogin(Player* player)
{
    if (!player || !sABConfig.IsAnnouncementEnabled())
        return;

    if (player->GetSession() && player->GetSession()->IsHeadless())
        return;

    if (sABConfig.IsGlobalEnabled())
    {
        ChatHandler(player).SendSysMessage("This server is running the |cff4CFF00AutoBalance |rmodule.");
    }
}

void AutoBalancePlayerScript::OnLevelChanged(Player* player, uint8 /*oldLevel*/)
{
    if (!player || !player->IsInWorld())
        return;

    Map* map = player->GetMap();
    if (map && map->IsDungeon())
        sAutoBalanceMgr.HandlePlayerLevelChanged(map, player);
}

void AutoBalancePlayerScript::OnGiveXP(Player* player, uint32& amount, Unit* victim)
{
    if (!player || !victim || amount == 0 || !sABConfig.IsRewardScalingXPEnabled())
        return;

    if (!victim->IsCreature())
        return;

    Creature* creature = static_cast<Creature*>(victim);
    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
    if (!mapInfo || !mapInfo->enabled)
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature);
    if (!cInfo)
        return;

    std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
    if (!cInfo->isActive || cInfo->xpModifier <= 0.0f)
        return;

    if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_DYNAMIC)
    {
        amount = static_cast<uint32>(std::round(static_cast<float>(amount) * cInfo->xpModifier));
    }
    else if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_FIXED)
    {
        uint32 const maxPlayers = AutoBalanceScaling::GetMapMaxPlayers(map);
        if (maxPlayers > 0)
        {
            float const ratio = static_cast<float>(mapInfo->playerCount) / static_cast<float>(maxPlayers);
            amount = static_cast<uint32>(std::round(static_cast<float>(amount) * cInfo->xpModifier * ratio));
        }
    }
}

void AutoBalancePlayerScript::OnMoneyChanged(Player* player, int32& amount)
{
    if (!player || amount <= 0 || !sABConfig.IsRewardScalingMoneyEnabled())
        return;

    Map* map = player->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
    if (!mapInfo)
        return;

    std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
    if (!mapInfo->enabled)
        return;

    ObjectGuid const lootGuid = player->GetLootGuid();
    if (lootGuid.IsCreature())
    {
        uint64 const instanceKey = sAutoBalanceMgr.GetMapKey(map);
        AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(instanceKey, lootGuid);
        float moneyMod = sABConfig.GetRewardScalingMoneyModifier();
        if (cInfo)
        {
            std::lock_guard<std::recursive_mutex> creatureGuard(cInfo->stateLock);
            if (cInfo->isActive)
                moneyMod = cInfo->moneyModifier;
        }

        if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_DYNAMIC)
        {
            amount = static_cast<int32>(std::round(static_cast<float>(amount) * moneyMod));
        }
        else if (sABConfig.GetRewardScalingMethod() == AUTOBALANCE_SCALING_FIXED)
        {
            uint32 const maxPlayers = AutoBalanceScaling::GetMapMaxPlayers(map);
            if (maxPlayers > 0)
            {
                float const ratio = static_cast<float>(mapInfo->playerCount) / static_cast<float>(maxPlayers);
                amount = static_cast<int32>(std::round(static_cast<float>(amount) * moneyMod * ratio));
            }
        }
    }
    else
    {
        // Container / non-creature loot distribution in Fixed mode
        uint32 const maxPlayers = AutoBalanceScaling::GetMapMaxPlayers(map);
        if (maxPlayers > 0)
        {
            float const ratio = static_cast<float>(mapInfo->playerCount) / static_cast<float>(maxPlayers);
            amount = static_cast<int32>(std::round(static_cast<float>(amount) * ratio));
        }
    }
}

void AutoBalancePlayerScript::OnCreatureKill(Player* killer, Creature* killed)
{
    if (!killer || !killed || !sABConfig.IsRewardSystemEnabled())
        return;

    Map* map = killer->GetMap();
    if (!map || !map->IsDungeon())
        return;

    if (!AutoBalanceScaling::IsBoss(killed))
        return;

    AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
    if (!mapInfo)
        return;

    std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
    if (!mapInfo->enabled || !sABConfig.IsLevelScalingEnabled())
        return;

    if (mapInfo->adjustedPlayerCount < sABConfig.GetMinPlayerReward())
        return;

    uint32 const rewardToken = map->IsRaid() ? sABConfig.GetRewardRaidToken() : sABConfig.GetRewardDungeonToken();
    if (!rewardToken)
        return;

    for (const auto& ref : map->GetPlayers())
    {
        Player* player = ref.getSource();
        if (!player || player->IsGameMaster() || player->GetLevel() < 60)
            continue;

        player->AddItem(rewardToken, 1);
    }
}

// --- AutoBalanceUnitScript ---------------------------------------------------

AutoBalanceUnitScript::AutoBalanceUnitScript()
    : UnitScript("AutoBalanceUnitScript", {
        UNITHOOK_MODIFY_MELEE_DAMAGE,
        UNITHOOK_MODIFY_SPELL_DAMAGE_TAKEN,
        UNITHOOK_MODIFY_HEAL_RECEIVED,
        UNITHOOK_ON_AURA_APPLY,
        UNITHOOK_ON_UNIT_ENTER_COMBAT,
        UNITHOOK_ON_UNIT_EXIT_COMBAT,
        UNITHOOK_ON_UNIT_UPDATE,
        UNITHOOK_ON_UNIT_DEATH
    })
{
}

void AutoBalanceUnitScript::ModifyMeleeDamage(Unit* target, Unit* attacker, uint32& damage)
{
    if (!target || !attacker || damage == 0)
        return;

    // Exclude players and player-owned attackers from PvE world-damage adjustment
    if (attacker->IsPlayer() || (attacker->GetOwner() && attacker->GetOwner()->IsPlayer()))
        return;

    if (!target->IsPlayer() && !(target->IsPet() && target->GetOwner() && target->GetOwner()->IsPlayer()))
        return;

    Map* map = target->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
    if (!mapInfo)
        return;

    // Tracked creatures have weapon damage adjusted directly in ApplyCreatureStats
    if (attacker->IsCreature())
    {
        Creature* creature = static_cast<Creature*>(attacker);
        AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature);
        if (cInfo)
        {
            std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
            if (cInfo->isActive)
                return;
        }
    }

    // Untracked attacker (environmental hazard, trap, untracked summon) striking a player or pet
    std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
    if (!mapInfo->enabled)
        return;

    float const mult = mapInfo->scaledWorldDamageMultiplier;
    if (mult != 1.0f)
        damage = std::max<uint32>(1, static_cast<uint32>(std::round(static_cast<float>(damage) * mult)));
}

void AutoBalanceUnitScript::ModifySpellDamageTaken(Unit* target, Unit* attacker, int32& damage, SpellEntry const* spellInfo)
{
    if (!target || !attacker || damage <= 0)
        return;

    // Exclude players and player-owned attackers from PvE damage adjustment
    if (attacker->IsPlayer() || (attacker->GetOwner() && attacker->GetOwner()->IsPlayer()))
        return;

    // Upstream exception checking
    if (spellInfo)
    {
        if (sABConfig.IsSpellToNeverModify(spellInfo->Id))
            return;
        if (sABConfig.IsSpellThatSpendsPlayerHealth(spellInfo->Id))
            return;
    }

    if (!target->IsPlayer() && !(target->IsPet() && target->GetOwner() && target->GetOwner()->IsPlayer()))
        return;

    Map* map = target->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::MapInfoPtr mapInfo = sAutoBalanceMgr.GetMapInfo(map);
    if (!mapInfo)
        return;

    bool isPercentSpell = false;
    if (spellInfo)
    {
        for (uint32 i = 0; i < 3; ++i)
        {
            if (spellInfo->EffectApplyAuraName[i] == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
            {
                isPercentSpell = true;
                break;
            }
        }
    }

    bool hasMultiplier = false;
    float mult = 1.0f;

    if (attacker->IsCreature())
    {
        Creature* creature = static_cast<Creature*>(attacker);
        AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature);
        if (cInfo)
        {
            std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
            if (cInfo->isActive)
            {
                mult = isPercentSpell ? cInfo->damageMultiplier : cInfo->scaledDamageMultiplier;
                hasMultiplier = true;
            }
        }
    }

    // Explicitly distinguish tracked source from untracked source.
    // A tracked multiplier of 1.0 must not select the unrelated world multiplier.
    if (!hasMultiplier)
    {
        std::lock_guard<std::recursive_mutex> guard(mapInfo->stateLock);
        if (mapInfo->enabled)
        {
            mult = isPercentSpell ? mapInfo->worldDamageMultiplier : mapInfo->scaledWorldDamageMultiplier;
            hasMultiplier = true;
        }
    }

    if (hasMultiplier && mult != 1.0f)
        damage = std::max(1, static_cast<int32>(std::round(static_cast<float>(damage) * mult)));
}

void AutoBalanceUnitScript::ModifyHealReceived(Unit* target, Unit* healer, uint32& heal, SpellEntry const* spellInfo)
{
    if (!target || !healer || heal == 0)
        return;

    if (spellInfo && sABConfig.IsSpellToNeverModify(spellInfo->Id))
        return;

    if (!target->IsCreature() || healer->IsPlayer())
        return;

    Creature* creature = static_cast<Creature*>(target);
    Map* map = creature->GetMap();
    if (!map || !map->IsDungeon())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature);
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

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature);
    if (!cInfo)
        return;

    std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
    if (!cInfo->isActive || cInfo->ccDurationMultiplier == 1.0f)
        return;

    bool const isCC = holder->HasMechanic(MECHANIC_CHARM) ||
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
        int32 const curDuration = holder->GetAuraDuration();
        int32 const maxDuration = holder->GetAuraMaxDuration();
        if (curDuration > 0)
        {
            int32 const newCur = static_cast<int32>(std::round(static_cast<float>(curDuration) * cInfo->ccDurationMultiplier));
            int32 const newMax = static_cast<int32>(std::round(static_cast<float>(maxDuration) * cInfo->ccDurationMultiplier));
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

void AutoBalanceUnitScript::OnUnitUpdate(Unit* unit, uint32 /*diff*/)
{
    if (!unit || !unit->IsCreature())
        return;

    Map* map = unit->GetMap();
    if (!map || !map->IsDungeon())
        return;

    Creature* creature = static_cast<Creature*>(unit);
    if (!creature->IsAlive())
        return;

    AutoBalanceManager::CreatureInfoPtr cInfo = sAutoBalanceMgr.GetCreatureInfo(creature);
    if (cInfo)
    {
        bool wasDead = false;
        {
            std::lock_guard<std::recursive_mutex> guard(cInfo->stateLock);
            wasDead = cInfo->wasAliveNowDead;
        }
        if (wasDead)
        {
            sAutoBalanceMgr.OnCreatureRespawn(creature);
            return;
        }
    }

    if (!sAutoBalanceMgr.IsCreatureScaled(creature))
        sAutoBalanceMgr.OnCreatureAdd(creature);
}

void AutoBalanceUnitScript::OnUnitDeath(Unit* unit, Unit* /*killer*/)
{
    if (!unit || !unit->IsCreature())
        return;

    sAutoBalanceMgr.OnCreatureDeath(static_cast<Creature*>(unit));
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

void AutoBalanceAllMapScript::OnMapUpdate(Map* map, uint32 diff)
{
    sAutoBalanceMgr.OnMapUpdate(map, diff);
}
