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
===========================================================================
*/
// g_save.c -- the campaign's persistence, after RTCW's single player
//
// G_SavePersistant/G_LoadPersistant (RTCW's): what the players carry from
// one map to the next (health, armor, weapons, ammo, inventory), written to
// save/current.psw as the level changes and read as each one first spawns
// in the next map.

#include "g_local.h"

#define PERSISTANT_FILE		"save/current.psw"
#define PERSISTANT_MAGIC	0x50534b44		// "DKSP"
#define PERSISTANT_VERSION	4		// 2: the inventory grid, 3: the belt, 4: the quests

// what a player carries (bg_inventory.c)
typedef struct {
	int		item[MAX_INV_SLOTS];
	int		count[MAX_INV_SLOTS];
	int		cell[MAX_INV_SLOTS];
	int		value[MAX_INV_SLOTS];
	int		flags[MAX_INV_SLOTS];
	int		belt[MAX_BELT_SLOTS];
	int		seen[MAX_ITEMS / 32];
	quest_t	quests[MAX_QUESTS];		// and what they're set to do (g_quests.c)
	int		questOrder;
} savedInventory_t;

static void G_SaveInventory( const gplayer_t *player, savedInventory_t *inv ) {
	memcpy( inv->item, player->ps.invItem, sizeof( inv->item ) );
	memcpy( inv->count, player->ps.invCount, sizeof( inv->count ) );
	memcpy( inv->cell, player->ps.invCell, sizeof( inv->cell ) );
	memcpy( inv->value, player->ps.invValue, sizeof( inv->value ) );
	memcpy( inv->flags, player->pers.invFlags, sizeof( inv->flags ) );
	memcpy( inv->belt, player->ps.invBelt, sizeof( inv->belt ) );
	memcpy( inv->seen, player->pers.invSeen, sizeof( inv->seen ) );
	memcpy( inv->quests, player->pers.quests, sizeof( inv->quests ) );
	inv->questOrder = player->pers.questOrder;
}

static void G_RestoreInventory( gplayer_t *player, const savedInventory_t *inv ) {
	memcpy( player->ps.invItem, inv->item, sizeof( inv->item ) );
	memcpy( player->ps.invCount, inv->count, sizeof( inv->count ) );
	memcpy( player->ps.invCell, inv->cell, sizeof( inv->cell ) );
	memcpy( player->ps.invValue, inv->value, sizeof( inv->value ) );
	memcpy( player->pers.invFlags, inv->flags, sizeof( inv->flags ) );
	memcpy( player->ps.invBelt, inv->belt, sizeof( inv->belt ) );
	memcpy( player->pers.invSeen, inv->seen, sizeof( inv->seen ) );
	memcpy( player->pers.quests, inv->quests, sizeof( inv->quests ) );
	player->pers.questOrder = inv->questOrder;
}

// what a player takes along
typedef struct {
	int		valid;
	int		health;
	int		stats[MAX_STATS];
	int		ammo[MAX_WEAPONS];
	int		ammoClip[MAX_WEAPONS];
	int		weapon;
	savedInventory_t	inventory;
} persistantPlayer_t;

typedef struct {
	int		magic;
	int		version;
	char	map[MAX_QPATH];				// the map they go to
	persistantPlayer_t	players[MAX_CLIENTS];
} persistantFile_t;

static persistantFile_t	g_persistant;

/*
================
G_SavePersistant

RTCW's: before the change to nextmap, what each player takes along
================
*/
qboolean G_SavePersistant( const char *nextmap ) {
	persistantPlayer_t *p;
	gplayer_t *player;
	fileHandle_t f;
	int i;

	memset( &g_persistant, 0, sizeof( g_persistant ) );
	g_persistant.magic = PERSISTANT_MAGIC;
	g_persistant.version = PERSISTANT_VERSION;
	Q_strncpyz( g_persistant.map, nextmap, sizeof( g_persistant.map ) );

	for ( i = 0; i < level.maxplayers && i < MAX_CLIENTS; i++ ) {
		player = &level.players[i];
		if ( player->pers.connected != CON_CONNECTED || player->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		p = &g_persistant.players[i];
		p->valid = qtrue;
		p->health = g_entities[i].health;
		memcpy( p->stats, player->ps.stats, sizeof( p->stats ) );
		memcpy( p->ammo, player->ps.ammo, sizeof( p->ammo ) );
		memcpy( p->ammoClip, player->ps.ammoClip, sizeof( p->ammoClip ) );
		p->weapon = player->ps.weapon;
		G_SaveInventory( player, &p->inventory );
	}

	if ( trap_FS_FOpenFile( PERSISTANT_FILE, &f, FS_WRITE ) < 0 ) {
		G_Printf( S_COLOR_YELLOW "G_SavePersistant: can't write %s\n", PERSISTANT_FILE );
		return qfalse;
	}
	trap_FS_Write( &g_persistant, sizeof( g_persistant ), f );
	trap_FS_FCloseFile( f );
	return qtrue;
}

/*
================
G_LoadPersistant

RTCW's: a player's first spawn in a map the level changed to gets what
they had; qfalse when there's nothing for them
================
*/
qboolean G_LoadPersistant( gentity_t *ent ) {
	gplayer_t *player = ent->player;
	persistantPlayer_t *p;
	fileHandle_t f;
	char map[MAX_QPATH];
	int len, i;

	if ( player->pers.persistantLoaded || !level.loadPersistant ) {
		return qfalse;		// once, and only in a map a changelevel led to (not a new game)
	}
	player->pers.persistantLoaded = qtrue;

	len = trap_FS_FOpenFile( PERSISTANT_FILE, &f, FS_READ );
	if ( len != (int)sizeof( g_persistant ) ) {
		if ( len >= 0 ) {
			trap_FS_FCloseFile( f );
		}
		return qfalse;
	}
	trap_FS_Read( &g_persistant, sizeof( g_persistant ), f );
	trap_FS_FCloseFile( f );

	trap_Cvar_VariableStringBuffer( "mapname", map, sizeof( map ) );
	if ( g_persistant.magic != PERSISTANT_MAGIC || g_persistant.version != PERSISTANT_VERSION
		|| Q_stricmp( g_persistant.map, map ) ) {
		return qfalse;		// not for this map: the campaign started here
	}

	i = ent - g_entities;
	if ( i < 0 || i >= MAX_CLIENTS || !g_persistant.players[i].valid ) {
		return qfalse;
	}
	p = &g_persistant.players[i];

	ent->health = player->ps.stats[STAT_HEALTH] = p->health;
	for ( i = 0; i < MAX_STATS; i++ ) {
		if ( i != STAT_HEALTH && i != STAT_DEAD_YAW ) {
			player->ps.stats[i] = p->stats[i];
		}
	}
	memcpy( player->ps.ammo, p->ammo, sizeof( p->ammo ) );
	memcpy( player->ps.ammoClip, p->ammoClip, sizeof( p->ammoClip ) );
	G_RestoreInventory( player, &p->inventory );
	if ( BG_HasWeapon( &player->ps, p->weapon ) ) {
		player->pers.persistantWeapon = p->weapon;
	}
	return qtrue;
}

/*
===============================================================================

	Saved games: RTCW's G_SaveGame and G_LoadGame

	save/<name>.svg: the map, the time, the level's, every entity as it is
	(its pointers made names and numbers: strings and functions by name, as
	RTCW's g_funcs.h has them, entities by number, items by classname, its
	script object by its type's name), the scripts' variables and threads
	(G_ScriptWriteGame) and the players'. "loadgame" goes back to the map at the time the game was
	saved (the engine's savegame_time) and puts it all back over the map's
	freshly spawned entities once the first player is in.

===============================================================================
*/

typedef struct {
	const char	*name;
	void		*func;
} saveFunc_t;

#include "g_funcs.h"		// made by code/tools/extractfuncs.py

#define MAX_SAVEGAME_SIZE	( 8 << 20 )

typedef enum {
	SF_STRING,			// char *: its text
	SF_FUNCTION,		// a callback: its name
	SF_ENTITY,			// gentity_t *: its number
	SF_ITEM,			// gitem_t *: its classname
	SF_HEAP				// what the load makes anew (bodies, histories, figures)
} saveFieldType_t;

typedef struct {
	size_t			ofs;
	saveFieldType_t	type;
} saveField_t;

#define SFOFS( x )		offsetof( gentity_t, x )

static const saveField_t g_entityFields[] = {
	{ SFOFS( classname ), SF_STRING },
	{ SFOFS( model ), SF_STRING },
	{ SFOFS( model2 ), SF_STRING },
	{ SFOFS( message ), SF_STRING },
	{ SFOFS( map ), SF_STRING },
	{ SFOFS( killtarget ), SF_STRING },
	{ SFOFS( keyname ), SF_STRING },
	{ SFOFS( pathtarget ), SF_STRING },
	{ SFOFS( target ), SF_STRING },
	{ SFOFS( targetname ), SF_STRING },
	{ SFOFS( team ), SF_STRING },
	{ SFOFS( targetShaderName ), SF_STRING },
	{ SFOFS( targetShaderNewName ), SF_STRING },
	{ SFOFS( dl_stylestring ), SF_STRING },
	{ SFOFS( dl_shader ), SF_STRING },
	{ SFOFS( think ), SF_FUNCTION },
	{ SFOFS( reached ), SF_FUNCTION },
	{ SFOFS( blocked ), SF_FUNCTION },
	{ SFOFS( touch ), SF_FUNCTION },
	{ SFOFS( use ), SF_FUNCTION },
	{ SFOFS( pain ), SF_FUNCTION },
	{ SFOFS( die ), SF_FUNCTION },
	{ SFOFS( snapshotCallback ), SF_FUNCTION },
	{ SFOFS( parent ), SF_ENTITY },
	{ SFOFS( nextTrain ), SF_ENTITY },
	{ SFOFS( prevTrain ), SF_ENTITY },
	{ SFOFS( target_ent ), SF_ENTITY },
	{ SFOFS( chain ), SF_ENTITY },
	{ SFOFS( enemy ), SF_ENTITY },
	{ SFOFS( activator ), SF_ENTITY },
	{ SFOFS( teamchain ), SF_ENTITY },
	{ SFOFS( teammaster ), SF_ENTITY },
	{ SFOFS( item ), SF_ITEM },
	{ SFOFS( player ), SF_HEAP },
	{ SFOFS( partClipModels ), SF_HEAP },
	{ SFOFS( characterHistory ), SF_HEAP },
	{ SFOFS( ragdoll ), SF_HEAP },
	{ SFOFS( body ), SF_HEAP },
};

// what a player gets back
typedef struct {
	int			valid;
	vec3_t		origin, velocity, viewangles;
	int			health;
	int			stats[MAX_STATS];
	int			ammo[MAX_WEAPONS];
	int			ammoClip[MAX_WEAPONS];
	int			powerups[MAX_POWERUPS];
	int			weapon;
	savedInventory_t	inventory;
} savePlayer_t;

// the engine's loadgame (sv_ccmds.c) reads the first five of the info:
// keep them first
typedef struct {
	saveInfo_t		info;
	savePlayer_t	players[MAX_CLIENTS];
} saveHeader_t;

// the file being written or read
static byte		*g_saveBuf;
static int		g_saveSize, g_saveOfs;
static qboolean	g_saveError;

void G_SaveBytes( const void *data, int len ) {
	if ( g_saveSize + len > MAX_SAVEGAME_SIZE ) {
		g_saveError = qtrue;
		return;
	}
	memcpy( g_saveBuf + g_saveSize, data, len );
	g_saveSize += len;
}

void G_SaveString( const char *s ) {
	int len = s ? strlen( s ) + 1 : 0;

	G_SaveBytes( &len, sizeof( len ) );
	if ( len ) {
		G_SaveBytes( s, len );
	}
}

void G_LoadBytes( void *data, int len ) {
	if ( g_saveOfs + len > g_saveSize ) {
		g_saveError = qtrue;
		memset( data, 0, len );
		return;
	}
	memcpy( data, g_saveBuf + g_saveOfs, len );
	g_saveOfs += len;
}

// a string read back, kept (G_NewString's heap), or NULL
static char *G_LoadString( void ) {
	char buf[MAX_STRING_CHARS];
	int len;

	G_LoadBytes( &len, sizeof( len ) );
	if ( len <= 0 ) {
		return NULL;
	}
	if ( len > (int)sizeof( buf ) || g_saveOfs + len > g_saveSize ) {
		g_saveError = qtrue;
		return NULL;
	}
	G_LoadBytes( buf, len );
	buf[len - 1] = '\0';
	return G_NewString( buf );
}

// a string read back into buf; qfalse if it was NULL
qboolean G_LoadStringBuf( char *buf, int size ) {
	int len;

	G_LoadBytes( &len, sizeof( len ) );
	buf[0] = '\0';
	if ( len <= 0 ) {
		return qfalse;
	}
	if ( len > size || g_saveOfs + len > g_saveSize ) {
		g_saveError = qtrue;
		return qfalse;
	}
	G_LoadBytes( buf, len );
	buf[len - 1] = '\0';
	return qtrue;
}

// a damaged save, as g_script.c reads it
void G_SaveFailed( void ) {
	g_saveError = qtrue;
}

static const char *G_FuncName( void *func ) {
	int i;

	for ( i = 0; g_saveFuncs[i].name; i++ ) {
		if ( g_saveFuncs[i].func == func ) {
			return g_saveFuncs[i].name;
		}
	}
	return NULL;
}

static void *G_FuncForName( const char *name ) {
	int i;

	for ( i = 0; g_saveFuncs[i].name; i++ ) {
		if ( !strcmp( g_saveFuncs[i].name, name ) ) {
			return g_saveFuncs[i].func;
		}
	}
	return NULL;
}

/*
================
G_WriteEntity

the entity as it is, its pointers after it as names and numbers
================
*/
static void G_WriteEntity( const gentity_t *ent ) {
	const saveField_t *f;
	gentity_t copy = *ent;
	void *p;
	int i, n;

	for ( i = 0; i < (int)ARRAY_LEN( g_entityFields ); i++ ) {
		memset( (byte *)&copy + g_entityFields[i].ofs, 0, sizeof( void * ) );
	}
	memset( &copy.clipModel, 0, sizeof( copy.clipModel ) );
	memset( &copy.scriptObject, 0, sizeof( copy.scriptObject ) );
	G_SaveBytes( &copy, sizeof( copy ) );

	for ( i = 0; i < (int)ARRAY_LEN( g_entityFields ); i++ ) {
		f = &g_entityFields[i];
		p = *(void **)( (byte *)ent + f->ofs );
		switch ( f->type ) {
			case SF_STRING:
				G_SaveString( p );
				break;
			case SF_FUNCTION:
				if ( p && !G_FuncName( p ) ) {
					G_Printf( S_COLOR_YELLOW "G_SaveGame: entity %d (%s) has a callback of no name\n",
							  ent->s.number, ent->classname ? ent->classname : "?" );
				}
				G_SaveString( p ? G_FuncName( p ) : NULL );
				break;
			case SF_ENTITY:
				n = p ? (gentity_t *)p - g_entities : -1;
				G_SaveBytes( &n, sizeof( n ) );
				break;
			case SF_ITEM:
				G_SaveString( p ? ( (gitem_t *)p )->classname : NULL );
				break;
			case SF_HEAP:
				n = p != NULL;
				G_SaveBytes( &n, sizeof( n ) );
				break;
		}
	}

	// its script object and spawn args
	G_ScriptWriteEntity( ent );
}

/*
================
G_SaveGame

RTCW's: the campaign's state to save/<name>.svg; qfalse if it couldn't be
================
*/
qboolean G_SaveGame( const char *name ) {
	static saveHeader_t header;
	gentity_t *ent;
	gplayer_t *player;
	savePlayer_t *sp;
	fileHandle_t f;
	char path[MAX_QPATH];
	int i, numEntities = 0;

	if ( g_gametype.integer != GT_CAMPAIGN || level.intermissiontime || level.changingLevel ) {
		G_Printf( "Can't save now.\n" );
		return qfalse;
	}

	memset( &header, 0, sizeof( header ) );
	header.info.magic = SAVEGAME_MAGIC;
	header.info.version = SAVEGAME_VERSION;
	trap_Cvar_VariableStringBuffer( "mapname", header.info.map, sizeof( header.info.map ) );
	header.info.time = level.time;
	header.info.skill = g_skill.integer;
	header.info.entitySize = sizeof( gentity_t );
	header.info.numSecrets = level.numSecrets;
	header.info.secretsFound = level.secretsFound;
	{
		qtime_t t;

		trap_RealTime( &t );
		Com_sprintf( header.info.date, sizeof( header.info.date ), "%04d-%02d-%02d %02d:%02d:%02d",
			1900 + t.tm_year, 1 + t.tm_mon, t.tm_mday, t.tm_hour, t.tm_min, t.tm_sec );
	}
	trap_GetConfigstring( CS_MESSAGE, header.info.title, sizeof( header.info.title ) );

	for ( i = 0; i < level.maxplayers && i < MAX_CLIENTS; i++ ) {
		player = &level.players[i];
		if ( player->pers.connected != CON_CONNECTED || player->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		if ( g_entities[i].health <= 0 ) {
			G_Printf( "Can't save while dead.\n" );
			return qfalse;
		}
		sp = &header.players[i];
		sp->valid = qtrue;
		VectorCopy( player->ps.origin, sp->origin );
		VectorCopy( player->ps.velocity, sp->velocity );
		VectorCopy( player->ps.viewangles, sp->viewangles );
		sp->health = g_entities[i].health;
		memcpy( sp->stats, player->ps.stats, sizeof( sp->stats ) );
		memcpy( sp->ammo, player->ps.ammo, sizeof( sp->ammo ) );
		memcpy( sp->ammoClip, player->ps.ammoClip, sizeof( sp->ammoClip ) );
		memcpy( sp->powerups, player->ps.powerups, sizeof( sp->powerups ) );
		sp->weapon = player->ps.weapon;
		G_SaveInventory( player, &sp->inventory );
		if ( !header.info.health ) {
			header.info.health = g_entities[i].health;
			header.info.armor = player->ps.stats[STAT_ARMOR];
		}
	}

	g_saveBuf = trap_HeapMalloc( MAX_SAVEGAME_SIZE );
	if ( !g_saveBuf ) {
		return qfalse;
	}
	g_saveSize = 0;
	g_saveError = qfalse;

	G_SaveBytes( &header, sizeof( header ) );
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse || ent->freeAfterEvent || ent->neverFree || !ent->classname ) {
			continue;
		}
		if ( !strcmp( ent->classname, "bodyque" ) || !strcmp( ent->classname, "tempEntity" ) ) {
			continue;
		}
		G_SaveBytes( &i, sizeof( i ) );
		G_WriteEntity( ent );
		numEntities++;
	}
	i = -1;
	G_SaveBytes( &i, sizeof( i ) );
	( (saveHeader_t *)g_saveBuf )->info.numEntities = numEntities;

	// the scripts' variables and threads
	G_ScriptWriteGame();

	if ( g_saveError ) {
		G_Printf( S_COLOR_YELLOW "G_SaveGame: the game is too big to save\n" );
		trap_HeapFree( g_saveBuf );
		return qfalse;
	}

	Com_sprintf( path, sizeof( path ), "save/%s.svg", name );
	if ( trap_FS_FOpenFile( path, &f, FS_WRITE ) < 0 ) {
		G_Printf( S_COLOR_YELLOW "G_SaveGame: can't write %s\n", path );
		trap_HeapFree( g_saveBuf );
		return qfalse;
	}
	trap_FS_Write( g_saveBuf, g_saveSize, f );
	trap_FS_FCloseFile( f );
	trap_HeapFree( g_saveBuf );
	G_Printf( "Saved %s (%d entities).\n", path, numEntities );
	return qtrue;
}

/*
================
G_ReadEntity

an entity back into its slot over what the map spawned there, which keeps
its own bodies; the rest of the map's entities go
================
*/
static void G_ReadEntity( gentity_t *ent, const gentity_t *fresh, qboolean freshUsed ) {
	const saveField_t *f;
	gentity_t saved;
	char *s;
	void *p;
	int i, n;

	G_LoadBytes( &saved, sizeof( saved ) );

	// what the map made there that the save makes anew keeps its heap
	saved.clipModel = ent->clipModel;
	for ( i = 0; i < (int)ARRAY_LEN( g_entityFields ); i++ ) {
		f = &g_entityFields[i];
		p = NULL;
		switch ( f->type ) {
			case SF_STRING:
				p = G_LoadString();
				break;
			case SF_FUNCTION:
				s = G_LoadString();
				if ( s ) {
					p = G_FuncForName( s );
					if ( !p ) {
						G_Printf( S_COLOR_YELLOW "G_LoadGame: no function %s\n", s );
					}
				}
				break;
			case SF_ENTITY:
				G_LoadBytes( &n, sizeof( n ) );
				p = ( n >= 0 && n < MAX_GENTITIES ) ? &g_entities[n] : NULL;
				break;
			case SF_ITEM:
				s = G_LoadString();
				p = s ? BG_FindItemByClassname( s ) : NULL;
				break;
			case SF_HEAP:
				G_LoadBytes( &n, sizeof( n ) );
				// the map's, if it made the same thing there
				p = ( n && freshUsed ) ? *(void **)( (const byte *)fresh + f->ofs ) : NULL;
				break;
		}
		*(void **)( (byte *)&saved + f->ofs ) = p;
	}
	saved.player = NULL;

	// its script object, instead of the map's
	ScriptObject_Free( &ent->scriptObject );
	memset( &saved.scriptObject, 0, sizeof( saved.scriptObject ) );
	G_ScriptReadEntity( &saved, ent - g_entities );
	*ent = saved;
}

/*
================
G_LoadGame

RTCW's: the saved game over the map spawned for it, once the first player
is in; qfalse if there's nothing to load
================
*/
qboolean G_LoadGame( void ) {
	static saveHeader_t header;
	static gentity_t fresh[MAX_GENTITIES];
	static int freshUsed[MAX_GENTITIES];		// 1 the map spawned it, 2 a saved one took the slot
	char name[MAX_QPATH];
	savePlayer_t *sp;
	gentity_t *ent;
	gplayer_t *player;
	fileHandle_t f;
	int i, num, len, ragdoll;

	if ( !trap_Cvar_VariableIntegerValue( "savegame_loading" ) ) {
		return qfalse;
	}
	trap_Cvar_Set( "savegame_loading", "0" );
	trap_Cvar_VariableStringBuffer( "savegame_filename", name, sizeof( name ) );

	len = trap_FS_FOpenFile( va( "save/%s.svg", name ), &f, FS_READ );
	if ( len < (int)sizeof( header ) || len > MAX_SAVEGAME_SIZE ) {
		if ( len >= 0 ) {
			trap_FS_FCloseFile( f );
		}
		return qfalse;
	}
	g_saveBuf = trap_HeapMalloc( len );
	trap_FS_Read( g_saveBuf, len, f );
	trap_FS_FCloseFile( f );
	g_saveSize = len;
	g_saveOfs = 0;
	g_saveError = qfalse;

	G_LoadBytes( &header, sizeof( header ) );
	if ( header.info.magic != SAVEGAME_MAGIC || header.info.version != SAVEGAME_VERSION || header.info.entitySize != (int)sizeof( gentity_t ) ) {
		trap_HeapFree( g_saveBuf );
		return qfalse;
	}

	// no destroy() for the map's script objects the save replaces
	G_ScriptLoading( qtrue );

	// what the map spawned, kept aside: its bodies go to the saved ones
	for ( i = MAX_CLIENTS; i < MAX_GENTITIES; i++ ) {
		fresh[i] = g_entities[i];
		freshUsed[i] = g_entities[i].inuse && g_entities[i].classname && !g_entities[i].neverFree;
		if ( g_entities[i].inuse ) {
			G_UnlinkEntity( &g_entities[i] );
		}
	}

	// the saved entities
	for ( ;; ) {
		G_LoadBytes( &num, sizeof( num ) );
		if ( num < 0 || g_saveError ) {
			break;
		}
		if ( num < MAX_CLIENTS || num >= MAX_GENTITIES ) {
			g_saveError = qtrue;
			break;
		}
		ent = &g_entities[num];
		G_ReadEntity( ent, &fresh[num], freshUsed[num] != 0 );
		if ( freshUsed[num] && ( !ent->classname || strcmp( fresh[num].classname, ent->classname ) ) ) {
			// not what the map made there: its body goes, none of its heap is this one's
			gentity_t *saved = ent;
			struct rigidBody_s *body = saved->body;

			saved->body = fresh[num].body;
			G_PhysicsFreeBody( saved );
			saved->body = NULL;
			saved->characterHistory = NULL;
			saved->partClipModels = NULL;
			(void)body;
		}
		freshUsed[num] = 2;		// taken
		if ( num >= level.num_entities ) {
			level.num_entities = num + 1;
		}
	}

	// the map's entities the game had removed go
	for ( i = MAX_CLIENTS; i < MAX_GENTITIES; i++ ) {
		if ( freshUsed[i] == 1 ) {
			gentity_t *keep = &g_entities[i];

			*keep = fresh[i];
			G_FreeEntity( keep );
		}
	}

	// the scripts' variables and threads, as they were
	if ( !g_saveError ) {
		G_ScriptReadGame();
	}
	G_ScriptLoading( qfalse );

	// in the world again
	for ( i = MAX_CLIENTS; i < level.num_entities; i++ ) {
		ent = &g_entities[i];
		if ( !ent->inuse ) {
			continue;
		}
		ragdoll = ent->ragdoll != NULL;
		ent->ragdoll = NULL;
		if ( ent->characterDef && !ent->characterHistory ) {
			ent->characterHistory = G_AllocCharacterHistory();
		}
		if ( ent->body ) {
			G_PhysicsSetPose( ent, ent->r.currentOrigin, ent->r.currentAngles );
		}
		if ( ent->r.linked ) {
			ent->r.linked = qfalse;
			G_LinkEntity( ent );
		}
		if ( ragdoll ) {
			G_StartRagdoll( ent );		// limp again, from its last pose
		}
	}

	// the players where they were, with what they had
	for ( i = 0; i < level.maxplayers && i < MAX_CLIENTS; i++ ) {
		sp = &header.players[i];
		player = &level.players[i];
		ent = &g_entities[i];
		if ( !sp->valid || player->pers.connected != CON_CONNECTED || !ent->inuse ) {
			continue;
		}
		G_SetOrigin( ent, sp->origin );
		VectorCopy( sp->origin, player->ps.origin );
		VectorCopy( sp->velocity, player->ps.velocity );
		SetPlayerViewAngle( ent, sp->viewangles );
		ent->health = sp->health;
		memcpy( player->ps.stats, sp->stats, sizeof( sp->stats ) );
		player->ps.stats[STAT_HEALTH] = sp->health;
		memcpy( player->ps.ammo, sp->ammo, sizeof( sp->ammo ) );
		memcpy( player->ps.ammoClip, sp->ammoClip, sizeof( sp->ammoClip ) );
		memcpy( player->ps.powerups, sp->powerups, sizeof( sp->powerups ) );
		player->ps.weapon = sp->weapon;
		player->ps.weaponstate = WEAPON_READY;
		G_RestoreInventory( player, &sp->inventory );
		G_QuestsSend( ent, qtrue );		// (begun before they were loaded)
		player->ps.eFlags ^= EF_TELEPORT_BIT;
		G_LinkEntity( ent );
	}

	level.numSecrets = header.info.numSecrets;
	level.secretsFound = header.info.secretsFound;

	trap_HeapFree( g_saveBuf );
	if ( g_saveError ) {
		G_Printf( S_COLOR_YELLOW "G_LoadGame: save/%s.svg is damaged\n", name );
		return qfalse;
	}
	G_Printf( "Loaded save/%s.svg.\n", name );
	return qtrue;
}

/*
================
Svcmd_SaveGame_f

savegame [name]: saves the campaign ("quick" by default); as in Daikatana
a save costs a Save Gem, unless g_unlimitedSaves
================
*/
void Svcmd_SaveGame_f( void ) {
	char name[MAX_QPATH];
	gentity_t *payer = NULL;
	int i;

	trap_Argv( 1, name, sizeof( name ) );
	if ( !name[0] ) {
		Q_strncpyz( name, "quick", sizeof( name ) );
	}
	if ( strchr( name, '/' ) || strchr( name, '\\' ) || strchr( name, '.' ) ) {
		G_Printf( "A saved game's name is a plain name.\n" );
		return;
	}
	if ( !trap_Cvar_VariableIntegerValue( "g_unlimitedSaves" ) ) {
		for ( i = 0; i < level.maxplayers; i++ ) {
			if ( level.players[i].pers.connected == CON_CONNECTED && G_InventoryCount( &g_entities[i], "item_savegem" ) ) {
				payer = &g_entities[i];
				break;
			}
		}
		if ( !payer ) {
			trap_SendServerCommand( -1, "cp \"You need a Save Gem\"" );
			return;
		}
		G_InventoryRemove( payer, "item_savegem", 1 );
	}
	if ( !G_SaveGame( name ) && payer ) {
		G_InventoryAdd( payer, "item_savegem", 1, 0 );		// not spent
		return;
	}
	trap_SendServerCommand( -1, "cp \"Game saved\"" );
}
