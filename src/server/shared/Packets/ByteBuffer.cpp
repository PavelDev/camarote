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

#include "ByteBuffer.h"
#include "Common.h"
#include "Log.h"

#include <ace/Stack_Trace.h>
#include <sstream>

ByteBufferPositionException::ByteBufferPositionException(bool add, size_t pos,
                                                         size_t size, size_t valueSize)
{
    std::ostringstream ss;
    ACE_Stack_Trace trace;

    ss << "Attempted to " << (add ? "put" : "get") << " value with size: "
       << valueSize << " in ByteBuffer (pos: " << pos << " size: " << size
       << ")\n\n" << trace.c_str();

    message().assign(ss.str());
}

ByteBufferSourceException::ByteBufferSourceException(size_t pos, size_t size,
                                                     size_t valueSize)
{
    std::ostringstream ss;
    ACE_Stack_Trace trace;

    ss << "Attempted to put a "
       << (valueSize > 0 ? "NULL-pointer" : "zero-sized value")
       << " in ByteBuffer (pos: " << pos << " size: " << size << ")\n\n"
       << trace.c_str();

    message().assign(ss.str());
}

std::string ByteBuffer::textlike() const
{
    std::ostringstream o;
    o << "STORAGE_SIZE: " << size();
    for (uint32 i = 0; i < size(); ++i)
    {
        char buf[1];
        snprintf(buf, sizeof(buf), "%c", read<uint8>(i));
        o << buf;
    }
    o << " ";

    return o.str();
}

std::string ByteBuffer::hexlike() const
{
    uint32 j = 1, k = 1;

    std::ostringstream o;
    o << "STORAGE_SIZE: " << size() << "\n\t";

    for (uint32 i = 0; i < size(); ++i)
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", read<uint8>(i));
        if ((i == (j * 8)) && ((i != (k * 16))))
        {
            o << "| ";
            ++j;
        }
        else if (i == (k * 16))
        {
            o << "\n\t";
            ++k;
            ++j;
        }

        o << buf;
    }

    return o.str();
}
