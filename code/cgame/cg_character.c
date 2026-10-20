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
// cg_character.c -- skeletal characters (bg_model.c, g_character.c)

#include "cg_local.h"

/*
================
CG_DebugLine

A line in the world, a thin quad facing the view.
================
*/
void CG_DebugLine( const vec3_t start, const vec3_t end, const vec4_t color ) {
	polyVert_t verts[4];
	vec3_t dir, view, side;
	byte rgba[4];
	int i;

	VectorSubtract( end, start, dir );
	VectorSubtract( start, cg.refdef.vieworg, view );
	CrossProduct( dir, view, side );
	if ( VectorNormalize( side ) == 0 ) {
		return;
	}
	VectorScale( side, 0.3f, side );
	for ( i = 0; i < 4; i++ ) {
		rgba[i] = color[i] * 255;
	}
	VectorAdd( start, side, verts[0].xyz );
	VectorSubtract( start, side, verts[1].xyz );
	VectorSubtract( end, side, verts[2].xyz );
	VectorAdd( end, side, verts[3].xyz );
	for ( i = 0; i < 4; i++ ) {
		verts[i].st[0] = verts[i].st[1] = 0;
		Byte4Copy( rgba, verts[i].modulate );
	}
	trap_R_AddPolyToScene( cgs.media.whiteShader, 4, verts, 0, 0 );

	// and the other way round, whichever side culling keeps
	{
		polyVert_t back[4];

		for ( i = 0; i < 4; i++ ) {
			back[i] = verts[3 - i];
		}
		trap_R_AddPolyToScene( cgs.media.whiteShader, 4, back, 0, 0 );
	}
}

/*
================
CG_DrawHitbox

A posed hitbox's twelve edges.
================
*/
static void CG_DrawHitbox( const bgHitboxPose_t *pose, const vec4_t color ) {
	vec3_t corners[8];
	int i, j;

	for ( i = 0; i < 8; i++ ) {
		vec3_t local;

		local[0] = ( i & 1 ) ? pose->maxs[0] : pose->mins[0];
		local[1] = ( i & 2 ) ? pose->maxs[1] : pose->mins[1];
		local[2] = ( i & 4 ) ? pose->maxs[2] : pose->mins[2];
		VectorCopy( pose->origin, corners[i] );
		for ( j = 0; j < 3; j++ ) {
			VectorMA( corners[i], local[j], pose->axis[j], corners[i] );
		}
	}
	for ( i = 0; i < 8; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			if ( !( i & ( 1 << j ) ) ) {
				CG_DebugLine( corners[i], corners[i | ( 1 << j )], color );
			}
		}
	}
}

/*
================
CG_DrawSkeleton

A pose's joints: a line from each to its parent, and its axes (x red,
y green, z blue).
================
*/
static void CG_DrawSkeleton( const bgModel_t *model, const float *joints, int numJoints, const refEntity_t *ent ) {
	static const vec4_t boneColor = { 1.0f, 1.0f, 0.3f, 1.0f };
	static const vec4_t axisColor[3] = { { 1, 0.2f, 0.2f, 1 }, { 0.2f, 1, 0.2f, 1 }, { 0.3f, 0.5f, 1, 1 } };
	vec3_t origins[MAX_MODEL_JOINTS], axes[MAX_MODEL_JOINTS][3], end;
	orientation_t tag;
	int i, j, k;

	for ( i = 0; i < numJoints; i++ ) {
		BG_JointOrientation( joints, i, &tag );
		VectorCopy( ent->origin, origins[i] );
		for ( j = 0; j < 3; j++ ) {
			VectorMA( origins[i], tag.origin[j], ent->axis[j], origins[i] );
		}
		for ( j = 0; j < 3; j++ ) {
			VectorClear( axes[i][j] );
			for ( k = 0; k < 3; k++ ) {
				VectorMA( axes[i][j], tag.axis[j][k], ent->axis[k], axes[i][j] );
			}
		}
	}
	for ( i = 0; i < numJoints && i < model->numJoints; i++ ) {
		j = model->joints[i].parent;
		if ( j >= 0 && j < numJoints ) {
			CG_DebugLine( origins[j], origins[i], boneColor );
		}
		for ( k = 0; k < 3; k++ ) {
			VectorMA( origins[i], 3, axes[i][k], end );
			CG_DebugLine( origins[i], end, axisColor[k] );
		}
	}
}

/*
================
CG_CharacterFrameCommands

The model's frame commands of the frames a channel's animation went through
since the last: "sound <file>", "fx <effect>". ("event"s are the game's.)
================
*/
static void CG_CharacterFrameCommands( centity_t *cent, const bgModel_t *model, int channel ) {
	const bgAnimChannel_t *ch = &cent->characterChannels[channel];
	const bgAnim_t *a;
	int i, from, to, f, frame, oldFrame, animFrame;
	float backlerp;

	if ( ch->anim < 0 || ch->anim >= model->numAnims ) {
		cent->characterAnim[channel] = -1;
		return;
	}
	a = &model->anims[ch->anim];
	BG_ModelFrame( model, ch->anim, ch->time, cg.time, &frame, &oldFrame, &backlerp, &animFrame );

	// a new animation starts from its first frame
	if ( cent->characterAnim[channel] != ch->anim || cent->characterAnimTime[channel] != ch->time ) {
		cent->characterAnim[channel] = ch->anim;
		cent->characterAnimTime[channel] = ch->time;
		cent->characterFrame[channel] = -1;
	}
	from = cent->characterFrame[channel];
	to = animFrame;
	if ( to == from ) {
		return;
	}
	cent->characterFrame[channel] = to;
	if ( to < from ) {
		to += a->numFrames;		// looped around
	}

	for ( f = from + 1; f <= to; f++ ) {
		int fr = f % a->numFrames;

		for ( i = 0; i < model->numFrameCommands; i++ ) {
			const bgFrameCommand_t *fc = &model->frameCommands[i];

			if ( fc->frame != fr || Q_stricmp( fc->anim, a->name ) ) {
				continue;
			}
			if ( !Q_stricmp( fc->command, "sound" ) ) {
				trap_S_StartSound( NULL, cent->currentState.number, CHAN_AUTO, trap_S_RegisterSound( fc->arg, qfalse ) );
			} else if ( !Q_stricmp( fc->command, "fx" ) ) {
				vec3_t up = { 0, 0, 1 };

				CG_FX_PlayEffect( fc->arg, cent->lerpOrigin, up );
			}
		}
	}
}

/*
================
CG_Character

An ET_CHARACTER: its mesh on its channels' animations blended (Doom 3's
way: legs, torso over its joints, cross-fades), their frame commands, and
with cg_drawHitboxes its hitboxes, cg_drawSkeleton its joints.
================
*/
void CG_Character( centity_t *cent ) {
	static const vec4_t hitboxColor = { 1.0f, 0.3f, 0.1f, 1.0f };
	entityState_t *s1 = &cent->currentState;
	const bgModel_t *model;
	bgHitboxPose_t poses[MAX_MODEL_HITBOXES];
	float joints[MAX_MODEL_JOINTS * 12];
	refEntity_t ent;
	int i, num, numJoints, c;

	if ( !BG_ValidModel( s1->modelindex2 ) ) {
		return;
	}
	model = &bg_models[s1->modelindex2];

	memset( &ent, 0, sizeof( ent ) );
	ent.hModel = cgs.gameModels[s1->modelindex];
	if ( !ent.hModel ) {
		return;
	}
	VectorCopy( cent->lerpOrigin, ent.origin );
	VectorCopy( cent->lerpOrigin, ent.oldorigin );
	VectorCopy( cent->lerpOrigin, ent.lightingOrigin );
	AnglesToAxis( cent->lerpAngles, ent.axis );

	BG_AnimChannelsFromState( cent->characterChannels, s1->legsAnim, s1->time, s1->torsoAnim, s1->time2 );
	// the legs' frames for the bounds, the blended skeleton to draw
	BG_ModelFrame( model, s1->legsAnim, s1->time, cg.time, &ent.frame, &ent.oldframe, &ent.backlerp, NULL );
	numJoints = BG_ModelPose( model, ent.hModel, cent->characterChannels, cg.time, joints );
	if ( numJoints > 0 ) {
		ent.skeleton = trap_R_AddSkeletonToScene( joints, numJoints );
	}
	if ( cg_drawSkeleton.integer < 2 ) {
		// (2: the skeleton alone)
		trap_R_AddRefEntityToScene( &ent );
	}

	for ( c = 0; c < MAX_ANIM_CHANNELS; c++ ) {
		CG_CharacterFrameCommands( cent, model, c );
	}

	if ( cg_drawSkeleton.integer && numJoints > 0 ) {
		CG_DrawSkeleton( model, joints, numJoints, &ent );
	}

	if ( cg_drawHitboxes.integer && numJoints > 0 ) {
		num = BG_ModelHitboxes( model, joints, numJoints, ent.origin, (const vec3_t *)ent.axis, poses );
		for ( i = 0; i < num; i++ ) {
			CG_DrawHitbox( &poses[i], hitboxColor );
		}
	}
}

/*
================
CG_JointEntity

A refEntity at a joint of a pose drawn as ent, with no model of its own,
for what goes there (the weapon, the flag); at ent if there's no joint.
================
*/
static void CG_JointEntity( const refEntity_t *ent, const float *joints, int numJoints, int joint, refEntity_t *out ) {
	orientation_t tag;
	int i, j;

	memset( out, 0, sizeof( *out ) );
	VectorCopy( ent->origin, out->origin );
	AxisCopy( ent->axis, out->axis );
	if ( joint >= 0 && joint < numJoints ) {
		BG_JointOrientation( joints, joint, &tag );
		for ( i = 0; i < 3; i++ ) {
			VectorMA( out->origin, tag.origin[i], ent->axis[i], out->origin );
		}
		for ( i = 0; i < 3; i++ ) {
			VectorClear( out->axis[i] );
			for ( j = 0; j < 3; j++ ) {
				VectorMA( out->axis[i], tag.axis[i][j], ent->axis[j], out->axis[i] );
			}
		}
	}
	VectorCopy( out->origin, out->oldorigin );
	VectorCopy( ent->lightingOrigin, out->lightingOrigin );
	out->shadowPlane = ent->shadowPlane;
	out->renderfx = ent->renderfx;
}

/*
================
CG_PlayerCharacter

A player whose model is a model def: legs (origin, axis, renderfx set)
becomes its mesh on its legsAnim and torsoAnim through the def's
"player_anim"s, blended as the game blends them for its hitboxes, the torso
and head turned to aim by torsoAxis (relative to the legs) and headAxis (to
the torso), as Quake 3 turns its models. torso, weapon and flag come back
at their joints, for what's drawn there; the weapon facing as the torso
aims.
================
*/
void CG_PlayerCharacter( centity_t *cent, const playerInfo_t *pi, refEntity_t *legs, const vec3_t torsoAxis[3],
						 const vec3_t headAxis[3], refEntity_t *torso, refEntity_t *weapon, refEntity_t *flag ) {
	static const vec4_t hitboxColor = { 1.0f, 0.3f, 0.1f, 1.0f };
	const entityState_t *s1 = &cent->currentState;
	const bgModel_t *model = &bg_models[pi->modelDef];
	bgHitboxPose_t poses[MAX_MODEL_HITBOXES];
	float joints[MAX_MODEL_JOINTS * 12];
	int numJoints, legsTime, torsoTime, i, num;

	if ( cent == &cg.cur_lc->predictedPlayerEntity ) {
		// predicted: no times from the game, they start when seen
		if ( cent->playerAnims[0] != s1->legsAnim ) {
			cent->playerAnims[0] = s1->legsAnim;
			cent->playerAnimTimes[0] = cg.time;
		}
		if ( cent->playerAnims[1] != s1->torsoAnim ) {
			cent->playerAnims[1] = s1->torsoAnim;
			cent->playerAnimTimes[1] = cg.time;
		}
		legsTime = cent->playerAnimTimes[0];
		torsoTime = cent->playerAnimTimes[1];
	} else {
		legsTime = s1->time;
		torsoTime = s1->time2;
	}
	BG_PlayerAnimChannels( model, cent->characterChannels, s1->legsAnim, legsTime, s1->torsoAnim, torsoTime );

	legs->hModel = pi->skeletalModel;
	legs->customSkin = 0;
	// the legs' frames for the bounds, the blended skeleton to draw
	BG_ModelFrame( model, cent->characterChannels[ANIMCHANNEL_LEGS].anim, cent->characterChannels[ANIMCHANNEL_LEGS].time,
				   cg.time, &legs->frame, &legs->oldframe, &legs->backlerp, NULL );
	VectorCopy( legs->origin, legs->oldorigin );
	numJoints = BG_ModelPose( model, legs->hModel, cent->characterChannels, cg.time, joints );
	if ( numJoints > 0 ) {
		BG_ModelAim( model, joints, numJoints, torsoAxis, headAxis );
		legs->skeleton = trap_R_AddSkeletonToScene( joints, numJoints );
	}
	if ( cg_drawSkeleton.integer < 2 ) {
		CG_AddRefEntityWithPowerups( legs, &cent->currentState );
	}

	for ( i = 0; i < MAX_ANIM_CHANNELS; i++ ) {
		CG_CharacterFrameCommands( cent, model, i );
	}

	CG_JointEntity( legs, joints, numJoints, model->torsoJoint, torso );
	// Daikatana's hardpoints are points: the weapon goes where the hand is,
	// turned as the torso aims rather than as the hand is
	CG_JointEntity( legs, joints, numJoints, model->weaponJoint, weapon );
	MatrixMultiply( (vec3_t *)torsoAxis, legs->axis, weapon->axis );
	CG_JointEntity( legs, joints, numJoints, model->flagJoint >= 0 ? model->flagJoint : model->torsoJoint, flag );

	if ( cg_drawSkeleton.integer && numJoints > 0 ) {
		CG_DrawSkeleton( model, joints, numJoints, legs );
	}
	if ( cg_drawHitboxes.integer && numJoints > 0 && !( legs->renderfx & RF_ONLY_MIRROR ) ) {
		num = BG_ModelHitboxes( model, joints, numJoints, legs->origin, (const vec3_t *)legs->axis, poses );
		for ( i = 0; i < num; i++ ) {
			CG_DrawHitbox( &poses[i], hitboxColor );
		}
	}
}
