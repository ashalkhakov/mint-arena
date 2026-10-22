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
// g_campaign.c -- the campaign (GT_CAMPAIGN): Daikatana's single player and
// coop levels, a level being several maps (e1m1a, e1m1b...) walked between
// by trigger_changelevel
//
// Its entities are Daikatana's, as bsp-sekai keeps them for the campaign:
// what Quake 2 and Daikatana do with them, done the Quake 3 way.

#include "g_local.h"

void	InitTrigger( gentity_t *self );		// g_trigger.c
void	InitMover( gentity_t *ent );		// g_mover.c

// Daikatana's skill spawnflags (p_user.h): not on easy, medium, hard
#define DK_SPAWNFLAG_NOT_EASY		0x1000

// trigger_changelevel's (triggers.cpp)
#define CHANGELEVEL_INTERMISSION	0x0001
#define CHANGELEVEL_THE_END			0x0008

// a key's (artifacts.cpp)
#define KEY_USE_ONCE				0x0001		// gone from the inventory once used
#define KEY_UNLOCK					0x0002		// used, what it opens needs it no more

// func_wall's (Quake 2's)
#define WALL_TRIGGER_SPAWN			1
#define WALL_TOGGLE					2
#define WALL_START_ON				4

/*
================
G_CampaignSkipEntity

A Daikatana entity not for this skill (g_skill: 0 easy, 1 medium, 2 hard).
================
*/
qboolean G_CampaignSkipEntity( const gentity_t *ent ) {
	int skill;

	if ( g_gametype.integer != GT_CAMPAIGN ) {
		return qfalse;
	}
	skill = Com_Clamp( 0, 2, g_skill.integer );
	return ( ent->spawnflags & ( DK_SPAWNFLAG_NOT_EASY << skill ) ) != 0;
}

/*
===============================================================================

	Spawn points

===============================================================================
*/

/*QUAKED info_player_coop (1 0 1) (-16 -16 -24) (16 16 32)
Daikatana's: where the other players of a coop game start, by the
"targetname" a changelevel names, as info_player_start's.
*/
void SP_info_player_coop( gentity_t *ent ) {
	int i;

	G_SpawnInt( "nobots", "0", &i );
	if ( i ) {
		ent->flags |= FL_NO_BOTS;
	}
}

static gentity_t *G_CampaignSpot( const char *classname, const char *targetname ) {
	gentity_t *spot = NULL;

	while ( ( spot = G_Find( spot, FOFS( classname ), classname ) ) != NULL ) {
		if ( targetname ) {
			if ( spot->targetname && !Q_stricmp( spot->targetname, targetname ) && !SpotWouldTelefrag( spot ) ) {
				return spot;
			}
		} else if ( !spot->targetname && !SpotWouldTelefrag( spot ) ) {
			return spot;
		}
	}
	return NULL;
}

/*
================
G_CampaignSpawnPoint

Where a player starts in the campaign: the start the changelevel that
brought them named (g_spawnTarget), a coop start of that name, the
unnamed start, any start. NULL for none.
================
*/
gentity_t *G_CampaignSpawnPoint( vec3_t origin, vec3_t angles ) {
	const char *target = g_spawnTarget.string[0] ? g_spawnTarget.string : NULL;
	gentity_t *spot = NULL;

	if ( target ) {
		spot = G_CampaignSpot( "info_player_start", target );
		if ( !spot ) {
			spot = G_CampaignSpot( "info_player_coop", target );
		}
	}
	if ( !spot ) {
		spot = G_CampaignSpot( "info_player_start", NULL );
	}
	if ( !spot ) {
		spot = G_Find( NULL, FOFS( classname ), "info_player_start" );
	}
	if ( !spot ) {
		spot = G_Find( NULL, FOFS( classname ), "info_player_coop" );
	}
	if ( !spot ) {
		return NULL;
	}

	VectorCopy( spot->s.origin, origin );
	origin[2] += 9;
	VectorCopy( spot->s.angles, angles );
	return spot;
}

/*
===============================================================================

	Targets: Quake 2's way, with delay, message and killtarget

===============================================================================
*/

void G_CampaignDelayedUse( gentity_t *ent ) {
	G_CampaignUseTargets( ent->parent, ent->activator );
	G_FreeEntity( ent );
}

/*
================
G_CampaignUseTargets

Quake 2's G_UseTargets: after "delay" seconds the "message" to the
activator, "killtarget"s removed (a monster killed), the "target"s used.
================
*/
void G_CampaignUseTargets( gentity_t *ent, gentity_t *activator ) {
	gentity_t *t;

	if ( !ent ) {
		return;
	}

	if ( ent->delay > 0 && ent->think != G_CampaignDelayedUse ) {
		// a temporary entity does it later, as Quake 2's DelayedUse
		t = G_Spawn();
		t->classname = "DelayedUse";
		t->nextthink = level.time + ent->delay * 1000;
		t->think = G_CampaignDelayedUse;
		t->activator = activator;
		t->parent = ent;
		t->delay = 0;
		return;
	}

	if ( ent->message && activator && activator->player ) {
		trap_SendServerCommand( activator - g_entities, va( "cp \"%s\"", ent->message ) );
	}

	if ( ent->killtarget ) {
		t = NULL;
		while ( ( t = G_Find( t, FOFS( targetname ), ent->killtarget ) ) != NULL ) {
			if ( G_IsMonster( t ) && t->health > 0 ) {
				// a monster dies of it, as it would (a turret blows up)
				G_Damage( t, ent, activator, NULL, NULL, t->health + 1000, DAMAGE_NO_PROTECTION, MOD_UNKNOWN );
			} else {
				G_FreeEntity( t );
			}
			if ( !ent->inuse ) {
				return;
			}
		}
	}

	G_UseTargets( ent, activator );
}

/*QUAKED trigger_relay (.5 .5 .5) (-8 -8 -8) (8 8 8)
Daikatana's (Quake 2's): when used, uses its targets, after "delay"
seconds, with "message" and "killtarget".
*/
void Use_TriggerRelay( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	G_CampaignUseTargets( self, activator );
}

void SP_trigger_relay( gentity_t *self ) {
	self->use = Use_TriggerRelay;
}

/*QUAKED trigger_counter (.5 .5 .5) ? NOMESSAGE
Daikatana's (Quake 2's): uses its targets the "count"th time it's used
(2 by default), saying how many are left before.
*/
void Use_TriggerCounter( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( self->count <= 0 ) {
		return;
	}
	self->count--;
	if ( self->count ) {
		if ( !( self->spawnflags & 1 ) && activator && activator->player ) {
			trap_SendServerCommand( activator - g_entities, va( "cp \"%i more to go...\"", self->count ) );
		}
		return;
	}
	if ( !( self->spawnflags & 1 ) && activator && activator->player ) {
		trap_SendServerCommand( activator - g_entities, "cp \"Sequence completed!\"" );
	}
	G_CampaignUseTargets( self, activator );
}

void SP_trigger_counter( gentity_t *self ) {
	if ( !self->count ) {
		self->count = 2;
	}
	self->use = Use_TriggerCounter;
}

/*
===============================================================================

	Secrets

===============================================================================
*/

/*QUAKED trigger_secret (.5 .5 .5) ?
Daikatana's: a secret found when touched, once ("message", "sound").
*/
void Touch_TriggerSecret( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if ( !other->player || other->health <= 0 ) {
		return;
	}
	level.secretsFound++;
	trap_SendServerCommand( other - g_entities, va( "cp \"%s\"", self->message ? self->message : "You found a secret!" ) );
	if ( self->noise_index ) {
		G_Sound( other, CHAN_AUTO, self->noise_index );
	}
	G_CampaignUseTargets( self, other );
	G_FreeEntity( self );
}

void SP_trigger_secret( gentity_t *self ) {
	char *sound;

	InitTrigger( self );
	if ( G_SpawnString( "sound", "", &sound ) && sound[0] ) {
		char path[MAX_QPATH];

		// Daikatana's are under sounds/, with backslashes
		char *c;

		Com_sprintf( path, sizeof( path ), "sounds/%s", sound );
		for ( c = path; *c; c++ ) {
			if ( *c == '\\' ) {
				*c = '/';
			}
		}
		self->noise_index = G_SoundIndex( path );
	}
	self->touch = Touch_TriggerSecret;
	level.numSecrets++;
	G_LinkEntity( self );
}

/*
===============================================================================

	Walls

===============================================================================
*/

void Use_FuncWall( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( self->r.linked ) {
		G_UnlinkEntity( self );
	} else {
		G_LinkEntity( self );
	}
	if ( !( self->spawnflags & WALL_TOGGLE ) ) {
		self->use = NULL;
	}
}

/*QUAKED func_wall (0 .5 .8) ? TRIGGER_SPAWN TOGGLE START_ON
Daikatana's (Quake 2's): a wall. TRIGGER_SPAWN: there once used; TOGGLE:
used, there and gone; START_ON: with them, there at first.
*/
void SP_func_wall( gentity_t *self ) {
	G_SetBrushModel( self, self->model );
	InitMover( self );
	VectorCopy( self->s.origin, self->s.pos.trBase );
	VectorCopy( self->s.origin, self->r.currentOrigin );

	// just a wall
	if ( !( self->spawnflags & ( WALL_TRIGGER_SPAWN | WALL_TOGGLE | WALL_START_ON ) ) ) {
		return;
	}
	if ( self->spawnflags & WALL_START_ON ) {
		self->spawnflags |= WALL_TOGGLE;
	}
	self->use = Use_FuncWall;
	if ( !( self->spawnflags & WALL_START_ON ) ) {
		G_UnlinkEntity( self );
	}
}

/*
===============================================================================

	Changing levels

===============================================================================
*/

/*
================
G_ChangeLevel

On to a map of the level (or the next level): the players go along
(G_SavePersistant), starting at the spawn point named target.
================
*/
void G_ChangeLevel( const char *map, const char *target ) {
	fileHandle_t f;
	char path[MAX_QPATH];

	if ( level.changingLevel ) {
		return;
	}
	Com_sprintf( path, sizeof( path ), "maps/%s.bsp", map );
	if ( trap_FS_FOpenFile( path, &f, FS_READ ) <= 0 ) {
		G_Printf( S_COLOR_YELLOW "trigger_changelevel: no map %s\n", map );
		trap_SendServerCommand( -1, va( "cp \"%s isn't there\"", map ) );
		return;
	}
	trap_FS_FCloseFile( f );

	level.changingLevel = qtrue;
	trap_Cvar_Set( "g_spawnTarget", target ? target : "" );
	trap_Cvar_Set( "g_persistant", "1" );		// the next map takes what's carried
	G_SavePersistant( map );
	trap_Cmd_ExecuteText( EXEC_INSERT, va( "%s %s\n", g_cheats.integer ? "devmap" : "map", map ) );
}

/*QUAKED trigger_changelevel (.5 .5 .5) ? INTERMISSION REQUIRES_SUPERFLY REQUIRES_MIKIKO THE_END
Daikatana's: touched by a player, on to "map", starting at the spawn point
whose targetname is "target".
*/
void Touch_TriggerChangelevel( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if ( !other->player || other->health <= 0 || other->player->sess.sessionTeam == TEAM_SPECTATOR ) {
		return;
	}
	if ( !self->map || !self->map[0] || !G_CampaignKeyCheck( self, other ) ) {
		return;
	}
	G_ChangeLevel( self->map, self->target );
}

void SP_trigger_changelevel( gentity_t *self ) {
	char *map;

	InitTrigger( self );
	G_SpawnString( "map", "", &map );
	if ( !map[0] ) {
		G_Printf( S_COLOR_YELLOW "trigger_changelevel at %s with no map\n", vtos( self->r.absmin ) );
	}
	self->map = G_NewString( map );
	// its target names the next map's spawn point, not an entity here
	self->touch = Touch_TriggerChangelevel;
	G_LinkEntity( self );
	G_DPrintf( "trigger_changelevel to %s (%s) at %s .. %s\n", self->map, self->target ? self->target : "", vtos( self->r.absmin ), vtos( self->r.absmax ) );
}

/*
===============================================================================

	The inventory: Doom 3's idInventory's items, Daikatana's keys, in the
	inventory grid

===============================================================================
*/

// an item's number by its def name, 0 for none
static int G_InventoryItemNum( const char *item ) {
	gitem_t *it = item ? BG_FindItemByClassname( item ) : NULL;

	return it ? BG_ItemNumForItem( it ) : 0;
}

/*
================
G_InventoryCount / G_InventoryAdd / G_InventoryRemove

What a player carries of an item, by its def name, in the inventory grid
(bg_inventory.c). Daikatana's key flags go with the item's slots.
================
*/
int G_InventoryCount( const gentity_t *ent, const char *item ) {
	int itemNum = G_InventoryItemNum( item );

	if ( !ent || !ent->player || !itemNum ) {
		return 0;
	}
	return BG_InvCount( &ent->player->ps, itemNum );
}

int G_InventoryAdd( gentity_t *ent, const char *item, int count, int flags ) {
	int itemNum = G_InventoryItemNum( item ), added, slot;

	if ( !ent || !ent->player ) {
		return 0;
	}
	if ( !itemNum ) {
		G_Printf( S_COLOR_YELLOW "G_InventoryAdd: no item %s\n", item ? item : "" );
		return 0;
	}
	added = BG_InvAdd( &ent->player->ps, itemNum, count, 0 );
	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( ent->player->ps.invItem[slot] == itemNum ) {
			ent->player->pers.invFlags[slot] = flags;
		}
	}
	if ( added < count ) {
		G_Printf( "G_InventoryAdd: no room for %d %s of %s\n", count - added, item, ent->player->pers.netname );
	}
	return added;
}

void G_InventoryRemove( gentity_t *ent, const char *item, int count ) {
	int itemNum = G_InventoryItemNum( item );

	if ( !ent || !ent->player || !itemNum ) {
		return;
	}
	BG_InvRemove( &ent->player->ps, itemNum, count );
}

static const char *G_ItemName( const char *classname ) {
	gitem_t *item = BG_FindItemByClassname( classname );

	return item && item->pickup_name ? item->pickup_name : classname;
}

/*
================
G_CampaignKeyCheck

Daikatana's com_DoKeyFunctions: an entity with a "keyname" works only for
whoever has that item, saying what's needed (its "message", or "You need
the..."). A key used once is gone; one that unlocks leaves it, and what it
targets, needing it no more. qtrue when it may go on.
================
*/
qboolean G_CampaignKeyCheck( gentity_t *self, gentity_t *activator ) {
	int keySlot, keyFlags;
	gentity_t *t;

	if ( !self->keyname || !self->keyname[0] ) {
		return qtrue;
	}
	if ( !activator || !activator->player ) {
		return qfalse;
	}
	keySlot = BG_InvFindSlot( &activator->player->ps, G_InventoryItemNum( self->keyname ) );
	if ( keySlot < 0 ) {
		if ( level.time >= self->pain_debounce_time ) {
			self->pain_debounce_time = level.time + 2000;
			trap_SendServerCommand( activator - g_entities, va( "cp \"%s %s\"",
									self->message ? self->message : "You need the", G_ItemName( self->keyname ) ) );
		}
		return qfalse;
	}

	keyFlags = activator->player->pers.invFlags[keySlot];
	if ( keyFlags & KEY_UNLOCK ) {
		// what it opens opens freely now
		if ( self->target ) {
			t = NULL;
			while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
				t->keyname = NULL;
			}
		}
		self->keyname = NULL;
	}
	if ( keyFlags & KEY_USE_ONCE ) {
		BG_InvRemoveSlot( &activator->player->ps, keySlot, 1 );
	}
	return qtrue;
}

/*QUAKED trigger_remove_inventory_item (.5 .5 .5) ?
Daikatana's: takes the "item" from whoever touches it (or uses it).
*/
void Use_TriggerRemoveItem( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	if ( activator && activator->player && G_InventoryCount( activator, self->keyname ) ) {
		G_InventoryRemove( activator, self->keyname, G_InventoryCount( activator, self->keyname ) );
	}
}

void Touch_TriggerRemoveItem( gentity_t *self, gentity_t *other, trace_t *trace ) {
	Use_TriggerRemoveItem( self, other, other );
}

void SP_trigger_remove_inventory_item( gentity_t *self ) {
	char *item;

	G_SpawnString( "item", "", &item );
	self->keyname = G_NewString( item );
	if ( self->model && self->model[0] == '*' ) {
		InitTrigger( self );
		self->touch = Touch_TriggerRemoveItem;
		G_LinkEntity( self );
	}
	self->use = Use_TriggerRemoveItem;
}
