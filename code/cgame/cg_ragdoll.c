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
// cg_ragdoll.c -- dead players go limp: Doom 3's articulated figures
// (bg_af.c), simulated by each client as Doom 3's multiplayer does
// (idPlayer::ReadFromSnapshot starts the ragdoll when the health drops)
//
// A skeletal player whose model def names a "ragdoll" becomes one when it
// dies, from the pose it had, moving as it was; the corpse the body queue
// leaves when it respawns takes the ragdoll over. They collide with the
// world and the movers.

#include "cg_local.h"
#include "../game/bg_af.h"

#define MAX_RAGDOLLS			8
#define RAGDOLL_STEP_MSEC		16			// Doom 3's USERCMD_MSEC
#define RAGDOLL_MAX_STEPS		8			// a frame catches up no more
#define RAGDOLL_ORPHAN_MSEC		1000		// a respawned player's waits for its corpse this long

typedef struct {
	qboolean	inUse;
	qboolean	server;					// the server runs it: its bodies come in the snapshots
	int			entityNum;				// whose it is
	int			playerNum;
	int			modelDef;
	qboolean	orphan;					// its player's alive again: its corpse may take it
	int			lastSeen;				// cg.time
	int			time;					// simulated up to
	afFigure_t	af;
} ragdoll_t;

static ragdoll_t	cg_ragdolls[MAX_RAGDOLLS];

/*
===============================================================================

	Collision: the world and the movers

===============================================================================
*/

static void CG_RagdollStoreTouch( cm_trace_t *best, const cm_trace_t *trace, int entityNum ) {
	*best = *trace;
	best->c.entityNum = entityNum;
	best->c.id = 0;
}

static qboolean CG_RagdollMotion( void *ctx, const afPhysics_t *af, int body, cm_trace_t *results, const vec3_t start,
								const vec3_t end, const rotation_t *rotation, const traceModel_t *trm, const vec3_t axis[3], int mask ) {
	centity_t **solids, *cent;
	cm_trace_t translational, rotational, trace;
	rotation_t endRotation;
	vec3_t endPosition, modelAxis[3], origin;
	qboolean rotates;
	int i, num;

	num = CG_SolidEntities( &solids );
	rotates = rotation->angle != 0.0f && !VectorCompare( rotation->vec, vec3_origin );

	// translation
	if ( !VectorCompare( start, end ) ) {
		trap_CM_Translation( &translational, start, end, trm, axis, mask, 0, vec3_origin, axisDefault );
		translational.c.entityNum = translational.fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
		for ( i = 0; i < num && translational.fraction > 0.0f; i++ ) {
			cent = solids[i];
			if ( cent->currentState.collisionType != CT_SUBMODEL || !( cent->currentState.contents & mask ) ) {
				continue;
			}
			BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, origin );
			AnglesToAxis( cent->lerpAngles, modelAxis );
			trap_CM_Translation( &trace, start, end, trm, axis, mask, trap_CM_InlineModel( cent->currentState.modelindex ),
								origin, (const vec3_t *)modelAxis );
			if ( trace.fraction < translational.fraction ) {
				CG_RagdollStoreTouch( &translational, &trace, cent->currentState.number );
			}
		}
	} else {
		memset( &translational, 0, sizeof( translational ) );
		translational.fraction = 1.0f;
		VectorCopy( end, translational.endpos );
		AxisCopy( axis, translational.endAxis );
		translational.c.entityNum = ENTITYNUM_NONE;
	}

	if ( !rotates ) {
		*results = translational;
		return translational.fraction < 1.0f;
	}

	// then the rotation, where the translation ended
	VectorCopy( translational.endpos, endPosition );
	endRotation = *rotation;
	VectorCopy( endPosition, endRotation.origin );

	trap_CM_Rotation( &rotational, endPosition, &endRotation, trm, axis, mask, 0, vec3_origin, axisDefault );
	rotational.c.entityNum = rotational.fraction != 1.0f ? ENTITYNUM_WORLD : ENTITYNUM_NONE;
	for ( i = 0; i < num && rotational.fraction > 0.0f; i++ ) {
		cent = solids[i];
		if ( cent->currentState.collisionType != CT_SUBMODEL || !( cent->currentState.contents & mask ) ) {
			continue;
		}
		BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, origin );
		AnglesToAxis( cent->lerpAngles, modelAxis );
		trap_CM_Rotation( &trace, endPosition, &endRotation, trm, axis, mask, trap_CM_InlineModel( cent->currentState.modelindex ),
						  origin, (const vec3_t *)modelAxis );
		if ( trace.fraction < rotational.fraction ) {
			CG_RagdollStoreTouch( &rotational, &trace, cent->currentState.number );
		}
	}

	if ( rotational.fraction < 1.0f ) {
		*results = rotational;
	} else {
		*results = translational;
		AxisCopy( rotational.endAxis, results->endAxis );
	}
	results->fraction = MAX( translational.fraction, rotational.fraction );

	return translational.fraction < 1.0f || rotational.fraction < 1.0f;
}

static int CG_RagdollContacts( void *ctx, const afPhysics_t *af, int body, contactInfo_t *contacts, int maxContacts,
								const vec3_t origin, const vec6_t dir, float depth, const traceModel_t *trm, const vec3_t axis[3], int mask ) {
	centity_t **solids, *cent;
	vec3_t modelAxis[3], entOrigin;
	int i, j, num, n, numSolids;

	n = trap_CM_Contacts( contacts, maxContacts, origin, dir, depth, trm, axis, mask, 0, vec3_origin, axisDefault );
	for ( j = 0; j < n; j++ ) {
		contacts[j].entityNum = ENTITYNUM_WORLD;
		contacts[j].id = 0;
	}

	numSolids = CG_SolidEntities( &solids );
	for ( i = 0; i < numSolids && n < maxContacts; i++ ) {
		cent = solids[i];
		if ( cent->currentState.collisionType != CT_SUBMODEL || !( cent->currentState.contents & mask ) ) {
			continue;
		}
		BG_EvaluateTrajectory( &cent->currentState.pos, cg.time, entOrigin );
		AnglesToAxis( cent->lerpAngles, modelAxis );
		num = trap_CM_Contacts( contacts + n, maxContacts - n, origin, dir, depth, trm, axis, mask,
								trap_CM_InlineModel( cent->currentState.modelindex ), entOrigin, (const vec3_t *)modelAxis );
		for ( j = 0; j < num; j++ ) {
			contacts[n + j].entityNum = cent->currentState.number;
			contacts[n + j].id = 0;
		}
		n += num;
	}
	return n;
}

/*
===============================================================================

	Ragdolls

===============================================================================
*/

static ragdoll_t *CG_RagdollForEntity( int entityNum ) {
	int i;

	for ( i = 0; i < MAX_RAGDOLLS; i++ ) {
		if ( cg_ragdolls[i].inUse && cg_ragdolls[i].entityNum == entityNum ) {
			return &cg_ragdolls[i];
		}
	}
	return NULL;
}

static ragdoll_t *CG_AllocRagdoll( void ) {
	ragdoll_t *oldest = NULL;
	int i;

	for ( i = 0; i < MAX_RAGDOLLS; i++ ) {
		if ( !cg_ragdolls[i].inUse ) {
			return &cg_ragdolls[i];
		}
		if ( !oldest || cg_ragdolls[i].lastSeen < oldest->lastSeen ) {
			oldest = &cg_ragdolls[i];
		}
	}
	return oldest;
}

/*
================
CG_ClearRagdolls
================
*/
void CG_ClearRagdolls( void ) {
	memset( cg_ragdolls, 0, sizeof( cg_ragdolls ) );
}

/*
================
CG_UpdateRagdolls

Drops what no one has drawn for a while, and orphans no corpse took.
================
*/
void CG_UpdateRagdolls( void ) {
	ragdoll_t *rd;
	int i;

	for ( i = 0; i < MAX_RAGDOLLS; i++ ) {
		rd = &cg_ragdolls[i];
		if ( !rd->inUse ) {
			continue;
		}
		if ( cg.time < rd->lastSeen || cg.time - rd->lastSeen > RAGDOLL_ORPHAN_MSEC ) {
			rd->inUse = qfalse;
		}
	}
}

/*
================
CG_RagdollAlive

A player alive again: its ragdoll waits for the corpse the body queue makes.
================
*/
void CG_RagdollAlive( centity_t *cent ) {
	ragdoll_t *rd = CG_RagdollForEntity( cent->currentState.number );

	if ( rd && !rd->orphan ) {
		rd->orphan = qtrue;
		rd->lastSeen = cg.time;
	}
}

static void CG_RagdollDebugLine( const vec3_t start, const vec3_t end, int color ) {
	static const vec4_t colors[3] = { { 0.2f, 1.0f, 0.2f, 1.0f }, { 0.2f, 0.4f, 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } };

	CG_DebugLine( start, end, colors[color] );
}

/*
================
CG_RagdollStart

idAF::StartFromCurrentPose: the figure where the model is, moving as it was
================
*/
static ragdoll_t *CG_RagdollStart( centity_t *cent, int modelDef, const refEntity_t *legs, const float *jointMats, int numJoints ) {
	const bgModel_t *model = &bg_models[modelDef];
	const afDecl_t *decl;
	float bindPose[MAX_MODEL_JOINTS * 12];
	ragdoll_t *rd;
	vec3_t velocity;
	int i;

	decl = BG_FindAFDecl( model->ragdoll );
	if ( !decl ) {
		return NULL;
	}

	rd = CG_AllocRagdoll();
	memset( rd, 0, sizeof( *rd ) );

	// the figure is made in the model's bind pose
	BG_ModelBindPose( model, bindPose );
	if ( !BG_AFLoad( &rd->af, decl, model, bindPose, model->numJoints ) ) {
		return NULL;
	}

	rd->inUse = qtrue;
	rd->entityNum = cent->currentState.number;
	rd->playerNum = cent->currentState.playerNum;
	rd->modelDef = modelDef;
	rd->lastSeen = cg.time;
	rd->time = cg.time;

	rd->af.physics.clip.motion = CG_RagdollMotion;
	rd->af.physics.clip.contacts = CG_RagdollContacts;
	rd->af.physics.clip.ctx = rd;
	VectorSet( rd->af.physics.gravityVector, 0, 0, -( cg.snap && cg.snap->pss[0].gravity ? cg.snap->pss[0].gravity : DEFAULT_GRAVITY ) );

	BG_AFSetupPose( &rd->af, jointMats, legs->origin, (const vec3_t *)legs->axis, cg.time );

	// moving as the player was (a player's pos.trDelta is its velocity, the
	// knockback of what killed it in it)
	if ( cent == &cg.cur_lc->predictedPlayerEntity ) {
		VectorCopy( cg.cur_lc->predictedPlayerState.velocity, velocity );
	} else {
		VectorCopy( cent->currentState.pos.trDelta, velocity );
	}
	for ( i = 0; i < rd->af.physics.numBodies; i++ ) {
		VectorCopy( velocity, rd->af.physics.bodies[i].current->spatialVelocity );
	}

	BG_AFTestSolid( &rd->af );
	BG_AFStart( &rd->af, cg.time );
	return rd;
}

/*
================
CG_Ragdoll

A dead skeletal player's pose (model space jointMats) and where its model
is (legs' origin and axis) from its ragdoll, started from the pose it has;
qfalse when it has none.
================
*/
qboolean CG_Ragdoll( centity_t *cent, int modelDef, refEntity_t *legs, float *jointMats, int numJoints ) {
	const bgModel_t *model = &bg_models[modelDef];
	ragdoll_t *rd;
	int steps, i;

	if ( !cg_ragdoll.integer || !model->ragdoll[0] || numJoints <= 0 ) {
		return qfalse;
	}

	rd = CG_RagdollForEntity( cent->currentState.number );
	if ( rd && rd->modelDef != modelDef ) {
		rd->inUse = qfalse;
		rd = NULL;
	}
	if ( !rd && cent->currentState.number != cent->currentState.playerNum ) {
		// a corpse the body queue left: its player's ragdoll, if it has one
		for ( i = 0; i < MAX_RAGDOLLS; i++ ) {
			if ( cg_ragdolls[i].inUse && cg_ragdolls[i].orphan && cg_ragdolls[i].playerNum == cent->currentState.playerNum
				 && cg_ragdolls[i].modelDef == modelDef ) {
				rd = &cg_ragdolls[i];
				rd->entityNum = cent->currentState.number;
				rd->orphan = qfalse;
				break;
			}
		}
	}
	if ( !rd ) {
		rd = CG_RagdollStart( cent, modelDef, legs, jointMats, numJoints );
		if ( !rd ) {
			return qfalse;
		}
	}
	rd->lastSeen = cg.time;

	// run it up to now in Doom 3's steps
	if ( cg.time < rd->time ) {
		rd->time = cg.time;
	}
	for ( steps = 0; rd->time + RAGDOLL_STEP_MSEC <= cg.time; steps++ ) {
		if ( steps >= RAGDOLL_MAX_STEPS ) {
			rd->time = cg.time;		// too far behind: the rest is skipped
			break;
		}
		rd->time += RAGDOLL_STEP_MSEC;
		AF_Evaluate( &rd->af.physics, RAGDOLL_STEP_MSEC, rd->time );
	}

	BG_AFPose( &rd->af, jointMats, legs->origin, legs->axis );
	legs->origin[2] -= cent->bodySink;		// the corpse sinking away
	VectorCopy( legs->origin, legs->oldorigin );
	VectorCopy( legs->origin, legs->lightingOrigin );

	if ( cg_drawRagdolls.integer ) {
		AF_DebugDraw( &rd->af.physics, CG_RagdollDebugLine );
	}
	return qtrue;
}

/*
================
CG_ServerRagdoll

An entity whose snapshot holds an articulated figure (the server's
ragdoll, bg_snapshot.c): the model's pose from its bodies, interpolated
between snapshots; qfalse when it holds none.
================
*/
qboolean CG_ServerRagdoll( centity_t *cent, int modelDef, refEntity_t *ent, float *jointMats, int numJoints ) {
	const bgModel_t *model = &bg_models[modelDef];
	const afDecl_t *decl;
	float bindPose[MAX_MODEL_JOINTS * 12];
	ragdoll_t *rd;

	if ( ( cent->currentState.snapshot[0] & 0xff ) != SNAPSHOT_AF || !model->ragdoll[0] || numJoints <= 0 ) {
		return qfalse;
	}

	rd = CG_RagdollForEntity( cent->currentState.number );
	if ( rd && ( !rd->server || rd->modelDef != modelDef ) ) {
		rd->inUse = qfalse;
		rd = NULL;
	}
	if ( !rd ) {
		decl = BG_FindAFDecl( model->ragdoll );
		if ( !decl ) {
			return qfalse;
		}
		rd = CG_AllocRagdoll();
		memset( rd, 0, sizeof( *rd ) );
		BG_ModelBindPose( model, bindPose );
		if ( !BG_AFLoad( &rd->af, decl, model, bindPose, model->numJoints ) ) {
			return qfalse;
		}
		rd->inUse = qtrue;
		rd->server = qtrue;
		rd->entityNum = cent->currentState.number;
		rd->playerNum = -1;
		rd->modelDef = modelDef;
	}
	rd->lastSeen = cg.time;

	if ( !BG_ReadAFSnapshot( &rd->af, &cent->currentState, cent->interpolate ? &cent->nextState : NULL,
							 cg.frameInterpolation, cent->lerpOrigin ) ) {
		return qfalse;
	}
	BG_AFPose( &rd->af, jointMats, ent->origin, ent->axis );
	VectorCopy( ent->origin, ent->oldorigin );
	VectorCopy( ent->origin, ent->lightingOrigin );
	return qtrue;
}
