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
#include "DBCStores.h"
#include "WorldPacket.h"
#include "WorldSession.h"
#include "ObjectMgr.h"
#include "GuildMgr.h"
#include "SpellMgr.h"
#include "Log.h"
#include "Opcodes.h"
#include "Spell.h"
#include "Totem.h"
#include "TemporarySummon.h"
#include "SpellAuras.h"
#include "CreatureAI.h"
#include "ScriptMgr.h"
#include "GameObjectAI.h"
#include "SpellAuraEffects.h"

void WorldSession::HandleClientCastFlags(WorldPacket& recvPacket, uint8 castFlags, SpellCastTargets& /*targets*/)
{
    if (castFlags & 0x8)   // Archaeology
    {
        uint32 count, entry, usedCount;
        uint8 type;
        recvPacket >> count;
        for (uint32 i = 0; i < count; ++i)
        {
            recvPacket >> type;
            switch (type)
            {
                case 2: // Keystones
                    recvPacket >> entry;        // Item id
                    recvPacket >> usedCount;    // Item count
                    GetPlayer()->GetArchaeologyMgr().AddProjectCost(entry, usedCount, false);
                    break;
                case 1: // Fragments
                    recvPacket >> entry;        // Currency id
                    recvPacket >> usedCount;    // Currency count
                    GetPlayer()->GetArchaeologyMgr().AddProjectCost(entry, usedCount, true);
                    break;
            }
        }
    }
}

void WorldSession::HandleUseItemOpcode(WorldPacket& recvPacket)
{
    // TODO: add targets.read() check
    Player* pUser = _player;

    // ignore for remote control state
    if (pUser->m_mover != pUser)
        return;

    float speed = 0.00f, elevation = 0.00f;
    std::string unkString;
    uint8 unkStringLen = 0;

    uint8 bagIndex, slot, castFlags = 0;
    uint8 castCount = 0;                                // next cast if exists (single or not)
    uint32 glyphIndex = 0;                              // something to do with glyphs?
    uint32 spellId = 0;                                 // casted spell id

    recvPacket >> bagIndex >> slot;

    ObjectGuid itemGuid, targetGuid, itemTarget, srcTransport, dstTransport, guid6, guid7;

    MovementInfo movementInfo;
    ObjectGuid movementTransportGuid;
    ObjectGuid movementGuid;
    uint32 unkMovementLoopCounter = 0;
    bool hasMovementFlags = false;
    bool hasMovementFlags2 = false;
    bool hasSplineElevation = false;
    bool hasAliveTime = false;
    bool hasFallData = false;
    bool hasFallDirection = false;
    bool hasTimeStamp = false;
    bool hasOrientation = false;
    bool hasTransportData = false;
    bool hasTransTime2 = false;
    bool hasTransTime3 = false;
    bool hasPitch = false;

    recvPacket.ReadBitSeq<4>(itemGuid);
    recvPacket.ReadBit(); // unkBit64
    bool hasUnkString = !recvPacket.ReadBit();
    bool hasCastFlags = !recvPacket.ReadBit();
    recvPacket.ReadBitSeq<5>(itemGuid);
    bool hasSpellID = !recvPacket.ReadBit();
    uint8 archeologyCounter = recvPacket.ReadBits(2);

    std::vector<uint8> archeologyType(archeologyCounter);
    std::vector<uint32> entry(archeologyCounter);
    std::vector<uint32> usedCount(archeologyCounter);

    bool hasDstTransport = recvPacket.ReadBit();
    bool hasMovement = recvPacket.ReadBit();
    recvPacket.ReadBit(); // unkBit72
    bool hasElevation = !recvPacket.ReadBit();
    bool hasCastCount = !recvPacket.ReadBit();
    recvPacket.ReadBitSeq<0>(itemGuid);
    bool hasUnkValues = !recvPacket.ReadBit();
    recvPacket.ReadBitSeq<6, 2, 1>(itemGuid);
    bool hasGlyph = !recvPacket.ReadBit();
    bool hasTransport = recvPacket.ReadBit();
    bool hasSpeed = !recvPacket.ReadBit(); // ??

    recvPacket.ReadBitSeq<3>(itemGuid);

    for (uint8 i = 0; i < archeologyCounter; i++)
        archeologyType[i] = recvPacket.ReadBits(2);

    recvPacket.ReadBitSeq<7>(itemGuid);

    if (hasDstTransport)
        recvPacket.ReadBitSeq<6, 1, 5, 0, 3, 2, 7, 4>(dstTransport);

    if (hasMovement)
    {
        recvPacket.ReadBit();

        hasMovementFlags = !recvPacket.ReadBit();
        hasMovementFlags2 = !recvPacket.ReadBit();
        hasSplineElevation = !recvPacket.ReadBit();

        recvPacket.ReadBitSeq<5>(movementGuid);

        hasAliveTime = !recvPacket.ReadBit();
        hasFallData = recvPacket.ReadBit();
        if (hasFallData)
            hasFallDirection = recvPacket.ReadBit();

        recvPacket.ReadBitSeq<7, 0>(movementGuid);
        unkMovementLoopCounter = recvPacket.ReadBits(22);
        recvPacket.ReadBitSeq<2>(movementGuid);

        hasTimeStamp = !recvPacket.ReadBit();

        recvPacket.ReadBit();
        recvPacket.ReadBitSeq<4, 1, 6>(movementGuid);

        hasOrientation = !recvPacket.ReadBit();

        recvPacket.ReadBitSeq<3>(movementGuid);
        recvPacket.ReadBit();

        hasTransportData = recvPacket.ReadBit();
        if (hasTransportData)
        {
            recvPacket.ReadBitSeq<3, 1, 6, 7, 4, 2>(movementTransportGuid);
            hasTransTime2 = recvPacket.ReadBit();
            recvPacket.ReadBitSeq<5, 0>(movementTransportGuid);
            hasTransTime3 = recvPacket.ReadBit();
        }

        if (hasMovementFlags)
            movementInfo.flags = recvPacket.ReadBits(30);

        hasPitch = !recvPacket.ReadBit();

        if (hasMovementFlags2)
            movementInfo.flags2 = recvPacket.ReadBits(13);
    }

    recvPacket.ReadBitSeq<0, 4, 7, 1, 2, 3, 6, 5>(targetGuid);

    if (hasCastFlags)
        castFlags = recvPacket.ReadBits(20);

    if (hasTransport)
        recvPacket.ReadBitSeq<2, 0, 1, 5, 7, 4, 3, 6>(srcTransport);

    recvPacket.ReadBitSeq<0, 4, 3, 5, 1, 7, 6, 2>(itemTarget);

    if (hasUnkValues)
        recvPacket.ReadBits(5);

    if (hasUnkString)
        unkStringLen = recvPacket.ReadBits(7);

    recvPacket.ReadByteSeq<7>(itemGuid);

    for (uint8 i = 0; i < archeologyCounter; i++)
    {
        switch (archeologyType[i])
        {
            case 1: // Fragments
                recvPacket >> entry[i];     // Currency ID
                recvPacket >> usedCount[i]; // Currency count
                break;
            case 2: // Keystones
                recvPacket >> entry[i];     // Item ID
                recvPacket >> usedCount[i]; // ItemCount
                break;
            default:
                break;
        }
    }

    recvPacket.ReadByteSeq<3, 6, 5, 4, 1, 0, 2>(itemGuid);
    recvPacket.ReadByteSeq<0, 4, 1, 7, 3, 6, 5, 2>(itemTarget);

    WorldLocation srcPos;
    if (hasTransport)
    {
        recvPacket.ReadByteSeq<7, 5, 6>(srcTransport);
        recvPacket >> srcPos.m_positionX;
        recvPacket.ReadByteSeq<3, 1>(srcTransport);
        recvPacket >> srcPos.m_positionY;
        recvPacket >> srcPos.m_positionZ;
        recvPacket.ReadByteSeq<0, 2, 4>(srcTransport);
    }

    if (hasMovement)
    {
        for (uint32 i = 0; i < unkMovementLoopCounter; ++i)
            recvPacket.read_skip<uint32>();

        if (hasFallData)
        {
            if (hasFallDirection)
            {
                recvPacket.read_skip<float>();
                recvPacket.read_skip<float>();
                recvPacket.read_skip<float>();
            }

            recvPacket.read_skip<float>();
            recvPacket.read_skip<uint32>();
        }

        recvPacket.ReadByteSeq<3>(movementGuid);

        if (hasTransportData)
        {
            recvPacket.read_skip<uint32>();

            if (hasTransTime3)
                recvPacket.read_skip<uint32>();
            if (hasTransTime2)
                recvPacket.read_skip<uint32>();

            recvPacket.read_skip<uint8>();
            recvPacket.ReadByteSeq<7, 2, 3, 0, 5>(movementTransportGuid);
            recvPacket.read_skip<float>();
            recvPacket.ReadByteSeq<6>(movementTransportGuid);
            recvPacket.read_skip<float>();
            recvPacket.ReadByteSeq<4>(movementTransportGuid);
            recvPacket.read_skip<float>();
            recvPacket.read_skip<float>();
            recvPacket.ReadByteSeq<1>(movementTransportGuid);
        }

        if (hasOrientation)
            recvPacket.read_skip<float>();

        recvPacket.ReadByteSeq<6, 1, 7>(movementGuid);
        recvPacket.read_skip<float>();

        if (hasTimeStamp)
            recvPacket.read_skip<uint32>();

        recvPacket.ReadByteSeq<2, 5>(movementGuid);
        recvPacket.read_skip<float>();

        if (hasAliveTime)
            recvPacket.read_skip<uint32>();

        if (hasSplineElevation)
            recvPacket.read_skip<float>();

        recvPacket.read_skip<float>();
        recvPacket.ReadByteSeq<4>(movementGuid);

        if (hasPitch)
            recvPacket.read_skip<float>();

        recvPacket.ReadByteSeq<0>(movementGuid);
    }

    if (hasCastCount)
        recvPacket >> castCount;

    WorldLocation destPos;
    if (hasDstTransport)
    {
        recvPacket.ReadByteSeq<6, 1>(dstTransport);
        recvPacket >> destPos.m_positionZ;
        recvPacket.ReadByteSeq<0>(dstTransport);
        recvPacket >> destPos.m_positionX;
        recvPacket.ReadByteSeq<5>(dstTransport);
        recvPacket >> destPos.m_positionY;
        recvPacket.ReadByteSeq<7, 2, 4, 3>(dstTransport);
    }

    if (hasSpeed)
        recvPacket >> speed;

    recvPacket.ReadByteSeq<1, 3, 7, 0, 4, 6, 2, 5>(targetGuid);

    if (hasSpellID)
        recvPacket >> spellId;

    if (hasElevation)
        recvPacket >> elevation;

    if (hasUnkString)
        unkString = recvPacket.ReadString(unkStringLen);

    if (hasGlyph)
        recvPacket >> glyphIndex;

    if (glyphIndex >= MAX_GLYPH_SLOT_INDEX)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    Item* pItem = pUser->GetUseableItemByPos(bagIndex, slot);
    if (!pItem)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    if (pItem->GetGUID() != itemGuid)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    TC_LOG_DEBUG("network", "WORLD: CMSG_USE_ITEM packet, bagIndex: %u, slot: %u, castCount: %u, spellId: %u, Item: %u, glyphIndex: %u, data length = %i", bagIndex, slot, castCount, spellId, pItem->GetEntry(), glyphIndex, (uint32)recvPacket.size());

    ItemTemplate const* proto = pItem->GetTemplate();
    if (!proto)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, NULL);
        return;
    }

    // some item classes can be used only in equipped state
    if (proto->InventoryType != INVTYPE_NON_EQUIP && !pItem->IsEquipped())
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, pItem, NULL);
        return;
    }

    InventoryResult msg = pUser->CanUseItem(pItem);
    if (msg != EQUIP_ERR_OK)
    {
        pUser->SendEquipError(msg, pItem, NULL);
        return;
    }

    // only allow conjured consumable, bandage, poisons (all should have the 2^21 item flag set in DB)
    if (proto->Class == ITEM_CLASS_CONSUMABLE && !(proto->Flags & ITEM_PROTO_FLAG_USEABLE_IN_ARENA) && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, NULL);
        return;
    }

    // don't allow items banned in arena
    if (proto->Flags & ITEM_PROTO_FLAG_NOT_USEABLE_IN_ARENA && pUser->InArena())
    {
        pUser->SendEquipError(EQUIP_ERR_NOT_DURING_ARENA_MATCH, pItem, NULL);
        return;
    }

    if (pUser->IsInCombat())
    {
        for (int i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
        {
            if (SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(proto->Spells[i].SpellId))
            {
                if (!spellInfo->CanBeUsedInCombat())
                {
                    pUser->SendEquipError(EQUIP_ERR_NOT_IN_COMBAT, pItem, NULL);
                    return;
                }
            }
        }
    }

    // check also  BIND_WHEN_PICKED_UP and BIND_QUEST_ITEM for .additem or .additemset case by GM (not binded at adding to inventory)
    if (pItem->GetTemplate()->Bonding == BIND_WHEN_USE || pItem->GetTemplate()->Bonding == BIND_WHEN_PICKED_UP || pItem->GetTemplate()->Bonding == BIND_QUEST_ITEM)
    {
        if (!pItem->IsSoulBound())
        {
            pItem->SetState(ITEM_CHANGED, pUser);
            pItem->SetBinding(true);
        }
    }

    Unit* mover = pUser->m_mover;
    if (mover != pUser && mover->GetTypeId() == TYPEID_PLAYER)
    {
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    SpellCastTargets targets;
    HandleClientCastFlags(recvPacket, castFlags, targets);

    targets.Initialize(castFlags, targetGuid, itemTarget, guid6, destPos, guid7, srcPos);
    targets.SetElevation(elevation);
    targets.SetSpeed(speed);
    targets.Update(mover);

    // Note: If script stop casting it must send appropriate data to client to prevent stuck item in gray state.
    if (!sScriptMgr->OnItemUse(pUser, pItem, targets))
    {
        // no script or script not process request by self
        pUser->CastItemUseSpell(pItem, targets, castCount, glyphIndex);
    }
}

void WorldSession::HandleOpenItemOpcode(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_OPEN_ITEM packet, data length = %i", (uint32)recvPacket.size());

    Player* pUser = _player;

    // ignore for remote control state
    if (pUser->m_mover != pUser)
        return;

    uint8 bagIndex, slot;

    recvPacket >> bagIndex >> slot;

    TC_LOG_INFO("network", "bagIndex: %u, slot: %u", bagIndex, slot);

    Item* item = pUser->GetItemByPos(bagIndex, slot);
    if (!item)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, NULL, NULL);
        return;
    }

    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
    {
        pUser->SendEquipError(EQUIP_ERR_ITEM_NOT_FOUND, item, NULL);
        return;
    }

    // Verify that the bag is an actual bag or wrapped item that can be used "normally"
    if (!(proto->Flags & ITEM_PROTO_FLAG_OPENABLE) && !item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FLAG_WRAPPED))
    {
        pUser->SendEquipError(EQUIP_ERR_CLIENT_LOCKED_OUT, item, NULL);
        TC_LOG_ERROR("network", "Possible hacking attempt: Player %s [guid: %u] tried to open item [guid: %u, entry: %u] which is not openable!",
                pUser->GetName().c_str(), pUser->GetGUIDLow(), item->GetGUIDLow(), proto->ItemId);
        return;
    }

    // locked item
    uint32 lockId = proto->LockID;
    if (lockId)
    {
        LockEntry const* lockInfo = sLockStore.LookupEntry(lockId);

        if (!lockInfo)
        {
            pUser->SendEquipError(EQUIP_ERR_ITEM_LOCKED, item, NULL);
            TC_LOG_ERROR("network", "WORLD::OpenItem: item [guid = %u] has an unknown lockId: %u!", item->GetGUIDLow(), lockId);
            return;
        }

        // was not unlocked yet
        if (item->IsLocked())
        {
            pUser->SendEquipError(EQUIP_ERR_ITEM_LOCKED, item, NULL);
            return;
        }
    }

    if (item->HasFlag(ITEM_FIELD_FLAGS, ITEM_FLAG_WRAPPED))// wrapped?
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHARACTER_GIFT_BY_ITEM);

        stmt->setUInt32(0, item->GetGUIDLow());

        PreparedQueryResult result = CharacterDatabase.Query(stmt);

        if (result)
        {
            Field* fields = result->Fetch();
            uint32 entry = fields[0].GetUInt32();
            uint32 flags = fields[1].GetUInt32();

            item->SetUInt64Value(ITEM_FIELD_GIFTCREATOR, 0);
            item->SetEntry(entry);
            item->SetUInt32Value(ITEM_FIELD_FLAGS, flags);
            item->SetState(ITEM_CHANGED, pUser);
        }
        else
        {
            TC_LOG_ERROR("network", "Wrapped item %u don't have record in character_gifts table and will deleted", item->GetGUIDLow());
            pUser->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            return;
        }

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_CHAR_GIFT);
        stmt->setUInt32(0, item->GetGUIDLow());
        CharacterDatabase.Execute(stmt);
    }
    else
        pUser->SendLoot(item->GetGUID(), LOOT_CORPSE);
}

void WorldSession::HandleGameObjectUseOpcode(WorldPacket& recvData)
{
    ObjectGuid guid;

    recvData.ReadBitSeq<5, 3, 1, 4, 6, 7, 2, 0>(guid);
    recvData.ReadByteSeq<6, 1, 5, 3, 4, 0, 2, 7>(guid);

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_GAMEOBJECT_USE Message [guid=%u]", GUID_LOPART(guid));

    // ignore for remote control state
    if (_player->m_mover != _player)
        return;

    if (GameObject* obj = GetPlayer()->GetMap()->GetGameObject(guid))
        obj->Use(_player);
}

void WorldSession::HandleGameobjectReportUse(WorldPacket& recvPacket)
{
    ObjectGuid guid;

    recvPacket.ReadBitSeq<5, 2, 7, 3, 0, 6, 4, 1>(guid);
    recvPacket.ReadByteSeq<6, 0, 5, 3, 4, 1, 7, 2>(guid);

    TC_LOG_DEBUG("network", "WORLD: Recvd CMSG_GAMEOBJECT_REPORT_USE Message [in game guid: %u]", GUID_LOPART(guid));

    // ignore for remote control state
    if (_player->m_mover != _player)
        return;

    GameObject* go = GetPlayer()->GetMap()->GetGameObject(guid);
    if (!go)
        return;

    if (!go->IsWithinDistInMap(_player, INTERACTION_DISTANCE))
        return;

    if (go->AI()->GossipHello(_player))
        return;

    _player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_USE_GAMEOBJECT, go->GetEntry());
}

void WorldSession::HandleCastSpellOpcode(WorldPacket& recvPacket)
{
    uint32 spellId = 0;
    uint32 glyphIndex = 0;
    uint8 castCount = 0;
    uint8 unkStringLength = 0;
    float speed = 0.0f, elevation = 0.0f;
    std::string unkString;

    ObjectGuid itemTarget, Guid2, Guid3, targetGuid;
#if 0
    MovementInfo movementInfo;
    ObjectGuid movementTransportGuid;
    ObjectGuid movementGuid;
    uint32 unkMovementLoopCounter = 0;
    bool hasMovementFlags = false;
    bool hasMovementFlags2 = false;
    bool hasSplineElevation = false;
    bool hasAliveTime = false;
    bool hasFallData = false;
    bool hasFallDirection = false;
    bool hasTimeStamp = false;
    bool hasOrientation = false;
    bool hasTransportData = false;
    bool hasTransTime2 = false;
    bool hasTransTime3 = false;
    bool hasPitch = false;
#endif
    bool hasTargetFlags = !recvPacket.ReadBit();
    bool hasCountCast = !recvPacket.ReadBit();
    bool hasUnk1 = !recvPacket.ReadBit();
    bool hasSpellId = !recvPacket.ReadBit();
    uint8 archeologyCounter = recvPacket.ReadBits(2);
    recvPacket.ReadBit(); // hasGuid0 (inversed)
    bool hasElevation = !recvPacket.ReadBit();
    recvPacket.ReadBit(); // hasTargetGuid (inversed)
    bool hasGuid3 = recvPacket.ReadBit();
    bool hasSpeed = !recvPacket.ReadBit();
    bool hasUnkString = !recvPacket.ReadBit();
    bool hasGuid2 = recvPacket.ReadBit();
    bool hasMovement = recvPacket.ReadBit();
    bool hasUnk4 = !recvPacket.ReadBit();

    uint32 targetFlags = 0;

    std::vector<uint8> archeologyType(archeologyCounter);
    std::vector<uint32> entry(archeologyCounter);
    std::vector<uint32> usedCount(archeologyCounter);

    for (int i = 0; i < archeologyCounter; ++i)
        archeologyType[i] = recvPacket.ReadBits(2);

    if (hasMovement)
    {
#if 0
        hasAliveTime = !recvPacket.ReadBit();
        recvPacket.ReadBitSeq<3>(movementGuid);
        hasSplineElevation = !recvPacket.ReadBit();
        unkMovementLoopCounter = recvPacket.ReadBits(22);
        hasFallData = recvPacket.ReadBit();
        hasMovementFlags = !recvPacket.ReadBit();
        recvPacket.ReadBitSeq<6, 0>(movementGuid);

        if (hasMovementFlags)
            movementInfo.flags = recvPacket.ReadBits(30);

        hasPitch = !recvPacket.ReadBit();

        recvPacket.ReadBit();
        recvPacket.ReadBitSeq<2, 7, 1, 5>(movementGuid);

        hasOrientation = !recvPacket.ReadBit();
        if (hasFallData)
            hasFallDirection = recvPacket.ReadBit();

        recvPacket.ReadBit();
        recvPacket.ReadBitSeq<4>(movementGuid);

        hasTransportData = recvPacket.ReadBit();
        if (hasTransportData)
        {
            hasTransTime2 = recvPacket.ReadBit();
            recvPacket.ReadBitSeq<6, 3, 1, 0, 4>(movementTransportGuid);
            hasTransTime3 = recvPacket.ReadBit();
            recvPacket.ReadBitSeq<7, 2, 5>(movementTransportGuid);
        }

        recvPacket.ReadBit();

        hasMovementFlags2 = !recvPacket.ReadBit();
        if (hasMovementFlags2)
            movementInfo.flags2 = recvPacket.ReadBits(13);

        hasTimeStamp = !recvPacket.ReadBit();
#endif
    }

    if (hasTargetFlags)
        targetFlags = recvPacket.ReadBits(20);

    recvPacket.ReadBitSeq<2, 1, 3, 6, 5, 4, 7, 0>(itemTarget);

    if (hasGuid3)
        recvPacket.ReadBitSeq<3, 6, 1, 0, 4, 5, 7, 2>(Guid3);

    if (hasGuid2)
        recvPacket.ReadBitSeq<6, 3, 5, 2, 0, 4, 1, 7>(Guid2);

    recvPacket.ReadBitSeq<5, 0, 2, 3, 1, 4, 6, 7>(targetGuid);

    if (hasUnkString)
        unkStringLength = recvPacket.ReadBits(7);

    if (hasUnk4)
        recvPacket.ReadBits(5);

    for (int i = 0; i < archeologyCounter; i++)
    {
        switch (archeologyType[i])
        {
            case 2: // Keystones
                recvPacket >> entry[i];        // Item id
                recvPacket >> usedCount[i];    // Item count
                break;
            case 1: // Fragments
                recvPacket >> entry[i];        // Currency id
                recvPacket >> usedCount[i];    // Currency count
                break;
            default:
                break;
        }
    }

    if (hasMovement)
    {
#if 0
        if (hasTransportData)
        {
            recvPacket.ReadByteSeq<5>(movementTransportGuid);

            if (hasTransTime3)
                recvPacket.read_skip<uint32>();

            recvPacket.read_skip<float>();
            recvPacket.read_skip<float>();

            if (hasTransTime2)
                recvPacket.read_skip<uint32>();

            recvPacket.ReadByteSeq<7, 2, 0>(movementTransportGuid);
            recvPacket.read_skip<uint32>();
            recvPacket.ReadByteSeq<1, 6, 3>(movementTransportGuid);
            recvPacket.read_skip<float>();
            recvPacket.ReadByteSeq<4>(movementTransportGuid);
            recvPacket.read_skip<uint8>();
            recvPacket.read_skip<float>();
        }

        if (hasOrientation)
            recvPacket.read_skip<float>();

        recvPacket.ReadByteSeq<6, 4>(movementGuid);

        if (hasFallData)
        {
            recvPacket.read_skip<uint32>();

            if (hasFallDirection)
            {
                recvPacket.read_skip<float>();
                recvPacket.read_skip<float>();
                recvPacket.read_skip<float>();
            }

            recvPacket.read_skip<float>();
        }

        recvPacket.ReadByteSeq<3, 2>(movementGuid);

        if (hasPitch)
            recvPacket.read_skip<float>();
        if (hasAliveTime)
            recvPacket.read_skip<uint32>();

        recvPacket.read_skip<float>();

        if (hasTimeStamp)
            recvPacket.read_skip<uint32>();

        recvPacket.ReadByteSeq<5, 0>(movementGuid);

        recvPacket.read_skip<float>();
        recvPacket.read_skip<float>();

        for (uint32 i = 0 ; i < unkMovementLoopCounter ; ++i)
            recvPacket.read_skip<uint32>();

        if (hasSplineElevation)
            recvPacket.read_skip<float>();

        recvPacket.ReadByteSeq<7, 1>(movementGuid);
#endif
    }

    if (hasSpellId)
        recvPacket >> spellId;

    WorldLocation srcPos;
    if (hasGuid2)
    {
        recvPacket.ReadByteSeq<7>(Guid2);
        recvPacket >> srcPos.m_positionX;
        recvPacket.ReadByteSeq<6, 0>(Guid2);
        recvPacket >> srcPos.m_positionY;
        recvPacket.ReadByteSeq<1, 4>(Guid2);
        recvPacket >> srcPos.m_positionZ;
        recvPacket.ReadByteSeq<3, 2, 5>(Guid2);
    }

    WorldLocation destPos;
    if (hasGuid3)
    {
        recvPacket.ReadByteSeq<5, 4, 3, 1>(Guid3);
        recvPacket >> destPos.m_positionZ;
        recvPacket >> destPos.m_positionY;
        recvPacket.ReadByteSeq<2, 6, 7>(Guid3);
        recvPacket >> destPos.m_positionX;
        recvPacket.ReadByteSeq<0>(Guid3);
    }

    recvPacket.ReadByteSeq<7, 2, 6, 0, 4, 5, 1, 3>(targetGuid);
    recvPacket.ReadByteSeq<1, 0, 2, 3, 5, 6, 7, 4>(itemTarget);

    if (hasCountCast)
        recvPacket >> castCount;

    if (hasElevation)
        recvPacket >> elevation;

    if (hasUnkString)
        unkString = recvPacket.ReadString(unkStringLength);

    if (hasUnk1)
        recvPacket >> glyphIndex; // not sure about this ...

    if (hasSpeed)
        recvPacket >> speed;

    TC_LOG_DEBUG("network", "WORLD: got cast spell packet, castCount: %u, spellId: %u, targetFlags: %u, data length = %u",
                   castCount, spellId, targetFlags, (uint32)recvPacket.size());

    // ignore for remote control state (for player case)
    Unit* mover = _player->m_mover;
    if (mover != _player && mover->GetTypeId() == TYPEID_PLAYER)
    {
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    auto spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown spell id %u", spellId);
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    // Override spell Id, client send base spell and not the overrided id
    for (auto const &overrideId : spellInfo->OverrideSpellList)
    {
        if (_player->HasSpell(overrideId))
        {
            auto const overrideInfo = sSpellMgr->GetSpellInfo(overrideId);
            if (overrideInfo)
            {
                spellInfo = overrideInfo;
                spellId = overrideId;
            }
            break;
        }
    }

    if (spellInfo->IsPassive())
    {
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    Unit* caster = mover;
    if (caster->GetTypeId() == TYPEID_UNIT && !caster->ToCreature()->HasSpell(spellId))
    {
        // If the vehicle creature does not have the spell but it allows the passenger to cast own spells
        // change caster to player and let him cast
        if (!_player->IsOnVehicle(caster) || spellInfo->CheckVehicle(_player) != SPELL_CAST_OK)
        {
            recvPacket.rfinish(); // prevent spam at ignore packet
            return;
        }

        caster = _player;
    }


    // SPELL_AURA_MOD_NEXT_SPELL - allow casting if players has aura with
    bool allowedCast = false;
    const Unit::AuraEffectList &auraEffects = caster->GetAuraEffectsByType(SPELL_AURA_MOD_NEXT_SPELL);
    if (!auraEffects.empty())
        for (auto itr : auraEffects)
            if (const SpellInfo * spellInfo = itr->GetSpellInfo())
                if (spellInfo->Effects[itr->GetEffIndex()].TriggerSpell == spellId)
                {
                    allowedCast = true;
                    caster->RemoveAllAurasByType(SPELL_AURA_MOD_NEXT_SPELL);
                    break;
                }

    if ((caster->GetTypeId() == TYPEID_PLAYER
            && !allowedCast
            // Hack for Throw Totem, Echo of Baine
            && spellId != 101603
            // Hack for disarm. Client sends the spell instead of gameobjectuse.
            && spellId != 1843
            // TODO may be add this attribute to spells above?
            && (spellInfo->AttributesCu & SPELL_ATTR0_CU_SKIP_SPELLBOOCK_CHECK) == 0
            && !spellInfo->IsAbilityOfSkillType(SKILL_ARCHAEOLOGY)))
    {
        // not have spell in spellbook
        // cheater? kick? ban?
        if (!caster->ToPlayer()->HasActiveSpell(spellId))
        {
            recvPacket.rfinish(); // prevent spam at ignore packet
            return;
        }
    }

    Unit::AuraEffectList swaps = mover->GetAuraEffectsByType(SPELL_AURA_OVERRIDE_ACTIONBAR_SPELLS);
    Unit::AuraEffectList const& swaps2 = mover->GetAuraEffectsByType(SPELL_AURA_OVERRIDE_ACTIONBAR_SPELLS_2);
    if (!swaps2.empty())
        swaps.insert(swaps.end(), swaps2.begin(), swaps2.end());

    if (!swaps.empty())
    {
        for (Unit::AuraEffectList::const_iterator itr = swaps.begin(); itr != swaps.end(); ++itr)
        {
            if ((*itr)->IsAffectingSpell(spellInfo))
            {
                if (SpellInfo const* newInfo = sSpellMgr->GetSpellInfo((*itr)->GetAmount()))
                {
                    spellInfo = newInfo;
                    spellId = newInfo->Id;
                }
                break;
            }
        }
    }

    // Client is resending autoshot cast opcode when other spell is casted during shoot rotation
    // Skip it to prevent "interrupt" message
    if (spellInfo->IsAutoRepeatRangedSpell() && caster->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)
        && caster->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL)->m_spellInfo == spellInfo)
    {
        recvPacket.rfinish();
        return;
    }

    // can't use our own spells when we're in possession of another unit,
    if (_player->isPossessing())
    {
        recvPacket.rfinish(); // prevent spam at ignore packet
        return;
    }

    // client provided targets
    SpellCastTargets targets;
    //HandleClientCastFlags(recvPacket, castFlags, targets);

    // Build SpellCastTargets
    /*uint32 targetFlags = 0;

    if (itemTarget)
        targetFlags |= TARGET_FLAG_ITEM;

    if (IS_UNIT_GUID(targetGuid))
        targetFlags |= TARGET_FLAG_UNIT;

    if (IS_GAMEOBJECT_GUID(targetGuid))
        targetFlags |= TARGET_FLAG_GAMEOBJECT;*/

    // TODO : TARGET_FLAG_TRADE_ITEM, TARGET_FLAG_SOURCE_LOCATION, TARGET_FLAG_DEST_LOCATION, TARGET_FLAG_UNIT_MINIPET, TARGET_FLAG_CORPSE_ENEMY, TARGET_FLAG_CORPSE_ALLY

    targets.Initialize(targetFlags, targetGuid, itemTarget, Guid3, destPos, Guid2, srcPos);
    targets.SetElevation(elevation);
    targets.SetSpeed(speed);
    targets.Update(mover);

    // auto-selection buff level base at target level (in spellInfo)
    if (targets.GetUnitTarget())
    {
        SpellInfo const* actualSpellInfo = spellInfo->GetAuraRankForLevel(targets.GetUnitTarget()->getLevel());

        // if rank not found then function return NULL but in explicit cast case original spell can be casted and later failed with appropriate error message
        if (actualSpellInfo)
            spellInfo = actualSpellInfo;
    }

    // Custom MoP Scripts - need to put in another manipulative function
    // Solace and Insanity - Switch Mind-flay casted if target has devouring plague
    if (spellInfo->Id == 15407 && _player->GetShapeshiftForm() == FORM_SHADOW && _player->HasSpell(139139))
    {
        if (auto unitTarget = targets.GetUnitTarget())
        {
            if (unitTarget->HasAura(2944, _player->GetGUID()))
            {
                if (SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(129197))
                {
                    spellInfo = newSpellInfo;
                    spellId = newSpellInfo->Id;
                }
            }
        }
    }
    // Aimed Shot - 19434 and Aimed Shot (for Master Marksman) - 82928
    else if (spellInfo->Id == 19434 && _player->HasAura(82926))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(82928);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Alter Time - 108978 and Alter Time (overrided) - 127140
    else if (spellInfo->Id == 108978 && _player->HasAura(110909))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(127140);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Fix Dark Soul for Destruction warlocks
    else if (spellInfo->Id == 113860 && _player->GetSpecializationId(_player->GetActiveSpec()) == SPEC_WARLOCK_DESTRUCTION)
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(113858);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Halo - 120517 and Halo - 120644 (shadow form)
    else if (spellInfo->Id == 120517 && _player->HasAura(15473))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(120644);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Cascade (shadow) - 127632 and Cascade - 121135
    else if (spellInfo->Id == 121135 && _player->HasAura(15473))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(127632);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Zen Pilgrimage - 126892 and Zen Pilgrimage : Return - 126895
    else if (spellInfo->Id == 126892 && _player->HasAura(126896))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(126895);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Soul Swap - 86121 and Soul Swap : Exhale - 86213
    else if (spellInfo->Id == 86121 && _player->HasAura(86211))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(86213);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
            _player->RemoveAura(86211);
        }
    }
    // Mage Bomb - 125430 and  Living Bomb - 44457
    else if (spellInfo->Id == 125430 && _player->HasSpell(44457))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(44457);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Mage Bomb - 125430 and Frost Bomb - 112948
    else if (spellInfo->Id == 125430 && _player->HasSpell(112948))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112948);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Mage Bomb - 125430 and  Nether Tempest - 114923
    else if (spellInfo->Id == 125430 && _player->HasSpell(114923))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(114923);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Evocation - 12051 and  Rune of Power - 116011
    else if (spellInfo->Id == 12051 && _player->HasSpell(116011))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(116011);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }
    // Surging Mist - 116694 and Surging Mist - 116995
    // Surging Mist is instantly casted if player is channeling Soothing Mist
    else if (spellInfo->Id == 116694 && _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->GetSpellInfo()->Id == 115175)
    {
        recvPacket.rfinish();
        _player->CastSpell(targets.GetUnitTarget(), 116995, true);
        _player->EnergizeBySpell(_player, 116995, 1, POWER_CHI);
        int32 powerCost = spellInfo->CalcPowerCost(_player, spellInfo->GetSchoolMask(), _player->GetSpellPowerEntryBySpell(spellInfo));
        _player->ModifyPower(POWER_MANA, -powerCost);
        return;
    }
    // Enveloping Mist - 124682 and Enveloping Mist - 132120
    // Enveloping Mist is instantly casted if player is channeling Soothing Mist
    else if (spellInfo->Id == 124682 && _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL) && _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL)->GetSpellInfo()->Id == 115175)
    {
        recvPacket.rfinish();
        _player->CastSpell(targets.GetUnitTarget(), 132120, true);
        int32 powerCost = spellInfo->CalcPowerCost(_player, spellInfo->GetSchoolMask(), _player->GetSpellPowerEntryBySpell(spellInfo));
        _player->ModifyPower(POWER_CHI, -powerCost);
        return;
    }
    // Glyph of Shadow Bolt
    else if (spellInfo->Id == 686 && _player->HasAura(56240))
    {
        SpellInfo const* newSpellInfo = sSpellMgr->GetSpellInfo(112092);
        if (newSpellInfo)
        {
            spellInfo = newSpellInfo;
            spellId = newSpellInfo->Id;
        }
    }

    uint32 gcd = _player->GetGlobalCooldownMgr().GetGlobalCooldown(caster, spellInfo);
    Spell* spell = new Spell(caster, spellInfo, TRIGGERED_NONE, 0, false, true);
    spell->m_cast_count = castCount;                       // set count of casts
    spell->m_glyphIndex = glyphIndex;
    spell->prepare(&targets, NULL, gcd);
}

void WorldSession::HandleCancelCastOpcode(WorldPacket& recvPacket)
{
    uint32 spellId = 0;
    bool hasSpell;

    recvPacket.ReadBit();
    hasSpell = !recvPacket.ReadBit();

    if (hasSpell)
        recvPacket >> spellId;

    recvPacket.read_skip<uint8>();                          // counter, increments with every CANCEL packet, don't use for now

    if (_player->IsNonMeleeSpellCasted(false))
        _player->InterruptNonMeleeSpells(false, spellId, false);
}

void WorldSession::HandleCancelAuraOpcode(WorldPacket& recvPacket)
{
    uint32 spellId;
    ObjectGuid casterGuid;

    recvPacket >> spellId;

    recvPacket.ReadBit();

    recvPacket.ReadBitSeq<0, 2, 4, 1, 3, 7, 5, 6>(casterGuid);
    recvPacket.ReadByteSeq<5, 1, 4, 6, 0, 7, 3, 2>(casterGuid);

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
        return;

    // not allow remove spells with attr SPELL_ATTR0_CANT_CANCEL
    if (spellInfo->Attributes & SPELL_ATTR0_CANT_CANCEL)
        return;

    // channeled spell case (it currently casted then)
    if (spellInfo->IsChanneled())
    {
        if (Spell* curSpell = _player->GetCurrentSpell(CURRENT_CHANNELED_SPELL))
            if (curSpell->m_spellInfo->Id == spellId)
                _player->InterruptSpell(CURRENT_CHANNELED_SPELL);
        return;
    }

    // non channeled case:
    // don't allow remove non positive spells
    // don't allow cancelling passive auras (some of them are visible)
    if (!spellInfo->IsPositive() || spellInfo->IsPassive())
        return;

    _player->RemoveOwnedAura(spellId, casterGuid, 0, AURA_REMOVE_BY_CANCEL);
}

void WorldSession::HandlePetCancelAuraOpcode(WorldPacket& recvPacket)
{
    ObjectGuid guid;
    uint32 spellId;

    recvPacket >> spellId;
    recvPacket.ReadBitSeq<4, 6, 5, 2, 3, 1, 0, 7>(guid);
    recvPacket.ReadByteSeq<1, 3, 2, 5, 0, 6, 7, 4>(guid);

    SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spellId);
    if (!spellInfo)
    {
        TC_LOG_ERROR("network", "WORLD: unknown PET spell id %u", spellId);
        return;
    }

    Creature* pet=ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!pet)
    {
        TC_LOG_ERROR("network", "HandlePetCancelAura: Attempt to cancel an aura for non-existant pet %u by player '%s'",
                     uint32(GUID_LOPART(guid)), GetPlayer()->GetName().c_str());
        return;
    }

    if (pet != GetPlayer()->GetGuardianPet() && pet != GetPlayer()->GetCharm())
    {
        TC_LOG_ERROR("network", "HandlePetCancelAura: Pet %u is not a pet of player '%s'",
                     uint32(GUID_LOPART(guid)), GetPlayer()->GetName().c_str());
        return;
    }

    if (!pet->IsAlive())
    {
        pet->SendPetActionFeedback(FEEDBACK_PET_DEAD);
        return;
    }

    pet->RemoveOwnedAura(spellId, 0, 0, AURA_REMOVE_BY_CANCEL);

    pet->AddCreatureSpellCooldown(spellId);
}

void WorldSession::HandleCancelGrowthAuraOpcode(WorldPacket& /*recvPacket*/)
{
}

void WorldSession::HandleCancelAutoRepeatSpellOpcode(WorldPacket& /*recvPacket*/)
{
    // may be better send SMSG_CANCEL_AUTO_REPEAT?
    // cancel and prepare for deleting
    _player->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
}

void WorldSession::HandleCancelChanneling(WorldPacket& recvData)
{
    recvData.read_skip<uint32>();                          // spellid, not used

    // ignore for remote control state (for player case)
    Unit* mover = _player->m_mover;
    if (mover != _player && mover->GetTypeId() == TYPEID_PLAYER)
        return;

    mover->InterruptSpell(CURRENT_CHANNELED_SPELL);
}

void WorldSession::HandleTotemDestroyed(WorldPacket& recvPacket)
{
    // ignore for remote control state
    if (_player->m_mover != _player)
        return;

    uint8 slotId;
    ObjectGuid totemGuid;

    recvPacket >> slotId;

    recvPacket.ReadBitSeq<5, 3, 0, 7, 5, 6, 2, 1>(totemGuid);
    recvPacket.ReadByteSeq<7, 2, 0, 6, 5, 3, 4, 1>(totemGuid);

    ++slotId;
    if (slotId >= MAX_TOTEM_SLOT)
        return;

    if (!_player->m_SummonSlot[slotId])
        return;

    Creature* totem = GetPlayer()->GetMap()->GetCreature(_player->m_SummonSlot[slotId]);
    if (totem && totem->isTotem())
        totem->ToTotem()->UnSummon();
}

void WorldSession::HandleSelfResOpcode(WorldPacket& /*recvData*/)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_SELF_RES");                  // empty opcode

    if (_player->HasAuraType(SPELL_AURA_PREVENT_RESURRECTION))
        return; // silent return, client should display error by itself and not send this opcode

    if (_player->GetUInt32Value(PLAYER_SELF_RES_SPELL))
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(_player->GetUInt32Value(PLAYER_SELF_RES_SPELL));
        if (spellInfo)
            _player->CastSpell(_player, spellInfo, false, 0);

        _player->SetUInt32Value(PLAYER_SELF_RES_SPELL, 0);
    }
}

void WorldSession::HandleSpellClick(WorldPacket& recvData)
{
    // Read guid
    ObjectGuid guid;

    recvData.ReadBitSeq<1, 7, 4, 5, 3, 6, 2>(guid);
    recvData.ReadBit();
    recvData.ReadBitSeq<0>(guid);
    recvData.ReadByteSeq<0, 2, 3, 1, 4, 6, 5, 7>(guid);

    // this will get something not in world. crash
    Creature* unit = ObjectAccessor::GetCreatureOrPetOrVehicle(*_player, guid);

    if (!unit)
        return;

    // TODO: Unit::SetCharmedBy: 28782 is not in world but 0 is trying to charm it! -> crash
    if (!unit->IsInWorld())
        return;

    unit->HandleSpellClick(_player);
}

void WorldSession::HandleMirrorImageDataRequest(WorldPacket& recvData)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_GET_MIRRORIMAGE_DATA");
    ObjectGuid guid;
    recvData.read<uint32>(); // displayId

    recvData.ReadBitSeq<0, 4, 1, 7, 6, 2, 5, 3>(guid);
    recvData.ReadByteSeq<4, 0, 6, 7, 3, 2, 1, 5>(guid);

    // Get unit for which data is needed by client
    Unit* unit = ObjectAccessor::GetObjectInWorld(guid, (Unit*)NULL);
    if (!unit)
        return;

    if (!unit->HasAuraType(SPELL_AURA_CLONE_CASTER))
        return;

    // Get creator of the unit (SPELL_AURA_CLONE_CASTER does not stack)
    Unit* creator = unit->GetAuraEffectsByType(SPELL_AURA_CLONE_CASTER).front()->GetCaster();
    if (!creator)
        return;

    if (creator->GetSimulacrumTarget())
        creator = creator->GetSimulacrumTarget();

    WorldPacket data(SMSG_MIRROR_IMAGE_DATA, 68);

    if (Player const * const player = creator->ToPlayer())
    {
        Guild const *guild = player->GetGuild();
        ObjectGuid guildGuid = guild ? guild->GetGUID() : 0;

        data << uint8(player->GetByteValue(PLAYER_BYTES, 3));       // haircolor
        data << uint8(player->GetByteValue(PLAYER_BYTES, 2));       // hair
        data << uint8(player->getRace());
        data << uint8(player->getGender());
        data << uint8(player->GetByteValue(PLAYER_BYTES_2, 0));     // facialhair
        data << uint8(player->GetByteValue(PLAYER_BYTES, 0));       // skin
        data << uint32(player->GetDisplayId());
        data << uint8(player->GetByteValue(PLAYER_BYTES, 1));       // face
        data << uint8(player->getClass());

        data.WriteBitSeq<7>(guid);
        data.WriteBitSeq<3, 1, 5, 7>(guildGuid);
        data.WriteBitSeq<6>(guid);
        data.WriteBitSeq<4>(guildGuid);
        data.WriteBitSeq<3>(guid);
        data.WriteBitSeq<6>(guildGuid);
        data.WriteBits(11, 22);
        data.WriteBitSeq<5, 1, 4>(guid);
        data.WriteBitSeq<2, 0>(guildGuid);
        data.WriteBitSeq<0, 2>(guid);

        data.WriteByteSeq<5, 2>(guildGuid);
        data.WriteByteSeq<4, 6>(guid);

        static EquipmentSlots const itemSlots[] =
        {
            EQUIPMENT_SLOT_HEAD,
            EQUIPMENT_SLOT_SHOULDERS,
            EQUIPMENT_SLOT_BODY,
            EQUIPMENT_SLOT_CHEST,
            EQUIPMENT_SLOT_WAIST,
            EQUIPMENT_SLOT_LEGS,
            EQUIPMENT_SLOT_FEET,
            EQUIPMENT_SLOT_WRISTS,
            EQUIPMENT_SLOT_HANDS,
            EQUIPMENT_SLOT_BACK,
            EQUIPMENT_SLOT_TABARD,
            EQUIPMENT_SLOT_END
        };

        // Display items in visible slots
        for (EquipmentSlots const* itr = &itemSlots[0]; *itr != EQUIPMENT_SLOT_END; ++itr)
        {
            if (*itr == EQUIPMENT_SLOT_HEAD && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM))
                data << uint32(0);
            else if (*itr == EQUIPMENT_SLOT_BACK && player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK))
                data << uint32(0);
            else if (Item const* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, *itr))
                data << uint32(item->GetTemplate()->DisplayInfoID);
            else
                data << uint32(0);
        }

        data.WriteByteSeq<1>(guildGuid);
        data.WriteByteSeq<1, 3, 2, 7>(guid);
        data.WriteByteSeq<3>(guildGuid);
        data.WriteByteSeq<5>(guid);
        data.WriteByteSeq<4, 0, 6>(guildGuid);
        data.WriteByteSeq<0>(guid);
        data.WriteByteSeq<7>(guildGuid);
    }
    else
    {
        ObjectGuid guildGuid = 0;

        data << uint8(0);   // skin
        data << uint8(creator->getRace());
        data << uint8(0);   // face
        data << uint8(creator->getGender());
        data << uint8(0);   // hair
        data << uint8(0);   // haircolor
        data << uint32(creator->GetDisplayId());
        data << uint8(0);   // facialhair
        data << uint8(creator->getClass());

        data.WriteBitSeq<7>(guid);
        data.WriteBitSeq<3, 1, 5, 7>(guildGuid);
        data.WriteBitSeq<6>(guid);
        data.WriteBitSeq<4>(guildGuid);
        data.WriteBitSeq<3>(guid);
        data.WriteBitSeq<6>(guildGuid);
        data.WriteBits(0, 22);
        data.WriteBitSeq<5, 1, 4>(guid);
        data.WriteBitSeq<2, 0>(guildGuid);
        data.WriteBitSeq<0, 2>(guid);

        data.WriteByteSeq<5, 2>(guildGuid);
        data.WriteByteSeq<4, 6>(guid);

        data.WriteByteSeq<1>(guildGuid);
        data.WriteByteSeq<1, 3, 2, 7>(guid);
        data.WriteByteSeq<3>(guildGuid);
        data.WriteByteSeq<5>(guid);
        data.WriteByteSeq<4, 0, 6>(guildGuid);
        data.WriteByteSeq<0>(guid);
        data.WriteByteSeq<7>(guildGuid);
    }

    SendPacket(&data);
}

void WorldSession::HandleUpdateProjectilePosition(WorldPacket& recvPacket)
{
    TC_LOG_DEBUG("network", "WORLD: CMSG_UPDATE_PROJECTILE_POSITION");

    uint64 casterGuid;
    uint32 spellId;
    uint8 castCount;
    float x, y, z;    // Position of missile hit

    recvPacket >> casterGuid;
    recvPacket >> spellId;
    recvPacket >> castCount;
    recvPacket >> x;
    recvPacket >> y;
    recvPacket >> z;

    Unit* caster = ObjectAccessor::GetUnit(*_player, casterGuid);
    if (!caster)
        return;

    Spell* spell = caster->FindCurrentSpellBySpellId(spellId);
    if (!spell || !spell->m_targets.HasDst())
        return;

    Position pos = *spell->m_targets.GetDstPos();
    pos.Relocate(x, y, z);
    spell->m_targets.ModDst(pos);

    WorldPacket data(SMSG_SET_PROJECTILE_POSITION, 21);
    data << uint64(casterGuid);
    data << uint8(castCount);
    data << float(x);
    data << float(y);
    data << float(z);
    caster->SendMessageToSet(&data, true);
}
