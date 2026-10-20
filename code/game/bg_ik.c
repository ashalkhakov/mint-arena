/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

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
// bg_ik.c -- Doom 3's walk IK (idIK_Walk, game/IK.cpp): feet planted on the
// floor under them, on stairs and slopes
//
// After the animations are blended, each foot's floor is traced; the ankles
// move to it (smoothed), the waist drops so the lower foot reaches, and each
// leg is solved as two bones, hip to knee to ankle, keeping its bend; the
// ankle keeps the orientation it had. The model def's keys are Doom 3's:
//
//	"ik_numLegs" 2, "ik_waist" "Body",
//	"ik_hip1" "Lupleg", "ik_knee1" "Lloleg", "ik_ankle1" "Lankle_r", "ik_foot1" "Lankle_r", (2 for the other)
//	"ik_footSize" 4, "ik_smoothing" 0.75, "ik_waistSmoothing" 0.75, "ik_footShift" 0,
//	"ik_waistShift" 0, "ik_minWaistAnkleDist" 0, "ik_footUpTrace" 32, "ik_footDownTrace" 32
//
// (no "ik_dir": the bend is the knee's in the pose.)

#ifdef CGAME
#include "../cgame/cg_local.h"
#define BG_IK_Trace( tr, start, mins, maxs, end, pass, mask )	CG_Trace( tr, start, mins, maxs, end, pass, mask )
#else
#include "g_local.h"
#define BG_IK_Trace( tr, start, mins, maxs, end, pass, mask )	G_TraceBox( tr, start, mins, maxs, end, pass, mask )
#endif

/*
================
BG_LoadModelIK

The model def's "ik_*" keys.
================
*/
void BG_LoadModelIK( bgModel_t *model ) {
	const struct dict_s *def = model->def;
	int i;

	model->ik.numLegs = DictGetInt( def, "ik_numLegs", "0" );
	if ( model->ik.numLegs > MAX_IK_LEGS ) {
		model->ik.numLegs = MAX_IK_LEGS;
	}
	for ( i = 0; i < model->ik.numLegs; i++ ) {
		model->ik.hip[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_hip%d", i + 1 ), "" ) );
		model->ik.knee[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_knee%d", i + 1 ), "" ) );
		model->ik.ankle[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_ankle%d", i + 1 ), "" ) );
		model->ik.foot[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_foot%d", i + 1 ), "" ) );
		if ( model->ik.foot[i] < 0 ) {
			model->ik.foot[i] = model->ik.ankle[i];
		}
		if ( model->ik.hip[i] < 0 || model->ik.knee[i] < 0 || model->ik.ankle[i] < 0 ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: model %s: leg %d of its IK has no joint\n", model->name, i + 1 );
			model->ik.numLegs = 0;
			return;
		}
	}
	// the arms (Doom 3's idIK_Reach keys)
	model->ik.numArms = Com_Clamp( 0, MAX_IK_LEGS, DictGetInt( def, "ik_numArms", "0" ) );
	for ( i = 0; i < model->ik.numArms; i++ ) {
		model->ik.shoulder[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_shoulder%d", i + 1 ), "" ) );
		model->ik.elbow[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_elbow%d", i + 1 ), "" ) );
		model->ik.hand[i] = BG_ModelJointIndex( model, DictGetStringReference( def, va( "ik_hand%d", i + 1 ), "" ) );
		if ( model->ik.shoulder[i] < 0 || model->ik.elbow[i] < 0 || model->ik.hand[i] < 0 ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: model %s: arm %d of its IK has no joint\n", model->name, i + 1 );
			model->ik.numArms = 0;
			break;
		}
	}
	model->ik.waist = BG_ModelJointIndex( model, DictGetStringReference( def, "ik_waist", "" ) );
	if ( model->ik.numLegs && model->ik.waist < 0 ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: model %s: its IK has no waist joint\n", model->name );
		model->ik.numLegs = 0;
		return;
	}
	model->ik.footSize = DictGetFloat( def, "ik_footSize", "4" ) * 0.5f;
	model->ik.smoothing = DictGetFloat( def, "ik_smoothing", "0.75" );
	model->ik.waistSmoothing = DictGetFloat( def, "ik_waistSmoothing", "0.75" );
	model->ik.footShift = DictGetFloat( def, "ik_footShift", "0" );
	model->ik.waistShift = DictGetFloat( def, "ik_waistShift", "0" );
	model->ik.minWaistAnkleDist = DictGetFloat( def, "ik_minWaistAnkleDist", "0" );
	model->ik.footUpTrace = DictGetFloat( def, "ik_footUpTrace", "32" );
	model->ik.footDownTrace = DictGetFloat( def, "ik_footDownTrace", "32" );
}

#define IK_MAX_SHIFT	18		// a step (STEPSIZE)

static void BG_IK_JointOrigin( const float *jointMats, int joint, vec3_t out ) {
	out[0] = jointMats[12 * joint + 3];
	out[1] = jointMats[12 * joint + 7];
	out[2] = jointMats[12 * joint + 11];
}

/*
================
BG_IK_SolveTwoBones

Doom 3's idIK::SolveTwoBones: where the joint between two bones of lengths
len0 and len1 goes, from start to end, bending toward dir.
================
*/
static qboolean BG_IK_SolveTwoBones( const vec3_t start, const vec3_t end, const vec3_t dir, float len0, float len1,
									 vec3_t joint ) {
	vec3_t vec0, vec1;
	float length, x, y;

	VectorSubtract( end, start, vec0 );
	length = VectorLength( vec0 );
	if ( length > len0 + len1 || length < fabs( len0 - len1 ) || length < 0.001f ) {
		VectorMA( start, 0.5f, vec0, joint );
		return qfalse;
	}
	VectorScale( vec0, 1.0f / length, vec0 );
	VectorMA( dir, -DotProduct( dir, vec0 ), vec0, vec1 );
	VectorNormalize( vec1 );

	x = ( length * length + len0 * len0 - len1 * len1 ) * ( 0.5f / length );
	y = len0 * len0 - x * x;
	y = y > 0 ? sqrt( y ) : 0;
	VectorMA( start, x, vec0, joint );
	VectorMA( joint, y, vec1, joint );
	return qtrue;
}

/*
================
BG_IK_TurnBone

Turns a joint (and what's under it) about itself so its bone goes from
pivot->oldEnd along pivot->newEnd: the smallest rotation that does it, so the
bone never twists about its length (a twisted thigh is a broken hip).
================
*/
static void BG_IK_TurnBone( const bgModel_t *model, float *jointMats, int numJoints, int joint,
							const vec3_t pivot, const vec3_t oldEnd, const vec3_t newEnd ) {
	vec3_t from, to, axis;
	float rot[3][3], c, s, t;
	int r, k;

	VectorSubtract( oldEnd, pivot, from );
	VectorSubtract( newEnd, pivot, to );
	if ( VectorNormalize( from ) < 0.001f || VectorNormalize( to ) < 0.001f ) {
		return;
	}
	CrossProduct( from, to, axis );
	s = VectorNormalize( axis );
	c = DotProduct( from, to );
	if ( s < 0.0001f ) {
		return;		// along already (or right back, which the solve never asks)
	}
	// Rodrigues: c I + s [axis]x + (1 - c) axis axis^T
	t = 1.0f - c;
	for ( r = 0; r < 3; r++ ) {
		for ( k = 0; k < 3; k++ ) {
			rot[r][k] = t * axis[r] * axis[k] + ( r == k ? c : 0 );
		}
	}
	rot[0][1] -= s * axis[2];	rot[1][0] += s * axis[2];
	rot[0][2] += s * axis[1];	rot[2][0] -= s * axis[1];
	rot[1][2] -= s * axis[0];	rot[2][1] += s * axis[0];
	BG_ModelRotateJoints( model, jointMats, numJoints, joint, rot );
}

/*
================
BG_ModelWalkIK

Plants a pose's feet (jointMats, model space) on the floor under them: the
model at origin with axis, standing at floorHeight (world z; where the
entity's box rests). state keeps the smoothing from frame to frame; with no
state, unsmoothed: the pose this moment (the game's hitboxes, at any time).
Nothing when the model has no IK, or isn't on the ground.
================
*/
void BG_ModelWalkIK( const bgModel_t *model, float *jointMats, int numJoints, const vec3_t origin,
					 const vec3_t axis[3], float floorHeight, qboolean onGround, int passEntityNum, int time,
					 bgIKState_t *state ) {
	const bgModelIK_t *ik = &model->ik;
	vec3_t up, start, end, mins, maxs, joint, world, waistOffset, kneeDir;
	vec3_t hip[MAX_IK_LEGS], knee[MAX_IK_LEGS], ankle[MAX_IK_LEGS], target[MAX_IK_LEGS];
	float shift, smallestShift, height, largestAnkleHeight, step, lerp, waistLerp, floors[MAX_IK_LEGS];
	float ankleRot[MAX_IK_LEGS][3][3], rot[3][3], len0, len1;
	trace_t tr;
	int i, r, c, k, msec;
	bgIKState_t once;
	qboolean smooth = state != NULL;

	if ( !ik->numLegs || numJoints <= ik->waist ) {
		return;
	}
	if ( !smooth ) {
		memset( &once, 0, sizeof( once ) );
		state = &once;
	}
	if ( !onGround ) {
		state->valid = qfalse;
		return;
	}

	// the smoothing Doom 3 does a 60 Hz frame, at the frame's length
	msec = state->valid ? time - state->time : 0;
	if ( msec < 0 ) {
		msec = 0;
	}
	lerp = state->valid ? pow( ik->smoothing, msec / 16.0f ) : 0;
	waistLerp = state->valid ? pow( ik->waistSmoothing, msec / 16.0f ) : 0;
	state->time = time;

	// the world's up in the model's space (players lean)
	for ( i = 0; i < 3; i++ ) {
		up[i] = axis[i][2];
	}
	VectorSet( mins, -ik->footSize, -ik->footSize, 0 );
	VectorSet( maxs, ik->footSize, ik->footSize, 0 );

	smallestShift = 1e9f;
	largestAnkleHeight = -1e9f;
	for ( i = 0; i < ik->numLegs; i++ ) {
		// the floor under the foot
		BG_IK_JointOrigin( jointMats, ik->foot[i], joint );
		VectorCopy( origin, world );
		for ( k = 0; k < 3; k++ ) {
			VectorMA( world, joint[k], axis[k], world );
		}
		VectorCopy( world, start );
		start[2] += ik->footUpTrace;
		VectorCopy( world, end );
		end[2] -= ik->footDownTrace;
		BG_IK_Trace( &tr, start, mins, maxs, end, passEntityNum, MASK_PLAYERSOLID );
		// no floor found, or the trace began in a wall: the foot stays
		floors[i] = ( tr.startsolid || tr.allsolid || tr.fraction == 1.0f ) ? floorHeight : tr.endpos[2];

		// the ankle up or down by as much as the floor is from where the
		// entity stands, smoothed; never more than a step, as the entity's
		// box couldn't stand there either
		shift = floors[i] - floorHeight + ik->footShift;
		shift = Com_Clamp( -IK_MAX_SHIFT, IK_MAX_SHIFT, shift );
		if ( shift < smallestShift ) {
			smallestShift = shift;
		}
		BG_IK_JointOrigin( jointMats, ik->ankle[i], ankle[i] );
		height = origin[2] + DotProduct( ankle[i], up );	// world heights, as Doom 3 smooths
		if ( state->valid ) {
			step = height + shift - state->ankleHeights[i];
			shift -= lerp * step;
		}
		state->ankleHeights[i] = height + shift;
		if ( height + shift > largestAnkleHeight ) {
			largestAnkleHeight = height + shift;
		}
		VectorMA( ankle[i], shift, up, target[i] );
	}

	// the waist drops so the lower foot reaches
	BG_IK_JointOrigin( jointMats, ik->waist, joint );
	shift = smallestShift + ik->waistShift;
	if ( ik->minWaistAnkleDist > 0 ) {
		height = origin[2] + DotProduct( joint, up ) + shift;
		if ( height - largestAnkleHeight < ik->minWaistAnkleDist ) {
			shift += ik->minWaistAnkleDist - ( height - largestAnkleHeight );
		}
	}
	height = origin[2] + DotProduct( joint, up ) + shift;
	if ( state->valid ) {
		step = height - state->waistHeight;
		shift -= waistLerp * step;
	}
	state->waistHeight = origin[2] + DotProduct( joint, up ) + shift;
	if ( !state->valid ) {
		state->valid = qtrue;
		if ( smooth ) {
			return;		// smoothed from the next frame
		}
	}
	VectorScale( up, shift, waistOffset );

	// the ankles' orientations before the legs turn, to keep
	for ( i = 0; i < ik->numLegs; i++ ) {
		for ( r = 0; r < 3; r++ ) {
			for ( c = 0; c < 3; c++ ) {
				ankleRot[i][r][c] = jointMats[12 * ik->ankle[i] + r * 4 + c];
			}
		}
	}

	BG_ModelTranslateJoints( model, jointMats, numJoints, ik->waist, waistOffset );

	for ( i = 0; i < ik->numLegs; i++ ) {
		BG_IK_JointOrigin( jointMats, ik->hip[i], hip[i] );
		BG_IK_JointOrigin( jointMats, ik->knee[i], knee[i] );
		BG_IK_JointOrigin( jointMats, ik->ankle[i], ankle[i] );
		len0 = Distance( hip[i], knee[i] );
		len1 = Distance( knee[i], ankle[i] );

		// the way the knee bends: where it is off the hip-ankle line, but
		// never backward (a knee bends only forward, as Doom 3's always does);
		// forward when the leg's nearly straight, where the offset is noise
		VectorSubtract( ankle[i], hip[i], start );
		VectorNormalize( start );
		VectorSubtract( knee[i], hip[i], kneeDir );
		VectorMA( kneeDir, -DotProduct( kneeDir, start ), start, kneeDir );
		if ( VectorNormalize( kneeDir ) < 1.0f || kneeDir[0] < 0.25f ) {
			VectorSet( world, 1, 0, 0 );		// the model's forward
			VectorMA( world, -DotProduct( world, start ), start, kneeDir );
			if ( VectorNormalize( kneeDir ) < 0.01f ) {
				continue;		// the leg points forward; leave it
			}
		}

		// a target the leg can't reach is pulled in along the hip's line to
		// where it can (Doom 3 leaves the knee at the midpoint, which folds
		// the leg)
		VectorSubtract( target[i], hip[i], start );
		height = VectorNormalize( start );
		if ( height > ( len0 + len1 ) * 0.999f ) {
			VectorMA( hip[i], ( len0 + len1 ) * 0.999f, start, target[i] );
		} else if ( height < fabs( len0 - len1 ) * 1.001f + 0.01f ) {
			VectorMA( hip[i], fabs( len0 - len1 ) * 1.001f + 0.01f, start, target[i] );
		}
		if ( !BG_IK_SolveTwoBones( hip[i], target[i], kneeDir, len0, len1, end ) ) {
			continue;		// as animated
		}
		// the thigh to the new knee, then the shin to the ankle's target
		BG_IK_TurnBone( model, jointMats, numJoints, ik->hip[i], hip[i], knee[i], end );
		BG_IK_JointOrigin( jointMats, ik->knee[i], knee[i] );
		BG_IK_JointOrigin( jointMats, ik->ankle[i], ankle[i] );
		BG_IK_TurnBone( model, jointMats, numJoints, ik->knee[i], knee[i], ankle[i], target[i] );

		// the ankle as it was turned: its rotation now back to before
		for ( r = 0; r < 3; r++ ) {
			for ( c = 0; c < 3; c++ ) {
				rot[r][c] = 0;
				for ( k = 0; k < 3; k++ ) {
					rot[r][c] += ankleRot[i][r][k] * jointMats[12 * ik->ankle[i] + c * 4 + k];
				}
			}
		}
		BG_ModelRotateJoints( model, jointMats, numJoints, ik->ankle[i], rot );
	}
}

/*
================
BG_PlayerClimbing

Whether legsAnim is a ladder's.
================
*/
qboolean BG_PlayerClimbing( int legsAnim ) {
	legsAnim &= ~ANIM_TOGGLEBIT;
	return legsAnim == LEGS_CLIMB || legsAnim == LEGS_CLIMBDOWN;
}

/*
================
BG_PlayerClimbPhase

On a ladder the legs' frames go by height, not time: a cycle of the climbing
animation for every "ladder_climb" units, so the hands and feet stay on the
rungs however fast the player goes, and stop with it. Climbing down plays
its own animation the same way the other way.
================
*/
void BG_PlayerClimbPhase( const bgModel_t *model, bgAnimChannel_t *legs, int legsAnim, float height, int time ) {
	const bgAnim_t *anim;
	float cycle, phase;

	if ( !BG_PlayerClimbing( legsAnim ) || model->ladderClimb <= 0 || legs->anim < 0 || legs->anim >= model->numAnims ) {
		return;
	}
	anim = &model->anims[legs->anim];
	if ( anim->fps <= 0 || anim->numFrames <= 1 ) {
		return;
	}
	cycle = anim->numFrames / anim->fps * 1000.0f;		// msec
	phase = height / model->ladderClimb;
	if ( ( legsAnim & ~ANIM_TOGGLEBIT ) == LEGS_CLIMBDOWN ) {
		phase = -phase;
	}
	phase -= floor( phase );
	legs->frameTime = time - (int)( phase * cycle );
}

/*
================
BG_PlayerLadder

The ladder a climbing player holds: traced as pmove finds it (the box a step
along the view against a ladder surface), so the game and
every client find the same one. yaw faces it; offset moves the model up to
it, its hands ("ladder_reach" ahead of it) on the rungs. qfalse when not
climbing, or no ladder's there.
================
*/
qboolean BG_PlayerLadder( int playerModel, int legsAnim, const vec3_t origin, float viewYaw, int passEntityNum,
						  float *yaw, vec3_t offset, vec4_t plane ) {
	const bgModel_t *model;
	vec3_t mins, maxs, forward, end;
	trace_t tr;

	if ( !BG_PlayerClimbing( legsAnim ) || !BG_ValidModel( playerModel ) ) {
		return qfalse;
	}
	model = &bg_models[playerModel];
	BG_PlayerBox( playerModel, qfalse, mins, maxs, NULL );
	VectorSet( forward, cos( DEG2RAD( viewYaw ) ), sin( DEG2RAD( viewYaw ) ), 0 );
	VectorMA( origin, 4, forward, end );		// ahead, as pmove looks
	BG_IK_Trace( &tr, origin, mins, maxs, end, passEntityNum, MASK_PLAYERSOLID );
	if ( tr.fraction == 1 || tr.startsolid || !( tr.surfaceFlags & SURF_LADDER ) ) {
		return qfalse;
	}
	*yaw = RAD2DEG( atan2( -tr.plane.normal[1], -tr.plane.normal[0] ) );
	VectorCopy( tr.plane.normal, plane );
	plane[3] = tr.plane.dist;
	// the box's face against the ladder (an axial box reaches out along the
	// normal by its half widths so weighted): the hands reach from the origin
	// to there, less their own reach
	VectorClear( offset );
	if ( model->ladderReach > 0 ) {
		VectorScale( tr.plane.normal, -( tr.fraction * 4 * -DotProduct( forward, tr.plane.normal )
										 + maxs[0] * fabs( tr.plane.normal[0] ) + maxs[1] * fabs( tr.plane.normal[1] )
										 - model->ladderReach ), offset );
		offset[2] = 0;
	}
	return qtrue;
}

#define LADDER_GRAB_MSEC		350		// taking hold of a ladder
#define LADDER_RELEASE_MSEC		300		// letting go of it

/*
================
BG_PlayerLadderWeight

How far into holding a ladder the player is, 0 to 1: easing in over the
grab after the climbing starts, out over the release after it stops (from
the legs' channel, so the game and the cgame agree). Changing between
climbing up and down holds on.
================
*/
static qboolean BG_IsClimbAnim( const bgModel_t *model, int anim ) {
	return anim >= 0 && ( anim == model->playerAnims[LEGS_CLIMB] || anim == model->playerAnims[LEGS_CLIMBDOWN] );
}

float BG_PlayerLadderWeight( const bgModel_t *model, const bgAnimChannel_t *legs, int legsAnim, int time ) {
	float f;

	if ( BG_PlayerClimbing( legsAnim ) ) {
		if ( BG_IsClimbAnim( model, legs->prevAnim ) ) {
			return 1.0f;
		}
		f = (float)( time - legs->time ) / LADDER_GRAB_MSEC;
	} else if ( BG_IsClimbAnim( model, legs->prevAnim ) ) {
		f = 1.0f - (float)( time - legs->time ) / LADDER_RELEASE_MSEC;
	} else {
		return 0;
	}
	f = Com_Clamp( 0, 1, f );
	return f * f * ( 3 - 2 * f );		// smoothstep
}

/*
================
BG_ModelReachArm

Doom 3's reach IK (idIK_Reach), one arm: the hand toward target (the model's
space), weight of the way from where the animation has it; the shoulder
stays, the elbow bends as it was, the hand keeps its orientation. For a
ladder's rungs, and for grabbing things and pressing buttons. qfalse when
the model has no such arm.
================
*/
qboolean BG_ModelReachArm( const bgModel_t *model, float *jointMats, int numJoints, int arm, const vec3_t target,
						   float weight ) {
	const bgModelIK_t *ik = &model->ik;
	vec3_t shoulder, elbow, hand, goal, line, bend, newElbow;
	float handRot[3][3], rot[3][3], len0, len1, reach;
	int k, r, c;

	if ( arm < 0 || arm >= ik->numArms || ik->shoulder[arm] >= numJoints || ik->elbow[arm] >= numJoints
		 || ik->hand[arm] >= numJoints ) {
		return qfalse;
	}
	if ( weight <= 0 ) {
		return qtrue;
	}
	BG_IK_JointOrigin( jointMats, ik->shoulder[arm], shoulder );
	BG_IK_JointOrigin( jointMats, ik->elbow[arm], elbow );
	BG_IK_JointOrigin( jointMats, ik->hand[arm], hand );
	len0 = Distance( shoulder, elbow );
	len1 = Distance( elbow, hand );

	// weight of the way, in reach of the shoulder
	VectorSubtract( target, hand, goal );
	VectorMA( hand, Com_Clamp( 0, 1, weight ), goal, goal );
	VectorSubtract( goal, shoulder, line );
	reach = VectorNormalize( line );
	if ( reach > ( len0 + len1 ) * 0.999f ) {
		VectorMA( shoulder, ( len0 + len1 ) * 0.999f, line, goal );
	}

	// the elbow bends as it does: off the shoulder-hand line, else back
	VectorSubtract( elbow, shoulder, bend );
	VectorMA( bend, -DotProduct( bend, line ), line, bend );
	if ( VectorNormalize( bend ) < 0.01f ) {
		VectorSet( bend, -1, 0, 0 );
		VectorMA( bend, -DotProduct( bend, line ), line, bend );
		if ( VectorNormalize( bend ) < 0.01f ) {
			return qtrue;
		}
	}
	if ( !BG_IK_SolveTwoBones( shoulder, goal, bend, len0, len1, newElbow ) ) {
		return qtrue;
	}
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			handRot[r][c] = jointMats[12 * ik->hand[arm] + r * 4 + c];
		}
	}
	BG_IK_TurnBone( model, jointMats, numJoints, ik->shoulder[arm], shoulder, elbow, newElbow );
	BG_IK_JointOrigin( jointMats, ik->elbow[arm], elbow );
	BG_IK_JointOrigin( jointMats, ik->hand[arm], hand );
	BG_IK_TurnBone( model, jointMats, numJoints, ik->elbow[arm], elbow, hand, goal );
	// the hand as it was turned
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			rot[r][c] = 0;
			for ( k = 0; k < 3; k++ ) {
				rot[r][c] += handRot[r][k] * jointMats[12 * ik->hand[arm] + c * 4 + k];
			}
		}
	}
	BG_ModelRotateJoints( model, jointMats, numJoints, ik->hand[arm], rot );
	return qtrue;
}

/*
================
BG_ModelLadderIK

On a ladder each hand reaches for its face (plane, world space) at the
height the climb has it, weight of the way there (taking hold, letting go).
The model is at origin with axis.
================
*/
#define LADDER_HAND_DEPTH	1.5f	// the hand's middle off the face

void BG_ModelLadderIK( const bgModel_t *model, float *jointMats, int numJoints, const vec3_t origin,
					   const vec3_t axis[3], const vec4_t plane, float weight ) {
	vec3_t hand, world, normal, target;
	float dist;
	int i, k;

	if ( weight <= 0 ) {
		return;
	}
	for ( k = 0; k < 3; k++ ) {
		normal[k] = DotProduct( plane, axis[k] );		// the ladder's normal in the model's space
	}
	for ( i = 0; i < model->ik.numArms; i++ ) {
		if ( model->ik.hand[i] >= numJoints ) {
			continue;
		}
		BG_IK_JointOrigin( jointMats, model->ik.hand[i], hand );
		VectorCopy( origin, world );
		for ( k = 0; k < 3; k++ ) {
			VectorMA( world, hand[k], axis[k], world );
		}
		dist = DotProduct( world, plane ) - plane[3] - LADDER_HAND_DEPTH;
		VectorMA( hand, -dist, normal, target );		// onto the face, as high as it is
		BG_ModelReachArm( model, jointMats, numJoints, i, target, weight );
	}
}
