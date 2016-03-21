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

#include "ObjectGridLoader.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Creature.h"
#include "Vehicle.h"
#include "GameObject.h"
#include "DynamicObject.h"
#include "Corpse.h"
#include "AreaTrigger.h"
#include "World.h"
#include "CellImpl.h"
#include "CreatureAI.h"

namespace {

template <typename T>
void AddObjectHelper(Cell const &cell, Map* map, T *obj)
{
    map->AddToGrid(obj, cell);

    obj->AddToWorld();
    if (obj->isActiveObject())
        map->AddToActive(obj);
}

template <typename T>
uint32 LoadHelper(CellGuidSet const &cellGuids, Cell const &cell, Map *map)
{
    uint32 count = 0;

    for (auto const &guid : cellGuids)
    {
        auto const obj = new T();
        if (!obj->LoadFromDB(guid, map))
        {
            delete obj;
            continue;
        }

        AddObjectHelper(cell, map, obj);
        ++count;
    }

    return count;
}

uint32 LoadHelper(CellCorpseMap const &cellCorpses, Cell const &cell, Map *map)
{
    uint32 count = 0;

    for (auto const &kvPair : cellCorpses)
    {
        if (kvPair.second != map->GetInstanceId())
            continue;

        auto const playerGuid = MAKE_NEW_GUID(kvPair.first, 0, HIGHGUID_PLAYER);

        auto const obj = sObjectAccessor->GetCorpseForPlayerGUID(playerGuid);
        if (!obj)
            continue;

        // TODO: this is a hack
        // corpse's map should be reset when the map is unloaded
        // but it may still exist when the grid is unloaded but map is not
        // in that case map == currMap
        obj->SetMap(map);

        if (obj->IsInGrid())
        {
            obj->AddToWorld();
            continue;
        }

        AddObjectHelper(cell, map, obj);
        ++count;
    }

    return count;
}

} // namespace

namespace Trinity {

void ObjectGridLoader::LoadN(NGrid const &grid, Map *map, Cell cell)
{
    uint32 gameObjects = 0;
    uint32 creatures = 0;
    uint32 corpses = 0;

    for (uint32 x = 0; x < MAX_NUMBER_OF_CELLS; ++x)
    {
        cell.data.Part.cell_x = x;
        for (uint32 y = 0; y < MAX_NUMBER_OF_CELLS; ++y)
        {
            cell.data.Part.cell_y = y;

            // Load creatures and gameobjects
            if (auto const cellGuids = sObjectMgr->GetCellObjectGuids(map->GetId(), map->GetSpawnMode(), cell.GetCellCoord().GetId()))
            {
                creatures += LoadHelper<Creature>(cellGuids->creatures, cell, map);
                gameObjects += LoadHelper<GameObject>(cellGuids->gameobjects, cell, map);
            }

            // Load corpses (not bones)
            if (auto const cellGuids = sObjectMgr->GetCellObjectGuids(map->GetId(), 0, cell.GetCellCoord().GetId()))
            {
                // corpses are always added to spawn mode 0 and they are spawned by their instance id
                corpses += LoadHelper(cellGuids->corpses, cell, map);
            }
        }
    }

    TC_LOG_DEBUG("maps", "%u GameObjects, %u Creatures, and %u Corpses/Bones loaded for grid [%d, %d] on map %u",
                 gameObjects, creatures, corpses, grid.getX(), grid.getY(), map->GetId());
}


void ObjectGridEvacuator::Visit(CreatureMapType &m)
{
    // creature in unloading grid can have respawn point in another grid
    // if it will be unloaded then it will not respawn in original grid until unload/load original grid
    // move to respawn point to prevent this case. For player view in respawn grid this will be normal respawn.

    std::size_t count = m.size();

    for (std::size_t i = 0; i < count;)
    {
        auto &creature = m[i];
        ASSERT(!creature->isPet() && "ObjectGridRespawnMover must not be called for pets");

        creature->GetMap()->CreatureRespawnRelocation(creature, true);

        // If creature respawned in different grid, size will change
        if (m.size() == count)
            ++i;
        else
            count = m.size();
    }
}

template <typename AnyMapType>
void ObjectGridUnloader::Visit(AnyMapType &m)
{
    while (!m.empty())
    {
        auto const obj = m.front();
        // if option set then object already saved at this moment
        if (!sWorld->getBoolConfig(CONFIG_SAVE_RESPAWN_TIME_IMMEDIATELY))
            obj->SaveRespawnTime();
        // Some creatures may summon other temp summons in CleanupsBeforeDelete()
        // So we need this even after cleaner (maybe we can remove cleaner)
        // Example: Flame Leviathan Turret 33139 is summoned when a creature is deleted
        // TODO: Check if that script has the correct logic. Do we really need to summon something before deleting?
        obj->CleanupsBeforeDelete();
        obj->RemoveFromGrid();
        delete obj;
    }
}

void ObjectGridStoper::Visit(CreatureMapType &m)
{
    // stop any fights at grid de-activation and remove dynobjects created at cast by creatures
    for (auto &source : m)
    {
        if (source->IsInCombat())
        {
            source->CombatStop();
            source->DeleteThreatList();
            // If creature calling RemoveCharmedBy during EnterEvadeMode, RemoveCharmedBy call AIM_Initialize so AI() pointer may be corrupt
            // Maybe we need to lock AI during the call of EnterEvadeMode ?
            source->SetLockAI(true);
            if (source->IsAIEnabled)
                source->AI()->EnterEvadeMode();    // Calls RemoveAllAuras
            source->SetLockAI(false);
        }

        source->RemoveAllDynObjects();
        source->RemoveAllAreasTrigger();       // Calls RemoveFromWorld, needs to be after RemoveAllAuras or we invalidate the Owner pointer of the aura
    }
}

template <typename AnyMapType>
void ObjectGridCleaner::Visit(AnyMapType &m)
{
    // look like we have a crash wile accessing to DynamicObject map here
    // I Guess it's DynamicObject delete pointer, we need to look at it anyway ...
    for (auto &object : m)
        object->CleanupsBeforeDelete();
}

} // namespace Trinity

template void Trinity::ObjectGridUnloader::Visit(CreatureMapType &);
template void Trinity::ObjectGridUnloader::Visit(GameObjectMapType &);
template void Trinity::ObjectGridUnloader::Visit(DynamicObjectMapType &);
template void Trinity::ObjectGridUnloader::Visit(CorpseMapType &);
template void Trinity::ObjectGridUnloader::Visit(AreaTriggerMapType &);

template void Trinity::ObjectGridCleaner::Visit(CreatureMapType &);
template void Trinity::ObjectGridCleaner::Visit(GameObjectMapType &);
template void Trinity::ObjectGridCleaner::Visit(DynamicObjectMapType &);
template void Trinity::ObjectGridCleaner::Visit(CorpseMapType &);
template void Trinity::ObjectGridCleaner::Visit(AreaTriggerMapType &);
