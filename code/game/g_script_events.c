/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/
// g_script_events.c -- the events scripts call on entities (Doom 3's
// idEntity's, idMover's...), and the table of all events (Event.cpp's
// idEventDef list): what sys and the entities respond to, by name

#include "g_local.h"

#define MAX_EVENTS		512

static eventDef_t	eventDefs[ MAX_EVENTS ];
static int			numEventDefs;

/*
================
Event_AddEvent
================
*/
static eventDef_t *Event_AddEvent( const char *name, const char *formatspec, char returnType ) {
	eventDef_t	*ev;
	int			i;

	for ( i = 0; i < numEventDefs; i++ ) {
		ev = &eventDefs[ i ];
		if ( !strcmp( ev->name, name ) ) {
			if ( Q_stricmp( ev->formatspec ? ev->formatspec : "", formatspec ? formatspec : "" ) || ev->returnType != returnType ) {
				G_Error( "Event '%s' defined twice with different arguments or return type", name );
			}
			return ev;
		}
	}

	if ( numEventDefs >= MAX_EVENTS ) {
		G_Error( "Event_AddEvent: more than %d events", MAX_EVENTS );
	}
	if ( formatspec && strlen( formatspec ) > D_EVENT_MAXARGS ) {
		G_Error( "Event '%s' has more than %d arguments", name, D_EVENT_MAXARGS );
	}

	ev = &eventDefs[ numEventDefs++ ];
	ev->name = name;
	ev->formatspec = formatspec;
	ev->returnType = returnType;
	ev->sysFunc = NULL;
	ev->entityFunc = NULL;
	return ev;
}

/*
================
Event_Init

the events by name, from what sys and the entities respond to
================
*/
void Event_Init( void ) {
	const sysEventCallback_t	*s;
	const entityEventCallback_t	*e;

	numEventDefs = 0;
	for ( s = threadEventCallbacks; s->name; s++ ) {
		Event_AddEvent( s->name, s->formatspec, s->returnType )->sysFunc = s->func;
	}
	for ( e = entityEventCallbacks; e->name; e++ ) {
		Event_AddEvent( e->name, e->formatspec, e->returnType )->entityFunc = e->func;
	}
}

/*
================
Event_FindEvent
================
*/
const eventDef_t *Event_FindEvent( const char *name ) {
	int i;

	if ( !numEventDefs ) {
		Event_Init();
	}
	for ( i = 0; i < numEventDefs; i++ ) {
		if ( !strcmp( eventDefs[ i ].name, name ) ) {
			return &eventDefs[ i ];
		}
	}
	return NULL;
}

/*
================
Event_EntityRespondsTo
================
*/
qboolean Event_EntityRespondsTo( const gentity_t *ent, const eventDef_t *ev ) {
	return ev->entityFunc != NULL;
}

/***********************************************************************

  idEntity's

***********************************************************************/

static const char *EntityName( const gentity_t *self ) {
	if ( self->player ) {
		return va( "player%d", (int)( self - g_entities ) + 1 );
	}
	if ( self->targetname ) {
		return self->targetname;
	}
	return va( "%s_%d", self->classname, (int)( self - g_entities ) );
}

static void Event_GetName( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnString( EntityName( self ) );
}

static void Event_SetName( gentity_t *self, const intptr_t *args ) {
	if ( self->targetname ) {
		Program_SetEntity( self->targetname, NULL );
	}
	self->targetname = G_NewString( EVARG_STRING( 0 ) );
	Program_SetEntity( self->targetname, self );
}

static void Event_Remove( gentity_t *self, const intptr_t *args ) {
	if ( self->player || self->s.number == ENTITYNUM_WORLD ) {
		return;
	}
	G_FreeEntity( self );
}

static void Event_Activate( gentity_t *self, const intptr_t *args ) {
	G_ScriptActivate( self, EVARG_ENTITY( 0 ) );
}

static void Event_ActivateTargets( gentity_t *self, const intptr_t *args ) {
	G_UseTargets( self, EVARG_ENTITY( 0 ) );
}

static int NumTargets( gentity_t *self, int which, gentity_t **found ) {
	gentity_t	*t;
	int			num;

	if ( !self->target ) {
		return 0;
	}
	num = 0;
	t = NULL;
	while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
		if ( t == self ) {
			continue;
		}
		if ( num == which && found ) {
			*found = t;
		}
		num++;
	}
	return num;
}

static void Event_NumTargets( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( NumTargets( self, -1, NULL ) );
}

static void Event_GetTarget( gentity_t *self, const intptr_t *args ) {
	gentity_t *found = NULL;

	NumTargets( self, ( int )EVARG_FLOAT( 0 ), &found );
	Thread_ReturnEntity( found );
}

static void Event_RandomTarget( gentity_t *self, const intptr_t *args ) {
	gentity_t	*found = NULL;
	const char	*ignore = EVARG_STRING( 0 );
	gentity_t	*t;
	gentity_t	*list[ MAX_GENTITIES ];
	int			num;

	num = 0;
	t = NULL;
	if ( self->target ) {
		while ( ( t = G_Find( t, FOFS( targetname ), self->target ) ) != NULL ) {
			if ( t == self || ( ignore[ 0 ] && t->targetname && !Q_stricmp( t->targetname, ignore ) ) ) {
				continue;
			}
			list[ num++ ] = t;
		}
	}
	if ( num ) {
		found = list[ rand() % num ];
	}
	Thread_ReturnEntity( found );
}

static void Event_SetOwner( gentity_t *self, const intptr_t *args ) {
	gentity_t *owner = EVARG_ENTITY( 0 );

	self->r.ownerNum = owner ? owner->s.number : ENTITYNUM_NONE;
}

static void Event_GetWorldOrigin( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnVector( self->r.currentOrigin );
}

static void Event_SetWorldOrigin( gentity_t *self, const intptr_t *args ) {
	if ( self->player ) {
		// moved there at once (a teleport with no effect)
		VectorCopy( EVARG_VECTOR( 0 ), self->player->ps.origin );
		VectorClear( self->player->ps.velocity );
		self->player->ps.eFlags ^= EF_TELEPORT_BIT;
		G_SetOrigin( self, EVARG_VECTOR( 0 ) );
		G_LinkEntity( self );
		return;
	}
	G_SetOrigin( self, EVARG_VECTOR( 0 ) );
	if ( self->r.linked ) {
		trap_LinkEntity( self );
	}
}

static void Event_GetAngles( gentity_t *self, const intptr_t *args ) {
	if ( self->player ) {
		Thread_ReturnVector( self->player->ps.viewangles );
	} else {
		Thread_ReturnVector( self->r.currentAngles );
	}
}

static void Event_SetAngles( gentity_t *self, const intptr_t *args ) {
	if ( self->player ) {
		SetPlayerViewAngle( self, EVARG_VECTOR( 0 ) );
		return;
	}
	VectorCopy( EVARG_VECTOR( 0 ), self->s.apos.trBase );
	self->s.apos.trType = TR_STATIONARY;
	self->s.apos.trTime = 0;
	VectorClear( self->s.apos.trDelta );
	VectorCopy( EVARG_VECTOR( 0 ), self->r.currentAngles );
	VectorCopy( EVARG_VECTOR( 0 ), self->s.angles );
}

static void Event_GetLinearVelocity( gentity_t *self, const intptr_t *args ) {
	if ( G_IsMonster( self ) ) {
		Thread_ReturnVector( self->monster.velocity );
		return;
	}
	if ( self->player ) {
		Thread_ReturnVector( self->player->ps.velocity );
	} else {
		Thread_ReturnVector( self->s.pos.trDelta );
	}
}

static void Event_SetLinearVelocity( gentity_t *self, const intptr_t *args ) {
	if ( G_IsMonster( self ) ) {
		// thrown: off the ground, a leap until it lands
		VectorCopy( EVARG_VECTOR( 0 ), self->monster.velocity );
		if ( self->monster.velocity[2] > 0 ) {
			self->monster.flags &= ~MONF_ONGROUND;
			self->monster.flags |= MONF_LEAPING;
		}
		return;
	}
	if ( self->player ) {
		VectorCopy( EVARG_VECTOR( 0 ), self->player->ps.velocity );
		return;
	}
	BG_EvaluateTrajectory( &self->s.pos, level.time, self->s.pos.trBase );
	VectorCopy( EVARG_VECTOR( 0 ), self->s.pos.trDelta );
	self->s.pos.trTime = level.time;
	self->s.pos.trType = VectorCompare( self->s.pos.trDelta, vec3_origin ) ? TR_STATIONARY : TR_LINEAR;
}

static void Event_GetSize( gentity_t *self, const intptr_t *args ) {
	vec3_t size;

	VectorSubtract( self->s.maxs, self->s.mins, size );
	Thread_ReturnVector( size );
}

static void Event_SetSize( gentity_t *self, const intptr_t *args ) {
	VectorCopy( EVARG_VECTOR( 0 ), self->s.mins );
	VectorCopy( EVARG_VECTOR( 1 ), self->s.maxs );
	if ( self->r.linked ) {
		trap_LinkEntity( self );
	}
}

static void Event_GetMins( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnVector( self->s.mins );
}

static void Event_GetMaxs( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnVector( self->s.maxs );
}

static void Event_IsHidden( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnInt( ( self->r.svFlags & SVF_NOCLIENT ) != 0 );
}

static void Event_Hide( gentity_t *self, const intptr_t *args ) {
	self->r.svFlags |= SVF_NOCLIENT;
}

static void Event_Show( gentity_t *self, const intptr_t *args ) {
	self->r.svFlags &= ~SVF_NOCLIENT;
}

static void Event_Touches( gentity_t *self, const intptr_t *args ) {
	gentity_t	*ent = EVARG_ENTITY( 0 );
	vec3_t		absMin, absMax;

	if ( !ent || !self->r.linked || !ent->r.linked ) {
		Thread_ReturnInt( qfalse );
		return;
	}
	VectorCopy( self->r.absmin, absMin );
	VectorCopy( self->r.absmax, absMax );
	Thread_ReturnInt( absMin[0] <= ent->r.absmax[0] && absMax[0] >= ent->r.absmin[0]
		&& absMin[1] <= ent->r.absmax[1] && absMax[1] >= ent->r.absmin[1]
		&& absMin[2] <= ent->r.absmax[2] && absMax[2] >= ent->r.absmin[2] );
}

static void Event_StartSoundShader( gentity_t *self, const intptr_t *args ) {
	int channel = EVARG_INT( 1 );

	G_Sound( self, channel ? channel : CHAN_AUTO, G_SoundIndex( ( char * )EVARG_STRING( 0 ) ) );
	Thread_ReturnFloat( 0 );
}

static void Event_SetKey( gentity_t *self, const intptr_t *args ) {
	G_ScriptSetSpawnArg( self, EVARG_STRING( 0 ), EVARG_STRING( 1 ) );
}

static void Event_GetKey( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnString( G_ScriptGetSpawnArg( self, EVARG_STRING( 0 ), "" ) );
}

static void Event_GetIntKey( gentity_t *self, const intptr_t *args ) {
	// scripts get ints as floats
	Thread_ReturnFloat( atoi( G_ScriptGetSpawnArg( self, EVARG_STRING( 0 ), "0" ) ) );
}

static void Event_GetFloatKey( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( atof( G_ScriptGetSpawnArg( self, EVARG_STRING( 0 ), "0" ) ) );
}

static void Event_GetVectorKey( gentity_t *self, const intptr_t *args ) {
	vec3_t v;

	VectorClear( v );
	sscanf( G_ScriptGetSpawnArg( self, EVARG_STRING( 0 ), "0 0 0" ), "%f %f %f", &v[0], &v[1], &v[2] );
	Thread_ReturnVector( v );
}

static void Event_GetEntityKey( gentity_t *self, const intptr_t *args ) {
	const char	*entname;
	gentity_t	*ent;

	entname = G_ScriptGetSpawnArg( self, EVARG_STRING( 0 ), NULL );
	if ( !entname ) {
		Thread_ReturnEntity( NULL );
		return;
	}

	ent = G_ScriptFindEntity( entname );
	if ( !ent ) {
		G_Printf( S_COLOR_YELLOW "WARNING: Couldn't find entity '%s' specified in '%s' key in entity '%s'\n", entname, EVARG_STRING( 0 ), EntityName( self ) );
	}

	Thread_ReturnEntity( ent );
}

static void Event_DistanceTo( gentity_t *self, const intptr_t *args ) {
	gentity_t *ent = EVARG_ENTITY( 0 );

	if ( !ent ) {
		// just say it's really far away
		Thread_ReturnFloat( MAX_WORLD_COORD );
	} else {
		Thread_ReturnFloat( Distance( ent->r.currentOrigin, self->r.currentOrigin ) );
	}
}

static void Event_DistanceToPoint( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( Distance( EVARG_VECTOR( 0 ), self->r.currentOrigin ) );
}

static void Event_EntityWait( gentity_t *self, const intptr_t *args ) {
	scriptThread_t *thread = Thread_CurrentThread();

	if ( !thread ) {
		G_Error( "Event 'wait' called from outside thread" );
	}
	Thread_WaitSec( thread, EVARG_FLOAT( 0 ) );
}

static void Event_EntityWaitFrame( gentity_t *self, const intptr_t *args ) {
	scriptThread_t *thread = Thread_CurrentThread();

	if ( !thread ) {
		G_Error( "Event 'waitFrame' called from outside thread" );
	}
	Thread_WaitFrame( thread );
}

static void Event_HasFunction( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnInt( ScriptObject_GetFunction( &self->scriptObject, EVARG_STRING( 0 ) ) != NULL );
}

static void Event_CallFunction( gentity_t *self, const intptr_t *args ) {
	const function_t	*func;
	scriptThread_t		*thread;

	func = ScriptObject_GetFunction( &self->scriptObject, EVARG_STRING( 0 ) );
	if ( !func ) {
		G_Error( "Unknown function '%s' in '%s'", EVARG_STRING( 0 ), ScriptObject_GetTypeName( &self->scriptObject ) );
	}

	if ( func->type->numParms != 1 ) {
		G_Error( "Function '%s' has the wrong number of parameters for 'callFunction'", EVARG_STRING( 0 ) );
	}
	if ( !Type_Inherits( self->scriptObject.type, func->type->parmTypes[ 0 ] ) ) {
		G_Error( "Function '%s' is the wrong type for 'callFunction'", EVARG_STRING( 0 ) );
	}

	// function args will be invalid after this call
	thread = Thread_CurrentThread();
	Thread_CallObjectFunction( thread, self, func, qfalse );
}

static void Event_GetHealth( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( self->health );
}

static void Event_SetHealth( gentity_t *self, const intptr_t *args ) {
	self->health = ( int )EVARG_FLOAT( 0 );
	if ( self->player ) {
		self->player->ps.stats[ STAT_HEALTH ] = self->health;
	}
}

/***********************************************************************

  a player's (idPlayer's)

***********************************************************************/

static void Event_GetMaxHealth( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( self->player ? self->player->ps.stats[ STAT_MAX_HEALTH ] : self->health );
}

static void Event_GiveInventoryItem( gentity_t *self, const intptr_t *args ) {
	if ( self->player ) {
		G_InventoryAdd( self, EVARG_STRING( 0 ), 1, 0 );
	}
}

static void Event_RemoveInventoryItem( gentity_t *self, const intptr_t *args ) {
	if ( self->player ) {
		G_InventoryRemove( self, EVARG_STRING( 0 ), 1 );
	}
}

static void Event_HasInventoryItem( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( self->player ? G_InventoryCount( self, EVARG_STRING( 0 ) ) : 0 );
}

static void Event_CenterPrint( gentity_t *self, const intptr_t *args ) {
	if ( self->player ) {
		trap_SendServerCommand( self - g_entities, va( "cp \"%s\"", EVARG_STRING( 0 ) ) );
	}
}

/***********************************************************************

  a monster's (Doom 3's idAI's and idActor's): g_monster.c

***********************************************************************/

#define MONSTER_OR_RETURN( ret )	if ( !G_IsMonster( self ) ) { ret; return; }

static void Event_SetState( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	G_MonsterSetState( self, EVARG_STRING( 0 ) );
}

static void Event_GetState( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnString( "" ) );
	Thread_ReturnString( self->monster.state );
}

static void Event_FindEnemy( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnEntity( NULL ) );
	Thread_ReturnEntity( G_MonsterFindEnemy( self, EVARG_INT( 0 ) ) );
}

static void Event_SetEnemy( gentity_t *self, const intptr_t *args ) {
	gentity_t *enemy = EVARG_ENTITY( 0 );

	MONSTER_OR_RETURN( (void)0 );
	self->monster.enemy = enemy ? enemy->s.number : ENTITYNUM_NONE;
	if ( enemy ) {
		VectorCopy( enemy->r.currentOrigin, self->monster.lastEnemyPos );
		self->monster.lastEnemySeen = level.time;
	}
}

static void Event_ClearEnemy( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.enemy = ENTITYNUM_NONE;
}

static void Event_GetEnemy( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnEntity( NULL ) );
	Thread_ReturnEntity( G_MonsterEnemy( self ) );
}

static void Event_EnemyRange( gentity_t *self, const intptr_t *args ) {
	gentity_t *enemy;

	MONSTER_OR_RETURN( Thread_ReturnFloat( MAX_WORLD_COORD ) );
	enemy = G_MonsterEnemy( self );
	Thread_ReturnFloat( enemy ? Distance( enemy->r.currentOrigin, self->r.currentOrigin ) : MAX_WORLD_COORD );
}

static void Event_EnemyRange2D( gentity_t *self, const intptr_t *args ) {
	gentity_t	*enemy;
	vec3_t		d;

	MONSTER_OR_RETURN( Thread_ReturnFloat( MAX_WORLD_COORD ) );
	enemy = G_MonsterEnemy( self );
	if ( !enemy ) {
		Thread_ReturnFloat( MAX_WORLD_COORD );
		return;
	}
	VectorSubtract( enemy->r.currentOrigin, self->r.currentOrigin, d );
	d[2] = 0;
	Thread_ReturnFloat( VectorLength( d ) );
}

static void Event_GetEnemyPos( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnVector( vec3_origin ) );
	Thread_ReturnVector( self->monster.lastEnemyPos );
}

static void Event_CanSee( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnInt( 0 ) );
	Thread_ReturnInt( G_MonsterCanSee( self, EVARG_ENTITY( 0 ) ) );
}

static void StartMove( gentity_t *self, int command ) {
	self->monster.moveCommand = command;
	self->monster.moveStatus = MOVE_STATUS_MOVING;
	self->monster.flags &= ~MONF_BLOCKED;
	if ( self->monster.faceCommand == FACE_NONE ) {
		self->monster.faceCommand = FACE_MOVE;
	}
}

static void Event_MoveToEnemy( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	if ( !G_MonsterEnemy( self ) ) {
		self->monster.moveStatus = MOVE_STATUS_DEST_NOT_FOUND;
		return;
	}
	StartMove( self, MOVE_TO_ENEMY );
}

static void Event_MoveToPosition( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	VectorCopy( EVARG_VECTOR( 0 ), self->monster.moveDest );
	StartMove( self, MOVE_TO_POSITION );
}

static void Event_MoveToEntity( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.moveEntity = EVARG_ENTITY( 0 )->s.number;
	StartMove( self, MOVE_TO_ENTITY );
}

static void Event_Wander( gentity_t *self, const intptr_t *args ) {
	vec3_t	dir, dest;
	trace_t	tr;
	float	yaw;

	MONSTER_OR_RETURN( (void)0 );
	// somewhere it can go in a straight line, a way off
	yaw = random() * 360;
	dir[0] = cos( DEG2RAD( yaw ) );
	dir[1] = sin( DEG2RAD( yaw ) );
	dir[2] = ( self->monster.flags & MONF_FLY ) ? crandom() * 0.3f : 0;
	VectorMA( self->r.currentOrigin, 256 + random() * 256, dir, dest );
	G_TraceBox( &tr, self->r.currentOrigin, self->monster.moveMins, self->monster.moveMaxs, dest, self->s.number, MASK_PLAYERSOLID | CONTENTS_MONSTERCLIP );
	VectorCopy( tr.endpos, self->monster.moveDest );
	StartMove( self, MOVE_WANDER );
}

static void Event_StopMove( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.moveCommand = MOVE_NONE;
	self->monster.moveStatus = MOVE_STATUS_DONE;
}

static void Event_MoveStatus( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnInt( MOVE_STATUS_DONE ) );
	if ( self->monster.moveStatus == MOVE_STATUS_MOVING && ( self->monster.flags & MONF_BLOCKED ) ) {
		Thread_ReturnInt( MOVE_STATUS_BLOCKED_BY_WALL );
		return;
	}
	Thread_ReturnInt( self->monster.moveStatus );
}

static void Event_SetMoveSpeed( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.moveSpeed = EVARG_FLOAT( 0 );
}

static void Event_FaceEnemy( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.faceCommand = FACE_ENEMY;
}

static void Event_FaceEntity( gentity_t *self, const intptr_t *args ) {
	gentity_t *ent = EVARG_ENTITY( 0 );

	MONSTER_OR_RETURN( (void)0 );
	self->monster.faceCommand = ent ? FACE_ENTITY : FACE_MOVE;
	self->monster.faceEntity = ent ? ent->s.number : -1;
}

static void Event_TurnTo( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.faceCommand = FACE_NONE;
	self->monster.idealYaw = AngleMod( EVARG_FLOAT( 0 ) );
}

static void Event_TurnToPos( gentity_t *self, const intptr_t *args ) {
	vec3_t dir;

	MONSTER_OR_RETURN( (void)0 );
	VectorSubtract( EVARG_VECTOR( 0 ), self->r.currentOrigin, dir );
	self->monster.faceCommand = FACE_NONE;
	self->monster.idealYaw = vectoyaw( dir );
}

static void Event_GetCurrentYaw( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( self->r.currentAngles[YAW] );
}

static void Event_FacingIdeal( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnInt( 1 ) );
	Thread_ReturnInt( fabs( AngleSubtract( self->monster.idealYaw, self->r.currentAngles[YAW] ) ) < 5 );
}

static void Event_SetFlySpeed( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.flySpeed = EVARG_FLOAT( 0 );
}

static void Event_SetFlyOffset( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.flyOffset = EVARG_INT( 0 );
}

static void Event_EnableGravity( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.flags &= ~MONF_NOGRAVITY;
}

static void Event_DisableGravity( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	self->monster.flags |= MONF_NOGRAVITY;
}

static void Event_Perch( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnInt( 0 ) );
	Thread_ReturnInt( G_MonsterPerch( self, EVARG_VECTOR( 0 ), EVARG_VECTOR( 1 ) ) );
}

static void Event_IsPerched( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( Thread_ReturnInt( 0 ) );
	Thread_ReturnInt( ( self->monster.flags & MONF_PERCHED ) != 0 );
}

/*
================
Event_GetJumpVelocity

the velocity that throws it to land at pos: going up at upSpeed (more if it
must, to get that high), across no faster than maxSpeed (it falls short then);
none if something's in the way of its arc, as Doom 3's (idAI's
PredictTrajectory): no use leaping into a wall
================
*/
static void Event_GetJumpVelocity( gentity_t *self, const intptr_t *args ) {
	vec3_t	d, vel;
	float	up = EVARG_FLOAT( 1 ), maxSpeed = EVARG_FLOAT( 2 ), g = g_gravity.value, h, dist, t, disc, speed;

	VectorSubtract( EVARG_VECTOR( 0 ), self->r.currentOrigin, d );
	h = d[2];
	d[2] = 0;
	dist = VectorNormalize( d );
	if ( g <= 0 ) {
		g = 800;
	}
	// up fast enough to get there, and a little over
	if ( h > 0 && up * up < 2 * g * ( h + 8 ) ) {
		up = sqrt( 2 * g * ( h + 8 ) );
	}
	// the time it comes down to the height: h = up t - g t^2 / 2
	disc = up * up - 2 * g * h;
	t = ( up + sqrt( MAX( disc, 0 ) ) ) / g;
	speed = t > 0.05f ? dist / t : maxSpeed;
	if ( speed > maxSpeed ) {
		speed = maxSpeed;
	}
	VectorScale( d, speed, vel );
	vel[2] = up;

	// along its arc, a tenth of a second at a time, until it comes down to
	// pos's height; it may come down on someone, not on the world short of it
	if ( G_IsMonster( self ) ) {
		vec3_t	from, to;
		trace_t	tr;
		float	s;

		VectorCopy( self->r.currentOrigin, from );
		for ( s = 0.1f; s < t + 0.1f && s < 3; s += 0.1f ) {
			VectorMA( self->r.currentOrigin, s, vel, to );
			to[2] -= 0.5f * g * s * s;
			G_TraceBox( &tr, from, self->monster.moveMins, self->monster.moveMaxs, to, self->s.number, self->clipmask & ~CONTENTS_BODY );
			if ( tr.startsolid || ( tr.fraction < 1 && Distance( tr.endpos, EVARG_VECTOR( 0 ) ) > 48 + self->monster.moveMaxs[0] ) ) {
				VectorClear( vel );
				break;
			}
			if ( tr.fraction < 1 ) {
				break;
			}
			VectorCopy( to, from );
		}
	}
	Thread_ReturnVector( vel );
}

static void Event_AllowMovement( gentity_t *self, const intptr_t *args ) {
	MONSTER_OR_RETURN( (void)0 );
	if ( EVARG_FLOAT( 0 ) ) {
		self->monster.flags &= ~MONF_NOMOVE;
	} else {
		self->monster.flags |= MONF_NOMOVE;
	}
}

/*
================
DamageFromDef

a damage def's ("damage", and "damage_random" more at most, Daikatana's)
================
*/
static int DamageFromDef( const char *name ) {
	const dict_t *def = BG_FindDef( name );

	if ( !def ) {
		G_Printf( S_COLOR_YELLOW "WARNING: no damage def %s\n", name );
		return 0;
	}
	return DictGetInt( def, "damage", "0" ) + (int)( random() * DictGetInt( def, "damage_random", "0" ) );
}

/*
================
MeleeReach

whether its enemy is in a melee attack's reach (its def's "range", else the
monster's "melee_range"): the gap between their boxes, as Doom 3's
TestMelee, in front of it, and seen
================
*/
static qboolean MeleeReach( gentity_t *self, gentity_t *enemy, const char *damageDef ) {
	const dict_t *def = damageDef[0] ? BG_FindDef( damageDef ) : NULL;
	vec3_t	gap, dir;
	float	range;
	int		k;

	range = def ? DictGetFloat( def, "range", G_ScriptGetSpawnArg( self, "melee_range", "64" ) )
				: atof( G_ScriptGetSpawnArg( self, "melee_range", "64" ) );
	for ( k = 0; k < 3; k++ ) {
		gap[k] = MAX( 0, MAX( enemy->r.absmin[k] - self->r.absmax[k], self->r.absmin[k] - enemy->r.absmax[k] ) );
	}
	VectorSubtract( enemy->r.currentOrigin, self->r.currentOrigin, dir );
	return VectorLength( gap ) <= range && fabs( AngleSubtract( vectoyaw( dir ), self->r.currentAngles[YAW] ) ) <= 60
		&& G_MonsterCanSee( self, enemy );
}

static void Event_TestMeleeAttack( gentity_t *self, const intptr_t *args ) {
	gentity_t *enemy;

	MONSTER_OR_RETURN( Thread_ReturnInt( 0 ) );
	enemy = G_MonsterEnemy( self );
	Thread_ReturnInt( enemy && MeleeReach( self, enemy, "" ) );
}

static void Event_AttackMelee( gentity_t *self, const intptr_t *args ) {
	gentity_t	*enemy;
	vec3_t		dir, point;

	MONSTER_OR_RETURN( Thread_ReturnInt( 0 ) );
	enemy = G_MonsterEnemy( self );
	if ( !enemy || !BG_FindDef( EVARG_STRING( 0 ) ) || !MeleeReach( self, enemy, EVARG_STRING( 0 ) ) ) {
		Thread_ReturnInt( 0 );
		return;
	}
	VectorSubtract( enemy->r.currentOrigin, self->r.currentOrigin, dir );
	VectorNormalize( dir );

	VectorMA( self->r.currentOrigin, self->monster.moveMaxs[0], dir, point );
	G_Damage( enemy, self, self, dir, point, DamageFromDef( EVARG_STRING( 0 ) ), 0, MOD_UNKNOWN );
	Thread_ReturnInt( 1 );
}

static void Event_DirectDamage( gentity_t *self, const intptr_t *args ) {
	gentity_t	*target = EVARG_ENTITY( 0 );
	vec3_t		dir;

	VectorSubtract( target->r.currentOrigin, self->r.currentOrigin, dir );
	VectorNormalize( dir );
	G_Damage( target, self, self, dir, target->r.currentOrigin, DamageFromDef( EVARG_STRING( 1 ) ), 0, MOD_UNKNOWN );
}

static int AnimChannel( int channel ) {
	return channel == ANIMCHANNEL_TORSO ? ANIMCHANNEL_TORSO : ANIMCHANNEL_LEGS;
}

static void Event_PlayAnim( gentity_t *self, const intptr_t *args ) {
	int anim;

	if ( self->s.eType != ET_CHARACTER || !BG_ValidModel( self->s.modelindex2 ) ) {
		Thread_ReturnInt( 0 );
		return;
	}
	anim = BG_ModelAnimForName( &bg_models[ self->s.modelindex2 ], EVARG_STRING( 1 ) );
	if ( anim < 0 ) {
		G_DPrintf( "%s has no animation %s\n", self->classname, EVARG_STRING( 1 ) );
		Thread_ReturnInt( 0 );
		return;
	}
	G_CharacterSetAnim( self, AnimChannel( EVARG_INT( 0 ) ), anim );
	Thread_ReturnInt( 1 );
}

static void Event_AnimDone( gentity_t *self, const intptr_t *args ) {
	const bgAnim_t	*anim;
	int				num, start;

	if ( self->s.eType != ET_CHARACTER || !BG_ValidModel( self->s.modelindex2 ) ) {
		Thread_ReturnInt( 1 );
		return;
	}
	if ( AnimChannel( EVARG_INT( 0 ) ) == ANIMCHANNEL_TORSO ) {
		num = self->s.torsoAnim - 1;
		start = self->s.time2;
	} else {
		num = self->s.legsAnim;
		start = self->s.time;
	}
	if ( num < 0 || num >= bg_models[ self->s.modelindex2 ].numAnims ) {
		Thread_ReturnInt( 1 );
		return;
	}
	anim = &bg_models[ self->s.modelindex2 ].anims[ num ];
	Thread_ReturnInt( !anim->loop && level.time - start >= ( anim->numFrames - EVARG_INT( 1 ) ) * 1000.0f / anim->fps );
}

static void Event_HasAnim( gentity_t *self, const intptr_t *args ) {
	if ( self->s.eType != ET_CHARACTER || !BG_ValidModel( self->s.modelindex2 ) ) {
		Thread_ReturnFloat( 0 );
		return;
	}
	Thread_ReturnFloat( BG_ModelAnimForName( &bg_models[ self->s.modelindex2 ], EVARG_STRING( 1 ) ) >= 0 );
}

static void Event_StartSound( gentity_t *self, const intptr_t *args ) {
	const bgSoundShader_t	*shader;
	const char				*value, *file;
	int						channel = EVARG_INT( 1 );

	// a key naming a sound shader (Doom 3's "snd_*"), or the shader itself
	value = G_ScriptGetSpawnArg( self, EVARG_STRING( 0 ), EVARG_STRING( 0 ) );
	shader = BG_FindSoundShader( value );
	file = shader ? BG_SoundShaderFile( shader ) : value;
	if ( file && file[0] && Q_stricmpn( file, "snd_", 4 ) ) {
		G_Sound( self, channel ? channel : CHAN_AUTO, G_SoundIndex( (char *)file ) );
	}
	Thread_ReturnFloat( 0 );
}

/***********************************************************************

  idMover_Binary's (Quake 3's doors, plats and buttons)

***********************************************************************/

void	Use_BinaryMover( gentity_t *ent, gentity_t *other, gentity_t *activator );	// g_mover.c
void	ReturnToPos1( gentity_t *ent );

static qboolean IsBinaryMover( const gentity_t *self ) {
	return self->s.eType == ET_MOVER && self->use == Use_BinaryMover;
}

static void Event_Open( gentity_t *self, const intptr_t *args ) {
	if ( !IsBinaryMover( self ) ) {
		return;
	}
	if ( self->moverState == MOVER_POS1 || self->moverState == MOVER_2TO1 ) {
		Use_BinaryMover( self->teammaster ? self->teammaster : self, self, G_ScriptPlayer() );
	}
}

static void Event_Close( gentity_t *self, const intptr_t *args ) {
	gentity_t *master = self->teammaster ? self->teammaster : self;

	if ( !IsBinaryMover( self ) ) {
		return;
	}
	if ( master->moverState == MOVER_POS2 ) {
		ReturnToPos1( master );
	}
}

static void Event_IsOpen( gentity_t *self, const intptr_t *args ) {
	gentity_t *master = self->teammaster ? self->teammaster : self;

	Thread_ReturnFloat( IsBinaryMover( self ) && master->moverState != MOVER_POS1 );
}

static void Event_IsMoving( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnFloat( self->s.eType == ET_MOVER && ( self->moverState == MOVER_1TO2 || self->moverState == MOVER_2TO1 ) );
}

static void Event_SetCallback( gentity_t *self, const intptr_t *args ) {
	Thread_ReturnInt( G_ScriptSetCallback( self ) );
}

// the entities' events (idEntity's, idMover_Binary's)
const entityEventCallback_t entityEventCallbacks[] = {
	{ "getName",			NULL,		's',	Event_GetName },
	{ "setName",			"s",		0,		Event_SetName },
	{ "remove",				NULL,		0,		Event_Remove },
	{ "activate",			"e",		0,		Event_Activate },
	{ "activateTargets",	"e",		0,		Event_ActivateTargets },
	{ "numTargets",			NULL,		'f',	Event_NumTargets },
	{ "getTarget",			"f",		'e',	Event_GetTarget },
	{ "randomTarget",		"s",		'e',	Event_RandomTarget },
	{ "setOwner",			"e",		0,		Event_SetOwner },
	{ "getWorldOrigin",		NULL,		'v',	Event_GetWorldOrigin },
	{ "setWorldOrigin",		"v",		0,		Event_SetWorldOrigin },
	{ "getOrigin",			NULL,		'v',	Event_GetWorldOrigin },
	{ "setOrigin",			"v",		0,		Event_SetWorldOrigin },
	{ "getAngles",			NULL,		'v',	Event_GetAngles },
	{ "setAngles",			"v",		0,		Event_SetAngles },
	{ "getLinearVelocity",	NULL,		'v',	Event_GetLinearVelocity },
	{ "setLinearVelocity",	"v",		0,		Event_SetLinearVelocity },
	{ "getSize",			NULL,		'v',	Event_GetSize },
	{ "setSize",			"vv",		0,		Event_SetSize },
	{ "getMins",			NULL,		'v',	Event_GetMins },
	{ "getMaxs",			NULL,		'v',	Event_GetMaxs },
	{ "isHidden",			NULL,		'd',	Event_IsHidden },
	{ "hide",				NULL,		0,		Event_Hide },
	{ "show",				NULL,		0,		Event_Show },
	{ "touches",			"E",		'd',	Event_Touches },
	{ "startSoundShader",	"sd",		'f',	Event_StartSoundShader },
	{ "setKey",				"ss",		0,		Event_SetKey },
	{ "getKey",				"s",		's',	Event_GetKey },
	{ "getIntKey",			"s",		'f',	Event_GetIntKey },
	{ "getFloatKey",		"s",		'f',	Event_GetFloatKey },
	{ "getVectorKey",		"s",		'v',	Event_GetVectorKey },
	{ "getEntityKey",		"s",		'e',	Event_GetEntityKey },
	{ "distanceTo",			"E",		'f',	Event_DistanceTo },
	{ "distanceToPoint",	"v",		'f',	Event_DistanceToPoint },
	{ "wait",				"f",		0,		Event_EntityWait },
	{ "waitFrame",			NULL,		0,		Event_EntityWaitFrame },
	{ "hasFunction",		"s",		'd',	Event_HasFunction },
	{ "callFunction",		"s",		0,		Event_CallFunction },
	{ "<script_setcallback>", NULL,		'd',	Event_SetCallback },
	{ "getHealth",			NULL,		'f',	Event_GetHealth },
	{ "setHealth",			"f",		0,		Event_SetHealth },

	{ "getMaxHealth",		NULL,		'f',	Event_GetMaxHealth },
	{ "giveInventoryItem",	"s",		0,		Event_GiveInventoryItem },
	{ "removeInventoryItem", "s",		0,		Event_RemoveInventoryItem },
	{ "hasInventoryItem",	"s",		'f',	Event_HasInventoryItem },
	{ "centerPrint",		"s",		0,		Event_CenterPrint },

	{ "setState",			"s",		0,		Event_SetState },
	{ "getState",			NULL,		's',	Event_GetState },
	{ "findEnemy",			"d",		'e',	Event_FindEnemy },
	{ "setEnemy",			"E",		0,		Event_SetEnemy },
	{ "clearEnemy",			NULL,		0,		Event_ClearEnemy },
	{ "getEnemy",			NULL,		'e',	Event_GetEnemy },
	{ "enemyRange",			NULL,		'f',	Event_EnemyRange },
	{ "enemyRange2D",		NULL,		'f',	Event_EnemyRange2D },
	{ "getEnemyPos",		NULL,		'v',	Event_GetEnemyPos },
	{ "canSee",				"E",		'd',	Event_CanSee },
	{ "moveToEnemy",		NULL,		0,		Event_MoveToEnemy },
	{ "moveToPosition",		"v",		0,		Event_MoveToPosition },
	{ "moveToEntity",		"e",		0,		Event_MoveToEntity },
	{ "wander",				NULL,		0,		Event_Wander },
	{ "stopMove",			NULL,		0,		Event_StopMove },
	{ "moveStatus",			NULL,		'd',	Event_MoveStatus },
	{ "setMoveSpeed",		"f",		0,		Event_SetMoveSpeed },
	{ "faceEnemy",			NULL,		0,		Event_FaceEnemy },
	{ "faceEntity",			"E",		0,		Event_FaceEntity },
	{ "turnTo",				"f",		0,		Event_TurnTo },
	{ "turnToPos",			"v",		0,		Event_TurnToPos },
	{ "getCurrentYaw",		NULL,		'f',	Event_GetCurrentYaw },
	{ "facingIdeal",		NULL,		'd',	Event_FacingIdeal },
	{ "setFlySpeed",		"f",		0,		Event_SetFlySpeed },
	{ "perch",				"vv",		'd',	Event_Perch },
	{ "isPerched",			NULL,		'd',	Event_IsPerched },
	{ "setFlyOffset",		"d",		0,		Event_SetFlyOffset },
	{ "enableGravity",		NULL,		0,		Event_EnableGravity },
	{ "disableGravity",		NULL,		0,		Event_DisableGravity },
	{ "allowMovement",		"f",		0,		Event_AllowMovement },
	{ "getJumpVelocity",	"vff",		'v',	Event_GetJumpVelocity },
	{ "testMeleeAttack",	NULL,		'd',	Event_TestMeleeAttack },
	{ "attackMelee",		"s",		'd',	Event_AttackMelee },
	{ "directDamage",		"es",		0,		Event_DirectDamage },
	{ "playAnim",			"ds",		'd',	Event_PlayAnim },
	{ "playCycle",			"ds",		'd',	Event_PlayAnim },
	{ "animDone",			"dd",		'd',	Event_AnimDone },
	{ "hasAnim",			"ds",		'f',	Event_HasAnim },
	{ "startSound",			"sdd",		'f',	Event_StartSound },

	{ "open",				NULL,		0,		Event_Open },
	{ "close",				NULL,		0,		Event_Close },
	{ "isOpen",				NULL,		'f',	Event_IsOpen },
	{ "isMoving",			NULL,		'f',	Event_IsMoving },
	{ NULL }
};
