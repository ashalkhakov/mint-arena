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
static int BG_IQMAnims( const char *meshName, bgAnim_t *anims, int max ) {
	void *buffer;
	long length;
	const iqmHeader_t *header;
	const iqmAnim_t *anim;
	const char *text;
	int i, num = 0;

	length = BG_FS_ReadFile( meshName, &buffer );
	if ( length < (long)sizeof( iqmHeader_t ) || !buffer ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}
	header = (const iqmHeader_t *)buffer;
	if ( memcmp( header->magic, IQM_MAGIC, sizeof( IQM_MAGIC ) ) || LittleLong( header->version ) != IQM_VERSION
		|| LittleLong( header->ofs_anims ) + LittleLong( header->num_anims ) * sizeof( iqmAnim_t ) > (unsigned)length
		|| LittleLong( header->ofs_text ) + LittleLong( header->num_text ) > (unsigned)length ) {
		BG_FS_FreeFile( buffer );
		return 0;
	}
	text = (const char *)buffer + LittleLong( header->ofs_text );
	anim = (const iqmAnim_t *)( (const byte *)buffer + LittleLong( header->ofs_anims ) );
	for ( i = 0; i < (int)LittleLong( header->num_anims ) && num < max; i++, anim++ ) {
		unsigned nameOfs = LittleLong( anim->name );

		if ( nameOfs >= LittleLong( header->num_text ) ) {
			continue;
		}
		Q_strncpyz( anims[num].name, text + nameOfs, sizeof( anims[num].name ) );
		anims[num].firstFrame = LittleLong( anim->first_frame );
		anims[num].numFrames = LittleLong( anim->num_frames );
		anims[num].fps = LittleFloat( anim->framerate );
		anims[num].loop = ( LittleLong( anim->flags ) & IQM_LOOP ) != 0;
		num++;
	}
	BG_FS_FreeFile( buffer );
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
	numIqmAnims = BG_IQMAnims( model->mesh, iqmAnims, MAX_MODEL_ANIMS );
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
BG_ModelHitboxes

The model's hitboxes posed: hModel is the mesh registered with this module's
renderer (the server's has one too), the frames from BG_ModelFrame, origin
and axis the entity's. Each box comes out as its joint's origin and axis and
its mins and maxs in that space. Returns how many.
================
*/
int BG_ModelHitboxes( const bgModel_t *model, qhandle_t hModel, int frame, int oldFrame, float backlerp,
					  const vec3_t origin, const vec3_t axis[3], bgHitboxPose_t *poses ) {
	orientation_t tag;
	int i, j, num = 0;

	for ( i = 0; i < model->numHitboxes; i++ ) {
		const bgHitbox_t *box = &model->hitboxes[i];
		bgHitboxPose_t *pose = &poses[num];

		if ( !trap_R_LerpTag( &tag, hModel, oldFrame, frame, 1.0f - backlerp, box->joint ) ) {
			continue;
		}
		// the joint's place in the world
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
