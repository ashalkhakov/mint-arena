/*
===========================================================================
Copyright (C) 2026 the Daikatana restoration project

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
//
// g_aiscript.c -- Daikatana's action scripts (action.cpp, the maps'
// cin/aiscripts/<map>.sca) as Doom 3 scripts: ./dk converts each map's into
// ai/maps/<map>.script (tools/aiscript/sca_convert.py), each script a
// function ai_<name>( entity actor ), compiled with the map's. What starts
// them is here: trigger_script, a monster_path_corner's "aiscript" when a
// monster gets there, a monster's own "aiscript" as it spawns, the map's
// Level_start (ai_level_start).

#include "g_local.h"

void InitTrigger( gentity_t *self );		// g_trigger.c
void SP_path_corner( gentity_t *self );	// g_mover.c

/*
================
G_RunAiScript

Daikatana's script of that name (any case), actor acting, in a thread of
its own: now, or from the next frame when delayed (as the map spawns).
qfalse if the map has no such script.
================
*/
qboolean G_RunAiScript( const char *name, gentity_t *actor, qboolean delayed ) {
	char				funcName[MAX_QPATH];
	const function_t	*func;
	scriptThread_t		*thread;

	Com_sprintf( funcName, sizeof( funcName ), "ai_%s", name );
	Q_strlwr( funcName );
	func = Program_FindFunction( funcName );
	if ( !func ) {
		G_Printf( S_COLOR_YELLOW "WARNING: no action script %s (./dk converts the maps' .sca)\n", name );
		return qfalse;
	}
	G_DPrintf( "aiscript %s, %s acting\n", name, actor ? actor->classname : "nobody" );

	thread = Thread_New();
	Thread_SetThreadName( thread, funcName );
	Interp_Reset( &thread->interpreter );
	if ( func->type->numParms == 1 ) {
		Interp_PushEntity( &thread->interpreter, actor );
	}
	Interp_EnterFunction( &thread->interpreter, func, qfalse );
	if ( delayed || level.spawning ) {
		Thread_DelayedStart( thread, 0 );
	} else if ( Thread_Start( thread ) ) {
		thread->removed = qtrue;
	}
	return qtrue;
}

/*
================
G_AiScriptLevelStart

the map's Level_start, once it has spawned
================
*/
void G_AiScriptLevelStart( void ) {
	if ( Program_FindFunction( "ai_level_start" ) ) {
		G_RunAiScript( "level_start", NULL, qtrue );
	}
}

/*
===============================================================================

trigger_script

===============================================================================
*/

#define SCRIPT_MULTIPLE		1		// again after "wait"; else once
#define SCRIPT_NOTOUCH		2		// only used, not touched

/*
================
Use_TriggerScript

its "aiscript" (who touched or used it acting), or its "cinescript"
(Daikatana's cinematic: cine/<name>.script's <name>::play)
================
*/
void Use_TriggerScript( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	const char *name;

	if ( level.time < self->pain_debounce_time ) {
		return;
	}
	name = G_ScriptGetSpawnArg( self, "cinescript", "" );
	if ( name[0] ) {
		if ( g_gametype.integer == GT_CAMPAIGN ) {
			G_ScriptCallFunction( va( "%s::play", name ), activator );
		}
	} else {
		name = G_ScriptGetSpawnArg( self, "aiscript", "" );
		if ( name[0] ) {
			G_RunAiScript( name, other && G_IsMonster( other ) ? other : NULL, qfalse );
		}
	}

	if ( !( self->spawnflags & SCRIPT_MULTIPLE ) ) {
		self->use = NULL;
		self->touch = NULL;
		self->think = G_FreeEntity;
		self->nextthink = level.time + FRAMETIME;
	} else {
		self->pain_debounce_time = level.time + self->wait * 1000;
	}
}

void Touch_TriggerScript( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if ( !other->player || other->health <= 0 ) {
		return;
	}
	Use_TriggerScript( self, other, other );
}

/*QUAKED trigger_script (.5 .5 .5) ? MULTIPLE NOTOUCH
Daikatana's: touched by a player, or used, it runs its "aiscript" (an
action script of the map) or plays its "cinescript". Once, unless MULTIPLE
("wait" seconds between).
*/
void SP_trigger_script( gentity_t *ent ) {
	G_SpawnFloat( "wait", "1", &ent->wait );
	InitTrigger( ent );
	ent->use = Use_TriggerScript;
	if ( !( ent->spawnflags & SCRIPT_NOTOUCH ) ) {
		ent->touch = Touch_TriggerScript;
	}
	trap_LinkEntity( ent );
}

/*
===============================================================================

monster_path_corner, and monsters' own scripts

===============================================================================
*/

// a monster got here (monster_base's state_PathFollow): its script runs
void Use_PathCornerScript( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	G_RunAiScript( G_ScriptGetSpawnArg( self, "aiscript", "" ), activator, qfalse );
}

/*QUAKED monster_path_corner (.5 .3 0) (-8 -8 -8) (8 8 8)
Daikatana's: a corner of a monster's path, "target" the next. A monster
getting here runs its "aiscript".
*/
void SP_monster_path_corner( gentity_t *ent ) {
	char *script;

	SP_path_corner( ent );
	if ( ent->inuse && G_SpawnString( "aiscript", "", &script ) && script[0] ) {
		ent->use = Use_PathCornerScript;
	}
}
