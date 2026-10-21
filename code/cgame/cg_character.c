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
CG_ModelSoundPick

What a model def's "snd_*" key plays: a sound shader's files (Doom 3's way),
or, not naming one, a list of files.
================
*/
static const char *CG_ModelSoundPick( const char *value, qboolean all, int index ) {
	static char picked[MAX_QPATH];
	const bgSoundShader_t *shader = BG_FindSoundShader( value );
	char files[MAX_STRING_CHARS], *p, *token;
	int n, pick;

	if ( shader ) {
		if ( all ) {
			return index < shader->numFiles ? shader->files[index] : NULL;
		}
		return BG_SoundShaderFile( shader );
	}
	Q_strncpyz( files, value, sizeof( files ) );
	p = files;
	for ( n = 0; COM_Parse( &p )[0]; n++ ) {
	}
	if ( !n || ( all && index >= n ) ) {
		return NULL;
	}
	pick = all ? index : rand() % n;
	p = files;
	for ( ; ; ) {
		token = COM_Parse( &p );
		if ( !pick-- ) {
			break;
		}
	}
	Q_strncpyz( picked, token, sizeof( picked ) );
	return picked;
}

/*
================
CG_RegisterModelSounds

Every sound a model def's "snd_*" keys play, loaded now rather than when
first played.
================
*/
void CG_RegisterModelSounds( int modelNum ) {
	const dictKeyValue_t *kv;
	const char *key, *file;
	int i, j;

	if ( !BG_ValidModel( modelNum ) ) {
		return;
	}
	for ( i = 0; i < DictGetNumKeyVals( bg_models[modelNum].def ); i++ ) {
		kv = DictGetKeyVal( bg_models[modelNum].def, i );
		key = PoolStrGetString( kv->key );
		if ( Q_stricmpn( key, "snd_", 4 ) ) {
			continue;
		}
		for ( j = 0; ( file = CG_ModelSoundPick( PoolStrGetString( kv->value ), qtrue, j ) ) != NULL; j++ ) {
			trap_S_RegisterSound( file, qfalse );
		}
	}
}

/*
================
CG_ModelSound

A sound of a model def's key, picked from its sound shader (or list); 0 for
none.
================
*/
sfxHandle_t CG_ModelSound( int modelNum, const char *key ) {
	const char *value, *file;

	if ( !BG_ValidModel( modelNum ) ) {
		return 0;
	}
	value = DictGetStringReference( bg_models[modelNum].def, key, "" );
	if ( !value[0] ) {
		return 0;
	}
	file = CG_ModelSoundPick( value, qfalse, 0 );
	return file ? trap_S_RegisterSound( file, qfalse ) : 0;
}

/*
================
CG_StartModelSound

Plays a sound of a model def's key at its sound shader's volume, times
scale. qfalse if the key has none.
================
*/
qboolean CG_StartModelSound( vec3_t origin, int entityNum, int channel, int modelNum, const char *key, float scale ) {
	const bgSoundShader_t *shader;
	const char *value, *file;
	float volume = 1;

	if ( !BG_ValidModel( modelNum ) ) {
		return qfalse;
	}
	value = DictGetStringReference( bg_models[modelNum].def, key, "" );
	if ( !value[0] ) {
		return qfalse;
	}
	shader = BG_FindSoundShader( value );
	if ( shader ) {
		volume = shader->volume;
	}
	file = CG_ModelSoundPick( value, qfalse, 0 );
	if ( !file ) {
		return qfalse;
	}
	trap_S_StartSoundVolume( origin, entityNum, channel, trap_S_RegisterSound( file, qfalse ), volume * scale );
	return qtrue;
}

/*
================
CG_Footstep

A foot coming down (the "footstep <joint> [volume]" frame command): what's
under it, its material's surface type, picks the model's
"snd_footstep_<type>", or "snd_footstep" (Doom 3's way), at the sound
shader's volume times the command's (walking steps are quieter).
================
*/
static void CG_Footstep( centity_t *cent, int modelNum, const char *args, const refEntity_t *ent,
						 const float *joints, int numJoints ) {
	const bgModel_t *model = &bg_models[modelNum];
	vec3_t foot, start, end;
	orientation_t tag;
	trace_t tr;
	char jointName[MAX_QPATH];
	float scale = 1;
	int joint, type, i;

	if ( !cg_footsteps.integer ) {
		return;
	}
	jointName[0] = '\0';
	sscanf( args, "%63s %f", jointName, &scale );
	VectorCopy( ent->origin, foot );
	joint = BG_ModelJointIndex( model, jointName );
	if ( joint >= 0 && joint < numJoints ) {
		BG_JointOrientation( joints, joint, &tag );
		for ( i = 0; i < 3; i++ ) {
			VectorMA( foot, tag.origin[i], ent->axis[i], foot );
		}
	}
	VectorCopy( foot, start );
	start[2] += 8;
	VectorCopy( foot, end );
	end[2] -= 24;
	CG_Trace( &tr, start, NULL, NULL, end, cent->currentState.number, MASK_PLAYERSOLID );
	if ( tr.fraction == 1.0f || ( tr.surfaceFlags & SURF_NOSTEPS ) ) {
		return;		// in the air
	}
	type = SURF_TYPE( tr.surfaceFlags );
	if ( !CG_StartModelSound( tr.endpos, cent->currentState.number, CHAN_BODY, modelNum,
							  va( "snd_footstep_%s", bg_surfaceTypeNames[type] ), scale ) ) {
		CG_StartModelSound( tr.endpos, cent->currentState.number, CHAN_BODY, modelNum, "snd_footstep", scale );
	}
}

/*
================
CG_CharacterFrameCommands

The model's frame commands of the frames a channel's animation went through
since the last: "sound <file>", "fx <effect>", "footstep <joint>". ("event"s
are the game's.) ent and joints are the pose drawn.
================
*/
static void CG_CharacterFrameCommands( centity_t *cent, int modelNum, int channel, const refEntity_t *ent,
									   const float *joints, int numJoints ) {
	const bgModel_t *model = &bg_models[modelNum];
	const bgAnimChannel_t *ch = &cent->characterChannels[channel];
	const bgAnim_t *a;
	int i, from, to, f, frame, oldFrame, animFrame;
	float backlerp;

	if ( ch->anim < 0 || ch->anim >= model->numAnims ) {
		cent->characterAnim[channel] = -1;
		return;
	}
	a = &model->anims[ch->anim];
	BG_ModelFrameRate( model, ch->anim, ch->frameTime, cg.time, ch->rate, &frame, &oldFrame, &backlerp, &animFrame );

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
			} else if ( !Q_stricmp( fc->command, "footstep" ) ) {
				CG_Footstep( cent, modelNum, fc->arg, ent, joints, numJoints );
			}
		}
	}
}

/*
===============================================================================

	Hitboxes as drawn: the effects of shots hit where the game's did
	(G_TraceShot), on the body part, not the box about the body

===============================================================================
*/

#define MAX_HITBOX_ENTITIES		64
#define HITBOX_KEEP_MSEC		250			// a pose this old is no use

typedef struct {
	int				entityNum;
	int				time;
	int				num;
	bgHitboxPose_t	poses[MAX_MODEL_HITBOXES];
} cgHitboxes_t;

static cgHitboxes_t		cg_hitboxes[MAX_HITBOX_ENTITIES];

static cgHitboxes_t *CG_FindHitboxes( int entityNum ) {
	int i;

	for ( i = 0; i < MAX_HITBOX_ENTITIES; i++ ) {
		if ( cg_hitboxes[i].time && cg_hitboxes[i].entityNum == entityNum ) {
			return &cg_hitboxes[i];
		}
	}
	return NULL;
}

/*
================
CG_StoreHitboxes

An entity's hitboxes on the pose it's drawn in, kept for the shots' effects.
================
*/
static void CG_StoreHitboxes( centity_t *cent, const bgModel_t *model, const float *joints, int numJoints, const refEntity_t *ent ) {
	cgHitboxes_t *h = CG_FindHitboxes( cent->currentState.number );
	int i;

	if ( numJoints <= 0 || ( ent->renderfx & RF_ONLY_MIRROR ) ) {
		return;
	}
	if ( !h ) {
		// a free one, or the oldest
		h = &cg_hitboxes[0];
		for ( i = 0; i < MAX_HITBOX_ENTITIES; i++ ) {
			if ( cg_hitboxes[i].time < h->time ) {
				h = &cg_hitboxes[i];
			}
		}
	}
	h->entityNum = cent->currentState.number;
	h->time = cg.time ? cg.time : 1;
	h->num = BG_ModelHitboxes( model, joints, numJoints, ent->origin, (const vec3_t *)ent->axis, h->poses );
	if ( ent->nonNormalizedAxes ) {
		// a scaled model's: the boxes scaled, their axes not
		float scale = VectorLength( ent->axis[0] );

		for ( i = 0; i < h->num; i++ ) {
			VectorNormalize( h->poses[i].axis[0] );
			VectorNormalize( h->poses[i].axis[1] );
			VectorNormalize( h->poses[i].axis[2] );
			VectorScale( h->poses[i].mins, scale, h->poses[i].mins );
			VectorScale( h->poses[i].maxs, scale, h->poses[i].maxs );
		}
	}
}

/*
================
CG_TraceHitboxes

The move against an entity's hitboxes as last drawn; qfalse when it misses
them all, or there are none, and *had qfalse when there are none.
================
*/
static qboolean CG_TraceHitboxes( int entityNum, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
								  trace_t *results, qboolean *had ) {
	cgHitboxes_t *h = CG_FindHitboxes( entityNum );
	traceModel_t boxTrm, shotTrm;
	cm_trace_t tr, best;
	vec3_t bounds[2];
	int i, bestBox = -1;

	*had = qfalse;
	if ( !h || !h->num || cg.time - h->time > HITBOX_KEEP_MSEC || cg.time < h->time ) {
		return qfalse;
	}
	*had = qtrue;

	// the shot as a trace model, a point for hitscan
	shotTrm.type = TRM_INVALID;
	if ( mins && maxs && ( !VectorCompare( mins, vec3_origin ) || !VectorCompare( maxs, vec3_origin ) ) ) {
		VectorCopy( mins, bounds[0] );
		VectorCopy( maxs, bounds[1] );
		TraceModelSetupBox( &shotTrm, bounds );
	}

	memset( &best, 0, sizeof( best ) );
	best.fraction = 1.0f;
	for ( i = 0; i < h->num; i++ ) {
		VectorCopy( h->poses[i].mins, bounds[0] );
		VectorCopy( h->poses[i].maxs, bounds[1] );
		boxTrm.type = TRM_INVALID;
		TraceModelSetupBox( &boxTrm, bounds );
		trap_CM_Translation( &tr, start, end, shotTrm.type == TRM_INVALID ? NULL : &shotTrm, axisDefault, -1,
							 trap_CM_TempTraceModel( &boxTrm, CONTENTS_BODY ), h->poses[i].origin, (const vec3_t *)h->poses[i].axis );
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
	results->entityNum = entityNum;
	results->contents = CONTENTS_BODY;
	return qtrue;
}

/*
================
CG_TraceShot

G_TraceShot's trace on the client: a skeletal character is hit on its
hitboxes as drawn, and missed past them.
================
*/
void CG_TraceShot( trace_t *results, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end,
				   int skipNumber, int mask ) {
	centity_t *skipped[8];
	int skippedContents[8], numSkipped = 0, i;
	qboolean had;

	for ( ;; ) {
		CG_Trace( results, start, mins, maxs, end, skipNumber, mask );
		if ( results->fraction == 1.0f || results->entityNum < 0 || results->entityNum >= ENTITYNUM_MAX_NORMAL ) {
			break;
		}
		if ( CG_TraceHitboxes( results->entityNum, start, mins, maxs, end, results, &had ) || !had ) {
			break;
		}
		// missed its boxes: on through its bounding box
		if ( numSkipped == ARRAY_LEN( skipped ) ) {
			results->fraction = 1.0f;
			VectorCopy( end, results->endpos );
			results->entityNum = ENTITYNUM_NONE;
			break;
		}
		skipped[numSkipped] = &cg_entities[results->entityNum];
		skippedContents[numSkipped] = skipped[numSkipped]->currentState.contents;
		skipped[numSkipped]->currentState.contents = 0;
		numSkipped++;
	}
	for ( i = 0; i < numSkipped; i++ ) {
		skipped[i]->currentState.contents = skippedContents[i];
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
	if ( s1->origin2[0] > 0.0f && s1->origin2[0] != 1.0f ) {
		// "modelscale"
		VectorScale( ent.axis[0], s1->origin2[0], ent.axis[0] );
		VectorScale( ent.axis[1], s1->origin2[0], ent.axis[1] );
		VectorScale( ent.axis[2], s1->origin2[0], ent.axis[2] );
		ent.nonNormalizedAxes = qtrue;
	}

	BG_AnimChannelsFromState( cent->characterChannels, s1->legsAnim, s1->time, CHARACTER_LEGS_RATE( s1 ), s1->torsoAnim, s1->time2 );
	// the legs' frames for the bounds, the blended skeleton to draw
	BG_ModelFrame( model, s1->legsAnim, s1->time, cg.time, &ent.frame, &ent.oldframe, &ent.backlerp, NULL );
	numJoints = BG_ModelPose( model, ent.hModel, cent->characterChannels, cg.time, joints );
	if ( CG_ServerRagdoll( cent, s1->modelindex2, &ent, joints, numJoints ) ) {
		// limp: the server's figure (its axis made again)
		if ( s1->origin2[0] > 0.0f && s1->origin2[0] != 1.0f ) {
			VectorScale( ent.axis[0], s1->origin2[0], ent.axis[0] );
			VectorScale( ent.axis[1], s1->origin2[0], ent.axis[1] );
			VectorScale( ent.axis[2], s1->origin2[0], ent.axis[2] );
		}
	} else if ( numJoints > 0 && cg_ik.integer ) {
		// feet on the floor under them, the box's bottom where it stands
		BG_ModelWalkIK( model, joints, numJoints, ent.origin, (const vec3_t *)ent.axis, ent.origin[2] + s1->mins[2],
						qtrue, s1->number, cg.time, &cent->ik );
	}
	if ( numJoints > 0 ) {
		ent.skeleton = trap_R_AddSkeletonToScene( joints, numJoints );
		CG_StoreHitboxes( cent, model, joints, numJoints, &ent );
	}
	if ( cg_drawSkeleton.integer < 2 ) {
		// (2: the skeleton alone)
		trap_R_AddRefEntityToScene( &ent );
	}

	for ( c = 0; c < MAX_ANIM_CHANNELS; c++ ) {
		CG_CharacterFrameCommands( cent, s1->modelindex2, c, &ent, joints, numJoints );
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
	BG_PlayerAnimChannels( model, cent->characterChannels, s1->legsAnim, legsTime, s1->torsoAnim, torsoTime, s1->weapon,
						   cent == &cg.cur_lc->predictedPlayerEntity ? cg.cur_lc->predictedPlayerState.swimPhase : s1->frame,
						   cg.time );
	// on a ladder, the climb's frames by height, the model up to the rungs
	BG_PlayerClimbPhase( model, &cent->characterChannels[ANIMCHANNEL_LEGS], s1->legsAnim, legs->origin[2], cg.time );
	if ( cent->onLadder ) {
		VectorAdd( legs->origin, cent->ladder.offset, legs->origin );
	}
	if ( cent->swimming ) {
		// the swimming body centred in the box, however it's turned
		BG_PlayerSwimOrigin( model, legs->origin, (const vec3_t *)legs->axis, legs->origin );
	}

	legs->hModel = pi->skeletalModel;
	legs->customSkin = 0;
	// the legs' frames for the bounds, the blended skeleton to draw
	BG_ModelFrameRate( model, cent->characterChannels[ANIMCHANNEL_LEGS].anim, cent->characterChannels[ANIMCHANNEL_LEGS].frameTime,
				   cg.time, cent->characterChannels[ANIMCHANNEL_LEGS].rate, &legs->frame, &legs->oldframe, &legs->backlerp, NULL );
	VectorCopy( legs->origin, legs->oldorigin );
	numJoints = BG_ModelPose( model, legs->hModel, cent->characterChannels, cg.time, joints );
	if ( !( s1->eFlags & EF_DEAD ) ) {
		CG_RagdollAlive( cent );
	} else if ( numJoints > 0 && CG_Ragdoll( cent, pi->modelDef, legs, joints, numJoints ) ) {
		// limp: the ragdoll's pose, where it lies
		legs->skeleton = trap_R_AddSkeletonToScene( joints, numJoints );
		numJoints = -numJoints;
	}
	if ( numJoints < 0 ) {
		numJoints = -numJoints;
	} else if ( numJoints > 0 ) {
		BG_ModelAim( model, joints, numJoints, torsoAxis, headAxis );
		if ( cent->onLadder ) {
			// the hands on the ladder, reaching for it as it takes hold
			BG_ModelLadderIK( model, joints, numJoints, legs->origin, (const vec3_t *)legs->axis, &cent->ladder,
							  cent->ladderWeight );
		}
		if ( cg_ik.integer ) {
			// feet on the floor under them, where the player's box rests
			BG_ModelWalkIK( model, joints, numJoints, legs->origin, (const vec3_t *)legs->axis, legs->origin[2] + MINS_Z,
							s1->groundEntityNum != ENTITYNUM_NONE && !( s1->eFlags & EF_DEAD ), s1->number, cg.time, &cent->ik );
		}
		legs->skeleton = trap_R_AddSkeletonToScene( joints, numJoints );
	}
	if ( numJoints > 0 ) {
		CG_StoreHitboxes( cent, model, joints, numJoints, legs );
	}
	if ( cg_drawSkeleton.integer < 2 ) {
		CG_AddRefEntityWithPowerups( legs, &cent->currentState );
	}

	for ( i = 0; i < MAX_ANIM_CHANNELS; i++ ) {
		CG_CharacterFrameCommands( cent, pi->modelDef, i, legs, joints, numJoints );
	}

	// Daikatana's hardpoints are points: the weapon goes where the hand is,
	// turned as the torso aims rather than as the hand is (torsoAxis first:
	// the caller's torso may be the same as torso, filled next)
	CG_JointEntity( legs, joints, numJoints, model->weaponJoint, weapon );
	MatrixMultiply( (vec3_t *)torsoAxis, legs->axis, weapon->axis );
	CG_JointEntity( legs, joints, numJoints, model->torsoJoint, torso );
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
