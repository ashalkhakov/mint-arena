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
#define PERSISTANT_VERSION	1

// what a player takes along
typedef struct {
	int		valid;
	int		health;
	int		stats[MAX_STATS];
	int		ammo[MAX_WEAPONS];
	int		ammoClip[MAX_WEAPONS];
	int		weapon;
	playerInventory_t	inventory;
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
		p->inventory = player->pers.inventory;
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

	if ( player->pers.persistantLoaded ) {
		return qfalse;
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
	player->pers.inventory = p->inventory;
	if ( BG_HasWeapon( &player->ps, p->weapon ) ) {
		player->pers.persistantWeapon = p->weapon;
	}
	return qtrue;
}
