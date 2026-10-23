/*
===========================================================================
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

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
// g_ragdoll.c -- the server's ragdolls: Doom 3's idAF on a character
// (idActor::StartRagdoll), every client seeing the same
//
// The figure collides through the game's clip (Doom 3's idClip), its bodies
// go to the clients in the entity's snapshot words (bg_snapshot.c).

#include "g_local.h"
#include "../game/bg_af.h"

#define RAGDOLL_STEP_MSEC		16			// Doom 3's USERCMD_MSEC

typedef struct gRagdoll_s {
	afFigure_t		af;
	clipModel_t		clips[AF_MAX_BODIES];	// the bodies' trace models, for the clip
	gentity_t		*self;
	int				time;					// simulated up to
	int				soundTime;				// of its last bounce
} gRagdoll_t;

#define RAGDOLL_BOUNCE_SPEED	60			// a body hitting something this fast is heard
#define RAGDOLL_BOUNCE_MSEC		250			// and not again sooner

/*
================
G_RagdollImpact

idAFEntity_Base::Collide: a body hitting something hard bounces
("snd_bounce", as an entity event)
================
*/
static void G_RagdollImpact( void *ctx, const afPhysics_t *af, int body, const cm_trace_t *collision, const vec3_t impulse ) {
	gRagdoll_t *rd = ctx;
	float speed = AF_ImpactSpeed( af, body, impulse );
	const char *sound;

	if ( collision->c.entityNum < 0 || speed < RAGDOLL_BOUNCE_SPEED || level.time - rd->soundTime < RAGDOLL_BOUNCE_MSEC ) {
		return;
	}
	rd->soundTime = level.time;
	sound = DictGetStringReference( bg_models[rd->self->characterDef].def, "snd_bounce", "" );
	G_SendSoundEvent( rd->self, CHAN_BODY, sound, speed / 300.0f );
}

static qboolean G_RagdollMotion( void *ctx, const afPhysics_t *af, int body, cm_trace_t *results, const vec3_t start,
								const vec3_t end, const rotation_t *rotation, const traceModel_t *trm, const vec3_t axis[3], int mask ) {
	gRagdoll_t *rd = ctx;

	return ClipMotion( &g_clip, results, start, end, rotation, &rd->clips[body], axis, mask, (sharedEntity_t *)rd->self );
}

static int G_RagdollContacts( void *ctx, const afPhysics_t *af, int body, contactInfo_t *contacts, int maxContacts,
							const vec3_t origin, const vec6_t dir, float depth, const traceModel_t *trm, const vec3_t axis[3], int mask ) {
	gRagdoll_t *rd = ctx;

	return ClipContacts( &g_clip, contacts, maxContacts, origin, dir, depth, &rd->clips[body], axis, mask, (sharedEntity_t *)rd->self );
}

static cmHandle_t G_RagdollTrmModel( void *ctx, const traceModel_t *trm ) {
	return trap_CM_SetupTrmModel( trm, 0 );
}

/*
================
G_StartRagdoll

idActor::StartRagdoll: the character goes limp from the pose it has now;
qfalse when its model has no figure
================
*/
qboolean G_StartRagdoll( gentity_t *ent ) {
	const bgModel_t *model;
	const afDecl_t *decl;
	gRagdoll_t *rd;
	bgAnimChannel_t channels[MAX_ANIM_CHANNELS];
	float pose[MAX_MODEL_JOINTS * 12], bindPose[MAX_MODEL_JOINTS * 12];
	vec3_t axis[3];
	int i, numJoints;

	if ( ent->ragdoll || !BG_ValidModel( ent->characterDef ) ) {
		return qfalse;
	}
	model = &bg_models[ent->characterDef];
	if ( !model->ragdoll[0] || !( decl = BG_FindAFDecl( model->ragdoll ) ) ) {
		G_DPrintf( "%s %d: no ragdoll (\"%s\")\n", ent->classname, ent->s.number, model->ragdoll );
		return qfalse;
	}

	rd = trap_HeapMalloc( sizeof( *rd ) );
	if ( !rd ) {
		return qfalse;
	}
	memset( rd, 0, sizeof( *rd ) );
	rd->self = ent;

	BG_ModelBindPose( model, bindPose );
	if ( !BG_AFLoad( &rd->af, decl, model, bindPose, model->numJoints,
					 ent->s.origin2[0] > 0.0f ? ent->s.origin2[0] : 1.0f ) ) {
		G_DPrintf( "%s %d: its ragdoll %s doesn't fit %s\n", ent->classname, ent->s.number, model->ragdoll, model->name );
		trap_HeapFree( rd );
		return qfalse;
	}
	for ( i = 0; i < rd->af.physics.numBodies; i++ ) {
		ClipModelInitFromTraceModel( &rd->clips[i], &rd->af.physics.bodies[i].trm );
	}
	ent->ragdoll = rd;
	rd->af.physics.clip.ctx = rd;
	rd->af.physics.clip.motion = G_RagdollMotion;
	rd->af.physics.clip.contacts = G_RagdollContacts;
	rd->af.physics.clip.impact = G_RagdollImpact;
	rd->af.physics.clip.trmModel = G_RagdollTrmModel;
	VectorSet( rd->af.physics.gravityVector, 0, 0, -g_gravity.value );

	// the pose it has (from nothing before: an uninitialized channel's
	// "previous animation" was what showed while the new one faded in, a
	// body put in its coffin pose limp in its model's first, standing)
	memset( channels, 0, sizeof( channels ) );
	for ( i = 0; i < MAX_ANIM_CHANNELS; i++ ) {
		channels[i].anim = channels[i].prevAnim = -1;
	}
	BG_AnimChannelsFromState( channels, ent->s.legsAnim, ent->s.time, CHARACTER_LEGS_RATE( &ent->s ), ent->s.torsoAnim, ent->s.time2 );
	numJoints = BG_ModelPose( model, channels, level.time, pose );
	if ( numJoints <= 0 ) {
		G_FreeRagdoll( ent );
		return qfalse;
	}
	AnglesToAxis( ent->r.currentAngles, axis );
	BG_AFSetupPose( &rd->af, pose, ent->r.currentOrigin, (const vec3_t *)axis, level.time );
	BG_AFTestSolid( &rd->af );
	BG_AFStart( &rd->af, level.time );
	rd->time = level.time;

	// a corpse now: its bodies go to the clients
	ent->s.contents = CONTENTS_CORPSE;
	ent->s.pos.trType = TR_INTERPOLATE;
	VectorClear( ent->s.apos.trBase );
	BG_WriteAFSnapshot( &rd->af, &ent->s );
	G_LinkEntity( ent );
	return qtrue;
}

/*
================
G_RunRagdoll

the figure up to now; its bodies to the snapshot
================
*/
void G_RunRagdoll( gentity_t *ent ) {
	gRagdoll_t *rd = ent->ragdoll;
	vec3_t bounds[2];

	if ( !rd ) {
		return;
	}
	rd->af.physics.gravityVector[2] = -g_gravity.value;
	while ( rd->time + RAGDOLL_STEP_MSEC <= level.time ) {
		rd->time += RAGDOLL_STEP_MSEC;
		AF_Evaluate( &rd->af.physics, RAGDOLL_STEP_MSEC, rd->time );
	}
	if ( AF_IsAtRest( &rd->af.physics ) ) {
		return;		// nothing changes: nothing is sent
	}

	BG_WriteAFSnapshot( &rd->af, &ent->s );
	VectorCopy( ent->s.pos.trBase, ent->r.currentOrigin );

	// the box about the bodies, for the snapshot's visibility
	AF_GetBounds( &rd->af.physics, bounds );
	VectorSubtract( bounds[0], ent->r.currentOrigin, ent->s.mins );
	VectorSubtract( bounds[1], ent->r.currentOrigin, ent->s.maxs );
	G_LinkEntity( ent );
}

/*
================
G_RagdollImpulse

a shot or a blast pushes the body nearest the point
================
*/
void G_RagdollImpulse( gentity_t *ent, const vec3_t point, const vec3_t impulse ) {
	gRagdoll_t *rd = ent->ragdoll;
	float d, best = 0;
	int i, nearest = 0;

	if ( !rd ) {
		return;
	}
	for ( i = 0; i < rd->af.physics.numBodies; i++ ) {
		d = DistanceSquared( point, rd->af.physics.bodies[i].current->worldOrigin );
		if ( i == 0 || d < best ) {
			best = d;
			nearest = i;
		}
	}
	AF_ApplyImpulse( &rd->af.physics, nearest, point, impulse );
}

void G_FreeRagdoll( gentity_t *ent ) {
	gRagdoll_t *rd = ent->ragdoll;
	int i;

	if ( !rd ) {
		return;
	}
	for ( i = 0; i < rd->af.physics.numBodies; i++ ) {
		ClipModelFree( &rd->clips[i] );
	}
	trap_HeapFree( rd );
	ent->ragdoll = NULL;
}
