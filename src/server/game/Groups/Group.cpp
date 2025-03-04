/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU GPL v2 license, you may redistribute it and/or modify it under version 2 of the License, or (at your option), any later version.
 * Copyright (C) 2008-2016 TrinityCore <http://www.trinitycore.org/>
 * Copyright (C) 2005-2009 MaNGOS <http://getmangos.com/>
 */

#include "Battleground.h"
#include "BattlegroundMgr.h"
#include "Common.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceSaveMgr.h"
#include "LFG.h"
#include "LFGMgr.h"
#include "Log.h"
#include "MapManager.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "SocialMgr.h"
#include "SpellAuras.h"
#include "UpdateFieldFlags.h"
#include "Util.h"
#include "Vehicle.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

Roll::Roll(uint64 _guid, LootItem const& li) : itemGUID(_guid), itemid(li.itemid),
    itemRandomPropId(li.randomPropertyId), itemRandomSuffix(li.randomSuffix), itemCount(li.count),
    totalPlayersRolling(0), totalNeed(0), totalGreed(0), totalPass(0), itemSlot(0),
    rollVoteMask(ROLL_ALL_TYPE_NO_DISENCHANT)
{
}

Roll::~Roll()
{
}

void Roll::setLoot(Loot* pLoot)
{
    link(pLoot, this);
}

Loot* Roll::getLoot()
{
    return getTarget();
}

Group::Group() : m_leaderGuid(0), m_leaderName(""), m_groupType(GROUPTYPE_NORMAL),
    m_dungeonDifficulty(DUNGEON_DIFFICULTY_NORMAL), m_raidDifficulty(RAID_DIFFICULTY_10MAN_NORMAL),
    m_bfGroup(nullptr), m_bgGroup(nullptr), m_lootMethod(FREE_FOR_ALL), m_lootThreshold(ITEM_QUALITY_UNCOMMON), m_looterGuid(0),
    m_masterLooterGuid(0), m_subGroupsCounts(nullptr), m_guid(0), m_counter(0), m_maxEnchantingLevel(0), _difficultyChangePreventionTime(0),
    _difficultyChangePreventionType(DIFFICULTY_PREVENTION_CHANGE_NONE)
{
    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
        m_targetIcons[i] = 0;
        sScriptMgr->OnConstructGroup(this);
}

Group::~Group()
{
    sScriptMgr->OnDestructGroup(this);

    if (m_bgGroup)
    {
#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
        sLog->outDebug(LOG_FILTER_BATTLEGROUND, "Group::~Group: battleground group being deleted.");
#endif
        if (m_bgGroup->GetBgRaid(TEAM_ALLIANCE) == this) m_bgGroup->SetBgRaid(TEAM_ALLIANCE, nullptr);
        else if (m_bgGroup->GetBgRaid(TEAM_HORDE) == this) m_bgGroup->SetBgRaid(TEAM_HORDE, nullptr);
        else sLog->outError("Group::~Group: battleground group is not linked to the correct battleground.");
    }
    Rolls::iterator itr;
    while (!RollId.empty())
    {
        itr = RollId.begin();
        Roll* r = *itr;
        RollId.erase(itr);
        delete(r);
    }

    // Sub group counters clean up
    delete[] m_subGroupsCounts;
}

bool Group::Create(Player* leader)
{
    uint64 leaderGuid = leader->GetGUID();
    uint32 lowguid = sGroupMgr->GenerateGroupId();

    m_guid = MAKE_NEW_GUID(lowguid, 0, HIGHGUID_GROUP);
    m_leaderGuid = leaderGuid;
    m_leaderName = leader->GetName();
    leader->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);

    if (isBGGroup() || isBFGroup())
        m_groupType = GROUPTYPE_BGRAID;

    if (m_groupType & GROUPTYPE_RAID)
        _initRaidSubGroupsCounter();

    if (!isLFGGroup())
        m_lootMethod = GROUP_LOOT;

    m_lootThreshold = ITEM_QUALITY_UNCOMMON;
    m_looterGuid = leaderGuid;
    m_masterLooterGuid = 0;

    m_dungeonDifficulty = DUNGEON_DIFFICULTY_NORMAL;
    m_raidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;

    if (!isBGGroup() && !isBFGroup())
    {
        m_dungeonDifficulty = leader->GetDungeonDifficulty();
        m_raidDifficulty = leader->GetRaidDifficulty();

        // Store group in database
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_INS_GROUP);

        uint8 index = 0;

        stmt->setUInt32(index++, lowguid);
        stmt->setUInt32(index++, GUID_LOPART(m_leaderGuid));
        stmt->setUInt8(index++, uint8(m_lootMethod));
        stmt->setUInt32(index++, GUID_LOPART(m_looterGuid));
        stmt->setUInt8(index++, uint8(m_lootThreshold));
        stmt->setUInt32(index++, uint32(m_targetIcons[0]));
        stmt->setUInt32(index++, uint32(m_targetIcons[1]));
        stmt->setUInt32(index++, uint32(m_targetIcons[2]));
        stmt->setUInt32(index++, uint32(m_targetIcons[3]));
        stmt->setUInt32(index++, uint32(m_targetIcons[4]));
        stmt->setUInt32(index++, uint32(m_targetIcons[5]));
        stmt->setUInt32(index++, uint32(m_targetIcons[6]));
        stmt->setUInt32(index++, uint32(m_targetIcons[7]));
        stmt->setUInt8(index++, uint8(m_groupType));
        stmt->setUInt32(index++, uint8(m_dungeonDifficulty));
        stmt->setUInt32(index++, uint8(m_raidDifficulty));
        stmt->setUInt32(index++, GUID_LOPART(m_masterLooterGuid));

        CharacterDatabase.Execute(stmt);

        ASSERT(AddMember(leader)); // If the leader can't be added to a new group because it appears full, something is clearly wrong.

        sScriptMgr->OnCreate(this, leader);
    }
    else if (!AddMember(leader))
        return false;

    return true;
}

bool Group::LoadGroupFromDB(Field* fields)
{
    m_guid = MAKE_NEW_GUID(fields[16].GetUInt32(), 0, HIGHGUID_GROUP);
    m_leaderGuid = MAKE_NEW_GUID(fields[0].GetUInt32(), 0, HIGHGUID_PLAYER);

    // group leader not exist
    if (!sObjectMgr->GetPlayerNameByGUID(fields[0].GetUInt32(), m_leaderName))
    {
        uint32 groupLowGuid = fields[16].GetUInt32();
        SQLTransaction trans = CharacterDatabase.BeginTransaction();
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP);
        stmt->setUInt32(0, groupLowGuid);
        trans->Append(stmt);
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER_ALL);
        stmt->setUInt32(0, groupLowGuid);
        trans->Append(stmt);
        CharacterDatabase.CommitTransaction(trans);
        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_LFG_DATA);
        stmt->setUInt32(0, groupLowGuid);
        CharacterDatabase.Execute(stmt);
        return false;
    }

    m_lootMethod = LootMethod(fields[1].GetUInt8());
    m_looterGuid = MAKE_NEW_GUID(fields[2].GetUInt32(), 0, HIGHGUID_PLAYER);
    m_lootThreshold = ItemQualities(fields[3].GetUInt8());

    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
        m_targetIcons[i] = fields[4 + i].GetUInt32();

    m_groupType  = GroupType(fields[12].GetUInt8());
    if (m_groupType & GROUPTYPE_RAID)
        _initRaidSubGroupsCounter();

    uint32 diff = fields[13].GetUInt8();
    if (diff >= MAX_DUNGEON_DIFFICULTY)
        m_dungeonDifficulty = DUNGEON_DIFFICULTY_NORMAL;
    else
        m_dungeonDifficulty = Difficulty(diff);

    uint32 r_diff = fields[14].GetUInt8();
    if (r_diff >= MAX_RAID_DIFFICULTY)
        m_raidDifficulty = RAID_DIFFICULTY_10MAN_NORMAL;
    else
        m_raidDifficulty = Difficulty(r_diff);

    m_masterLooterGuid = MAKE_NEW_GUID(fields[15].GetUInt32(), 0, HIGHGUID_PLAYER);

    if (m_groupType & GROUPTYPE_LFG)
        sLFGMgr->_LoadFromDB(fields, GetGUID());

    return true;
}

void Group::LoadMemberFromDB(uint32 guidLow, uint8 memberFlags, uint8 subgroup, uint8 roles)
{
    MemberSlot member;
    member.guid = MAKE_NEW_GUID(guidLow, 0, HIGHGUID_PLAYER);

    // skip non-existed member
    if (!sObjectMgr->GetPlayerNameByGUID(member.guid, member.name))
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER);
        stmt->setUInt32(0, guidLow);
        stmt->setUInt32(1, GetLowGUID());
        CharacterDatabase.Execute(stmt);
        return;
    }

    member.group = subgroup;
    member.flags = memberFlags;
    member.roles = roles;

    m_memberSlots.push_back(member);
    if (!isBGGroup() && !isBFGroup())
        sWorld->UpdateGlobalPlayerGroup(guidLow, GetLowGUID());

    SubGroupCounterIncrease(subgroup);

    sLFGMgr->SetupGroupMember(member.guid, GetGUID());
}

void Group::ConvertToLFG()
{
    m_groupType = GroupType(m_groupType | GROUPTYPE_LFG | GROUPTYPE_LFG_RESTRICTED);
    m_lootMethod = NEED_BEFORE_GREED;
    if (!isBGGroup() && !isBFGroup())
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_TYPE);

        stmt->setUInt8(0, uint8(m_groupType));
        stmt->setUInt32(1, GetLowGUID());

        CharacterDatabase.Execute(stmt);
    }

    SendUpdate();
}

void Group::ConvertToRaid()
{
    m_groupType = GroupType(m_groupType | GROUPTYPE_RAID);

    _initRaidSubGroupsCounter();

    if (!isBGGroup() && !isBFGroup())
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_TYPE);

        stmt->setUInt8(0, uint8(m_groupType));
        stmt->setUInt32(1, GetLowGUID());

        CharacterDatabase.Execute(stmt);
    }

    SendUpdate();

    // update quest related GO states (quest activity dependent from raid membership)
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
        if (Player* player = ObjectAccessor::FindPlayer(citr->guid))
            player->UpdateForQuestWorldObjects();

    // pussywizard: client automatically clears df "eye" near minimap, so remove from raid browser
    if (sLFGMgr->GetState(GetLeaderGUID()) == lfg::LFG_STATE_RAIDBROWSER)
        sLFGMgr->LeaveLfg(GetLeaderGUID());
}

bool Group::AddInvite(Player* player)
{
    if (!player || player->GetGroupInvite())
        return false;
    Group* group = player->GetGroup();
    if (group && (group->isBGGroup() || group->isBFGroup()))
        group = player->GetOriginalGroup();
    if (group)
        return false;

    RemoveInvite(player);

    m_invitees.insert(player);

    player->SetGroupInvite(this);

    sScriptMgr->OnGroupInviteMember(this, player->GetGUID());

    return true;
}

bool Group::AddLeaderInvite(Player* player)
{
    if (!AddInvite(player))
        return false;

    m_leaderGuid = player->GetGUID();
    m_leaderName = player->GetName();
    return true;
}

void Group::RemoveInvite(Player* player)
{
    if (player)
    {
        if (!m_invitees.empty())
            m_invitees.erase(player);
        player->SetGroupInvite(nullptr);
    }
}

void Group::RemoveAllInvites()
{
    for (InvitesList::iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
        if (*itr)
            (*itr)->SetGroupInvite(nullptr);

    m_invitees.clear();
}

Player* Group::GetInvited(uint64 guid) const
{
    for (InvitesList::const_iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
    {
        if ((*itr) && (*itr)->GetGUID() == guid)
            return (*itr);
    }
    return nullptr;
}

Player* Group::GetInvited(const std::string& name) const
{
    for (InvitesList::const_iterator itr = m_invitees.begin(); itr != m_invitees.end(); ++itr)
    {
        if ((*itr) && (*itr)->GetName() == name)
            return (*itr);
    }
    return nullptr;
}

bool Group::AddMember(Player* player)
{
    if (!player)
        return false;

    // Get first not-full group
    uint8 subGroup = 0;
    if (m_subGroupsCounts)
    {
        bool groupFound = false;
        for (; subGroup < MAX_RAID_SUBGROUPS; ++subGroup)
        {
            if (m_subGroupsCounts[subGroup] < MAXGROUPSIZE)
            {
                groupFound = true;
                break;
            }
        }
        // We are raid group and no one slot is free
        if (!groupFound)
            return false;
    }

    MemberSlot member;
    member.guid      = player->GetGUID();
    member.name      = player->GetName();
    member.group     = subGroup;
    member.flags     = 0;
    member.roles     = 0;
    m_memberSlots.push_back(member);
    if (!isBGGroup() && !isBFGroup())
        sWorld->UpdateGlobalPlayerGroup(player->GetGUIDLow(), GetLowGUID());

    SubGroupCounterIncrease(subGroup);

    player->SetGroupInvite(nullptr);
    if (player->GetGroup())
    {
        if (isBGGroup() || isBFGroup()) // if player is in group and he is being added to BG raid group, then call SetBattlegroundRaid()
            player->SetBattlegroundOrBattlefieldRaid(this, subGroup);
        else //if player is in bg raid and we are adding him to normal group, then call SetOriginalGroup()
            player->SetOriginalGroup(this, subGroup);
    }
    else //if player is not in group, then call set group
        player->SetGroup(this, subGroup);

    // if the same group invites the player back, cancel the homebind timer
    _cancelHomebindIfInstance(player);

    if (!isRaidGroup())                                      // reset targetIcons for non-raid-groups
    {
        for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
            m_targetIcons[i] = 0;
    }

    if (!isBGGroup() && !isBFGroup())
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_GROUP_MEMBER);
        stmt->setUInt32(0, GetLowGUID());
        stmt->setUInt32(1, GUID_LOPART(member.guid));
        stmt->setUInt8(2, member.flags);
        stmt->setUInt8(3, member.group);
        stmt->setUInt8(4, member.roles);
        CharacterDatabase.Execute(stmt);
    }

    SendUpdate();

    if (player)
    {
        sScriptMgr->OnGroupAddMember(this, player->GetGUID());

        if (!IsLeader(player->GetGUID()) && !isBGGroup() && !isBFGroup())
        {
            Player::ResetInstances(player->GetGUIDLow(), INSTANCE_RESET_GROUP_JOIN, false);

            if (player->GetDungeonDifficulty() != GetDungeonDifficulty())
            {
                player->SetDungeonDifficulty(GetDungeonDifficulty());
                player->SendDungeonDifficulty(true);
            }
            if (player->GetRaidDifficulty() != GetRaidDifficulty())
            {
                player->SetRaidDifficulty(GetRaidDifficulty());
                player->SendRaidDifficulty(true);
            }
        }
        else if (IsLeader(player->GetGUID()) && isLFGGroup()) // pussywizard
        {
            Player::ResetInstances(player->GetGUIDLow(), INSTANCE_RESET_GROUP_JOIN, false);
        }

        player->SetGroupUpdateFlag(GROUP_UPDATE_FULL);
        UpdatePlayerOutOfRange(player);

        // quest related GO state dependent from raid membership
        if (isRaidGroup())
            player->UpdateForQuestWorldObjects();

        {
            // Broadcast new player group member fields to rest of the group
            player->SetFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);

            UpdateData groupData;
            WorldPacket groupDataPacket;

            // Broadcast group members' fields to player
            for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                if (itr->GetSource() == player) // pussywizard: no check same map, adding members is single threaded
                    continue;

                if (Player* member = itr->GetSource())
                {
                    if (player->HaveAtClient(member))
                    {
                        member->SetFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
                        member->BuildValuesUpdateBlockForPlayer(&groupData, player);
                        member->RemoveFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
                    }

                    if (member->HaveAtClient(player))
                    {
                        UpdateData newData;
                        WorldPacket newDataPacket;
                        player->BuildValuesUpdateBlockForPlayer(&newData, member);
                        if (newData.HasData())
                        {
                            newData.BuildPacket(&newDataPacket);
                            member->SendDirectMessage(&newDataPacket);
                        }
                    }
                }
            }

            if (groupData.HasData())
            {
                groupData.BuildPacket(&groupDataPacket);
                player->SendDirectMessage(&groupDataPacket);
            }

            player->RemoveFieldNotifyFlag(UF_FLAG_PARTY_MEMBER);
        }

        if (m_maxEnchantingLevel < player->GetSkillValue(SKILL_ENCHANTING))
            m_maxEnchantingLevel = player->GetSkillValue(SKILL_ENCHANTING);
    }

    return true;
}

bool Group::RemoveMember(uint64 guid, const RemoveMethod& method /*= GROUP_REMOVEMETHOD_DEFAULT*/, uint64 kicker /*= 0*/, const char* reason /*= nullptr*/)
{
    BroadcastGroupUpdate();

    // LFG group vote kick handled in scripts
    if (isLFGGroup() && method == GROUP_REMOVEMETHOD_KICK)
    {
        sLFGMgr->InitBoot(GetGUID(), kicker, guid, std::string(reason ? reason : ""));
        return m_memberSlots.size() > 0;
    }

    // remove member and change leader (if need) only if strong more 2 members _before_ member remove (BG/BF allow 1 member group)
    if (GetMembersCount() > ((isBGGroup() || isLFGGroup() || isBFGroup()) ? 1u : 2u))
    {
        Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(guid);
        if (player)
        {
            // Battleground group handling
            if (isBGGroup() || isBFGroup())
                player->RemoveFromBattlegroundOrBattlefieldRaid();
            else
                // Regular group
            {
                if (player->GetOriginalGroup() == this)
                    player->SetOriginalGroup(nullptr);
                else
                    player->SetGroup(nullptr);

                // quest related GO state dependent from raid membership
                player->UpdateForQuestWorldObjects();
            }

            WorldPacket data;

            if (method == GROUP_REMOVEMETHOD_KICK || method == GROUP_REMOVEMETHOD_KICK_LFG)
            {
                data.Initialize(SMSG_GROUP_UNINVITE, 0);
                player->GetSession()->SendPacket(&data);
            }

            // Do we really need to send this opcode?
            data.Initialize(SMSG_GROUP_LIST, 1 + 1 + 1 + 1 + 8 + 4 + 4 + 8);
            data << uint8(0x10) << uint8(0) << uint8(0) << uint8(0);
            data << uint64(m_guid) << uint32(m_counter) << uint32(0) << uint64(0);
            player->GetSession()->SendPacket(&data);
        }

        // Remove player from group in DB
        if (!isBGGroup() && !isBFGroup())
        {
            PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER);
            stmt->setUInt32(0, GUID_LOPART(guid));
            stmt->setUInt32(1, GetLowGUID());
            CharacterDatabase.Execute(stmt);
        }

        // Reevaluate group enchanter if the leaving player had enchanting skill or the player is offline
        if (!player || player->GetSkillValue(SKILL_ENCHANTING))
            ResetMaxEnchantingLevel();

        // Remove player from loot rolls
        for (Rolls::iterator it = RollId.begin(); it != RollId.end();)
        {
            Roll* roll = *it;
            Roll::PlayerVote::iterator itr2 = roll->playerVote.find(guid);
            if (itr2 == roll->playerVote.end())
            {
                ++it;
                continue;
            }

            if (itr2->second == GREED || itr2->second == DISENCHANT)
                --roll->totalGreed;
            else if (itr2->second == NEED)
                --roll->totalNeed;
            else if (itr2->second == PASS)
                --roll->totalPass;

            if (itr2->second != NOT_VALID)
                --roll->totalPlayersRolling;

            roll->playerVote.erase(itr2);

            // Xinef: itr can be erased inside
            // Xinef: player is removed from all vote lists so it will not pass above playerVote == playerVote.end statement during second iteration
            if (CountRollVote(guid, roll->itemGUID, MAX_ROLL_TYPE))
                it = RollId.begin();
            else
                ++it;
        }

        // Update subgroups
        member_witerator slot = _getMemberWSlot(guid);
        if (slot != m_memberSlots.end())
        {
            SubGroupCounterDecrease(slot->group);
            m_memberSlots.erase(slot);
            if (!isBGGroup() && !isBFGroup())
                sWorld->UpdateGlobalPlayerGroup(GUID_LOPART(guid), 0);
        }

        // Pick new leader if necessary
        bool validLeader = true;
        if (m_leaderGuid == guid)
        {
            validLeader = false;
            for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
            {
                if (ObjectAccessor::FindPlayerInOrOutOfWorld(itr->guid))
                {
                    ChangeLeader(itr->guid);
                    validLeader = true;
                    break;
                }
            }
        }

        _homebindIfInstance(player);
        if (!isBGGroup() && !isBFGroup())
            Player::ResetInstances(guid, INSTANCE_RESET_GROUP_LEAVE, false);

        sScriptMgr->OnGroupRemoveMember(this, guid, method, kicker, reason);

        SendUpdate();

        if (!validLeader)
        {
            // pussywizard: temp do nothing, something causes crashes in MakeNewGroup
            //Disband();
            //return false;
        }

        if (isLFGGroup() && GetMembersCount() == 1)
        {
            Player* leader = ObjectAccessor::FindPlayerInOrOutOfWorld(GetLeaderGUID());
            uint32 mapId = sLFGMgr->GetDungeonMapId(GetGUID());
            lfg::LfgState state = sLFGMgr->GetState(GetGUID());
            if (!mapId || !leader || (leader->IsAlive() && leader->GetMapId() != mapId) || state == lfg::LFG_STATE_NONE)
            {
                Disband();
                return false;
            }
        }

        if (m_memberMgr.getSize() < ((isLFGGroup() || isBGGroup() || isBFGroup()) ? 1u : 2u))
        {
            Disband();
            return false;
        }

        return true;
    }
    // If group size before player removal <= 2 then disband it
    else
    {
        sScriptMgr->OnGroupRemoveMember(this, guid, method, kicker, reason);
        Disband();
        return false;
    }
}

void Group::ChangeLeader(uint64 newLeaderGuid)
{
    member_witerator slot = _getMemberWSlot(newLeaderGuid);

    if (slot == m_memberSlots.end())
        return;

    Player* newLeader = ObjectAccessor::FindPlayerInOrOutOfWorld(slot->guid);

    // Don't allow switching leader to offline players
    if (!newLeader)
        return;

    if (!isBGGroup() && !isBFGroup())
    {
        SQLTransaction trans = CharacterDatabase.BeginTransaction();
        // Update the group leader
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_LEADER);
        stmt->setUInt32(0, newLeader->GetGUIDLow());
        stmt->setUInt32(1, GetLowGUID());
        trans->Append(stmt);
        CharacterDatabase.CommitTransaction(trans);

        sInstanceSaveMgr->CopyBinds(m_leaderGuid, newLeaderGuid, newLeader);
    }

    if (Player* oldLeader = ObjectAccessor::FindPlayerInOrOutOfWorld(m_leaderGuid))
        oldLeader->RemoveFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);

    newLeader->SetFlag(PLAYER_FLAGS, PLAYER_FLAGS_GROUP_LEADER);
    m_leaderGuid = newLeader->GetGUID();
    m_leaderName = newLeader->GetName();
    ToggleGroupMemberFlag(slot, MEMBER_FLAG_ASSISTANT, false);

    WorldPacket data(SMSG_GROUP_SET_LEADER, m_leaderName.size() + 1);
    data << slot->name;
    BroadcastPacket(&data, true);

    sScriptMgr->OnGroupChangeLeader(this, newLeaderGuid, m_leaderGuid); // This hook should be executed at the end - Not used anywhere in the original core
}

void Group::Disband(bool hideDestroy /* = false */)
{
    sScriptMgr->OnGroupDisband(this);

    Player* player;
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        if (!isBGGroup() && !isBFGroup())
            sWorld->UpdateGlobalPlayerGroup(GUID_LOPART(citr->guid), 0);

        player = ObjectAccessor::FindPlayerInOrOutOfWorld(citr->guid);

        _homebindIfInstance(player);
        if (!isBGGroup() && !isBFGroup())
            Player::ResetInstances(citr->guid, INSTANCE_RESET_GROUP_LEAVE, false);

        if (!player)
            continue;

        //we cannot call _removeMember because it would invalidate member iterator
        //if we are removing player from battleground raid
        if (isBGGroup() || isBFGroup())
            player->RemoveFromBattlegroundOrBattlefieldRaid();
        else
        {
            //we can remove player who is in battleground from his original group
            if (player->GetOriginalGroup() == this)
                player->SetOriginalGroup(nullptr);
            else
                player->SetGroup(nullptr);
        }

        // quest related GO state dependent from raid membership
        if (isRaidGroup())
            player->UpdateForQuestWorldObjects();

        WorldPacket data;
        if (!hideDestroy)
        {
            data.Initialize(SMSG_GROUP_DESTROYED, 0);
            player->GetSession()->SendPacket(&data);
        }

        //we already removed player from group and in player->GetGroup() is his original group, send update
        if (Group* group = player->GetGroup())
        {
            group->SendUpdate();
        }
        else
        {
            data.Initialize(SMSG_GROUP_LIST, 1 + 1 + 1 + 1 + 8 + 4 + 4 + 8);
            data << uint8(0x10) << uint8(0) << uint8(0) << uint8(0);
            data << uint64(m_guid) << uint32(m_counter) << uint32(0) << uint64(0);
            player->GetSession()->SendPacket(&data);
        }
    }
    RollId.clear();
    m_memberSlots.clear();

    RemoveAllInvites();

    if (!isBGGroup() && !isBFGroup())
    {
        SQLTransaction trans = CharacterDatabase.BeginTransaction();

        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP);
        stmt->setUInt32(0, GetLowGUID());
        trans->Append(stmt);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_GROUP_MEMBER_ALL);
        stmt->setUInt32(0, GetLowGUID());
        trans->Append(stmt);

        CharacterDatabase.CommitTransaction(trans);

        stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_LFG_DATA);
        stmt->setUInt32(0, GetLowGUID());
        CharacterDatabase.Execute(stmt);
    }

    sGroupMgr->RemoveGroup(this);
    delete this;
}

/*********************************************************/
/***                   LOOT SYSTEM                     ***/
/*********************************************************/

void Group::SendLootStartRoll(uint32 CountDown, uint32 mapid, const Roll& r)
{
    WorldPacket data(SMSG_LOOT_START_ROLL, (8 + 4 + 4 + 4 + 4 + 4 + 4 + 1));
    data << uint64(r.itemGUID);                             // guid of rolled item
    data << uint32(mapid);                                  // 3.3.3 mapid
    data << uint32(r.itemSlot);                             // itemslot
    data << uint32(r.itemid);                               // the itemEntryId for the item that shall be rolled for
    data << uint32(r.itemRandomSuffix);                     // randomSuffix
    data << uint32(r.itemRandomPropId);                     // item random property ID
    data << uint32(r.itemCount);                            // items in stack
    data << uint32(CountDown);                              // the countdown time to choose "need" or "greed"
    data << uint8(r.rollVoteMask);                          // roll type mask

    for (Roll::PlayerVote::const_iterator itr = r.playerVote.begin(); itr != r.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!p)
            continue;

        if (itr->second == NOT_EMITED_YET)
            p->GetSession()->SendPacket(&data);
    }
}

void Group::SendLootStartRollToPlayer(uint32 countDown, uint32 mapId, Player* p, bool canNeed, Roll const& r)
{
    if (!p)
        return;

    WorldPacket data(SMSG_LOOT_START_ROLL, (8 + 4 + 4 + 4 + 4 + 4 + 4 + 1));
    data << uint64(r.itemGUID);                             // guid of rolled item
    data << uint32(mapId);                                  // 3.3.3 mapid
    data << uint32(r.itemSlot);                             // itemslot
    data << uint32(r.itemid);                               // the itemEntryId for the item that shall be rolled for
    data << uint32(r.itemRandomSuffix);                     // randomSuffix
    data << uint32(r.itemRandomPropId);                     // item random property ID
    data << uint32(r.itemCount);                            // items in stack
    data << uint32(countDown);                              // the countdown time to choose "need" or "greed"
    uint8 voteMask = r.rollVoteMask;
    if (!canNeed)
        voteMask &= ~ROLL_FLAG_TYPE_NEED;
    data << uint8(voteMask);                                // roll type mask

    p->GetSession()->SendPacket(&data);
}

void Group::SendLootRoll(uint64 sourceGuid, uint64 targetGuid, uint8 rollNumber, uint8 rollType, Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ROLL, (8 + 4 + 8 + 4 + 4 + 4 + 1 + 1 + 1));
    data << uint64(sourceGuid);                             // guid of the item rolled
    data << uint32(roll.itemSlot);                          // slot
    data << uint64(targetGuid);
    data << uint32(roll.itemid);                            // the itemEntryId for the item that shall be rolled for
    data << uint32(roll.itemRandomSuffix);                  // randomSuffix
    data << uint32(roll.itemRandomPropId);                  // Item random property ID
    data << uint8(rollNumber);                              // 0: "Need for: [item name]" > 127: "you passed on: [item name]"      Roll number
    data << uint8(rollType);                                // 0: "Need for: [item name]" 0: "You have selected need for [item name] 1: need roll 2: greed roll
    data << uint8(0);                                       // 1: "You automatically passed on: %s because you cannot loot that item." - Possibly used in need befor greed

    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!p)
            continue;

        if (itr->second != NOT_VALID)
            p->GetSession()->SendPacket(&data);
    }
}

void Group::SendLootRollWon(uint64 sourceGuid, uint64 targetGuid, uint8 rollNumber, uint8 rollType, Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ROLL_WON, (8 + 4 + 4 + 4 + 4 + 8 + 1 + 1));
    data << uint64(sourceGuid);                             // guid of the item rolled
    data << uint32(roll.itemSlot);                          // slot
    data << uint32(roll.itemid);                            // the itemEntryId for the item that shall be rolled for
    data << uint32(roll.itemRandomSuffix);                  // randomSuffix
    data << uint32(roll.itemRandomPropId);                  // Item random property
    data << uint64(targetGuid);                             // guid of the player who won.
    data << uint8(rollNumber);                              // rollnumber realted to SMSG_LOOT_ROLL
    data << uint8(rollType);                                // rollType related to SMSG_LOOT_ROLL

    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* p = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!p)
            continue;

        if (itr->second != NOT_VALID)
            p->GetSession()->SendPacket(&data);
    }
}

void Group::SendLootAllPassed(Roll const& roll)
{
    WorldPacket data(SMSG_LOOT_ALL_PASSED, (8 + 4 + 4 + 4 + 4));
    data << uint64(roll.itemGUID);                             // Guid of the item rolled
    data << uint32(roll.itemSlot);                             // Item loot slot
    data << uint32(roll.itemid);                               // The itemEntryId for the item that shall be rolled for
    data << uint32(roll.itemRandomPropId);                     // Item random property ID
    data << uint32(roll.itemRandomSuffix);                     // Item random suffix ID

    for (Roll::PlayerVote::const_iterator itr = roll.playerVote.begin(); itr != roll.playerVote.end(); ++itr)
    {
        Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(itr->first);
        if (!player)
            continue;

        if (itr->second != NOT_VALID)
            player->GetSession()->SendPacket(&data);
    }
}

// notify group members which player is the allowed looter for the given creature
void Group::SendLooter(Creature* creature, Player* groupLooter)
{
    ASSERT(creature);

    WorldPacket data(SMSG_LOOT_LIST, (8 + 8));
    data << uint64(creature->GetGUID());

    if (GetLootMethod() == MASTER_LOOT && creature->loot.hasOverThresholdItem())
        data.appendPackGUID(GetMasterLooterGuid());
    else
        data << uint8(0);

    if (groupLooter)
        data.append(groupLooter->GetPackGUID());
    else
        data << uint8(0);

    BroadcastPacket(&data, false);
}

void Group::GroupLoot(Loot* loot, WorldObject* pLootedObject)
{
    std::vector<LootItem>::iterator i;
    ItemTemplate const* item;
    uint8 itemSlot = 0;

    for (i = loot->items.begin(); i != loot->items.end(); ++i, ++itemSlot)
    {
        if (i->freeforall)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        if (!item)
        {
            //sLog->outDebug("Group::GroupLoot: missing item prototype for item with id: %d", i->itemid);
            continue;
        }

        //roll for over-threshold item if it's one-player loot
        if (item->Quality >= uint32(m_lootThreshold))
        {
            uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
            Roll* r = new Roll(newitemGUID, *i);

            //a vector is filled with only near party members
            for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* member = itr->GetSource();
                if (!member)
                    continue;
                if (member->IsAtGroupRewardDistance(pLootedObject))
                {
                    if (i->AllowedForPlayer(member))
                    {
                        r->totalPlayersRolling++;

                        if (member->GetPassOnGroupLoot())
                        {
                            r->playerVote[member->GetGUID()] = PASS;
                            r->totalPass++;
                            // can't broadcast the pass now. need to wait until all rolling players are known.
                        }
                        else
                            r->playerVote[member->GetGUID()] = NOT_EMITED_YET;
                    }
                }
            }

            if (r->totalPlayersRolling > 0)
            {
                r->setLoot(loot);
                r->itemSlot = itemSlot;
                if (item->DisenchantID && m_maxEnchantingLevel >= item->RequiredDisenchantSkill)
                    r->rollVoteMask |= ROLL_FLAG_TYPE_DISENCHANT;

                loot->items[itemSlot].is_blocked = true;

                // If there is any "auto pass", broadcast the pass now.
                if (r->totalPass)
                {
                    for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
                    {
                        Player* p = ObjectAccessor::FindPlayer(itr->first);
                        if (!p)
                            continue;

                        if (itr->second == PASS)
                            SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                    }
                }

                SendLootStartRoll(60000, pLootedObject->GetMapId(), *r);

                RollId.push_back(r);

                if (Creature* creature = pLootedObject->ToCreature())
                {
                    creature->m_groupLootTimer = 60000;
                    creature->lootingGroupLowGUID = GetLowGUID();
                }
                else if (GameObject* go = pLootedObject->ToGameObject())
                {
                    go->m_groupLootTimer = 60000;
                    go->lootingGroupLowGUID = GetLowGUID();
                }
            }
            else
                delete r;
        }
        else
            i->is_underthreshold = true;
    }

    for (i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i, ++itemSlot)
    {
        if (!i->follow_loot_rules)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        if (!item)
        {
            //sLog->outDebug("Group::GroupLoot: missing item prototype for item with id: %d", i->itemid);
            continue;
        }

        uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
        Roll* r = new Roll(newitemGUID, *i);

        //a vector is filled with only near party members
        for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* member = itr->GetSource();
            if (!member)
                continue;

            if (member->IsAtGroupRewardDistance(pLootedObject))
            {
                if (i->AllowedForPlayer(member))
                {
                    r->totalPlayersRolling++;
                    r->playerVote[member->GetGUID()] = NOT_EMITED_YET;
                }
            }
        }

        if (r->totalPlayersRolling > 0)
        {
            r->setLoot(loot);
            r->itemSlot = itemSlot;

            loot->quest_items[itemSlot - loot->items.size()].is_blocked = true;

            SendLootStartRoll(60000, pLootedObject->GetMapId(), *r);

            RollId.push_back(r);

            if (Creature* creature = pLootedObject->ToCreature())
            {
                creature->m_groupLootTimer = 60000;
                creature->lootingGroupLowGUID = GetLowGUID();
            }
            else if (GameObject* go = pLootedObject->ToGameObject())
            {
                go->m_groupLootTimer = 60000;
                go->lootingGroupLowGUID = GetLowGUID();
            }
        }
        else
            delete r;
    }
}

void Group::NeedBeforeGreed(Loot* loot, WorldObject* lootedObject)
{
    ItemTemplate const* item;
    uint8 itemSlot = 0;
    for (std::vector<LootItem>::iterator i = loot->items.begin(); i != loot->items.end(); ++i, ++itemSlot)
    {
        if (i->freeforall)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);

        //roll for over-threshold item if it's one-player loot
        if (item->Quality >= uint32(m_lootThreshold))
        {
            uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
            Roll* r = new Roll(newitemGUID, *i);

            for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
            {
                Player* playerToRoll = itr->GetSource();
                if (!playerToRoll)
                    continue;

                if (i->AllowedForPlayer(playerToRoll) && playerToRoll->IsAtGroupRewardDistance(lootedObject))
                {
                    r->totalPlayersRolling++;
                    if (playerToRoll->GetPassOnGroupLoot())
                    {
                        r->playerVote[playerToRoll->GetGUID()] = PASS;
                        r->totalPass++;
                        // can't broadcast the pass now. need to wait until all rolling players are known.
                    }
                    else
                        r->playerVote[playerToRoll->GetGUID()] = NOT_EMITED_YET;
                }
            }

            if (r->totalPlayersRolling > 0)
            {
                r->setLoot(loot);
                r->itemSlot = itemSlot;
                if (item->DisenchantID && m_maxEnchantingLevel >= item->RequiredDisenchantSkill)
                    r->rollVoteMask |= ROLL_FLAG_TYPE_DISENCHANT;

                if (item->Flags2 & ITEM_FLAGS_EXTRA_NEED_ROLL_DISABLED)
                    r->rollVoteMask &= ~ROLL_FLAG_TYPE_NEED;

                loot->items[itemSlot].is_blocked = true;

                //Broadcast Pass and Send Rollstart
                for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
                {
                    Player* p = ObjectAccessor::FindPlayer(itr->first);
                    if (!p)
                        continue;

                    if (itr->second == PASS)
                        SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                    else
                        SendLootStartRollToPlayer(60000, lootedObject->GetMapId(), p, p->CanRollForItemInLFG(item, lootedObject) == EQUIP_ERR_OK, *r);
                }

                RollId.push_back(r);

                if (Creature* creature = lootedObject->ToCreature())
                {
                    creature->m_groupLootTimer = 60000;
                    creature->lootingGroupLowGUID = GetLowGUID();
                }
                else if (GameObject* go = lootedObject->ToGameObject())
                {
                    go->m_groupLootTimer = 60000;
                    go->lootingGroupLowGUID = GetLowGUID();
                }
            }
            else
                delete r;
        }
        else
            i->is_underthreshold = true;
    }

    for (std::vector<LootItem>::iterator i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i, ++itemSlot)
    {
        if (!i->follow_loot_rules)
            continue;

        item = sObjectMgr->GetItemTemplate(i->itemid);
        uint64 newitemGUID = MAKE_NEW_GUID(sObjectMgr->GenerateLowGuid(HIGHGUID_ITEM), 0, HIGHGUID_ITEM);
        Roll* r = new Roll(newitemGUID, *i);

        for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
        {
            Player* playerToRoll = itr->GetSource();
            if (!playerToRoll)
                continue;

            if (i->AllowedForPlayer(playerToRoll) && playerToRoll->IsAtGroupRewardDistance(lootedObject))
            {
                r->totalPlayersRolling++;
                r->playerVote[playerToRoll->GetGUID()] = NOT_EMITED_YET;
            }
        }

        if (r->totalPlayersRolling > 0)
        {
            r->setLoot(loot);
            r->itemSlot = itemSlot;

            loot->quest_items[itemSlot - loot->items.size()].is_blocked = true;

            //Broadcast Pass and Send Rollstart
            for (Roll::PlayerVote::const_iterator itr = r->playerVote.begin(); itr != r->playerVote.end(); ++itr)
            {
                Player* p = ObjectAccessor::FindPlayer(itr->first);
                if (!p)
                    continue;

                if (itr->second == PASS)
                    SendLootRoll(newitemGUID, p->GetGUID(), 128, ROLL_PASS, *r);
                else
                    SendLootStartRollToPlayer(60000, lootedObject->GetMapId(), p, p->CanRollForItemInLFG(item, lootedObject) == EQUIP_ERR_OK, *r);
            }

            RollId.push_back(r);

            if (Creature* creature = lootedObject->ToCreature())
            {
                creature->m_groupLootTimer = 60000;
                creature->lootingGroupLowGUID = GetLowGUID();
            }
            else if (GameObject* go = lootedObject->ToGameObject())
            {
                go->m_groupLootTimer = 60000;
                go->lootingGroupLowGUID = GetLowGUID();
            }
        }
        else
            delete r;
    }
}

void Group::MasterLoot(Loot* loot, WorldObject* pLootedObject)
{
#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
    sLog->outDebug(LOG_FILTER_NETWORKIO, "Group::MasterLoot (SMSG_LOOT_MASTER_LIST, 330)");
#endif

    for (std::vector<LootItem>::iterator i = loot->items.begin(); i != loot->items.end(); ++i)
    {
        if (i->freeforall)
            continue;

        i->is_blocked = !i->is_underthreshold;
    }

    for (std::vector<LootItem>::iterator i = loot->quest_items.begin(); i != loot->quest_items.end(); ++i)
    {
        if (!i->follow_loot_rules)
            continue;

        i->is_blocked = !i->is_underthreshold;
    }

    uint32 real_count = 0;

    WorldPacket data(SMSG_LOOT_MASTER_LIST, 330);
    data << (uint8)GetMembersCount();

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* looter = itr->GetSource();
        if (!looter->IsInWorld())
            continue;

        if (looter->IsAtGroupRewardDistance(pLootedObject))
        {
            data << uint64(looter->GetGUID());
            ++real_count;
        }
    }

    data.put<uint8>(0, real_count);

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* looter = itr->GetSource();
        if (looter->IsAtGroupRewardDistance(pLootedObject))
            looter->GetSession()->SendPacket(&data);
    }
}

bool Group::CountRollVote(uint64 playerGUID, uint64 Guid, uint8 Choice)
{
    Rolls::iterator rollI = GetRoll(Guid);
    if (rollI == RollId.end())
        return false;
    Roll* roll = *rollI;

    Roll::PlayerVote::iterator itr = roll->playerVote.find(playerGUID);
    // this condition means that player joins to the party after roll begins
    // Xinef: if choice == MAX_ROLL_TYPE, player was removed from the map in removefromgroup
    // Xinef: itr can be invalid as it is not used below
    if (Choice < MAX_ROLL_TYPE && itr == roll->playerVote.end())
        return false;

    if (roll->getLoot())
        if (roll->getLoot()->items.empty())
            return false;

    switch (Choice)
    {
        case ROLL_PASS:                                     // Player choose pass
            SendLootRoll(0, playerGUID, 128, ROLL_PASS, *roll);
            ++roll->totalPass;
            itr->second = PASS;
            break;
        case ROLL_NEED:                                     // player choose Need
            SendLootRoll(0, playerGUID, 0, 0, *roll);
            ++roll->totalNeed;
            itr->second = NEED;
            break;
        case ROLL_GREED:                                    // player choose Greed
            SendLootRoll(0, playerGUID, 128, ROLL_GREED, *roll);
            ++roll->totalGreed;
            itr->second = GREED;
            break;
        case ROLL_DISENCHANT:                               // player choose Disenchant
            SendLootRoll(0, playerGUID, 128, ROLL_DISENCHANT, *roll);
            ++roll->totalGreed;
            itr->second = DISENCHANT;
            break;
    }

    if (roll->totalPass + roll->totalNeed + roll->totalGreed >= roll->totalPlayersRolling)
    {
        CountTheRoll(rollI, nullptr);
        return true;
    }
    return false;
}

//called when roll timer expires
void Group::EndRoll(Loot* pLoot, Map* allowedMap)
{
    for (Rolls::iterator itr = RollId.begin(); itr != RollId.end();)
    {
        if ((*itr)->getLoot() == pLoot)
        {
            CountTheRoll(itr, allowedMap);           //i don't have to edit player votes, who didn't vote ... he will pass
            itr = RollId.begin();
        }
        else
            ++itr;
    }
}

void Group::CountTheRoll(Rolls::iterator rollI, Map* allowedMap)
{
    Roll* roll = *rollI;
    if (!roll->isValid())                                   // is loot already deleted ?
    {
        RollId.erase(rollI);
        delete roll;
        return;
    }

    //end of the roll
    if (roll->totalNeed > 0)
    {
        if (!roll->playerVote.empty())
        {
            uint8 maxresul = 0;
            uint64 maxguid = 0; // pussywizard: start with 0 >_>
            Player* player = nullptr;

            for (Roll::PlayerVote::const_iterator itr = roll->playerVote.begin(); itr != roll->playerVote.end(); ++itr)
            {
                if (itr->second != NEED)
                    continue;

                player = ObjectAccessor::FindPlayer(itr->first);
                if (!player || (allowedMap != nullptr && player->FindMap() != allowedMap))
                {
                    --roll->totalNeed;
                    continue;
                }

                uint8 randomN = urand(1, 100);
                SendLootRoll(0, itr->first, randomN, ROLL_NEED, *roll);
                if (maxresul < randomN)
                {
                    maxguid  = itr->first;
                    maxresul = randomN;
                }
            }

            if (maxguid) // pussywizard: added condition
            {
                SendLootRollWon(0, maxguid, maxresul, ROLL_NEED, *roll);
                player = ObjectAccessor::FindPlayer(maxguid);

                if (player)
                {
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_NEED_ON_LOOT, roll->itemid, maxresul);

                    ItemPosCountVec dest;
                    LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);
                    InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
                    if (msg == EQUIP_ERR_OK)
                    {
                        item->is_looted = true;
                        roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
                        roll->getLoot()->unlootedCount--;
                        AllowedLooterSet looters = item->GetAllowedLooters();
                        player->StoreNewItem(dest, roll->itemid, true, item->randomPropertyId, looters);
                        player->UpdateLootAchievements(item, roll->getLoot());
                    }
                    else
                    {
                        item->is_blocked = false;
                        item->rollWinnerGUID = player->GetGUID();
                        player->SendEquipError(msg, nullptr, nullptr, roll->itemid);
                    }
                }
            }
            else
                roll->totalNeed = 0;
        }
    }
    if (roll->totalNeed == 0 && roll->totalGreed > 0) // pussywizard: if (roll->totalNeed == 0 && ...), not else if, because numbers can be modified above if player is on a different map
    {
        if (!roll->playerVote.empty())
        {
            uint8 maxresul = 0;
            uint64 maxguid = 0; // pussywizard: start with 0
            Player* player = nullptr;
            RollVote rollvote = NOT_VALID;

            Roll::PlayerVote::iterator itr;
            for (itr = roll->playerVote.begin(); itr != roll->playerVote.end(); ++itr)
            {
                if (itr->second != GREED && itr->second != DISENCHANT)
                    continue;

                player = ObjectAccessor::FindPlayer(itr->first);
                if (!player || (allowedMap != nullptr && player->FindMap() != allowedMap))
                {
                    --roll->totalGreed;
                    continue;
                }

                uint8 randomN = urand(1, 100);
                SendLootRoll(0, itr->first, randomN, itr->second, *roll);
                if (maxresul < randomN)
                {
                    maxguid  = itr->first;
                    maxresul = randomN;
                    rollvote = itr->second;
                }
            }

            if (maxguid) // pussywizard: added condition
            {
                SendLootRollWon(0, maxguid, maxresul, rollvote, *roll);
                player = ObjectAccessor::FindPlayer(maxguid);

                if (player)
                {
                    player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_ROLL_GREED_ON_LOOT, roll->itemid, maxresul);

                    LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);

                    if (rollvote == GREED)
                    {
                        ItemPosCountVec dest;
                        InventoryResult msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, roll->itemid, item->count);
                        if (msg == EQUIP_ERR_OK)
                        {
                            item->is_looted = true;
                            roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
                            roll->getLoot()->unlootedCount--;
                            AllowedLooterSet looters = item->GetAllowedLooters();
                            player->StoreNewItem(dest, roll->itemid, true, item->randomPropertyId, looters);
                            player->UpdateLootAchievements(item, roll->getLoot());
                        }
                        else
                        {
                            item->is_blocked = false;
                            item->rollWinnerGUID = player->GetGUID();
                            player->SendEquipError(msg, nullptr, nullptr, roll->itemid);
                        }
                    }
                    else if (rollvote == DISENCHANT)
                    {
                        item->is_looted = true;
                        roll->getLoot()->NotifyItemRemoved(roll->itemSlot);
                        roll->getLoot()->unlootedCount--;
                        ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(roll->itemid);
                        player->AutoStoreLoot(pProto->DisenchantID, LootTemplates_Disenchant, true);
                        player->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CAST_SPELL, 13262); // Disenchant
                    }
                }
            }
            else
                roll->totalGreed = 0;
        }
    }
    if (roll->totalNeed == 0 && roll->totalGreed == 0) // pussywizard: if, not else, because numbers can be modified above if player is on a different map
    {
        SendLootAllPassed(*roll);

        // remove is_blocked so that the item is lootable by all players
        LootItem* item = &(roll->itemSlot >= roll->getLoot()->items.size() ? roll->getLoot()->quest_items[roll->itemSlot - roll->getLoot()->items.size()] : roll->getLoot()->items[roll->itemSlot]);
        if (item)
            item->is_blocked = false;
    }

    RollId.erase(rollI);
    delete roll;
}

void Group::SetTargetIcon(uint8 id, uint64 whoGuid, uint64 targetGuid)
{
    if (id >= TARGETICONCOUNT)
        return;

    // clean other icons
    if (targetGuid != 0)
        for (int i = 0; i < TARGETICONCOUNT; ++i)
            if (m_targetIcons[i] == targetGuid)
                SetTargetIcon(i, 0, 0);

    m_targetIcons[id] = targetGuid;

    WorldPacket data(MSG_RAID_TARGET_UPDATE, (1 + 8 + 1 + 8));
    data << uint8(0);                                       // set targets
    data << uint64(whoGuid);
    data << uint8(id);
    data << uint64(targetGuid);
    BroadcastPacket(&data, true);
}

void Group::SendTargetIconList(WorldSession* session)
{
    if (!session)
        return;

    WorldPacket data(MSG_RAID_TARGET_UPDATE, (1 + TARGETICONCOUNT * 9));
    data << uint8(1);                                       // list targets

    for (uint8 i = 0; i < TARGETICONCOUNT; ++i)
    {
        if (m_targetIcons[i] == 0)
            continue;

        data << uint8(i);
        data << uint64(m_targetIcons[i]);
    }

    session->SendPacket(&data);
}

void Group::SendUpdate()
{
    for (member_witerator witr = m_memberSlots.begin(); witr != m_memberSlots.end(); ++witr)
        SendUpdateToPlayer(witr->guid, &(*witr));
}

void Group::SendUpdateToPlayer(uint64 playerGUID, MemberSlot* slot)
{
    Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(playerGUID);

    if (!player || player->GetGroup() != this)
        return;

    // if MemberSlot wasn't provided
    if (!slot)
    {
        member_witerator witr = _getMemberWSlot(playerGUID);

        if (witr == m_memberSlots.end()) // if there is no MemberSlot for such a player
            return;

        slot = &(*witr);
    }

    WorldPacket data(SMSG_GROUP_LIST, (1 + 1 + 1 + 1 + 1 + 4 + 8 + 4 + 4 + (GetMembersCount() - 1) * (13 + 8 + 1 + 1 + 1 + 1) + 8 + 1 + 8 + 1 + 1 + 1 + 1));
    data << uint8(m_groupType);                         // group type (flags in 3.3)
    data << uint8(slot->group);
    data << uint8(slot->flags);
    data << uint8(slot->roles);
    if (isLFGGroup())
    {
        data << uint8(sLFGMgr->GetState(m_guid) == lfg::LFG_STATE_FINISHED_DUNGEON ? 2 : 0); // FIXME - Dungeon save status? 2 = done
        data << uint32(sLFGMgr->GetDungeon(m_guid));
    }

    data << uint64(m_guid);
    data << uint32(m_counter++);                        // 3.3, value increases every time this packet gets sent
    data << uint32(GetMembersCount() - 1);
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        if (slot->guid == citr->guid)
            continue;

        Player* member = ObjectAccessor::FindPlayerInOrOutOfWorld(citr->guid);

        uint8 onlineState = (member && !member->GetSession()->PlayerLogout()) ? MEMBER_STATUS_ONLINE : MEMBER_STATUS_OFFLINE;
        onlineState = onlineState | ((isBGGroup() || isBFGroup()) ? MEMBER_STATUS_PVP : 0);

        data << citr->name;
        data << uint64(citr->guid);                     // guid
        data << uint8(onlineState);                     // online-state
        data << uint8(citr->group);                     // groupid
        data << uint8(citr->flags);                     // See enum GroupMemberFlags
        data << uint8(citr->roles);                     // Lfg Roles
    }

    data << uint64(m_leaderGuid);                       // leader guid

    if (GetMembersCount() - 1)
    {
        data << uint8(m_lootMethod);                    // loot method

        if (m_lootMethod == MASTER_LOOT)
            data << uint64(m_masterLooterGuid);         // master looter guid
        else
            data << uint64(0);                          // looter guid

        data << uint8(m_lootThreshold);                 // loot threshold
        data << uint8(m_dungeonDifficulty);             // Dungeon Difficulty
        data << uint8(m_raidDifficulty);                // Raid Difficulty
        data << uint8(m_raidDifficulty >= RAID_DIFFICULTY_10MAN_HEROIC);    // 3.3 Dynamic Raid Difficulty - 0 normal/1 heroic
    }

    player->GetSession()->SendPacket(&data);
}

void Group::UpdatePlayerOutOfRange(Player* player)
{
    if (!player || !player->IsInWorld())
        return;

    WorldPacket data;
    player->GetSession()->BuildPartyMemberStatsChangedPacket(player, &data);

    Player* member;
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        member = itr->GetSource();
        if (member && (!member->IsInMap(player) || !member->IsWithinDist(player, member->GetSightRange(player), false)))
            member->GetSession()->SendPacket(&data);
    }
}

void Group::BroadcastPacket(WorldPacket* packet, bool ignorePlayersInBGRaid, int group, uint64 ignore)
{
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (!player || (ignore != 0 && player->GetGUID() == ignore) || (ignorePlayersInBGRaid && player->GetGroup() != this))
            continue;

        if (group == -1 || itr->getSubGroup() == group)
            player->GetSession()->SendPacket(packet);
    }
}

void Group::BroadcastReadyCheck(WorldPacket* packet)
{
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (player)
            if (IsLeader(player->GetGUID()) || IsAssistant(player->GetGUID()))
                player->GetSession()->SendPacket(packet);
    }
}

void Group::OfflineReadyCheck()
{
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(citr->guid);
        if (!player)
        {
            WorldPacket data(MSG_RAID_READY_CHECK_CONFIRM, 9);
            data << uint64(citr->guid);
            data << uint8(0);
            BroadcastReadyCheck(&data);
        }
    }
}

bool Group::SameSubGroup(Player const* member1, Player const* member2) const
{
    if (!member1 || !member2)
        return false;

    if (member1->GetGroup() != this || member2->GetGroup() != this)
        return false;
    else
        return member1->GetSubGroup() == member2->GetSubGroup();
}

// Allows setting sub groups both for online or offline members
void Group::ChangeMembersGroup(uint64 guid, uint8 group)
{
    // Only raid groups have sub groups
    if (!isRaidGroup())
        return;

    // Check if player is really in the raid
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    // Abort if the player is already in the target sub group
    uint8 prevSubGroup = GetMemberGroup(guid);
    if (prevSubGroup == group)
        return;

    // Update the player slot with the new sub group setting
    slot->group = group;

    // Increase the counter of the new sub group..
    SubGroupCounterIncrease(group);

    // ..and decrease the counter of the previous one
    SubGroupCounterDecrease(prevSubGroup);

    // Preserve new sub group in database for non-raid groups
    if (!isBGGroup() && !isBFGroup())
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_MEMBER_SUBGROUP);

        stmt->setUInt8(0, group);
        stmt->setUInt32(1, GUID_LOPART(guid));

        CharacterDatabase.Execute(stmt);
    }

    // In case the moved player is online, update the player object with the new sub group references
    if (Player* player = ObjectAccessor::FindPlayerInOrOutOfWorld(guid))
    {
        if (player->GetGroup() == this)
            player->GetGroupRef().setSubGroup(group);
        else // If player is in BG raid, it is possible that he is also in normal raid - and that normal raid is stored in m_originalGroup reference
            player->GetOriginalGroupRef().setSubGroup(group);
    }

    // Broadcast the changes to the group
    SendUpdate();
}

// Retrieve the next Round-Roubin player for the group
//
// No update done if loot method is FFA.
//
// If the RR player is not yet set for the group, the first group member becomes the round-robin player.
// If the RR player is set, the next player in group becomes the round-robin player.
//
// If ifneed is true,
//      the current RR player is checked to be near the looted object.
//      if yes, no update done.
//      if not, he loses his turn.
void Group::UpdateLooterGuid(WorldObject* pLootedObject, bool ifneed)
{
    // round robin style looting applies for all low
    // quality items in each loot method except free for all
    if (GetLootMethod() == FREE_FOR_ALL)
        return;

    uint64 oldLooterGUID = GetLooterGuid();
    member_citerator guid_itr = _getMemberCSlot(oldLooterGUID);
    if (guid_itr != m_memberSlots.end())
    {
        if (ifneed)
        {
            // not update if only update if need and ok
            Player* looter = ObjectAccessor::FindPlayer(guid_itr->guid);
            if (looter && looter->IsAtGroupRewardDistance(pLootedObject))
                return;
        }
        ++guid_itr;
    }

    // search next after current
    Player* pNewLooter = nullptr;
    for (member_citerator itr = guid_itr; itr != m_memberSlots.end(); ++itr)
    {
        if (Player* player = ObjectAccessor::FindPlayer(itr->guid))
            if (player->IsAtGroupRewardDistance(pLootedObject))
            {
                pNewLooter = player;
                break;
            }
    }

    if (!pNewLooter)
    {
        // search from start
        for (member_citerator itr = m_memberSlots.begin(); itr != guid_itr; ++itr)
        {
            if (Player* player = ObjectAccessor::FindPlayer(itr->guid))
                if (player->IsAtGroupRewardDistance(pLootedObject))
                {
                    pNewLooter = player;
                    break;
                }
        }
    }

    if (pNewLooter)
    {
        if (oldLooterGUID != pNewLooter->GetGUID())
        {
            SetLooterGuid(pNewLooter->GetGUID());
            SendUpdate();
        }
    }
    else
    {
        SetLooterGuid(0);
        SendUpdate();
    }
}

GroupJoinBattlegroundResult Group::CanJoinBattlegroundQueue(Battleground const* bgTemplate, BattlegroundQueueTypeId  /*bgQueueTypeId*/, uint32 MinPlayerCount, uint32 /*MaxPlayerCount*/, bool isRated, uint32 arenaSlot)
{
    // check if this group is LFG group
    if (isLFGGroup())
        return ERR_LFG_CANT_USE_BATTLEGROUND;

    BattlemasterListEntry const* bgEntry = sBattlemasterListStore.LookupEntry(bgTemplate->GetBgTypeID());
    if (!bgEntry)
        return ERR_GROUP_JOIN_BATTLEGROUND_FAIL;

    // too many players in the group
    if (GetMembersCount() > bgEntry->maxGroupSize)
        return ERR_BATTLEGROUND_NONE;

    // get a player as reference, to compare other players' stats to (arena team id, level bracket, etc.)
    Player* reference = GetFirstMember()->GetSource();
    if (!reference)
        return ERR_BATTLEGROUND_JOIN_FAILED;

    PvPDifficultyEntry const* bracketEntry = GetBattlegroundBracketByLevel(bgTemplate->GetMapId(), reference->getLevel());
    if (!bracketEntry)
        return ERR_BATTLEGROUND_JOIN_FAILED;

    uint32 arenaTeamId = reference->GetArenaTeamId(arenaSlot);
    TeamId teamId = reference->GetTeamId();

    BattlegroundQueueTypeId bgQueueTypeIdRandom = BattlegroundMgr::BGQueueTypeId(BATTLEGROUND_RB, 0);

    // check every member of the group to be able to join
    uint32 memberscount = 0;
    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next(), ++memberscount)
    {
        Player* member = itr->GetSource();

        // don't let join with offline members
        if (!member)
            return ERR_BATTLEGROUND_JOIN_FAILED;

        if (!sScriptMgr->CanGroupJoinBattlegroundQueue(this, member, bgTemplate, MinPlayerCount, isRated, arenaSlot))
            return ERR_BATTLEGROUND_JOIN_FAILED;

        // don't allow cross-faction groups to join queue
        if (member->GetTeamId() != teamId)
            return ERR_BATTLEGROUND_JOIN_TIMED_OUT;

        // don't let join rated matches if the arena team id doesn't match
        if (isRated && member->GetArenaTeamId(arenaSlot) != arenaTeamId)
            return ERR_BATTLEGROUND_JOIN_FAILED;

        // not in the same battleground level braket, don't let join
        PvPDifficultyEntry const* memberBracketEntry = GetBattlegroundBracketByLevel(bracketEntry->mapId, member->getLevel());
        if (memberBracketEntry != bracketEntry)
            return ERR_BATTLEGROUND_JOIN_RANGE_INDEX;

        // check for deserter debuff in case not arena queue
        if (bgTemplate->GetBgTypeID() != BATTLEGROUND_AA && !member->CanJoinToBattleground())
            return ERR_GROUP_JOIN_BATTLEGROUND_DESERTERS;

        // check if someone in party is using dungeon system
        if (member->isUsingLfg())
            return ERR_LFG_CANT_USE_BATTLEGROUND;

        // pussywizard: prevent joining when any member is in bg/arena
        if (member->InBattleground())
            return ERR_BATTLEGROUND_JOIN_FAILED;

        // pussywizard: check for free slot, this is actually ensured before calling this function, but just in case
        if (!member->HasFreeBattlegroundQueueId())
            return ERR_BATTLEGROUND_TOO_MANY_QUEUES;

        // don't let join if someone from the group is in bg queue random
        if (member->InBattlegroundQueueForBattlegroundQueueType(bgQueueTypeIdRandom))
            return ERR_IN_RANDOM_BG;

        // don't let join to bg queue random if someone from the group is already in bg queue
        if (bgTemplate->GetBgTypeID() == BATTLEGROUND_RB && member->InBattlegroundQueue())
            return ERR_IN_NON_RANDOM_BG;

        // don't let Death Knights join BG queues when they are not allowed to be teleported yet
        if (member->getClass() == CLASS_DEATH_KNIGHT && member->GetMapId() == 609 && !member->IsGameMaster() && !member->HasSpell(50977))
            return ERR_GROUP_JOIN_BATTLEGROUND_FAIL;
    }

    // for arenas: check party size is proper
    if (bgTemplate->isArena() && memberscount != MinPlayerCount)
        return ERR_ARENA_TEAM_PARTY_SIZE;

    return GroupJoinBattlegroundResult(bgTemplate->GetBgTypeID());
}

//===================================================
//============== Roll ===============================
//===================================================

void Roll::targetObjectBuildLink()
{
    // called from link()
    getTarget()->addLootValidatorRef(this);
}

void Group::SetDungeonDifficulty(Difficulty difficulty)
{
    m_dungeonDifficulty = difficulty;
    if (!isBGGroup() && !isBFGroup())
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_DIFFICULTY);

        stmt->setUInt8(0, uint8(m_dungeonDifficulty));
        stmt->setUInt32(1, GetLowGUID());

        CharacterDatabase.Execute(stmt);
    }

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        player->SetDungeonDifficulty(difficulty);
        player->SendDungeonDifficulty(true);
    }
}

void Group::SetRaidDifficulty(Difficulty difficulty)
{
    m_raidDifficulty = difficulty;
    if (!isBGGroup() && !isBFGroup())
    {
        PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_RAID_DIFFICULTY);

        stmt->setUInt8(0, uint8(m_raidDifficulty));
        stmt->setUInt32(1, GetLowGUID());

        CharacterDatabase.Execute(stmt);
    }

    for (GroupReference* itr = GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        player->SetRaidDifficulty(difficulty);
        player->SendRaidDifficulty(true);
    }
}

void Group::ResetInstances(uint8 method, bool isRaid, Player* leader)
{
    if (isBGGroup() || isBFGroup() || isLFGGroup())
        return;

    switch (method)
    {
        case INSTANCE_RESET_ALL:
            {
                if (leader->GetDifficulty(false) != DUNGEON_DIFFICULTY_NORMAL)
                    break;
                std::vector<InstanceSave*> toUnbind;
                BoundInstancesMap const& m_boundInstances = sInstanceSaveMgr->PlayerGetBoundInstances(leader->GetGUIDLow(), Difficulty(DUNGEON_DIFFICULTY_NORMAL));
                for (BoundInstancesMap::const_iterator itr = m_boundInstances.begin(); itr != m_boundInstances.end(); ++itr)
                {
                    InstanceSave* instanceSave = itr->second.save;
                    const MapEntry* entry = sMapStore.LookupEntry(itr->first);
                    if (!entry || entry->IsRaid() || !instanceSave->CanReset())
                        continue;

                    Map* map = sMapMgr->FindMap(instanceSave->GetMapId(), instanceSave->GetInstanceId());
                    if (!map || map->ToInstanceMap()->Reset(method))
                    {
                        leader->SendResetInstanceSuccess(instanceSave->GetMapId());
                        toUnbind.push_back(instanceSave);
                    }
                    else
                        leader->SendResetInstanceFailed(0, instanceSave->GetMapId());
                }
                for (std::vector<InstanceSave*>::const_iterator itr = toUnbind.begin(); itr != toUnbind.end(); ++itr)
                    sInstanceSaveMgr->UnbindAllFor(*itr);
            }
            break;
        case INSTANCE_RESET_CHANGE_DIFFICULTY:
            {
                std::vector<InstanceSave*> toUnbind;
                BoundInstancesMap const& m_boundInstances = sInstanceSaveMgr->PlayerGetBoundInstances(leader->GetGUIDLow(), leader->GetDifficulty(isRaid));
                for (BoundInstancesMap::const_iterator itr = m_boundInstances.begin(); itr != m_boundInstances.end(); ++itr)
                {
                    InstanceSave* instanceSave = itr->second.save;
                    const MapEntry* entry = sMapStore.LookupEntry(itr->first);
                    if (!entry || entry->IsRaid() != isRaid || !instanceSave->CanReset())
                        continue;

                    Map* map = sMapMgr->FindMap(instanceSave->GetMapId(), instanceSave->GetInstanceId());
                    if (!map || map->ToInstanceMap()->Reset(method))
                    {
                        leader->SendResetInstanceSuccess(instanceSave->GetMapId());
                        toUnbind.push_back(instanceSave);
                    }
                    else
                        leader->SendResetInstanceFailed(0, instanceSave->GetMapId());
                }
                for (std::vector<InstanceSave*>::const_iterator itr = toUnbind.begin(); itr != toUnbind.end(); ++itr)
                    sInstanceSaveMgr->UnbindAllFor(*itr);
            }
            break;
    }
}

void Group::_homebindIfInstance(Player* player)
{
    if (player && !player->IsGameMaster() && player->FindMap() && sMapStore.LookupEntry(player->GetMapId())->IsDungeon())
        player->m_InstanceValid = false;
}

void Group::_cancelHomebindIfInstance(Player* player)
{
    // if player is reinvited to group and in the instance - cancel homebind timer
    if (!player->FindMap() || !player->FindMap()->IsDungeon())
        return;
    InstancePlayerBind* bind = sInstanceSaveMgr->PlayerGetBoundInstance(player->GetGUIDLow(), player->FindMap()->GetId(), player->GetDifficulty(player->FindMap()->IsRaid()));
    if (bind && bind->save->GetInstanceId() == player->GetInstanceId())
        player->m_InstanceValid = true;
}

void Group::BroadcastGroupUpdate(void)
{
    // FG: HACK: force flags update on group leave - for values update hack
    // -- not very efficient but safe
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        Player* pp = ObjectAccessor::FindPlayer(citr->guid);
        if (pp)
        {
            pp->ForceValuesUpdateAtIndex(UNIT_FIELD_BYTES_2);
            pp->ForceValuesUpdateAtIndex(UNIT_FIELD_FACTIONTEMPLATE);
#if defined(ENABLE_EXTRAS) && defined(ENABLE_EXTRA_LOGS)
            sLog->outStaticDebug("-- Forced group value update for '%s'", pp->GetName().c_str());
#endif
        }
    }
}

void Group::ResetMaxEnchantingLevel()
{
    m_maxEnchantingLevel = 0;
    Player* pMember = nullptr;
    for (member_citerator citr = m_memberSlots.begin(); citr != m_memberSlots.end(); ++citr)
    {
        pMember = ObjectAccessor::FindPlayer(citr->guid);
        if (pMember && m_maxEnchantingLevel < pMember->GetSkillValue(SKILL_ENCHANTING))
            m_maxEnchantingLevel = pMember->GetSkillValue(SKILL_ENCHANTING);
    }
}

void Group::SetLootMethod(LootMethod method)
{
    m_lootMethod = method;
}

void Group::SetLooterGuid(uint64 guid)
{
    m_looterGuid = guid;
}

void Group::SetMasterLooterGuid(uint64 guid)
{
    m_masterLooterGuid = guid;
}

void Group::SetLootThreshold(ItemQualities threshold)
{
    m_lootThreshold = threshold;
}

void Group::SetLfgRoles(uint64 guid, const uint8 roles)
{
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    slot->roles = roles;
    SendUpdate();
}

bool Group::IsFull() const
{
    return isRaidGroup() ? (m_memberSlots.size() >= MAXRAIDSIZE) : (m_memberSlots.size() >= MAXGROUPSIZE);
}

bool Group::isLFGGroup() const
{
    return m_groupType & GROUPTYPE_LFG;
}

bool Group::isRaidGroup() const
{
    return m_groupType & GROUPTYPE_RAID;
}

bool Group::isBGGroup() const
{
    return m_bgGroup != nullptr;
}

bool Group::isBFGroup() const
{
    return m_bfGroup != nullptr;
}

bool Group::IsCreated() const
{
    return GetMembersCount() > 0;
}

uint64 Group::GetLeaderGUID() const
{
    return m_leaderGuid;
}

uint64 Group::GetGUID() const
{
    return m_guid;
}

uint32 Group::GetLowGUID() const
{
    return GUID_LOPART(m_guid);
}

const char* Group::GetLeaderName() const
{
    return m_leaderName.c_str();
}

LootMethod Group::GetLootMethod() const
{
    return m_lootMethod;
}

uint64 Group::GetLooterGuid() const
{
    return m_looterGuid;
}

uint64 Group::GetMasterLooterGuid() const
{
    return m_masterLooterGuid;
}

ItemQualities Group::GetLootThreshold() const
{
    return m_lootThreshold;
}

bool Group::IsMember(uint64 guid) const
{
    return _getMemberCSlot(guid) != m_memberSlots.end();
}

bool Group::IsLeader(uint64 guid) const
{
    return (GetLeaderGUID() == guid);
}

uint64 Group::GetMemberGUID(const std::string& name)
{
    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->name == name)
            return itr->guid;
    return 0;
}

bool Group::IsAssistant(uint64 guid) const
{
    member_citerator mslot = _getMemberCSlot(guid);
    if (mslot == m_memberSlots.end())
        return false;
    return mslot->flags & MEMBER_FLAG_ASSISTANT;
}

bool Group::SameSubGroup(uint64 guid1, uint64 guid2) const
{
    member_citerator mslot2 = _getMemberCSlot(guid2);
    if (mslot2 == m_memberSlots.end())
        return false;
    return SameSubGroup(guid1, &*mslot2);
}

bool Group::SameSubGroup(uint64 guid1, MemberSlot const* slot2) const
{
    member_citerator mslot1 = _getMemberCSlot(guid1);
    if (mslot1 == m_memberSlots.end() || !slot2)
        return false;
    return (mslot1->group == slot2->group);
}

bool Group::HasFreeSlotSubGroup(uint8 subgroup) const
{
    return (m_subGroupsCounts && m_subGroupsCounts[subgroup] < MAXGROUPSIZE);
}

uint8 Group::GetMemberGroup(uint64 guid) const
{
    member_citerator mslot = _getMemberCSlot(guid);
    if (mslot == m_memberSlots.end())
        return (MAX_RAID_SUBGROUPS + 1);
    return mslot->group;
}

void Group::SetBattlegroundGroup(Battleground* bg)
{
    m_bgGroup = bg;
}

void Group::SetBattlefieldGroup(Battlefield* bg)
{
    m_bfGroup = bg;
}

void Group::SetGroupMemberFlag(uint64 guid, bool apply, GroupMemberFlags flag)
{
    // Assistants, main assistants and main tanks are only available in raid groups
    if (!isRaidGroup())
        return;

    // Check if player is really in the raid
    member_witerator slot = _getMemberWSlot(guid);
    if (slot == m_memberSlots.end())
        return;

    // Do flag specific actions, e.g ensure uniqueness
    switch (flag)
    {
        case MEMBER_FLAG_MAINASSIST:
            RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINASSIST);         // Remove main assist flag from current if any.
            break;
        case MEMBER_FLAG_MAINTANK:
            RemoveUniqueGroupMemberFlag(MEMBER_FLAG_MAINTANK);           // Remove main tank flag from current if any.
            break;
        case MEMBER_FLAG_ASSISTANT:
            break;
        default:
            return;                                                      // This should never happen
    }

    // Switch the actual flag
    ToggleGroupMemberFlag(slot, flag, apply);

    // Preserve the new setting in the db
    PreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_GROUP_MEMBER_FLAG);

    stmt->setUInt8(0, slot->flags);
    stmt->setUInt32(1, GUID_LOPART(guid));

    CharacterDatabase.Execute(stmt);

    // Broadcast the changes to the group
    SendUpdate();
}

Difficulty Group::GetDifficulty(bool isRaid) const
{
    return isRaid ? m_raidDifficulty : m_dungeonDifficulty;
}

Difficulty Group::GetDungeonDifficulty() const
{
    return m_dungeonDifficulty;
}

Difficulty Group::GetRaidDifficulty() const
{
    return m_raidDifficulty;
}

bool Group::isRollLootActive() const
{
    return !RollId.empty();
}

Group::Rolls::iterator Group::GetRoll(uint64 Guid)
{
    Rolls::iterator iter;
    for (iter = RollId.begin(); iter != RollId.end(); ++iter)
        if ((*iter)->itemGUID == Guid && (*iter)->isValid())
            return iter;
    return RollId.end();
}

void Group::LinkMember(GroupReference* pRef)
{
    m_memberMgr.insertFirst(pRef);
}

void Group::_initRaidSubGroupsCounter()
{
    // Sub group counters initialization
    if (!m_subGroupsCounts)
        m_subGroupsCounts = new uint8[MAX_RAID_SUBGROUPS];

    memset((void*)m_subGroupsCounts, 0, (MAX_RAID_SUBGROUPS)*sizeof(uint8));

    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        ++m_subGroupsCounts[itr->group];
}

Group::member_citerator Group::_getMemberCSlot(uint64 Guid) const
{
    for (member_citerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->guid == Guid)
            return itr;
    return m_memberSlots.end();
}

Group::member_witerator Group::_getMemberWSlot(uint64 Guid)
{
    for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->guid == Guid)
            return itr;
    return m_memberSlots.end();
}

void Group::SubGroupCounterIncrease(uint8 subgroup)
{
    if (m_subGroupsCounts)
        ++m_subGroupsCounts[subgroup];
}

void Group::SubGroupCounterDecrease(uint8 subgroup)
{
    if (m_subGroupsCounts)
        --m_subGroupsCounts[subgroup];
}

void Group::RemoveUniqueGroupMemberFlag(GroupMemberFlags flag)
{
    for (member_witerator itr = m_memberSlots.begin(); itr != m_memberSlots.end(); ++itr)
        if (itr->flags & flag)
            itr->flags &= ~flag;
}

void Group::ToggleGroupMemberFlag(member_witerator slot, uint8 flag, bool apply)
{
    if (apply)
        slot->flags |= flag;
    else
        slot->flags &= ~flag;
}
