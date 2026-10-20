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
// g_character.c -- skeletal characters and their hitboxes
//
// A character is drawn as its model def's mesh (bg_model.c) playing an
// animation, and shots hit its hitboxes posed on the joints: a trace that
// reaches the character's bounding box is tested again against each box,
// exactly, with the collision model, and goes on past it if it misses them
// all. What a box hit tells the damage its zone's "damage_scale".
//
// The animation and position of the last frames are kept, so a shot rewound
// for lag (g_unlagged.c) hits the boxes where they were then.

#include "g_local.h"

char *G_AddSpawnVarToken( const char *string );

#define CHARACTER_HISTORY	32		// frames, a bit over a second and a half

typedef struct {
	int				time;
	bgAnimChannel_t	channels[MAX_ANIM_CHANNELS];
	vec3_t			origin, angles;
} characterState_t;

struct characterHistory_s {
	bgAnimChannel_t		channels[MAX_ANIM_CHANNELS];	// now, with what they cross-fade from
	characterState_t	states[CHARACTER_HISTORY];
	int					head;		// the latest
	int					num;
};

/*
================
G_CharacterRecord

Keeps the character's animation and position of this frame.
================
*/
static void G_CharacterRecord( gentity_t *ent ) {
	struct characterHistory_s *h = ent->characterHistory;
	characterState_t *st;

	if ( !h ) {
		return;
	}
	if ( h->num && h->states[h->head].time == level.time ) {
		st = &h->states[h->head];
	} else {
		h->head = ( h->head + 1 ) % CHARACTER_HISTORY;
		if ( h->num < CHARACTER_HISTORY ) {
			h->num++;
		}
		st = &h->states[h->head];
	}
	st->time = level.time;
	BG_AnimChannelsFromState( h->channels, ent->s.legsAnim, ent->s.time, ent->s.torsoAnim, ent->s.time2 );
	memcpy( st->channels, h->channels, sizeof( st->channels ) );
	VectorCopy( ent->r.currentOrigin, st->origin );
	VectorCopy( ent->r.currentAngles, st->angles );
}

/*
================
G_CharacterStateAt

The character's animation and position at time: the latest kept that's
no later, or as it is now.
================
*/
static void G_CharacterStateAt( const gentity_t *ent, int time, characterState_t *out ) {
	const struct characterHistory_s *h = ent->characterHistory;
	int i, n;

	if ( h ) {
		for ( n = 0, i = h->head; n < h->num; n++, i = ( i + CHARACTER_HISTORY - 1 ) % CHARACTER_HISTORY ) {
			if ( h->states[i].time <= time ) {
				*out = h->states[i];
				out->time = time;
				return;
			}
		}
	}
	out->time = time;
	if ( h ) {
		memcpy( out->channels, h->channels, sizeof( out->channels ) );
	}
	VectorCopy( ent->r.currentOrigin, out->origin );
	VectorCopy( ent->r.currentAngles, out->angles );
}

/*
================
G_CharacterHitboxes

The character's hitboxes posed at time. Returns how many.
================
*/
int G_CharacterHitboxes( const gentity_t *ent, int time, bgHitboxPose_t *poses ) {
	const bgModel_t *model;
	characterState_t st;
	vec3_t axis[3];
	float joints[MAX_MODEL_JOINTS * 12];
	int numJoints;

	if ( ent->s.eType != ET_CHARACTER || !BG_ValidModel( ent->s.modelindex2 ) || !ent->characterModel ) {
		return 0;
	}
	model = &bg_models[ent->s.modelindex2];
	G_CharacterStateAt( ent, time, &st );
	// the skeleton of its channels' animations blended, as the cgame draws it
	numJoints = BG_ModelPose( model, ent->characterModel, st.channels, time, joints );
	AnglesToAxis( st.angles, axis );
	return BG_ModelHitboxes( model, joints, numJoints, st.origin, (const vec3_t *)axis, poses );
}

/*
================
G_TraceHitboxes

The move from start to end (a box of mins/maxs) against the character's
hitboxes, exactly. qfalse when it misses them all.
================
*/
static qboolean G_TraceHitboxes( gentity_t *ent, const vec3_t start, const vec3_t mins, const vec3_t maxs,
								 const vec3_t end, int contentmask, trace_t *results, int *zone ) {
	bgHitboxPose_t poses[MAX_MODEL_HITBOXES];
	traceModel_t boxTrm, shotTrm;
	cm_trace_t tr, best;
	vec3_t bounds[2];
	cmHandle_t handle;
	int i, num, bestBox = -1;

	num = G_CharacterHitboxes( ent, level.hitboxTime ? level.hitboxTime : level.time, poses );
	if ( !num ) {
		return qfalse;
	}

	// the shot as a trace model, a point for hitscan
	shotTrm.type = TRM_INVALID;
	if ( !VectorCompare( mins, vec3_origin ) || !VectorCompare( maxs, vec3_origin ) ) {
		VectorCopy( mins, bounds[0] );
		VectorCopy( maxs, bounds[1] );
		TraceModelSetupBox( &shotTrm, bounds );
	}

	memset( &best, 0, sizeof( best ) );
	best.fraction = 1.0f;
	for ( i = 0; i < num; i++ ) {
		VectorCopy( poses[i].mins, bounds[0] );
		VectorCopy( poses[i].maxs, bounds[1] );
		boxTrm.type = TRM_INVALID;
		TraceModelSetupBox( &boxTrm, bounds );
		handle = trap_CM_SetupTrmModel( &boxTrm, 0 );
		trap_CM_Translation( &tr, start, end, shotTrm.type == TRM_INVALID ? NULL : &shotTrm, axisDefault,
							 -1, handle, poses[i].origin, (const vec3_t *)poses[i].axis );
		if ( tr.fraction < best.fraction ) {
			best = tr;
			bestBox = i;
		}
	}
	if ( bestBox < 0 ) {
		return qfalse;
	}

	memset( results, 0, sizeof( *results ) );
	results->fraction = best.fraction;
	VectorCopy( best.endpos, results->endpos );
	VectorCopy( best.c.normal, results->plane.normal );
	results->plane.dist = DotProduct( results->plane.normal, results->endpos );
	results->entityNum = ent->s.number;
	results->contents = CONTENTS_BODY;
	results->surfaceFlags = 0;
	*zone = poses[bestBox].zone;

	if ( g_debugHitboxes.integer ) {
		const bgModel_t *model = &bg_models[ent->s.modelindex2];

		G_Printf( "hit %s %d: hitbox %s, zone %s x%.2g\n", ent->classname, ent->s.number,
			model->hitboxes[poses[bestBox].hitbox].joint, model->zones[*zone].name, model->zones[*zone].damageScale );
	}
	return qtrue;
}

/*
================
G_TraceShot

A trace for something that hits characters on their hitboxes: shots and
projectiles. It goes past a character whose boxes it misses. zone, if not
NULL, is the zone of the hitbox it hit, -1 for none.
================
*/
void G_TraceShot( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
				  int passEntityNum, int contentmask, int *zone ) {
	gentity_t *skipped[8], *ent;
	int skippedContents[8], numSkipped = 0, i, hitZone = -1;

	if ( zone ) {
		*zone = -1;
	}
	if ( !mins ) {
		mins = vec3_origin;
	}
	if ( !maxs ) {
		maxs = vec3_origin;
	}
	for ( ;; ) {
		G_Trace( results, start, mins, maxs, end, passEntityNum, contentmask, TT_AABB );
		if ( results->fraction == 1.0f || results->entityNum < 0 || results->entityNum >= ENTITYNUM_MAX_NORMAL ) {
			break;
		}
		ent = &g_entities[results->entityNum];
		if ( ent->s.eType != ET_CHARACTER || !ent->characterModel ) {
			break;
		}
		if ( G_TraceHitboxes( ent, start, mins, maxs, end, contentmask, results, &hitZone ) ) {
			if ( zone ) {
				*zone = hitZone;
			}
			break;
		}
		// missed its boxes: on through its bounding box (traces read the
		// entity's contents)
		if ( numSkipped == ARRAY_LEN( skipped ) ) {
			results->fraction = 1.0f;
			VectorCopy( end, results->endpos );
			results->entityNum = ENTITYNUM_NONE;
			break;
		}
		skipped[numSkipped] = ent;
		skippedContents[numSkipped++] = ent->s.contents;
		ent->s.contents = 0;
	}
	for ( i = 0; i < numSkipped; i++ ) {
		skipped[i]->s.contents = skippedContents[i];
	}
}

/*
================
G_HitboxDamageScale

The damage scale of a character's zone.
================
*/
float G_HitboxDamageScale( const gentity_t *ent, int zone ) {
	const bgModel_t *model;

	if ( zone < 0 || ent->s.eType != ET_CHARACTER || !BG_ValidModel( ent->s.modelindex2 ) ) {
		return 1.0f;
	}
	model = &bg_models[ent->s.modelindex2];
	return zone < model->numZones ? model->zones[zone].damageScale : 1.0f;
}

/*
================
G_CharacterSetAnim

What a channel plays from now on (-1: the torso stops, its joints go back to
the legs'); a change cross-fades over the model's "blend_time".
================
*/
void G_CharacterSetAnim( gentity_t *ent, int channel, int anim ) {
	if ( channel == ANIMCHANNEL_TORSO ) {
		ent->s.torsoAnim = anim >= 0 ? anim + 1 : 0;
		ent->s.time2 = level.time;
	} else {
		ent->s.legsAnim = anim < 0 ? 0 : anim;
		ent->s.time = level.time;
	}
	G_CharacterRecord( ent );
}

static void G_CharacterThink( gentity_t *ent ) {
	const bgModel_t *model = &bg_models[ent->s.modelindex2];

	// "anim_cycle": goes through the animations, a few seconds each
	if ( ent->wait > 0 && level.time - ent->s.time >= ent->wait * 1000 && model->numAnims > 1 ) {
		G_CharacterSetAnim( ent, ANIMCHANNEL_LEGS, ( ent->s.legsAnim + 1 ) % model->numAnims );
	}
	G_CharacterRecord( ent );
	ent->nextthink = level.time + 1;
}

static void G_CharacterPain( gentity_t *self, gentity_t *attacker, int damage ) {
	if ( g_debugHitboxes.integer ) {
		G_Printf( "%s %d took %d damage, %d health left\n", self->classname, self->s.number, damage, self->health );
	}
}

static void G_CharacterDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath ) {
	// a test dummy: back up
	self->health = self->count;
	self->takedamage = qtrue;
}

/*QUAKED misc_character (1 0 0) (-16 -16 -24) (16 16 32)
A skeletal character (bg_model.c), for now a target to shoot.
"model"			the model def
"anim"			the animation it plays (the first by default), on the legs channel
"torso_anim"	an animation for the torso channel (its joints) over that
"anim_cycle"	seconds before it goes on to the next animation, 0 never
"health"		how much it takes (1000; it gets up again)
"angle"			facing
*/
void SP_misc_character( gentity_t *ent ) {
	const bgModel_t *model;
	char *name;
	int anim, health;

	G_SpawnString( "model", "", &name );
	ent->s.modelindex2 = BG_ModelForName( name );
	if ( !BG_ValidModel( ent->s.modelindex2 ) ) {
		G_Printf( S_COLOR_YELLOW "WARNING: misc_character at %s: no model def \"%s\"\n", vtos( ent->s.origin ), name );
		G_FreeEntity( ent );
		return;
	}
	model = &bg_models[ent->s.modelindex2];

	ent->s.eType = ET_CHARACTER;
	ent->s.modelindex = G_ModelIndex( (char *)model->mesh );
	ent->characterModel = trap_R_RegisterModel( model->mesh );
	if ( !ent->characterModel ) {
		G_Printf( S_COLOR_YELLOW "WARNING: misc_character at %s: can't load %s\n", vtos( ent->s.origin ), model->mesh );
	}
	ent->characterHistory = trap_HeapMalloc( sizeof( *ent->characterHistory ) );
	memset( ent->characterHistory, 0, sizeof( *ent->characterHistory ) );
	{
		int c;

		// nothing to cross-fade from
		for ( c = 0; c < MAX_ANIM_CHANNELS; c++ ) {
			ent->characterHistory->channels[c].anim = ent->characterHistory->channels[c].prevAnim = -1;
		}
	}

	DictGetVector( model->def, "mins", "-16 -16 -24", ent->s.mins );
	DictGetVector( model->def, "maxs", "16 16 48", ent->s.maxs );
	ent->s.contents = CONTENTS_BODY;
	ent->s.collisionType = CT_AABB;

	G_SpawnString( "anim", "", &name );
	anim = name[0] ? BG_ModelAnimForName( model, name ) : 0;
	G_SpawnFloat( "anim_cycle", "0", &ent->wait );
	G_SpawnInt( "health", "1000", &health );
	ent->health = ent->count = health;
	ent->takedamage = qtrue;
	ent->pain = G_CharacterPain;
	ent->die = G_CharacterDie;

	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorCopy( ent->s.angles, ent->r.currentAngles );
	ent->s.legsAnim = anim < 0 ? 0 : anim;
	ent->s.time = level.time;
	ent->s.torsoAnim = 0;
	G_SpawnString( "torso_anim", "", &name );
	if ( name[0] && BG_ModelAnimForName( model, name ) >= 0 ) {
		ent->s.torsoAnim = BG_ModelAnimForName( model, name ) + 1;
		ent->s.time2 = level.time;
	}
	G_CharacterRecord( ent );
	ent->think = G_CharacterThink;
	ent->nextthink = level.time + 1;
	G_LinkEntity( ent );
}

/*
================
Svcmd_SpawnCharacter_f

spawncharacter <model def> [anim] [height] [side]: puts one 96 units in
front of the first player, facing them, height units up and side units to
their left (cheat, for testing).
================
*/
void Svcmd_SpawnCharacter_f( void ) {
	gentity_t *player, *ent;
	vec3_t forward;
	char name[MAX_QPATH], anim[MAX_QPATH], height[16], side[16];
	vec3_t right;

	if ( !g_cheats.integer ) {
		G_Printf( "Cheats are not enabled on this server.\n" );
		return;
	}
	player = &g_entities[0];
	if ( !player->inuse || !player->player ) {
		G_Printf( "no player\n" );
		return;
	}
	trap_Argv( 1, name, sizeof( name ) );
	trap_Argv( 2, anim, sizeof( anim ) );
	trap_Argv( 3, height, sizeof( height ) );
	trap_Argv( 4, side, sizeof( side ) );

	ent = G_Spawn();
	ent->classname = "misc_character";
	AngleVectors( tv( 0, player->player->ps.viewangles[YAW], 0 ), forward, right, NULL );
	VectorMA( player->r.currentOrigin, 96, forward, ent->s.origin );
	VectorMA( ent->s.origin, -atof( side ), right, ent->s.origin );
	ent->s.origin[2] += atof( height );
	VectorSet( ent->s.angles, 0, player->player->ps.viewangles[YAW] + 180, 0 );

	// as if spawned from a map
	level.numSpawnVars = 0;
	level.numSpawnVarChars = 0;
	level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( "model" );
	level.spawnVars[level.numSpawnVars++][1] = G_AddSpawnVarToken( name );
	if ( anim[0] ) {
		level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( "anim" );
		level.spawnVars[level.numSpawnVars++][1] = G_AddSpawnVarToken( anim );
	}
	SP_misc_character( ent );
	level.numSpawnVars = 0;
}

/*
================
Svcmd_CharacterAnim_f

characteranim <entity number | all> <legs | torso> <anim | none>: what a
character's channel plays, cross-fading (cheat, for testing).
================
*/
void Svcmd_CharacterAnim_f( void ) {
	char who[16], channelName[16], anim[MAX_QPATH];
	gentity_t *ent;
	int i, channel, animNum;

	if ( !g_cheats.integer ) {
		G_Printf( "Cheats are not enabled on this server.\n" );
		return;
	}
	trap_Argv( 1, who, sizeof( who ) );
	trap_Argv( 2, channelName, sizeof( channelName ) );
	trap_Argv( 3, anim, sizeof( anim ) );
	channel = !Q_stricmp( channelName, "torso" ) ? ANIMCHANNEL_TORSO : ANIMCHANNEL_LEGS;

	for ( i = 0, ent = g_entities; i < level.num_entities; i++, ent++ ) {
		if ( !ent->inuse || ent->s.eType != ET_CHARACTER || ( Q_stricmp( who, "all" ) && atoi( who ) != i ) ) {
			continue;
		}
		animNum = !Q_stricmp( anim, "none" ) ? -1 : BG_ModelAnimForName( &bg_models[ent->s.modelindex2], anim );
		if ( animNum < 0 && Q_stricmp( anim, "none" ) ) {
			G_Printf( "%d: no animation %s\n", i, anim );
			continue;
		}
		G_CharacterSetAnim( ent, channel, animNum );
	}
}
