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
BG_IK_BoneFrame

Doom 3's idIK::GetBoneAxis: a frame along a bone, x down it, y toward dir,
as a matrix's columns.
================
*/
static void BG_IK_BoneFrame( const vec3_t start, const vec3_t end, const vec3_t dir, float m[3][3] ) {
	vec3_t x, y, z;
	int r;

	VectorSubtract( end, start, x );
	VectorNormalize( x );
	VectorMA( dir, -DotProduct( dir, x ), x, y );
	if ( VectorNormalize( y ) == 0 ) {
		PerpendicularVector( y, x );
	}
	CrossProduct( x, y, z );
	for ( r = 0; r < 3; r++ ) {
		m[r][0] = x[r];
		m[r][1] = y[r];
		m[r][2] = z[r];
	}
}

/*
================
BG_IK_TurnBone

Turns a joint (and what's under it) about itself so its bone goes from
along from->to old to along to new, keeping the bend's plane (dir).
================
*/
static void BG_IK_TurnBone( const bgModel_t *model, float *jointMats, int numJoints, int joint,
							const vec3_t pivot, const vec3_t oldEnd, const vec3_t newEnd, const vec3_t dir ) {
	float oldFrame[3][3], newFrame[3][3], rot[3][3];
	int r, c, k;

	BG_IK_BoneFrame( pivot, oldEnd, dir, oldFrame );
	BG_IK_BoneFrame( pivot, newEnd, dir, newFrame );
	// new * old transposed
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			rot[r][c] = 0;
			for ( k = 0; k < 3; k++ ) {
				rot[r][c] += newFrame[r][k] * oldFrame[c][k];
			}
		}
	}
	BG_ModelRotateJoints( model, jointMats, numJoints, joint, rot );
}

/*
================
BG_ModelWalkIK

Plants a pose's feet (jointMats, model space) on the floor under them: the
model at origin with axis, standing at floorHeight (world z; where the
entity's box rests). state keeps the smoothing from frame to frame. Nothing
when the model has no IK, or isn't on the ground.
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

	if ( !ik->numLegs || numJoints <= ik->waist ) {
		return;
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
		floors[i] = tr.endpos[2];

		// the ankle up or down by as much as the floor is from where the
		// entity stands, smoothed
		shift = floors[i] - floorHeight + ik->footShift;
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
		return;
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

		// the way the knee bends: where it is off the hip-ankle line, or
		// forward when the leg's straight
		VectorSubtract( ankle[i], hip[i], start );
		VectorNormalize( start );
		VectorSubtract( knee[i], hip[i], kneeDir );
		VectorMA( kneeDir, -DotProduct( kneeDir, start ), start, kneeDir );
		if ( VectorNormalize( kneeDir ) < 0.01f ) {
			VectorSet( kneeDir, 1, 0, 0 );		// the model's forward
		}

		BG_IK_SolveTwoBones( hip[i], target[i], kneeDir, len0, len1, end );
		// the thigh to the new knee, then the shin to the ankle's target
		BG_IK_TurnBone( model, jointMats, numJoints, ik->hip[i], hip[i], knee[i], end, kneeDir );
		BG_IK_JointOrigin( jointMats, ik->knee[i], knee[i] );
		BG_IK_JointOrigin( jointMats, ik->ankle[i], ankle[i] );
		BG_IK_TurnBone( model, jointMats, numJoints, ik->knee[i], knee[i], ankle[i], target[i], kneeDir );

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
