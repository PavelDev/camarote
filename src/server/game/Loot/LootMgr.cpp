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

#include "LootMgr.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "World.h"
#include "Util.h"
#include "SharedDefines.h"
#include "SpellMgr.h"
#include "SpellInfo.h"
#include "Group.h"
#include "Containers.h"

namespace {

void writeLootItemData(LootItem const &li, uint8 index, uint8 slotType,
                       ByteBuffer &bitBuffer, ByteBuffer &dataBuffer)
{
    bitBuffer.WriteBit(true);
    bitBuffer.WriteBit(index == 0);
    bitBuffer.WriteBits(1, 2);
    bitBuffer.WriteBit(0);
    bitBuffer.WriteBits(slotType, 3);

    if (index)
        dataBuffer << index;

    dataBuffer << uint32(li.count);
    dataBuffer << uint32(sObjectMgr->GetItemTemplate(li.itemid)->DisplayInfoID);
    dataBuffer << uint32(li.randomSuffix);
    dataBuffer << uint32(li.randomPropertyId);
    dataBuffer << uint32(0);
    dataBuffer << uint32(li.itemid);
}

} // namespace

static Rates const qualityToRate[MAX_ITEM_QUALITY] =
{
    RATE_DROP_ITEM_POOR,                                    // ITEM_QUALITY_POOR
    RATE_DROP_ITEM_NORMAL,                                  // ITEM_QUALITY_NORMAL
    RATE_DROP_ITEM_UNCOMMON,                                // ITEM_QUALITY_UNCOMMON
    RATE_DROP_ITEM_RARE,                                    // ITEM_QUALITY_RARE
    RATE_DROP_ITEM_EPIC,                                    // ITEM_QUALITY_EPIC
    RATE_DROP_ITEM_LEGENDARY,                               // ITEM_QUALITY_LEGENDARY
    RATE_DROP_ITEM_ARTIFACT,                                // ITEM_QUALITY_ARTIFACT
};

LootStore LootTemplates_Creature("creature_loot_template",           "creature entry",                  true);
LootStore LootTemplates_Disenchant("disenchant_loot_template",       "item disenchant id",              true);
LootStore LootTemplates_Fishing("fishing_loot_template",             "area id",                         true);
LootStore LootTemplates_Gameobject("gameobject_loot_template",       "gameobject entry",                true);
LootStore LootTemplates_Item("item_loot_template",                   "item entry",                      true);
LootStore LootTemplates_Mail("mail_loot_template",                   "mail template id",                false);
LootStore LootTemplates_Milling("milling_loot_template",             "item entry (herb)",               true);
LootStore LootTemplates_Pickpocketing("pickpocketing_loot_template", "creature pickpocket lootid",      true);
LootStore LootTemplates_Prospecting("prospecting_loot_template",     "item entry (ore)",                true);
LootStore LootTemplates_Reference("reference_loot_template",         "reference id",                    false);
LootStore LootTemplates_Skinning("skinning_loot_template",           "creature skinning id",            true);
LootStore LootTemplates_Spell("spell_loot_template",                 "spell id (random item creating)", false);

class LootTemplate::LootGroup                               // A set of loot definitions for items (refs are not allowed)
{
    public:
        void AddEntry(LootStoreItem const &item);           // Adds an entry to the group (at loading stage)
        bool HasQuestDrop() const;                          // True if group includes at least 1 quest drop entry
        bool HasQuestDropForPlayer(Player const* player) const;
                                                            // The same for active quests of the player
        void Process(Loot& loot, uint16 lootMode) const;    // Rolls an item from the group (if any) and adds the item to the loot
        float RawTotalChance() const;                       // Overall chance for the group (without equal chanced items)
        float TotalChance() const;                          // Overall chance for the group

        void GetCompatableLFRItems(LootStoreItemList &compatableItems, uint32 specialisation) const;
        bool IsEligibleForLFRLootItem(LootStoreItem const &item, uint32 specialisation) const;

        void Verify(LootStore const& lootstore, uint32 id, uint8 group_id) const;
        void CollectLootIds(LootIdSet& set) const;
        void CheckLootRefs(LootTemplateMap const& store, LootIdSet* ref_set) const;
        LootStoreItemList* GetExplicitlyChancedItemList() { return &ExplicitlyChanced; }
        LootStoreItemList* GetEqualChancedItemList() { return &EqualChanced; }
        void CopyConditions(ConditionList conditions);
    private:
        LootStoreItemList ExplicitlyChanced;                // Entries with chances defined in DB
        LootStoreItemList EqualChanced;                     // Zero chances - every entry takes the same chance

        LootStoreItem const* Roll() const;                 // Rolls an item from the group, returns NULL if all miss their chances
};

//Remove all data and free all memory
void LootStore::Clear()
{
    for (LootTemplateMap::const_iterator itr=m_LootTemplates.begin(); itr != m_LootTemplates.end(); ++itr)
        delete itr->second;
    m_LootTemplates.clear();
}

// Checks validity of the loot store
// Actual checks are done within LootTemplate::Verify() which is called for every template
void LootStore::Verify() const
{
    for (LootTemplateMap::const_iterator i = m_LootTemplates.begin(); i != m_LootTemplates.end(); ++i)
        i->second->Verify(*this, i->first);
}

// Loads a *_loot_template DB table into loot store
// All checks of the loaded template are called from here, no error reports at loot generation required
uint32 LootStore::LoadLootTable()
{
    LootTemplateMap::const_iterator tab;

    // Clearing store (for reloading case)
    Clear();

    //                                                  0     1            2               3         4         5             6
    QueryResult result = WorldDatabase.PQuery("SELECT entry, item, ChanceOrQuestChance, lootmode, groupid, mincountOrRef, maxcount FROM %s", GetName());

    if (!result)
        return 0;

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 entry               = fields[0].GetUInt32();
        uint32 item                = fields[1].GetUInt32();
        float  chanceOrQuestChance = fields[2].GetFloat();
        uint16 lootmode            = fields[3].GetUInt16();
        uint8  group               = fields[4].GetUInt8();
        int32  mincountOrRef       = fields[5].GetInt32();
        int32  maxcount            = fields[6].GetUInt8();

        if (maxcount > std::numeric_limits<uint8>::max())
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: maxcount value (%u) to large. must be less %u - skipped", GetName(), entry, item, maxcount, std::numeric_limits<uint8>::max());
            continue;                                   // error already printed to log/console.
        }

        if (group >= 1 << 7)                                     // it stored in 7 bit field
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: group (%u) must be less %u - skipped", GetName(), entry, item, group, 1 << 7);
            return false;
        }

        LootStoreItem storeitem = LootStoreItem(item, chanceOrQuestChance, lootmode, group, mincountOrRef, maxcount);

        if (!storeitem.IsValid(*this, entry))            // Validity checks
            continue;

        // Looking for the template of the entry
                                                        // often entries are put together
        if (m_LootTemplates.empty() || tab->first != entry)
        {
            // Searching the template (in case template Id changed)
            tab = m_LootTemplates.find(entry);
            if (tab == m_LootTemplates.end())
            {
                std::pair< LootTemplateMap::iterator, bool > pr = m_LootTemplates.insert(LootTemplateMap::value_type(entry, new LootTemplate));
                tab = pr.first;
            }
        }
        // else is empty - template Id and iter are the same
        // finally iter refers to already existed or just created <entry, LootTemplate>

        // Adds current row to the template
        tab->second->AddEntry(storeitem);
        ++count;
    }
    while (result->NextRow());

    Verify();                                           // Checks validity of the loot store

    return count;
}

bool LootStore::HaveQuestLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator itr = m_LootTemplates.find(loot_id);
    if (itr == m_LootTemplates.end())
        return false;

    // scan loot for quest items
    return itr->second->HasQuestDrop(m_LootTemplates);
}

bool LootStore::HaveQuestLootForPlayer(uint32 loot_id, Player* player) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);
    if (tab != m_LootTemplates.end())
        if (tab->second->HasQuestDropForPlayer(m_LootTemplates, player))
            return true;

    return false;
}

void LootStore::ResetConditions()
{
    for (LootTemplateMap::iterator itr = m_LootTemplates.begin(); itr != m_LootTemplates.end(); ++itr)
    {
        ConditionList empty;
        (*itr).second->CopyConditions(empty);
    }
}

LootTemplate const* LootStore::GetLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);

    if (tab == m_LootTemplates.end())
        return NULL;

    return tab->second;
}

LootTemplate* LootStore::GetLootForConditionFill(uint32 loot_id)
{
    LootTemplateMap::iterator tab = m_LootTemplates.find(loot_id);

    if (tab == m_LootTemplates.end())
        return NULL;

    return tab->second;
}

uint32 LootStore::LoadAndCollectLootIds(LootIdSet& lootIdSet)
{
    uint32 count = LoadLootTable();

    for (LootTemplateMap::const_iterator tab = m_LootTemplates.begin(); tab != m_LootTemplates.end(); ++tab)
        lootIdSet.insert(tab->first);

    return count;
}

void LootStore::CheckLootRefs(LootIdSet* ref_set) const
{
    for (LootTemplateMap::const_iterator ltItr = m_LootTemplates.begin(); ltItr != m_LootTemplates.end(); ++ltItr)
        ltItr->second->CheckLootRefs(m_LootTemplates, ref_set);
}

void LootStore::ReportUnusedIds(LootIdSet const& lootIdSet) const
{
    // all still listed ids isn't referenced
    for (LootIdSet::const_iterator itr = lootIdSet.begin(); itr != lootIdSet.end(); ++itr)
        TC_LOG_ERROR("sql.sql", "Table '%s' entry %d isn't %s and not referenced from loot, and then useless.", GetName(), *itr, GetEntryName());
}

void LootStore::ReportNotExistedId(uint32 id) const
{
    TC_LOG_ERROR("sql.sql", "Table '%s' entry %d (%s) does not exist but used as loot id in DB.", GetName(), id, GetEntryName());
}

//
// --------- LootStoreItem ---------
//

// Checks if the entry (quest, non-quest, reference) takes it's chance (at loot generation)
// RATE_DROP_ITEMS is no longer used for all types of entries
bool LootStoreItem::Roll(bool rate) const
{
    if (chance >= 100.0f)
        return true;

    if (mincountOrRef < 0)                                   // reference case
        return roll_chance_f(chance* (rate ? sWorld->getRate(RATE_DROP_ITEM_REFERENCED) : 1.0f));

    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemid);

    float qualityModifier = pProto && rate ? sWorld->getRate(qualityToRate[pProto->Quality]) : 1.0f;

    return roll_chance_f(chance*qualityModifier);
}

// Checks correctness of values
bool LootStoreItem::IsValid(LootStore const& store, uint32 entry) const
{
    if (mincountOrRef == 0)
    {
        TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: wrong mincountOrRef (%d) - skipped", store.GetName(), entry, itemid, mincountOrRef);
        return false;
    }

    if (mincountOrRef > 0)                                  // item (quest or non-quest) entry, maybe grouped
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
        if (!proto)
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: item entry not listed in `item_template` - skipped", store.GetName(), entry, itemid);
            return false;
        }

        if (chance == 0 && group == 0)                      // Zero chance is allowed for grouped entries only
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: equal-chanced grouped entry, but group not defined - skipped", store.GetName(), entry, itemid);
            return false;
        }

        if (chance != 0 && chance < 0.000001f)             // loot with low chance
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: low chance (%f) - skipped",
                store.GetName(), entry, itemid, chance);
            return false;
        }

        if (maxcount < mincountOrRef)                       // wrong max count
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: max count (%u) less that min count (%i) - skipped", store.GetName(), entry, itemid, int32(maxcount), mincountOrRef);
            return false;
        }
    }
    else                                                    // mincountOrRef < 0
    {
        if (needs_quest)
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: quest chance will be treated as non-quest chance", store.GetName(), entry, itemid);
        else if (chance == 0)                              // no chance for the reference
        {
            TC_LOG_ERROR("sql.sql", "Table '%s' entry %d item %d: zero chance is specified for a reference, skipped", store.GetName(), entry, itemid);
            return false;
        }
    }
    return true;                                            // Referenced template existence is checked at whole store level
}

//
// --------- LootItem ---------
//

// Constructor, copies most fields from LootStoreItem and generates random count
LootItem::LootItem(LootStoreItem const& li)
{
    itemid      = li.itemid;
    conditions   = li.conditions;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemid);
    freeforall  = proto && (proto->Flags & ITEM_PROTO_FLAG_PARTY_LOOT);
    follow_loot_rules = proto && (proto->FlagsCu & ITEM_FLAGS_CU_FOLLOW_LOOT_RULES);

    needs_quest = li.needs_quest;

    count       = urand(li.mincountOrRef, li.maxcount);     // constructor called for mincountOrRef > 0 only
    randomSuffix = GenerateEnchSuffixFactor(itemid);
    randomPropertyId = Item::GenerateItemRandomPropertyId(itemid);
    is_looted = 0;
    is_blocked = 0;
    is_underthreshold = 0;
    is_counted = 0;
    canSave = true;
}

// Basic checks for player/item compatibility - if false no chance to see the item in the loot
bool LootItem::AllowedForPlayer(Player const* player) const
{
    // DB conditions check
    if (!sConditionMgr->IsObjectMeetToConditions(const_cast<Player*>(player), conditions))
        return false;

    ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemid);
    if (!pProto)
        return false;

    // not show loot for players without profession or those who already know the recipe
    if ((pProto->Flags & ITEM_PROTO_FLAG_SMART_LOOT) && (!player->HasSkill(pProto->RequiredSkill) || player->HasSpell(pProto->Spells[1].SpellId)))
        return false;

    // not show loot for not own team
    if ((pProto->Flags2 & ITEM_FLAGS_EXTRA_HORDE_ONLY) && player->GetTeam() != HORDE)
        return false;

    if ((pProto->Flags2 & ITEM_FLAGS_EXTRA_ALLIANCE_ONLY) && player->GetTeam() != ALLIANCE)
        return false;

    // check quest requirements
    if (!(pProto->FlagsCu & ITEM_FLAGS_CU_IGNORE_QUEST_STATUS) && ((needs_quest || (pProto->StartQuest && player->GetQuestStatus(pProto->StartQuest) != QUEST_STATUS_NONE)) && !player->HasQuestForItem(itemid)))
        return false;

    return true;
}

void LootItem::AddAllowedLooter(const Player* player)
{
    allowedGUIDs.insert(player->GetGUIDLow());
}

//
// --------- Loot ---------
//

// Inserts the item into the loot (called by LootTemplate processors)
void Loot::AddItem(LootStoreItem const & item)
{
    if (shared)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
        if (!proto)
            return;

        if (item.needs_quest || !item.conditions.empty() || (proto->Flags && ITEM_PROTO_FLAG_PARTY_LOOT))
        {
            items.push_back(LootItem(item));
            
            // non-conditional one-player only items are counted here,
            // free for all items are counted in FillFFALoot(),
            // non-ffa conditionals are counted in FillNonQuestNonFFAConditionalLoot()
            if (!item.needs_quest && item.conditions.empty() && !(proto->Flags & ITEM_PROTO_FLAG_PARTY_LOOT))
                ++unlootedCount;
        }
        return;
    }

    if (item.needs_quest)                                   // Quest drop
    {
        if (quest_items.size() < MAX_NR_QUEST_ITEMS)
            quest_items.push_back(LootItem(item));
    }
    else if (items.size() < MAX_NR_LOOT_ITEMS)              // Non-quest drop
    {
        items.push_back(LootItem(item));

        // non-conditional one-player only items are counted here,
        // free for all items are counted in FillFFALoot(),
        // non-ffa conditionals are counted in FillNonQuestNonFFAConditionalLoot()
        if (item.conditions.empty())
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item.itemid);
            if (!proto || (proto->Flags & ITEM_PROTO_FLAG_PARTY_LOOT) == 0)
                ++unlootedCount;
        }
    }
}

// fill LFR item for a single raid memeber
void Loot::FillLFRLoot(uint32 lootId, LootStore const& store, Player* member)
{
    if (!member)
        return;

    auto lootTemplate = store.GetLootFor(lootId);
    if (!lootTemplate)
        return;

    items.reserve(MAX_NR_LOOT_ITEMS);
    lootTemplate->ProcessLFRItem(*this, member->GetLootSpecOrClassSpec());
}

// fill LFR gold for a single raid member
void Loot::FillLFRMoney(uint32 maxGold, uint32 minGold, uint32 groupSize, bool wonItem)
{
    // gold fall back
    if (!maxGold)
        maxGold = 3300000;
    if (!minGold)
        minGold = 3200000;

    uint32 rewardGold = 0;
    if (maxGold > 0)
    {
        // calculate base gold amount
        if (maxGold <= minGold)
            rewardGold = uint32(maxGold * sWorld->getRate(RATE_DROP_MONEY));
        else if ((maxGold - minGold) < 32700)
            rewardGold = uint32(urand(minGold, maxGold) * sWorld->getRate(RATE_DROP_MONEY));
        else
            rewardGold = uint32(urand(minGold >> 8, maxGold >> 8) * sWorld->getRate(RATE_DROP_MONEY)) << 8;

        // split by amount of raid members in the group
        rewardGold /= groupSize;

        // if no item was awarded, give bonus gold amount
        if (!wonItem)
            rewardGold += urand(rewardGold * 0.05f, rewardGold * 0.10f);
    }

    gold = rewardGold;
}

// Calls processor of corresponding LootTemplate (which handles everything including references)
bool Loot::FillLoot(uint32 lootId, LootStore const& store, Player* lootOwner, bool personal, bool noEmptyError, uint16 lootMode /*= LOOT_MODE_DEFAULT*/)
{
    // Must be provided
    if (!lootOwner)
        return false;

    LootTemplate const* tab = store.GetLootFor(lootId);
    if (!tab)
    {
        if (!noEmptyError)
            TC_LOG_ERROR("sql.sql", "Table '%s' loot id #%u used but it doesn't have records.", store.GetName(), lootId);
        return false;
    }

    items.reserve(MAX_NR_LOOT_ITEMS);
    quest_items.reserve(MAX_NR_QUEST_ITEMS);

    tab->Process(*this, store.IsRatesAllowed(), lootMode);          // Processing is done there, callback via Loot::AddItem()

    // Setting access rights for group loot case
    Group* group = lootOwner->GetGroup();
    if (!personal && group)
    {
        roundRobinPlayer = lootOwner->GetGUID();

        for (GroupReference* itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
            if (Player* player = itr->GetSource())   // should actually be looted object instead of lootOwner but looter has to be really close so doesnt really matter
                FillNotNormalLootFor(player, player->IsAtGroupRewardDistance(lootOwner));

        for (uint8 i = 0; i < items.size(); ++i)
        {
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(items[i].itemid))
                if (proto->Quality < uint32(group->GetLootThreshold()))
                    items[i].is_underthreshold = true;
        }
    }
    // ... for personal loot
    else
        FillNotNormalLootFor(lootOwner, true);

    if ((lootId == 90406 || lootId == 90399 || lootId == 90397 || lootId == 90400 || lootId == 90398 || lootId == 90395 || lootId == 90401)
            && lootMode == LOOT_MODE_DEFAULT)
    {
        for (auto const &itemCurrent : items)
            for (auto spellId : sSpellMgr->mSpellCreateItemList)
                if (auto const spellInfo = sSpellMgr->GetSpellInfo(spellId))
                    if (spellInfo->Effects[EFFECT_0].ItemType == itemCurrent.itemid)
                        if (!lootOwner->HasActiveSpell(spellId))
                            lootOwner->learnSpell(spellId, false);
    }

    return true;
}

bool Loot::FillSharedLoot(uint32 lootId, LootStore const& store, std::list<uint64> looters, Creature* creature, bool noEmptyError /*= false*/, uint16 lootMode /*= LOOT_MODE_DEFAULT*/)
{
    LootTemplate const* tab = store.GetLootFor(lootId);
    if (!tab)
    {
        if (!noEmptyError)
            TC_LOG_ERROR("sql", "Table '%s' loot id #%u used but it doesn't have records.", store.GetName(), lootId);
        return false;
    }

    items.reserve(MAX_NR_LOOT_ITEMS);
    quest_items.reserve(MAX_NR_QUEST_ITEMS);

    bool filled = false;
    // between 3 and 6 raid members minimum must win item loot
    uint32 itemWinCount = urand(uint32(CalculatePct(looters.size(), 12)), uint32(CalculatePct(looters.size(), 24)));
    uint32 itemWinCounter = 0;

    // handle loot for each member
    uint32 memberCounter = 0;

    auto lfrLootBind = sObjectMgr->GetLFRLootBind(creature->GetEntry(), creature->GetTypeId());
    if (!lfrLootBind)
    {
        TC_LOG_ERROR("general", "LFR loot bind invalid in Loot::FIllSharedLoot for creature entry %u", creature->GetEntry());
        return false;
    }

    for (std::list<uint64>::const_iterator i = looters.begin(), end = looters.end(); i != end; ++i)
    {
        Player *player = ObjectAccessor::FindPlayer(*i);
        if (!player)
            continue;

        if (player->HasLFRLootBind(lfrLootBind->Id))
            continue;
        
        bool wonItem = false;
        uint8 personalChance = sWorld->getIntConfig(CONFIG_PERSONAL_LOOT_CHANCE);
        // loot quota is not being met, automatically give raid member loot
        if ((looters.size() - memberCounter) < (itemWinCount - itemWinCounter))
            wonItem = true;
        else if (roll_chance_i(urand(personalChance - 5, personalChance + 5))) // otherwise raid member has 25-30% chance to win loot
            wonItem = true;

        if (wonItem)
        {
            itemWinCounter++;
            player->SetLFRLootBind(lfrLootBind->Id);
            tab->Process(*this, player->GetMap()->GetDifficulty(), store.IsRatesAllowed(), lootMode);
            player->SendPersonalLoot(tab, player->GetMap()->GetDifficulty(), lootMode);
        }
        
        memberCounter++;
    }

    return true;
}

void Loot::FillNotNormalLootFor(Player* player, bool presentAtLooting)
{
    uint32 plguid = player->GetGUIDLow();

    QuestItemMap::const_iterator qmapitr = PlayerQuestItems.find(plguid);
    if (qmapitr == PlayerQuestItems.end())
        FillQuestLoot(player);

    qmapitr = PlayerFFAItems.find(plguid);
    if (qmapitr == PlayerFFAItems.end())
        FillFFALoot(player);

    qmapitr = PlayerNonQuestNonFFAConditionalItems.find(plguid);
    if (qmapitr == PlayerNonQuestNonFFAConditionalItems.end())
        FillNonQuestNonFFAConditionalLoot(player, presentAtLooting);

    // if not auto-processed player will have to come and pick it up manually
    if (!presentAtLooting)
        return;

    // Process currency items
    uint32 max_slot = GetMaxSlotInLootFor(player);
    LootItem const* item = NULL;
    uint32 itemsSize = uint32(items.size());
    for (uint32 i = 0; i < max_slot; ++i)
    {
        if (i < items.size())
            item = &items[i];
        else
            item = &quest_items[i-itemsSize];

        if (!item->is_looted && item->freeforall && item->AllowedForPlayer(player))
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(item->itemid))
                if (proto->IsCurrencyToken())
                    player->StoreLootItem(i, this);
    }
}

QuestItemList* Loot::FillFFALoot(Player* player)
{
    QuestItemList* ql = new QuestItemList();

    for (uint8 i = 0; i < items.size(); ++i)
    {
        LootItem &item = items[i];
        if (!item.is_looted && item.freeforall && item.AllowedForPlayer(player))
        {
            ql->push_back(QuestItem(i));
            ++unlootedCount;
        }
    }
    if (ql->empty())
    {
        delete ql;
        return NULL;
    }

    PlayerFFAItems[player->GetGUIDLow()] = ql;
    return ql;
}

QuestItemList* Loot::FillQuestLoot(Player* player)
{
    if (items.size() == MAX_NR_LOOT_ITEMS)
        return NULL;

    QuestItemList* ql = new QuestItemList();

    for (uint8 i = 0; i < quest_items.size(); ++i)
    {
        LootItem &item = quest_items[i];

        if (!item.is_looted && (item.AllowedForPlayer(player) || (item.follow_loot_rules && player->GetGroup() && ((player->GetGroup()->GetLootMethod() == MASTER_LOOT && player->GetGroup()->GetLooterGuid() == player->GetGUID()) || player->GetGroup()->GetLootMethod() != MASTER_LOOT ))))
        {
            ql->push_back(QuestItem(i));

            // quest items get blocked when they first appear in a
            // player's quest vector
            //
            // increase once if one looter only, looter-times if free for all
            if (item.freeforall || !item.is_blocked)
                ++unlootedCount;
            if (!player->GetGroup() || (player->GetGroup()->GetLootMethod() != GROUP_LOOT && player->GetGroup()->GetLootMethod() != ROUND_ROBIN))
                item.is_blocked = true;

            if (items.size() + ql->size() == MAX_NR_LOOT_ITEMS)
                break;
        }
    }
    if (ql->empty())
    {
        delete ql;
        return NULL;
    }

    PlayerQuestItems[player->GetGUIDLow()] = ql;
    return ql;
}

QuestItemList* Loot::FillNonQuestNonFFAConditionalLoot(Player* player, bool presentAtLooting)
{
    QuestItemList* ql = new QuestItemList();

    for (uint8 i = 0; i < items.size(); ++i)
    {
        LootItem &item = items[i];
        if (!item.is_looted && !item.freeforall && (item.AllowedForPlayer(player) || (item.follow_loot_rules && player->GetGroup() && ((player->GetGroup()->GetLootMethod() == MASTER_LOOT && player->GetGroup()->GetLooterGuid() == player->GetGUID()) || player->GetGroup()->GetLootMethod() != MASTER_LOOT ))))
        {
            if (presentAtLooting)
                item.AddAllowedLooter(player);
            if (!item.conditions.empty())
            {
                ql->push_back(QuestItem(i));
                if (!item.is_counted)
                {
                    ++unlootedCount;
                    item.is_counted = true;
                }
            }
        }
    }
    if (ql->empty())
    {
        delete ql;
        return NULL;
    }

    PlayerNonQuestNonFFAConditionalItems[player->GetGUIDLow()] = ql;
    return ql;
}

//===================================================

void Loot::NotifyItemRemoved(uint8 lootIndex)
{
    // notify all players that are looting this that the item was removed
    // convert the index to the slot the player sees
    std::set<uint64>::iterator i_next;
    for (std::set<uint64>::iterator i = PlayersLooting.begin(); i != PlayersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if (Player* player = ObjectAccessor::FindPlayer(*i))
            player->SendNotifyLootItemRemoved(lootIndex);
        else
            PlayersLooting.erase(i);
    }
}

void Loot::NotifyMoneyRemoved()
{
    // notify all players that are looting this that the money was removed
    std::set<uint64>::iterator i_next;
    for (std::set<uint64>::iterator i = PlayersLooting.begin(); i != PlayersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if (Player* player = ObjectAccessor::FindPlayer(*i))
            player->SendNotifyLootMoneyRemoved();
        else
            PlayersLooting.erase(i);
    }
}

void Loot::NotifyQuestItemRemoved(uint8 questIndex)
{
    // when a free for all questitem is looted
    // all players will get notified of it being removed
    // (other questitems can be looted by each group member)
    // bit inefficient but isn't called often

    std::set<uint64>::iterator i_next;
    for (std::set<uint64>::iterator i = PlayersLooting.begin(); i != PlayersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        if (Player* player = ObjectAccessor::FindPlayer(*i))
        {
            QuestItemMap::const_iterator pq = PlayerQuestItems.find(player->GetGUIDLow());
            if (pq != PlayerQuestItems.end() && pq->second)
            {
                // find where/if the player has the given item in it's vector
                QuestItemList& pql = *pq->second;

                uint8 j;
                for (j = 0; j < pql.size(); ++j)
                    if (pql[j].index == questIndex)
                        break;

                if (j < pql.size())
                    player->SendNotifyLootItemRemoved(items.size()+j);
            }
        }
        else
            PlayersLooting.erase(i);
    }
}

void Loot::generateMoneyLoot(uint32 minAmount, uint32 maxAmount)
{
    if (maxAmount > 0)
    {
        if (maxAmount <= minAmount)
            gold = uint32(maxAmount * sWorld->getRate(RATE_DROP_MONEY));
        else if ((maxAmount - minAmount) < 32700)
            gold = uint32(urand(minAmount, maxAmount) * sWorld->getRate(RATE_DROP_MONEY));
        else
            gold = uint32(urand(minAmount >> 8, maxAmount >> 8) * sWorld->getRate(RATE_DROP_MONEY)) << 8;
    }
}

void Loot::DeleteLootItemFromContainerItemDB(uint32 itemID)
{
    // Deletes a single item associated with an openable item from the DB
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_ITEM);
    stmt->setUInt32(0, containerID);
    stmt->setUInt32(1, itemID);
    CharacterDatabase.Execute(stmt);

    // Mark the item looted to prevent resaving
    for (LootItemList::iterator itr = items.begin(); itr != items.end(); ++itr)
    {
        if (itr->itemid != itemID)
            continue;

        itr->canSave = true;
        break;
    }
}

void Loot::DeleteLootMoneyFromContainerItemDB()
{
    // Deletes money loot associated with an openable item from the DB
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ITEMCONTAINER_MONEY);
    stmt->setUInt32(0, containerID);
    CharacterDatabase.Execute(stmt);
}

LootItem* Loot::LootItemInSlot(uint32 lootSlot, Player* player, QuestItem* *qitem, QuestItem* *ffaitem, QuestItem* *conditem)
{
    LootItem* item = NULL;
    bool is_looted = true;
    if (lootSlot >= items.size())
    {
        uint32 questSlot = lootSlot - items.size();
        QuestItemMap::const_iterator itr = PlayerQuestItems.find(player->GetGUIDLow());
        if (itr != PlayerQuestItems.end() && questSlot < itr->second->size())
        {
            QuestItem* qitem2 = &itr->second->at(questSlot);
            if (qitem)
                *qitem = qitem2;
            item = &quest_items[qitem2->index];
            is_looted = qitem2->is_looted;
        }
    }
    else
    {
        item = &items[lootSlot];
        is_looted = item->is_looted;
        if (item->freeforall)
        {
            QuestItemMap::const_iterator itr = PlayerFFAItems.find(player->GetGUIDLow());
            if (itr != PlayerFFAItems.end())
            {
                for (QuestItemList::const_iterator iter=itr->second->begin(); iter!= itr->second->end(); ++iter)
                    if (iter->index == lootSlot)
                    {
                        QuestItem* ffaitem2 = (QuestItem*)&(*iter);
                        if (ffaitem)
                            *ffaitem = ffaitem2;
                        is_looted = ffaitem2->is_looted;
                        break;
                    }
            }
        }
        else if (!item->conditions.empty())
        {
            QuestItemMap::const_iterator itr = PlayerNonQuestNonFFAConditionalItems.find(player->GetGUIDLow());
            if (itr != PlayerNonQuestNonFFAConditionalItems.end())
            {
                for (QuestItemList::const_iterator iter=itr->second->begin(); iter!= itr->second->end(); ++iter)
                {
                    if (iter->index == lootSlot)
                    {
                        QuestItem* conditem2 = (QuestItem*)&(*iter);
                        if (conditem)
                            *conditem = conditem2;
                        is_looted = conditem2->is_looted;
                        break;
                    }
                }
            }
        }
    }

    if (is_looted)
        return NULL;

    return item;
}

uint32 Loot::GetMaxSlotInLootFor(Player const *player) const
{
    QuestItemMap::const_iterator itr = PlayerQuestItems.find(player->GetGUIDLow());
    return items.size() + (itr != PlayerQuestItems.end() ?  itr->second->size() : 0);
}

// return true if there is any FFA, quest or conditional item for the player.
bool Loot::hasItemFor(Player const *player) const
{
    QuestItemMap const& lootPlayerQuestItems = GetPlayerQuestItems();
    QuestItemMap::const_iterator q_itr = lootPlayerQuestItems.find(player->GetGUIDLow());
    if (q_itr != lootPlayerQuestItems.end())
    {
        QuestItemList* q_list = q_itr->second;
        for (QuestItemList::const_iterator qi = q_list->begin(); qi != q_list->end(); ++qi)
        {
            const LootItem &item = quest_items[qi->index];
            if (!qi->is_looted && !item.is_looted)
                return true;
        }
    }

    QuestItemMap const& lootPlayerFFAItems = GetPlayerFFAItems();
    QuestItemMap::const_iterator ffa_itr = lootPlayerFFAItems.find(player->GetGUIDLow());
    if (ffa_itr != lootPlayerFFAItems.end())
    {
        QuestItemList* ffa_list = ffa_itr->second;
        for (QuestItemList::const_iterator fi = ffa_list->begin(); fi != ffa_list->end(); ++fi)
        {
            const LootItem &item = items[fi->index];
            if (!fi->is_looted && !item.is_looted)
                return true;
        }
    }

    QuestItemMap const& lootPlayerNonQuestNonFFAConditionalItems = GetPlayerNonQuestNonFFAConditionalItems();
    QuestItemMap::const_iterator nn_itr = lootPlayerNonQuestNonFFAConditionalItems.find(player->GetGUIDLow());
    if (nn_itr != lootPlayerNonQuestNonFFAConditionalItems.end())
    {
        QuestItemList* conditional_list = nn_itr->second;
        for (QuestItemList::const_iterator ci = conditional_list->begin(); ci != conditional_list->end(); ++ci)
        {
            const LootItem &item = items[ci->index];
            if (!ci->is_looted && !item.is_looted)
                return true;
        }
    }

    return false;
}

// return true if there is any item over the group threshold (i.e. not underthreshold).
bool Loot::hasOverThresholdItem() const
{
    for (uint8 i = 0; i < items.size(); ++i)
    {
        if (!items[i].is_looted && !items[i].is_underthreshold && !items[i].freeforall)
            return true;
    }

    return false;
}

ByteBuffer& operator<<(ByteBuffer& b, LootView const& lv)
{
    if (lv.permission == NONE_PERMISSION)
    {
        b.WriteBit(0);      // Unk bit
        b.WriteBit(0);      // Creature guid 0
        b.WriteBit(0);      // Creature guid 1
        b.WriteBit(1);      // !Unk bit
        b.WriteBit(0);      // Loot guid 7
        b.WriteBit(0);      // Creature guid 3
        b.WriteBit(0);      // Loot guid 2
        b.WriteBit(0);      // Loot guid 0
        b.WriteBit(1);      // 4 - X = Permission != 3
        b.WriteBit(0);      // Loot guid 1
        b.WriteBit(0);      // Unk bit
        b.WriteBit(0);      // Creature guid 6
        b.WriteBit(1);      // !HasLootType
        b.WriteBit(0);      // Loot guid 3
        b.WriteBit(1);      // !Unk bit
        b.WriteBits(0, 20); // Currency count
        b.WriteBit(0);      // Creature guid 7
        b.WriteBit(0);      // Creature guid 4
        b.WriteBit(0);      // Loot guid 6
        b.WriteBit(0);      // Creature guid 2
        b.WriteBit(1);      // !hasGold
        b.WriteBit(0);      // Loot guid 4
        b.WriteBits(0, 19); // Items count
        b.WriteBit(0);      // Creature guid 5
        b.WriteBit(0);      // Unk bit
        b.FlushBits();
        return b;
    }

    Loot &l = lv.loot;
    auto &lItems = l.items;

    ByteBuffer dataBuffer;

    ObjectGuid creatureGuid = lv._guid;
    ObjectGuid lootViewGuid = MAKE_NEW_GUID(GUID_LOPART(creatureGuid), 0, HIGHGUID_LOOT);

    bool unkBit69 = false;
    bool unkBit48 = true;
    bool unkBit84 = true;
    bool unkBit32 = true;
    uint8 itemsShown = 0;
    uint8 currenciesShown = 0;

    b.WriteBit(unkBit69);
    b.WriteBitSeq<0, 1>(creatureGuid);
    b.WriteBit(!unkBit48);
    b.WriteBitSeq<7>(lootViewGuid);
    b.WriteBitSeq<3>(creatureGuid);
    b.WriteBitSeq<2, 0>(lootViewGuid);
    b.WriteBit(lv.permission == ROUND_ROBIN_PERMISSION);
    b.WriteBitSeq<1>(lootViewGuid);
    b.WriteBit(unkBit84);
    b.WriteBitSeq<6>(creatureGuid);
    b.WriteBit(!lv._loot_type);
    b.WriteBitSeq<3>(lootViewGuid);
    b.WriteBit(!unkBit32);

    // Currencies
    b.WriteBits(currenciesShown, 20);

    b.WriteBitSeq<7, 4>(creatureGuid);
    b.WriteBitSeq<6>(lootViewGuid);

    for (uint8 i = 0; i < currenciesShown; i++)
        b.WriteBits(2, 3);

    b.WriteBitSeq<2>(creatureGuid);
    b.WriteBit(!lv.loot.gold);
    b.WriteBitSeq<4>(lootViewGuid);

    auto const itemsShownPos = b.bitwpos();
    b.WriteBits(itemsShown, 19);

    switch (lv.permission)
    {
        case GROUP_PERMISSION:
        {
            // if you are not the round-robin group looter, you can only see
            // blocked rolled items and quest items, and !ffa items
            for (uint8 i = 0; i < lItems.size(); ++i)
            {
                if (!lItems[i].is_looted && !lItems[i].freeforall && lItems[i].conditions.empty() && lItems[i].AllowedForPlayer(lv.viewer))
                {
                    uint8 slot_type;

                    if (lItems[i].is_blocked)
                        slot_type = LOOT_SLOT_TYPE_ROLL_ONGOING;
                    else if (l.roundRobinPlayer == 0 || !lItems[i].is_underthreshold || lv.viewer->GetGUID() == l.roundRobinPlayer)
                    {
                        // no round robin owner or he has released the loot
                        // or it IS the round robin group owner
                        // => item is lootable
                        slot_type = LOOT_SLOT_TYPE_ALLOW_LOOT;
                    }
                    else
                        // item shall not be displayed.
                        continue;

                    writeLootItemData(lItems[i], i, slot_type, b, dataBuffer);
                    ++itemsShown;
                }
            }
            break;
        }
        case ROUND_ROBIN_PERMISSION:
        {
            for (uint8 i = 0; i < lItems.size(); ++i)
            {
                if (!lItems[i].is_looted && !lItems[i].freeforall && lItems[i].conditions.empty() && lItems[i].AllowedForPlayer(lv.viewer))
                {
                    if (l.roundRobinPlayer != 0 && lv.viewer->GetGUID() != l.roundRobinPlayer)
                        // item shall not be displayed.
                        continue;

                    writeLootItemData(lItems[i], i, LOOT_SLOT_TYPE_ALLOW_LOOT, b, dataBuffer);
                    ++itemsShown;
                }
            }
            break;
        }
        case ALL_PERMISSION:
        case MASTER_PERMISSION:
        case OWNER_PERMISSION:
        {
            uint8 slot_type = LOOT_SLOT_TYPE_ALLOW_LOOT;
            switch (lv.permission)
            {
                case MASTER_PERMISSION:
                    slot_type = LOOT_SLOT_TYPE_MASTER;
                    break;
                case OWNER_PERMISSION:
                    slot_type = LOOT_SLOT_TYPE_OWNER;
                    break;
                default:
                    break;
            }

            for (uint8 i = 0; i < lItems.size(); ++i)
            {
                if (!lItems[i].is_looted && !lItems[i].freeforall && lItems[i].conditions.empty() && lItems[i].AllowedForPlayer(lv.viewer))
                {
                    writeLootItemData(lItems[i], i, slot_type, b, dataBuffer);
                    ++itemsShown;
                }
            }
            break;
        }
        default:
            return b;
    }

    LootSlotType slotType = lv.permission == OWNER_PERMISSION ? LOOT_SLOT_TYPE_OWNER : LOOT_SLOT_TYPE_ALLOW_LOOT;
    QuestItemMap const& lootPlayerQuestItems = l.GetPlayerQuestItems();
    QuestItemMap::const_iterator q_itr = lootPlayerQuestItems.find(lv.viewer->GetGUIDLow());
    if (q_itr != lootPlayerQuestItems.end())
    {
        QuestItemList* q_list = q_itr->second;
        for (QuestItemList::const_iterator qi = q_list->begin(); qi != q_list->end(); ++qi)
        {
            LootItem &item = l.quest_items[qi->index];
            if (!qi->is_looted && !item.is_looted)
            {
                uint8 slottype = 0;
                if (item.follow_loot_rules)
                {
                    switch (lv.permission)
                    {
                        case MASTER_PERMISSION:
                            slottype = uint8(LOOT_SLOT_TYPE_MASTER);
                            break;
                        case GROUP_PERMISSION:
                        case ROUND_ROBIN_PERMISSION:
                            if (!item.is_blocked)
                                slottype = uint8(LOOT_SLOT_TYPE_ALLOW_LOOT);
                            else
                                slottype = uint8(LOOT_SLOT_TYPE_ROLL_ONGOING);
                            break;
                        default:
                            slottype = uint8(slotType);
                            break;
                    }
                }
                else
                   slottype = uint8(slotType);

                uint8 const index = l.items.size() + (qi - q_list->begin());
                writeLootItemData(item, index, slottype, b, dataBuffer);
                ++itemsShown;
            }
        }
    }

    QuestItemMap const& lootPlayerFFAItems = l.GetPlayerFFAItems();
    QuestItemMap::const_iterator ffa_itr = lootPlayerFFAItems.find(lv.viewer->GetGUIDLow());
    if (ffa_itr != lootPlayerFFAItems.end())
    {
        QuestItemList* ffa_list = ffa_itr->second;
        for (QuestItemList::const_iterator fi = ffa_list->begin(); fi != ffa_list->end(); ++fi)
        {
            LootItem &item = lItems[fi->index];
            if (!fi->is_looted && !item.is_looted)
            {
                writeLootItemData(item, fi->index, slotType, b, dataBuffer);
                ++itemsShown;
            }
        }
    }

    QuestItemMap const& lootPlayerNonQuestNonFFAConditionalItems = l.GetPlayerNonQuestNonFFAConditionalItems();
    QuestItemMap::const_iterator nn_itr = lootPlayerNonQuestNonFFAConditionalItems.find(lv.viewer->GetGUIDLow());
    if (nn_itr != lootPlayerNonQuestNonFFAConditionalItems.end())
    {
        QuestItemList* conditional_list = nn_itr->second;
        for (QuestItemList::const_iterator ci = conditional_list->begin(); ci != conditional_list->end(); ++ci)
        {
            LootItem &item = lItems[ci->index];
            if (!ci->is_looted && !item.is_looted)
            {
                uint8 slottype = 0;

                if (item.follow_loot_rules)
                {
                    switch (lv.permission)
                    {
                        case MASTER_PERMISSION:
                            slottype = uint8(LOOT_SLOT_TYPE_MASTER);
                            break;
                        case GROUP_PERMISSION:
                        case ROUND_ROBIN_PERMISSION:
                            if (!item.is_blocked)
                                slottype = uint8(LOOT_SLOT_TYPE_ALLOW_LOOT);
                            else
                                slottype = uint8(LOOT_SLOT_TYPE_ROLL_ONGOING);
                            break;
                        default:
                            slottype = uint8(slotType);
                            break;
                    }
                }
                else
                    slottype = uint8(slotType);

                writeLootItemData(item, ci->index, slottype, b, dataBuffer);
                ++itemsShown;
            }
        }
    }

    b.WriteBitSeq<5>(creatureGuid);
    b.WriteBit(0);
    b.FlushBits();

    b.PutBits(itemsShownPos, itemsShown, 19);

    if (lv.permission != ROUND_ROBIN_PERMISSION)
        b << uint8(2);

    for (uint8 i = 0; i < currenciesShown; i++)
    {
        b << uint8(0 /*++index*/);
        b << uint32(0 /*Currency ID*/);
        b << uint32(0 /*Currency count*/);
    }

    b.WriteByteSeq<3>(creatureGuid);
    b.append(dataBuffer);
    b.WriteByteSeq<0>(creatureGuid);

    if (lv.loot.gold)
        b << uint32(lv.loot.gold);

    if (lv._loot_type)
        b << uint8(lv._loot_type);

    b.WriteByteSeq<5>(lootViewGuid);
    b.WriteByteSeq<7, 6>(creatureGuid);
    b.WriteByteSeq<1>(lootViewGuid);

    if (unkBit48)
        b << uint8(2);

    if (unkBit32)
        b << uint8(17);

    b.WriteByteSeq<0>(lootViewGuid);
    b.WriteByteSeq<2, 5>(creatureGuid);
    b.WriteByteSeq<7, 3>(lootViewGuid);
    b.WriteByteSeq<1>(creatureGuid);
    b.WriteByteSeq<2>(lootViewGuid);
    b.WriteByteSeq<4>(creatureGuid);
    b.WriteByteSeq<4, 6>(lootViewGuid);

    return b;
}

//
// --------- LootTemplate::LootGroup ---------
//

// Adds an entry to the group (at loading stage)
void LootTemplate::LootGroup::AddEntry(LootStoreItem const &item)
{
    if (item.chance != 0)
        ExplicitlyChanced.push_back(item);
    else
        EqualChanced.push_back(item);
}

// Rolls an item from the group, returns NULL if all miss their chances
LootStoreItem const* LootTemplate::LootGroup::Roll() const
{
    if (!ExplicitlyChanced.empty())                             // First explicitly chanced entries are checked
    {
        float Roll = (float)rand_chance();

        for (uint32 i = 0; i < ExplicitlyChanced.size(); ++i)   // check each explicitly chanced entry in the template and modify its chance based on quality.
        {
            if (ExplicitlyChanced[i].chance >= 100.0f)
                return &ExplicitlyChanced[i];

            Roll -= ExplicitlyChanced[i].chance;
            if (Roll < 0)
                return &ExplicitlyChanced[i];
        }
    }
    if (!EqualChanced.empty())                              // If nothing selected yet - an item is taken from equal-chanced part
        return &EqualChanced[irand(0, EqualChanced.size()-1)];

    return NULL;                                            // Empty drop from the group
}

// True if group includes at least 1 quest drop entry
bool LootTemplate::LootGroup::HasQuestDrop() const
{
    for (LootStoreItemList::const_iterator i=ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (i->needs_quest)
            return true;
    for (LootStoreItemList::const_iterator i=EqualChanced.begin(); i != EqualChanced.end(); ++i)
        if (i->needs_quest)
            return true;
    return false;
}

// True if group includes at least 1 quest drop entry for active quests of the player
bool LootTemplate::LootGroup::HasQuestDropForPlayer(Player const* player) const
{
    for (LootStoreItemList::const_iterator i = ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (player->HasQuestForItem(i->itemid))
            return true;
    for (LootStoreItemList::const_iterator i = EqualChanced.begin(); i != EqualChanced.end(); ++i)
        if (player->HasQuestForItem(i->itemid))
            return true;
    return false;
}

void LootTemplate::LootGroup::CopyConditions(ConditionList /*conditions*/)
{
    for (LootStoreItemList::iterator i = ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
    {
        i->conditions.clear();
    }
    for (LootStoreItemList::iterator i = EqualChanced.begin(); i != EqualChanced.end(); ++i)
    {
        i->conditions.clear();
    }
}

void LootTemplate::LootGroup::GetCompatableLFRItems(LootStoreItemList &compatableItems, uint32 specialisation) const
{
    // find items that are compatable with the class specialisation
    for (auto const lootItem : EqualChanced)
    {
        if (IsEligibleForLFRLootItem(lootItem, specialisation))
            compatableItems.push_back(lootItem);
    }
}

// check if a particular item is allowed to be dropped for a class specialisation
bool LootTemplate::LootGroup::IsEligibleForLFRLootItem(LootStoreItem const &item, uint32 specialisation) const
{
    auto itemList = sObjectMgr->GetItemSpecialisations();

    auto itemSpecialisationContainer = itemList.find(item.itemid);
    if (itemSpecialisationContainer == itemList.end())
        return false;

    for (auto const &itemSpecialisation : itemSpecialisationContainer->second)
        if (itemSpecialisation->Specialisation == specialisation)
            return true;

    return false;
}

// Rolls an item from the group (if any takes its chance) and adds the item to the loot
void LootTemplate::LootGroup::Process(Loot& loot, uint16 lootMode) const
{
    // build up list of possible drops
    LootStoreItemList EqualPossibleDrops = EqualChanced;
    LootStoreItemList ExplicitPossibleDrops = ExplicitlyChanced;

    uint8 uiAttemptCount = 0;
    const uint8 uiMaxAttempts = ExplicitlyChanced.size() + EqualChanced.size();

    while (!ExplicitPossibleDrops.empty() || !EqualPossibleDrops.empty())
    {
        if (uiAttemptCount == uiMaxAttempts)             // already tried rolling too many times, just abort
            return;

        LootStoreItem* item = NULL;

        // begin rolling (normally called via Roll())
        LootStoreItemList::iterator itr;
        uint8 itemSource = 0;
        if (!ExplicitPossibleDrops.empty())              // First explicitly chanced entries are checked
        {
            itemSource = 1;
            float Roll = (float)rand_chance();
            // check each explicitly chanced entry in the template and modify its chance based on quality
            for (itr = ExplicitPossibleDrops.begin(); itr != ExplicitPossibleDrops.end(); itr = ExplicitPossibleDrops.erase(itr))
            {
                if (itr->chance >= 100.0f)
                {
                    item = &*itr;
                    break;
                }

                Roll -= itr->chance;
                if (Roll < 0)
                {
                    item = &*itr;
                    break;
                }
            }
        }
        if (item == NULL && !EqualPossibleDrops.empty()) // If nothing selected yet - an item is taken from equal-chanced part
        {
            itemSource = 2;
            itr = EqualPossibleDrops.begin();
            std::advance(itr, irand(0, EqualPossibleDrops.size()-1));
            item = &*itr;
        }
        // finish rolling

        ++uiAttemptCount;

        if (item != NULL && item->lootmode & lootMode)   // only add this item if roll succeeds and the mode matches
        {
            bool duplicate = false;
            if (ItemTemplate const* _proto = sObjectMgr->GetItemTemplate(item->itemid))
            {
                uint8 _item_counter = 0;
                for (LootItemList::const_iterator _item = loot.items.begin(); _item != loot.items.end(); ++_item)
                    if (_item->itemid == item->itemid)                             // search through the items that have already dropped
                    {
                        ++_item_counter;
                        if (_proto->InventoryType == 0 && _item_counter == 3)      // Non-equippable items are limited to 3 drops
                            duplicate = true;
                        else if (_proto->InventoryType != 0 && _item_counter == 1) // Equippable item are limited to 1 drop
                            duplicate = true;
                    }
            }
            if (duplicate) // if item->itemid is a duplicate, remove it
                switch (itemSource)
                {
                    case 1: // item came from ExplicitPossibleDrops
                        ExplicitPossibleDrops.erase(itr);
                        break;
                    case 2: // item came from EqualPossibleDrops
                        EqualPossibleDrops.erase(itr);
                        break;
                }
            else           // otherwise, add the item and exit the function
            {
                loot.AddItem(*item);
                return;
            }
        }
    }
}

// Overall chance for the group without equal chanced items
float LootTemplate::LootGroup::RawTotalChance() const
{
    float result = 0;

    for (LootStoreItemList::const_iterator i=ExplicitlyChanced.begin(); i != ExplicitlyChanced.end(); ++i)
        if (!i->needs_quest)
            result += i->chance;

    return result;
}

// Overall chance for the group
float LootTemplate::LootGroup::TotalChance() const
{
    float result = RawTotalChance();

    if (!EqualChanced.empty() && result < 100.0f)
        return 100.0f;

    return result;
}

void LootTemplate::LootGroup::Verify(LootStore const& lootstore, uint32 id, uint8 group_id) const
{
    float chance = RawTotalChance();
    if (chance > 101.0f)                                    // TODO: replace with 100% when DBs will be ready
    {
        TC_LOG_ERROR("sql.sql", "Table '%s' entry %u group %d has total chance > 100%% (%f)", lootstore.GetName(), id, group_id, chance);
    }

    if (chance >= 100.0f && !EqualChanced.empty())
    {
        TC_LOG_ERROR("sql.sql", "Table '%s' entry %u group %d has items with chance=0%% but group total chance >= 100%% (%f)", lootstore.GetName(), id, group_id, chance);
    }
}

void LootTemplate::LootGroup::CheckLootRefs(LootTemplateMap const& /*store*/, LootIdSet* ref_set) const
{
    for (LootStoreItemList::const_iterator ieItr=ExplicitlyChanced.begin(); ieItr != ExplicitlyChanced.end(); ++ieItr)
    {
        if (ieItr->mincountOrRef < 0)
        {
            if (!LootTemplates_Reference.GetLootFor(-ieItr->mincountOrRef))
                LootTemplates_Reference.ReportNotExistedId(-ieItr->mincountOrRef);
            else if (ref_set)
                ref_set->erase(-ieItr->mincountOrRef);
        }
    }

    for (LootStoreItemList::const_iterator ieItr=EqualChanced.begin(); ieItr != EqualChanced.end(); ++ieItr)
    {
        if (ieItr->mincountOrRef < 0)
        {
            if (!LootTemplates_Reference.GetLootFor(-ieItr->mincountOrRef))
                LootTemplates_Reference.ReportNotExistedId(-ieItr->mincountOrRef);
            else if (ref_set)
                ref_set->erase(-ieItr->mincountOrRef);
        }
    }
}

//
// --------- LootTemplate ---------
//

// Adds an entry to the group (at loading stage)
void LootTemplate::AddEntry(LootStoreItem const &item)
{
    if (item.group > 0 && item.mincountOrRef > 0)           // Group
    {
        if (item.group >= Groups.size())
            Groups.resize(item.group);                      // Adds new group the the loot template if needed
        Groups[item.group-1].AddEntry(item);                // Adds new entry to the group
    }
    else                                                    // Non-grouped entries and references are stored together
        Entries.push_back(item);
}

void LootTemplate::CopyConditions(ConditionList const &conditions)
{
    for (LootStoreItemList::iterator i = Entries.begin(); i != Entries.end(); ++i)
        i->conditions.clear();

    for (LootGroups::iterator i = Groups.begin(); i != Groups.end(); ++i)
        i->CopyConditions(conditions);
}

void LootTemplate::CopyConditions(LootItem* li) const
{
    // Copies the conditions list from a template item to a LootItem
    for (auto itr = Entries.begin(); itr != Entries.end(); ++itr)
    {
        if (itr->itemid != li->itemid)
            continue;

        li->conditions = itr->conditions;
        break;
    }
}

void LootTemplate::ProcessLFRItem(Loot& loot, uint32 specialisation) const
{
    if (!specialisation)
        return;

    // find all compatable items from loot table for the class specialisation
    LootStoreItemList compatableItems;
    for (auto const &lootGroup : Groups)
        lootGroup.GetCompatableLFRItems(compatableItems, specialisation);

    if (compatableItems.size())
    {
        // select a random item that can be used by the class specialisation
        uint32 itemIndex = urand(0, compatableItems.size() - 1);

        loot.AddItem(compatableItems[itemIndex]);
        loot.isBonusItem.push_back(false);
    }
}

// Rolls for every item in the template and adds the rolled items the the loot
void LootTemplate::Process(Loot& loot, bool rate, uint16 lootMode, uint8 groupId) const
{
    if (groupId)                                            // Group reference uses own processing of the group
    {
        if (groupId > Groups.size())
            return;                                         // Error message already printed at loading stage

        Groups[groupId-1].Process(loot, lootMode);
        return;
    }

    // Rolling non-grouped items
    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i)
    {
        if (i->lootmode &~ lootMode)                          // Do not add if mode mismatch
            continue;

        if (!i->Roll(rate))
            continue;                                         // Bad luck for the entry

        if (ItemTemplate const* _proto = sObjectMgr->GetItemTemplate(i->itemid))
        {
            uint8 _item_counter = 0;
            LootItemList::const_iterator _item = loot.items.begin();
            for (; _item != loot.items.end(); ++_item)
                if (_item->itemid == i->itemid)                               // search through the items that have already dropped
                {
                    ++_item_counter;
                    if (_proto->InventoryType == 0 && _item_counter == 3)     // Non-equippable items are limited to 3 drops
                        continue;
                    else if (_proto->InventoryType != 0 && _item_counter == 1) // Equippable item are limited to 1 drop
                        continue;
                }
            if (_item != loot.items.end())
                continue;
        }

        if (i->mincountOrRef < 0)                             // References processing
        {
            LootTemplate const* Referenced = LootTemplates_Reference.GetLootFor(-i->mincountOrRef);

            if (!Referenced)
                continue;                                     // Error message already printed at loading stage

            uint32 maxcount = uint32(float(i->maxcount) * sWorld->getRate(RATE_DROP_ITEM_REFERENCED_AMOUNT));
            for (uint32 loop = 0; loop < maxcount; ++loop)    // Ref multiplicator
                Referenced->Process(loot, rate, lootMode, i->group);
        }
        else                                                  // Plain entries (not a reference, not grouped)
            loot.AddItem(*i);                                 // Chance is already checked, just add
    }

    // Now processing groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i)
        i->Process(loot, lootMode);
}

bool CanUseItem(Player* player, ItemTemplate const* proto)
{
    if (proto)
    {
        if ((proto->Flags2 & ITEM_FLAGS_EXTRA_HORDE_ONLY) && player->GetTeam() != HORDE)
            return false;

        if ((proto->Flags2 & ITEM_FLAGS_EXTRA_ALLIANCE_ONLY) && player->GetTeam() != ALLIANCE)
            return false;

        if ((proto->AllowableClass & player->getClassMask()) == 0 || (proto->AllowableRace & player->getRaceMask()) == 0)
            return false;
    }
    else 
        return false;

    return true;
}

LootItem* LootTemplate::GeneratePersonalLoot(Player* player, uint32 difficulty, uint16 lootMode) const
{
    uint32 specialisation = player->GetLootSpecOrClassSpec();
    // find all compatable items from loot table for the class specialisation
    LootStoreItemList compatibleItems;
    for (auto const &lootGroup : Groups)
        lootGroup.GetCompatableLFRItems(compatibleItems, specialisation);

    if (!compatibleItems.empty())
    {
        bool _returned = false;
        while (!_returned)
        {
            const LootStoreItem item = Trinity::Containers::SelectRandomContainerElement(compatibleItems);
            if (item.itemid && CanUseItem(player, sObjectMgr->GetItemTemplate(item.itemid)))
            {
                return new LootItem(item);
                _returned = true;
            }
        }
    }

    return NULL;
}

// True if template includes at least 1 quest drop entry
bool LootTemplate::HasQuestDrop(LootTemplateMap const& /*store*/, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())
            return false;                                   // Error message [should be] already printed at loading stage
        return Groups[groupId-1].HasQuestDrop();
    }

    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i)
    {
        if (i->mincountOrRef < 0)                           // References
            return LootTemplates_Reference.HaveQuestLootFor(-i->mincountOrRef);
        else if (i->needs_quest)
            return true;                                    // quest drop found
    }

    // Now processing groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i)
        if (i->HasQuestDrop())
            return true;

    return false;
}

// True if template includes at least 1 quest drop for an active quest of the player
bool LootTemplate::HasQuestDropForPlayer(LootTemplateMap const& store, Player const* player, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())
            return false;                                   // Error message already printed at loading stage
        return Groups[groupId-1].HasQuestDropForPlayer(player);
    }

    // Checking non-grouped entries
    for (LootStoreItemList::const_iterator i = Entries.begin(); i != Entries.end(); ++i)
    {
        if (i->mincountOrRef < 0)                           // References processing
        {
            LootTemplateMap::const_iterator Referenced = store.find(-i->mincountOrRef);
            if (Referenced == store.end())
                continue;                                   // Error message already printed at loading stage
            if (Referenced->second->HasQuestDropForPlayer(store, player, i->group))
                return true;
        }
        else if (player->HasQuestForItem(i->itemid))
            return true;                                    // active quest drop found
    }

    // Now checking groups
    for (LootGroups::const_iterator i = Groups.begin(); i != Groups.end(); ++i)
        if (i->HasQuestDropForPlayer(player))
            return true;

    return false;
}

// Checks integrity of the template
void LootTemplate::Verify(LootStore const& lootstore, uint32 id) const
{
    // Checking group chances
    for (uint32 i=0; i < Groups.size(); ++i)
        Groups[i].Verify(lootstore, id, i+1);

    // TODO: References validity checks
}

void LootTemplate::CheckLootRefs(LootTemplateMap const& store, LootIdSet* ref_set) const
{
    for (LootStoreItemList::const_iterator ieItr = Entries.begin(); ieItr != Entries.end(); ++ieItr)
    {
        if (ieItr->mincountOrRef < 0)
        {
            if (!LootTemplates_Reference.GetLootFor(-ieItr->mincountOrRef))
                LootTemplates_Reference.ReportNotExistedId(-ieItr->mincountOrRef);
            else if (ref_set)
                ref_set->erase(-ieItr->mincountOrRef);
        }
    }

    for (LootGroups::const_iterator grItr = Groups.begin(); grItr != Groups.end(); ++grItr)
        grItr->CheckLootRefs(store, ref_set);
}
bool LootTemplate::addConditionItem(Condition* cond)
{
    if (!cond || !cond->isLoaded())//should never happen, checked at loading
    {
        TC_LOG_ERROR("loot", "LootTemplate::addConditionItem: condition is null");
        return false;
    }
    if (!Entries.empty())
    {
        for (LootStoreItemList::iterator i = Entries.begin(); i != Entries.end(); ++i)
        {
            if (i->itemid == uint32(cond->SourceEntry))
            {
                i->conditions.push_back(cond);
                return true;
            }
        }
    }
    if (!Groups.empty())
    {
        for (LootGroups::iterator groupItr = Groups.begin(); groupItr != Groups.end(); ++groupItr)
        {
            LootStoreItemList* itemList = (*groupItr).GetExplicitlyChancedItemList();
            if (!itemList->empty())
            {
                for (LootStoreItemList::iterator i = itemList->begin(); i != itemList->end(); ++i)
                {
                    if ((*i).itemid == uint32(cond->SourceEntry))
                    {
                        (*i).conditions.push_back(cond);
                        return true;
                    }
                }
            }
            itemList = (*groupItr).GetEqualChancedItemList();
            if (!itemList->empty())
            {
                for (LootStoreItemList::iterator i = itemList->begin(); i != itemList->end(); ++i)
                {
                    if ((*i).itemid == uint32(cond->SourceEntry))
                    {
                        (*i).conditions.push_back(cond);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool LootTemplate::isReference(uint32 id)
{
    for (LootStoreItemList::const_iterator ieItr = Entries.begin(); ieItr != Entries.end(); ++ieItr)
    {
        if (ieItr->itemid == id && ieItr->mincountOrRef < 0)
            return true;
    }
    return false;//not found or not reference
}

/**
 * @brief merge: Merge two Loot objects, resulting in this Loot object containing the
 * LootItem of @a other.
 *
 * @param other: Loot object this Loot is to be merged with. Cleaned if function
 * @param source: Player looting this loot object. Used to find the items in the maps
 * for quest items.
 *
 * @return: 0 if merging is successfull, 1 if merging fails (i.e: merging the two Loot
 * objects results in this Loot object containing more than 16 items.
 */
uint32 Loot::Merge(Loot* other, Player* source)
{
    if (!other || !source)
        return 1;

    typedef std::multimap<uint32, LootItem> LootItemByIDMap;
    typedef std::pair<LootItemByIDMap::iterator, LootItemByIDMap::iterator> LootItemByIDBounds;

    LootItemByIDMap itemsMap, questItemsMap;

    Loot* loots[2] = { this, other };
    for (Loot* loot: loots)
    {
        for (std::vector<LootItem>::iterator it = loot->items.begin(); it != loot->items.end(); ++it)
        {
            LootItem item = *it; // Current item
            if (item.is_looted)
                continue;
                
            uint32 itemId = item.itemid;
            ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemId);

            LootItemByIDMap::iterator itemIt = itemsMap.find(itemId);
            if (itemIt != itemsMap.end())
            {
                LootItemByIDBounds bounds = itemsMap.equal_range(itemId);
                if (std::distance(bounds.first, bounds.second) == 1) // Only one item in the map for now, easier thant if many
                {
                    LootItem& firstItem = (bounds.first->second); // Item already present, check if stack will not excedd max stack size
                    if (firstItem.is_looted)
                    {
                        itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        continue;
                    }

                    if (uint32(firstItem.count + item.count) > pProto->GetMaxStackSize())
                    {
                        // Create new LootItem if first item already hit max stack size
                        if (firstItem.count == pProto->GetMaxStackSize())
                            itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        else // Else try stack
                        {
                            if (!firstItem.randomPropertyId && !firstItem.randomSuffix) // No suffix or property, split in two
                            {
                                uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                firstItem.count = pProto->GetMaxStackSize();
                                item.count = overflow;
                                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                            else if (firstItem.randomPropertyId) // Stack and split only if same prop id
                            {
                                if (item.randomPropertyId == firstItem.randomPropertyId)
                                {
                                    uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                    firstItem.count = pProto->GetMaxStackSize();
                                    item.count = overflow;
                                    itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                                }
                                else
                                    itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                            else
                            {
                                if (item.randomSuffix == firstItem.randomSuffix) // Stack and split only if same suffix id
                                {
                                    uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                    firstItem.count = pProto->GetMaxStackSize();
                                    item.count = overflow;
                                    itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                                }
                                else
                                    itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                        }
                    }
                    else
                    {
                        if (!item.randomPropertyId && !item.randomSuffix)
                            firstItem.count += item.count;
                        else if (item.randomPropertyId)
                        {
                            if (item.randomPropertyId == firstItem.randomPropertyId)
                                firstItem.count += item.count;
                            else
                                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        }
                        else
                        {
                            if (item.randomSuffix == firstItem.randomSuffix)
                                firstItem.count += item.count;
                            else
                                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        }
                    }
                }
                else // Many items
                {
                    if (!item.randomPropertyId && !item.randomSuffix) // Both 0
                    {
                        LootItem& firstItem = bounds.first->second;
                        while (firstItem.is_looted || firstItem.count == pProto->GetMaxStackSize()) // Try to find an item not at max stack
                        {
                            ++(bounds.first);
                            if (bounds.first == bounds.second)
                                break;
                            else
                                firstItem = bounds.first->second;
                        }

                        if (bounds.first == bounds.second)
                            itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        else
                        {
                            if (uint32(item.count + firstItem.count) > pProto->GetMaxStackSize())
                            {
                                uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                firstItem.count = pProto->GetMaxStackSize();
                                item.count = overflow;
                                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                            else
                                firstItem.count += item.count;
                        }
                    }
                    else if (item.randomPropertyId) // Suffix == 0
                    {
                        LootItem& firstItem = bounds.first->second;
                        while (firstItem.is_looted ||
                              (firstItem.count == pProto->GetMaxStackSize() && firstItem.randomPropertyId != item.randomPropertyId))
                        {
                            ++(bounds.first);
                            if (bounds.first == bounds.second)
                                break;
                            else
                                firstItem = bounds.first->second;
                        }

                        if (bounds.first == bounds.second)
                            itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        else
                        {
                            if (uint32(item.count + firstItem.count) > pProto->GetMaxStackSize())
                            {
                                uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                firstItem.count = pProto->GetMaxStackSize();
                                item.count = overflow;
                                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                            else
                                firstItem.count += item.count;
                        }

                    }
                    else // Property == 0
                    {
                        LootItem& firstItem = bounds.first->second;
                        while (firstItem.is_looted ||
                              (firstItem.count == pProto->GetMaxStackSize() && firstItem.randomSuffix != item.randomSuffix))
                        {
                            ++(bounds.first);
                            if (bounds.first == bounds.second)
                                break;
                            else
                                firstItem = bounds.first->second;
                        }

                        if (bounds.first == bounds.second)
                            itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        else
                        {
                            if (uint32(item.count + firstItem.count) > pProto->GetMaxStackSize())
                            {
                                uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                firstItem.count = pProto->GetMaxStackSize();
                                item.count = overflow;
                                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                            else
                                firstItem.count += item.count;
                        }
                    }
                }
            }
            else
                itemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
        }

        QuestItemMap::const_iterator questIt = loot->GetPlayerQuestItems().find(source->GetGUIDLow());
        if (questIt != loot->GetPlayerQuestItems().end())
        {
            for (std::vector<QuestItem>::iterator it = questIt->second->begin(); it != questIt->second->end(); ++it)
            {
                QuestItem questItem = *it;
                LootItem item = loot->quest_items[questItem.index];

                uint32 itemId = item.itemid;
                ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemId);

                LootItemByIDMap::iterator itemIt = questItemsMap.find(itemId);
                if (itemIt != questItemsMap.end())
                {
                    LootItemByIDBounds bounds = questItemsMap.equal_range(itemId);
                    if (std::distance(bounds.first, bounds.second) > 1)
                    {
                        LootItem& firstItem = bounds.first->second;
                        while (firstItem.count == pProto->GetMaxStackSize())
                        {
                            ++(bounds.first);
                            if (bounds.first == bounds.second)
                                break;
                            else
                                firstItem = bounds.first->second;
                        }

                        if (bounds.first == bounds.second)
                            questItemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        else
                        {
                            if (uint32(firstItem.count + item.count) > pProto->GetMaxStackSize())
                            {
                                uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                                firstItem.count = pProto->GetMaxStackSize();
                                item.count = overflow;
                                questItemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                            }
                            else
                                firstItem.count += item.count;
                        }
                    }
                    else
                    {
                        LootItem& firstItem = bounds.first->second;
                        if (uint32(firstItem.count + item.count) > pProto->GetMaxStackSize())
                        {
                            uint32 overflow = firstItem.count + item.count - pProto->GetMaxStackSize();
                            firstItem.count = pProto->GetMaxStackSize();
                            item.count = overflow;
                            questItemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
                        }
                        else
                            firstItem.count += item.count;
                    }
                }
                else
                    questItemsMap.insert(std::pair<uint32, LootItem>(itemId, item));
            }
        }
    }

    if (itemsMap.size() + questItemsMap.size() >= MAX_NR_LOOT_ITEMS)
        return 1;

    uint32 _gold = gold + other->gold;
    other->clear();
    clear();

    gold = _gold;
    for (LootItemByIDMap::const_iterator it = itemsMap.cbegin(); it != itemsMap.cend(); ++it)
    {
        LootItem lootItem = it->second;
        items.push_back(lootItem);

        if (lootItem.conditions.empty() && !lootItem.freeforall && !lootItem.needs_quest)
            ++unlootedCount;
    }

    for (LootItemByIDMap::const_iterator it = questItemsMap.cbegin(); it != questItemsMap.cend(); ++it)
        quest_items.push_back(it->second);

    Group* group = source->GetGroup();
    if (group)
    {
        roundRobinPlayer = source->GetGUID();

        for (GroupReference* itr = group->GetFirstMember(); itr != NULL; itr = itr->next())
            if (Player* player = itr->GetSource())   // should actually be looted object instead of lootOwner but looter has to be really close so doesnt really matter
                FillNotNormalLootFor(player, player->IsAtGroupRewardDistance(source));

        for (uint8 i = 0; i < items.size(); ++i)
        {
            if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(items[i].itemid))
                if (proto->Quality < uint32(group->GetLootThreshold()))
                    items[i].is_underthreshold = true;
        }
    }
    else
        FillNotNormalLootFor(source, true);

    return 0;
}

void LoadLootTemplates_Creature()
{
    TC_LOG_INFO("server.loading", "Loading creature loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Creature.LoadAndCollectLootIds(lootIdSet);

    // Remove real entries and check loot existence
    CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
    {
        if (uint32 lootid = itr->second.lootid)
        {
            if (lootIdSet.find(lootid) == lootIdSet.end())
                LootTemplates_Creature.ReportNotExistedId(lootid);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Creature.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u creature loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 creature loot templates. DB table `creature_loot_template` is empty");
}

void LoadLootTemplates_Disenchant()
{
    TC_LOG_INFO("server.loading", "Loading disenchanting loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Disenchant.LoadAndCollectLootIds(lootIdSet);

    for (uint32 i = 0; i < sItemDisenchantLootStore.GetNumRows(); ++i)
    {
        ItemDisenchantLootEntry const* disenchant = sItemDisenchantLootStore.LookupEntry(i);
        if (!disenchant)
            continue;

        uint32 lootid = disenchant->Id;
        if (lootIdSet.find(lootid) == lootIdSet.end())
            LootTemplates_Disenchant.ReportNotExistedId(lootid);
        else
            lootIdSetUsed.insert(lootid);
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Disenchant.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u disenchanting loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 disenchanting loot templates. DB table `disenchant_loot_template` is empty");
}

void LoadLootTemplates_Fishing()
{
    TC_LOG_INFO("server.loading", "Loading fishing loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Fishing.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sAreaStore.GetNumRows(); ++i)
        if (AreaTableEntry const* areaEntry = sAreaStore.LookupEntry(i))
            if (lootIdSet.find(areaEntry->ID) != lootIdSet.end())
                lootIdSet.erase(areaEntry->ID);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Fishing.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u fishing loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 fishing loot templates. DB table `fishing_loot_template` is empty");
}

void LoadLootTemplates_Gameobject()
{
    TC_LOG_INFO("server.loading", "Loading gameobject loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Gameobject.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    GameObjectTemplateContainer const* gotc = sObjectMgr->GetGameObjectTemplates();
    for (GameObjectTemplateContainer::const_iterator itr = gotc->begin(); itr != gotc->end(); ++itr)
    {
        if (uint32 lootid = itr->second.GetLootId())
        {
            if (lootIdSet.find(lootid) == lootIdSet.end())
                LootTemplates_Gameobject.ReportNotExistedId(lootid);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Gameobject.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u gameobject loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 gameobject loot templates. DB table `gameobject_loot_template` is empty");
}

void LoadLootTemplates_Item()
{
    TC_LOG_INFO("server.loading", "Loading item loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Item.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    auto const &its = sObjectMgr->GetItemTemplateStore();
    for (auto itr = its.cbegin(); itr != its.cend(); ++itr) {
        if ((itr->second.Flags & ITEM_PROTO_FLAG_OPENABLE) == 0)
            continue;

        auto const lootItr = lootIdSet.find(itr->second.ItemId);
        if (lootItr != lootIdSet.end())
            lootIdSet.erase(lootItr);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Item.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u item loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 item loot templates. DB table `item_loot_template` is empty");
}

void LoadLootTemplates_Milling()
{
    TC_LOG_INFO("server.loading", "Loading milling loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Milling.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    auto const &its = sObjectMgr->GetItemTemplateStore();
    for (auto itr = its.cbegin(); itr != its.cend(); ++itr)
    {
        if (!(itr->second.Flags & ITEM_PROTO_FLAG_MILLABLE))
            continue;

        auto const lootItr = lootIdSet.find(itr->second.ItemId);
        if (lootItr != lootIdSet.end())
            lootIdSet.erase(lootItr);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Milling.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u milling loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 milling loot templates. DB table `milling_loot_template` is empty");
}

void LoadLootTemplates_Pickpocketing()
{
    TC_LOG_INFO("server.loading", "Loading pickpocketing loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Pickpocketing.LoadAndCollectLootIds(lootIdSet);

    // Remove real entries and check loot existence
    CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
    {
        if (uint32 lootid = itr->second.pickpocketLootId)
        {
            if (lootIdSet.find(lootid) == lootIdSet.end())
                LootTemplates_Pickpocketing.ReportNotExistedId(lootid);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Pickpocketing.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u pickpocketing loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 pickpocketing loot templates. DB table `pickpocketing_loot_template` is empty");
}

void LoadLootTemplates_Prospecting()
{
    TC_LOG_INFO("server.loading", "Loading prospecting loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Prospecting.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    auto const &its = sObjectMgr->GetItemTemplateStore();
    for (auto itr = its.cbegin(); itr != its.cend(); ++itr)
    {
        if (!(itr->second.Flags & ITEM_PROTO_FLAG_PROSPECTABLE))
            continue;

        auto const lootItr = lootIdSet.find(itr->second.ItemId);
        if (lootItr != lootIdSet.end())
            lootIdSet.erase(lootItr);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Prospecting.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u prospecting loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 prospecting loot templates. DB table `prospecting_loot_template` is empty");
}

void LoadLootTemplates_Mail()
{
    TC_LOG_INFO("server.loading", "Loading mail loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Mail.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sMailTemplateStore.GetNumRows(); ++i)
        if (sMailTemplateStore.LookupEntry(i))
            if (lootIdSet.find(i) != lootIdSet.end())
                lootIdSet.erase(i);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Mail.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u mail loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 mail loot templates. DB table `mail_loot_template` is empty");
}

void LoadLootTemplates_Skinning()
{
    TC_LOG_INFO("server.loading", "Loading skinning loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet, lootIdSetUsed;
    uint32 count = LootTemplates_Skinning.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    CreatureTemplateContainer const* ctc = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator itr = ctc->begin(); itr != ctc->end(); ++itr)
    {
        if (uint32 lootid = itr->second.SkinLootId)
        {
            if (lootIdSet.find(lootid) == lootIdSet.end())
                LootTemplates_Skinning.ReportNotExistedId(lootid);
            else
                lootIdSetUsed.insert(lootid);
        }
    }

    for (LootIdSet::const_iterator itr = lootIdSetUsed.begin(); itr != lootIdSetUsed.end(); ++itr)
        lootIdSet.erase(*itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Skinning.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u skinning loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 skinning loot templates. DB table `skinning_loot_template` is empty");
}

void LoadLootTemplates_Spell()
{
    TC_LOG_INFO("server.loading", "Loading spell loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    uint32 count = LootTemplates_Spell.LoadAndCollectLootIds(lootIdSet);

    // remove real entries and check existence loot
    for (uint32 spell_id = 1; spell_id < sSpellMgr->GetSpellInfoStoreSize(); ++spell_id)
    {
        SpellInfo const* spellInfo = sSpellMgr->GetSpellInfo(spell_id);
        if (!spellInfo)
            continue;

        // possible cases
        if (!spellInfo->IsLootCrafting())
            continue;

        if (lootIdSet.find(spell_id) == lootIdSet.end())
        {
            // not report about not trainable spells (optionally supported by DB)
            // ignore 61756 (Northrend Inscription Research (FAST QA VERSION) for example
            if (!(spellInfo->Attributes & SPELL_ATTR0_NOT_SHAPESHIFT) || (spellInfo->Attributes & SPELL_ATTR0_TRADESPELL))
            {
                LootTemplates_Spell.ReportNotExistedId(spell_id);
            }
        }
        else
            lootIdSet.erase(spell_id);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Spell.ReportUnusedIds(lootIdSet);

    if (count)
        TC_LOG_INFO("server.loading", ">> Loaded %u spell loot templates in %u ms", count, GetMSTimeDiffToNow(oldMSTime));
    else
        TC_LOG_ERROR("server.loading", ">> Loaded 0 spell loot templates. DB table `spell_loot_template` is empty");
}

void LoadLootTemplates_Reference()
{
    TC_LOG_INFO("server.loading", "Loading reference loot templates...");

    uint32 oldMSTime = getMSTime();

    LootIdSet lootIdSet;
    LootTemplates_Reference.LoadAndCollectLootIds(lootIdSet);

    // check references and remove used
    LootTemplates_Creature.CheckLootRefs(&lootIdSet);
    LootTemplates_Fishing.CheckLootRefs(&lootIdSet);
    LootTemplates_Gameobject.CheckLootRefs(&lootIdSet);
    LootTemplates_Item.CheckLootRefs(&lootIdSet);
    LootTemplates_Milling.CheckLootRefs(&lootIdSet);
    LootTemplates_Pickpocketing.CheckLootRefs(&lootIdSet);
    LootTemplates_Skinning.CheckLootRefs(&lootIdSet);
    LootTemplates_Disenchant.CheckLootRefs(&lootIdSet);
    LootTemplates_Prospecting.CheckLootRefs(&lootIdSet);
    LootTemplates_Mail.CheckLootRefs(&lootIdSet);
    LootTemplates_Reference.CheckLootRefs(&lootIdSet);

    // output error for any still listed ids (not referenced from any loot table)
    LootTemplates_Reference.ReportUnusedIds(lootIdSet);

    TC_LOG_INFO("server.loading", ">> Loaded refence loot templates in %u ms", GetMSTimeDiffToNow(oldMSTime));
}
