/*
 * Copyright (C) 2008-2014 TrinityCore <http://www.trinitycore.org/>
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

/* ScriptData
Name: message_commandscript
%Complete: 100
Comment: All message related commands
Category: commandscripts
EndScriptData */

#include "ScriptMgr.h"
#include "Chat.h"
#include "ChannelMgr.h"
#include "Language.h"
#include "Player.h"
#include "ObjectMgr.h"

class message_commandscript : public CommandScript
{
public:
    message_commandscript() : CommandScript("message_commandscript") { }

    ChatCommand* GetCommands() const
    {
        static ChatCommand channelSetCommandTable[] =
        {
            { "ownership",      SEC_ADMINISTRATOR,  false,  &HandleChannelSetOwnership,         "", NULL },
            { NULL,             0,                  false,  NULL,                               "", NULL }
        };
        static ChatCommand channelCommandTable[] =
        {
            { "set",            SEC_ADMINISTRATOR,  true,   NULL,                               "", channelSetCommandTable },
            { NULL,             0,                  false,  NULL,                               "", NULL }
        };
        static ChatCommand commandTable[] =
        {
            { "qannounce",      SEC_ADMINISTRATOR,  false, &HandleQAnnounceCommand,      "", NULL },
            { "channel",        SEC_ADMINISTRATOR,  true,   NULL,                               "", channelCommandTable  },
            { "nameannounce",   SEC_MODERATOR,      true,   &HandleNameAnnounceCommand,         "", NULL },
            { "gmnameannounce", SEC_MODERATOR,      true,   &HandleGMNameAnnounceCommand,       "", NULL },
            { "announce",       SEC_MODERATOR,      true,   &HandleAnnounceCommand,             "", NULL },
            { "gmannounce",     SEC_MODERATOR,      true,   &HandleGMAnnounceCommand,           "", NULL },
            { "notify",         SEC_MODERATOR,      true,   &HandleNotifyCommand,               "", NULL },
            { "gmnotify",       SEC_MODERATOR,      true,   &HandleGMNotifyCommand,             "", NULL },
            { "whispers",       SEC_MODERATOR,      false,  &HandleWhispersCommand,             "", NULL },
            { NULL,             0,                  false,  NULL,                               "", NULL }
        };
        return commandTable;
    }

    static bool HandleChannelSetOwnership(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;
        char const* channelStr = strtok((char*)args, " ");
        char const* argStr = strtok(NULL, "");

        if (!channelStr || !argStr)
            return false;

        Player* player = handler->GetSession()->GetPlayer();
        Channel* channcel = NULL;

        if (ChannelMgr* cMgr = ChannelMgr::forTeam(player->GetTeam()))
            channcel = cMgr->GetChannel(channelStr, player);

        if (strcmp(argStr, "on") == 0)
        {
            if (channcel)
                channcel->SetOwnership(true);
            PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHANNEL_OWNERSHIP);
            stmt->setUInt8 (0, 1);
            stmt->setString(1, channelStr);
            CharacterDatabase.Execute(stmt);
            handler->PSendSysMessage(LANG_CHANNEL_ENABLE_OWNERSHIP, channelStr);
        }
        else if (strcmp(argStr, "off") == 0)
        {
            if (channcel)
                channcel->SetOwnership(false);
            PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHANNEL_OWNERSHIP);
            stmt->setUInt8 (0, 0);
            stmt->setString(1, channelStr);
            CharacterDatabase.Execute(stmt);
            handler->PSendSysMessage(LANG_CHANNEL_DISABLE_OWNERSHIP, channelStr);
        }
        else
            return false;

        return true;
    }

    static bool HandleNameAnnounceCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        WorldSession* session = handler->GetSession();
        if (!session)
        {
            sWorld->SendWorldText(LANG_ANNOUNCE_COLOR_ADMIN, "Console", "Server", args);
            return true;
        }

        switch (session->GetSecurity())
        {
            case SEC_ADMINISTRATOR:
            case SEC_CONSOLE:
                sWorld->SendWorldText(LANG_ANNOUNCE_COLOR_ADMIN, "Admin", session->GetPlayer()->GetName().c_str(), args);
                break;
            case SEC_GAMEMASTER:
                sWorld->SendWorldText(LANG_ANNOUNCE_COLOR_GM, "GM", session->GetPlayer()->GetName().c_str(), args);
                break;
            case SEC_MODERATOR:
                sWorld->SendWorldText(LANG_ANNOUNCE_COLOR_GM, "Mod", session->GetPlayer()->GetName().c_str(), args);
                break;
            default:
                break;
        }

        return true;
    }

    static bool HandleGMNameAnnounceCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        WorldSession* session = handler->GetSession();
        sWorld->SendGMText(LANG_ANNOUNCE_COLOR_GM, "GM CHAT", session->GetPlayer()->GetName().c_str(), args);

        return true;
    }

    static bool HandleQAnnounceCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        WorldSession* session = handler->GetSession();
        sWorld->SendWorldText(LANG_ANNOUNCE_COLOR_QA, session->GetPlayer()->GetName().c_str(), args);

        return true;
    }

    // global announce
    static bool HandleAnnounceCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        char buff[2048];
        sprintf(buff, handler->GetTrinityString(LANG_SYSTEMMESSAGE), args);
        sWorld->SendServerMessage(SERVER_MSG_STRING, buff);
        return true;
    }
    // announce to logged in GMs
    static bool HandleGMAnnounceCommand(ChatHandler* /*handler*/, char const* args)
    {
        if (!*args)
            return false;

        sWorld->SendGMText(LANG_GM_BROADCAST, args);
        return true;
    }
    // notification player at the screen
    static bool HandleNotifyCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string str = handler->GetTrinityString(LANG_GLOBAL_NOTIFY);
        str += args;
        size_t len = str.length();

        WorldPacket data(SMSG_NOTIFICATION, 2 + len);
        data.WriteBits(len, 12);
        data.FlushBits();
        data.append(str.c_str(), len);
        sWorld->SendGlobalMessage(&data);

        return true;
    }
    // notification GM at the screen
    static bool HandleGMNotifyCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
            return false;

        std::string str = handler->GetTrinityString(LANG_GM_NOTIFY);
        str += args;
        size_t len = str.length();

        WorldPacket data(SMSG_NOTIFICATION, 2 + len);
        data.WriteBits(len, 12);
        data.FlushBits();
        data.append(str.c_str(), len);
        sWorld->SendGlobalGMMessage(&data);

        return true;
    }
    // Enable\Dissable accept whispers (for GM)
    static bool HandleWhispersCommand(ChatHandler* handler, char const* args)
    {
        if (!*args)
        {
            handler->PSendSysMessage(LANG_COMMAND_WHISPERACCEPTING, handler->GetSession()->GetPlayer()->acceptsWhispers() ?  handler->GetTrinityString(LANG_ON) : handler->GetTrinityString(LANG_OFF));
            return true;
        }

        std::string argStr = strtok((char*)args, " ");
        // whisper on
        if (argStr == "on")
        {
            handler->GetSession()->GetPlayer()->SetAcceptWhispers(true);
            handler->SendSysMessage(LANG_COMMAND_WHISPERON);
            return true;
        }

        // whisper off
        if (argStr == "off")
        {
            // Remove all players from the Gamemaster's whisper whitelist
            handler->GetSession()->GetPlayer()->ClearWhisperWhiteList();
            handler->GetSession()->GetPlayer()->SetAcceptWhispers(false);
            handler->SendSysMessage(LANG_COMMAND_WHISPEROFF);
            return true;
        }

        if (argStr == "remove")
        {
            std::string name = strtok(NULL, " ");
            if (normalizePlayerName(name))
            {
                if (Player* player = sObjectAccessor->FindPlayerByName(name))
                {
                    handler->GetSession()->GetPlayer()->RemoveFromWhisperWhiteList(player->GetGUID());
                    handler->PSendSysMessage(LANG_COMMAND_WHISPEROFFPLAYER, name.c_str());
                    return true;
                }
                else
                {
                    handler->PSendSysMessage(LANG_PLAYER_NOT_FOUND, name.c_str());
                    handler->SetSentErrorMessage(true);
                    return false;
                }
            }
        }
        handler->SendSysMessage(LANG_USE_BOL);
        handler->SetSentErrorMessage(true);
        return false;
    }
};

void AddSC_message_commandscript()
{
    new message_commandscript();
}
