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
//		"anim run"			"runa 15 loop"		// an MD3's frames of that name (runa1, runa2...),
//												// with the fps and loop if not the file's
//		"hitbox Head"		"-4.5 -4 0 4.5 4 10 head"	// mins, maxs in the joint's space, zone
//		"damage_scale head"	"2"					// Doom 3's
//		"surface"			"metal"				// Doom 3's surface type: hits spark, no blood (flesh else)
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
/*
================
BG_MD3Anims

An MD3's animations, for "anim <name>" "<frames' name>": its frames named
alike but for their numbers (Daikatana's "walka1", "walka2"...), at 10 fps.
================
*/
static int BG_MD3Anims( const char *meshName, bgAnim_t *anims, int max ) {
	struct {
		char	ident[4];
		int		version;
		char	name[MAX_QPATH];
		int		flags;
		int		numFrames, numTags, numSurfaces, numSkins;
		int		ofsFrames;
	} header;
	char frame[56], name[MAX_QPATH];
	fileHandle_t f;
	int length, i, n, num = 0;

	if ( Q_stricmp( COM_GetExtension( meshName ), "md3" ) ) {
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
	if ( memcmp( header.ident, "IDP3", 4 ) || LittleLong( header.ofsFrames ) + LittleLong( header.numFrames ) * 56 > length ) {
		trap_FS_FCloseFile( f );
		return 0;
	}
	trap_FS_Seek( f, LittleLong( header.ofsFrames ), FS_SEEK_SET );
	for ( i = 0; i < LittleLong( header.numFrames ); i++ ) {
		// bounds, local origin, radius, then the name
		trap_FS_Read( frame, sizeof( frame ), f );
		Q_strncpyz( name, frame + 40, MIN( 17, (int)sizeof( name ) ) );
		for ( n = strlen( name ); n > 0 && name[n - 1] >= '0' && name[n - 1] <= '9'; n-- ) {
		}
		name[n] = 0;
		if ( num > 0 && !Q_stricmp( anims[num - 1].name, name ) ) {
			anims[num - 1].numFrames++;
			continue;
		}
		if ( num == max ) {
			break;
		}
		Q_strncpyz( anims[num].name, name, sizeof( anims[num].name ) );
		anims[num].firstFrame = i;
		anims[num].numFrames = 1;
		anims[num].fps = 10;
		anims[num].loop = qfalse;
		num++;
	}
	trap_FS_FCloseFile( f );
	return num;
}

static int BG_IQMAnims( const char *meshName, bgAnim_t *anims, int max, bgModel_t *model ) {
	static char text[65536];
	iqmHeader_t header;
	iqmAnim_t anim;
	fileHandle_t f;
	int length, i, k, num = 0;
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
			for ( k = 0; k < 3; k++ ) {
				model->joints[i].bindOrigin[k] = LittleFloat( *(float *)&joint[2 + k] );
			}
			for ( k = 0; k < 4; k++ ) {
				model->joints[i].bindQuat[k] = LittleFloat( *(float *)&joint[5 + k] );
			}
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
// Doom 3's surface types (SURFTYPE_*, a material's SURF_TYPE), and the
// three Daikatana fills in after them: "snd_footstep_<name>" and the like
const char *bg_surfaceTypeNames[MAX_SURFACE_TYPES] = {
	"none", "metal", "stone", "flesh", "wood", "cardboard", "liquid", "glass",
	"plastic", "ricochet", "ice", "snow", "sand", "surftype13", "surftype14", "surftype15"
};

// animNumber_t's names, for "player_anim <name>"
static const char *bg_playerAnimNames[MAX_TOTALANIMATIONS] = {
	"BOTH_DEATH1", "BOTH_DEAD1", "BOTH_DEATH2", "BOTH_DEAD2", "BOTH_DEATH3", "BOTH_DEAD3",
	"TORSO_GESTURE", "TORSO_ATTACK", "TORSO_ATTACK2", "TORSO_DROP", "TORSO_RAISE", "TORSO_STAND", "TORSO_STAND2",
	"LEGS_WALKCR", "LEGS_WALK", "LEGS_RUN", "LEGS_BACK", "LEGS_SWIM", "LEGS_JUMP", "LEGS_LAND", "LEGS_JUMPB",
	"LEGS_LANDB", "LEGS_IDLE", "LEGS_IDLECR", "LEGS_TURN",
	"TORSO_GETFLAG", "TORSO_GUARDBASE", "TORSO_PATROL", "TORSO_FOLLOWME", "TORSO_AFFIRMATIVE", "TORSO_NEGATIVE",
	NULL,	// MAX_ANIMATIONS
	"LEGS_BACKCR", "LEGS_BACKWALK", "FLAG_RUN", "FLAG_STAND", "FLAG_STAND2RUN",
	"LEGS_CLIMB", "LEGS_CLIMBDOWN"
};

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
	if ( !numIqmAnims ) {
		numIqmAnims = BG_MD3Anims( model->mesh, iqmAnims, MAX_MODEL_ANIMS );
	}
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
				// a name, then maybe the fps and "loop" instead of the file's
				char animName[MAX_QPATH], rest[2][16];
				int words;

				rest[0][0] = rest[1][0] = 0;
				words = sscanf( value, "%63s %15s %15s", animName, rest[0], rest[1] );
				for ( j = 0; j < numIqmAnims; j++ ) {
					if ( !Q_stricmp( iqmAnims[j].name, animName ) ) {
						break;
					}
				}
				if ( words < 1 || j == numIqmAnims ) {
					Com_Printf( S_COLOR_YELLOW "WARNING: model %s: %s has no animation %s\n", name, model->mesh, value );
					continue;
				}
				anim->firstFrame = iqmAnims[j].firstFrame;
				anim->numFrames = iqmAnims[j].numFrames;
				anim->fps = iqmAnims[j].fps;
				anim->loop = iqmAnims[j].loop;
				for ( n = 0; n < 2; n++ ) {
					if ( !Q_stricmp( rest[n], "loop" ) ) {
						anim->loop = qtrue;
					} else if ( atof( rest[n] ) > 0 ) {
						anim->fps = atof( rest[n] );
					}
				}
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
			// the command, and the rest of the line its arguments
			if ( sscanf( value, "%15s %63[^\n]", fc->command, fc->arg ) < 1 ) {
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
	model->metal = !Q_stricmp( DictGetStringReference( def, "surface", "flesh" ), "metal" );

	// as a player model
	for ( i = 0; i < MAX_TOTALANIMATIONS; i++ ) {
		model->playerAnims[i] = -1;
		if ( !bg_playerAnimNames[i] ) {
			continue;
		}
		word[1][0] = 0;
		if ( sscanf( DictGetStringReference( def, va( "player_anim %s", bg_playerAnimNames[i] ), "" ),
					 "%63s %63s", word[0], word[1] ) < 1 ) {
			continue;
		}
		model->playerAnims[i] = BG_ModelAnimForName( model, word[0] );
		model->playerAnimHold[i] = !Q_stricmp( word[1], "hold" );
		if ( model->playerAnims[i] < 0 ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: model %s: player_anim %s: no animation %s\n", name,
						bg_playerAnimNames[i], word[0] );
		}
	}
	Q_strncpyz( model->ragdoll, DictGetStringReference( def, "ragdoll", "" ), sizeof( model->ragdoll ) );
	model->torsoJoint = BG_ModelJointIndex( model, DictGetStringReference( def, "aim_torso", "Waist" ) );
	model->headJoint = BG_ModelJointIndex( model, DictGetStringReference( def, "aim_head", "Head" ) );
	model->weaponJoint = BG_ModelJointIndex( model, DictGetStringReference( def, "joint_weapon", "WEAPON_ATTACHER" ) );
	model->flagJoint = BG_ModelJointIndex( model, DictGetStringReference( def, "joint_flag", "FLAG_ATTACHER" ) );
	BG_LoadModelIK( model );

	// its box, Quake 3's player's unless it says
	DictGetVector( def, "mins", va( "-15 -15 %d", MINS_Z ), model->mins );
	DictGetVector( def, "maxs", "15 15 32", model->maxs );
	model->crouchHeight = DictGetFloat( def, "crouch_height", "16" );
	model->eyeHeight = DictGetFloat( def, "eye_height", va( "%d", DEFAULT_VIEWHEIGHT ) );
	model->crouchEyeHeight = DictGetFloat( def, "crouch_eye_height", va( "%d", CROUCH_VIEWHEIGHT ) );
	model->runSpeed = DictGetFloat( def, "run_speed", "0" );
	DictGetVector( def, "swim_center", "0 0 0", model->swimCenter );
	model->walkScale = DictGetFloat( def, "walk_scale", "0.5" );
	for ( i = 0; i < model->numAnims; i++ ) {
		model->anims[i].speed = DictGetFloat( def, va( "anim_speed %s", model->anims[i].name ), "0" );
	}
	model->ladderClimb = DictGetFloat( def, "ladder_climb", "0" );
	model->ladderReach = DictGetFloat( def, "ladder_reach", "0" );

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
	for ( i = 0; i < EntityDef_Num(); i++ ) {
		def = EntityDef_ForNum( i );
		if ( !DictFindKey( def, "mesh" ) ) {
			continue;
		}
		if ( bg_numModels == MAX_MODEL_DEFS ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: more than %d models, %s left out\n", MAX_MODEL_DEFS - 1, EntityDef_Name( i ) );
			break;
		}
		BG_LoadModel( &bg_models[bg_numModels++], def, EntityDef_Name( i ) );
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
	BG_ModelFrameRate( model, animNum, animTime, time, 1.0f, frame, oldFrame, backlerp, animFrame );
}

/*
================
BG_ModelFrameRate

BG_ModelFrame played rate times as fast.
================
*/
void BG_ModelFrameRate( const bgModel_t *model, int animNum, int animTime, int time, float rate,
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
	f = ( time - animTime ) * 0.001f * anim->fps * ( rate > 0 ? rate : 1.0f );
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
	channel->prevFrameTime = channel->frameTime;
	channel->prevRate = channel->rate;
	channel->anim = anim;
	channel->time = time;
	channel->frameTime = time;
	channel->rate = 1.0f;
}

/*
================
BG_AnimChannelsFromState

The channels of an entity's legsAnim/time (the legs, played legsRate
faster: a character's walk to how fast it goes, 0 as 1) and torsoAnim/time2
(the torso, 0 for none, else the animation plus one).
================
*/
void BG_AnimChannelsFromState( bgAnimChannel_t *channels, int legsAnim, int legsTime, float legsRate, int torsoAnim, int torsoTime ) {
	BG_AnimChannelPlay( &channels[ANIMCHANNEL_LEGS], legsAnim, legsTime );
	channels[ANIMCHANNEL_LEGS].rate = legsRate > 0 ? legsRate : 1.0f;
	BG_AnimChannelPlay( &channels[ANIMCHANNEL_TORSO], torsoAnim > 0 ? torsoAnim - 1 : -1, torsoAnim > 0 ? torsoTime : 0 );
}

static void BG_AddLayer( const bgModel_t *model, skeletonBlend_t *blend, int anim, int animTime, float rate, int time,
						 float weight, const unsigned int *mask ) {
	memset( blend, 0, sizeof( *blend ) );
	BG_ModelFrameRate( model, anim, animTime, time, rate, &blend->frame, &blend->oldframe, &blend->backlerp, NULL );
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
				BG_AddLayer( model, &blends[n++], ch->prevAnim, ch->prevFrameTime, ch->prevRate, time, 1.0f, NULL );
				BG_AddLayer( model, &blends[n++], ch->anim >= 0 ? ch->anim : 0, ch->frameTime, ch->rate, time, fade, NULL );
			} else {
				BG_AddLayer( model, &blends[n++], ch->anim >= 0 ? ch->anim : 0, ch->frameTime, ch->rate, time, 1.0f, NULL );
			}
			continue;
		}
		if ( ch->anim >= 0 ) {
			if ( ch->prevAnim >= 0 && fade < 1.0f ) {
				BG_AddLayer( model, &blends[n++], ch->prevAnim, ch->prevFrameTime, ch->prevRate, time, 1.0f, mask );
			}
			BG_AddLayer( model, &blends[n++], ch->anim, ch->frameTime, ch->rate, time, fade, mask );
		} else if ( ch->prevAnim >= 0 && fade < 1.0f ) {
			// stopped: fades out to the legs'
			BG_AddLayer( model, &blends[n++], ch->prevAnim, ch->prevFrameTime, ch->prevRate, time, 1.0f - fade, mask );
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
BG_ModelBindPose

The skeleton's bind pose (the IQM's joints), model space jointMats as
BG_ModelPose gives them: 3x4 rows, the joint's axes in the columns.
================
*/
void BG_ModelBindPose( const bgModel_t *model, float *jointMats ) {
	const bgJoint_t *joint;
	float local[12], *m, *p;
	float x, y, z, w;
	int i, r, c;

	for ( i = 0; i < model->numJoints; i++ ) {
		joint = &model->joints[i];
		x = joint->bindQuat[0]; y = joint->bindQuat[1]; z = joint->bindQuat[2]; w = joint->bindQuat[3];

		local[0] = 1 - 2 * ( y * y + z * z );	local[1] = 2 * ( x * y - w * z );		local[2] = 2 * ( x * z + w * y );
		local[4] = 2 * ( x * y + w * z );		local[5] = 1 - 2 * ( x * x + z * z );	local[6] = 2 * ( y * z - w * x );
		local[8] = 2 * ( x * z - w * y );		local[9] = 2 * ( y * z + w * x );		local[10] = 1 - 2 * ( x * x + y * y );
		local[3] = joint->bindOrigin[0];
		local[7] = joint->bindOrigin[1];
		local[11] = joint->bindOrigin[2];

		m = &jointMats[12 * i];
		if ( joint->parent < 0 || joint->parent >= i ) {
			memcpy( m, local, sizeof( local ) );
			continue;
		}

		// the parent's times the joint's
		p = &jointMats[12 * joint->parent];
		for ( r = 0; r < 3; r++ ) {
			for ( c = 0; c < 4; c++ ) {
				m[r * 4 + c] = p[r * 4 + 0] * local[0 * 4 + c] + p[r * 4 + 1] * local[1 * 4 + c] + p[r * 4 + 2] * local[2 * 4 + c]
								+ ( c == 3 ? p[r * 4 + 3] : 0.0f );
			}
		}
	}
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
BG_PlayerModelForName

The model def of a player model ("model" in the userinfo, "hiro" or
"hiro/skin"): model_<name>, or the def of the name itself. 0 for none,
a Quake 3 model then.
================
*/
int BG_PlayerModelForName( const char *modelName ) {
	char name[MAX_QPATH], *slash;
	int m;

	Q_strncpyz( name, modelName, sizeof( name ) );
	slash = strchr( name, '/' );
	if ( slash ) {
		*slash = 0;
	}
	if ( !name[0] ) {
		return 0;
	}
	m = BG_ModelForName( va( "model_%s", name ) );
	if ( !m ) {
		m = BG_ModelForName( name );
	}
	return m;
}

/*
================
BG_PlayerLegsRate

How much faster the legs' animation plays so the feet keep to the ground: the
speed pmove moves the player at for it (running "run_speed", walking
"walk_scale" of it, crouching a quarter) over the speed the animation goes
("anim_speed"). 1 for one that doesn't move, or isn't measured.
================
*/
static float BG_PlayerLegsRate( const bgModel_t *model, int legs, int anim ) {
	float speed;

	if ( model->runSpeed <= 0 || anim < 0 || anim >= model->numAnims || model->anims[anim].speed <= 0 ) {
		return 1.0f;
	}
	switch ( legs ) {
	case LEGS_RUN:
	case LEGS_BACK:
		speed = model->runSpeed;
		break;
	case LEGS_WALK:
	case LEGS_BACKWALK:
		speed = model->runSpeed * model->walkScale;
		break;
	case LEGS_WALKCR:
	case LEGS_BACKCR:
		speed = model->runSpeed * 0.25f;	// pm_duckScale
		break;
	default:
		return 1.0f;
	}
	return Com_Clamp( 0.5f, 3.0f, speed / model->anims[anim].speed );
}

/*
================
BG_PlayerAnimChannels

A player's channels from its legsAnim and torsoAnim (animNumber_t, with
the toggle bit) and when they started: each through the model's
"player_anim"s, in the variant for the weapon held when the model has
it: the weapon def's "player_anim_suffix" added to the animation's name
(Daikatana's: "a" for pistols, "b" for rifles; Doom 3 names a weapon's
player animations by its "animPrefix"). The torso plays its own only when
it has one and isn't doing what the legs are (BOTH_); otherwise the legs'
animation shows through.
================
*/
static int BG_WeaponAnimVariant( const bgModel_t *model, int anim, const char *suffix ) {
	int variant;

	if ( anim < 0 || !suffix[0] ) {
		return anim;
	}
	variant = BG_ModelAnimForName( model, va( "%s%s", model->anims[anim].name, suffix ) );
	return variant >= 0 ? variant : anim;
}

void BG_PlayerAnimChannels( const bgModel_t *model, bgAnimChannel_t *channels, int legsAnim, int legsTime,
							int torsoAnim, int torsoTime, int weapon, int swimPhase, int time ) {
	int legs = legsAnim & ~ANIM_TOGGLEBIT, torso = torsoAnim & ~ANIM_TOGGLEBIT;
	const char *suffix = BG_ValidWeapon( weapon )
		? DictGetStringReference( BG_Weapon( weapon )->def, "player_anim_suffix", "" ) : "";
	int anim;

	anim = legs >= 0 && legs < MAX_TOTALANIMATIONS ? BG_WeaponAnimVariant( model, model->playerAnims[legs], suffix ) : -1;
	if ( anim >= 0 && model->playerAnimHold[legs] ) {
		legsTime -= 3600000;	// long since over: its last frame
	}
	if ( anim < 0 ) {
		// one the model doesn't have (landing): the legs go on with theirs,
		// or its first if they have none
		if ( channels[ANIMCHANNEL_LEGS].anim < 0 ) {
			BG_AnimChannelPlay( &channels[ANIMCHANNEL_LEGS], 0, legsTime );
		}
	} else if ( anim == channels[ANIMCHANNEL_LEGS].anim && model->anims[anim].loop ) {
		// the looping one already playing (running on after running
		// backwards): it keeps its phase, its footsteps come round
	} else {
		BG_AnimChannelPlay( &channels[ANIMCHANNEL_LEGS], anim, legsTime );
	}

	channels[ANIMCHANNEL_LEGS].rate = BG_PlayerLegsRate( model, legs, channels[ANIMCHANNEL_LEGS].anim );
	// swimming, the stroke is pmove's (swimPhase), so the arms pull as it surges
	anim = channels[ANIMCHANNEL_LEGS].anim;
	if ( legs == LEGS_SWIM && anim >= 0 && model->anims[anim].fps > 0 ) {
		channels[ANIMCHANNEL_LEGS].frameTime = time - (int)( (float)swimPhase / SWIM_PHASE
			* model->anims[anim].numFrames / model->anims[anim].fps * 1000.0f );
		channels[ANIMCHANNEL_LEGS].rate = 1.0f;
	}

	anim = -1;
	if ( torso >= 0 && torso < MAX_TOTALANIMATIONS && torso != legs ) {
		anim = BG_WeaponAnimVariant( model, model->playerAnims[torso], suffix );
		if ( anim >= 0 && model->playerAnimHold[torso] ) {
			torsoTime -= 3600000;
		}
	}
	BG_AnimChannelPlay( &channels[ANIMCHANNEL_TORSO], anim, anim >= 0 ? torsoTime : 0 );
}

/*
================
BG_PlayerAimAngles

Where a player's legs, torso and head face, Quake 3's way (CG_PlayerAngles)
without the swinging: the legs toward the movement, the torso a quarter of
that and three quarters of the view's pitch, the head the view.
================
*/
void BG_PlayerAimAngles( const vec3_t viewAngles, int movementDir, vec3_t legs, vec3_t torso, vec3_t head ) {
	static const int movementOffsets[8] = { 0, 22, 45, -22, 0, 22, -45, -22 };
	float pitch;

	if ( movementDir < 0 || movementDir > 7 ) {
		movementDir = 0;
	}
	VectorCopy( viewAngles, head );
	head[YAW] = AngleMod( head[YAW] );
	VectorClear( legs );
	VectorClear( torso );
	legs[YAW] = head[YAW] + movementOffsets[movementDir];
	torso[YAW] = head[YAW] + 0.25f * movementOffsets[movementDir];
	pitch = head[PITCH] > 180 ? head[PITCH] - 360 : head[PITCH];
	torso[PITCH] = pitch * 0.75f;
}

/*
================
BG_PlayerBox

A player's box and eyes, standing or crouched, by its model (0: Quake 3's).
================
*/
void BG_PlayerBox( int playerModel, qboolean crouched, vec3_t mins, vec3_t maxs, int *viewheight ) {
	const bgModel_t *model;

	if ( !BG_ValidModel( playerModel ) ) {
		VectorSet( mins, -15, -15, MINS_Z );
		VectorSet( maxs, 15, 15, crouched ? 16 : 32 );
		if ( viewheight ) {
			*viewheight = crouched ? CROUCH_VIEWHEIGHT : DEFAULT_VIEWHEIGHT;
		}
		return;
	}
	model = &bg_models[playerModel];
	VectorCopy( model->mins, mins );
	VectorCopy( model->maxs, maxs );
	if ( crouched ) {
		maxs[2] = model->crouchHeight;
	}
	if ( viewheight ) {
		*viewheight = (int)( ( crouched ? model->crouchEyeHeight : model->eyeHeight ) + 0.5f );
	}
}

/*
================
BG_ModelRotateJoints

Turns a joint and every joint under it by rot (columns: the new axes, in
the model's space) about the joint.
================
*/
void BG_ModelRotateJoints( const bgModel_t *model, float *jointMats, int numJoints, int joint, const float rot[3][3] ) {
	qboolean under[MAX_MODEL_JOINTS];
	float pivot[3], m[12], *j;
	int i, r, c, k;

	if ( joint < 0 || joint >= numJoints || joint >= model->numJoints ) {
		return;
	}
	for ( r = 0; r < 3; r++ ) {
		pivot[r] = jointMats[12 * joint + r * 4 + 3];
	}
	for ( i = 0; i < numJoints && i < model->numJoints; i++ ) {
		// IQM joints come after their parents
		under[i] = i == joint || ( model->joints[i].parent >= 0 && model->joints[i].parent < i
								   && under[model->joints[i].parent] );
		if ( !under[i] ) {
			continue;
		}
		j = &jointMats[12 * i];
		for ( r = 0; r < 3; r++ ) {
			for ( c = 0; c < 3; c++ ) {
				m[r * 4 + c] = 0;
				for ( k = 0; k < 3; k++ ) {
					m[r * 4 + c] += rot[r][k] * j[k * 4 + c];
				}
			}
			m[r * 4 + 3] = pivot[r];
			for ( k = 0; k < 3; k++ ) {
				m[r * 4 + 3] += rot[r][k] * ( j[k * 4 + 3] - pivot[k] );
			}
		}
		memcpy( j, m, sizeof( m ) );
	}
}

/*
================
BG_ModelTranslateJoints

Moves a joint and every joint under it.
================
*/
void BG_ModelTranslateJoints( const bgModel_t *model, float *jointMats, int numJoints, int joint, const vec3_t offset ) {
	qboolean under[MAX_MODEL_JOINTS];
	int i;

	if ( joint < 0 || joint >= numJoints || joint >= model->numJoints ) {
		return;
	}
	for ( i = 0; i < numJoints && i < model->numJoints; i++ ) {
		under[i] = i == joint || ( model->joints[i].parent >= 0 && model->joints[i].parent < i
								   && under[model->joints[i].parent] );
		if ( under[i] ) {
			jointMats[12 * i + 3] += offset[0];
			jointMats[12 * i + 7] += offset[1];
			jointMats[12 * i + 11] += offset[2];
		}
	}
}

/*
================
BG_ModelAim

Turns a pose's torso and head to aim, as Quake 3 turns a player's torso and
head models: torsoAxis relative to the legs (the entity), headAxis relative
to the torso (CG_PlayerAngles's).
================
*/
void BG_ModelAim( const bgModel_t *model, float *jointMats, int numJoints, const vec3_t torsoAxis[3],
				  const vec3_t headAxis[3] ) {
	float t[3][3], h[3][3], th[3][3], rot[3][3];
	int r, c, k;

	// as matrices, the axes their columns
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			t[r][c] = torsoAxis[c][r];
			h[r][c] = headAxis[c][r];
		}
	}
	BG_ModelRotateJoints( model, jointMats, numJoints, model->torsoJoint, t );

	// the head's on top of the torso's: t h t^-1 about the head
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			th[r][c] = 0;
			for ( k = 0; k < 3; k++ ) {
				th[r][c] += t[r][k] * h[k][c];
			}
		}
	}
	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			rot[r][c] = 0;
			for ( k = 0; k < 3; k++ ) {
				rot[r][c] += th[r][k] * t[c][k];	// t^-1 = t transposed
			}
		}
	}
	BG_ModelRotateJoints( model, jointMats, numJoints, model->headJoint, rot );
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

/*
================
BG_SwimStrokeMsec

How long a swimming stroke takes: the player model's swim animation once
through (Daikatana's breaststroke, a second), else a second.
================
*/
int BG_SwimStrokeMsec( int playerModel ) {
	const bgModel_t *model;
	const bgAnim_t *anim;

	if ( BG_ValidModel( playerModel ) ) {
		model = &bg_models[playerModel];
		if ( model->playerAnims[LEGS_SWIM] >= 0 && model->playerAnims[LEGS_SWIM] < model->numAnims ) {
			anim = &model->anims[model->playerAnims[LEGS_SWIM]];
			if ( anim->fps > 0 && anim->numFrames > 1 ) {
				return (int)( anim->numFrames / anim->fps * 1000.0f );
			}
		}
	}
	return 1000;
}

/*
================
BG_SwimStrokePull

How hard the stroke pulls at swimPhase, 0 to 1: a swell while the arms
sweep back, nothing while they come forward again (the glide).
================
*/
float BG_SwimStrokePull( int swimPhase ) {
	float f = (float)( swimPhase & ( SWIM_PHASE - 1 ) ) / SWIM_PHASE;

	if ( f >= SWIM_PULL ) {
		return 0;
	}
	return sin( M_PI * f / SWIM_PULL );
}

#define SWIM_HEAD_YAW		60		// how far the head turns from the body, swimming
#define SWIM_HEAD_PITCH		45

/*
================
BG_PlayerSwimAngles

A swimmer's body lies along where it's going (pmove turns that toward where
it looks only so fast), the head turned to look, so far as a neck turns;
treading water, it faces the view, a little upright. qfalse when not
swimming. The angles as BG_PlayerAimAngles gives them (absolute).
================
*/
qboolean BG_PlayerSwimAngles( const bgModel_t *model, int legsAnim, const vec3_t velocity, const vec3_t viewAngles,
							  vec3_t legs, vec3_t torso, vec3_t head ) {
	vec3_t dir;
	float speed, d;
	int i;

	if ( ( legsAnim & ~ANIM_TOGGLEBIT ) != LEGS_SWIM || model->playerAnims[LEGS_SWIM] < 0 ) {
		return qfalse;
	}
	VectorCopy( velocity, dir );
	speed = VectorNormalize( dir );
	VectorClear( legs );
	if ( speed > 20 ) {
		vectoangles( dir, legs );
		legs[ROLL] = 0;
	} else {
		legs[YAW] = viewAngles[YAW];
		legs[PITCH] = -30;		// treading, head up
	}
	legs[PITCH] = AngleNormalize180( legs[PITCH] );
	VectorCopy( legs, torso );
	// the head looks, so far as it turns
	VectorCopy( viewAngles, head );
	for ( i = 0; i < 2; i++ ) {
		d = AngleNormalize180( head[i] - legs[i] );
		d = Com_Clamp( i == YAW ? -SWIM_HEAD_YAW : -SWIM_HEAD_PITCH, i == YAW ? SWIM_HEAD_YAW : SWIM_HEAD_PITCH, d );
		head[i] = legs[i] + d;
	}
	head[ROLL] = 0;
	return qtrue;
}

/*
================
BG_PlayerSwimOrigin

Where a swimmer's model goes: its swimming body's middle ("swim_center")
at the middle of the player's box, however it's turned.
================
*/
void BG_PlayerSwimOrigin( const bgModel_t *model, const vec3_t origin, const vec3_t axis[3], vec3_t out ) {
	int k;

	VectorCopy( origin, out );
	out[2] += 0.5f * ( model->mins[2] + model->maxs[2] );
	for ( k = 0; k < 3; k++ ) {
		VectorMA( out, -model->swimCenter[k], axis[k], out );
	}
}
