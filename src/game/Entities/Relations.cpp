/*
 * This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "Server/DBCStores.h"
#include "Entities/Unit.h"
#include "Entities/Player.h"
#include "Entities/Corpse.h"
#include "Entities/GameObject.h"
#include "Entities/DynamicObject.h"
#include "Globals/ObjectMgr.h"
#include "Globals/ObjectAccessor.h"
#include "Tools/Formulas.h"

/////////////////////////////////////////////////
/// @file       Relations.cpp
/// @date       September, 2017
/// @brief      Relations API for supported Entities.
///
/// Relations API controls various interactions between entities, such as friendliness or hostility.
///
/// Relations API is split into three tiers:
///
/// <b>Tier 1</b> is a direct reverse engineered gameplay logic from the game.
/// It should never be modified with custom logic to be in sync with the client we aim to support.
/// Each function presented here has a counterpart in client for comparision.
///
/// <b>Tier 2</b> is a server-side extension to plug holes left by original client-side perspective.
/// It builds up on previous tier for entities not represented client-side.
/// All functions presented here are required to mimic the look and feel of the original API.
/// No functions in this tier have a real client-side counterpart.
///
/// <b>Tier 3</b> is a custom server-side convinience API for CMaNGOS.
/// This is a stylistically relaxed set of custom wrappers and helpers for various subsystems.
/// All functions presented in this tier are exclusive to the emulator and have no outside influence.
///
/// Tier 1 is implied to be "set in stone" as it comes from 1st hand source - the game itself.
/// The only reason Tier 1 API should be ever modified is to fix possible mistakes in reverse engineered code.
/// Any user modifications or additions to Tier 1 which are not coming from client's code should be rejected to preserve overall integrity.
///
/// Tiers 2 and 3 are serverside APIs and will be extended in the future as demand arises during actual rollout.
/////////////////////////////////////////////////

/*##########################
########            ########
########   TIER 1   ########
########            ########
##########################*/

/////////////////////////////////////////////////
/// Controlling player: get which player is the "master" of the unit gameplay-wise
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::GetControllingPlayer(CGUnit_C *this)</tt>
/// Contains optional logic for getting original permanent "master" by ignoring charms (also known as "UI PoV"), datamined from other functions.
/////////////////////////////////////////////////
Player const* Unit::GetControllingPlayer(bool ignoreCharms/* = false*/) const
{
    // Mode selector: normal or permanent (UI point of view, ignore charms)
    ObjectGuid const& (Unit::* getter) () const = (ignoreCharms ? &Unit::GetOwnerGuid : &Unit::GetMasterGuid);

    // Original logic begins

    // Pre-TBC variant
    if (ObjectGuid const& masterGuid = (this->*getter)())
    {
        if (Unit const* master = ObjectAccessor::GetUnit(*this, masterGuid))
        {
            if (master->GetTypeId() == TYPEID_PLAYER)
                return static_cast<Player const*>(master);
        }
    }
    else if (GetTypeId() == TYPEID_PLAYER)
        return static_cast<Player const*>(this);
    return nullptr;
}

/////////////////////////////////////////////////
/// Get faction template to faction tenplate reaction
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>static function (original symbol name unknown)</tt>
/////////////////////////////////////////////////
static inline ReputationRank GetFactionReaction(FactionTemplateEntry const* thisTemplate, FactionTemplateEntry const* otherTemplate)
{
    MANGOS_ASSERT(thisTemplate)
    MANGOS_ASSERT(otherTemplate)

    // Original logic begins

    if (otherTemplate->factionGroupMask & thisTemplate->enemyGroupMask)
        return REP_HOSTILE;

    if (thisTemplate->enemyFaction[0] && otherTemplate->faction)
    {
        for (unsigned int i : thisTemplate->enemyFaction)
        {
            if (i == otherTemplate->faction)
                return REP_HOSTILE;
        }
    }

    if (otherTemplate->factionGroupMask & thisTemplate->friendGroupMask)
        return REP_FRIENDLY;

    if (thisTemplate->friendFaction[0] && otherTemplate->faction)
    {
        for (unsigned int i : thisTemplate->friendFaction)
        {
            if (i == otherTemplate->faction)
                return REP_FRIENDLY;
        }
    }

    if (thisTemplate->factionGroupMask & otherTemplate->friendGroupMask)
        return REP_FRIENDLY;

    if (otherTemplate->friendFaction[0] && thisTemplate->faction)
    {
        for (unsigned int i : otherTemplate->friendFaction)
        {
            if (i == thisTemplate->faction)
                return REP_FRIENDLY;
        }
    }
    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// Get faction template to unit reaction
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>static CGUnit_C::UnitReaction(int factionTemplateID, const CGUnit_C *unit)</tt>
/// Faction template id was replaced with FactionTemplateEntry ptr for performance, now caller is responsible for lookup
/// Used as static function instead of being a static method of Unit class
/////////////////////////////////////////////////
static ReputationRank GetFactionReaction(FactionTemplateEntry const* thisTemplate, Unit const* unit)
{
    MANGOS_ASSERT(unit)

    // Original logic begins

    if (thisTemplate)
    {
        if (const FactionTemplateEntry* unitFactionTemplate = unit->GetFactionTemplateEntry())
        {
            if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
            {
                if (const Player* unitPlayer = unit->GetControllingPlayer())
                {
                    if (unitPlayer->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP) && thisTemplate->IsContestedGuardFaction())
                        return REP_HOSTILE;

                    if (const ReputationRank* rank = unitPlayer->GetReputationMgr().GetForcedRankIfAny(thisTemplate))
                        return (*rank);

                    const FactionEntry* thisFactionEntry = sFactionStore.LookupEntry(thisTemplate->faction);
                    if (thisFactionEntry && thisFactionEntry->HasReputation())
                    {
                        const ReputationMgr& reputationMgr = unitPlayer->GetReputationMgr();
                        return reputationMgr.GetRank(thisFactionEntry);
                    }
                }

            }
            // Default fallback if player-specific checks didn't catch anything: facton to faction
            return GetFactionReaction(thisTemplate, unitFactionTemplate);
        }
    }
    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// Get unit to unit reaction
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::UnitReaction(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
ReputationRank Unit::GetReactionTo(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic begins

    if (this == unit)
        return REP_FRIENDLY;

    if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
    {
        const Player* thisPlayer = GetControllingPlayer();

        if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
        {
            const Player* unitPlayer = unit->GetControllingPlayer();

            if (!thisPlayer || !unitPlayer)
                return REP_NEUTRAL;

            // Pre-TBC same player check: not present clientside in this order, but in for optimization (same result achieved through same group check below)
            if (thisPlayer == unitPlayer)
                return REP_FRIENDLY;

            if (thisPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) && unitPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) && thisPlayer->GetGuidValue(PLAYER_DUEL_ARBITER) == unitPlayer->GetGuidValue(PLAYER_DUEL_ARBITER))
                return thisPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) != unitPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) ? REP_HOSTILE : REP_FRIENDLY;

            // Pre-WotLK group check: always, replaced with faction template check in WotLK
            if (thisPlayer->IsInGroup(unitPlayer))
                return REP_FRIENDLY;

            // Pre-WotLK FFA check, known limitation: FFA doesn't work with totem elementals both client-side and server-side
            if (thisPlayer->IsPvPFreeForAll() && unitPlayer->IsPvPFreeForAll())
                return REP_HOSTILE;
        }

        if (thisPlayer)
        {
            if (const FactionTemplateEntry* unitFactionTemplate = unit->GetFactionTemplateEntry())
            {
                if (const ReputationRank* rank = thisPlayer->GetReputationMgr().GetForcedRankIfAny(unitFactionTemplate))
                    return (*rank);

                const FactionEntry* unitFactionEntry = sFactionStore.LookupEntry(unitFactionTemplate->faction);

                // If the faction has reputation ranks available, "at war" and contested PVP flags decide outcome
                if (unitFactionEntry && unitFactionEntry->HasReputation())
                {
                    // Pre-TBC contested check: not present clientside in this order, but in for optimization (same result achieved through faction to unit check below)
                    if (thisPlayer->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_CONTESTED_PVP) && unitFactionTemplate->IsContestedGuardFaction())
                        return REP_HOSTILE;

                    return thisPlayer->GetReputationMgr().IsAtWar(unitFactionEntry) ? REP_HOSTILE : REP_FRIENDLY;
                }
            }
        }
    }
    // Default fallback if player-specific checks didn't catch anything: facton to unit
    ReputationRank reaction = GetFactionReaction(GetFactionTemplateEntry(), unit);

    // Persuation support
    if (reaction > REP_HOSTILE && reaction < REP_HONORED && (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PERSUADED) || GetPersuadedGuid() == unit->GetObjectGuid()))
    {
        if (const FactionTemplateEntry* unitFactionTemplate = unit->GetFactionTemplateEntry())
        {
            const FactionEntry* unitFactionEntry = sFactionStore.LookupEntry(unitFactionTemplate->faction);
            if (unitFactionEntry && unitFactionEntry->HasReputation())
                reaction = ReputationRank(int32(reaction) + 1);
        }
    }
    return reaction;
}

/////////////////////////////////////////////////
/// Get unit to corpse reaction
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::UnitReaction(const CGUnit_C *this, const CGCorpse_C *corpse)</tt>
/////////////////////////////////////////////////
ReputationRank Unit::GetReactionTo(const Corpse* corpse) const
{
    MANGOS_ASSERT(corpse)

    // Original logic begins

    if (const FactionTemplateEntry* thisTemplate = GetFactionTemplateEntry())
    {
        if (const uint32 corpseTemplateId = corpse->GetFaction())
        {
            if (const FactionTemplateEntry* corpseTemplate = sFactionTemplateStore.LookupEntry(corpseTemplateId))
                return GetFactionReaction(thisTemplate, corpseTemplate);
        }
    }
    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// Get GO to unit reaction
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGGameObject_C::ObjectReaction(const CGGameObject_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
ReputationRank GameObject::GetReactionTo(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic begins

    if (const Unit* owner = GetOwner())
        return owner->GetReactionTo(unit);

    if (const uint32 faction = GetUInt32Value(GAMEOBJECT_FACTION))
    {
        if (const FactionTemplateEntry* factionTemplate = sFactionTemplateStore.LookupEntry(faction))
            return GetFactionReaction(factionTemplate, unit);
    }

    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// Reaction preset: Unit sees another unit as an enemy
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::UnitIsEnemy(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
bool Unit::IsEnemy(Unit const* unit) const
{
    return (GetReactionTo(unit) < REP_UNFRIENDLY);
}

/////////////////////////////////////////////////
/// Reaction preset: Unit sees another unit as a friend
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::UnitIsFriend(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
bool Unit::IsFriend(Unit const* unit) const
{
    return (GetReactionTo(unit) > REP_NEUTRAL);
}

/////////////////////////////////////////////////
/// Opposition: Unit treats another unit as an enemy it can attack (generic)
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanAttack(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/// Backbone of all spells which can target hostile units.
/////////////////////////////////////////////////
bool Unit::CanAttack(const Unit* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // Creatures cannot attack player ghosts, unless it is a specially flagged ghost creature
    if (GetTypeId() == TYPEID_UNIT && unit->GetTypeId() == TYPEID_PLAYER && static_cast<const Player*>(unit)->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
    {
        if (!(static_cast<const Creature*>(this)->GetCreatureInfo()->HasFlag(CreatureTypeFlags::VISIBLE_TO_GHOSTS)))
            return false;
    }

    // We can't attack unit when at least one of these flags is present on it:
    const uint32 mask = (UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_UNTARGETABLE | UNIT_FLAG_TAXI_FLIGHT | UNIT_FLAG_UNINTERACTIBLE);
    if (unit->HasFlag(UNIT_FIELD_FLAGS, mask))
        return false;

    // Cross-check immunity and sanctuary flags: this <-> unit
    const bool thisPlayerControlled = HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
    if (thisPlayerControlled)
    {
        if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER))
            return false;
    }
    else if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC))
        return false;

    const bool unitPlayerControlled = unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
    if (unitPlayerControlled)
    {
        if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER))
            return false;
    }
    else if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC))
        return false;

    if (thisPlayerControlled || unitPlayerControlled)
    {
        if (thisPlayerControlled && unitPlayerControlled)
        {
            if (IsFriend(unit))
                return false;

            const Player* thisPlayer = GetControllingPlayer();
            if (!thisPlayer)
                return true;

            const Player* unitPlayer = unit->GetControllingPlayer();
            if (!unitPlayer)
                return true;

            if (thisPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) && unitPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) && thisPlayer->GetGuidValue(PLAYER_DUEL_ARBITER) == unitPlayer->GetGuidValue(PLAYER_DUEL_ARBITER))
                return true;

            if (unitPlayer->IsPvP())
                return true;

            if (thisPlayer->IsPvPFreeForAll() && unitPlayer->IsPvPFreeForAll())
                return true;

            return false;
        }
        return (!IsFriend(unit));
    }
    return (IsEnemy(unit) || unit->IsEnemy(this));
}

/////////////////////////////////////////////////
/// Opposition: Unit treats another unit as an enemy it can attack (immediate response)
///
/// @note Relations API Tier 1
///
/// Backported from TBC+ client-side counterpart: <tt>CGUnit_C::CanAttackNow(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/// Intended usage is to verify direct requests to attack something.
/// First appeared in TBC+ clients, backported for API unification between expansions.
/////////////////////////////////////////////////
bool Unit::CanAttackNow(const Unit* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // We can't initiate attack while dead or ghost
    if (!IsAlive())
        return false;

    // We can't initiate attack while mounted
    if (IsMounted())
        return false;

    // We can't initiate attack on dead units
    if (!unit->IsAlive())
        return false;

    return CanAttack(unit);
}

/////////////////////////////////////////////////
/// Assistance: Unit treats another unit as an ally it can help
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanAssist(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/// Backbone of all spells which can target friendly units.
/// Optional ignoreFlags parameter first appeared in TBC+ clients, backported for API unification between expansions.
/////////////////////////////////////////////////
bool Unit::CanAssist(const Unit* unit, bool /*ignoreFlags*/) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // We can't assist unselectable unit
    if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE))
        return false;

    // Exclude non-friendlies at this point
    if (GetReactionTo(unit) < REP_FRIENDLY)
        return false;

    // Pre-WotLK: backbone of lua UnitIsPVP(), a member of unit class client-side
    auto isPvPUI = [](Unit const * self)
    {
        if (Unit const* master = self->GetMaster())
        {
            if (self->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER))
                return false;
            return master->IsPvP();
        }
        return self->IsPvP();
    };

    // Detect player controlled unit and exit early
    if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
    {
        const Player* thisPlayer = GetControllingPlayer();
        const Player* unitPlayer = unit->GetControllingPlayer();

        if (thisPlayer && unitPlayer)
        {
            if (thisPlayer->GetGuidValue(PLAYER_DUEL_ARBITER) != unitPlayer->GetGuidValue(PLAYER_DUEL_ARBITER)
                || thisPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) != unitPlayer->GetUInt32Value(PLAYER_DUEL_TEAM))
                return false;

            if (unitPlayer->IsPvPFreeForAll() && !thisPlayer->IsPvPFreeForAll())
                return false;
        }
        return true;
    }

    // If we continue here, unit is an npc. Detect if we are an npc too, so we can exit early
    if (!HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
        return true;

    // Pre-TBC: We are left with player assisting an npc case here: can assist friendly NPCs with PVP flag
    if (isPvPUI(unit))
        return true;

    return false;
}

/////////////////////////////////////////////////
/// Assistance: Unit treats a corpse as an ally corpse it can help
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanAssist(const CGUnit_C *this, const CGCorpse_C *corpse)</tt>
/// Backbone of all spells which can target friendly corpses.
/////////////////////////////////////////////////
bool Unit::CanAssist(Corpse const* corpse) const
{
    return GetReactionTo(corpse) > REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// Cooperation: Unit can cooperate with another unit
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanCooperate(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
bool Unit::CanCooperate(const Unit* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // Can't cooperate with yourself
    if (this == unit)
        return false;

    // We can't cooperate while being charmed or with charmed unit
    if (GetCharmerGuid() || unit->GetCharmerGuid())
        return false;

    if (const FactionTemplateEntry* thisFactionTemplate = GetFactionTemplateEntry())
    {
        if (const FactionTemplateEntry* unitFactionTemplate = unit->GetFactionTemplateEntry())
        {
            if (thisFactionTemplate->factionGroupMask == unitFactionTemplate->factionGroupMask)
                // Pre-TBC: CanAttack check is not present clientside (always true), but potentially can be useful serverside to resolve some corner cases (e.g. duels). TODO: research needed
                return (!CanAttack(unit));
        }
    }
    return false;
}

/////////////////////////////////////////////////
/// Interaction: Unit can interact with an object (generic)
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanInteract(const CGUnit_C *this, const CGGameObject_C *object)</tt>
/////////////////////////////////////////////////
bool Unit::CanInteract(const GameObject* object) const
{
    MANGOS_ASSERT(object)

    // Original logic

    // Can't interact with GOs as a ghost
    if (GetTypeId() == TYPEID_PLAYER && static_cast<const Player*>(this)->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
        return false;

    return (object->GetReactionTo(this) > REP_UNFRIENDLY);
}

/////////////////////////////////////////////////
/// Interaction: Unit can interact with another unit (generic)
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanInteract(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
bool Unit::CanInteract(const Unit* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // Unit must be selectable
    if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_UNINTERACTIBLE))
        return false;

    // Unit must have NPC flags so we can actually interact in some way
    if (!unit->GetUInt32Value(UNIT_NPC_FLAGS))
        return false;

    // We can't interact with anyone as a ghost except specially flagged NPCs
    if (GetTypeId() == TYPEID_PLAYER && static_cast<const Player*>(this)->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
    {
        if (unit->GetTypeId() != TYPEID_UNIT || !(static_cast<const Creature*>(unit)->GetCreatureInfo()->HasFlag(CreatureTypeFlags::VISIBLE_TO_GHOSTS)))
            return false;
    }

    return (GetReactionTo(unit) > REP_UNFRIENDLY && unit->GetReactionTo(this) > REP_UNFRIENDLY);
}

/////////////////////////////////////////////////
/// Interaction: Unit can interact with another unit (immediate response)
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>CGUnit_C::CanInteractNow(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/////////////////////////////////////////////////
bool Unit::CanInteractNow(const Unit* unit) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // We can't intract while on taxi
    if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_TAXI_FLIGHT))
        return false;

    // We can't interact while being charmed
    if (GetCharmerGuid())
        return false;

    // We can't interact with anyone while being dead (this does not apply to player ghosts, which allow very limited interactions)
    if (!IsAlive() && (GetTypeId() == TYPEID_UNIT || !(static_cast<const Player*>(this)->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))))
        return false;

    // We can't interact with anyone while being shapeshifted, unless form flags allow us to do so
    if (IsShapeShifted())
    {
        if (SpellShapeshiftFormEntry const* formEntry = sSpellShapeshiftFormStore.LookupEntry(GetShapeshiftForm()))
        {
            if (!(formEntry->flags1 & SHAPESHIFT_FLAG_CAN_NPC_INTERACT))
                return false;
        }
    }

    // We can't interact with dead units, unless it's a creature with special flag
    if (!unit->IsAlive())
    {
        if (GetTypeId() != TYPEID_UNIT || !(static_cast<const Creature*>(unit)->GetCreatureInfo()->HasFlag(CreatureTypeFlags::INTERACT_WHILE_DEAD)))
            return false;
    }

    // We can't interact with charmed units
    if (unit->GetCharmerGuid())
        return false;

    // We can't interact with units who are currently fighting
    if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PET_IN_COMBAT) || unit->GetVictim())
        return false;

    return CanInteract(unit);
}

/////////////////////////////////////////////////
/// Trivial: Unit does not count as a worthy target for another unit
///
/// @note Relations API Tier 1
///
/// Based on client-side counterpart: <tt>static CGPlayer_C::UnitIsTrivial(const CGUnit_C *unit)</tt>
/// Points of view are swapped to fit in with the rest of API, logic is preserved.
/////////////////////////////////////////////////
bool Unit::IsTrivialForTarget(Unit const* pov) const
{
    MANGOS_ASSERT(pov)

    // Original logic adapation for server (original function was operating as a local player PoV only)

    // Players are never seen as trivial
    if (GetTypeId() == TYPEID_PLAYER)
        return false;

    // Perform a level range query on the appropriate global constant NON_TRIVIAL_LEVEL_DIFFS array for the expansion
    return MaNGOS::XP::IsTrivialLevelDifference(pov->GetLevelForTarget(this), GetLevelForTarget(pov));
}

/////////////////////////////////////////////////
/// Civilian: Unit counts as a dishonorable kill for another unit
///
/// @note Relations API Tier 1
///
/// Client-side counterpart: <tt>static function (original symbol name unknown)</tt>
/////////////////////////////////////////////////
bool Unit::IsCivilianForTarget(Unit const* pov) const
{
    MANGOS_ASSERT(pov)

    // Original logic

    // PvP-enabled enemy npcs with civilian flag
    if (IsPvP() && GetTypeId() == TYPEID_UNIT && static_cast<const Creature*>(this)->IsCivilian())
        return (IsTrivialForTarget(pov) && IsEnemy(pov));

    return false;
}

/////////////////////////////////////////////////
/// Group: Unit counts as being placed in the same group (party or raid) with another unit (for gameplay purposes)
///
/// @note Relations API Tier 1
///
/// Based on client-side counterpart: <tt>static CGUnit_C::IsUnitInGroup(const CGUnit_C *this, const CGUnit_C *unit)</tt>
/// Points of view are swapped to fit in with the rest of API, logic is preserved.
/// Additionally contains optional detection of same group from UI standpoint (ignoring charms).
/////////////////////////////////////////////////
bool Unit::IsInGroup(Unit const* other, bool party/* = false*/, bool ignoreCharms/* = false*/) const
{
    MANGOS_ASSERT(other)

    // Original logic adaptation for server (original function was operating as a local player PoV only)

    // Same unit is always in group with itself
    if (this == other)
        return true;

    // Only player controlled
    if (this->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED) && other->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
    {
        // Check if controlling players are in the same group (same logic as client, but not local)
        if (const Player* thisPlayer = GetControllingPlayer(ignoreCharms))
        {
            if (const Player* otherPlayer = other->GetControllingPlayer(ignoreCharms))
            {
                const Group* group = thisPlayer->GetGroup();
                return (thisPlayer == otherPlayer || (group && group == otherPlayer->GetGroup() && (!party || group->SameSubGroup(thisPlayer, otherPlayer))));
            }
        }
    }

    return false;
}

/*##########################
########            ########
########   TIER 2   ########
########            ########
##########################*/

/////////////////////////////////////////////////
/// [Serverside] Get default WorldObject (hierarchy) reaction to a unit
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Game always defaults reactions to neutral.
/////////////////////////////////////////////////
ReputationRank WorldObject::GetReactionTo(Unit const* /*unit*/) const
{
    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// [Serverside] Get default WorldObject (hierarchy) reaction to a corpse
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Game always defaults reactions to neutral.
/////////////////////////////////////////////////
ReputationRank WorldObject::GetReactionTo(Corpse const* /*corpse*/) const
{
    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// [Serverside] Reaction preset: WorldObject (hierarchy) sees a unit as an enemy
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Game always defaults reactions to "neutral", so this is always false (neutral is not an enemy).
/////////////////////////////////////////////////
bool WorldObject::IsEnemy(Unit const* /*unit*/) const
{
    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Reaction preset: WorldObject (hierarchy) sees a unit as a friend
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Game always defaults reactions to "neutral", so this is always false (neutral is not a friend).
/////////////////////////////////////////////////
bool WorldObject::IsFriend(Unit const* /*unit*/) const
{
    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Reaction preset: GO sees a unit as an enemy
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Some gameobjects can be involved in spell casting, so server needs additional API support.
/////////////////////////////////////////////////
bool GameObject::IsEnemy(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    if (const Unit* owner = GetOwner())
        return owner->IsEnemy(unit);

    if (const uint32 faction = GetUInt32Value(GAMEOBJECT_FACTION))
    {
        if (const FactionTemplateEntry* factionTemplate = sFactionTemplateStore.LookupEntry(faction))
            return (GetFactionReaction(factionTemplate, unit) < REP_UNFRIENDLY);
    }

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Reaction preset: GO sees a unit as a friend
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Some gameobjects can be involved in spell casting, so server needs additional API support.
/////////////////////////////////////////////////
bool GameObject::IsFriend(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    if (const Unit* owner = GetOwner())
        return owner->IsFriend(unit);

    if (const uint32 faction = GetUInt32Value(GAMEOBJECT_FACTION))
    {
        if (const FactionTemplateEntry* factionTemplate = sFactionTemplateStore.LookupEntry(faction))
            return (GetFactionReaction(factionTemplate, unit) > REP_NEUTRAL);
    }

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Get DynamicObject reaction to a unit
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Dynamic objects act as serverside proxy casters for units.
/////////////////////////////////////////////////
ReputationRank DynamicObject::GetReactionTo(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    if (const Unit* caster = GetCaster())
        return caster->GetReactionTo(unit);

    return REP_NEUTRAL;
}

/////////////////////////////////////////////////
/// [Serverside] Reaction preset: DynamicObject sees a unit as an enemy
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Dynamic objects act as serverside proxy casters for units.
/////////////////////////////////////////////////
bool DynamicObject::IsEnemy(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    if (const Unit* caster = GetCaster())
        return caster->IsEnemy(unit);

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Reaction preset: DynamicObject sees a unit as a friend
///
/// @note Relations API Tier 2
///
/// This function is not intented to have client-side counterpart by original design.
/// Dynamic objects act as serverside proxy casters for units.
/////////////////////////////////////////////////
bool DynamicObject::IsFriend(Unit const* unit) const
{
    MANGOS_ASSERT(unit)

    if (const Unit* caster = GetCaster())
        return caster->IsFriend(unit);

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Group: Extension for creatures, player-controlled defaults to unit, creatures check based on friendliness
///
/// @note Relations API Tier 2
///
/// No client counterpart, since client only deals with player-controlled entities
/////////////////////////////////////////////////
bool Creature::IsInGroup(Unit const* other, bool party/* = false*/, bool ignoreCharms/* = false*/) const
{
    MANGOS_ASSERT(other)

    if (this->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED) || other->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
        return Unit::IsInGroup(other, party, ignoreCharms);

    // Faction-based based on research
    return this->IsFriend(other);
}

/////////////////////////////////////////////////
/// [Serverside] Group: Extension for players, ignoring charms also ignores PC flag presence for UI PoV
///
/// @note Relations API Tier 2
///
/// No client counterpart, since client only deals with player-controlled entities
/////////////////////////////////////////////////
bool Player::IsInGroup(Unit const* other, bool party/* = false*/, bool ignoreCharms/* = false*/) const
{
    MANGOS_ASSERT(other)

    if (this != other && ignoreCharms && other->IsPlayer())
    {
        const Player* otherPlayer = static_cast<Player const*>(other);
        const Group* group = this->GetGroup();
        return (group && group == otherPlayer->GetGroup() && (!party || group->SameSubGroup(this, otherPlayer)));
    }

    return Unit::IsInGroup(other, party, ignoreCharms);
}

/*##########################
########            ########
########   TIER 3   ########
########            ########
##########################*/

/////////////////////////////////////////////////
/// [Serverside] Get player to corpse reaction
///
/// @note Relations API Tier 3
///
/// This function is a required serverside component for crossfaction functionality.
/////////////////////////////////////////////////
ReputationRank Player::GetReactionTo(const Corpse* corpse) const
{
    MANGOS_ASSERT(corpse)

    if (Player* corpseOwner = sObjectMgr.GetPlayer(corpse->GetOwnerGuid()))
    {
        if (this != corpseOwner && GetTeam() != corpseOwner->GetTeam())
        {
            // [XFACTION]: Swap faction check with "Alliance Generic" and 'Horde Generic" for crossfaction functionality
            if (sWorld.getConfig(CONFIG_BOOL_ALLOW_TWO_SIDE_INTERACTION_GROUP) && IsInGroup(corpseOwner))
            {
                uint32 id = (GetTeam() == ALLIANCE ? 1054 : 1495);
                return GetFactionReaction(GetFactionTemplateEntry(), sFactionTemplateStore.LookupEntry(id));
            }
        }
    }

    return Unit::GetReactionTo(corpse);
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: DynamicObject can target a target with a harmful spell
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// Dynamic objects act as serverside proxy casters for units.
/// It utilizes owners CanAttackSpell if owner exists
/////////////////////////////////////////////////
bool DynamicObject::CanAttackSpell(Unit const* target, SpellEntry const* spellInfo, bool isAOE) const
{
    MANGOS_ASSERT(target)

    if (Unit* owner = GetCaster())
        return owner->CanAttackSpell(target, spellInfo, isAOE);

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Assistance: DynamicObject can target a target with a helpful spell
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// Dynamic objects act as serverside proxy casters for units.
/// It utilizes owners CanAssistSpell if owner exists
/////////////////////////////////////////////////
bool DynamicObject::CanAssistSpell(Unit const* target, SpellEntry const* spellInfo) const
{
    MANGOS_ASSERT(target)

    if (Unit* owner = GetCaster())
        return owner->CanAttackSpell(target, spellInfo);

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: GameObject can target a target with a harmful spell
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// Some gameobjects can be involved in spell casting, so server needs additional API support.
/// It utilizes owners CanAttackSpell if owner exists
/////////////////////////////////////////////////
bool GameObject::CanAttackSpell(Unit const* target, SpellEntry const* spellInfo, bool isAOE) const
{
    MANGOS_ASSERT(target)

    if (Unit* owner = GetOwner())
        return owner->CanAttackSpell(target, spellInfo, isAOE);

    if (target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
        return !IsFriend(target);

    return IsEnemy(target);
}

/////////////////////////////////////////////////
/// [Serverside] Assistance: GameObject can target a target with a helpful spell
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// Some gameobjects can be involved in spell casting, so server needs additional API support.
/// It utilizes owners CanAssistSpell if owner exists
/////////////////////////////////////////////////
bool GameObject::CanAssistSpell(Unit const* target, SpellEntry const* spellInfo) const
{
    MANGOS_ASSERT(target)

    if (Unit* owner = GetOwner())
        return owner->CanAssistSpell(target, spellInfo);

    if (target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
        return !IsEnemy(target);

    return IsFriend(target);
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: Unit can target a target with a harmful spell
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// It utilizes SpellEntry for additional target filtering.
/// Also an additional fine grained check needs to be done for AOE spells, because they
/// need to skip PVP enabled targets in some special cases. (Chain spells, AOE)
/////////////////////////////////////////////////
bool Unit::CanAttackSpell(Unit const* target, SpellEntry const* spellInfo, bool isAOE) const
{
    MANGOS_ASSERT(target);

    bool ignoreFlagsSource = false;
    bool ignoreFlagsTarget = false;
    if (spellInfo)
    {
        // inversealive is needed for some spells which need to be casted at dead targets (aoe)
        if (!target->IsAlive() && !spellInfo->HasAttribute(SPELL_ATTR_EX2_ALLOW_DEAD_TARGET))
            return false;

        ignoreFlagsSource = spellInfo->HasAttribute(SPELL_ATTR_EX3_IGNORE_CASTER_AND_TARGET_RESTRICTIONS);
        ignoreFlagsTarget = spellInfo->HasAttribute(SPELL_ATTR_EX3_IGNORE_CASTER_AND_TARGET_RESTRICTIONS);
    }

    if (CanAttackInCombat(target, ignoreFlagsSource, ignoreFlagsTarget))
    {
        if (target->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
        {
            if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
            {
                // PVP-flagged PC units cant *unintentionally* attack PVP-unflagged PC units with AOE (unless in FFA action) and vice versa
                // Pre-WotLK: Using reverse Unit::CanAttack() checks:
                if (isAOE)
                {
                    if (const Player* thisPlayer = GetControllingPlayer())
                    {
                        if (const Player* unitPlayer = target->GetControllingPlayer())
                        {
                            if (!thisPlayer->IsInDuelWith(unitPlayer) && thisPlayer->IsPvP() != unitPlayer->IsPvP())
                                return (thisPlayer->IsPvPFreeForAll() && unitPlayer->IsPvPFreeForAll());
                        }
                    }
                }
            }
            else
            {
                // NPC units cant *unintentionally* attack non-hostile PC units which aren't at war with them
                if (!IsEnemy(target))
                {
                    if (const Player* unitPlayer = target->GetControllingPlayer())
                    {
                        if (const FactionTemplateEntry* thisFactionTemplate = GetFactionTemplateEntry())
                        {
                            const FactionEntry* thisFactionEntry = sFactionStore.LookupEntry(thisFactionTemplate->faction);
                            if (thisFactionEntry && thisFactionEntry->HasReputation())
                                return unitPlayer->GetReputationMgr().IsAtWar(thisFactionEntry);
                        }
                    }
                }
            }
        }
        return true;
    }
    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Assistance: Unit can target a target with a helpful spell
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// It utilizes owners CanAssistSpell if owner exists
/////////////////////////////////////////////////
bool Unit::CanAssistSpell(Unit const* target, SpellEntry const* spellInfo) const
{
    return CanAssist(target);
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: Unit can attack a target on sight
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// Typically used in AIs in MoveInLineOfSight
/////////////////////////////////////////////////
bool Unit::CanAttackOnSight(Unit const* target) const
{
    MANGOS_ASSERT(target)

    // Do not aggro on a unit which is moving home at the moment
    if (target->GetCombatManager().IsEvadingHome())
        return false;

    // Do not aggro while a successful feign death is active
    if (!IsIgnoringFeignDeath() && target->IsFeigningDeathSuccessfully())
        return false;

    // Pets in disabled state (e.g. when player is mounted) do not draw aggro on sight
    // TODO: Fix for temporary pets and charms
    if (target->GetTypeId() == TYPEID_UNIT && static_cast<Creature const*>(target)->IsPet() && static_cast<Pet const*>(target)->GetModeFlags() & PET_MODE_DISABLE_ACTIONS)
        return false;

    return (CanAttack(target) && IsEnemy(target));
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: Unit can attack a target on sight
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// Typically used for combat checks for at war case
/////////////////////////////////////////////////
bool Unit::CanAttackInCombat(Unit const* target, bool ignoreFlagsSource, bool ignoreFlagsTarget) const
{
    if (!CanAttackServerside(target, ignoreFlagsSource, ignoreFlagsTarget))
    {
        if (target->IsPlayerControlled()) // If this is not fine grained enough, incorporation into CanAttack or copypaste of that whole func will be necessary
        {
            // NPC should be able to attack players who are at war with the npc
            if (IsFriend(target))
            {
                if (const Player* unitPlayer = target->GetControllingPlayer())
                {
                    if (const FactionTemplateEntry* thisFactionTemplate = GetFactionTemplateEntry())
                    {
                        FactionEntry const* thisFactionEntry = sFactionStore.LookupEntry(thisFactionTemplate->faction);
                        if (thisFactionEntry && thisFactionEntry->HasReputation() && unitPlayer->GetReputationMgr().IsAtWar(thisFactionEntry))
                            return true;
                    }
                }
            }
        }
        return false;
    }

    return true;
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: Unit can attack a target on sight
///
/// @note Relations API Tier 3
///
/// This function is altered counterpart of CanAttack from clientside with added parameters for spells.
/// Only used as customized CanAttack inside CanAttackSpell flow
/////////////////////////////////////////////////
bool Unit::CanAttackServerside(Unit const* unit, bool ignoreFlagsSource, bool ignoreFlagsTarget) const
{
    MANGOS_ASSERT(unit)

    // Original logic

    // Creatures cannot attack player ghosts, unless it is a specially flagged ghost creature
    if (GetTypeId() == TYPEID_UNIT && unit->GetTypeId() == TYPEID_PLAYER && static_cast<const Player*>(unit)->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_GHOST))
    {
        if (!(static_cast<const Creature*>(this)->GetCreatureInfo()->HasFlag(CreatureTypeFlags::VISIBLE_TO_GHOSTS)))
            return false;
    }

    // We can't attack unit when at least one of these flags is present on it:
    const uint32 mask = (UNIT_FLAG_SPAWNING | UNIT_FLAG_NOT_ATTACKABLE_1 | UNIT_FLAG_UNTARGETABLE | UNIT_FLAG_TAXI_FLIGHT | UNIT_FLAG_UNINTERACTIBLE);
    if (unit->HasFlag(UNIT_FIELD_FLAGS, mask))
        return false;

    const bool thisPlayerControlled = HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);
    const bool unitPlayerControlled = unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED);

    if (!ignoreFlagsTarget) // used by spell attributes
    {
        // Cross-check immunity and sanctuary flags: this <-> unit
        if (thisPlayerControlled)
        {
            if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER))
                return false;
        }
        else if (unit->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC))
            return false;
    }

    if (!ignoreFlagsSource)
    {
        if (unitPlayerControlled)
        {
            if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_PLAYER))
                return false;
        }
        else if (HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_IMMUNE_TO_NPC))
            return false;
    }

    if (thisPlayerControlled || unitPlayerControlled)
    {
        if (thisPlayerControlled && unitPlayerControlled)
        {
            if (IsFriend(unit))
                return false;

            const Player* thisPlayer = GetControllingPlayer();
            if (!thisPlayer)
                return true;

            const Player* unitPlayer = unit->GetControllingPlayer();
            if (!unitPlayer)
                return true;

            if (thisPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) && unitPlayer->GetUInt32Value(PLAYER_DUEL_TEAM) && thisPlayer->GetGuidValue(PLAYER_DUEL_ARBITER) == unitPlayer->GetGuidValue(PLAYER_DUEL_ARBITER))
                return true;

            if (unitPlayer->IsPvP())
                return true;

            if (thisPlayer->IsPvPFreeForAll() && unitPlayer->IsPvPFreeForAll())
                return true;

            return false;
        }
        return (!IsFriend(unit));
    }
    return (IsEnemy(unit) || unit->IsEnemy(this));
}

/////////////////////////////////////////////////
/// [Serverside] Fog of War: Unit can be seen by other unit through invisibility effects
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// A helper function to determine if unit is always visible to another unit.
/////////////////////////////////////////////////
bool Unit::IsFogOfWarVisibleStealth(Unit const* other) const
{
    MANGOS_ASSERT(other)

    // Gamemasters can see through invisibility
    if (other->GetTypeId() == TYPEID_PLAYER && static_cast<Player const*>(other)->IsGameMaster())
        return true;

    switch (sWorld.getConfig(CONFIG_UINT32_FOGOFWAR_STEALTH))
    {
        default: return IsInGroup(other);
        case 1:  return CanCooperate(other);
    }
}

/////////////////////////////////////////////////
/// [Serverside] Fog of War: Unit's health values can be seen by other unit
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// A helper function to determine if unit's health values are always visible to another unit.
/////////////////////////////////////////////////
bool Unit::IsFogOfWarVisibleHealth(Unit const* other) const
{
    MANGOS_ASSERT(other)

    // Gamemasters can see health values
    if (other->GetTypeId() == TYPEID_PLAYER && static_cast<Player const*>(other)->IsGameMaster())
        return true;

    switch (sWorld.getConfig(CONFIG_UINT32_FOGOFWAR_HEALTH))
    {
        default: return IsInGroup(other, false, true);
        case 1:  return IsInTeam(other);
        case 2:  return true;
    }
}

/////////////////////////////////////////////////
/// [Serverside] Fog of War: Unit's stat values can be seen by other unit
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// A helper function to determine if unit's stat values are always visible to another unit.
/////////////////////////////////////////////////
bool Unit::IsFogOfWarVisibleStats(Unit const* other) const
{
    MANGOS_ASSERT(other)

    // Gamemasters can see stat values
    if (other->GetTypeId() == TYPEID_PLAYER && static_cast<Player const*>(other)->IsGameMaster())
        return true;

    switch (sWorld.getConfig(CONFIG_UINT32_FOGOFWAR_STATS))
    {
        default: return (this == other || GetSummonerGuid() == other->GetObjectGuid());
        case 1:  return IsInTeam(other);
        case 2:  return true;
    }
}

/////////////////////////////////////////////////
/// [Serverside] Guild: Unit counts as being placed in the same guild with another unit (for gameplay purposes)
///
/// @note Relations API Tier 3
///
/// Loosely inspired by client-side Lua script counterpart: <tt>UnitIsInMyGuild()</tt>
/// Additionally contains optional detection of same guild from UI standpoint (ignoring charms).
/////////////////////////////////////////////////
bool Unit::IsInGuild(Unit const* other, bool ignoreCharms/* = false*/) const
{
    MANGOS_ASSERT(other)

    // Same unit is always in guild with itself
    if (this == other)
        return true;

    // Only player controlled
    if (this->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED) && other->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
    {
        // Check if controlling players are in the same group (same logic as client, but not local)
        if (const Player* thisPlayer = GetControllingPlayer(ignoreCharms))
        {
            if (const Player* otherPlayer = other->GetControllingPlayer(ignoreCharms))
                return (thisPlayer == otherPlayer || (thisPlayer->GetGuildId() == otherPlayer->GetGuildId()));
        }
    }

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Team: Check if both units are in the same faction team (for gameplay purposes)
///
/// @note Relations API Tier 3
///
/// Loosely inspired by client-side Lua script counterpart: <tt>UnitFactionGroup()</tt>
/// Additionally contains optional detection of same team temporarily with taking charms in account.
bool Unit::IsInTeam(const Unit *other, bool ignoreCharms) const
{
    MANGOS_ASSERT(other)

    // Same unit is always in team with itself
    if (this == other)
        return true;

    // Only player controlled
    if (this->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED) && other->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_PLAYER_CONTROLLED))
    {
        // Check if controlling players are in the same group (same logic as client, but not local)
        if (const Player* thisPlayer = GetControllingPlayer(ignoreCharms))
        {
            if (const Player* otherPlayer = other->GetControllingPlayer(ignoreCharms))
                return (thisPlayer == otherPlayer || (thisPlayer->GetTeam() == otherPlayer->GetTeam()));
        }
    }

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: this can assist who in attacking enemy
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// A helper function used to determine if current unit can assist who against enemy
/// Used in several assistance checks
/////////////////////////////////////////////////
bool Unit::CanAssistInCombatAgainst(Unit const* who, Unit const* enemy) const
{
    MANGOS_ASSERT(who)
    MANGOS_ASSERT(enemy)

    if (GetMap()->Instanceable()) // in dungeons nothing else needs to be evaluated
        return CanJoinInAttacking(enemy);

    if (IsInCombat()) // if fighting something else, do not assist
        return false;

    if (CanAssist(who) && CanAttackOnSight(enemy))
        return true;

    return false;
}

/////////////////////////////////////////////////
/// [Serverside] Opposition: this can join combat against enemy
///
/// @note Relations API Tier 3
///
/// This function is not intented to have client-side counterpart by original design.
/// A helper function used to determine if current unit can join combat against enemy
/// Used in several assistance checks
/////////////////////////////////////////////////
bool Unit::CanJoinInAttacking(Unit const* enemy) const
{
    if (!CanEnterCombat())
        return false;

    if (!CanInitiateAttack())
        return false;

    if (IsFeigningDeathSuccessfully())
        return false;

    if (HasAuraType(SPELL_AURA_MOD_UNATTACKABLE))
        return false;

    if (!CanAttack(enemy))
        return false;

    return true;
}
