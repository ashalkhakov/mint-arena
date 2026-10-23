/*
===========================================================================
Copyright (C) 1999-2010 id Software LLC, a ZeniMax Media company.

This file is part of Spearmint Source Code.

Spearmint Source Code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 3 of the License,
or (at your option) any later version.

Spearmint Source Code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Spearmint Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/
//
// g_local.h -- local definitions for game module

#include "../idlib/q_shared.h"
#include "../renderercommon/tr_types.h"
#include "bg_public.h"
#include "g_public.h"
#include "ai_ea.h"
#include "g_clip.h"

//==================================================================

#define BODY_QUEUE_SIZE		64

#define	FRAMETIME			100					// msec
#define	CARNAGE_REWARD_TIME	3000
#define REWARD_SPRITE_TIME	2000

#define	INTERMISSION_DELAY_TIME	1000
#define	SP_INTERMISSION_DELAY_TIME	5000

// gentity->flags
#define	FL_GODMODE				0x00000010
#define	FL_NOTARGET				0x00000020
#define	FL_TEAMSLAVE			0x00000400	// not the first on the team
#define FL_NO_KNOCKBACK			0x00000800
#define FL_DROPPED_ITEM			0x00001000
#define FL_NO_BOTS				0x00002000	// spawn point not for bot use
#define FL_NO_HUMANS			0x00004000	// spawn point just for bots
#define FL_FORCE_GESTURE		0x00008000	// force gesture on player

// movers are things like doors, plats, buttons, etc
typedef enum {
	MOVER_POS1,
	MOVER_POS2,
	MOVER_1TO2,
	MOVER_2TO1,

	// Rotating Doors
	ROTATOR_POS1,
	ROTATOR_POS2,
	ROTATOR_1TO2,
	ROTATOR_2TO1
} moverState_t;

#define SP_PODIUM_MODEL		"models/mapobjects/podium/podium4.md3"

//============================================================================

typedef struct gentity_s gentity_t;
typedef struct gplayer_s gplayer_t;

#include "g_script.h"

// a monster's (g_monster.c), Doom 3's idAI's
#define MONSTER_ROUTE			12			// a flyer's route's waypoints
#define MONF_MONSTER			0x0001
#define MONF_FLY				0x0002
#define MONF_ONGROUND			0x0004
#define MONF_PAIN				0x0008		// hurt since its last think
#define MONF_DEAD				0x0010
#define MONF_ENEMY_VISIBLE		0x0020
#define MONF_ENEMY_IN_FOV		0x0040
#define MONF_BLOCKED			0x0080		// its move got nowhere
#define MONF_IMPACT				0x0100		// hit the world hard since its last think
#define MONF_NOGRAVITY			0x0200
#define MONF_NOMOVE				0x0400		// allowMovement( 0 )
#define MONF_LEAPING			0x0800		// thrown (setLinearVelocity up), until it lands: it hurts what it hits
#define MONF_LEAP_HIT			0x1000		// its leap hit someone since its last think
#define MONF_HIT_REACH			0x2000		// its model reaches past its box (hitMins/hitMaxs): shots look for it
#define MONF_PERCHED			0x4000		// landed on a wall, a ceiling or the floor (a flyer): it stays, until it's told to move
#define MONF_NO_UNSTICK			0x8000		// "unstick" 0: left where it's put, in something or not (a cinematic's actor)

typedef enum {
	MOVE_NONE,
	MOVE_TO_ENEMY,
	MOVE_TO_ENTITY,
	MOVE_TO_POSITION,
	MOVE_WANDER
} monsterMove_t;

// Doom 3's moveStatus_t, as scripts know them
typedef enum {
	MOVE_STATUS_DONE,
	MOVE_STATUS_MOVING,
	MOVE_STATUS_WAITING,
	MOVE_STATUS_DEST_NOT_FOUND,
	MOVE_STATUS_DEST_UNREACHABLE,
	MOVE_STATUS_BLOCKED_BY_WALL,
	MOVE_STATUS_BLOCKED_BY_OBJECT,
	MOVE_STATUS_BLOCKED_BY_ENEMY,
	MOVE_STATUS_BLOCKED_BY_MONSTER
} monsterMoveStatus_t;

typedef enum {
	FACE_NONE,			// the ideal yaw (turnTo...)
	FACE_MOVE,			// where it goes
	FACE_ENEMY,
	FACE_ENTITY
} monsterFace_t;

typedef struct {
	int			flags;				// MONF_*
	int			moveCommand;		// monsterMove_t
	int			moveStatus;			// monsterMoveStatus_t
	vec3_t		moveDest;
	int			moveEntity;			// -1 none
	float		moveSpeed;
	int			faceCommand;		// monsterFace_t
	int			faceEntity;			// -1 none
	float		idealYaw;
	float		turnRate;			// degrees a second
	vec3_t		velocity;
	vec3_t		legsFrom;			// where it was the frame before (G_MonsterLegsRate: moved by a script too)
	float		legsRateMax;		// "legs_rate_max"
	int			enemy;				// ENTITYNUM_NONE none
	vec3_t		lastEnemyPos;
	int			lastEnemySeen;
	float		fov;
	float		sightRange;
	int			thread;				// its script's (its states)
	char		state[MAX_QPATH];
	char		idealState[MAX_QPATH];
	int			blockedBy;			// what its move last ran into
	int			painTime;
	float		impactSpeed;		// hitting the world faster hurts it
	float		impactDamage;		// per 100 u/s above that
	float		impactBounce;
	int			impactTime;
	float		pendingImpactDamage;
	float		flySpeed;
	float		flyAccel;
	float		flyOffset;
	float		flyBob;
	float		roll;				// a flyer's banking
	vec3_t		route[MONSTER_ROUTE];	// a flyer's way through the air (g_airnav.c), when it can't fly straight
	int			routeLen;
	int			routeIndex;
	int			routeTime;
	vec3_t		routeGoal;			// what the route goes to
	vec3_t		moveMins, moveMaxs;	// the box it moves with, its entity's
	vec3_t		hitMins, hitMaxs;	// as far as its model reaches (MONF_HIT_REACH), what shots meet
	vec3_t		perchNormal;		// what it's perched on faces this way (MONF_PERCHED)
	vec3_t		perchAngles;		// its belly to it
	vec3_t		flyHitMins, flyHitMaxs;	// its hit box when it isn't perched
	int			fcAnim, fcTime, fcFrame;	// the frame commands run to (G_MonsterFrameCommands)
} monsterState_t;

struct gentity_s {
	entityShared_t	r;				// shared by both the server system and game
	entityState_t	s;				// communicated by server to clients

	// DO NOT MODIFY ANYTHING ABOVE THIS, THE SERVER
	// EXPECTS THE FIELDS IN THAT ORDER!
	//================================

	struct gplayer_s	*player;			// NULL if not a player

	qboolean	inuse;

	char		*classname;			// set in QuakeEd
	int			spawnflags;			// set in QuakeEd

	qboolean	neverFree;			// if true, FreeEntity will only unlink
									// bodyque uses this

	int			flags;				// FL_* variables

	char		*model;
	char		*model2;
	int			freetime;			// level.time when the object was freed
	
	int			eventTime;			// events will be cleared EVENT_VALID_MSEC after set
	qboolean	freeAfterEvent;
	qboolean	unlinkAfterEvent;

	qboolean	physicsObject;		// if true, it can be pushed by movers and fall off edges
									// all game items are physicsObjects, 
	float		physicsBounce;		// 1.0 = continuous bounce, 0.0 = no bounce
	int			clipmask;			// brushes with this content value will be collided against
									// when moving.  items and corpses do not collide against
									// players, for instance

	// movers
	moverState_t moverState;
	int			soundPos1;
	int			sound1to2;
	int			sound2to1;
	int			soundPos2;
	int			soundLoop;
	gentity_t	*parent;
	gentity_t	*nextTrain;
	gentity_t	*prevTrain;
	vec3_t		pos1, pos2;
	vec3_t		trainOffset;		// a func_train without an origin brush: minus its first corner

	char		*message;
	char		*map;				// trigger_changelevel's next map
	char		*killtarget;		// Quake 2's: removed when its targets are used
	float		delay;				// Quake 2's: seconds before its targets are used
	char		*keyname;			// Daikatana's: the item it needs (an inventory item's classname)
	char		*pathtarget;		// Quake 2's: what a path_corner uses when a train reaches it

	int			timestamp;		// body queue sinking, etc

	char		*target;
	char		*targetname;
	char		*team;
	char		*targetShaderName;
	char		*targetShaderNewName;
	gentity_t	*target_ent;

	float		speed;
	vec3_t		movedir;

	int			nextthink;
	void		(*think)(gentity_t *self);
	void		(*reached)(gentity_t *self);	// movers call this when hitting endpoint
	void		(*blocked)(gentity_t *self, gentity_t *other);
	void		(*touch)(gentity_t *self, gentity_t *other, trace_t *trace);
	void		(*use)(gentity_t *self, gentity_t *other, gentity_t *activator);
	void		(*pain)(gentity_t *self, gentity_t *attacker, int damage);
	void		(*die)(gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod);
	qboolean	(*snapshotCallback)(gentity_t *self, gentity_t *player);

	int			pain_debounce_time;
	int			fly_sound_debounce_time;	// wind tunnel

	int			health;

	qboolean	takedamage;

	int			damage;
	int			splashDamage;	// quad will increase this without increasing radius
	int			splashRadius;
	int			methodOfDeath;
	int			splashMethodOfDeath;
	int			missileFlags;	// MISSILE_*, from the projectile def

	int			count;
	float		frobDistance;	// how far a player reaches to use it, "frob_distance" (g_frob.c)
	qboolean	frobable;		// players use it by looking at it, "frobable"
	int			invValue;		// a dropped item's own state, an armor suit's points (g_inventory.c)

	gentity_t	*chain;
	gentity_t	*enemy;
	gentity_t	*activator;
	gentity_t	*teamchain;		// next entity in team
	gentity_t	*teammaster;	// master of the team

#ifdef MISSIONPACK
	int			kamikazeTime;
	int			kamikazeShockTime;
#endif

	int			watertype;
	int			waterlevel;

	int			noise_index;

	// timing variables
	float		wait;
	float		random;
	float		distance;		// rotating doors

	gitem_t		*item;			// for bonus items

	// dlights
	vec3_t		dl_color;
	char		*dl_stylestring;
	char		*dl_shader;
	int			dl_atten;

	// info for bots
	qboolean	botvalid;
	float		update_time;
	float		ltime;
	vec3_t		visorigin;
	vec3_t		lastvisorigin;
	vec3_t		lastAngles;
	vec3_t		lastMins;
	vec3_t		lastMaxs;
	int			areanum;

    // physics
    clipModel_t     clipModel;
	clipModel_t		*partClipModels;	// a compound prop's pieces, collided with instead of clipModel
	int				numPartClipModels;

	int				characterDef;		// a skeletal character's model def (bg_models), ET_CHARACTER or a player
	struct characterHistory_s	*characterHistory;	// its animation and place the last frames
	struct gRagdoll_s	*ragdoll;			// gone limp: its articulated figure (g_ragdoll.c)
	struct rigidBody_s	*body;			// physics props, see g_physics.c
	vec3_t			pmoveStartVelocity;	// player velocity before Pmove clipped it, for pushing props

	scriptObject_t	scriptObject;		// its "scriptobject" (g_script.c): its type's fields

	// a camera's (g_cinematic.c)
	char		*cameraName;			// func_cameraspline: cameras/<name>.camera
	char		*cameraTarget;			// func_cameraview: what it looks at
	int			cameraStartTime;		// when it started playing (0: not yet)
	qboolean	cameraPlaying;
	float		cameraFov;
	monsterState_t	monster;			// a monster's (g_monster.c)
};


typedef enum {
	CON_DISCONNECTED,
	CON_CONNECTING,
	CON_CONNECTED
} clientConnected_t;

typedef enum {
	SPECTATOR_NOT,
	SPECTATOR_FREE,
	SPECTATOR_FOLLOW,
	SPECTATOR_SCOREBOARD
} spectatorState_t;

typedef enum {
	TEAM_BEGIN,		// Beginning a team game, spawn at base
	TEAM_ACTIVE		// Now actively playing
} playerTeamStateState_t;

typedef struct {
	playerTeamStateState_t	state;

	int			location;

	float		lasthurtcarrier;
	float		lastreturnedflag;
	float		lastfraggedcarrier;
} playerTeamState_t;

// player data that stays across multiple levels or tournament restarts
// this is achieved by writing all the data to cvar strings at game shutdown
// time and reading them back at connection time.  Anything added here
// MUST be dealt with in G_InitSessionData() / G_ReadSessionData() / G_WriteSessionData()
typedef struct {
	team_t		sessionTeam;
	int			spectatorNum;		// for determining next-in-line to play
	spectatorState_t	spectatorState;
	int			spectatorPlayer;	// for chasecam and follow mode
	int			wins, losses;		// tournament stats
	qboolean	teamLeader;			// true when this player is a team leader
} playerSession_t;

//
#define MAX_NETNAME			36
#define	MAX_VOTE_COUNT		3

// player data that stays across multiple respawns, but is cleared
// on each level change or team change at PlayerBegin()
typedef struct {
	int			connectionNum;		// index in level.connections
	int			localPlayerNum;		// client's local player number in range of 0 to MAX_SPLITVIEW-1
	clientConnected_t	connected;	
	usercmd_t	cmd;				// we would lose angles if not persistant
	qboolean	localClient;		// true if "ip" info key is "localhost"
	qboolean	initialSpawn;		// the first spawn should be at a cool location
	qboolean	predictItemPickup;	// based on cg_predictItems userinfo
	qboolean	pmoveFixed;			//
	int			antiLag;			// based on cg_antiLag userinfo
	char		netname[MAX_NETNAME];
	int			maxHealth;			// for handicapping
	int			enterTime;			// level.time the player entered the game
	playerTeamState_t teamState;	// status in teamplay games
	int			voteCount;			// to prevent people from constantly calling votes
	int			teamVoteCount;		// to prevent people from constantly calling votes
	qboolean	teamInfo;			// send team overlay updates?
	// the campaign's
	int				invSeen[MAX_ITEMS / 32];	// the items carried at the last sync, for the belt (g_inventory.c)
	int				invFlags[MAX_INV_SLOTS];	// Daikatana's key flags of the inventory's slots (KEY_USE_ONCE...)
	qboolean	persistantLoaded;		// what the last map gave them is theirs (G_LoadPersistant)
	int			persistantWeapon;		// the weapon they had up there
} playerPersistant_t;

#define MAX_PLAYER_MARKERS 17

typedef struct {
	vec3_t		mins;
	vec3_t		maxs;
	vec3_t		origin;
	int			time;
} playerMarker_t;

// this structure is cleared on each PlayerSpawn(),
// except for 'player->pers' and 'player->sess'
struct gplayer_s {
	// ps MUST be the first element, because the server expects it
	playerState_t	ps;				// communicated by server to clients

	// the rest of the structure is private to game
	playerPersistant_t	pers;
	qboolean	cinematicHidden;		// hidden for a cinematic (g_cinematic.c)
	int			cinematicOldUp;			// last frame's upmove and buttons, for a press that skips one
	int			cinematicOldButtons;
	playerSession_t		sess;

	qboolean	readyToExit;		// wishes to leave the intermission

	qboolean	noclip;

	// history for backward reconcile
	int			topMarker;
	playerMarker_t	playerMarkers[MAX_PLAYER_MARKERS];
	playerMarker_t	backupMarker;

	int			frameOffset;		// an approximation of the actual server time we received this
									// command (not in 50ms increments)

	int			lastCmdServerTime;	// ucmd.serverTime from last usercmd_t

	int			lastCmdTime;		// level.time of last usercmd_t, for EF_CONNECTION
									// we can't just use pers.lastCommand.time, because
									// of the g_sycronousclients case
	int			buttons;
	int			oldbuttons;
	int			latched_buttons;

	vec3_t		oldOrigin;

	// sum up damage over an entire frame, so
	// shotgun blasts give a single big kick
	int			damage_armor;		// damage absorbed by armor
	int			damage_blood;		// damage taken out of health
	int			damage_knockback;	// impact damage
	vec3_t		damage_from;		// origin for vector calculation
	qboolean	damage_fromWorld;	// if true, don't use the damage_from vector

	int			accurateCount;		// for "impressive" reward sound

	int			accuracy_shots;		// total number of shots
	int			accuracy_hits;		// total number of hits

	//
	int			lastkilled_player;	// last player that this player killed
	int			lasthurt_player;	// last player that damaged this player
	int			lasthurt_mod;		// type of damage the player did

	// timers
	int			respawnTime;		// can respawn when time > this, force after g_forcerespwan
	int			inactivityTime;		// kick players when time > this
	qboolean	inactivityWarning;	// qtrue if the five seoond warning has been given
	int			rewardTime;			// clear the EF_AWARD_IMPRESSIVE, etc when time > this

	int			airOutTime;

	int			invRefuseTime;		// "no room" said again from then on (g_inventory.c)
	int			invBotTime;			// a bot looks in its inventory again then

	int			lastKillTime;		// for multiple kill rewards

	qboolean	fireHeld;			// used for hook
	gentity_t	*hook;				// grapple hook if out

	int			switchTeamTime;		// time the player switched teams

	// timeResidual is used to handle events that happen every second
	// like health / armor countdowns and regeneration
	int			timeResidual;

#ifdef MISSIONPACK
	gentity_t	*persistantPowerup;
	int			portalID;
	int			ammoTimes[MAX_WEAPONS];
	int			invulnerabilityTime;
#endif

	char		*areabits;
};


// A single client can have multiple players, for splitscreen.
typedef struct gconnection_s {
	int			numLocalPlayers;				// for quick access, the players could be any indexes in localPlayers[].
	int			localPlayerNums[MAX_SPLITVIEW];
} gconnection_t;


//
// this structure is cleared as each map is entered
//
#define	MAX_SPAWN_VARS			64
#define	MAX_SPAWN_VARS_CHARS	4096

typedef struct {
	struct gplayer_s	*players;		// [maxplayers]

	struct gentity_s	*gentities;
	int			gentitySize;
	int			num_entities;		// MAX_CLIENTS <= num_entities <= ENTITYNUM_MAX_NORMAL

	gconnection_t	*connections;

	int			warmupTime;			// restart match at this time

	fileHandle_t	logFile;

	// store latched cvars here that we want to get at often
	int			maxplayers;
	int			maxconnections;

	int			framenum;
	int			time;					// in msec
	int			previousTime;			// so movers can back up when blocked

	int			startTime;				// level.time the map was started

	int			frameStartTime;			// time this server frame started

	int			teamScores[TEAM_NUM_TEAMS];
	int			lastTeamLocationTime;		// last time of client team location update

	qboolean	newSession;				// don't use any old session data, because
										// we changed gametype

	qboolean	restarted;				// waiting for a map_restart to fire

	int			numConnectedPlayers;
	int			numNonSpectatorPlayers;	// includes connecting players
	int			numPlayingPlayers;		// connected, non-spectators
	int			sortedPlayers[MAX_CLIENTS];		// sorted by score
	int			follow1, follow2;		// playerNums for auto-follow spectators

	int			snd_fry;				// sound index for standing in lava

	int			warmupModificationCount;	// for detecting if g_warmup is changed
	int			botReportModificationCount;

	// voting state
	char		voteString[MAX_STRING_CHARS];
	char		voteDisplayString[MAX_STRING_CHARS];
	int			voteTime;				// level.time vote was called
	int			voteExecuteTime;		// time the vote is executed
	int			voteYes;
	int			voteNo;
	int			numVotingPlayers;		// set by CalculateRanks

	// team voting state
	char		teamVoteString[2][MAX_STRING_CHARS];
	int			teamVoteTime[2];		// level.time vote was called
	int			teamVoteYes[2];
	int			teamVoteNo[2];
	int			numTeamVotingPlayers[2];// set by CalculateRanks

	// spawn variables
	qboolean	spawning;				// the G_Spawn*() functions are valid
	int			numSpawnVars;
	char		*spawnVars[MAX_SPAWN_VARS][2];	// key / value pairs
	int			numSpawnVarChars;
	char		spawnVarChars[MAX_SPAWN_VARS_CHARS];
	int			spawnEntityOffset;

	// intermission state
	int			intermissionQueued;		// intermission was qualified, but
										// wait INTERMISSION_DELAY_TIME before
										// actually going there so the last
										// frag can be watched.  Disable future
										// kills during this delay
	int			intermissiontime;		// time the intermission was started
	char		*changemap;
	qboolean	readyToExit;			// at least one player wants to exit
	int			exitTime;
	vec3_t		intermission_origin;	// also used for spectator spawns
	vec3_t		intermission_angle;

	qboolean	locationLinked;			// target_locations get linked
	qboolean	bounceLightmaps;		// the map's lightmaps have only the bounce of its steady lights (worldspawn "_bounceLightmaps")
	gentity_t	*locationHead;			// head of the location list
	int			bodyQueIndex;			// dead bodies
	gentity_t	*bodyQue[BODY_QUEUE_SIZE];
#ifdef MISSIONPACK
	int			portalSequence;
#endif

	int			hitboxTime;				// the time shots hit hitboxes at, while rewound for lag; 0 now

	// the campaign's
	qboolean	changingLevel;			// a trigger_changelevel was touched
	qboolean	loadPersistant;			// a changelevel led here: the players take what they carried
	int			numSecrets;
	int			secretsFound;

	// Doom 3's cinematic mode (g_cinematic.c)
	qboolean	botLibLoaded;			// the bots' AI and the map's AAS worlds (G_BotLibLoaded)

	gentity_t	*camera;				// what every player sees from; NULL: their own eyes
	qboolean	inCinematic;
	qboolean	skipCinematic;			// a player skipped it: waits end at once
	int			cinematicStartTime, cinematicSkipTime;
	vec3_t		cameraLastOrigin, cameraLastAngles;
	gentity_t	*cinematicEyes[MAX_CLIENTS];	// each player's portal entity, seeing from the camera
	vec4_t		fadeColor;				// the fade: colour, and the alpha it goes to
	float		fadeFromAlpha;
	int			fadeStartTime, fadeDuration;
} level_locals_t;


//
// g_spawn.c
//
qboolean	G_SpawnString( const char *key, const char *defaultString, char **out );
// spawn string returns a temporary reference, you must CopyString() if you want to keep it
qboolean	G_SpawnFloat( const char *key, const char *defaultString, float *out );
qboolean	G_SpawnInt( const char *key, const char *defaultString, int *out );
qboolean	G_SpawnVector( const char *key, const char *defaultString, float *out );
void		G_SpawnEntitiesFromString( void );
char *G_NewString( const char *string );
char		*G_AddSpawnVarToken( const char *string );
gentity_t	*G_SpawnGEntityFromSpawnVars( void );		// from level.spawnVars, as a map's

//
// g_cmds.c
//
void Cmd_Score_f (gentity_t *ent);
void StopFollowing( gentity_t *ent );
void BroadcastTeamChange( gplayer_t *player, int oldTeam );
void SetTeam( gentity_t *ent, const char *s );
void Cmd_FollowCycle_f( gentity_t *ent, int dir );
void G_Say( gentity_t *ent, gentity_t *target, int mode, const char *chatText );

//
// g_items.c
//
void G_RunItem( gentity_t *ent );
void RespawnItem( gentity_t *ent );

void UseHoldableItem( gentity_t *ent );
gentity_t *Drop_Item( gentity_t *ent, gitem_t *item, float angle );
gentity_t *LaunchItem( gitem_t *item, vec3_t origin, vec3_t velocity );
void G_SpawnItem (gentity_t *ent, gitem_t *item);
void FinishSpawningItem( gentity_t *ent );
void Add_Ammo (gentity_t *ent, int weapon, int count);
void Touch_Item (gentity_t *ent, gentity_t *other, trace_t *trace);

void ClearRegisteredItems( void );
void RegisterItem( gitem_t *item );
void SaveRegisteredItems( void );

//
// g_utils.c
//
int		G_FindConfigstringIndex( char *name, int start, int max, qboolean create );
int		G_ModelIndex( char *name );
int		G_SoundIndex( char *name );
void	trap_SendServerCommand( int playerNum, char *cmd );
void	G_TeamCommand( team_t team, char *cmd );
void	G_KillBox (gentity_t *ent);
gentity_t *G_Find (gentity_t *from, int fieldofs, const char *match);
gentity_t *G_PickTarget (char *targetname);
void	G_UseTargets (gentity_t *ent, gentity_t *activator);
void	G_SetBrushModel( gentity_t *ent, const char *name );
void	G_SetMovedir ( vec3_t angles, vec3_t movedir);

void	G_InitGentity( gentity_t *e );
gentity_t	*G_Spawn (void);
gentity_t *G_TempEntity( vec3_t origin, int event );
void	G_Sound( gentity_t *ent, int channel, int soundIndex );
void	G_FreeEntity( gentity_t *e );
qboolean	G_EntitiesFree( void );

void	G_TouchTriggers (gentity_t *ent);

float	*tv (float x, float y, float z);
char	*vtos( const vec3_t v );

float vectoyaw( const vec3_t vec );

void G_AddPredictableEvent( gentity_t *ent, int event, int eventParm );
void G_AddEvent( gentity_t *ent, int event, int eventParm );
void G_SetOrigin( gentity_t *ent, vec3_t origin );
void AddRemap(const char *oldShader, const char *newShader, float timeOffset);
const char *BuildShaderStateConfig( void );

//
// g_combat.c
//
qboolean CanDamage (gentity_t *targ, vec3_t origin);
void G_Damage (gentity_t *targ, gentity_t *inflictor, gentity_t *attacker, vec3_t dir, vec3_t point, int damage, int dflags, int mod);
qboolean G_RadiusDamage (vec3_t origin, gentity_t *attacker, float damage, float radius, gentity_t *ignore, int mod);
int G_InvulnerabilityEffect( gentity_t *targ, vec3_t dir, vec3_t point, vec3_t impactpoint, vec3_t bouncedir );
void body_die( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath );
void TossPlayerItems( gentity_t *self );
#ifdef MISSIONPACK
void TossPlayerPersistantPowerups( gentity_t *self );
#endif

// damage flags
#define DAMAGE_RADIUS				0x00000001	// damage was indirect
#define DAMAGE_NO_ARMOR				0x00000002	// armour does not protect from this damage
#define DAMAGE_NO_KNOCKBACK			0x00000004	// do not affect velocity, just view angles
#define DAMAGE_NO_PROTECTION		0x00000008  // armor, shields, invulnerability, and godmode have no effect
#ifdef MISSIONPACK
#define DAMAGE_NO_TEAM_PROTECTION	0x00000010  // armor, shields, invulnerability, and godmode have no effect
#endif

//
// g_missile.c
//
void G_RunMissile( gentity_t *ent );
void G_ExplodeMissile( gentity_t *ent );

#define MISSILE_DETONATE_ON_WORLD	1	// else it bounces off, or stops with no bounce
#define MISSILE_DETONATE_ON_ACTOR	2	// else it bounces off players and the like
#define MISSILE_DETONATE_ON_FUSE	4	// else it goes away
#define MISSILE_MAX_BOUNCES			8	// detonates when it would bounce again after count bounces
#define MISSILE_STICK				16	// stops where it hits instead of bouncing
#define MISSILE_DETONATE_ON_WATER	32	// hits water surfaces and goes off there

gentity_t *fire_projectile( gentity_t *self, const struct dict_s *def, int weapon, vec3_t start, vec3_t dir );
void G_MonsterFireWeapon( gentity_t *ent, int weapon, const vec3_t start, const vec3_t aim );


//
// g_mover.c
//
void G_RunMover( gentity_t *ent );
void Touch_DoorTrigger( gentity_t *ent, gentity_t *other, trace_t *trace );

//
// g_trigger.c
//
void trigger_teleporter_touch (gentity_t *self, gentity_t *other, trace_t *trace );


//
// g_misc.c
//
void TeleportPlayer( gentity_t *player, vec3_t origin, vec3_t angles );
#ifdef MISSIONPACK
void DropPortalSource( gentity_t *ent );
void DropPortalDestination( gentity_t *ent );
#endif


//
// g_weapon.c
//
qboolean LogAccuracyHit( gentity_t *target, gentity_t *attacker );
void CalcMuzzlePoint ( gentity_t *ent, int weapon, vec3_t forward, vec3_t right, vec3_t up, vec3_t muzzlePoint );
void Weapon_HookFree (gentity_t *ent);
qboolean G_IsStartWeapon( int weapon );
void Weapon_HookThink (gentity_t *ent);


//
// g_client.c
//
int TeamCount( int ignorePlayerNum, team_t team );
int TeamLeader( int team );
team_t PickTeam( int ignorePlayerNum );
void SetPlayerViewAngle( gentity_t *ent, vec3_t angle );
gentity_t *SelectSpawnPoint (vec3_t avoidPoint, vec3_t origin, vec3_t angles, qboolean isbot);
void CopyToBodyQue( gentity_t *ent );
void PlayerRespawn(gentity_t *ent);
void BeginIntermission (void);
void InitBodyQue (void);
void PlayerSpawn( gentity_t *ent );
void player_die (gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod);
void AddScore( gentity_t *ent, vec3_t origin, int score );
void CalculateRanks( void );
qboolean SpotWouldTelefrag( gentity_t *spot );

//
// g_unlagged.c
//
void G_ResetHistory( gentity_t *ent );
void G_StoreHistory( gentity_t *ent );
void G_TimeShiftAllClients( int time, gentity_t *skip );
void G_UnTimeShiftAllClients( gentity_t *skip );
void G_DoTimeShiftFor( gentity_t *ent );
void G_UndoTimeShiftFor( gentity_t *ent );
void G_UnTimeShiftClient( gentity_t *client );
void G_PredictPlayerMove( gentity_t *ent, float frametime );

//
// g_svcmds.c
//
qboolean	G_ConsoleCommand( void );
qboolean	G_ConsoleCompleteArgument( int completeArgument );
void G_RegisterCommands( void );
void G_ProcessIPBans(void);
qboolean G_FilterPacket (char *from);

//
// g_weapon.c
//
void FireWeapon( gentity_t *ent );

//
// g_character.c
//
int		G_CharacterHitboxes( const gentity_t *ent, int time, bgHitboxPose_t *poses );
void	G_TraceShot( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
					 int passEntityNum, int contentmask, int *zone );
float	G_HitboxDamageScale( const gentity_t *ent, int zone );
void	G_PlayerSetModel( gentity_t *ent, const char *modelName );
void	G_PlayerCharacterFrame( gentity_t *ent );
void	G_CharacterSetAnim( gentity_t *ent, int channel, int anim );
void	Svcmd_CharacterAnim_f( void );
void	SP_misc_character( gentity_t *ent );
void	Svcmd_SpawnCharacter_f( void );
void AltFireWeapon( gentity_t *ent );
#ifdef MISSIONPACK
void G_StartKamikaze( gentity_t *ent );
#endif

//
// g_cmds.c
//
void DeathmatchScoreboardMessage( gentity_t *ent );
char *ConcatArgs( int start );
qboolean StringIsInteger( const char * s );

//
// g_main.c
//
void MovePlayerToIntermission( gentity_t *ent );
void FindIntermissionPoint( void );
void SetLeader(int team, int player);
void CheckTeamLeader( int team );
void G_RunThink (gentity_t *ent);
void AddTournamentQueue(gplayer_t *player);
void QDECL G_LogPrintf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void SendScoreboardMessageToAllClients( void );
void QDECL G_DPrintf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void QDECL G_Printf( const char *fmt, ... ) __attribute__ ((format (printf, 1, 2)));
void QDECL G_Error( const char *fmt, ... ) __attribute__ ((noreturn, format (printf, 1, 2)));

//
// g_client.c
//
char *PlayerConnect( int playerNum, qboolean firstTime, qboolean isBot, int connectionNum, int localPlayerNum );
void PlayerUserinfoChanged( int playerNum );
qboolean PlayerDisconnect( int playerNum, qboolean force );
void PlayerBegin( int playerNum );
void ClientCommand( int connectionNum );
float PlayerHandicap( gplayer_t *player );

//
// g_active.c
//
void PlayerThink( int playerNum );
void PlayerEndFrame( gentity_t *ent );
void G_RunPlayer( gentity_t *ent );

//
// g_team.c
//
qboolean OnSameTeam( gentity_t *ent1, gentity_t *ent2 );
void Team_CheckDroppedItem( gentity_t *dropped );

//
// g_session.c
//
void G_ReadSessionData( gplayer_t *player );
void G_InitSessionData( gplayer_t *player, char *userinfo );

void G_InitWorldSession( void );
void G_WriteSessionData( void );

//
// g_world.c
//
//
// g_campaign.c
//
qboolean	G_CampaignSkipEntity( const gentity_t *ent );
gentity_t	*G_CampaignSpawnPoint( vec3_t origin, vec3_t angles );
void		G_CampaignUseTargets( gentity_t *ent, gentity_t *activator );
qboolean	G_CampaignKeyCheck( gentity_t *self, gentity_t *activator );
int			G_InventoryCount( const gentity_t *ent, const char *item );
int			G_InventoryAdd( gentity_t *ent, const char *item, int count, int flags );

//
// g_inventory.c
//
extern qboolean g_itemFrobbed;		// g_items.c

void		G_InventorySpawn( gentity_t *ent, const int *savedItem, const int *savedCount,
							const int *savedCell, const int *savedValue );
void		G_InventorySync( gentity_t *ent );
int			G_InventoryTakeItem( gentity_t *ent, gentity_t *other );
void		G_InventoryRefuse( gentity_t *other, gitem_t *item );
qboolean	G_InventoryUse( gentity_t *ent, int slot );
qboolean	G_InventoryWear( gentity_t *ent, int slot );
qboolean	G_InventoryTakeOff( gentity_t *ent );
qboolean	G_InventoryDrop( gentity_t *ent, int slot, int count );
qboolean	G_InventoryDropWorn( gentity_t *ent );
void		G_InventoryBotThink( gentity_t *ent );
void		Cmd_Inventory_f( gentity_t *ent, const char *cmd );

//
// g_frob.c
//
void		G_FrobThink( gentity_t *ent );
void		G_InventoryRemove( gentity_t *ent, const char *item, int count );
void		G_ChangeLevel( const char *map, const char *target );
void		SP_info_player_coop( gentity_t *ent );
void		SP_trigger_relay( gentity_t *self );
void		SP_trigger_counter( gentity_t *self );
void		SP_trigger_secret( gentity_t *self );
void		SP_trigger_changelevel( gentity_t *self );
void		SP_trigger_remove_inventory_item( gentity_t *self );
void		SP_func_wall( gentity_t *self );

//
// g_save.c
//
qboolean	G_SavePersistant( const char *nextmap );
qboolean	G_LoadPersistant( gentity_t *ent );
qboolean	G_SaveGame( const char *name );
struct characterHistory_s *G_AllocCharacterHistory( void );		// g_character.c

// g_airnav.c
int			G_AirRoute( const vec3_t start, const vec3_t goal, const vec3_t mins, const vec3_t maxs, int passEntityNum,
						vec3_t *route, int max );
qboolean	G_AirClear( const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs, int passEntityNum );
void		G_AirNavShutdown( void );

// g_monster.c
void		SP_monster( gentity_t *ent );
qboolean	G_IsMonster( const gentity_t *ent );

//
// g_aiscript.c
//
qboolean	G_RunAiScript( const char *name, gentity_t *actor, qboolean delayed );
void		G_AiScriptLevelStart( void );
void		G_TargetEye( const gentity_t *ent, vec3_t eye );		// where a monster aims at it
qboolean	G_MonsterAttackMelee( gentity_t *self, const char *damageDef );		// g_script_events.c
qboolean	G_MonsterAttackWeapon( gentity_t *self, const char *weaponDef, const vec3_t offset );
qboolean	G_BotLibLoaded( void );		// g_main.c: the bots' AI and the map's AAS worlds, loaded when first needed
qboolean	G_MonsterPerch( gentity_t *ent, const vec3_t pos, const vec3_t normal );
gentity_t	*G_MonsterEnemy( gentity_t *ent );
gentity_t	*G_MonsterFindEnemy( gentity_t *ent, qboolean useFov );
qboolean	G_MonsterCanSee( gentity_t *ent, gentity_t *other );
void		G_MonsterEye( const gentity_t *ent, vec3_t eye );
void		G_MonsterSetState( gentity_t *ent, const char *name );
void		G_MonsterThink( gentity_t *ent );
void		G_MonsterPain( gentity_t *self, gentity_t *attacker, int damage );
void		G_MonsterDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath );
void		G_MonsterTouch( gentity_t *self, gentity_t *other, trace_t *trace );

// g_save.c: what a saved game is written with and read from
void		G_SaveBytes( const void *data, int len );
void		G_SaveString( const char *s );
void		G_LoadBytes( void *data, int len );
qboolean	G_LoadStringBuf( char *buf, int size );
void		G_SaveFailed( void );
void		Svcmd_SaveGame_f( void );
qboolean	G_LoadGame( void );

//
// g_netevent.c
//
void		G_ServerSendEvent( const gentity_t *ent, int event, const bitMsg_t *msg, qboolean saveEvent, int excludePlayer );
void		G_SendSavedEvents( int playerNum );
void		G_RemoveEntityEvents( const gentity_t *ent );
void		G_ClearSavedEvents( void );
void		G_SendSoundEvent( const gentity_t *ent, int channel, const char *soundShader, float volume );

//
// g_ragdoll.c
//
qboolean	G_StartRagdoll( gentity_t *ent );
void		G_RunRagdoll( gentity_t *ent );
void		G_RagdollImpulse( gentity_t *ent, const vec3_t point, const vec3_t impulse );
void		G_FreeRagdoll( gentity_t *ent );

extern clip_t g_clip;

void G_SectorList_f( void );
void G_GetBrushBounds( int modelindex, vec3_t mins, vec3_t maxs );
void G_LinkEntity( gentity_t *ent );
void G_UnlinkEntity( gentity_t *ent );
qboolean G_EntityContact( const vec3_t mins, const vec3_t maxs, const sharedEntity_t *gEnt, traceType_t type );
int G_EntitiesInBox( const vec3_t mins, const vec3_t maxs, int *entityList, int maxcount );
void G_Trace( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, traceType_t type );
void G_TraceBox( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask );
void G_TraceCapsule( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask );
void G_ClipToEntities( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passEntityNum, int contentmask, traceType_t type );
void G_ClipToEntity( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int entityNum, int contentmask, traceType_t type );
int G_PointContents( const vec3_t p, int passEntityNum );

//
// g_bot.c
//
void G_InitBots( qboolean restart );
char *G_GetBotInfoByNumber( int num );
char *G_GetBotInfoByName( const char *name );
void G_CheckBotSpawn( void );
void G_RemoveQueuedBotBegin( int playerNum );
qboolean G_BotConnect( int playerNum, qboolean restart );
void Svcmd_AddBot_f( void );
void Svcmd_AddBotComplete( char *args, int argNum );
void Svcmd_BotList_f( void );
void BotInterbreedEndMatch( void );

//
// g_botlib.c
//
void G_BotInitBotLib(void);

//
// ai_main.c
//
#define MAX_FILEPATH			144

//bot settings
typedef struct bot_settings_s
{
	char characterfile[MAX_FILEPATH];
	float skill;
} bot_settings_t;

int BotAISetup( int restart );
int BotAIShutdown( int restart );
int BotAILoadMap( int restart );
int BotAISetupPlayer(int playernum, struct bot_settings_s *settings, qboolean restart);
int BotAIShutdownPlayer( int playernum, qboolean restart );
int BotAIStartFrame( int time );
void BotTestAAS(vec3_t origin);
void Svcmd_BotTeamplayReport_f( void );
void Svcmd_BotGoto_f( void );

#include "g_team.h" // teamplay specific stuff
#include "g_syscalls.h"


extern	level_locals_t	level;
extern	gentity_t		g_entities[MAX_GENTITIES];

#define	FOFS(x) ((size_t)&(((gentity_t *)0)->x))

extern	vmCvar_t	g_gametype;
extern	vmCvar_t	g_dedicated;
extern	vmCvar_t	g_cheats;
extern	vmCvar_t	g_maxplayers;			// allow this many total, including spectators
extern	vmCvar_t	g_maxGamePlayers;		// allow this many active
extern	vmCvar_t	g_restarted;

extern	vmCvar_t	g_dmflags;
extern	vmCvar_t	g_fraglimit;
extern	vmCvar_t	g_timelimit;
extern	vmCvar_t	g_capturelimit;
extern	vmCvar_t	g_friendlyFire;
extern	vmCvar_t	g_password;
extern	vmCvar_t	g_needpass;
extern	vmCvar_t	g_gravity;
extern	vmCvar_t	g_speed;
extern	vmCvar_t	g_knockback;
extern	vmCvar_t	g_quadfactor;
extern	vmCvar_t	g_forcerespawn;
extern	vmCvar_t	g_inactivity;
extern	vmCvar_t	g_debugMove;
extern	vmCvar_t	g_debugDamage;
extern	vmCvar_t	g_debugHitboxes;
extern	vmCvar_t	g_ragdollPush;
extern	vmCvar_t	g_skill;
extern	vmCvar_t	g_spawnTarget;
extern	vmCvar_t	g_unlimitedSaves;
extern	vmCvar_t	g_weaponRespawn;
extern	vmCvar_t	g_touchPickup;
extern	vmCvar_t	g_debugFrob;
extern	vmCvar_t	g_weaponTeamRespawn;
extern	vmCvar_t	g_synchronousClients;
extern	vmCvar_t	g_motd;
extern	vmCvar_t	g_warmup;
extern	vmCvar_t	g_doWarmup;
extern	vmCvar_t	g_allowVote;
extern	vmCvar_t	g_teamAutoJoin;
extern	vmCvar_t	g_teamForceBalance;
extern	vmCvar_t	g_banIPs;
extern	vmCvar_t	g_filterBan;
extern	vmCvar_t	g_redteam;
extern	vmCvar_t	g_blueteam;
extern	vmCvar_t	g_smoothClients;
extern	vmCvar_t	pmove_overbounce;
extern	vmCvar_t	pmove_fixed;
extern	vmCvar_t	pmove_msec;
extern	vmCvar_t	g_rankings;
extern	vmCvar_t	g_singlePlayer;
extern	vmCvar_t	g_proxMineTimeout;
extern	vmCvar_t	g_playerCapsule;
extern	vmCvar_t	g_instagib;


#include "g_physics.h"

//
// g_cinematic.c
//
qboolean	G_CameraView( gentity_t *cam, vec3_t origin, vec3_t angles, float *fov );
void		G_CameraStart( gentity_t *cam );
void		G_CameraStop( gentity_t *cam );
void		G_SetCamera( gentity_t *cam );
void		G_SkipCinematic( void );
void		G_CinematicConfigString( void );
void		G_CinematicPlayerThink( gentity_t *ent, usercmd_t *ucmd );
void		G_RunCinematic( void );
void		G_SetFade( const vec4_t color, float alpha, int ms );
void		G_GlobalSound( const char *sound );
void		G_GlobalLoop( const char *sound );
void		G_StopGlobalLoops( void );
