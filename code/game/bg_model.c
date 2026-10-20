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
// bg_model.c -- skeletal models: animations, hitboxes and frame commands
//
// A model is an entity def with a "mesh", an IQM (Doom 3's "model" decl
// as keys of an entityDef):
//
//	entityDef model_testchar {
//		"mesh"				"characters/test/testchar.iqm"
//		"anim idle"			"idle"				// the IQM's animation of the name
//		"anim walk"			"walk"
//		"anim wave"			"0 20 10 loop"		// or frames: first, count, fps, loop
//		"hitbox Head"		"-4.5 -4 0 4.5 4 10 head"	// mins, maxs in the joint's space, zone
//		"damage_scale head"	"2"					// Doom 3's
//		"frame walk 3"		"sound sounds/global/p_step1.wav"
//	}
//
// The game and the cgame number the models alike, pose the hitboxes from
// the joints (trap_R_LerpTag) on the frame the animation is on, and so
// agree where they are.

#ifdef CGAME
#include "../cgame/cg_local.h"
#else
#include "g_local.h"
#endif

#define MASK_WORDS		SKELETON_MASK_WORDS

// the parts of an IQM (renderercommon/iqm.h in the engine) read here
#define IQM_MAGIC		"INTERQUAKEMODEL"
#define IQM_VERSION		2
#define IQM_LOOP		1

typedef struct {
	char			magic[16];
	unsigned int	version, filesize, flags;
	unsigned int	num_text, ofs_text;
	unsigned int	num_meshes, ofs_meshes;
	unsigned int	num_vertexarrays, num_vertexes, ofs_vertexarrays;
	unsigned int	num_triangles, ofs_triangles, ofs_adjacency;
	unsigned int	num_joints, ofs_joints;
	unsigned int	num_poses, ofs_poses;
	unsigned int	num_anims, ofs_anims;
	unsigned int	num_frames, num_framechannels, ofs_frames, ofs_bounds;
	unsigned int	num_comment, ofs_comment;
	unsigned int	num_extensions, ofs_extensions;
} iqmHeader_t;

typedef struct {
	unsigned int	name;
	unsigned int	first_frame, num_frames;
	float			framerate;
	unsigned int	flags;
} iqmAnim_t;

bgModel_t	bg_models[MAX_MODEL_DEFS];
int			bg_numModels = 1;		// 0 is none

/*
================
BG_IQMAnims

The animations an IQM names, for "anim <name>" "<IQM animation>".
================
*/
static int BG_IQMAnims( const char *meshName, bgAnim_t *anims, int max, bgModel_t *model ) {
	static char text[65536];
	iqmHeader_t header;
	iqmAnim_t anim;
	fileHandle_t f;
	int length, i, num = 0;
	unsigned numText;

	// only the header, the text and the animations: the rest can be big
	if ( Q_stricmp( COM_GetExtension( meshName ), "iqm" ) ) {
		return 0;
	}
	length = trap_FS_FOpenFile( meshName, &f, FS_READ );
	if ( length < (int)sizeof( header ) ) {
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		return 0;
	}
	trap_FS_Read( &header, sizeof( header ), f );
	numText = LittleLong( header.num_text );
	if ( memcmp( header.magic, IQM_MAGIC, sizeof( IQM_MAGIC ) ) || LittleLong( header.version ) != IQM_VERSION
		|| numText >= sizeof( text ) || LittleLong( header.ofs_text ) + numText > (unsigned)length
		|| LittleLong( header.ofs_anims ) + LittleLong( header.num_anims ) * sizeof( iqmAnim_t ) > (unsigned)length ) {
		trap_FS_FCloseFile( f );
		return 0;
	}
	trap_FS_Seek( f, LittleLong( header.ofs_text ), FS_SEEK_SET );
	trap_FS_Read( text, numText, f );
	text[numText] = 0;
	trap_FS_Seek( f, LittleLong( header.ofs_anims ), FS_SEEK_SET );
	for ( i = 0; i < (int)LittleLong( header.num_anims ) && num < max; i++ ) {
		unsigned nameOfs;

		trap_FS_Read( &anim, sizeof( anim ), f );
		nameOfs = LittleLong( anim.name );
		if ( nameOfs >= numText ) {
			continue;
		}
		Q_strncpyz( anims[num].name, text + nameOfs, sizeof( anims[num].name ) );
		anims[num].firstFrame = LittleLong( anim.first_frame );
		anims[num].numFrames = LittleLong( anim.num_frames );
		anims[num].fps = LittleFloat( anim.framerate );
		anims[num].loop = ( LittleLong( anim.flags ) & IQM_LOOP ) != 0;
		num++;
	}

	// the skeleton: names and parents
	model->numJoints = 0;
	if ( LittleLong( header.ofs_joints ) + LittleLong( header.num_joints ) * 48 <= (unsigned)length ) {
		trap_FS_Seek( f, LittleLong( header.ofs_joints ), FS_SEEK_SET );
		for ( i = 0; i < (int)LittleLong( header.num_joints ) && i < MAX_MODEL_JOINTS; i++ ) {
			int joint[12];		// name, parent, translate, rotate, scale

			trap_FS_Read( joint, sizeof( joint ), f );
			if ( (unsigned)LittleLong( joint[0] ) < numText ) {
				Q_strncpyz( model->joints[i].name, text + LittleLong( joint[0] ), sizeof( model->joints[i].name ) );
			}
			model->joints[i].parent = LittleLong( joint[1] );
			model->numJoints++;
		}
	}
	trap_FS_FCloseFile( f );
	return num;
}

static int BG_ZoneNum( bgModel_t *model, const char *name ) {
	int i;

	for ( i = 0; i < model->numZones; i++ ) {
		if ( !Q_stricmp( model->zones[i].name, name ) ) {
			return i;
		}
	}
	if ( model->numZones == MAX_MODEL_ZONES ) {
		return 0;
	}
	Q_strncpyz( model->zones[i].name, name, sizeof( model->zones[i].name ) );
	model->zones[i].damageScale = 1.0f;
	return model->numZones++;
}

/*
================
BG_ModelJointIndex

The joint of the name, -1 for none.
================
*/
int BG_ModelJointIndex( const bgModel_t *model, const char *name ) {
	int i;

	for ( i = 0; i < model->numJoints; i++ ) {
		if ( !Q_stricmp( model->joints[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

static void BG_MaskJoint( unsigned int *mask, int joint, qboolean set ) {
	if ( joint < 0 || joint >= MASK_WORDS * 32 ) {
		return;
	}
	if ( set ) {
		mask[joint >> 5] |= 1u << ( joint & 31 );
	} else {
		mask[joint >> 5] &= ~( 1u << ( joint & 31 ) );
	}
}

static qboolean BG_JointUnder( const bgModel_t *model, int joint, int root ) {
	while ( joint >= 0 ) {
		if ( joint == root ) {
			return qtrue;
		}
		joint = model->joints[joint].parent;
	}
	return qfalse;
}

/*
================
BG_ModelChannel

A channel's joints, Doom 3's way: "*Waist" is the joint and all under it,
"Waist" the joint alone, "-" before either takes them out.
================
*/
static void BG_ModelChannel( bgModel_t *model, int channel, const char *joints ) {
	char buf[MAX_STRING_CHARS], *p, *token;
	unsigned int *mask = model->channelMask[channel];
	qboolean remove, all;
	int i, joint;

	memset( mask, 0, sizeof( model->channelMask[0] ) );
	Q_strncpyz( buf, joints, sizeof( buf ) );
	for ( p = buf; ; ) {
		token = COM_ParseExt( &p, qfalse );
		if ( !token[0] ) {
			break;
		}
		remove = ( *token == '-' );
		if ( remove ) {
			token++;
		}
		all = ( *token == '*' );
		if ( all ) {
			token++;
		}
		joint = BG_ModelJointIndex( model, token );
		if ( joint < 0 ) {
			continue;	// a rig without it: no such channel
		}
		for ( i = 0; i < model->numJoints; i++ ) {
			if ( i == joint || ( all && BG_JointUnder( model, i, joint ) ) ) {
				BG_MaskJoint( mask, i, !remove );
			}
		}
	}
}

/*
================
BG_LoadModel
================
*/
static void BG_LoadModel( bgModel_t *model, const dict_t *def, const char *name ) {
	bgAnim_t iqmAnims[MAX_MODEL_ANIMS];
	const dictKeyValue_t *kv;
	const char *key, *value;
	char buf[MAX_STRING_CHARS], word[2][MAX_QPATH], zone[MAX_QPATH];
	int numIqmAnims, i, j, n;

	memset( model, 0, sizeof( *model ) );
	model->def = def;
	model->name = name;
	Q_strncpyz( model->mesh, DictGetStringReference( def, "mesh", "" ), sizeof( model->mesh ) );
	numIqmAnims = BG_IQMAnims( model->mesh, iqmAnims, MAX_MODEL_ANIMS, model );
	BG_ZoneNum( model, "body" );	// zone 0: what isn't in another

	for ( i = 0; i < DictGetNumKeyVals( def ); i++ ) {
		kv = DictGetKeyVal( def, i );
		key = PoolStrGetString( kv->key );
		value = PoolStrGetString( kv->value );
		Q_strncpyz( buf, key, sizeof( buf ) );

		if ( !Q_stricmpn( buf, "anim ", 5 ) ) {
			bgAnim_t *anim;
			char loop[16];
			float fps;

			if ( model->numAnims == MAX_MODEL_ANIMS ) {
				continue;
			}
			anim = &model->anims[model->numAnims];
			Q_strncpyz( anim->name, buf + 5, sizeof( anim->name ) );
			loop[0] = 0;
			if ( sscanf( value, "%d %d %f %15s", &anim->firstFrame, &anim->numFrames, &fps, loop ) >= 3 ) {
				anim->fps = fps;
				anim->loop = !Q_stricmp( loop, "loop" );
			} else {
				for ( j = 0; j < numIqmAnims; j++ ) {
					if ( !Q_stricmp( iqmAnims[j].name, value ) ) {
						break;
					}
				}
				if ( j == numIqmAnims ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: model %s: %s has no animation %s\n", name, model->mesh, value );
					continue;
				}
				anim->firstFrame = iqmAnims[j].firstFrame;
				anim->numFrames = iqmAnims[j].numFrames;
				anim->fps = iqmAnims[j].fps;
				anim->loop = iqmAnims[j].loop;
			}
			if ( anim->numFrames < 1 ) {
				anim->numFrames = 1;
			}
			if ( anim->fps <= 0 ) {
				anim->fps = 10;
			}
			model->numAnims++;
		} else if ( !Q_stricmpn( buf, "hitbox ", 7 ) ) {
			bgHitbox_t *box;

			if ( model->numHitboxes == MAX_MODEL_HITBOXES ) {
				continue;
			}
			box = &model->hitboxes[model->numHitboxes];
			Q_strncpyz( box->joint, buf + 7, sizeof( box->joint ) );
			zone[0] = 0;
			n = sscanf( value, "%f %f %f %f %f %f %63s", &box->mins[0], &box->mins[1], &box->mins[2],
						&box->maxs[0], &box->maxs[1], &box->maxs[2], zone );
			if ( n < 6 ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: model %s: bad \"%s\"\n", name, key );
				continue;
			}
			box->zone = zone[0] ? BG_ZoneNum( model, zone ) : 0;
			model->numHitboxes++;
		} else if ( !Q_stricmpn( buf, "channel ", 8 ) ) {
			// parsed below, once the joints are known
		} else if ( !Q_stricmpn( buf, "frame ", 6 ) ) {
			bgFrameCommand_t *fc;

			if ( model->numFrameCommands == MAX_MODEL_FRAMECOMMANDS
				|| sscanf( buf + 6, "%63s %63s", word[0], word[1] ) != 2 ) {
				continue;
			}
			fc = &model->frameCommands[model->numFrameCommands];
			Q_strncpyz( fc->anim, word[0], sizeof( fc->anim ) );
			fc->frame = atoi( word[1] );
			if ( sscanf( value, "%15s %63s", fc->command, fc->arg ) < 1 ) {
				continue;
			}
			model->numFrameCommands++;
		}
	}

	// the joints of the hitboxes
	for ( i = 0; i < model->numHitboxes; i++ ) {
		model->hitboxes[i].jointIndex = BG_ModelJointIndex( model, model->hitboxes[i].joint );
		if ( model->hitboxes[i].jointIndex < 0 && model->numJoints ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: model %s: no joint %s for a hitbox\n", name, model->hitboxes[i].joint );
		}
	}

	// channels: "channel torso" "*Waist" (Doom 3's defaults for its rigs)
	BG_ModelChannel( model, ANIMCHANNEL_TORSO, DictGetStringReference( def, "channel torso", "*Waist" ) );
	BG_ModelChannel( model, ANIMCHANNEL_HEAD, DictGetStringReference( def, "channel head", "*Head" ) );
	model->blendTime = DictGetFloat( def, "blend_time", "0.2" ) * 1000;

	// damage scales of the zones the hitboxes name
	for ( i = 0; i < model->numZones; i++ ) {
		model->zones[i].damageScale = DictGetFloat( def, va( "damage_scale %s", model->zones[i].name ), "1" );
	}
}

/*
================
BG_InitModels

Every def with a "mesh", numbered from 1 by name. Called by BG_InitItems.
================
*/
void BG_InitModels( void ) {
	const dict_t *def;
	int i;

	bg_numModels = 1;
	for ( i = 0; i < BG_NumDefs(); i++ ) {
		def = BG_DefForNum( i );
		if ( !DictFindKey( def, "mesh" ) ) {
			continue;
		}
		if ( bg_numModels == MAX_MODEL_DEFS ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: more than %d models, %s left out\n", MAX_MODEL_DEFS - 1, BG_DefName( i ) );
			break;
		}
		BG_LoadModel( &bg_models[bg_numModels++], def, BG_DefName( i ) );
	}
}

/*
================
BG_ModelForName
================
*/
int BG_ModelForName( const char *name ) {
	int i;

	for ( i = 1; i < bg_numModels; i++ ) {
		if ( !Q_stricmp( bg_models[i].name, name ) ) {
			return i;
		}
	}
	return 0;
}

/*
================
BG_ModelAnimForName

The model's animation of the name, -1 for none.
================
*/
int BG_ModelAnimForName( const bgModel_t *model, const char *name ) {
	int i;

	for ( i = 0; i < model->numAnims; i++ ) {
		if ( !Q_stricmp( model->anims[i].name, name ) ) {
			return i;
		}
	}
	return -1;
}

/*
================
BG_ModelFrame

The frames an animation started at animTime is between at time, as a
refEntity_t takes them: backlerp is how much of oldFrame. animFrame is
the frame of the animation (not of the model) it is on.
================
*/
void BG_ModelFrame( const bgModel_t *model, int animNum, int animTime, int time,
					int *frame, int *oldFrame, float *backlerp, int *animFrame ) {
	const bgAnim_t *anim;
	float f;
	int cur, next;

	if ( animNum < 0 || animNum >= model->numAnims ) {
		*frame = *oldFrame = 0;
		*backlerp = 0;
		if ( animFrame ) {
			*animFrame = 0;
		}
		return;
	}
	anim = &model->anims[animNum];
	f = ( time - animTime ) * 0.001f * anim->fps;
	if ( f < 0 ) {
		f = 0;
	}
	cur = (int)f;
	f -= cur;
	if ( anim->loop ) {
		cur %= anim->numFrames;
		next = ( cur + 1 ) % anim->numFrames;
	} else if ( cur >= anim->numFrames - 1 ) {
		cur = next = anim->numFrames - 1;
		f = 0;
	} else {
		next = cur + 1;
	}
	*oldFrame = anim->firstFrame + cur;
	*frame = anim->firstFrame + next;
	*backlerp = 1.0f - f;
	if ( animFrame ) {
		*animFrame = cur;
	}
}

/*
================
BG_AnimChannelPlay

A channel plays the animation from time on; what it played before is kept
to cross-fade from.
================
*/
void BG_AnimChannelPlay( bgAnimChannel_t *channel, int anim, int time ) {
	if ( anim == channel->anim && time == channel->time ) {
		return;
	}
	channel->prevAnim = channel->anim;
	channel->prevTime = channel->time;
	channel->anim = anim;
	channel->time = time;
}

/*
================
BG_AnimChannelsFromState

The channels of an entity's legsAnim/time (the legs) and torsoAnim/time2
(the torso, 0 for none, else the animation plus one).
================
*/
void BG_AnimChannelsFromState( bgAnimChannel_t *channels, int legsAnim, int legsTime, int torsoAnim, int torsoTime ) {
	BG_AnimChannelPlay( &channels[ANIMCHANNEL_LEGS], legsAnim, legsTime );
	BG_AnimChannelPlay( &channels[ANIMCHANNEL_TORSO], torsoAnim > 0 ? torsoAnim - 1 : -1, torsoAnim > 0 ? torsoTime : 0 );
}

static void BG_AddLayer( const bgModel_t *model, skeletonBlend_t *blend, int anim, int animTime, int time,
						 float weight, const unsigned int *mask ) {
	memset( blend, 0, sizeof( *blend ) );
	BG_ModelFrame( model, anim, animTime, time, &blend->frame, &blend->oldframe, &blend->backlerp, NULL );
	blend->weight = weight;
	if ( mask ) {
		memcpy( blend->jointMask, mask, sizeof( blend->jointMask ) );
	}
}

/*
================
BG_ModelSkeletonBlends

The layers to blend the skeleton from at time (Doom 3's idAnimator): the
legs over every joint, cross-fading from their last animation; then the
torso and the head over their joints, fading in, cross-fading, or fading
out to what's under them. The fades start when the new animation does, so
the game and the cgame weigh them alike. Returns how many layers.
================
*/
int BG_ModelSkeletonBlends( const bgModel_t *model, const bgAnimChannel_t *channels, int time, void *out, int maxBlends ) {
	skeletonBlend_t *blends = out;
	int c, n = 0;
	float fade;

	for ( c = 0; c < MAX_ANIM_CHANNELS && n + 2 <= maxBlends; c++ ) {
		const bgAnimChannel_t *ch = &channels[c];
		const unsigned int *mask = c == ANIMCHANNEL_LEGS ? NULL : model->channelMask[c];
		int m;

		if ( mask ) {
			for ( m = 0; m < MASK_WORDS && !mask[m]; m++ ) {
			}
			if ( m == MASK_WORDS ) {
				continue;	// the rig has no such channel
			}
		}
		fade = model->blendTime > 0 ? (float)( time - ch->time ) / model->blendTime : 1.0f;
		if ( fade > 1.0f ) {
			fade = 1.0f;
		} else if ( fade < 0.0f ) {
			fade = 0.0f;
		}

		if ( c == ANIMCHANNEL_LEGS ) {
			// the base: the old animation under the new one fading in
			if ( ch->prevAnim >= 0 && fade < 1.0f ) {
				BG_AddLayer( model, &blends[n++], ch->prevAnim, ch->prevTime, time, 1.0f, NULL );
				BG_AddLayer( model, &blends[n++], ch->anim >= 0 ? ch->anim : 0, ch->time, time, fade, NULL );
			} else {
				BG_AddLayer( model, &blends[n++], ch->anim >= 0 ? ch->anim : 0, ch->time, time, 1.0f, NULL );
			}
			continue;
		}
		if ( ch->anim >= 0 ) {
			if ( ch->prevAnim >= 0 && fade < 1.0f ) {
				BG_AddLayer( model, &blends[n++], ch->prevAnim, ch->prevTime, time, 1.0f, mask );
			}
			BG_AddLayer( model, &blends[n++], ch->anim, ch->time, time, fade, mask );
		} else if ( ch->prevAnim >= 0 && fade < 1.0f ) {
			// stopped: fades out to the legs'
			BG_AddLayer( model, &blends[n++], ch->prevAnim, ch->prevTime, time, 1.0f - fade, mask );
		}
	}
	return n;
}

/*
================
BG_ModelPose

The joints (3x4 matrices in the model's space) of the channels' animations
blended at time: hModel is the mesh registered with this module's renderer
(the server's has one too). Returns how many.
================
*/
int BG_ModelPose( const bgModel_t *model, qhandle_t hModel, const bgAnimChannel_t *channels, int time, float *jointMats ) {
	skeletonBlend_t blends[MAX_ANIM_CHANNELS * 2];
	int n;

	n = BG_ModelSkeletonBlends( model, channels, time, blends, ARRAY_LEN( blends ) );
	return trap_R_BlendSkeleton( hModel, blends, n, jointMats, MAX_MODEL_JOINTS );
}

/*
================
BG_JointOrientation

A joint of a pose as a tag (LerpTag's).
================
*/
void BG_JointOrientation( const float *jointMats, int joint, orientation_t *tag ) {
	const float *m = &jointMats[12 * joint];
	int r, c;

	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			tag->axis[c][r] = m[r * 4 + c];
		}
		tag->origin[r] = m[r * 4 + 3];
	}
}

/*
================
BG_ModelHitboxes

The model's hitboxes on a pose (BG_ModelPose), at the entity's origin and
axis. Each box comes out as its joint's origin and axis and its mins and
maxs in that space. Returns how many.
================
*/
int BG_ModelHitboxes( const bgModel_t *model, const float *jointMats, int numJoints,
					  const vec3_t origin, const vec3_t axis[3], bgHitboxPose_t *poses ) {
	orientation_t tag;
	int i, j, num = 0;

	for ( i = 0; i < model->numHitboxes; i++ ) {
		const bgHitbox_t *box = &model->hitboxes[i];
		bgHitboxPose_t *pose = &poses[num];

		if ( box->jointIndex < 0 || box->jointIndex >= numJoints ) {
			continue;
		}
		BG_JointOrientation( jointMats, box->jointIndex, &tag );
		VectorCopy( origin, pose->origin );
		for ( j = 0; j < 3; j++ ) {
			VectorMA( pose->origin, tag.origin[j], axis[j], pose->origin );
		}
		MatrixMultiply( tag.axis, (vec3_t *)axis, pose->axis );
		VectorCopy( box->mins, pose->mins );
		VectorCopy( box->maxs, pose->maxs );
		pose->hitbox = i;
		pose->zone = box->zone;
		num++;
	}
	return num;
}
