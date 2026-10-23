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
//
// Players whose model is a model def ("model hiro": model_hiro) are skeletal
// characters too: their legsAnim and torsoAnim go through the def's
// "player_anim"s, and their torso and head turn to aim as Quake 3's models do.

#include "g_local.h"


#define CHARACTER_HISTORY	32		// frames, a bit over a second and a half

typedef struct {
	int				time;
	bgAnimChannel_t	channels[MAX_ANIM_CHANNELS];
	vec3_t			origin, angles;		// angles: a player's view
	int				movementDir;		// a player's
	qboolean		onGround;			// a player's, for its walk IK
	int				legsAnim;			// a player's, for a ladder
	vec3_t			velocity;			// a player's, swimming along it
	int				ladderYaw;			// a player's, the ladder it holds
} characterState_t;

struct characterHistory_s {
	bgAnimChannel_t		channels[MAX_ANIM_CHANNELS];	// now, with what they cross-fade from
	characterState_t	states[CHARACTER_HISTORY];
	int					head;		// the latest
	int					num;
	int					legsAnim, torsoAnim;		// a player's, and when they started
	int					legsTime, torsoTime;
};

static struct characterHistory_s	g_playerHistories[MAX_CLIENTS];

/*
================
G_ResetCharacterHistory
================
*/
static void G_ResetCharacterHistory( struct characterHistory_s *h ) {
	int c;

	memset( h, 0, sizeof( *h ) );
	// nothing to cross-fade from
	for ( c = 0; c < MAX_ANIM_CHANNELS; c++ ) {
		h->channels[c].anim = h->channels[c].prevAnim = -1;
	}
	h->legsAnim = h->torsoAnim = -1;
}

/*
================
G_AllocCharacterHistory

a character's history, new (a loaded game's characters)
================
*/
struct characterHistory_s *G_AllocCharacterHistory( void ) {
	struct characterHistory_s *h = trap_HeapMalloc( sizeof( *h ) );

	G_ResetCharacterHistory( h );
	return h;
}

/*
================
G_CharacterRecord

Keeps the character's animation and position of this frame.
================
*/
void G_CharacterRecord( gentity_t *ent ) {
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
	if ( ent->player ) {
		BG_PlayerAnimChannels( &bg_models[ent->characterDef], h->channels, ent->s.legsAnim, ent->s.time,
							   ent->s.torsoAnim, ent->s.time2, ent->s.weapon, ent->player->ps.swimPhase, level.time );
		BG_PlayerClimbPhase( &bg_models[ent->characterDef], &h->channels[ANIMCHANNEL_LEGS], ent->s.legsAnim,
							 ent->r.currentOrigin[2], level.time );
		st->legsAnim = ent->s.legsAnim;
		VectorCopy( ent->player->ps.velocity, st->velocity );
		st->ladderYaw = ent->player->ps.ladderYaw;
		VectorCopy( ent->player->ps.viewangles, st->angles );
		st->movementDir = ( ent->s.eFlags & EF_DEAD ) ? 0 : ent->player->ps.movementDir;
		st->onGround = ent->player->ps.groundEntityNum != ENTITYNUM_NONE && !( ent->s.eFlags & EF_DEAD );
	} else {
		BG_AnimChannelsFromState( h->channels, ent->s.legsAnim, ent->s.time, CHARACTER_LEGS_RATE( &ent->s ), ent->s.torsoAnim, ent->s.time2 );
		VectorCopy( ent->r.currentAngles, st->angles );
	}
	memcpy( st->channels, h->channels, sizeof( st->channels ) );
	VectorCopy( ent->r.currentOrigin, st->origin );
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
	if ( ent->player ) {
		VectorCopy( ent->player->ps.viewangles, out->angles );
		out->movementDir = ent->player->ps.movementDir;
		out->onGround = ent->player->ps.groundEntityNum != ENTITYNUM_NONE && !( ent->s.eFlags & EF_DEAD );
		out->legsAnim = ent->s.legsAnim;
		VectorCopy( ent->player->ps.velocity, out->velocity );
		out->ladderYaw = ent->player->ps.ladderYaw;
	} else {
		VectorCopy( ent->r.currentAngles, out->angles );
	}
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

	if ( !BG_ValidModel( ent->characterDef ) ) {
		return 0;
	}
	model = &bg_models[ent->characterDef];
	G_CharacterStateAt( ent, time, &st );
	// the skeleton of its channels' animations blended, as the cgame draws it
	numJoints = BG_ModelPose( model, st.channels, time, joints );
	if ( ent->player ) {
		vec3_t legs, torso, head, torsoAxis[3], headAxis[3];
		bgLadder_t ladder;
		float ladderWeight;
		qboolean onLadder, swimming;

		// facing its legs, the torso and head turned to aim (as the cgame
		// does, without its swinging); on a ladder facing it
		BG_PlayerAimAngles( st.angles, st.movementDir, legs, torso, head );
		swimming = BG_PlayerSwimAngles( model, st.legsAnim, st.velocity, st.angles, legs, torso, head );
		ladderWeight = BG_PlayerLadderWeight( model, &st.channels[ANIMCHANNEL_LEGS], st.legsAnim, time );
		onLadder = ladderWeight > 0 && BG_PlayerLadder( ent->characterDef, LEGS_CLIMB, ent->r.currentOrigin, st.ladderYaw,
														 ent->s.number, &ladder );
		if ( onLadder ) {
			VectorScale( ladder.offset, ladderWeight, ladder.offset );
			legs[YAW] = LerpAngle( legs[YAW], ladder.yaw, ladderWeight );
			torso[YAW] = LerpAngle( torso[YAW], ladder.yaw, ladderWeight );
		}
		AnglesSubtract( head, torso, head );
		AnglesSubtract( torso, legs, torso );
		AnglesToAxis( torso, torsoAxis );
		AnglesToAxis( head, headAxis );
		BG_ModelAim( model, joints, numJoints, (const vec3_t *)torsoAxis, (const vec3_t *)headAxis );
		AnglesToAxis( legs, axis );
		// where unlagged put it back then
		VectorCopy( ent->r.currentOrigin, st.origin );
		if ( onLadder ) {
			VectorAdd( st.origin, ladder.offset, st.origin );
			BG_ModelLadderIK( model, joints, numJoints, st.origin, (const vec3_t *)axis, &ladder, ladderWeight );
		}
		if ( swimming ) {
			BG_PlayerSwimOrigin( model, st.origin, (const vec3_t *)axis, st.origin );
		}
		// the feet on the floor, as the cgame draws them (unsmoothed)
		BG_ModelWalkIK( model, joints, numJoints, st.origin, (const vec3_t *)axis, st.origin[2] + MINS_Z,
						st.onGround, ent->s.number, time, NULL );
	} else {
		AnglesToAxis( st.angles, axis );
		BG_ModelWalkIK( model, joints, numJoints, st.origin, (const vec3_t *)axis, st.origin[2] + ent->s.mins[2],
						qtrue, ent->s.number, time, NULL );
	}
	if ( ent->s.origin2[0] > 0.0f && ent->s.origin2[0] != 1.0f ) {
		// a scaled model ("modelscale"): its joints where it's drawn, the
		// boxes scaled with them, their axes not (as CG_StoreHitboxes)
		float	scale = ent->s.origin2[0];
		int		num, i;

		VectorScale( axis[0], scale, axis[0] );
		VectorScale( axis[1], scale, axis[1] );
		VectorScale( axis[2], scale, axis[2] );
		num = BG_ModelHitboxes( model, joints, numJoints, st.origin, (const vec3_t *)axis, poses );
		for ( i = 0; i < num; i++ ) {
			VectorNormalize( poses[i].axis[0] );
			VectorNormalize( poses[i].axis[1] );
			VectorNormalize( poses[i].axis[2] );
			VectorScale( poses[i].mins, scale, poses[i].mins );
			VectorScale( poses[i].maxs, scale, poses[i].maxs );
		}
		return num;
	}
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
		const bgModel_t *model = &bg_models[ent->characterDef];

		G_Printf( "hit %s %d: hitbox %s, zone %s x%.2g\n", ent->classname, ent->s.number,
			model->hitboxes[poses[bestBox].hitbox].joint, model->zones[*zone].name, model->zones[*zone].damageScale );
	}
	return qtrue;
}

/*
================
G_SegmentMeetsBox

whether the line from a to b goes through the box
================
*/
static qboolean G_SegmentMeetsBox( const vec3_t a, const vec3_t b, const vec3_t lo, const vec3_t hi ) {
	float	t0 = 0, t1 = 1, d, ta, tb;
	int		k;

	for ( k = 0; k < 3; k++ ) {
		d = b[k] - a[k];
		if ( fabs( d ) < 0.0001f ) {
			if ( a[k] < lo[k] || a[k] > hi[k] ) {
				return qfalse;
			}
			continue;
		}
		ta = ( lo[k] - a[k] ) / d;
		tb = ( hi[k] - a[k] ) / d;
		if ( ta > tb ) {
			d = ta; ta = tb; tb = d;
		}
		t0 = MAX( t0, ta );
		t1 = MIN( t1, tb );
		if ( t0 > t1 ) {
			return qfalse;
		}
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
		if ( !ent->characterDef || ent->ragdoll ) {
			break;		// (a ragdoll's box is hit)
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

	// characters whose models reach past the boxes they move with (a skeet's
	// wings): their hitboxes, nearer than what it hit
	for ( i = 0; i < level.num_entities; i++ ) {
		trace_t	tr;
		vec3_t	lo, hi;
		int		z;

		ent = &g_entities[i];
		if ( !ent->inuse || !( ent->monster.flags & MONF_HIT_REACH ) || ent->ragdoll || i == passEntityNum
			|| i == results->entityNum || !ent->characterDef || !( ent->s.contents & contentmask ) ) {
			continue;
		}
		VectorAdd( ent->r.currentOrigin, ent->monster.hitMins, lo );
		VectorAdd( ent->r.currentOrigin, ent->monster.hitMaxs, hi );
		VectorSubtract( lo, maxs, lo );
		VectorSubtract( hi, mins, hi );
		if ( !G_SegmentMeetsBox( start, results->endpos, lo, hi ) ) {
			continue;
		}
		if ( G_TraceHitboxes( ent, start, mins, maxs, end, contentmask, &tr, &z ) && tr.fraction < results->fraction ) {
			*results = tr;
			if ( zone ) {
				*zone = z;
			}
		}
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

	if ( zone < 0 || !BG_ValidModel( ent->characterDef ) ) {
		return 1.0f;
	}
	model = &bg_models[ent->characterDef];
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

/*
================
G_PlayerSetModel

A player's model: a model def makes it a skeletal character, with hitboxes.
================
*/
void G_PlayerSetModel( gentity_t *ent, const char *modelName ) {
	int m = BG_PlayerModelForName( modelName );

	if ( m && m == ent->characterDef ) {
		return;
	}
	ent->characterDef = m;
	ent->characterHistory = &g_playerHistories[ent->s.number];
	G_ResetCharacterHistory( ent->characterHistory );
}

/*
================
G_PlayerCharacterFrame

At the end of a player's frame: when its animations started (time, time2,
for the cgame to blend them the same), and its history.
================
*/
void G_PlayerCharacterFrame( gentity_t *ent ) {
	struct characterHistory_s *h = ent->characterHistory;

	if ( !ent->characterDef || !h ) {
		return;
	}
	if ( ent->s.legsAnim != h->legsAnim ) {
		h->legsAnim = ent->s.legsAnim;
		h->legsTime = level.time;
	}
	if ( ent->s.torsoAnim != h->torsoAnim ) {
		h->torsoAnim = ent->s.torsoAnim;
		h->torsoTime = level.time;
	}
	ent->s.time = h->legsTime;
	ent->s.time2 = h->torsoTime;
	G_CharacterRecord( ent );
}

void G_CharacterThink( gentity_t *ent ) {
	const bgModel_t *model = &bg_models[ent->s.modelindex2];

	if ( ent->ragdoll ) {
		// limp
		G_RunRagdoll( ent );
		ent->nextthink = level.time + 1;
		return;
	}

	// "anim_cycle": goes through the animations, a few seconds each
	if ( ent->wait > 0 && level.time - ent->s.time >= ent->wait * 1000 && model->numAnims > 1 ) {
		G_CharacterSetAnim( ent, ANIMCHANNEL_LEGS, ( ent->s.legsAnim + 1 ) % model->numAnims );
	}
	G_CharacterRecord( ent );
	ent->nextthink = level.time + 1;
}

void G_CharacterPain( gentity_t *self, gentity_t *attacker, int damage ) {
	if ( g_debugHitboxes.integer ) {
		G_Printf( "%s %d took %d damage, %d health left\n", self->classname, self->s.number, damage, self->health );
	}
}

void G_CharacterDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath ) {
	// Doom 3's idActor::Killed: limp, if its model has a figure
	if ( G_StartRagdoll( self ) ) {
		self->takedamage = qtrue;		// shots still push it
		self->health = 1 << 30;
		return;
	}
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
"health"		how much it takes (1000); then it goes limp if its model def has a
				"ragdoll", else it gets up again
"modelscale"	how much bigger it is (1)
"frame"			of its animation, the pose it starts in
"ragdoll"		1: limp from the start, as a body lying where it's put
"angle"			facing
*/
void SP_misc_character( gentity_t *ent ) {
	const bgModel_t *model;
	char *name;
	int anim, health, frame, ragdoll;

	G_SpawnString( "model", "", &name );
	ent->s.modelindex2 = BG_ModelForName( name );
	if ( !BG_ValidModel( ent->s.modelindex2 ) ) {
		G_Printf( S_COLOR_YELLOW "WARNING: misc_character at %s: no model def \"%s\"\n", vtos( ent->s.origin ), name );
		G_FreeEntity( ent );
		return;
	}
	model = &bg_models[ent->s.modelindex2];

	ent->s.eType = ET_CHARACTER;
	ent->characterDef = ent->s.modelindex2;
	ent->s.modelindex = G_ModelIndex( (char *)model->mesh );
	if ( !model->numJoints ) {
		G_Printf( S_COLOR_YELLOW "WARNING: misc_character at %s: %s has no skeleton\n", vtos( ent->s.origin ), model->mesh );
	}
	ent->characterHistory = trap_HeapMalloc( sizeof( *ent->characterHistory ) );
	G_ResetCharacterHistory( ent->characterHistory );

	DictGetVector( model->def, "mins", "-16 -16 -24", ent->s.mins );
	DictGetVector( model->def, "maxs", "16 16 48", ent->s.maxs );
	G_SpawnFloat( "modelscale", "1", &ent->s.origin2[0] );
	if ( ent->s.origin2[0] <= 0.0f ) {
		ent->s.origin2[0] = 1.0f;
	}
	VectorScale( ent->s.mins, ent->s.origin2[0], ent->s.mins );
	VectorScale( ent->s.maxs, ent->s.origin2[0], ent->s.maxs );
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
	// a frame of it: as if it began that long ago
	G_SpawnInt( "frame", "0", &frame );
	if ( frame > 0 && ent->s.legsAnim < model->numAnims && model->anims[ent->s.legsAnim].fps > 0 ) {
		ent->s.time -= frame * 1000 / model->anims[ent->s.legsAnim].fps;
	}
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

	// a body: limp from the start, from the pose it's put in
	G_SpawnInt( "ragdoll", "0", &ragdoll );
	if ( ragdoll && G_StartRagdoll( ent ) ) {
		ent->takedamage = qtrue;		// shots push it
		ent->health = 1 << 30;
		if ( ragdoll != 2 ) {
			G_RagdollRest( ent );		// lying as it's put, till it's hit (2: falls from it)
		}
	}
}

/*
================
Svcmd_SpawnCharacter_f

spawncharacter <model def> [anim] [height] [side] [ragdoll]: puts one 96
units in front of the first player, facing them, height units up and side
units to their left; ragdoll 1 limp in that pose till it's hit, 2 falling
from it (cheat, for testing).
================
*/
void Svcmd_SpawnCharacter_f( void ) {
	gentity_t *player, *ent;
	vec3_t forward;
	char name[MAX_QPATH], anim[MAX_QPATH], height[16], side[16], ragdoll[16];
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
	trap_Argv( 5, ragdoll, sizeof( ragdoll ) );

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
	if ( atoi( ragdoll ) ) {
		level.spawnVars[level.numSpawnVars][0] = G_AddSpawnVarToken( "ragdoll" );
		level.spawnVars[level.numSpawnVars++][1] = G_AddSpawnVarToken( ragdoll );
	}
	SP_misc_character( ent );
	level.numSpawnVars = 0;
	if ( ent->inuse ) {
		G_Printf( "character %d\n", ent->s.number );
	}
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
