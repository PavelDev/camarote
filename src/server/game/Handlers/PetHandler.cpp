/*
 * Copyright (C) 2008-2013 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "Common.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "SpellMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "Spell.h"
#include "ObjectAccessor.h"
#include "CreatureAI.h"
#include "Util.h"
#include "Pet.h"
#include "World.h"
#include "Group.h"
#include "SpellInfo.h"
#include "SpellAuraEffects.h"

void WorldSession::HandleDismissCritter(WorldPacket& recvData)
{
    uint64 guid;
    recvData >> guid;

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_DISMISS_CRITTER for GUID " UI64FMTD, guid);

    Unit* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!pet)
    {
        TC_LOG_DEBUG("network", "Vanitypet (guid: %u) does not exist - player '%s' (guid: %u / account: %u) attempted to dismiss it (possibly lagged out)",
                uint32(GUID_LOPART(guid)), GetPlayer()->GetName().c_str(), GetPlayer()->GetGUIDLow(), GetAccountId());
        return;
    }

    if (_player->GetCritterGUID() == pet->GetGUID())
    {
         if (pet->GetTypeId() == TYPEID_UNIT && pet->ToCreature()->IsSummon())
             pet->ToTempSummon()->UnSummon();
    }
}

void WorldSession::HandlePetAction(WorldPacket & recvData)
{
    ObjectGuid guid1;
    ObjectGuid guid2;
    uint32 data;
    float x, y, z;
    recvData >> data;

    // Position
    recvData >> x;
    recvData >> z;
    recvData >> y;

    recvData.ReadBitSeq<0, 2, 6, 1>(guid1);
    recvData.ReadBitSeq<6, 4, 0, 1, 3>(guid2);
    recvData.ReadBitSeq<3>(guid1);
    recvData.ReadBitSeq<2>(guid2);
    recvData.ReadBitSeq<4>(guid1);
    recvData.ReadBitSeq<5, 7>(guid2);
    recvData.ReadBitSeq<5, 7>(guid1);

    recvData.ReadByteSeq<7>(guid2);
    recvData.ReadByteSeq<7>(guid1);
    recvData.ReadByteSeq<6, 0, 3>(guid2);
    recvData.ReadByteSeq<1>(guid1);
    recvData.ReadByteSeq<2, 1>(guid2);
    recvData.ReadByteSeq<2, 5, 6, 0, 3>(guid1);
    recvData.ReadByteSeq<4, 5>(guid2);
    recvData.ReadByteSeq<4>(guid1);

    uint32 spellid = UNIT_ACTION_BUTTON_ACTION(data);
    uint8 flag = UNIT_ACTION_BUTTON_TYPE(data);             //delete = 0x07 CastSpell = C1

    // used also for charmed creature
    Unit* pet= ObjectAccessor::GetUnit(*_player, guid1);
    TC_LOG_INFO("network", "HandlePetAction: Pet %u - flag: %u, spellid: %u, target: %u.",
                uint32(GUID_LOPART(guid1)), uint32(flag), spellid, uint32(GUID_LOPART(guid2)));

    if (!pet)
    {
        TC_LOG_ERROR("network", "HandlePetAction: Pet (GUID: %u) doesn't exist for player '%s'",
                     uint32(GUID_LOPART(guid1)), GetPlayer()->GetName().c_str());
        return;
    }

    if (pet != GetPlayer()->GetFirstControlled())
    {
        TC_LOG_ERROR("network", "HandlePetAction: Pet (GUID: %u) does not belong to player '%s'",
                     uint32(GUID_LOPART(guid1)), GetPlayer()->GetName().c_str());
        return;
    }

    if (!pet->IsAlive())
    {
        SpellInfo const* spell = (flag == ACT_ENABLED || flag == ACT_PASSIVE) ? sSpellMgr->GetSpellInfo(spellid) : NULL;
        if (!spell)
            return;
        if (!(spell->Attributes & SPELL_ATTR0_CASTABLE_WHILE_DEAD))
            return;
    }

    //TODO: allow control charmed player?
    if (pet->GetTypeId() == TYPEID_PLAYER && !(flag == ACT_COMMAND && spellid == COMMAND_ATTACK))
        return;

    if (GetPlayer()->m_Controlled.size() == 1)
        HandlePetActionHelper(pet, guid1, spellid, flag, guid2, x, y ,z);
    else
    {
        //If a pet is dismissed, m_Controlled will change
        std::vector<Unit*> controlled;
        for (Unit::ControlList::iterator itr = GetPlayer()->m_Controlled.begin(); itr != GetPlayer()->m_Controlled.end(); ++itr)
            if ((*itr)->GetEntry() == pet->GetEntry() && (*itr)->IsAlive())
                controlled.push_back(*itr);
        for (std::vector<Unit*>::iterator itr = controlled.begin(); itr != controlled.end(); ++itr)
            HandlePetActionHelper(*itr, guid1, spellid, flag, guid2, x, y, z);
    }
}

void WorldSession::HandlePetStopAttack(WorldPacket &recvData)
{
    ObjectGuid guid;

    recvData.ReadBitSeq<4, 2, 0, 6, 1, 7, 5, 3>(guid);
    recvData.ReadByteSeq<3, 2, 4, 0, 7, 6, 5, 1>(guid);

    TC_LOG_DEBUG("network", "WORLD: Received CMSG_PET_STOP_ATTACK");

    Unit* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!pet)
    {
        TC_LOG_ERROR("network", "HandlePetStopAttack: Pet %u does not exist", uint32(GUID_LOPART(guid)));
        return;
    }

    if (pet != GetPlayer()->GetPet() && pet != GetPlayer()->GetCharm())
    {
        TC_LOG_ERROR("network", "HandlePetStopAttack: Pet GUID %u isn't a pet or charmed creature of player %s",
                     uint32(GUID_LOPART(guid)), GetPlayer()->GetName().c_str());
        return;
    }

    if (!pet->IsAlive())
        return;

    pet->AttackStop();
}

void WorldSession::HandlePetActionHelper(Unit* pet, uint64 guid1, uint32 spellid, uint16 flag, uint64 guid2, float x, float y, float z)
{
    CharmInfo* charmInfo = pet->GetCharmInfo();
    if (!charmInfo)
    {
        TC_LOG_ERROR("network", "WorldSession::HandlePetAction(petGuid: " UI64FMTD ", tagGuid: " UI64FMTD ", spellId: %u, flag: %u): object (entry: %u TypeId: %u) is considered pet-like but doesn't have a charminfo!",
            guid1, guid2, spellid, flag, pet->GetGUIDLow(), pet->GetTypeId());
        return;
    }

    switch (flag)
    {
        case ACT_COMMAND:                                   //0x07
            switch (spellid)
            {
                case COMMAND_STAY:                          //flat=1792  //STAY
                    pet->StopMoving();
                    pet->GetMotionMaster()->Clear(false);
                    pet->GetMotionMaster()->MoveIdle();
                    charmInfo->SetCommandState(COMMAND_STAY);

                    charmInfo->SetIsCommandAttack(false);
                    charmInfo->SetIsAtStay(true);
                    charmInfo->SetIsFollowing(false);
                    charmInfo->SetIsReturning(false);
                    charmInfo->SaveStayPosition();
                    break;
                case COMMAND_FOLLOW:                        //spellid=1792  //FOLLOW
                    pet->AttackStop();
                    pet->InterruptNonMeleeSpells(false);
                    pet->GetMotionMaster()->MoveFollow(_player, PET_FOLLOW_DIST, pet->GetFollowAngle());
                    charmInfo->SetCommandState(COMMAND_FOLLOW);

                    charmInfo->SetIsCommandAttack(false);
                    charmInfo->SetIsAtStay(false);
                    charmInfo->SetIsReturning(true);
                    charmInfo->SetIsFollowing(false);
                    break;
                case COMMAND_ATTACK:                        //spellid=1792  //ATTACK
                {
                    // Can't attack if owner is pacified
                    if (_player->HasAuraType(SPELL_AURA_MOD_PACIFY))
                    {
                        //pet->SendPetCastFail(spellid, SPELL_FAILED_PACIFIED);
                        //TODO: Send proper error message to client
                        return;
                    }

                    // only place where pet can be player
                    Unit* TargetUnit = ObjectAccessor::GetUnit(*_player, guid2);
                    if (!TargetUnit)
                        return;

                    if (Unit* owner = pet->GetOwner())
                        if (!owner->IsValidAttackTarget(TargetUnit))
                            return;

                    // Not let attack through obstructions
                    if (sWorld->getBoolConfig(CONFIG_PET_LOS))
                    {
                        if (!pet->IsWithinLOSInMap(TargetUnit))
                            return;
                    }

                    pet->ClearUnitState(UNIT_STATE_FOLLOW);
                    // This is true if pet has no target or has target but targets differs.
                    if (pet->GetVictim() != TargetUnit || (pet->GetVictim() == TargetUnit && !pet->GetCharmInfo()->IsCommandAttack()))
                    {
                        if (pet->GetVictim())
                            pet->AttackStop();

                        // Summon gargoyle should attack the same target as ghoul
                        if (Unit* owner = pet->GetOwner())
                        {
                            if (owner->getClass() == CLASS_DEATH_KNIGHT)
                            {
                                for (Unit::ControlList::iterator itr = owner->m_Controlled.begin(); itr != owner->m_Controlled.end(); ++itr)
                                {
                                    if ((*itr)->GetEntry() == 27829 && !(*itr)->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_NON_ATTACKABLE))
                                    {
                                        owner->AddAura(49206, TargetUnit);
                                        break;
                                    }
                                }
                            }
                        }

                        if (pet->GetTypeId() != TYPEID_PLAYER && pet->ToCreature()->IsAIEnabled)
                        {
                            charmInfo->SetIsCommandAttack(true);
                            charmInfo->SetIsAtStay(false);
                            charmInfo->SetIsFollowing(false);
                            charmInfo->SetIsReturning(false);

                            pet->ToCreature()->AI()->AttackStart(TargetUnit);
                            if (Unit* owner = pet->GetOwner())
                                if (Player* playerOwner = owner->ToPlayer())
                                    for (Unit::ControlList::iterator itr = playerOwner->m_Controlled.begin(); itr != playerOwner->m_Controlled.end(); ++itr)
                                        if ((*itr)->isGuardian() && (*itr)->IsAIEnabled)
                                            (*itr)->ToCreature()->AI()->AttackStart(TargetUnit);

                            //10% chance to play special pet attack talk, else growl
                            if (pet->ToCreature()->isPet() && ((Pet*)pet)->getPetType() == SUMMON_PET && pet != TargetUnit && urand(0, 100) < 10)
                                pet->SendPetTalk((uint32)PET_TALK_ATTACK);
                            else
                            {
                                // 90% chance for pet and 100% chance for charmed creature
                                pet->SendPetAIReaction(guid1);
                            }
                        }
                        else                                // charmed player
                        {
                            if (pet->GetVictim() && pet->GetVictim() != TargetUnit)
                                pet->AttackStop();

                            charmInfo->SetIsCommandAttack(true);
                            charmInfo->SetIsAtStay(false);
                            charmInfo->SetIsFollowing(false);
                            charmInfo->SetIsReturning(false);

                            pet->Attack(TargetUnit, true);
                            pet->SendPetAIReaction(guid1);
                        }
                    }
                    break;
                }
                case COMMAND_ABANDON:                       // abandon (hunter pet) or dismiss (summoned pet)
                    if (pet->GetCharmerGUID() == GetPlayer()->GetGUID())
                        _player->StopCastingCharm();
                    else if (pet->GetOwnerGUID() == GetPlayer()->GetGUID())
                    {
                        ASSERT(pet->GetTypeId() == TYPEID_UNIT);
                        if (pet->isPet())
                        {
                            PetRemoveMode removeMode = (static_cast<Pet*>(pet)->getPetType() == HUNTER_PET)
                                    ? PET_REMOVE_ABANDON
                                    : PET_REMOVE_DISMISS;
                            GetPlayer()->RemovePet(removeMode, PET_REMOVE_FLAG_RESET_CURRENT);
                        }
                        else if (pet->HasUnitTypeMask(UNIT_MASK_MINION))
                        {
                            ((Minion*)pet)->UnSummon();
                        }
                    }
                    break;
                case COMMAND_MOVE_TO:
                    pet->StopMoving();
                    pet->GetMotionMaster()->Clear(false);
                    pet->GetMotionMaster()->MoveIdle();
                    pet->GetMotionMaster()->MovePoint(0, x, y, z);

                    charmInfo->SetCommandState(COMMAND_MOVE_TO);
                    charmInfo->SetIsCommandAttack(false);
                    charmInfo->SetIsAtStay(true);
                    charmInfo->SetIsFollowing(false);
                    charmInfo->SetIsReturning(false);
                    charmInfo->SaveStayPosition();
                    break;
                default:
                    TC_LOG_ERROR("network", "WORLD: unknown PET flag Action %i and spellid %i.", uint32(flag), spellid);
            }
            break;
        case ACT_REACTION:                                  // 0x6
            switch (spellid)
            {
                case REACT_PASSIVE:                         //passive
                    pet->AttackStop();
                    break;
                case REACT_DEFENSIVE:                       //recovery
                case REACT_AGGRESSIVE:                      //activete
                case REACT_ASSIST:
                    if (pet->GetTypeId() == TYPEID_UNIT)
                        pet->ToCreature()->SetReactState(ReactStates(spellid));
                    break;
                default:
                    break;
            }
            break;
        case ACT_DISABLED:                                  // 0x81    spell (disabled), ignore
        case ACT_PASSIVE:                                   // 0x01
        case ACT_ENABLED:                                   // 0xC1    spell
        {
            Unit* unit_target = NULL;

            if (guid2)
                unit_target = ObjectAccessor::GetUnit(*_player, guid2);

            // do not cast unknown spells
            SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellid);
            if (!spellInfo)
            {
                TC_LOG_ERROR("network", "WORLD: unknown PET spell id %i", spellid);
                return;
            }

            if (spellInfo->StartRecoveryCategory > 0)
                if (pet->GetCharmInfo() && pet->GetCharmInfo()->GetGlobalCooldownMgr().HasGlobalCooldown(pet, spellInfo))
                    return;

            for (auto const &spellEffect : spellInfo->Effects)
            {
                if (spellEffect.TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY
                        || spellEffect.TargetA.GetTarget() == TARGET_UNIT_DEST_AREA_ENEMY
                        || spellEffect.TargetA.GetTarget() == TARGET_DEST_DYNOBJ_ENEMY)
                {
                    return;
                }
            }

            // do not cast not learned spells
            if (!pet->HasSpell(spellid) || spellInfo->IsPassive())
                return;

            //  Clear the flags as if owner clicked 'attack'. AI will reset them
            //  after AttackStart, even if spell failed
            if (pet->GetCharmInfo())
            {
                pet->GetCharmInfo()->SetIsAtStay(false);
                pet->GetCharmInfo()->SetIsCommandAttack(true);
                pet->GetCharmInfo()->SetIsReturning(false);
                pet->GetCharmInfo()->SetIsFollowing(false);
            }

            Spell* spell = new Spell(pet, spellInfo, TRIGGERED_NONE);

            SpellCastResult result = spell->CheckPetCast(unit_target);

            //auto turn to target unless possessed
            if (result == SPELL_FAILED_UNIT_NOT_INFRONT && !pet->isPossessed() && !pet->IsVehicle())
            {
                if (unit_target)
                {
                    pet->SetInFront(unit_target);
                    if (unit_target->GetTypeId() == TYPEID_PLAYER)
                        pet->SendUpdateToPlayer((Player*)unit_target);
                }
                else if (Unit* unit_target2 = spell->m_targets.GetUnitTarget())
                {
                    pet->SetInFront(unit_target2);
                    if (unit_target2->GetTypeId() == TYPEID_PLAYER)
                        pet->SendUpdateToPlayer((Player*)unit_target2);
                }

                if (Unit* powner = pet->GetCharmerOrOwner())
                    if (powner->GetTypeId() == TYPEID_PLAYER)
                        pet->SendUpdateToPlayer(powner->ToPlayer());

                result = SPELL_CAST_OK;
            }

            if (result == SPELL_CAST_OK)
            {
                pet->ToCreature()->AddCreatureSpellCooldown(spellid);

                unit_target = spell->m_targets.GetUnitTarget();

                //10% chance to play special pet attack talk, else growl
                //actually this only seems to happen on special spells, fire shield for imp, torment for voidwalker, but it's stupid to check every spell
                if (pet->ToCreature()->isPet() && (((Pet*)pet)->getPetType() == SUMMON_PET) && (pet != unit_target) && (urand(0, 100) < 10))
                    pet->SendPetTalk((uint32)PET_TALK_SPECIAL_SPELL);
                else
                {
                    pet->SendPetAIReaction(guid1);
                }

                if (unit_target && !GetPlayer()->IsFriendlyTo(unit_target) && !pet->isPossessed() && !pet->IsVehicle())
                {
                    // This is true if pet has no target or has target but targets differs.
                    if (pet->GetVictim() != unit_target)
                    {
                        if (pet->GetVictim())
                            pet->AttackStop();
                        pet->GetMotionMaster()->Clear();
                        if (pet->ToCreature()->IsAIEnabled)
                            pet->ToCreature()->AI()->AttackStart(unit_target);
                    }
                }

                spell->prepare(&(spell->m_targets));
            }
            else
            {
                if (pet->isPossessed() || pet->IsVehicle())
                    Spell::SendCastResult(GetPlayer(), spellInfo, NULL, 0, result);
                else
                    pet->SendPetCastFail(spellid, result);

                if (!pet->ToCreature()->HasSpellCooldown(spellid))
                    GetPlayer()->SendClearCooldown(spellid, pet);

                spell->finish(false);
                delete spell;

                // reset specific flags in case of spell fail. AI will reset other flags
                if (pet->GetCharmInfo())
                    pet->GetCharmInfo()->SetIsCommandAttack(false);
            }
            break;
        }
        default:
            TC_LOG_ERROR("network", "WORLD: unknown PET flag Action %i and spellid %i.", uint32(flag), spellid);
    }
}

void WorldSession::HandlePetNameQuery(WorldPacket & recvData)
{
    TC_LOG_INFO("network", "HandlePetNameQuery. CMSG_PET_NAME_QUERY");

    ObjectGuid petGuid;
    ObjectGuid petNumber;

    recvData.ReadBitSeq<5>(petGuid);
    recvData.ReadBitSeq<3>(petNumber);
    recvData.ReadBitSeq<6>(petGuid);
    recvData.ReadBitSeq<5, 7>(petNumber);
    recvData.ReadBitSeq<2, 4>(petGuid);
    recvData.ReadBitSeq<2>(petNumber);
    recvData.ReadBitSeq<3>(petGuid);
    recvData.ReadBitSeq<1>(petNumber);
    recvData.ReadBitSeq<7>(petGuid);
    recvData.ReadBitSeq<6>(petNumber);
    recvData.ReadBitSeq<1, 0>(petGuid);
    recvData.ReadBitSeq<4, 0>(petNumber);

    recvData.ReadByteSeq<5>(petNumber);
    recvData.ReadByteSeq<4, 3>(petGuid);
    recvData.ReadByteSeq<7, 4>(petNumber);
    recvData.ReadByteSeq<5, 2, 0, 6>(petGuid);
    recvData.ReadByteSeq<2, 0, 6>(petNumber);
    recvData.ReadByteSeq<1>(petGuid);
    recvData.ReadByteSeq<3>(petNumber);
    recvData.ReadByteSeq<7>(petGuid);
    recvData.ReadByteSeq<1>(petNumber);

    SendPetNameQuery(petNumber, petGuid);
}

void WorldSession::SendPetNameQuery(uint32 petnumber, uint64 petguid)
{
    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, petguid);
    if (!pet)
    {
        WorldPacket data(SMSG_PET_NAME_QUERY_RESPONSE);
        data << uint64(petnumber);
        data.WriteBit(0);
        data.FlushBits();
        _player->GetSession()->SendPacket(&data);
        return;
    }

    std::string name = pet->GetName();

    WorldPacket data(SMSG_PET_NAME_QUERY_RESPONSE);

    data << uint64(petnumber);
    data.WriteBit(pet->isPet() ? 1 : 0);

    if (Pet* playerPet = pet->ToPet())
    {
        DeclinedName const* declinedNames = playerPet->GetDeclinedNames();
        if (declinedNames)
        {
            for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
                data.WriteBits(declinedNames->name[i].size(), 7);
        }
        else
        {
            for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
                data.WriteBits(0, 7);
        }

        data.WriteBit(0);   // Unk bit
        data.WriteBits(name.size(), 8);
        data.FlushBits();

        if (declinedNames)
        {
            for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
                data.WriteString(declinedNames->name[i]);
        }

        data << uint32(playerPet->GetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP));
        data.WriteString(name);
    }
    else
    {
        data.FlushBits();
    }

    _player->GetSession()->SendPacket(&data);
}

bool WorldSession::CheckStableMaster(uint64 guid)
{
    // spell case or GM
    if (guid == GetPlayer()->GetGUID())
    {
        if (!GetPlayer()->isGameMaster() && !GetPlayer()->HasAuraType(SPELL_AURA_OPEN_STABLE))
        {
            TC_LOG_DEBUG("network", "Player (GUID:%u) attempt open stable in cheating way.", GUID_LOPART(guid));
            return false;
        }
    }
    // stable master case
    else
    {
        if (!GetPlayer()->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_STABLEMASTER))
        {
            TC_LOG_DEBUG("network", "Stablemaster (GUID:%u) not found or you can't interact with him.", GUID_LOPART(guid));
            return false;
        }
    }
    return true;
}

void WorldSession::HandlePetSetAction(WorldPacket & recvData)
{
    TC_LOG_INFO("network", "HandlePetSetAction. CMSG_PET_SET_ACTION");

    ObjectGuid petguid;
    uint32 position;
    uint32 data;

    recvData >> data >> position;
    recvData.ReadBitSeq<1, 7, 3, 5, 2, 6, 4, 0>(petguid);
    recvData.ReadByteSeq<0, 1, 2, 3, 7, 4, 6, 5>(petguid);

    Unit* pet = ObjectAccessor::GetUnit(*_player, petguid);

    if (!pet || pet != _player->GetFirstControlled())
    {
        TC_LOG_ERROR("network", "HandlePetSetAction: Unknown pet (GUID: %u) or pet owner (GUID: %u)", GUID_LOPART(petguid), _player->GetGUIDLow());
        return;
    }

    CharmInfo* charmInfo = pet->GetCharmInfo();
    if (!charmInfo)
    {
        TC_LOG_ERROR("network", "WorldSession::HandlePetSetAction: object (GUID: %u TypeId: %u) is considered pet-like but doesn't have a charminfo!", pet->GetGUIDLow(), pet->GetTypeId());
        return;
    }

    uint8 act_state = UNIT_ACTION_BUTTON_TYPE(data);

    //ignore invalid position
    if (position >= MAX_UNIT_ACTION_BAR_INDEX)
        return;

    uint8 act_state_0 = UNIT_ACTION_BUTTON_TYPE(data);
    if ((act_state_0 == ACT_COMMAND && UNIT_ACTION_BUTTON_ACTION(data) != COMMAND_MOVE_TO) || act_state_0 == ACT_REACTION)
    {
        uint32 spell_id_0 = UNIT_ACTION_BUTTON_ACTION(data);
        UnitActionBarEntry const* actionEntry_1 = charmInfo->GetActionBarEntry(position);
        if (!actionEntry_1 || spell_id_0 != actionEntry_1->GetAction() ||
            act_state_0 != actionEntry_1->GetType())
            return;
    }

    uint32 spell_id = UNIT_ACTION_BUTTON_ACTION(data);
    //uint8 act_state = UNIT_ACTION_BUTTON_TYPE(data);

    TC_LOG_INFO("network", "Player %s has changed pet spell action. Position: %u, Spell: %u, State: 0x%X",
                _player->GetName().c_str(), position, spell_id, uint32(act_state));

    //if it's act for spell (en/disable/cast) and there is a spell given (0 = remove spell) which pet doesn't know, don't add
    if (!((act_state == ACT_ENABLED || act_state == ACT_DISABLED || act_state == ACT_PASSIVE) && spell_id && !pet->HasSpell(spell_id)))
    {
        if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell_id))
        {
            //sign for autocast
            if (act_state == ACT_ENABLED)
            {
                if (pet->GetTypeId() == TYPEID_UNIT && pet->ToCreature()->isPet())
                    ((Pet*)pet)->ToggleAutocast(spellInfo, true);
                else
                    for (Unit::ControlList::iterator itr = GetPlayer()->m_Controlled.begin(); itr != GetPlayer()->m_Controlled.end(); ++itr)
                        if ((*itr)->GetEntry() == pet->GetEntry())
                            (*itr)->GetCharmInfo()->ToggleCreatureAutocast(spellInfo, true);
            }
            //sign for no/turn off autocast
            else if (act_state == ACT_DISABLED)
            {
                if (pet->GetTypeId() == TYPEID_UNIT && pet->ToCreature()->isPet())
                    ((Pet*)pet)->ToggleAutocast(spellInfo, false);
                else
                    for (Unit::ControlList::iterator itr = GetPlayer()->m_Controlled.begin(); itr != GetPlayer()->m_Controlled.end(); ++itr)
                        if ((*itr)->GetEntry() == pet->GetEntry())
                            (*itr)->GetCharmInfo()->ToggleCreatureAutocast(spellInfo, false);
            }
        }

        charmInfo->SetActionBar(position, spell_id, ActiveStates(act_state));
    }
}

void WorldSession::HandlePetRename(WorldPacket & recvData)
{
    TC_LOG_INFO("network", "HandlePetRename. CMSG_PET_RENAME");

    std::string name;
    DeclinedName declinedname;
    uint8 declinedNameLength[MAX_DECLINED_NAME_CASES] = {0, 0, 0, 0, 0};

    recvData.read_skip<uint32>();   // unk, client send 2048, maybe flags ?
    bool hasName = !recvData.ReadBit();
    bool isdeclined = recvData.ReadBit();

    if (isdeclined)
        for(int i = 0; i < MAX_DECLINED_NAME_CASES; i++)
            declinedNameLength[i] = recvData.ReadBits(7);

    if (hasName)
    {
        uint8 nameLenght = recvData.ReadBits(8);
        name = recvData.ReadString(nameLenght);
    }

    Pet* pet = GetPlayer()->GetPet();
                                                            // check it!
    if (!pet || !pet->isPet() || pet->getPetType() != HUNTER_PET
            || !pet->HasByteFlag(UNIT_FIELD_BYTES_2, 2, UNIT_CAN_BE_RENAMED)
            || pet->GetOwnerGUID() != _player->GetGUID()
            || !pet->GetCharmInfo())
    {
        return;
    }

    PetNameInvalidReason res = ObjectMgr::CheckPetName(name);
    if (res != PET_NAME_SUCCESS)
    {
        SendPetNameInvalid(res, name, NULL);
        return;
    }

    if (sObjectMgr->IsReservedName(name))
    {
        SendPetNameInvalid(PET_NAME_RESERVED, name, NULL);
        return;
    }

    pet->SetName(name);

    Player* owner = pet->GetOwner();
    if (owner && owner->GetGroup())
        owner->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_NAME);

    pet->RemoveByteFlag(UNIT_FIELD_BYTES_2, 2, UNIT_CAN_BE_RENAMED);

    if (isdeclined)
    {
        for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            declinedname.name[i] = recvData.ReadString(declinedNameLength[i]);

        std::wstring wname;
        Utf8toWStr(name, wname);
        if (!ObjectMgr::CheckDeclinedNames(wname, declinedname))
        {
            SendPetNameInvalid(PET_NAME_DECLENSION_DOESNT_MATCH_BASE_NAME, name, &declinedname);
            return;
        }
    }

    SQLTransaction trans = CharacterDatabase.BeginTransaction();
    if (isdeclined)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_PET_DECLINED_NAME);
        stmt->setUInt32(0, pet->GetCharmInfo()->GetPetNumber());
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_ADD_PET_DECLINED_NAME);
        stmt->setUInt32(0, _player->GetGUIDLow());

        for (uint8 i = 0; i < 5; i++)
            stmt->setString(i+1, declinedname.name[i]);

        trans->Append(stmt);
    }

    CharacterDatabase.CommitTransaction(trans);

    pet->SavePetToDB();

    pet->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, uint32(time(NULL))); // cast can't be helped
}

void WorldSession::HandlePetAbandon(WorldPacket& recvData)
{
    ObjectGuid guid;

    recvData.ReadBitSeq<7, 0, 6, 3, 1, 2, 4, 5>(guid);
    recvData.ReadByteSeq<7, 4, 3, 0, 2, 6, 5, 1>(guid);

    TC_LOG_INFO("network", "HandlePetAbandon. CMSG_PET_ABANDON pet guid is %u", GUID_LOPART(guid));

    if (!_player->IsInWorld())
        return;

    // pet/charmed
    Creature* pet = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);
    if (pet)
    {
        if (pet->isPet()) {
            _player->RemovePet(PET_REMOVE_ABANDON);
            if (GetPlayer()->getClass() == CLASS_HUNTER)
                SendPetList(0, PET_SLOT_ACTIVE_FIRST, PET_SLOT_ACTIVE_LAST);
        } else if (pet->GetGUID() == _player->GetCharmGUID()) {
            _player->StopCastingCharm();
        }
    }
}

void WorldSession::HandlePetCastSpellOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_PET_CAST_SPELL");

    ObjectGuid casterGUID;
    ObjectGuid unkGUID1;
    ObjectGuid transportDstGUID;
    ObjectGuid transportSrcGUID;
    ObjectGuid targetGUID;
    ObjectGuid unkGUID2;
    bool hasDestPos;
    bool hasSrcPos;
    bool hasSpeed;
    bool hasSpell;
    bool hasGlyphIndex;
    bool hasTargetFlags;
    bool hasElevation;
    bool hasString;
    bool hasCastCount;
    bool hasUnk5bits;
    uint32 archeologyCounter = 0;
    WorldLocation dstLoc, srcLoc;
    float speed = 0.0f;
    float elevation = 0.0f;
    uint32 targetFlags = 0;
    uint32 spellID = 0;
    uint32 stringLenght = 0;
    uint8 castCount = 0;

    recvPacket.ReadBitSeq<3, 5>(casterGUID);
    hasDestPos = recvPacket.ReadBit();
    recvPacket.ReadBit();                   // unk bit
    hasSpeed = !recvPacket.ReadBit();
    hasSrcPos = recvPacket.ReadBit();
    hasSpell = !recvPacket.ReadBit();
    recvPacket.ReadBitSeq<0>(casterGUID);
    hasGlyphIndex = !recvPacket.ReadBit();
    recvPacket.ReadBitSeq<7>(casterGUID);
    hasTargetFlags = !recvPacket.ReadBit();
    hasElevation = !recvPacket.ReadBit();
    recvPacket.ReadBit();                   // has movement info
    hasString = !recvPacket.ReadBit();
    recvPacket.ReadBit();                   // !inverse bit, unk
    hasCastCount = !recvPacket.ReadBit();
    recvPacket.ReadBitSeq<2, 4>(casterGUID);
    archeologyCounter = recvPacket.ReadBits(2);
    recvPacket.ReadBitSeq<1>(casterGUID);
    hasUnk5bits = !recvPacket.ReadBit();

    for (uint32 i = 0; i < archeologyCounter; i++)
        recvPacket.ReadBits(2);             // archeology type

    recvPacket.ReadBitSeq<6>(casterGUID);

    if (hasDestPos)
        recvPacket.ReadBitSeq<2, 7, 4, 0, 1, 6, 5, 3>(transportDstGUID);

    // movement block (disabled by patch client-side)

    if (hasSrcPos)
        recvPacket.ReadBitSeq<6, 2, 3, 1, 5, 4, 0, 7>(transportSrcGUID);

    if (hasUnk5bits)
        recvPacket.ReadBits(5);             // unk 5 bits

    // Target GUID
    recvPacket.ReadBitSeq<3, 5, 6, 2, 4, 1, 7, 0>(targetGUID);

    // unkGUID1
    recvPacket.ReadBitSeq<3, 1, 5, 2, 4, 7, 0, 6>(unkGUID1);

    if (hasTargetFlags)
        targetFlags = recvPacket.ReadBits(20);

    if (hasString)
        stringLenght = recvPacket.ReadBits(7);

    recvPacket.ReadByteSeq<0, 4, 5, 1, 2, 3, 7>(casterGUID);

    for (uint32 i = 0; i < archeologyCounter; i++)
    {
        recvPacket.read_skip<uint32>(); // entry
        recvPacket.read_skip<uint32>(); // counter
    }

    recvPacket.ReadByteSeq<6>(casterGUID);
    recvPacket.ReadByteSeq<1, 5, 4, 2, 7, 3, 0>(unkGUID1);

    if (hasSrcPos)
    {
        recvPacket.ReadByteSeq<4>(transportSrcGUID);
        srcLoc.m_positionY = recvPacket.read<float>();
        recvPacket.ReadByteSeq<2, 6>(transportSrcGUID);
        srcLoc.m_positionZ = recvPacket.read<float>();
        srcLoc.m_positionX = recvPacket.read<float>();
        recvPacket.ReadByteSeq<1, 3, 5, 7, 0>(transportSrcGUID);
    }

    // Target GUID
    recvPacket.ReadByteSeq<7, 4, 2, 6, 3, 0, 5, 1>(targetGUID);

    if (hasDestPos)
    {
        dstLoc.m_positionX = recvPacket.read<float>();
        recvPacket.ReadByteSeq<5, 7, 2, 0, 1, 3, 6>(transportDstGUID);
        dstLoc.m_positionZ = recvPacket.read<float>();
        dstLoc.m_positionY = recvPacket.read<float>();
        recvPacket.ReadByteSeq<4>(transportDstGUID);
    }

    if (hasGlyphIndex)
        recvPacket.read_skip<uint32>();     // glyph index

    if (hasElevation)
        elevation = recvPacket.read<float>();

    if (hasSpell)
        spellID = recvPacket.read<uint32>();

    if (hasCastCount)
        castCount = recvPacket.read<uint8>();

    if (hasString)
        recvPacket.ReadString(stringLenght);

    if (hasSpeed)
        speed = recvPacket.read<float>();

    TC_LOG_DEBUG("network", "WORLD: CMSG_PET_CAST_SPELL, castCount: %u, spellId %u, targetFlags %u", castCount, spellID, targetFlags);

    // This opcode is also sent from charmed and possessed units (players and creatures)
    if (!_player->GetGuardianPet() && !_player->GetCharm())
        return;

    Unit* caster = ObjectAccessor::GetUnit(*_player, casterGUID);

    if (!caster || (caster != _player->GetGuardianPet() && caster != _player->GetCharm()))
    {
        TC_LOG_ERROR("network", "HandlePetCastSpellOpcode: Pet %u isn't pet of player %s .", uint32(GUID_LOPART(casterGUID)), GetPlayer()->GetName().c_str());
        return;
    }

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellID);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown PET spell id %i", spellID);
        return;
    }

    if (spellInfo->StartRecoveryCategory > 0) // Check if spell is affected by GCD
        if (caster->GetTypeId() == TYPEID_UNIT && caster->GetCharmInfo() && caster->GetCharmInfo()->GetGlobalCooldownMgr().HasGlobalCooldown(caster, spellInfo))
        {
            caster->SendPetCastFail(spellID, SPELL_FAILED_NOT_READY);
            return;
        }

    // do not cast not learned spells
    if (!caster->HasSpell(spellID) || spellInfo->IsPassive())
        return;

    SpellCastTargets targets;
    targets.Initialize(targetFlags, targetGUID, unkGUID1, transportDstGUID, dstLoc, transportSrcGUID, srcLoc);
    targets.SetElevation(elevation);
    targets.SetSpeed(speed);
    targets.Update(caster);

    // Interrupt auto-cast if other spell is forced by player
    if (caster->IsNonMeleeSpellCasted(false, true, true, false, true))
        caster->InterruptNonMeleeSpells(false, 0, false);

    caster->ClearUnitState(UNIT_STATE_FOLLOW);

    Spell* spell = new Spell(caster, spellInfo, TRIGGERED_NONE, 0, false, true);
    spell->m_cast_count = castCount;                    // probably pending spell cast
    spell->m_targets = targets;

    // TODO: need to check victim?
    SpellCastResult result;
    if (caster->m_movedPlayer)
        result = spell->CheckPetCast(caster->m_movedPlayer->GetSelectedUnit());
    else
        result = spell->CheckPetCast(NULL);
    if (result == SPELL_CAST_OK)
    {
        if (caster->GetTypeId() == TYPEID_UNIT)
        {
            Creature* pet = caster->ToCreature();
            pet->AddCreatureSpellCooldown(spellID);
            if (pet->isPet())
            {
                Pet* p = (Pet*)pet;
                // 10% chance to play special pet attack talk, else growl
                // actually this only seems to happen on special spells, fire shield for imp, torment for voidwalker, but it's stupid to check every spell
                if (p->getPetType() == SUMMON_PET && (urand(0, 100) < 10))
                    pet->SendPetTalk((uint32)PET_TALK_SPECIAL_SPELL);
                else
                    pet->SendPetAIReaction(spellID);
            }
        }

        spell->prepare(&(spell->m_targets));
    }
    else
    {
        caster->SendPetCastFail(spellID, result);
        if (caster->GetTypeId() == TYPEID_PLAYER)
        {
            if (!caster->ToPlayer()->HasSpellCooldown(spellID))
                GetPlayer()->SendClearCooldown(spellID, caster);
        }
        else
        {
            if (!caster->ToCreature()->HasSpellCooldown(spellID))
                GetPlayer()->SendClearCooldown(spellID, caster);
        }

        spell->finish(false);
        delete spell;
    }
}

void WorldSession::SendPetNameInvalid(uint32 error, const std::string& name, DeclinedName *declinedName)
{
    WorldPacket data(SMSG_PET_NAME_INVALID);

    data.WriteBit(bool(declinedName));

    if (declinedName)
    {
        for (uint32 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            data.WriteBits(declinedName->name[i].size(), 7);
    }

    data.WriteBit(0);
    data.WriteBits(name.size(), 8);
    data.FlushBits();

    if (name.size())
        data.append(name.c_str(), name.size());

    if (declinedName)
    {
        for (uint32 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            if (declinedName->name[i].size())
                data.append(declinedName->name[i].c_str(), declinedName->name[i].size());
    }

    data << uint8(1);
    data << uint32(error);

    SendPacket(&data);
}

void WorldSession::HandleLearnPetSpecialization(WorldPacket & recvData)
{
    uint32 index = recvData.read<uint32>();
    // GUID : useless =P
    recvData.rfinish();

    if (_player->IsInCombat())
        return;

    uint32 specializationId = 0;

    switch(index)
    {
        case 0:
            specializationId = SPEC_PET_FEROCITY;
            break;
        case 1:
            specializationId = SPEC_PET_TENACITY;
            break;
        case 2:
            specializationId = SPEC_PET_CUNNING;
            break;
        default:
            break;
    }

    if (!specializationId)
        return;

    Pet* pet = _player->GetPet();
    if (!pet)
        return;

    if (pet->getPetType() != PetType::HUNTER_PET)
        return;

    if (pet->GetSpecializationId())
        pet->UnlearnSpecializationSpell();

    pet->SetSpecializationId(specializationId);
    pet->LearnSpecializationSpell();
    _player->SendTalentsInfoData(true);
}
