/*
 * Copyright (C) 2011 TrintiyCore <http://www.trinitycore.org/>
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

#ifndef TRINITY_DB2STRUCTURE_H
#define TRINITY_DB2STRUCTURE_H

#include "Define.h"
#include "ItemPrototype.h"

#define MAX_SPELL_REAGENTS 8

// GCC has alternative #pragma pack(N) syntax and old gcc version does not support pack(push, N), also any gcc version does not support it at some platform
#if defined(__GNUC__)
#pragma pack(1)
#else
#pragma pack(push, 1)
#endif

// Structures used to access raw DB2 data and required packing to portability
struct ItemEntry
{
    uint32   ID;                                             // 0
    uint32   Class;                                          // 1
    uint32   SubClass;                                       // 2
    int32    SoundOverrideSubclass;                          // 3
    int32    Material;                                       // 4
    uint32   DisplayId;                                      // 5
    uint32   InventoryType;                                  // 6
    uint32   Sheath;                                         // 7
};

struct ItemCurrencyCostEntry
{
    //uint32  Id;
    uint32  ItemId;
};

// 5.4.0 17399 check @author : Izidor
struct ItemSparseEntry
{
    uint32     ID;                                           // 0
    uint32     Quality;                                      // 1
    uint32     Flags;                                        // 2
    uint32     Flags2;                                       // 3
    uint32     Unk540_1;                                     // 4 unk flag
    float      Unk430_1;                                     // 5
    float      Unk430_2;                                     // 6
    uint32     BuyCount;                                     // 7
    uint32     BuyPrice;                                     // 8
    uint32     SellPrice;                                    // 9
    uint32     InventoryType;                                // 10
    int32      AllowableClass;                               // 11
    int32      AllowableRace;                                // 12
    uint32     ItemLevel;                                    // 13
    int32      RequiredLevel;                                // 14
    uint32     RequiredSkill;                                // 15
    uint32     RequiredSkillRank;                            // 16
    uint32     RequiredSpell;                                // 17
    uint32     RequiredHonorRank;                            // 18
    uint32     RequiredCityRank;                             // 19
    uint32     RequiredReputationFaction;                    // 20
    uint32     RequiredReputationRank;                       // 21
    uint32     MaxCount;                                     // 22
    uint32     Stackable;                                    // 23
    uint32     ContainerSlots;                               // 24
    int32      ItemStatType[MAX_ITEM_PROTO_STATS];           // 25 - 35
    int32      ItemStatValue[MAX_ITEM_PROTO_STATS];          // 36 - 46
    int32      ItemStatUnk1[MAX_ITEM_PROTO_STATS];           // 47 - 57
    int32      ItemStatUnk2[MAX_ITEM_PROTO_STATS];           // 58 - 68
    uint32     ScalingStatDistribution;                      // 69
    uint32     DamageType;                                   // 70
    uint32     Delay;                                        // 71
    float      RangedModRange;                               // 72
    int32      SpellId[MAX_ITEM_PROTO_SPELLS];               //
    int32      SpellTrigger[MAX_ITEM_PROTO_SPELLS];          //
    int32      SpellCharges[MAX_ITEM_PROTO_SPELLS];          //
    int32      SpellCooldown[MAX_ITEM_PROTO_SPELLS];         //
    int32      SpellCategory[MAX_ITEM_PROTO_SPELLS];         //
    int32      SpellCategoryCooldown[MAX_ITEM_PROTO_SPELLS]; //
    uint32     Bonding;                                      // 99
    char*      Name;                                         // 100
    char*      Name2;                                        // 101
    char*      Name3;                                        // 102
    char*      Name4;                                        // 103
    char*      Description;                                  // 104
    uint32     PageText;                                     // 105
    uint32     LanguageID;                                   // 106
    uint32     PageMaterial;                                 // 107
    uint32     StartQuest;                                   // 108
    uint32     LockID;                                       // 109
    int32      Material;                                     // 110
    uint32     Sheath;                                       // 111
    uint32     RandomProperty;                               // 112
    uint32     RandomSuffix;                                 // 113
    uint32     ItemSet;                                      // 114
    uint32     Area;                                         // 115
    uint32     Map;                                          // 116
    uint32     BagFamily;                                    // 117
    uint32     TotemCategory;                                // 118
    uint32     Color[MAX_ITEM_PROTO_SOCKETS];                // 119 - 121
    uint32     Content[MAX_ITEM_PROTO_SOCKETS];              // 122 - 124
    int32      SocketBonus;                                  // 125
    uint32     GemProperties;                                // 126
    float      ArmorDamageModifier;                          // 127
    uint32     Duration;                                     // 128
    uint32     ItemLimitCategory;                            // 129
    uint32     HolidayId;                                    // 130
    float      StatScalingFactor;                            // 131
    int32      CurrencySubstitutionId;                       // 132
    int32      CurrencySubstitutionCount;                    // 133
};

struct ItemUpgradeEntry
{
    uint32 Id;
    uint32 itemUpgradePath;
    uint32 itemLevelUpgrade;
    uint32 prevItemUpgradeId;
    uint32 currencyId;
    uint32 currencyCost;
};

struct RulesetItemUpgradeEntry
{
    uint32 Id;
    uint32 unk;
    uint32 itemUpgradeId;
    uint32 itemid;
};

#define MAX_ITEM_EXT_COST_ITEMS         5
#define MAX_ITEM_EXT_COST_CURRENCIES    5

struct ItemExtendedCostEntry
{
    uint32      ID;                                                     // 0 extended-cost entry id
    //uint32    reqhonorpoints;                                         // 1 required honor points, only 0
    //uint32    reqarenapoints;                                         // 2 required arena points, only 0
    uint32      RequiredArenaSlot;                                      // 3 arena slot restrictions (min slot value)
    uint32      RequiredItem[MAX_ITEM_EXT_COST_ITEMS];                  // 4-8 required item id
    uint32      RequiredItemCount[MAX_ITEM_EXT_COST_ITEMS];             // 9-13 required count of 1st item
    uint32      RequiredPersonalArenaRating;                            // 14 required personal arena rating
    //uint32    ItemPurchaseGroup;                                      // 15, only 0
    uint32      RequiredCurrency[MAX_ITEM_EXT_COST_CURRENCIES];         // 16-20 required curency id
    uint32      RequiredCurrencyCount[MAX_ITEM_EXT_COST_CURRENCIES];    // 21-25 required curency count
    uint32      RequiredFactionId;
    uint32      RequiredFactionStanding;
    uint32      RequirementFlags;
    uint32      RequiredGuildLevel;
    uint32      RequiredAchievement;
};

// ------------------------------------------------
// Battle Pet Datastores
// ------------------------------------------------

#define MAX_BATTLE_PET_PROPERTIES 6

// BattlePetAbility.db2
struct BattlePetAbilityEntry
{
    uint32 Id;                                              // 0
    uint32 FamilyId;                                        // 1 - battle pet family id or -1 for aura
    //uint32 Unk1                                           // 2 - icon id?
    uint32 Cooldown;                                        // 3 - cooldown in turns
    uint32 VisualId;                                        // 4 - visual id (BattlePetVisual.db2)
    uint32 Flags;                                           // 5 - flags (see BattlePetAbilityFlags enum)
    char*  Name;                                            // 6 - name text
    char*  Description;                                     // 7 - description text
};

// BattlePetAbilityState.db2
struct BattlePetAbilityStateEntry
{
    uint32 Id;                                              // 0
    uint32 AbilityId;                                       // 1 - battle pet ability id (BattlePetAbility.db2)
    uint32 StateId;                                         // 2 - battle pet state id (BattlePetState.db2)
    int32 Value;                                            // 3 - value for state
};

// BattlePetAbilityEffect.db2
struct BattlePetAbilityEffectEntry
{
    uint32 Id;                                              // 0
    uint32 AbilityTurnId;                                   // 1 - ability turn id (BattlePetAbilityTurn.db2)
    uint32 VisualId;                                        // 2 - visual id (BattlePetVisual.db2)
    uint32 TriggerAbility;                                  // 3 - parent ability
    uint32 EffectProperty;                                  // 4 - effect property id (BattlePetEffectProperties.db2)
    //uint32 Unk1;                                          // 5 - effect property offset?
    int32  Properties[MAX_BATTLE_PET_PROPERTIES];           // 6 - values for effect property
};

// BattlePetAbilityTurn.db2
struct BattlePetAbilityTurnEntry
{
    uint32 Id;                                              // 0
    uint32 AbilityId;                                       // 1 - parent ability id (BattlePetAbility.db2)
    uint32 VisualId;                                        // 2 - visual id (BattlePetVisual.db2)
    uint32 Duration;                                        // 3 - amount of turns the ability lasts
    uint32 HasProcType;                                     // 4 - if set to 1, next value is positive else -1
    int32  ProcType;
};

// BattlePetBreedQuality.db2
struct BattlePetBreedQualityEntry
{
    uint32 Id;                                              // 0
    uint32 Quality;                                         // 1 - battle pet quality id (same as item quality)
    float  Multiplier;                                      // 2 - multiplier, better the quality higher the multiplier
};

// BattlePetBreedState.db2
struct BattlePetBreedStateEntry
{
    uint32 Id;                                              // 0
    uint32 BreedId;                                         // 1 - battle pet breed id
    uint32 StateId;                                         // 2 - battle pet state id (BattlePetState.db2)
    int32  Modifier;                                        // 3 - modifier value for state
};

// BattlePetEffectProperties.db2
struct BattlePetEffectPropertiesEntry
{
    uint32 Id;                                              // 0
    uint32 Flags;                                           // 1 - flags
    char*  PropertyName[MAX_BATTLE_PET_PROPERTIES];         // 2 - name
    uint32 IsAura[MAX_BATTLE_PET_PROPERTIES];               // 3 - set to 1 if an aura id
};

// BattlePetNPCTeamMember.db2
// TODO...

// BattlePetSpecies.db2
struct BattlePetSpeciesEntry
{
    uint32 Id;                                              // 0
    uint32 NpcId;                                           // 1 - npc id
    uint32 IconId;                                          // 2 - icon id
    uint32 SpellId;                                         // 3 - summon spell id (Spell.dbc)
    uint32 FamilyId;                                        // 4 - battle pet family id
    //int32 Unk1;                                           // 5
    uint32 Flags;                                           // 6 - flags (see BattlePetSpeciesFlags enum)
    char*  Description;                                     // 7 - description text, contains method to obtain and cost
    char*  Flavor;                                          // 8 - flavor text
};

// BattlePetSpeciesState.db2
struct BattlePetSpeciesStateEntry
{
    uint32 Id;                                              // 0
    uint32 SpeciesId;                                       // 1 - battle pet species id (BattlePetSpecies.db2)
    uint32 StateId;                                         // 2 - battle pet state id (BattlePetState.db2)
    int32  Modifier;                                        // 3 - modifier value for state
};

// BattlePetSpeciesXAbility.db2
struct BattlePetSpeciesXAbilityEntry
{
    uint32 Id;                                              // 0
    uint32 SpeciesId;                                       // 1 - battle pet species id (BattlePetSpecies.db2)
    uint32 AbilityId;                                       // 2 - battle pet ability id (BattlePetAbility.db2)
    uint32 RequiredLevel;                                   // 3 - required level to use this ability
    int32  SlotId;                                          // 4 - ability slot id (0-2)
};

// BattlePetState.db2
struct BattlePetState
{
    uint32 Id;                                              // 0
    // uint32 Unk1;                                         // 1 - parent? only set for 34 (Mechanic_IsBurning)
    char * Name;                                            // 2 - state name
    uint32 Flags;                                           // 3 - flags
};

// BattlePetVisual.db2
// TODO...

// ------------------------------------------------

// SpellReagents.db2
// @author Selenium: 5.4 valid
struct SpellReagentsEntry
{
    //uint32    Id;                                         // 0        m_ID
    int32     Reagent[MAX_SPELL_REAGENTS];                  // 1-9      m_reagent
    uint32    ReagentCount[MAX_SPELL_REAGENTS];             // 10-18    m_reagentCount
};

struct QuestPackageItemEntry final
{
    uint32 Id;
    uint32 Package;
    uint32 Item;
    uint32 Count;

    // some kind of group/flag. 2 is set for Hero's Purse, a special reward
    // substituation that is provided for certain quests when nothing else fits
    // in
    uint32 Unk2;
};

// GCC has alternative #pragma pack(N) syntax and old gcc version does not support pack(push, N), also any gcc version does not support it at some platform
#if defined(__GNUC__)
#pragma pack()
#else
#pragma pack(pop)
#endif

#endif
