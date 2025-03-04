/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 */

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "BattlegroundMgr.h"
#include "Group.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

ArenaTeam::ArenaTeam()
    : TeamId(0), Type(0), TeamName(), CaptainGuid(0), BackgroundColor(0), EmblemStyle(0), EmblemColor(0),
      BorderStyle(0), BorderColor(0)
{
    Stats.WeekGames   = 0;
    Stats.SeasonGames = 0;
    Stats.Rank        = 0;
    Stats.Rating      = sWorld->getIntConfig(CONFIG_ARENA_START_RATING);
    Stats.WeekWins    = 0;
    Stats.SeasonWins  = 0;
}

ArenaTeam::~ArenaTeam()
{
}

bool ArenaTeam::Create(uint64 captainGuid, uint8 type, std::string const& teamName, uint32 backgroundColor, uint8 emblemStyle, uint32 emblemColor, uint8 borderStyle, uint32 borderColor)
{
    // Check if captain is present
    if (!ObjectAccessor::FindPlayerInOrOutOfWorld(captainGuid))
        return false;

    // Check if arena team name is already taken
    if (sArenaTeamMgr->GetArenaTeamByName(TeamName))
        return false;

    // Generate new arena team id
    TeamId = sArenaTeamMgr->GenerateArenaTeamId();

    // Assign member variables
    CaptainGuid = captainGuid;
    Type = type;
    TeamName = teamName;
    BackgroundColor = backgroundColor;
    EmblemStyle = emblemStyle;
    EmblemColor = emblemColor;
    BorderStyle = borderStyle;
    BorderColor = borderColor;
    uint32 captainLowGuid = GUID_LOPART(captainGuid);

    // Save arena team to db
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ARENA_TEAM);
    stmt->setUInt32(0, TeamId);
    stmt->setString(1, TeamName);
    stmt->setUInt32(2, captainLowGuid);
    stmt->setUInt8(3, Type);
    stmt->setUInt16(4, Stats.Rating);
    stmt->setUInt32(5, BackgroundColor);
    stmt->setUInt8(6, EmblemStyle);
    stmt->setUInt32(7, EmblemColor);
    stmt->setUInt8(8, BorderStyle);
    stmt->setUInt32(9, BorderColor);
    CharacterDatabase.Execute(stmt);

    // Add captain as member
    AddMember(CaptainGuid);
    return true;
}

bool ArenaTeam::AddMember(uint64 playerGuid)
{
    std::string playerName;
    uint8 playerClass;

    // Check if arena team is full (Can't have more than type * 2 players)
    if (GetMembersSize() >= GetType() * 2)
        return false;

    // xinef: Get player name and class from player storage or global data storage
    Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(playerGuid);
    if (player)
    {
        playerClass = player->getClass();
        playerName = player->GetName();
    }
    else
    {
        GlobalPlayerData const* playerData = sWorld->GetGlobalPlayerData(GUID_LOPART(playerGuid));
        if (!playerData)
            return false;

        playerName = playerData->name;
        playerClass = playerData->playerClass;
    }

    if (!sScriptMgr->CanAddMember(this, playerGuid))
        return false;

    // Check if player is already in a similar arena team
    if ((player && player->GetArenaTeamId(GetSlot())) || Player::GetArenaTeamIdFromStorage(GUID_LOPART(playerGuid), GetSlot()) != 0)
    {
        sLog->outError("Arena: Player %s (guid: %u) already has an arena team of type %u", playerName.c_str(), GUID_LOPART(playerGuid), GetType());
        return false;
    }

    // Set player's personal rating
    uint32 personalRating = 0;

    if (sWorld->getIntConfig(CONFIG_ARENA_START_PERSONAL_RATING) > 0)
        personalRating = sWorld->getIntConfig(CONFIG_ARENA_START_PERSONAL_RATING);
    else if (GetRating() >= 1000)
        personalRating = 1000;

    // xinef: zomg! sync query
    // Try to get player's match maker rating from db and fall back to config setting if not found
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_SEL_MATCH_MAKER_RATING);
    stmt->setUInt32(0, GUID_LOPART(playerGuid));
    stmt->setUInt8(1, GetSlot());
    PreparedQueryResult result = CharacterDatabase.Query(stmt);

    uint16 matchMakerRating;
    uint16 maxMMR;
    if (result)
    {
        matchMakerRating = (*result)[0].GetUInt16();
        uint16 Max = (*result)[1].GetUInt16();
        maxMMR = std::max(Max, matchMakerRating);
    }
    else
    {
        matchMakerRating = sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);
        maxMMR = matchMakerRating;
    }

    // Remove all player signatures from other petitions
    // This will prevent player from joining too many arena teams and corrupt arena team data integrity
    Player::RemovePetitionsAndSigns(playerGuid, GetType());

    // Feed data to the struct
    ArenaTeamMember newMember;
    //newMember.Name             = playerName;
    newMember.Guid             = playerGuid;
    newMember.Class            = playerClass;
    newMember.SeasonGames      = 0;
    newMember.WeekGames        = 0;
    newMember.SeasonWins       = 0;
    newMember.WeekWins         = 0;
    newMember.PersonalRating   = personalRating;
    newMember.MatchMakerRating = matchMakerRating;
    newMember.MaxMMR           = maxMMR;

    Members.push_back(newMember);
    sWorld->UpdateGlobalPlayerArenaTeam(GUID_LOPART(playerGuid), GetSlot(), GetId());

    // Save player's arena team membership to db
    stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_ARENA_TEAM_MEMBER);
    stmt->setUInt32(0, TeamId);
    stmt->setUInt32(1, GUID_LOPART(playerGuid));
    CharacterDatabase.Execute(stmt);

    // Inform player if online
    if (player)
    {
        player->SetInArenaTeam(TeamId, GetSlot(), GetType());
        player->SetArenaTeamIdInvited(0);

        // Hide promote/remove buttons
        if (CaptainGuid != playerGuid)
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_MEMBER, 1);
    }

    return true;
}

bool ArenaTeam::LoadArenaTeamFromDB(QueryResult result)
{
    if (!result)
        return false;

    Field* fields = result->Fetch();

    TeamId            = fields[0].GetUInt32();
    TeamName          = fields[1].GetString();
    CaptainGuid       = MAKE_NEW_GUID(fields[2].GetUInt32(), 0, HIGHGUID_PLAYER);
    Type              = fields[3].GetUInt8();
    BackgroundColor   = fields[4].GetUInt32();
    EmblemStyle       = fields[5].GetUInt8();
    EmblemColor       = fields[6].GetUInt32();
    BorderStyle       = fields[7].GetUInt8();
    BorderColor       = fields[8].GetUInt32();
    Stats.Rating      = fields[9].GetUInt16();
    Stats.WeekGames   = fields[10].GetUInt16();
    Stats.WeekWins    = fields[11].GetUInt16();
    Stats.SeasonGames = fields[12].GetUInt16();
    Stats.SeasonWins  = fields[13].GetUInt16();
    Stats.Rank        = fields[14].GetUInt32();

    return true;
}

bool ArenaTeam::LoadMembersFromDB(QueryResult result)
{
    if (!result)
        return false;

    bool captainPresentInTeam = false;

    do
    {
        Field* fields = result->Fetch();

        // Prevent crash if db records are broken when all members in result are already processed and current team doesn't have any members
        if (!fields)
            break;

        uint32 arenaTeamId = fields[0].GetUInt32();

        // We loaded all members for this arena_team already, break cycle
        if (arenaTeamId > TeamId)
            break;

        ArenaTeamMember newMember;
        newMember.Guid             = MAKE_NEW_GUID(fields[1].GetUInt32(), 0, HIGHGUID_PLAYER);
        newMember.WeekGames        = fields[2].GetUInt16();
        newMember.WeekWins         = fields[3].GetUInt16();
        newMember.SeasonGames      = fields[4].GetUInt16();
        newMember.SeasonWins       = fields[5].GetUInt16();
        //newMember.Name             = fields[6].GetString();
        newMember.Class            = fields[7].GetUInt8();
        newMember.PersonalRating   = fields[8].GetUInt16();
        newMember.MatchMakerRating = fields[9].GetUInt16() > 0 ? fields[9].GetUInt16() : sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);;
        newMember.MaxMMR           = std::max(fields[10].GetUInt16(), newMember.MatchMakerRating);

        // Delete member if character information is missing
        if (fields[6].GetString().empty())
        {
            sLog->outErrorDb("ArenaTeam %u has member with empty name - probably player %u doesn't exist, deleting him from memberlist!", arenaTeamId, GUID_LOPART(newMember.Guid));
            this->DelMember(newMember.Guid, true);
            continue;
        }

        // Check if team team has a valid captain
        if (newMember.Guid == GetCaptain())
            captainPresentInTeam = true;

        // Put the player in the team
        Members.push_back(newMember);
        sWorld->UpdateGlobalPlayerArenaTeam(GUID_LOPART(newMember.Guid), GetSlot(), GetId());
    } while (result->NextRow());

    if (Empty() || !captainPresentInTeam)
    {
        // Arena team is empty or captain is not in team, delete from db
#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "ArenaTeam %u does not have any members or its captain is not in team, disbanding it...", TeamId);
#endif
        return false;
    }

    return true;
}

bool ArenaTeam::SetName(std::string const& name)
{
    if (TeamName == name || name.empty() || name.length() > 24 || sObjectMgr->IsReservedName(name) || !ObjectMgr::IsValidCharterName(name))
        return false;

    TeamName = name;
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_NAME);
    stmt->setString(0, TeamName);
    stmt->setUInt32(1, GetId());
    CharacterDatabase.Execute(stmt);
    return true;
}

void ArenaTeam::SetCaptain(uint64 guid)
{
    // Disable remove/promote buttons
    Player* oldCaptain = ObjectAccessor::FindPlayerInOrOutOfWorld(GetCaptain());
    if (oldCaptain)
        oldCaptain->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_MEMBER, 1);

    // Set new captain
    CaptainGuid = guid;

    // Update database
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_CAPTAIN);
    stmt->setUInt32(0, GUID_LOPART(guid));
    stmt->setUInt32(1, GetId());
    CharacterDatabase.Execute(stmt);

    // Enable remove/promote buttons
    if (Player* newCaptain = ObjectAccessor::FindPlayerInOrOutOfWorld(guid))
    {
        newCaptain->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_MEMBER, 0);
        /*if (oldCaptain)
        {
            sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Player: %s [GUID: %u] promoted player: %s [GUID: %u] to leader of arena team [Id: %u] [Type: %u].",
                oldCaptain->GetName().c_str(), oldCaptain->GetGUIDLow(), newCaptain->GetName().c_str(),
                newCaptain->GetGUIDLow(), GetId(), GetType());
        }*/
    }
}

void ArenaTeam::DelMember(uint64 guid, bool cleanDb)
{
    Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(guid);
    Group* group = (player && player->GetGroup()) ? player->GetGroup() : nullptr;

    // Remove member from team
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // Remove queues of members
        if (Player* playerMember = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->Guid))
        {
            if (group && playerMember->GetGroup() && group->GetGUID() == playerMember->GetGroup()->GetGUID())
            {
                if (BattlegroundQueueTypeId bgQueue = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_AA, GetType()))
                {
                    GroupQueueInfo ginfo;
                    BattlegroundQueue& queue = sBattlegroundMgr->GetBattlegroundQueue(bgQueue);
                    if (queue.GetPlayerGroupInfoData(playerMember->GetGUID(), &ginfo))
                    {
                        if (!ginfo.IsInvitedToBGInstanceGUID)
                        {
                            WorldPacket data;
                            playerMember->RemoveBattlegroundQueueId(bgQueue);
                            sBattlegroundMgr->BuildBattlegroundStatusPacket(&data, nullptr, playerMember->GetBattlegroundQueueIndex(bgQueue), STATUS_NONE, 0, 0, 0, TEAM_NEUTRAL);
                            queue.RemovePlayer(playerMember->GetGUID(), true, 0);
                            playerMember->GetSession()->SendPacket(&data);
                        }
                    }
                }
            }
        }

        if (itr->Guid == guid)
        {
            Members.erase(itr);
            sWorld->UpdateGlobalPlayerArenaTeam(GUID_LOPART(guid), GetSlot(), 0);
            break;
        }
    }

    // Inform player and remove arena team info from player data
    if (player)
    {
        player->GetSession()->SendArenaTeamCommandResult(ERR_ARENA_TEAM_QUIT_S, GetName(), "", 0);
        // delete all info regarding this team
        for (uint32 i = 0; i < ARENA_TEAM_END; ++i)
            player->SetArenaTeamInfoField(GetSlot(), ArenaTeamInfoType(i), 0);
    }

    // Only used for single member deletion, for arena team disband we use a single query for more efficiency
    if (cleanDb)
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM_MEMBER);
        stmt->setUInt32(0, GetId());
        stmt->setUInt32(1, GUID_LOPART(guid));
        CharacterDatabase.Execute(stmt);
    }
}

void ArenaTeam::Disband(WorldSession* session)
{
    // Remove all members from arena team
    while (!Members.empty())
        DelMember(Members.front().Guid, false);

    // Broadcast update
    if (session)
    {
        BroadcastEvent(ERR_ARENA_TEAM_DISBANDED_S, 0, 2, session->GetPlayerName(), GetName(), "");
    }

    // Update database
    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM_MEMBERS);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    // Remove arena team from ObjectMgr
    sArenaTeamMgr->RemoveArenaTeam(TeamId);
}

void ArenaTeam::Disband()
{
    // Remove all members from arena team
    while (!Members.empty())
        DelMember(Members.front().Guid, false);

    // Update database
    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_ARENA_TEAM_MEMBERS);
    stmt->setUInt32(0, TeamId);
    trans->Append(stmt);

    CharacterDatabase.CommitTransaction(trans);

    // Remove arena team from ObjectMgr
    sArenaTeamMgr->RemoveArenaTeam(TeamId);
}

void ArenaTeam::Roster(WorldSession* session)
{
    Player* player = nullptr;

    uint8 unk308 = 0;
    std::string tempName;

    WorldPacket data(SMSG_ARENA_TEAM_ROSTER, 100);
    data << uint32(GetId());                                // team id
    data << uint8(unk308);                                  // 308 unknown value but affect packet structure
    data << uint32(GetMembersSize());                       // members count
    data << uint32(GetType());                              // arena team type?

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        player = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->Guid);

        data << uint64(itr->Guid);                          // guid
        data << uint8((player ? 1 : 0));                        // online flag
        tempName = "";
        sObjectMgr->GetPlayerNameByGUID(itr->Guid, tempName);
        data << tempName;                                  // member name
        data << uint32((itr->Guid == GetCaptain() ? 0 : 1));// captain flag 0 captain 1 member
        data << uint8((player ? player->getLevel() : 0));           // unknown, level?
        data << uint8(itr->Class);                         // class
        data << uint32(itr->WeekGames);                    // played this week
        data << uint32(itr->WeekWins);                     // wins this week
        data << uint32(itr->SeasonGames);                  // played this season
        data << uint32(itr->SeasonWins);                   // wins this season
        data << uint32(itr->PersonalRating);               // personal rating
        if (unk308)
        {
            data << float(0.0f);                           // 308 unk
            data << float(0.0f);                           // 308 unk
        }
    }

    session->SendPacket(&data);
#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent SMSG_ARENA_TEAM_ROSTER");
#endif
}

void ArenaTeam::Query(WorldSession* session)
{
    WorldPacket data(SMSG_ARENA_TEAM_QUERY_RESPONSE, 4 * 7 + GetName().size() + 1);
    data << uint32(GetId());                                // team id
    data << GetName();                                      // team name
    data << uint32(GetType());                              // arena team type (2=2x2, 3=3x3 or 5=5x5)
    data << uint32(BackgroundColor);                        // background color
    data << uint32(EmblemStyle);                            // emblem style
    data << uint32(EmblemColor);                            // emblem color
    data << uint32(BorderStyle);                            // border style
    data << uint32(BorderColor);                            // border color
    session->SendPacket(&data);
#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent SMSG_ARENA_TEAM_QUERY_RESPONSE");
#endif
}

void ArenaTeam::SendStats(WorldSession* session)
{
    WorldPacket data(SMSG_ARENA_TEAM_STATS, 4 * 7);
    data << uint32(GetId());                                // team id
    data << uint32(Stats.Rating);                           // rating
    data << uint32(Stats.WeekGames);                        // games this week
    data << uint32(Stats.WeekWins);                         // wins this week
    data << uint32(Stats.SeasonGames);                      // played this season
    data << uint32(Stats.SeasonWins);                       // wins this season
    data << uint32(Stats.Rank);                             // rank
    session->SendPacket(&data);
}

void ArenaTeam::NotifyStatsChanged()
{
    // This is called after a rated match ended
    // Updates arena team stats for every member of the team (not only the ones who participated!)
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->Guid))
            SendStats(player->GetSession());
}

void ArenaTeam::Inspect(WorldSession* session, uint64 guid)
{
    ArenaTeamMember* member = GetMember(guid);
    if (!member || GetSlot() >= MAX_ARENA_SLOT)
        return;

    WorldPacket data(MSG_INSPECT_ARENA_TEAMS, 8 + 1 + 4 * 6);
    data << uint64(guid);                                   // player guid
    data << uint8(GetSlot());                               // slot (0...2)
    data << uint32(GetId());                                // arena team id
    data << uint32(Stats.Rating);                           // rating
    data << uint32(Stats.SeasonGames);                      // season played
    data << uint32(Stats.SeasonWins);                       // season wins
    data << uint32(member->SeasonGames);                    // played (count of all games, that the inspected member participated...)
    data << uint32(member->PersonalRating);                 // personal rating
    session->SendPacket(&data);
}

void ArenaTeamMember::ModifyPersonalRating(Player* player, int32 mod, uint32 type)
{
    if (int32(PersonalRating) + mod < 0)
        PersonalRating = 0;
    else
        PersonalRating += mod;

    if (player && ArenaTeam::GetSlotByType(type) < 3)
    {
        player->SetArenaTeamInfoField(ArenaTeam::GetSlotByType(type), ARENA_TEAM_PERSONAL_RATING, PersonalRating);
        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_PERSONAL_RATING, PersonalRating, type);
    }
}

void ArenaTeamMember::ModifyMatchmakerRating(int32 mod, uint32 /*slot*/)
{
    if (mod < 0)
    {
        // pussywizard: prevent lowering MMR too much from max achieved MMR
        int32 maxAllowedDrop = (int32)sWorld->getIntConfig(CONFIG_MAX_ALLOWED_MMR_DROP);
        mod = std::min<int32>(std::max<int32>(-((int32)MatchMakerRating - (int32)MaxMMR + maxAllowedDrop), mod), 0);
    }

    if (int32(MatchMakerRating) + mod < 0)
        MatchMakerRating = 0;
    else
        MatchMakerRating += mod;

    if (MatchMakerRating > MaxMMR)
        MaxMMR = MatchMakerRating;
}

void ArenaTeam::BroadcastPacket(WorldPacket* packet)
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->Guid))
            player->GetSession()->SendPacket(packet);
}

void ArenaTeam::BroadcastEvent(ArenaTeamEvents event, uint64 guid, uint8 strCount, std::string const& str1, std::string const& str2, std::string const& str3)
{
    WorldPacket data(SMSG_ARENA_TEAM_EVENT, 1 + 1 + 1);
    data << uint8(event);
    data << uint8(strCount);
    switch (strCount)
    {
        case 0:
            break;
        case 1:
            data << str1;
            break;
        case 2:
            data << str1 << str2;
            break;
        case 3:
            data << str1 << str2 << str3;
            break;
        default:
            sLog->outError("Unhandled strCount %u in ArenaTeam::BroadcastEvent", strCount);
            return;
    }

    if (guid)
        data << uint64(guid);

    BroadcastPacket(&data);

#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
    sLog->outDebug(LOG_FILTER_NETWORKIO, "WORLD: Sent SMSG_ARENA_TEAM_EVENT");
#endif
}

void ArenaTeam::MassInviteToEvent(WorldSession* session)
{
    WorldPacket data(SMSG_CALENDAR_ARENA_TEAM, (Members.size() - 1) * (4 + 8 + 1));
    data << uint32(Members.size() - 1);

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid != session->GetPlayer()->GetGUID())
        {
            data.appendPackGUID(itr->Guid);
            data << uint8(0); // unk
        }
    }

    session->SendPacket(&data);
}

uint8 ArenaTeam::GetSlotByType(uint32 type)
{
    uint8 slot = 0xFF;

    auto const& itr = ArenaSlotByType.find(type);
    if (itr == ArenaSlotByType.end())
    {
        sLog->outError("FATAL: Unknown arena team type %u for some arena team", type);
        return slot;
    }

    slot = ArenaSlotByType.at(type);

    // Get the changed slot type
    sScriptMgr->OnGetSlotByType(type, slot);

    if (slot != 0xFF)
    {
        return slot;
    }

    sLog->outError("FATAL: Unknown arena team type %u for some arena team", type);
    return 0xFF;
}

bool ArenaTeam::IsMember(uint64 guid) const
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Guid == guid)
            return true;

    return false;
}

uint32 ArenaTeam::GetPoints(uint32 memberRating)
{
    // Returns how many points would be awarded with this team type with this rating
    float points;

    uint32 rating = memberRating + 150 < Stats.Rating ? memberRating : Stats.Rating;

    if (rating <= 1500)
    {
        if (sWorld->getIntConfig(CONFIG_ARENA_SEASON_ID) < 6)
            points = (float)rating * 0.22f + 14.0f;
        else
            points = 344;
    }
    else
        points = 1511.26f / (1.0f + 1639.28f * exp(-0.00412f * (float)rating));

    // Type penalties for teams < 5v5
    if (Type == ARENA_TEAM_2v2)
        points *= 0.76f;
    else if (Type == ARENA_TEAM_3v3)
        points *= 0.88f;

    sScriptMgr->OnGetArenaPoints(this, points);

    points *= sWorld->getRate(RATE_ARENA_POINTS);

    return (uint32) points;
}

uint32 ArenaTeam::GetAverageMMR(Group* group) const
{
    if (!group)
        return 0;

    uint32 matchMakerRating = 0;
    uint32 playerDivider = 0;
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // Skip if player is not online
        if (!ObjectAccessor::FindPlayerInOrOutOfWorld(itr->Guid))
            continue;

        // Skip if player is not member of group
        if (!group->IsMember(itr->Guid))
            continue;

        matchMakerRating += itr->MatchMakerRating;
        ++playerDivider;
    }

    // x/0 = crash
    if (playerDivider == 0)
        playerDivider = 1;

    matchMakerRating /= playerDivider;

    return matchMakerRating;
}

float ArenaTeam::GetChanceAgainst(uint32 ownRating, uint32 opponentRating)
{
    // Returns the chance to win against a team with the given rating, used in the rating adjustment calculation
    // ELO system
    return 1.0f / (1.0f + exp(log(10.0f) * (float)((float)opponentRating - (float)ownRating) / 650.0f));
}

int32 ArenaTeam::GetMatchmakerRatingMod(uint32 ownRating, uint32 opponentRating, bool won /*, float& confidence_factor*/)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;
    float mod = won_mod - chance;

    // Work in progress:
    /*
    // This is a simulation, as there is not much info on how it really works
    float confidence_mod = min(1.0f - fabs(mod), 0.5f);

    // Apply confidence factor to the mod:
    mod *= confidence_factor

    // And only after that update the new confidence factor
    confidence_factor -= ((confidence_factor - 1.0f) * confidence_mod) / confidence_factor;
    */

    // Real rating modification
    mod *= sWorld->getFloatConfig(CONFIG_ARENA_MATCHMAKER_RATING_MODIFIER);

    return (int32)ceil(mod);
}

int32 ArenaTeam::GetRatingMod(uint32 ownRating, uint32 opponentRating, bool won /*, float confidence_factor*/)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);

    // Calculate the rating modification
    float mod;

    // TODO: Replace this hack with using the confidence factor (limiting the factor to 2.0f)
    if (won)
    {
        if (ownRating < 1300)
        {
            float win_rating_modifier1 = sWorld->getFloatConfig(CONFIG_ARENA_WIN_RATING_MODIFIER_1);

            if (ownRating < 1000)
                mod =  win_rating_modifier1 * (1.0f - chance);
            else
                mod = ((win_rating_modifier1 / 2.0f) + ((win_rating_modifier1 / 2.0f) * (1300.0f - float(ownRating)) / 300.0f)) * (1.0f - chance);
        }
        else
            mod = sWorld->getFloatConfig(CONFIG_ARENA_WIN_RATING_MODIFIER_2) * (1.0f - chance);
    }
    else
        mod = sWorld->getFloatConfig(CONFIG_ARENA_LOSE_RATING_MODIFIER) * (-chance);

    return (int32)ceil(mod);
}

void ArenaTeam::FinishGame(int32 mod, const Map* bgMap)
{
    // Rating can only drop to 0
    if (int32(Stats.Rating) + mod < 0)
        Stats.Rating = 0;
    else
    {
        Stats.Rating += mod;

        // Check if rating related achivements are met
        for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
            if (Player* member = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->Guid))
                if (member->FindMap() == bgMap)
                    member->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_TEAM_RATING, Stats.Rating, Type);
    }

    // Update number of games played per season or week
    Stats.WeekGames += 1;
    Stats.SeasonGames += 1;

    // Update team's rank, start with rank 1 and increase until no team with more rating was found
    Stats.Rank = 1;
    ArenaTeamMgr::ArenaTeamContainer::const_iterator i = sArenaTeamMgr->GetArenaTeamMapBegin();
    for (; i != sArenaTeamMgr->GetArenaTeamMapEnd(); ++i)
    {
        if (i->second->GetType() == Type && i->second->GetStats().Rating > Stats.Rating)
            ++Stats.Rank;
    }
}

int32 ArenaTeam::WonAgainst(uint32 Own_MMRating, uint32 Opponent_MMRating, int32& rating_change, const Map* bgMap)
{
    // Called when the team has won
    // Change in Matchmaker rating
    int32 mod = GetMatchmakerRatingMod(Own_MMRating, Opponent_MMRating, true);

    // Change in Team Rating
    rating_change = GetRatingMod(Stats.Rating, Opponent_MMRating, true);

    // Modify the team stats accordingly
    FinishGame(rating_change, bgMap);

    // Update number of wins per season and week
    Stats.WeekWins += 1;
    Stats.SeasonWins += 1;

    // Return the rating change, used to display it on the results screen
    return mod;
}

int32 ArenaTeam::LostAgainst(uint32 Own_MMRating, uint32 Opponent_MMRating, int32& rating_change, const Map* bgMap)
{
    // Called when the team has lost
    // Change in Matchmaker Rating
    int32 mod = GetMatchmakerRatingMod(Own_MMRating, Opponent_MMRating, false);

    // Change in Team Rating
    rating_change = GetRatingMod(Stats.Rating, Opponent_MMRating, false);

    // Modify the team stats accordingly
    FinishGame(rating_change, bgMap);

    // return the rating change, used to display it on the results screen
    return mod;
}

void ArenaTeam::MemberLost(Player* player, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange)
{
    // Called for each participant of a match after losing
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == player->GetGUID())
        {
            // Update personal rating
            int32 mod = GetRatingMod(itr->PersonalRating, againstMatchmakerRating, false);
            itr->ModifyPersonalRating(player, mod, GetType());

            // Update matchmaker rating
            itr->ModifyMatchmakerRating(MatchmakerRatingChange, GetSlot());

            // Update personal played stats
            itr->WeekGames += 1;
            itr->SeasonGames += 1;

            // update the unit fields
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_WEEK,  itr->WeekGames);
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_SEASON,  itr->SeasonGames);
            return;
        }
    }
}

void ArenaTeam::MemberWon(Player* player, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange)
{
    // called for each participant after winning a match
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == player->GetGUID())
        {
            // update personal rating
            int32 mod = GetRatingMod(itr->PersonalRating, againstMatchmakerRating, true);
            sScriptMgr->OnBeforeUpdatingPersonalRating(mod, GetType());
            itr->ModifyPersonalRating(player, mod, GetType());

            // update matchmaker rating (pussywizard: but don't allow it to go over team rating)
            if (itr->MatchMakerRating < Stats.Rating)
            {
                mod = std::min(MatchmakerRatingChange, Stats.Rating - itr->MatchMakerRating);
                sScriptMgr->OnBeforeUpdatingPersonalRating(mod, GetType());
                itr->ModifyMatchmakerRating(mod, GetSlot());
            }

            // update personal stats
            itr->WeekGames += 1;
            itr->SeasonGames += 1;
            itr->SeasonWins += 1;
            itr->WeekWins += 1;
            // update unit fields
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_WEEK, itr->WeekGames);
            player->SetArenaTeamInfoField(GetSlot(), ARENA_TEAM_GAMES_SEASON, itr->SeasonGames);
            return;
        }
    }
}

void ArenaTeam::UpdateArenaPointsHelper(std::map<uint32, uint32>& playerPoints)
{
    // Called after a match has ended and the stats are already modified
    // Helper function for arena point distribution (this way, when distributing, no actual calculation is required, just a few comparisons)
    // 10 played games per week is a minimum
    if (Stats.WeekGames < sWorld->getIntConfig(CONFIG_ARENA_GAMES_REQUIRED))
        return;

    // To get points, a player has to participate in at least 30% of the matches
    uint32 requiredGames = (uint32)ceil(Stats.WeekGames * 0.3f);

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        // The player participated in enough games, update his points
        uint32 pointsToAdd = 0;
        if (itr->WeekGames >= requiredGames)
            pointsToAdd = GetPoints(itr->PersonalRating);

        std::map<uint32, uint32>::iterator plr_itr = playerPoints.find(GUID_LOPART(itr->Guid));
        if (plr_itr != playerPoints.end())
        {
            // Check if there is already more points
            if (plr_itr->second < pointsToAdd)
                playerPoints[GUID_LOPART(itr->Guid)] = pointsToAdd;
        }
        else
            playerPoints[GUID_LOPART(itr->Guid)] = pointsToAdd;
    }
}

void ArenaTeam::SaveToDB()
{
    if (!sScriptMgr->CanSaveToDB(this))
        return;

    // Save team and member stats to db
    // Called after a match has ended or when calculating arena_points

    SQLTransaction trans = CharacterDatabase.BeginTransaction();

    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_STATS);
    stmt->setUInt16(0, Stats.Rating);
    stmt->setUInt16(1, Stats.WeekGames);
    stmt->setUInt16(2, Stats.WeekWins);
    stmt->setUInt16(3, Stats.SeasonGames);
    stmt->setUInt16(4, Stats.SeasonWins);
    stmt->setUInt32(5, Stats.Rank);
    stmt->setUInt32(6, GetId());
    trans->Append(stmt);

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_ARENA_TEAM_MEMBER);
        stmt->setUInt16(0, itr->PersonalRating);
        stmt->setUInt16(1, itr->WeekGames);
        stmt->setUInt16(2, itr->WeekWins);
        stmt->setUInt16(3, itr->SeasonGames);
        stmt->setUInt16(4, itr->SeasonWins);
        stmt->setUInt32(5, GetId());
        stmt->setUInt32(6, GUID_LOPART(itr->Guid));
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CHARACTER_ARENA_STATS);
        stmt->setUInt32(0, GUID_LOPART(itr->Guid));
        stmt->setUInt8(1, GetSlot());
        stmt->setUInt16(2, itr->MatchMakerRating);
        stmt->setUInt16(3, itr->MaxMMR);
        trans->Append(stmt);
    }

    CharacterDatabase.CommitTransaction(trans);
}

void ArenaTeam::FinishWeek()
{
    // Reset team stats
    Stats.WeekGames = 0;
    Stats.WeekWins = 0;

    // Reset member stats
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        itr->WeekGames = 0;
        itr->WeekWins = 0;
    }
}

bool ArenaTeam::IsFighting() const
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(itr->Guid))
            if (player->GetMap()->IsBattleArena())
                return true;

    return false;
}

ArenaTeamMember* ArenaTeam::GetMember(const std::string& name)
{
    return GetMember(sObjectMgr->GetPlayerGUIDByName(name));
}

ArenaTeamMember* ArenaTeam::GetMember(uint64 guid)
{
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Guid == guid)
            return &(*itr);

    return nullptr;
}

uint8 ArenaTeam::GetReqPlayersForType(uint32 type)
{
    auto const& itr = ArenaReqPlayersForType.find(type);
    if (itr == ArenaReqPlayersForType.end())
    {
        sLog->outError("FATAL: Unknown arena type %u!", type);
        return 0xFF;
    }

    return ArenaReqPlayersForType.at(type);
}

void ArenaTeam::CreateTempArenaTeam(std::vector<Player*> playerList, uint8 type, std::string const& teamName)
{
    auto playerCountInTeam = static_cast<uint32>(playerList.size());

    ASSERT(playerCountInTeam == GetReqPlayersForType(type));

    // Generate new arena team id
    TeamId = sArenaTeamMgr->GenerateTempArenaTeamId();

    // Assign member variables
    CaptainGuid = playerList[0]->GetGUID();
    Type = type;
    TeamName = teamName;

    BackgroundColor = 0;
    EmblemStyle = 0;
    EmblemColor = 0;
    BorderStyle = 0;
    BorderColor = 0;

    Stats.WeekGames = 0;
    Stats.SeasonGames = 0;
    Stats.Rating = 0;
    Stats.WeekWins = 0;
    Stats.SeasonWins = 0;

    for (auto const& _player : playerList)
    {
        ArenaTeam* team = sArenaTeamMgr->GetArenaTeamById(_player->GetArenaTeamId(GetSlotByType(type)));
        if (!team)
            continue;

        ArenaTeamMember newMember;
        for (auto const& itr : Members)
            newMember = itr;

        Stats.WeekGames += team->Stats.WeekGames;
        Stats.SeasonGames += team->Stats.SeasonGames;
        Stats.Rating += team->GetRating();
        Stats.WeekWins += team->Stats.WeekWins;
        Stats.SeasonWins += team->Stats.SeasonWins;

        Members.push_back(newMember);
    }

    Stats.WeekGames /= playerCountInTeam;
    Stats.SeasonGames /= playerCountInTeam;
    Stats.Rating /= playerCountInTeam;
    Stats.WeekWins /= playerCountInTeam;
    Stats.SeasonWins /= playerCountInTeam;
}

// init/update unordered_map ArenaSlotByType
std::unordered_map<uint32, uint8> ArenaTeam::ArenaSlotByType =
{
    { ARENA_TEAM_2v2, ARENA_SLOT_2v2},
    { ARENA_TEAM_3v3, ARENA_SLOT_3v3},
    { ARENA_TEAM_5v5, ARENA_SLOT_5v5}
};

// init/update unordered_map ArenaReqPlayersForType
std::unordered_map<uint8, uint8> ArenaTeam::ArenaReqPlayersForType =
{
    { ARENA_TYPE_2v2, 4},
    { ARENA_TYPE_3v3, 6},
    { ARENA_TYPE_5v5, 10}
};
