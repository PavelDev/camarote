/*####################################################################################
###############################CROSSFACTION BATTLEGROUNDS#############################
####################################################################################*/

#include "CrossFactionBattleground.h"
#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Player.h"
#include "Chat.h"
#include "BattlegroundQueue.h"

uint8 Unit::getRace(bool forceoriginal) const
{
    if (GetTypeId() == TYPEID_PLAYER)
    {
        Player* pPlayer = ((Player*)this);

        if (forceoriginal || !pPlayer->GetBattleground())
            return pPlayer->getORace(); // return real race id

        if (pPlayer->InArena())
            return GetByteValue(UNIT_FIELD_BYTES_0, 0);

        if (!pPlayer->IsPlayingNative())
            return pPlayer->getFRace();
    }

    return GetByteValue(UNIT_FIELD_BYTES_0, 0);
}

bool Player::SendRealNameQuery()
{
    if (IsPlayingNative())
        return false;

    WorldPacket data(SMSG_NAME_QUERY_RESPONSE, 8 + 1 + 1 + 1 + 1 + 1 + 10);

    ObjectGuid playerGuid = GetGUID();
    ObjectGuid unkGuid = 0;

    data.WriteBitSeq<5, 7, 3, 0, 4, 1, 6, 2>(playerGuid);
    data.FlushBits();

    data.WriteByteSeq<7, 4, 3>(playerGuid);

    data << uint8(0);
    data << uint32(0);
    data << uint8(getRace());
    data << uint8(!getGender());
    data << uint8(getLevel());
    data << uint8(getClass());
    data << uint32(realmID);

    data.WriteByteSeq<1, 5, 0, 6, 2>(playerGuid);

    data.WriteBitSeq<6>(playerGuid);
    data.WriteBitSeq<7>(unkGuid);
    data.WriteBits(GetName().length(), 6);
    data.WriteBitSeq<1, 7, 2>(playerGuid);
    data.WriteBitSeq<4>(unkGuid);
    data.WriteBitSeq<4, 0>(playerGuid);
    data.WriteBitSeq<1>(unkGuid);

    auto const &declinedNames = GetDeclinedNames();

    if (declinedNames)
    {
        for (auto const &name : declinedNames->name)
            data.WriteBits(name.size(), 7);
    }
    else
    {
        for (uint8 i = 0; i < MAX_DECLINED_NAME_CASES; ++i)
            data.WriteBits(0, 7);
    }

    data.WriteBitSeq<3>(unkGuid);
    data.WriteBitSeq<3>(playerGuid);
    data.WriteBitSeq<5, 0>(unkGuid);
    data.WriteBitSeq<5>(playerGuid);
    data.WriteBit(false); // unk
    data.WriteBitSeq<2, 6>(unkGuid);
    data.FlushBits();

    data.WriteString(GetName());
    data.WriteByteSeq<4>(playerGuid);
    data.WriteByteSeq<3>(unkGuid);
    data.WriteByteSeq<6>(playerGuid);
    data.WriteByteSeq<2, 4>(unkGuid);
    data.WriteByteSeq<5, 1, 7>(playerGuid);

    if (declinedNames)
    {
        for (auto const &name : declinedNames->name)
            data.WriteString(name);
    }

    data.WriteByteSeq<3>(playerGuid);
    data.WriteByteSeq<7, 1, 6>(unkGuid);
    data.WriteByteSeq<0>(playerGuid);
    data.WriteByteSeq<0>(unkGuid);
    data.WriteByteSeq<2>(playerGuid);
    data.WriteByteSeq<5>(unkGuid);

    GetSession()->SendPacket(&data);
    return true;
}

uint32 _getRace(uint32 lel)
{
    switch (lel)
    {
        case RACE_HUMAN: return RACE_ORC;
        case RACE_ORC: return RACE_HUMAN;
        case RACE_DWARF: return RACE_UNDEAD_PLAYER;
        case RACE_NIGHTELF: return RACE_TAUREN;
        case RACE_UNDEAD_PLAYER: return RACE_DWARF;
        case RACE_TAUREN: return RACE_NIGHTELF;
        case RACE_GNOME: return RACE_TROLL;
        case RACE_TROLL: return RACE_GNOME;
        case RACE_GOBLIN: return RACE_WORGEN;
        case RACE_BLOODELF: return RACE_DRAENEI;
        case RACE_DRAENEI: return RACE_BLOODELF;
        case RACE_WORGEN: return RACE_GOBLIN;
        case RACE_PANDAREN_ALLI: return RACE_PANDAREN_HORDE;
        case RACE_PANDAREN_HORDE: return RACE_PANDAREN_ALLI;
        default: return 0;
    }

    return 0;
};

void Player::SetFakeRace()
{
    m_fakeRace = _getRace(getRace());
}

bool Player::SendBattleGroundChat(uint32 msgtype, std::string message)
{
    // Select distance to broadcast to.
    float distance = msgtype == CHAT_MSG_SAY ? sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY) : sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_YELL);

    if (Battleground* pBattleGround = GetBattleground())
    {
        if (pBattleGround->isArena()) // Only fake chat in BG's. CFBG should not interfere with arenas.
            return false;

        for (Battleground::BattlegroundPlayerMap::const_iterator itr = pBattleGround->GetPlayers().begin(); itr != pBattleGround->GetPlayers().end(); ++itr)
        {
            if (Player* pPlayer = ObjectAccessor::FindPlayer(itr->first))
            {
                if (GetDistance2d(pPlayer->GetPositionX(), pPlayer->GetPositionY()) <= distance)
                {
                    WorldPacket data(SMSG_MESSAGE_CHAT, 200);

                    if (GetTeam() == pPlayer->GetTeam())
                        BuildPlayerChat(&data, msgtype, message, LANG_UNIVERSAL);
                    else if (msgtype != CHAT_MSG_EMOTE)
                        BuildPlayerChat(&data, msgtype, message, pPlayer->GetTeam() == ALLIANCE ? LANG_ORCISH : LANG_COMMON);

                    pPlayer->GetSession()->SendPacket(&data);
                }
            }
        }
        return true;
    }
    else
        return false;
}

void Player::FitPlayerInTeam(bool action, Battleground* pBattleGround)
{
    if (!pBattleGround)
        pBattleGround = GetBattleground();
    if ((!pBattleGround || pBattleGround->isArena()) && action)
        return;
    else if (!IsPlayingNative() && action)
        setFactionForRace(getRace());
    else
        setFactionForRace(getORace());

    if (action)
        SetForgetBGPlayers(true);
    else
        SetForgetInListPlayers(true);

    if (pBattleGround && action)
        SendChatMessage("%s: You are playing for the %s in this battleground!", GetTeam() == ALLIANCE ? "|TInterface\\icons\\Achievement_pvp_a_a:25|t" : "|TInterface\\icons\\Achievement_pvp_h_h:25|t", GetTeam() == ALLIANCE ? "Alliance" : "Horde");
}

void Player::DoForgetPlayersInList()
{
    // m_FakePlayers is filled from a vector within the battleground
    // they were in previously so all players that have been in that BG will be invalidated.
    for (fakePlayers::const_iterator itr = m_fakePlayers.begin(); itr != m_fakePlayers.end(); ++itr)
    {
        WorldPacket data(SMSG_INVALIDATE_PLAYER, 8);
        ObjectGuid guid = *itr;
        data.WriteBitSeq<7, 2, 5, 1, 3, 0, 6, 4>(guid);
        data.WriteByteSeq<2, 5, 1, 0, 7, 3, 4, 6>(guid);
        GetSession()->SendPacket(&data);
        if (Player* pPlayer = ObjectAccessor::FindPlayer(*itr))
            GetSession()->SendNameQueryOpcode(pPlayer->GetGUID());
    }

    m_fakePlayers.clear();
}

void Player::DoForgetPlayersInBG(Battleground* pBattleGround)
{
    for (auto itr = pBattleGround->GetPlayers().begin(); itr != pBattleGround->GetPlayers().end(); ++itr)
    {
        // Here we invalidate players in the bg to the added player
        WorldPacket data1(SMSG_INVALIDATE_PLAYER, 8);
        ObjectGuid guid = itr->first;
        data1.WriteBitSeq<7, 2, 5, 1, 3, 0, 6, 4>(guid);
        data1.WriteByteSeq<2, 5, 1, 0, 7, 3, 4, 6>(guid);
        GetSession()->SendPacket(&data1);

        if (Player* pPlayer = ObjectAccessor::FindPlayer(itr->first))
        {
            GetSession()->SendNameQueryOpcode(pPlayer->GetGUID()); // Send namequery answer instantly if player is available
            // Here we invalidate the player added to players in the bg
            WorldPacket data2(SMSG_INVALIDATE_PLAYER, 8);
            ObjectGuid guid = GetGUID();
            data2.WriteBitSeq<7, 2, 5, 1, 3, 0, 6, 4>(guid);
            data2.WriteByteSeq<2, 5, 1, 0, 7, 3, 4, 6>(guid);
            pPlayer->GetSession()->SendPacket(&data2);
            pPlayer->GetSession()->SendNameQueryOpcode(GetGUID());
        }
    }
}

bool BattlegroundQueue::CheckCrossFactionMatch(Battleground* bg, BattlegroundBracketId bracket_id)
{
    if (!sWorld->getBoolConfig(BATTLEGROUND_CROSSFACTION_ENABLED) || bg->isArena())
        return false; // Only do this if crossbg's are enabled.

    // Here we will add all players to selectionpool, later we check if there are enough and launch a bg.
    FillCrossPlayersToBG(bg, bracket_id, true);

    if (sBattlegroundMgr->isTesting() && (m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() || m_SelectionPools[TEAM_HORDE].GetPlayerCount()))
        return true;

    uint8 MPT = bg->GetMinPlayersPerTeam();
    if (m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() < MPT || m_SelectionPools[TEAM_HORDE].GetPlayerCount() < MPT)
        return false;

    return true;
}

// This function will invite players in the least populated faction, which makes battleground queues much faster.
// This function will return true if cross faction battlegrounds are enabled, otherwise return false,
// which is useful in FillPlayersToBG. Because then we can interrupt the regular invitation if cross faction bg's are enabled.
bool BattlegroundQueue::FillCrossPlayersToBG(Battleground* bg, BattlegroundBracketId bracket_id, bool start)
{
    uint8 queuedPeople = 0;
    for (GroupsQueueType::const_iterator itr = m_QueuedGroups[bracket_id][BG_QUEUE_MIXED].begin(); itr != m_QueuedGroups[bracket_id][BG_QUEUE_MIXED].end(); ++itr)
        if (!(*itr)->IsInvitedToBGInstanceGUID)
            queuedPeople += (*itr)->Players.size();

    if (sWorld->getBoolConfig(BATTLEGROUND_CROSSFACTION_ENABLED) && (sBattlegroundMgr->isTesting() || queuedPeople >= bg->GetMinPlayersPerTeam()*2 || !start))
    {
        int32 aliFree   = start ? bg->GetMaxPlayersPerTeam() : bg->GetFreeSlotsForTeam(ALLIANCE);
        int32 hordeFree = start ? bg->GetMaxPlayersPerTeam() : bg->GetFreeSlotsForTeam(HORDE);
        // Empty selection pools. They will be refilled from queued groups.
        m_SelectionPools[TEAM_ALLIANCE].Init();
        m_SelectionPools[TEAM_HORDE].Init();
        int32 valiFree = aliFree;
        int32 vhordeFree = hordeFree;
        int32 diff = 0;

        // Add teams to their own factions as far as possible.
        if (start)
        {
            QueuedGroupMap m_PreGroupMap_a, m_PreGroupMap_h;
            int32 m_SmallestOfTeams = 0;
            int32 queuedAlliance = 0;
            int32 queuedHorde = 0;

            for (GroupsQueueType::const_iterator itr = m_QueuedGroups[bracket_id][BG_QUEUE_MIXED].begin(); itr != m_QueuedGroups[bracket_id][BG_QUEUE_MIXED].end(); ++itr)
            {
                if ((*itr)->IsInvitedToBGInstanceGUID)
                    continue;

                bool alliance = (*itr)->OriginalTeam == ALLIANCE;

                if (alliance)
                {
                    m_PreGroupMap_a.insert(std::make_pair((*itr)->Players.size(), *itr));
                    queuedAlliance += (*itr)->Players.size();
                }
                else
                {
                    m_PreGroupMap_h.insert(std::make_pair((*itr)->Players.size(), *itr));
                    queuedHorde += (*itr)->Players.size();
                }
            }

            m_SmallestOfTeams = std::min(std::min(aliFree, queuedAlliance), std::min(hordeFree, queuedHorde));

            valiFree -= PreAddPlayers(m_PreGroupMap_a, m_SmallestOfTeams, aliFree);
            vhordeFree -= PreAddPlayers(m_PreGroupMap_h, m_SmallestOfTeams, hordeFree);
        }

        QueuedGroupMap m_QueuedGroupMap;

        for (GroupsQueueType::const_iterator itr = m_QueuedGroups[bracket_id][BG_QUEUE_MIXED].begin(); itr != m_QueuedGroups[bracket_id][BG_QUEUE_MIXED].end(); ++itr)
            m_QueuedGroupMap.insert(std::make_pair((*itr)->Players.size(), *itr));

        for (QueuedGroupMap::reverse_iterator itr = m_QueuedGroupMap.rbegin(); itr != m_QueuedGroupMap.rend(); ++itr)
        {
            GroupsQueueType allypool = m_SelectionPools[TEAM_ALLIANCE].SelectedGroups;
            GroupsQueueType hordepool = m_SelectionPools[TEAM_HORDE].SelectedGroups;

            GroupQueueInfo* ginfo = itr->second;

            // If player already was invited via pre adding (add to own team first) or he was already invited to a bg, skip.
            if (ginfo->IsInvitedToBGInstanceGUID ||
                std::find(allypool.begin(), allypool.end(), ginfo) != allypool.end() ||
                std::find(hordepool.begin(), hordepool.end(), ginfo) != hordepool.end() ||
                (m_SelectionPools[TEAM_ALLIANCE].GetPlayerCount() >= bg->GetMinPlayersPerTeam() &&
                m_SelectionPools[TEAM_HORDE].GetPlayerCount() >= bg->GetMinPlayersPerTeam()))
                continue;

            diff = abs(valiFree - vhordeFree);
            bool moreAli = valiFree < vhordeFree;
            if (diff > 0)
                ginfo->Team = moreAli ? HORDE : ALLIANCE;

            bool alliance = ginfo->Team == ALLIANCE;
            if (m_SelectionPools[alliance ? TEAM_ALLIANCE : TEAM_HORDE].AddGroup(ginfo, alliance ? aliFree : hordeFree))
                alliance ? valiFree -= ginfo->Players.size() : vhordeFree -= ginfo->Players.size();
        }

        return true;
    }
    return false;
}

int32 BattlegroundQueue::PreAddPlayers(QueuedGroupMap m_PreGroupMap, int32 MaxAdd, uint32 MaxInTeam)
{
    int32 LeftToAdd = MaxAdd;
    uint32 Added = 0;

    for (QueuedGroupMap::reverse_iterator itr = m_PreGroupMap.rbegin(); itr != m_PreGroupMap.rend(); ++itr)
    {
        int32 PlayerSize = itr->first;
        bool alliance = itr->second->OriginalTeam == ALLIANCE;

        if (PlayerSize <= LeftToAdd && m_SelectionPools[alliance ? TEAM_ALLIANCE : TEAM_HORDE].AddGroup(itr->second, MaxInTeam))
            LeftToAdd -= PlayerSize, Added -= PlayerSize;
    }

    return LeftToAdd;
}

void Player::SendChatMessage(const char *format, ...)
{
    if (!IsInWorld())
        return;

    if (format)
    {
        va_list ap;
        char str [2048];
        va_start(ap, format);
        vsnprintf(str, 2048, format, ap);
        va_end(ap);

        ChatHandler(GetSession()).SendSysMessage(str);
    }
}