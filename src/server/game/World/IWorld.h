/*
 * Copyright (C) 2016+     AzerothCore <www.azerothcore.org>, released under GNU AGPL v3 license: https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3
 */

#ifndef AZEROTHCORE_IWORLD_H
#define AZEROTHCORE_IWORLD_H

#include "Callback.h"
#include "Common.h"
#include "QueryResult.h"
#include "SharedDefines.h"
#include "Timer.h"
#include <atomic>
#include <list>
#include <map>
#include <set>

class WorldSession;
class Player;

/// Storage class for commands issued for delayed execution
struct CliCommandHolder
{
    typedef void Print(void*, const char*);
    typedef void CommandFinished(void*, bool success);

    void* m_callbackArg;
    char* m_command;
    Print* m_print;

    CommandFinished* m_commandFinished;

    CliCommandHolder(void* callbackArg, const char* command, Print* zprint, CommandFinished* commandFinished)
            : m_callbackArg(callbackArg), m_print(zprint), m_commandFinished(commandFinished)
    {
        // TODO: fix Codacy warning
        //  "Does not handle strings that are not \0-terminated; if given one it may perform an over-read (it could cause a crash if unprotected) (CWE-126)."
        size_t len = strlen(command) + 1;
        m_command = new char[len];
        memcpy(m_command, command, len);
    }

    ~CliCommandHolder() { delete[] m_command; }
};

typedef std::unordered_map<uint32, WorldSession*> SessionMap;

// ServerMessages.dbc
enum ServerMessageType
{
    SERVER_MSG_SHUTDOWN_TIME      = 1,
    SERVER_MSG_RESTART_TIME       = 2,
    SERVER_MSG_STRING             = 3,
    SERVER_MSG_SHUTDOWN_CANCELLED = 4,
    SERVER_MSG_RESTART_CANCELLED  = 5
};

/// Configuration elements
enum WorldBoolConfigs
{
    CONFIG_DURABILITY_LOSS_IN_PVP = 0,
    CONFIG_ADDON_CHANNEL,
    CONFIG_ALLOW_PLAYER_COMMANDS,
    CONFIG_CLEAN_CHARACTER_DB,
    CONFIG_STATS_SAVE_ONLY_ON_LOGOUT,
    CONFIG_ALLOW_TWO_SIDE_ACCOUNTS,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_CALENDAR,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHAT,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_CHANNEL,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_GROUP,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_GUILD,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_MAIL,
    CONFIG_ALLOW_TWO_SIDE_WHO_LIST,
    CONFIG_ALLOW_TWO_SIDE_ADD_FRIEND,
    CONFIG_ALLOW_TWO_SIDE_TRADE,
    CONFIG_ALL_TAXI_PATHS,
    CONFIG_INSTANCE_IGNORE_LEVEL,
    CONFIG_INSTANCE_IGNORE_RAID,
    CONFIG_INSTANCE_GMSUMMON_PLAYER,
    CONFIG_INSTANCE_SHARED_ID,
    CONFIG_GM_LOG_TRADE,
    CONFIG_ALLOW_GM_GROUP,
    CONFIG_ALLOW_GM_FRIEND,
    CONFIG_GM_LOWER_SECURITY,
    CONFIG_SKILL_PROSPECTING,
    CONFIG_SKILL_MILLING,
    CONFIG_SAVE_RESPAWN_TIME_IMMEDIATELY,
    CONFIG_WEATHER,
    CONFIG_ALWAYS_MAX_SKILL_FOR_LEVEL,
    CONFIG_QUEST_IGNORE_RAID,
    CONFIG_DETECT_POS_COLLISION,
    CONFIG_RESTRICTED_LFG_CHANNEL,
    CONFIG_SILENTLY_GM_JOIN_TO_CHANNEL,
    CONFIG_TALENTS_INSPECTING,
    CONFIG_CHAT_FAKE_MESSAGE_PREVENTING,
    CONFIG_CHAT_MUTE_FIRST_LOGIN,
    CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVP,
    CONFIG_DEATH_CORPSE_RECLAIM_DELAY_PVE,
    CONFIG_DEATH_BONES_WORLD,
    CONFIG_DEATH_BONES_BG_OR_ARENA,
    CONFIG_DIE_COMMAND_MODE,
    CONFIG_DECLINED_NAMES_USED,
    CONFIG_BATTLEGROUND_DISABLE_QUEST_SHARE_IN_BG,
    CONFIG_BATTLEGROUND_DISABLE_READY_CHECK_IN_BG,
    CONFIG_BATTLEGROUND_CAST_DESERTER,
    CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_ENABLE,
    CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_LIMITED_ENABLE,
    CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_PLAYERONLY,
    CONFIG_BATTLEGROUND_STORE_STATISTICS_ENABLE,
    CONFIG_BATTLEGROUND_TRACK_DESERTERS,
    CONFIG_BG_XP_FOR_KILL,
    CONFIG_ARENA_AUTO_DISTRIBUTE_POINTS,
    CONFIG_ARENA_SEASON_IN_PROGRESS,
    CONFIG_ARENA_QUEUE_ANNOUNCER_ENABLE,
    CONFIG_OFFHAND_CHECK_AT_SPELL_UNLEARN,
    CONFIG_VMAP_INDOOR_CHECK,
    CONFIG_PET_LOS,
    CONFIG_START_ALL_SPELLS,
    CONFIG_START_ALL_EXPLORED,
    CONFIG_START_ALL_REP,
    CONFIG_ALWAYS_MAXSKILL,
    CONFIG_PVP_TOKEN_ENABLE,
    CONFIG_NO_RESET_TALENT_COST,
    CONFIG_SHOW_KICK_IN_WORLD,
    CONFIG_SHOW_MUTE_IN_WORLD,
    CONFIG_SHOW_BAN_IN_WORLD,
    CONFIG_CHATLOG_CHANNEL,
    CONFIG_CHATLOG_WHISPER,
    CONFIG_CHATLOG_SYSCHAN,
    CONFIG_CHATLOG_PARTY,
    CONFIG_CHATLOG_RAID,
    CONFIG_CHATLOG_GUILD,
    CONFIG_CHATLOG_PUBLIC,
    CONFIG_CHATLOG_ADDON,
    CONFIG_CHATLOG_BGROUND,
    CONFIG_AUTOBROADCAST,
    CONFIG_ALLOW_TICKETS,
    CONFIG_DELETE_CHARACTER_TICKET_TRACE,
    CONFIG_PRESERVE_CUSTOM_CHANNELS,
    CONFIG_WINTERGRASP_ENABLE,
    CONFIG_PDUMP_NO_PATHS,
    CONFIG_PDUMP_NO_OVERWRITE,
    CONFIG_ENABLE_MMAPS, // pussywizard
    CONFIG_ENABLE_LOGIN_AFTER_DC, // pussywizard
    CONFIG_DONT_CACHE_RANDOM_MOVEMENT_PATHS, // pussywizard
    CONFIG_QUEST_IGNORE_AUTO_ACCEPT,
    CONFIG_QUEST_IGNORE_AUTO_COMPLETE,
    CONFIG_QUEST_ENABLE_QUEST_TRACKER,
    CONFIG_WARDEN_ENABLED,
    CONFIG_ENABLE_CONTINENT_TRANSPORT,
    CONFIG_ENABLE_CONTINENT_TRANSPORT_PRELOADING,
    CONFIG_MINIGOB_MANABONK,
    CONFIG_IP_BASED_ACTION_LOGGING,
    CONFIG_CALCULATE_CREATURE_ZONE_AREA_DATA,
    CONFIG_CALCULATE_GAMEOBJECT_ZONE_AREA_DATA,
    CONFIG_CHECK_GOBJECT_LOS,
    CONFIG_CLOSE_IDLE_CONNECTIONS,
    CONFIG_LFG_LOCATION_ALL, // Player can join LFG anywhere
    CONFIG_PRELOAD_ALL_NON_INSTANCED_MAP_GRIDS,
    CONFIG_ALLOW_TWO_SIDE_INTERACTION_EMOTE,
    CONFIG_ITEMDELETE_METHOD,
    CONFIG_ITEMDELETE_VENDOR,
    CONFIG_SET_ALL_CREATURES_WITH_WAYPOINT_MOVEMENT_ACTIVE,
    CONFIG_DEBUG_BATTLEGROUND,
    CONFIG_DEBUG_ARENA,
    CONFIG_DUNGEON_ACCESS_REQUIREMENTS_PORTAL_CHECK_ILVL,
    CONFIG_DUNGEON_ACCESS_REQUIREMENTS_LFG_DBC_LEVEL_OVERRIDE,
    CONFIG_REGEN_HP_CANNOT_REACH_TARGET_IN_RAID,
    CONFIG_SET_BOP_ITEM_TRADEABLE,
    BOOL_CONFIG_VALUE_COUNT
};

enum WorldFloatConfigs
{
    CONFIG_GROUP_XP_DISTANCE = 0,
    CONFIG_MAX_RECRUIT_A_FRIEND_DISTANCE,
    CONFIG_SIGHT_MONSTER,
    CONFIG_LISTEN_RANGE_SAY,
    CONFIG_LISTEN_RANGE_TEXTEMOTE,
    CONFIG_LISTEN_RANGE_YELL,
    CONFIG_CREATURE_FAMILY_FLEE_ASSISTANCE_RADIUS,
    CONFIG_CREATURE_FAMILY_ASSISTANCE_RADIUS,
    CONFIG_CHANCE_OF_GM_SURVEY,
    CONFIG_ARENA_WIN_RATING_MODIFIER_1,
    CONFIG_ARENA_WIN_RATING_MODIFIER_2,
    CONFIG_ARENA_LOSE_RATING_MODIFIER,
    CONFIG_ARENA_MATCHMAKER_RATING_MODIFIER,
    FLOAT_CONFIG_VALUE_COUNT
};

enum WorldIntConfigs
{
    CONFIG_COMPRESSION = 0,
    CONFIG_INTERVAL_MAPUPDATE,
    CONFIG_INTERVAL_CHANGEWEATHER,
    CONFIG_INTERVAL_DISCONNECT_TOLERANCE,
    CONFIG_INTERVAL_SAVE,
    CONFIG_PORT_WORLD,
    CONFIG_SOCKET_TIMEOUTTIME,
    CONFIG_SESSION_ADD_DELAY,
    CONFIG_GAME_TYPE,
    CONFIG_REALM_ZONE,
    CONFIG_STRICT_PLAYER_NAMES,
    CONFIG_STRICT_CHARTER_NAMES,
    CONFIG_STRICT_CHANNEL_NAMES,
    CONFIG_STRICT_PET_NAMES,
    CONFIG_MIN_PLAYER_NAME,
    CONFIG_MIN_CHARTER_NAME,
    CONFIG_MIN_PET_NAME,
    CONFIG_CHARACTER_CREATING_DISABLED,
    CONFIG_CHARACTER_CREATING_DISABLED_RACEMASK,
    CONFIG_CHARACTER_CREATING_DISABLED_CLASSMASK,
    CONFIG_CHARACTERS_PER_ACCOUNT,
    CONFIG_CHARACTERS_PER_REALM,
    CONFIG_HEROIC_CHARACTERS_PER_REALM,
    CONFIG_CHARACTER_CREATING_MIN_LEVEL_FOR_HEROIC_CHARACTER,
    CONFIG_SKIP_CINEMATICS,
    CONFIG_MAX_PLAYER_LEVEL,
    CONFIG_MIN_DUALSPEC_LEVEL,
    CONFIG_START_PLAYER_LEVEL,
    CONFIG_START_HEROIC_PLAYER_LEVEL,
    CONFIG_START_PLAYER_MONEY,
    CONFIG_MAX_HONOR_POINTS,
    CONFIG_START_HONOR_POINTS,
    CONFIG_MAX_ARENA_POINTS,
    CONFIG_START_ARENA_POINTS,
    CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL,
    CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL_DIFFERENCE,
    CONFIG_INSTANCE_RESET_TIME_HOUR,
    CONFIG_INSTANCE_RESET_TIME_RELATIVE_TIMESTAMP,
    CONFIG_INSTANCE_UNLOAD_DELAY,
    CONFIG_MAX_PRIMARY_TRADE_SKILL,
    CONFIG_MIN_PETITION_SIGNS,
    CONFIG_GM_LOGIN_STATE,
    CONFIG_GM_VISIBLE_STATE,
    CONFIG_GM_ACCEPT_TICKETS,
    CONFIG_GM_CHAT,
    CONFIG_GM_WHISPERING_TO,
    CONFIG_GM_LEVEL_IN_GM_LIST,
    CONFIG_GM_LEVEL_IN_WHO_LIST,
    CONFIG_START_GM_LEVEL,
    CONFIG_GROUP_VISIBILITY,
    CONFIG_MAIL_DELIVERY_DELAY,
    CONFIG_UPTIME_UPDATE,
    CONFIG_SKILL_CHANCE_ORANGE,
    CONFIG_SKILL_CHANCE_YELLOW,
    CONFIG_SKILL_CHANCE_GREEN,
    CONFIG_SKILL_CHANCE_GREY,
    CONFIG_SKILL_CHANCE_MINING_STEPS,
    CONFIG_SKILL_CHANCE_SKINNING_STEPS,
    CONFIG_SKILL_GAIN_CRAFTING,
    CONFIG_SKILL_GAIN_DEFENSE,
    CONFIG_SKILL_GAIN_GATHERING,
    CONFIG_SKILL_GAIN_WEAPON,
    CONFIG_MAX_OVERSPEED_PINGS,
    CONFIG_EXPANSION,
    CONFIG_CHATFLOOD_MESSAGE_COUNT,
    CONFIG_CHATFLOOD_MESSAGE_DELAY,
    CONFIG_CHATFLOOD_MUTE_TIME,
    CONFIG_EVENT_ANNOUNCE,
    CONFIG_CREATURE_FAMILY_ASSISTANCE_DELAY,
    CONFIG_CREATURE_FAMILY_FLEE_DELAY,
    CONFIG_WORLD_BOSS_LEVEL_DIFF,
    CONFIG_QUEST_LOW_LEVEL_HIDE_DIFF,
    CONFIG_QUEST_HIGH_LEVEL_HIDE_DIFF,
    CONFIG_CHAT_STRICT_LINK_CHECKING_SEVERITY,
    CONFIG_CHAT_STRICT_LINK_CHECKING_KICK,
    CONFIG_CHAT_CHANNEL_LEVEL_REQ,
    CONFIG_CHAT_WHISPER_LEVEL_REQ,
    CONFIG_CHAT_SAY_LEVEL_REQ,
    CONFIG_PARTY_LEVEL_REQ,
    CONFIG_CHAT_TIME_MUTE_FIRST_LOGIN,
    CONFIG_TRADE_LEVEL_REQ,
    CONFIG_TICKET_LEVEL_REQ,
    CONFIG_AUCTION_LEVEL_REQ,
    CONFIG_MAIL_LEVEL_REQ,
    CONFIG_CORPSE_DECAY_NORMAL,
    CONFIG_CORPSE_DECAY_RARE,
    CONFIG_CORPSE_DECAY_ELITE,
    CONFIG_CORPSE_DECAY_RAREELITE,
    CONFIG_CORPSE_DECAY_WORLDBOSS,
    CONFIG_DEATH_SICKNESS_LEVEL,
    CONFIG_INSTANT_LOGOUT,
    CONFIG_DISABLE_BREATHING,
    CONFIG_BATTLEGROUND_QUEUE_ANNOUNCER_SPAM_DELAY,
    CONFIG_BATTLEGROUND_PREMATURE_FINISH_TIMER,
    CONFIG_BATTLEGROUND_PREMADE_GROUP_WAIT_FOR_MATCH,
    CONFIG_BATTLEGROUND_REPORT_AFK_TIMER,
    CONFIG_BATTLEGROUND_REPORT_AFK,
    CONFIG_BATTLEGROUND_INVITATION_TYPE,
    CONFIG_BATTLEGROUND_PLAYER_RESPAWN,
    CONFIG_BATTLEGROUND_BUFF_RESPAWN,
    CONFIG_ARENA_MAX_RATING_DIFFERENCE,
    CONFIG_ARENA_RATING_DISCARD_TIMER,
    CONFIG_ARENA_AUTO_DISTRIBUTE_INTERVAL_DAYS,
    CONFIG_ARENA_GAMES_REQUIRED,
    CONFIG_ARENA_SEASON_ID,
    CONFIG_ARENA_START_RATING,
    CONFIG_ARENA_START_PERSONAL_RATING,
    CONFIG_ARENA_START_MATCHMAKER_RATING,
    CONFIG_HONOR_AFTER_DUEL,
    CONFIG_PVP_TOKEN_MAP_TYPE,
    CONFIG_PVP_TOKEN_ID,
    CONFIG_PVP_TOKEN_COUNT,
    CONFIG_INTERVAL_LOG_UPDATE,
    CONFIG_MIN_LOG_UPDATE,
    CONFIG_ENABLE_SINFO_LOGIN,
    CONFIG_PLAYER_ALLOW_COMMANDS,
    CONFIG_NUMTHREADS,
    CONFIG_LOGDB_CLEARINTERVAL,
    CONFIG_LOGDB_CLEARTIME,
    CONFIG_TELEPORT_TIMEOUT_NEAR, // pussywizard
    CONFIG_TELEPORT_TIMEOUT_FAR, // pussywizard
    CONFIG_MAX_ALLOWED_MMR_DROP, // pussywizard
    CONFIG_CLIENTCACHE_VERSION,
    CONFIG_GUILD_EVENT_LOG_COUNT,
    CONFIG_GUILD_BANK_EVENT_LOG_COUNT,
    CONFIG_MIN_LEVEL_STAT_SAVE,
    CONFIG_RANDOM_BG_RESET_HOUR,
    CONFIG_CALENDAR_DELETE_OLD_EVENTS_HOUR,
    CONFIG_GUILD_RESET_HOUR,
    CONFIG_CHARDELETE_KEEP_DAYS,
    CONFIG_CHARDELETE_METHOD,
    CONFIG_CHARDELETE_MIN_LEVEL,
    CONFIG_AUTOBROADCAST_CENTER,
    CONFIG_AUTOBROADCAST_INTERVAL,
    CONFIG_MAX_RESULTS_LOOKUP_COMMANDS,
    CONFIG_DB_PING_INTERVAL,
    CONFIG_PRESERVE_CUSTOM_CHANNEL_DURATION,
    CONFIG_PERSISTENT_CHARACTER_CLEAN_FLAGS,
    CONFIG_LFG_OPTIONSMASK,
    CONFIG_MAX_INSTANCES_PER_HOUR,
    CONFIG_WINTERGRASP_PLR_MAX,
    CONFIG_WINTERGRASP_PLR_MIN,
    CONFIG_WINTERGRASP_PLR_MIN_LVL,
    CONFIG_WINTERGRASP_BATTLETIME,
    CONFIG_WINTERGRASP_NOBATTLETIME,
    CONFIG_WINTERGRASP_RESTART_AFTER_CRASH,
    CONFIG_PACKET_SPOOF_POLICY,
    CONFIG_PACKET_SPOOF_BANMODE,
    CONFIG_PACKET_SPOOF_BANDURATION,
    CONFIG_WARDEN_CLIENT_RESPONSE_DELAY,
    CONFIG_WARDEN_CLIENT_CHECK_HOLDOFF,
    CONFIG_WARDEN_CLIENT_FAIL_ACTION,
    CONFIG_WARDEN_CLIENT_BAN_DURATION,
    CONFIG_WARDEN_NUM_MEM_CHECKS,
    CONFIG_WARDEN_NUM_LUA_CHECKS,
    CONFIG_WARDEN_NUM_OTHER_CHECKS,
    CONFIG_BIRTHDAY_TIME,
    CONFIG_SOCKET_TIMEOUTTIME_ACTIVE,
    CONFIG_INSTANT_TAXI,
    CONFIG_AFK_PREVENT_LOGOUT,
    CONFIG_ICC_BUFF_HORDE,
    CONFIG_ICC_BUFF_ALLIANCE,
    CONFIG_ITEMDELETE_QUALITY,
    CONFIG_ITEMDELETE_ITEM_LEVEL,
    CONFIG_BG_REWARD_WINNER_HONOR_FIRST,
    CONFIG_BG_REWARD_WINNER_ARENA_FIRST,
    CONFIG_BG_REWARD_WINNER_HONOR_LAST,
    CONFIG_BG_REWARD_WINNER_ARENA_LAST,
    CONFIG_BG_REWARD_LOSER_HONOR_FIRST,
    CONFIG_BG_REWARD_LOSER_HONOR_LAST,
    CONFIG_CHARTER_COST_GUILD,
    CONFIG_CHARTER_COST_ARENA_2v2,
    CONFIG_CHARTER_COST_ARENA_3v3,
    CONFIG_CHARTER_COST_ARENA_5v5,
    CONFIG_MAX_WHO_LIST_RETURN,
    CONFIG_WAYPOINT_MOVEMENT_STOP_TIME_FOR_PLAYER,
    CONFIG_DUNGEON_ACCESS_REQUIREMENTS_PRINT_MODE,
    CONFIG_DUNGEON_ACCESS_REQUIREMENTS_OPTIONAL_STRING_ID,
    CONFIG_GUILD_BANK_INITIAL_TABS,
    CONFIG_GUILD_BANK_TAB_COST_0,
    CONFIG_GUILD_BANK_TAB_COST_1,
    CONFIG_GUILD_BANK_TAB_COST_2,
    CONFIG_GUILD_BANK_TAB_COST_3,
    CONFIG_GUILD_BANK_TAB_COST_4,
    CONFIG_GUILD_BANK_TAB_COST_5,
    CONFIG_GM_LEVEL_CHANNEL_MODERATION,
    CONFIG_TOGGLE_XP_COST,
    CONFIG_NPC_EVADE_IF_NOT_REACHABLE,
    CONFIG_NPC_REGEN_TIME_IF_NOT_REACHABLE_IN_RAID,
    CONFIG_FFA_PVP_TIMER,
    INT_CONFIG_VALUE_COUNT
};

/// Server rates
enum Rates
{
    RATE_HEALTH = 0,
    RATE_POWER_MANA,
    RATE_POWER_RAGE_INCOME,
    RATE_POWER_RAGE_LOSS,
    RATE_POWER_RUNICPOWER_INCOME,
    RATE_POWER_RUNICPOWER_LOSS,
    RATE_POWER_FOCUS,
    RATE_POWER_ENERGY,
    RATE_SKILL_DISCOVERY,
    RATE_DROP_ITEM_POOR,
    RATE_DROP_ITEM_NORMAL,
    RATE_DROP_ITEM_UNCOMMON,
    RATE_DROP_ITEM_RARE,
    RATE_DROP_ITEM_EPIC,
    RATE_DROP_ITEM_LEGENDARY,
    RATE_DROP_ITEM_ARTIFACT,
    RATE_DROP_ITEM_REFERENCED,

    RATE_DROP_ITEM_REFERENCED_AMOUNT,
    RATE_SELLVALUE_ITEM_POOR,
    RATE_SELLVALUE_ITEM_NORMAL,
    RATE_SELLVALUE_ITEM_UNCOMMON,
    RATE_SELLVALUE_ITEM_RARE,
    RATE_SELLVALUE_ITEM_EPIC,
    RATE_SELLVALUE_ITEM_LEGENDARY,
    RATE_SELLVALUE_ITEM_ARTIFACT,
    RATE_SELLVALUE_ITEM_HEIRLOOM,
    RATE_BUYVALUE_ITEM_POOR,
    RATE_BUYVALUE_ITEM_NORMAL,
    RATE_BUYVALUE_ITEM_UNCOMMON,
    RATE_BUYVALUE_ITEM_RARE,
    RATE_BUYVALUE_ITEM_EPIC,
    RATE_BUYVALUE_ITEM_LEGENDARY,
    RATE_BUYVALUE_ITEM_ARTIFACT,
    RATE_BUYVALUE_ITEM_HEIRLOOM,
    RATE_DROP_MONEY,
    RATE_REWARD_BONUS_MONEY,
    RATE_XP_KILL,
    RATE_XP_BG_KILL,
    RATE_XP_QUEST,
    RATE_XP_EXPLORE,
    RATE_XP_PET,
    RATE_XP_PET_NEXT_LEVEL,
    RATE_REPAIRCOST,
    RATE_REPUTATION_GAIN,
    RATE_REPUTATION_LOWLEVEL_KILL,
    RATE_REPUTATION_LOWLEVEL_QUEST,
    RATE_REPUTATION_RECRUIT_A_FRIEND_BONUS,
    RATE_CREATURE_NORMAL_HP,
    RATE_CREATURE_ELITE_ELITE_HP,
    RATE_CREATURE_ELITE_RAREELITE_HP,
    RATE_CREATURE_ELITE_WORLDBOSS_HP,
    RATE_CREATURE_ELITE_RARE_HP,
    RATE_CREATURE_NORMAL_DAMAGE,
    RATE_CREATURE_ELITE_ELITE_DAMAGE,
    RATE_CREATURE_ELITE_RAREELITE_DAMAGE,
    RATE_CREATURE_ELITE_WORLDBOSS_DAMAGE,
    RATE_CREATURE_ELITE_RARE_DAMAGE,
    RATE_CREATURE_NORMAL_SPELLDAMAGE,
    RATE_CREATURE_ELITE_ELITE_SPELLDAMAGE,
    RATE_CREATURE_ELITE_RAREELITE_SPELLDAMAGE,
    RATE_CREATURE_ELITE_WORLDBOSS_SPELLDAMAGE,
    RATE_CREATURE_ELITE_RARE_SPELLDAMAGE,
    RATE_CREATURE_AGGRO,
    RATE_REST_INGAME,
    RATE_REST_OFFLINE_IN_TAVERN_OR_CITY,
    RATE_REST_OFFLINE_IN_WILDERNESS,
    RATE_DAMAGE_FALL,
    RATE_AUCTION_TIME,
    RATE_AUCTION_DEPOSIT,
    RATE_AUCTION_CUT,
    RATE_HONOR,
    RATE_ARENA_POINTS,
    RATE_TALENT,
    RATE_CORPSE_DECAY_LOOTED,
    RATE_INSTANCE_RESET_TIME,
    RATE_TARGET_POS_RECALCULATION_RANGE,
    RATE_DURABILITY_LOSS_ON_DEATH,
    RATE_DURABILITY_LOSS_DAMAGE,
    RATE_DURABILITY_LOSS_PARRY,
    RATE_DURABILITY_LOSS_ABSORB,
    RATE_DURABILITY_LOSS_BLOCK,
    RATE_MOVESPEED,
    MAX_RATES
};

// xinef: global storage
struct GlobalPlayerData
{
    uint32 guidLow;
    uint32 accountId;
    std::string name;
    uint8 race;
    uint8 playerClass;
    uint8 gender;
    uint8 level;
    uint16 mailCount;
    uint32 guildId;
    uint32 groupId;
    std::map<uint8, uint32> arenaTeamId;
};

class IWorld
{
public:
    virtual ~IWorld() {}
    virtual WorldSession* FindSession(uint32 id) const = 0;
    virtual WorldSession* FindOfflineSession(uint32 id) const = 0;
    virtual WorldSession* FindOfflineSessionForCharacterGUID(uint32 guidLow) const = 0;
    virtual void AddSession(WorldSession* s) = 0;
    virtual void SendAutoBroadcast() = 0;
    virtual bool KickSession(uint32 id) = 0;
    virtual void UpdateMaxSessionCounters() = 0;
    virtual const SessionMap& GetAllSessions() const = 0;
    virtual uint32 GetActiveAndQueuedSessionCount() const = 0;
    virtual uint32 GetActiveSessionCount() const = 0;
    virtual uint32 GetQueuedSessionCount() const = 0;
    virtual uint32 GetMaxQueuedSessionCount() const = 0;
    virtual uint32 GetMaxActiveSessionCount() const = 0;
    virtual uint32 GetPlayerCount() const = 0;
    virtual uint32 GetMaxPlayerCount() const = 0;
    virtual void IncreasePlayerCount() = 0;
    virtual void DecreasePlayerCount() = 0;
    virtual Player* FindPlayerInZone(uint32 zone) = 0;
    virtual bool IsClosed() const = 0;
    virtual void SetClosed(bool val) = 0;
    virtual AccountTypes GetPlayerSecurityLimit() const = 0;
    virtual void SetPlayerSecurityLimit(AccountTypes sec) = 0;
    virtual void LoadDBAllowedSecurityLevel() = 0;
    virtual void SetPlayerAmountLimit(uint32 limit) = 0;
    virtual uint32 GetPlayerAmountLimit() const = 0;
    virtual void AddQueuedPlayer(WorldSession*) = 0;
    virtual bool RemoveQueuedPlayer(WorldSession* session) = 0;
    virtual int32 GetQueuePos(WorldSession*) = 0;
    virtual bool HasRecentlyDisconnected(WorldSession*) = 0;
    virtual bool getAllowMovement() const = 0;
    virtual void SetAllowMovement(bool allow) = 0;
    virtual void SetNewCharString(std::string const& str) = 0;
    virtual std::string const& GetNewCharString() const = 0;
    virtual LocaleConstant GetDefaultDbcLocale() const = 0;
    virtual std::string const& GetDataPath() const = 0;
    virtual time_t const& GetStartTime() const = 0;
    virtual time_t const& GetGameTime() const = 0;
    virtual uint32 GetUptime() const = 0;
    virtual uint32 GetUpdateTime() const = 0;
    virtual void SetRecordDiffInterval(int32 t)  = 0;
    virtual time_t GetNextDailyQuestsResetTime() const = 0;
    virtual time_t GetNextWeeklyQuestsResetTime() const = 0;
    virtual time_t GetNextRandomBGResetTime() const = 0;
    virtual uint16 GetConfigMaxSkillValue() const = 0;
    virtual void SetInitialWorldSettings() = 0;
    virtual void LoadConfigSettings(bool reload = false) = 0;
    virtual void SendWorldText(uint32 string_id, ...) = 0;
    virtual void SendGlobalText(const char* text, WorldSession* self) = 0;
    virtual void SendGMText(uint32 string_id, ...) = 0;
    virtual void SendGlobalMessage(WorldPacket* packet, WorldSession* self = nullptr, TeamId teamId = TEAM_NEUTRAL) = 0;
    virtual void SendGlobalGMMessage(WorldPacket* packet, WorldSession* self = nullptr, TeamId teamId = TEAM_NEUTRAL) = 0;
    virtual bool SendZoneMessage(uint32 zone, WorldPacket* packet, WorldSession* self = nullptr, TeamId teamId = TEAM_NEUTRAL) = 0;
    virtual void SendZoneText(uint32 zone, const char* text, WorldSession* self = nullptr, TeamId teamId = TEAM_NEUTRAL) = 0;
    virtual void SendServerMessage(ServerMessageType type, const char* text = "", Player* player = nullptr) = 0;
    virtual bool IsShuttingDown() const = 0;
    virtual uint32 GetShutDownTimeLeft() const = 0;
    virtual void ShutdownServ(uint32 time, uint32 options, uint8 exitcode) = 0;
    virtual void ShutdownCancel() = 0;
    virtual void ShutdownMsg(bool show = false, Player* player = nullptr) = 0;
    virtual void Update(uint32 diff) = 0;
    virtual void UpdateSessions(uint32 diff) = 0;
    virtual void setRate(Rates rate, float value) = 0;
    virtual float getRate(Rates rate) const = 0;
    virtual void setBoolConfig(WorldBoolConfigs index, bool value) = 0;
    virtual bool getBoolConfig(WorldBoolConfigs index) const = 0;
    virtual void setFloatConfig(WorldFloatConfigs index, float value) = 0;
    virtual float getFloatConfig(WorldFloatConfigs index) const = 0;
    virtual void setIntConfig(WorldIntConfigs index, uint32 value) = 0;
    virtual uint32 getIntConfig(WorldIntConfigs index) const = 0;
    virtual void setWorldState(uint32 index, uint64 value) = 0;
    virtual uint64 getWorldState(uint32 index) const = 0;
    virtual void LoadWorldStates() = 0;
    virtual bool IsPvPRealm() const = 0;
    virtual bool IsFFAPvPRealm() const = 0;
    virtual void KickAll() = 0;
    virtual void KickAllLess(AccountTypes sec) = 0;
    virtual uint32 GetNextWhoListUpdateDelaySecs() = 0;
    virtual void LoadGlobalPlayerDataStore() = 0;
    virtual uint32 GetGlobalPlayerGUID(std::string const& name) const = 0;
    virtual GlobalPlayerData const* GetGlobalPlayerData(uint32 guid) const = 0;
    virtual void AddGlobalPlayerData(uint32 guid, uint32 accountId, std::string const& name, uint8 gender, uint8 race, uint8 playerClass, uint8 level, uint16 mailCount, uint32 guildId) = 0;
    virtual void UpdateGlobalPlayerData(uint32 guid, uint8 mask, std::string const& name, uint8 level = 0, uint8 gender = 0, uint8 race = 0, uint8 playerClass = 0) = 0;
    virtual void UpdateGlobalPlayerMails(uint32 guid, int16 count, bool add = true) = 0;
    virtual void UpdateGlobalPlayerGuild(uint32 guid, uint32 guildId) = 0;
    virtual void UpdateGlobalPlayerGroup(uint32 guid, uint32 groupId) = 0;
    virtual void UpdateGlobalPlayerArenaTeam(uint32 guid, uint8 slot, uint32 arenaTeamId) = 0;
    virtual void UpdateGlobalNameData(uint32 guidLow, std::string const& oldName, std::string const& newName) = 0;
    virtual void DeleteGlobalPlayerData(uint32 guid, std::string const& name) = 0;
    virtual void ProcessCliCommands() = 0;
    virtual void QueueCliCommand(CliCommandHolder* commandHolder) = 0;
    virtual void ForceGameEventUpdate() = 0;
    virtual void UpdateRealmCharCount(uint32 accid) = 0;
    virtual LocaleConstant GetAvailableDbcLocale(LocaleConstant locale) const = 0;
    virtual void LoadDBVersion() = 0;
    virtual char const* GetDBVersion() const = 0;
    virtual void LoadAutobroadcasts() = 0;
    virtual void UpdateAreaDependentAuras() = 0;
    virtual uint32 GetCleaningFlags() const = 0;
    virtual void   SetCleaningFlags(uint32 flags) = 0;
    virtual void   ResetEventSeasonalQuests(uint16 event_id) = 0;
    virtual time_t GetNextTimeWithDayAndHour(int8 dayOfWeek, int8 hour) = 0;
    virtual time_t GetNextTimeWithMonthAndHour(int8 month, int8 hour) = 0;
    virtual std::string const& GetRealmName() const = 0;
    virtual void SetRealmName(std::string name) = 0;
};

#endif //AZEROTHCORE_IWORLD_H
