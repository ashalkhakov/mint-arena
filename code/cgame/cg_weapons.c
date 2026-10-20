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

In addition, Spearmint Source Code is also subject to certain additional terms.
You should have received a copy of these additional terms immediately following
the terms and conditions of the GNU General Public License.  If not, please
request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional
terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc.,
Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/
//
// cg_weapons.c -- events and effects dealing with weapons
#include "cg_local.h"

static qboolean CG_CalcMuzzlePoint( int entityNum, vec3_t muzzle );


/*
==========================
CG_RailTrail
==========================
*/
void CG_RailTrail (playerInfo_t *pi, vec3_t start, vec3_t end) {
	vec3_t axis[36], move, move2, vec, temp;
	float  len;
	int    i, j, skip;
 
	localEntity_t *le;
	refEntity_t   *re;
 
#define RADIUS   4
#define ROTATION 1
#define SPACING  5
 
	start[2] -= 4;
 
	le = CG_AllocLocalEntity();
	re = &le->refEntity;
 
	le->leType = LE_FADE_RGB;
	le->startTime = cg.time;
	le->endTime = cg.time + cg_railTrailTime.value;
	le->lifeRate = 1.0 / (le->endTime - le->startTime);
 
	re->shaderTime = cg.time;
	re->reType = RT_RAIL_CORE;
	re->customShader = cgs.media.railCoreShader;
 
	VectorCopy(start, re->origin);
	VectorCopy(end, re->oldorigin);
 
	re->shaderRGBA[0] = pi->color1[0] * 255;
	re->shaderRGBA[1] = pi->color1[1] * 255;
	re->shaderRGBA[2] = pi->color1[2] * 255;
	re->shaderRGBA[3] = 255;

	le->color[0] = pi->color1[0] * 0.75;
	le->color[1] = pi->color1[1] * 0.75;
	le->color[2] = pi->color1[2] * 0.75;
	le->color[3] = 1.0f;

	AxisClear( re->axis );
 
	if (cg_oldRail.integer)
	{
		// nudge down a bit so it isn't exactly in center
		re->origin[2] -= 8;
		re->oldorigin[2] -= 8;
		return;
	}

	VectorCopy (start, move);
	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);
	PerpendicularVector(temp, vec);
	for (i = 0 ; i < 36; i++)
	{
		RotatePointAroundVector(axis[i], vec, temp, i * 10);//banshee 2.4 was 10
	}

	VectorMA(move, 20, vec, move);
	VectorScale (vec, SPACING, vec);

	skip = -1;
 
	j = 18;
	for (i = 0; i < len; i += SPACING)
	{
		if (i != skip)
		{
			skip = i + SPACING;
			le = CG_AllocLocalEntity();
			re = &le->refEntity;
			le->leFlags = LEF_PUFF_DONT_SCALE;
			le->leType = LE_MOVE_SCALE_FADE;
			le->startTime = cg.time;
			le->endTime = cg.time + (i>>1) + 600;
			le->lifeRate = 1.0 / (le->endTime - le->startTime);

			re->shaderTime = cg.time;
			re->reType = RT_SPRITE;
			re->radius = 1.1f;
			re->customShader = cgs.media.railRingsShader;

			re->shaderRGBA[0] = pi->color2[0] * 255;
			re->shaderRGBA[1] = pi->color2[1] * 255;
			re->shaderRGBA[2] = pi->color2[2] * 255;
			re->shaderRGBA[3] = 255;

			le->color[0] = pi->color2[0] * 0.75;
			le->color[1] = pi->color2[1] * 0.75;
			le->color[2] = pi->color2[2] * 0.75;
			le->color[3] = 1.0f;

			le->pos.trType = TR_LINEAR;
			le->pos.trTime = cg.time;

			VectorCopy( move, move2);
			VectorMA(move2, RADIUS , axis[j], move2);
			VectorCopy(move2, le->pos.trBase);

			le->pos.trDelta[0] = axis[j][0]*6;
			le->pos.trDelta[1] = axis[j][1]*6;
			le->pos.trDelta[2] = axis[j][2]*6;
		}

		VectorAdd (move, vec, move);

		j = (j + ROTATION) % 36;
	}
}

/*
==========================
CG_GrappleTrail
==========================
*/
void CG_GrappleTrail( centity_t *ent, const weaponInfo_t *wi ) {
	vec3_t	origin;
	refEntity_t		beam;

	VectorCopy( ent->lerpOrigin, origin );
	ent->trailTime = cg.time;

	memset( &beam, 0, sizeof( beam ) );
	if ( !cg.cur_lc->renderingThirdPerson && cg.cur_ps->playerNum == ent->currentState.otherEntityNum ) {
		VectorCopy( cg.cur_lc->flashOrigin, beam.origin );
	} else {
		VectorCopy( cg_entities[ ent->currentState.otherEntityNum ].pe.flashOrigin, beam.origin );
	}
	VectorCopy( origin, beam.oldorigin );

	beam.reType = RT_LIGHTNING;
	beam.customShader = cgs.media.lightningShader;

	AxisClear( beam.axis );
	beam.shaderRGBA[0] = 0xff;
	beam.shaderRGBA[1] = 0xff;
	beam.shaderRGBA[2] = 0xff;
	beam.shaderRGBA[3] = 0xff;
	CG_AddRefEntityWithMinLight( &beam );
}

/*
=================
CG_DefSounds

Up to MAX_WEAPON_SOUNDS sounds a def's key lists, one is played at random.
=================
*/
static void CG_DefSounds( const dict_t *def, const char *key, sfxHandle_t *sounds ) {
	const char *list;
	char buf[MAX_STRING_CHARS];
	char *p, *token;
	int i;

	memset( sounds, 0, MAX_WEAPON_SOUNDS * sizeof( sounds[0] ) );
	if ( !def || !DictGetString( def, key, "", &list ) || !list[0] ) {
		return;
	}
	Q_strncpyz( buf, list, sizeof( buf ) );
	p = buf;
	for ( i = 0; i < MAX_WEAPON_SOUNDS; ) {
		token = COM_ParseExt( &p, qfalse );
		if ( !token[0] ) {
			break;
		}
		sounds[i] = trap_S_RegisterSound( token, qfalse );
		if ( sounds[i] ) {
			i++;
		}
	}
}

// one sound, the first a def's key lists
static sfxHandle_t CG_DefSound( const dict_t *def, const char *key ) {
	sfxHandle_t sounds[MAX_WEAPON_SOUNDS];

	CG_DefSounds( def, key, sounds );
	return sounds[0];
}

static sfxHandle_t CG_RandomSound( const sfxHandle_t *sounds ) {
	int c;

	for ( c = 0; c < MAX_WEAPON_SOUNDS && sounds[c]; c++ ) {
	}
	return c ? sounds[rand() % c] : 0;
}

static qhandle_t CG_DefModel( const dict_t *def, const char *key ) {
	const char *name;

	if ( !def || !DictGetString( def, key, "", &name ) || !name[0] ) {
		return 0;
	}
	return trap_R_RegisterModel( name );
}

static qhandle_t CG_DefEffect( const dict_t *def, const char *key ) {
	const char *name;

	if ( !def || !DictGetString( def, key, "", &name ) || !name[0] ) {
		return 0;
	}
	return CG_FX_RegisterEffect( name );
}

/*
=================
CG_ModelFrameNames

The frame names of an md3, up to max of them: Daikatana's models (and
Quake 2's) name their frames by animation, "ready01", "shoota01"...
=================
*/
#define MAX_MODEL_FRAMES	1024
#define MD3_FRAME_NAME		16

static int CG_ModelFrameNames( const char *name, char names[][MD3_FRAME_NAME], int max ) {
	fileHandle_t f;
	int len, numFrames, ofsFrames, i;
	byte header[108];
	byte frame[56];

	len = trap_FS_FOpenFile( name, &f, FS_READ );
	if ( len < (int)sizeof( header ) ) {
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		return 0;
	}
	trap_FS_Read( header, sizeof( header ), f );
	if ( memcmp( header, "IDP3", 4 ) ) {
		trap_FS_FCloseFile( f );
		return 0;
	}
	numFrames = LittleLong( *(int *)( header + 76 ) );
	ofsFrames = LittleLong( *(int *)( header + 92 ) );
	if ( numFrames > max ) {
		numFrames = max;
	}
	if ( numFrames < 0 || ofsFrames < (int)sizeof( header ) || ofsFrames + numFrames * (int)sizeof( frame ) > len ) {
		trap_FS_FCloseFile( f );
		return 0;
	}
	trap_FS_Seek( f, ofsFrames, FS_SEEK_SET );
	for ( i = 0; i < numFrames; i++ ) {
		trap_FS_Read( frame, sizeof( frame ), f );
		memcpy( names[i], frame + 40, MD3_FRAME_NAME );
		names[i][MD3_FRAME_NAME - 1] = 0;
	}
	trap_FS_FCloseFile( f );
	return numFrames;
}

/*
=================
CG_WeaponAnimations

The view model's animations, from the def's "anim_raise", "anim_lower",
"anim_idle", "anim_fire", "anim_reload" and "anim_empty": each the name of
its frames ("shoota" for "shoota01" to "shoota08"), and frames a second
("anim_fps", 10, by default).
=================
*/
static qboolean CG_WeaponAnimations( weaponInfo_t *wi, const dict_t *def, const char *model ) {
	static char names[MAX_MODEL_FRAMES][MD3_FRAME_NAME];
	static const struct {
		int			anim;
		const char	*key;
		qboolean	loop;
	} keys[] = {
		{ WP_ANIM_ACTIVATE, "anim_raise", qfalse },
		{ WP_ANIM_DISARM, "anim_lower", qfalse },
		{ WP_ANIM_IDLE, "anim_idle", qtrue },
		{ WP_ANIM_FIRE, "anim_fire", qfalse },
		{ WP_ANIM_RELOAD, "anim_reload", qfalse },
		{ WP_ANIM_EMPTY, "anim_empty", qfalse },
		{ WP_ANIM_EXTRA1, "anim_altfire", qfalse },
	};
	animation_t *anim;
	const char *value;
	char frameName[MAX_QPATH];
	float fps, defaultFps;
	int numFrames, i, j, k, len;

	numFrames = CG_ModelFrameNames( model, names, MAX_MODEL_FRAMES );
	if ( !numFrames ) {
		return qfalse;
	}
	defaultFps = DictGetFloat( def, "anim_fps", "10" );

	for ( i = 0; i < MAX_WEAPON_ANIMATIONS; i++ ) {
		anim = &wi->animations[i];
		anim->firstFrame = 0;
		anim->numFrames = 1;
		anim->loopFrames = 0;
		anim->frameLerp = anim->initialLerp = 100;
	}

	for ( i = 0; i < ARRAY_LEN( keys ); i++ ) {
		anim = &wi->animations[keys[i].anim];
		if ( !DictGetString( def, keys[i].key, "", &value ) || !value[0] ) {
			// no such animation: hold the idle one's first frame
			if ( keys[i].anim != WP_ANIM_IDLE ) {
				*anim = wi->animations[WP_ANIM_IDLE];
				anim->numFrames = 1;
				anim->loopFrames = 0;
			}
			continue;
		}
		fps = defaultFps;
		if ( sscanf( value, "%63s %f", frameName, &fps ) < 1 ) {
			continue;
		}
		if ( fps <= 0 ) {
			fps = defaultFps;
		}
		len = strlen( frameName );
		for ( j = 0; j < numFrames; j++ ) {
			if ( !Q_stricmpn( names[j], frameName, len ) && ( !names[j][len] || ( names[j][len] >= '0' && names[j][len] <= '9' ) ) ) {
				break;
			}
		}
		if ( j == numFrames ) {
			CG_Printf( S_COLOR_YELLOW "WARNING: no frames %s in %s\n", frameName, model );
			continue;
		}
		for ( k = j + 1; k < numFrames; k++ ) {
			if ( Q_stricmpn( names[k], frameName, len ) || ( names[k][len] && ( names[k][len] < '0' || names[k][len] > '9' ) ) ) {
				break;
			}
		}
		anim->firstFrame = j;
		anim->numFrames = k - j;
		anim->loopFrames = keys[i].loop ? anim->numFrames : 0;
		anim->frameLerp = anim->initialLerp = 1000 / fps;
		anim->reversed = anim->flipflop = 0;
	}
	return qtrue;
}

/*
=================
CG_ProjectileTrail

The projectile's "fx_fly", every "fx_fly_interval" seconds along its path.
=================
*/
static void CG_ProjectileTrail( centity_t *ent, const weaponInfo_t *wi ) {
	vec3_t	origin, velocity;
	int		t, startTime;
	entityState_t	*es;

	es = &ent->currentState;
	startTime = ent->trailTime;
	t = wi->flyInterval * ( ( startTime + wi->flyInterval ) / wi->flyInterval );

	for ( ; t <= cg.time; t += wi->flyInterval ) {
		BG_EvaluateTrajectory( &es->pos, t, origin );
		BG_EvaluateTrajectoryDelta( &es->pos, t, velocity );
		VectorNegate( velocity, velocity );
		if ( VectorNormalize( velocity ) == 0 ) {
			VectorSet( velocity, 0, 0, 1 );
		}
		CG_FX_PlayEffectID( wi->flyEffect, origin, velocity );
	}
	ent->trailTime = cg.time;
}

/*
=================
CG_RegisterWeapon

The media of a weapon, from its entity def (see bg_defs.c):
	"model_view"		the first person model, with "anim_*" animations
	"model_world"		the model on the player's tag_weapon (or the item's)
	"model_flash"		a flash on the weapon's tag_flash
	"icon"				(or the item's)
	"snd_fire"			sounds to choose from at random
	"snd_hum", "snd_firing", "snd_raise", "snd_altfire"
	"flashColor", "flashRadius"	a light where it's fired
	"fx_muzzle"			an effect (effects/*.efx) where it's fired
and from its projectile's def:
	"model", "light_color", "light_radius", "snd_fly"
	"fx_fly"			an effect every "fx_fly_interval" seconds (0.05)
	"fx_detonate", "snd_explode"	where it detonates, "snd_explode_flesh" on a
						body, "fx_detonate_water" and "snd_explode_water" in water
	"fx_bounce", "snd_bounce"	where it bounces or sticks
	"fx_impact", "fx_impact_flesh", "snd_impact"	where hitscan shots hit
	"mtr_detonate", "decal_size"	a mark left there
=================
*/
void CG_RegisterWeapon( int weaponNum ) {
	weaponInfo_t	*weaponInfo;
	const bgWeapon_t	*weapon;
	const dict_t	*def, *projectile;
	gitem_t			*ammo;
	const char		*name;
	vec3_t			mins, maxs;
	int				i;

	if ( !BG_ValidWeapon( weaponNum ) ) {
		return;
	}
	weaponInfo = &cg_weapons[weaponNum];
	if ( weaponInfo->registered ) {
		return;
	}

	memset( weaponInfo, 0, sizeof( *weaponInfo ) );
	weaponInfo->registered = qtrue;

	weapon = BG_Weapon( weaponNum );
	def = weapon->def;
	projectile = weapon->projectile;

	weaponInfo->item = BG_FindItemForWeapon( weaponNum );
	if ( weaponInfo->item ) {
		CG_RegisterItemVisuals( BG_ItemNumForItem( weaponInfo->item ) );
	}

	// the models
	weaponInfo->weaponModel = CG_DefModel( def, "model_world" );
	if ( !weaponInfo->weaponModel && weaponInfo->item && weaponInfo->item->world_model[0][0] ) {
		weaponInfo->weaponModel = trap_R_RegisterModel( weaponInfo->item->world_model[0] );
	}
	trap_R_ModelBounds( weaponInfo->weaponModel, mins, maxs, 0, 0, 0 );
	for ( i = 0 ; i < 3 ; i++ ) {
		weaponInfo->weaponMidpoint[i] = mins[i] + 0.5 * ( maxs[i] - mins[i] );
	}
	weaponInfo->flashModel = CG_DefModel( def, "model_flash" );
	weaponInfo->handsModel = trap_R_RegisterModel( "models/weapons2/shotgun/shotgun_hand.md3" );

	if ( DictGetString( def, "model_view", "", &name ) && name[0] ) {
		weaponInfo->firstModel = trap_R_RegisterModel( name );
		if ( weaponInfo->firstModel ) {
			weaponInfo->animated = CG_WeaponAnimations( weaponInfo, def, name );
		}
	}
	if ( !weaponInfo->firstModel ) {
		weaponInfo->firstModel = weaponInfo->weaponModel;
	}

	// the icons
	if ( DictGetString( def, "icon", "", &name ) && name[0] ) {
		weaponInfo->weaponIcon = trap_R_RegisterShader( name );
	} else if ( weaponInfo->item && weaponInfo->item->icon[0] ) {
		weaponInfo->weaponIcon = trap_R_RegisterShader( weaponInfo->item->icon );
	}
	weaponInfo->ammoIcon = weaponInfo->weaponIcon;

	ammo = BG_FindItemForAmmo( weaponNum );
	if ( ammo && ammo->world_model[0][0] ) {
		weaponInfo->ammoModel = trap_R_RegisterModel( ammo->world_model[0] );
	}

	// firing
	CG_DefSounds( def, "snd_fire", weaponInfo->flashSound );
	weaponInfo->readySound = CG_DefSound( def, "snd_hum" );
	weaponInfo->firingSound = CG_DefSound( def, "snd_firing" );
	weaponInfo->raiseSound = CG_DefSound( def, "snd_raise" );
	weaponInfo->altFireSound = CG_DefSound( def, "snd_altfire" );
	DictGetVector( def, "flashColor", "0 0 0", weaponInfo->flashDlightColor );
	weaponInfo->flashDlight = DictGetFloat( def, "flashRadius", "0" );
	weaponInfo->muzzleEffect = CG_DefEffect( def, "fx_muzzle" );

	// the projectile
	if ( !projectile ) {
		return;
	}
	weaponInfo->missileModel = CG_DefModel( projectile, "model" );
	weaponInfo->missileSound = CG_DefSound( projectile, "snd_fly" );
	DictGetVector( projectile, "light_color", "1 1 1", weaponInfo->missileDlightColor );
	weaponInfo->missileDlight = DictGetFloat( projectile, "light_radius", "0" );
	weaponInfo->flyEffect = CG_DefEffect( projectile, "fx_fly" );
	weaponInfo->flyInterval = DictGetFloat( projectile, "fx_fly_interval", "0.05" ) * 1000;
	if ( weaponInfo->flyInterval < 10 ) {
		weaponInfo->flyInterval = 10;
	}
	if ( weaponInfo->flyEffect ) {
		weaponInfo->missileTrailFunc = CG_ProjectileTrail;
	}
	weaponInfo->detonateEffect = CG_DefEffect( projectile, "fx_detonate" );
	CG_DefSounds( projectile, "snd_explode", weaponInfo->detonateSound );
	CG_DefSounds( projectile, "snd_explode_flesh", weaponInfo->detonateFleshSound );
	weaponInfo->waterEffect = CG_DefEffect( projectile, "fx_detonate_water" );
	CG_DefSounds( projectile, "snd_explode_water", weaponInfo->waterSound );
	weaponInfo->bounceEffect = CG_DefEffect( projectile, "fx_bounce" );
	CG_DefSounds( projectile, "snd_bounce", weaponInfo->bounceSound );
	weaponInfo->impactEffect = CG_DefEffect( projectile, "fx_impact" );
	weaponInfo->impactFleshEffect = CG_DefEffect( projectile, "fx_impact_flesh" );
	CG_DefSounds( projectile, "snd_impact", weaponInfo->impactSound );
	if ( DictGetString( projectile, "mtr_detonate", "", &name ) && name[0] ) {
		weaponInfo->markShader = trap_R_RegisterShader( name );
		weaponInfo->markRadius = DictGetFloat( projectile, "decal_size", "32" ) * 0.5f;
	}
}


/*
=================
CG_RegisterItemVisuals

The server says this item is used on this level
=================
*/
void CG_RegisterItemVisuals( int itemNum ) {
	itemInfo_t		*itemInfo;
	gitem_t			*item;

	if ( itemNum < 0 || itemNum >= BG_NumItems() ) {
		CG_Error( "CG_RegisterItemVisuals: itemNum %d out of range [0-%d]", itemNum, BG_NumItems()-1 );
	}

	itemInfo = &cg_items[ itemNum ];
	if ( itemInfo->registered ) {
		return;
	}

	item = BG_ItemForItemNum( itemNum );

	memset( itemInfo, 0, sizeof( *itemInfo ) );
	itemInfo->registered = qtrue;

	if ( item->world_model[0][0] ) {
		itemInfo->models[0] = trap_R_RegisterModel( item->world_model[0] );
	}

	if ( item->icon[0] ) {
		itemInfo->icon = trap_R_RegisterShader( item->icon );
	}

	if ( item->giType == IT_WEAPON ) {
		CG_RegisterWeapon( item->giTag );
	}

	//
	// powerups have an accompanying ring or sphere
	//
	if ( item->giType == IT_POWERUP || item->giType == IT_HEALTH ) {
		if ( item->world_model[1] ) {
			itemInfo->models[1] = trap_R_RegisterModel( item->world_model[1] );
		}
	}
}


/*
========================================================================================

VIEW WEAPON

========================================================================================
*/

/*
=================
CG_MapTorsoToWeaponFrame

=================
*/
static int CG_MapTorsoToWeaponFrame( playerInfo_t *pi, int frame ) {

	// change weapon
	if ( frame >= pi->animations[TORSO_DROP].firstFrame 
		&& frame < pi->animations[TORSO_DROP].firstFrame + 9 ) {
		return frame - pi->animations[TORSO_DROP].firstFrame + 6;
	}

	// stand attack
	if ( frame >= pi->animations[TORSO_ATTACK].firstFrame 
		&& frame < pi->animations[TORSO_ATTACK].firstFrame + 6 ) {
		return 1 + frame - pi->animations[TORSO_ATTACK].firstFrame;
	}

	// stand attack 2
	if ( frame >= pi->animations[TORSO_ATTACK2].firstFrame 
		&& frame < pi->animations[TORSO_ATTACK2].firstFrame + 6 ) {
		return 1 + frame - pi->animations[TORSO_ATTACK2].firstFrame;
	}
	
	return 0;
}


/*
==============
CG_CalculateViewModelPosition

A first person model (a weapon def's "model_view") is drawn at the eye,
facing the view, as Daikatana draws them (CL_AddWeaponBob): only a small
circle of bob while running, which settles when standing.
==============
*/
#define VIEWMODEL_BOB_RADIUS	0.35f
#define VIEWMODEL_BOB_RATE		0.032f		// radians per unit of speed
#define VIEWMODEL_BOB_SETTLE	7.5f		// radians a second

static void CG_CalculateViewModelPosition( const playerState_t *ps, vec3_t origin, vec3_t angles ) {
	static float bobTime;
	float frametime, speed, x, y;

	VectorCopy( cg.refdef.vieworg, origin );
	VectorCopy( cg.refdefViewAngles, angles );

	frametime = cg.frametime * 0.001f;
	speed = cg.xyspeed;
	if ( speed > 375 ) {
		speed = 375;
	}
	if ( speed > 3 && ps->groundEntityNum != ENTITYNUM_NONE ) {
		bobTime += speed * VIEWMODEL_BOB_RATE * frametime;
		if ( bobTime > 2 * M_PI ) {
			bobTime -= 2 * M_PI;
		}
	} else if ( bobTime >= M_PI ) {
		bobTime = MAX( M_PI, bobTime - VIEWMODEL_BOB_SETTLE * frametime );
	} else {
		bobTime = MIN( M_PI, bobTime + VIEWMODEL_BOB_SETTLE * frametime );
	}

	x = cos( bobTime ) * VIEWMODEL_BOB_RADIUS;
	y = fabs( x );
	VectorMA( origin, x, cg.refdef.viewaxis[1], origin );
	VectorMA( origin, -y, cg.refdef.viewaxis[2], origin );
}

/*
==============
CG_CalculateWeaponPosition
==============
*/
static void CG_CalculateWeaponPosition( vec3_t origin, vec3_t angles ) {
	float	scale;
	int		delta;
	float	fracsin;

	VectorCopy( cg.refdef.vieworg, origin );
	VectorCopy( cg.refdefViewAngles, angles );

	// on odd legs, invert some angles
	if ( cg.bobcycle & 1 ) {
		scale = -cg.xyspeed;
	} else {
		scale = cg.xyspeed;
	}

	// gun angles from bobbing
	angles[ROLL] += scale * cg.bobfracsin * 0.005;
	angles[YAW] += scale * cg.bobfracsin * 0.01;
	angles[PITCH] += cg.xyspeed * cg.bobfracsin * 0.005;

	// drop the weapon when landing
	delta = cg.time - cg.cur_lc->landTime;
	if ( delta < LAND_DEFLECT_TIME ) {
		origin[2] += cg.cur_lc->landChange*0.25 * delta / LAND_DEFLECT_TIME;
	} else if ( delta < LAND_DEFLECT_TIME + LAND_RETURN_TIME ) {
		origin[2] += cg.cur_lc->landChange*0.25 * 
			(LAND_DEFLECT_TIME + LAND_RETURN_TIME - delta) / LAND_RETURN_TIME;
	}

#if 0
	// drop the weapon when stair climbing
	delta = cg.time - cg.stepTime;
	if ( delta < STEP_TIME/2 ) {
		origin[2] -= cg.stepChange*0.25 * delta / (STEP_TIME/2);
	} else if ( delta < STEP_TIME ) {
		origin[2] -= cg.stepChange*0.25 * (STEP_TIME - delta) / (STEP_TIME/2);
	}
#endif

	// idle drift
	scale = 80; // cg.xyspeed + 40;
	fracsin = sin( cg.time * 0.001 );
	angles[ROLL] += scale * fracsin * 0.01;
	angles[YAW] += scale * fracsin * 0.01;
	angles[PITCH] += scale * fracsin * 0.01;
}


/*
========================
CG_AddWeaponWithPowerups
========================
*/
static void CG_AddWeaponWithPowerups( refEntity_t *gun, int powerups ) {
	// add powerup effects
	if ( powerups & ( 1 << PW_INVIS ) ) {
		gun->customShader = cgs.media.invisShader;
		CG_AddRefEntityWithMinLight( gun );
	} else {
		CG_AddRefEntityWithMinLight( gun );

		if ( powerups & ( 1 << PW_BATTLESUIT ) ) {
			gun->customShader = cgs.media.battleWeaponShader;
			CG_AddRefEntityWithMinLight( gun );
		}
		if ( powerups & ( 1 << PW_QUAD ) ) {
			gun->customShader = cgs.media.quadWeaponShader;
			CG_AddRefEntityWithMinLight( gun );
		}
	}
}


/*
=============
CG_AddPlayerWeapon

Used for both the view weapon (ps is valid) and the world modelother character models (ps is NULL)
The main player will have this called for BOTH cases, so effects like light and
sound should only be done on the world model case.
=============
*/
void CG_AddPlayerWeapon( refEntity_t *parent, playerState_t *ps, centity_t *cent, int team ) {
	refEntity_t	gun;
	refEntity_t	flash;
	vec3_t		angles;
	weapon_t	weaponNum;
	weaponInfo_t	*weapon;
	centity_t	*nonPredictedCent;
	orientation_t	lerped;
	playerInfo_t *pi;
	int		anim;

	weaponNum = cent->currentState.weapon;

	CG_RegisterWeapon( weaponNum );
	weapon = &cg_weapons[weaponNum];

	pi = &cgs.playerinfo[cent->currentState.playerNum];

	// add the weapon
	memset( &gun, 0, sizeof( gun ) );
	VectorCopy( parent->lightingOrigin, gun.lightingOrigin );
	gun.shadowPlane = parent->shadowPlane;
	gun.renderfx = parent->renderfx;

	Byte4Copy( pi->c1RGBA, gun.shaderRGBA );

	// make sure we aren't looking at cg.cur_lc->predictedPlayerEntity for LG
	if ( cent == &cg.cur_lc->predictedPlayerEntity ) {
		nonPredictedCent = &cg_entities[cent->currentState.playerNum];
	} else {
		nonPredictedCent = cent;
	}

	if ( !weapon->weaponModel || !weapon->flashModel ) {
		// use default flash origin when no flash model
		if ( ps ) {
			VectorCopy( cg.firstPersonViewOrg, cg.cur_lc->flashOrigin );
			VectorMA( cg.cur_lc->flashOrigin, -8, cg.firstPersonViewAxis[2], cg.cur_lc->flashOrigin );

			VectorCopy( cg.cur_lc->flashOrigin, flash.origin );
		} else {
			VectorCopy( cent->lerpOrigin, nonPredictedCent->pe.flashOrigin );

			anim = nonPredictedCent->currentState.legsAnim & ~ANIM_TOGGLEBIT;
			if ( anim == LEGS_WALKCR || anim == LEGS_IDLECR ) {
				nonPredictedCent->pe.flashOrigin[2] += CROUCH_VIEWHEIGHT - 6;
			} else {
				nonPredictedCent->pe.flashOrigin[2] += DEFAULT_VIEWHEIGHT - 6;
			}

			VectorCopy( nonPredictedCent->pe.flashOrigin, flash.origin );
		}
	}

	//Elder: We are in third person, use the third-person model (DEFAULT)
	if ( ps == NULL || !weapon->animated ) {
		gun.hModel = weapon->weaponModel;
	} 
	//Elder: we are in first-person, use the first-person (NOT default) model
	else {
		gun.hModel = weapon->firstModel;
	}
	if (!gun.hModel) {
		return;
	}

	if ( !ps ) {
		// add weapon ready sound
		cent->pe.lightningFiring = qfalse;
		if ( ( cent->currentState.eFlags & EF_FIRING ) && weapon->firingSound ) {
			// lightning gun and guantlet make a different sound when fire is held down
			trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, weapon->firingSound );
			cent->pe.lightningFiring = qtrue;
		} else if ( weapon->readySound ) {
			trap_S_AddLoopingSound( cent->currentState.number, cent->lerpOrigin, vec3_origin, weapon->readySound );
		}
	} else {
		// in first person
		if ( weapon->animated ) {
			CG_WeaponAnimation( cent, weapon, &gun.oldframe, &gun.frame, &gun.backlerp );
		}
	}

	if ( ( ps && weapon->animated ) || !parent->hModel ) {
		// a first person model goes where the hand is, keeping its own
		// animation's frames and lerp; on a skeletal character, where its
		// weapon joint is
		VectorCopy(parent->origin, gun.origin);
		AxisCopy( parent->axis, gun.axis );
	} else {
		trap_R_LerpTag(&lerped, parent->hModel, parent->oldframe, parent->frame,
			1.0 - parent->backlerp, "tag_weapon");
		VectorCopy(parent->origin, gun.origin);

		VectorMA(gun.origin, lerped.origin[0], parent->axis[0], gun.origin);

		// Make weapon appear left-handed for 2 and centered for 3
		if(ps && cg_drawGun[cg.cur_localPlayerNum].integer == 2)
			VectorMA(gun.origin, -lerped.origin[1], parent->axis[1], gun.origin);
		else if(!ps || cg_drawGun[cg.cur_localPlayerNum].integer != 3)
				VectorMA(gun.origin, lerped.origin[1], parent->axis[1], gun.origin);

		VectorMA(gun.origin, lerped.origin[2], parent->axis[2], gun.origin);

		MatrixMultiply(lerped.axis, ((refEntity_t *)parent)->axis, gun.axis);

		gun.backlerp = parent->backlerp;
	}

	CG_AddWeaponWithPowerups( &gun, cent->currentState.powerups );

	memset( &flash, 0, sizeof( flash ) );
	VectorCopy( parent->lightingOrigin, flash.lightingOrigin );
	flash.shadowPlane = parent->shadowPlane;
	flash.renderfx = parent->renderfx;

	flash.hModel = weapon->flashModel;
	if ( flash.hModel ) {
		angles[YAW] = 0;
		angles[PITCH] = 0;
		angles[ROLL] = crandom() * 10;
		AnglesToAxis( angles, flash.axis );

		Byte4Copy( pi->c1RGBA, flash.shaderRGBA );

		CG_PositionRotatedEntityOnTag( &flash, &gun, gun.hModel, "tag_flash");

		// update muzzle origin
		if ( ps ) {
			VectorCopy( flash.origin, cg.cur_lc->flashOrigin );
		} else {
			VectorCopy( flash.origin, nonPredictedCent->pe.flashOrigin );
		}
	} else if ( ps ) {
		VectorCopy( cg.cur_lc->flashOrigin, flash.origin );
	} else {
		VectorCopy( nonPredictedCent->pe.flashOrigin, flash.origin );
	}

	// impulse flash
	if ( cg.time - cent->muzzleFlashTime > MUZZLE_FLASH_TIME ) {
		return;
	}

	if ( flash.hModel ) {
		CG_AddRefEntityWithMinLight( &flash );
	}

	if ( ps || cg.cur_lc->renderingThirdPerson ||
		cent->currentState.number != cg.cur_lc->predictedPlayerState.playerNum ) {
		if ( weapon->flashDlight > 0 ) {
			trap_R_AddLightToScene( flash.origin, weapon->flashDlight + (rand()&31), 1.0f, weapon->flashDlightColor[0],
				weapon->flashDlightColor[1], weapon->flashDlightColor[2], 0 );
		}
	}
}


/*
==============
CG_AddViewWeapon

Add the weapon, and flash for the player's view
==============
*/
void CG_AddViewWeapon( playerState_t *ps ) {
	refEntity_t	hand;
	centity_t	*cent;
	playerInfo_t	*pi;
	vec3_t		fovOffset;
	vec3_t		angles;
	weaponInfo_t	*weapon;

	if ( ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}

	if ( ps->pm_type == PM_INTERMISSION ) {
		return;
	}

	// no gun if in third person view or a camera is active
	//if ( cg.cur_lc->renderingThirdPerson || cg.cur_lc->cameraMode) {
	if ( cg.cur_lc->renderingThirdPerson ) {
		return;
	}


	// allow the gun to be completely removed
	if ( !cg_drawGun[cg.cur_localPlayerNum].integer ) {
		// use default flash origin
		VectorCopy( cg.firstPersonViewOrg, cg.cur_lc->flashOrigin );
		VectorMA( cg.cur_lc->flashOrigin, -8, cg.firstPersonViewAxis[2], cg.cur_lc->flashOrigin );

		return;
	}

	// don't draw if testing a gun model
	if ( cg.testGun ) {
		return;
	}

	VectorClear(fovOffset);

	cent = &cg.cur_lc->predictedPlayerEntity;	// &cg_entities[cg.snap->ps.playerNum];
	CG_RegisterWeapon( ps->weapon );
	weapon = &cg_weapons[ ps->weapon ];

	if ( !weapon->animated ) {
		if ( cg.viewWeaponFov > 90 ) {
			// drop gun lower at higher fov
			fovOffset[2] = -0.2 * ( cg.viewWeaponFov - 90 ) * cg.refdef.weapon_fov_x / cg.viewWeaponFov;
		} else if ( cg.viewWeaponFov < 90 ) {
			// move gun forward at lowerer fov
			fovOffset[0] = -0.2 * ( cg.viewWeaponFov - 90 ) * cg.refdef.weapon_fov_x / cg.viewWeaponFov;
		}
	}

	memset (&hand, 0, sizeof(hand));

	// set up gun position
	if ( weapon->animated ) {
		// a first person model made for its place in front of the eye
		CG_CalculateViewModelPosition( ps, hand.origin, angles );
	} else {
		CG_CalculateWeaponPosition( hand.origin, angles );
	}

	VectorMA( hand.origin, (cg_gun_x.value+fovOffset[0]), cg.refdef.viewaxis[0], hand.origin );
	VectorMA( hand.origin, (cg_gun_y.value+fovOffset[1]), cg.refdef.viewaxis[1], hand.origin );
	VectorMA( hand.origin, (cg_gun_z.value+fovOffset[2]), cg.refdef.viewaxis[2], hand.origin );

	AnglesToAxis( angles, hand.axis );

	// map torso animations to weapon animations
	if ( cg_gun_frame.integer || weapon->animated ) {
		// development tool
		hand.frame = hand.oldframe = cg_gun_frame.integer;
		hand.backlerp = 0;
	} else {
		// get playerinfo for animation map
		pi = &cgs.playerinfo[ cent->currentState.playerNum ];
		hand.frame = CG_MapTorsoToWeaponFrame( pi, cent->pe.torso.frame );
		hand.oldframe = CG_MapTorsoToWeaponFrame( pi, cent->pe.torso.oldFrame );
		hand.backlerp = cent->pe.torso.backlerp;
	}

	hand.hModel = weapon->handsModel;
	hand.renderfx = RF_DEPTHHACK | RF_NO_MIRROR;

	// add everything onto the hand
	CG_AddPlayerWeapon( &hand, ps, &cg.cur_lc->predictedPlayerEntity, ps->persistant[PERS_TEAM] );
}

/*
==============================================================================

WEAPON SELECTION

==============================================================================
*/

/*
===================
CG_DrawWeaponSelect
===================
*/
void CG_DrawWeaponSelect( void ) {
	int		i;
	int		count;
	int		x, y;
	char	*name;
	float	*color;

	CG_SetScreenPlacement(PLACE_CENTER, PLACE_BOTTOM);

	// don't display if dead
	if ( cg.cur_lc->predictedPlayerState.stats[STAT_HEALTH] <= 0 ) {
		return;
	}

	color = CG_FadeColor( cg.cur_lc->weaponSelectTime, WEAPON_SELECT_TIME );
	if ( !color ) {
		return;
	}
	trap_R_SetColor( color );

	// showing weapon select clears pickup item display, but not the blend blob
	cg.cur_lc->itemPickupTime = 0;

	// count the number of weapons owned
	count = 0;
	for ( i = 1 ; i < MAX_WEAPONS ; i++ ) {
		if ( BG_HasWeapon( cg.cur_ps, i ) ) {
			count++;
		}
	}

	x = 320 - count * 20;
	y = 380;

	for ( i = 1 ; i < MAX_WEAPONS ; i++ ) {
		if ( !BG_HasWeapon( cg.cur_ps, i ) ) {
			continue;
		}

		CG_RegisterWeapon( i );

		// draw weapon icon
		CG_DrawPic( x, y, 32, 32, cg_weapons[i].weaponIcon );

		// draw selection marker
		if ( i == cg.cur_lc->weaponSelect ) {
			CG_DrawPic( x-4, y-4, 40, 40, cgs.media.selectShader );
		}

		// no ammo cross on top
		if ( !BG_WeaponHasAmmo( cg.cur_ps, i ) ) {
			CG_DrawPic( x, y, 32, 32, cgs.media.noammoShader );
		}

		x += 40;
	}

	// draw the selected name
	if ( cg_weapons[ cg.cur_lc->weaponSelect ].item ) {
		name = cg_weapons[ cg.cur_lc->weaponSelect ].item->pickup_name;
		if ( name ) {
			CG_DrawString( SCREEN_WIDTH / 2, y - 6, name, UI_CENTER|UI_VA_BOTTOM|UI_DROPSHADOW|UI_BIGFONT, color );
		}
	}

	trap_R_SetColor( NULL );
}


/*
===============
CG_WeaponSelectable
===============
*/
static qboolean CG_WeaponSelectable( playerState_t *ps, int i ) {
	if ( !BG_WeaponHasAmmo( ps, i ) ) {
		return qfalse;
	}
	if ( !BG_HasWeapon( ps, i ) ) {
		return qfalse;
	}

	return qtrue;
}

/*
===============
CG_NextWeapon_f
===============
*/
void CG_NextWeapon_f( int localPlayerNum ) {
	int		i;
	int		original;
	playerState_t	*ps;
	localPlayer_t	*player;

	if ( cg.localPlayers[localPlayerNum].playerNum == -1 ) {
		return;
	}

	ps = &cg.snap->pss[localPlayerNum];
	player = &cg.localPlayers[localPlayerNum];

	if ( ps->pm_flags & PMF_FOLLOW ) {
		return;
	}

	player->weaponSelectTime = cg.time;
	original = player->weaponSelect;

	for ( i = 0 ; i < MAX_WEAPONS ; i++ ) {
		player->weaponSelect++;
		if ( player->weaponSelect == MAX_WEAPONS ) {
			player->weaponSelect = 0;
		}
		if ( BG_Weapon( player->weaponSelect )->melee && cg_cyclePastGauntlet[localPlayerNum].integer ) {
			continue;		// never cycle to gauntlet
		}
		if ( CG_WeaponSelectable( ps, player->weaponSelect ) ) {
			break;
		}
	}
	if ( i == MAX_WEAPONS ) {
		player->weaponSelect = original;
	}
}

/*
===============
CG_PrevWeapon_f
===============
*/
void CG_PrevWeapon_f( int localPlayerNum ) {
	int		i;
	int		original;
	playerState_t	*ps;
	localPlayer_t	*player;

	if ( cg.localPlayers[localPlayerNum].playerNum == -1 ) {
		return;
	}

	ps = &cg.snap->pss[localPlayerNum];
	player = &cg.localPlayers[localPlayerNum];

	if ( ps->pm_flags & PMF_FOLLOW ) {
		return;
	}

	player->weaponSelectTime = cg.time;
	original = player->weaponSelect;

	for ( i = 0 ; i < MAX_WEAPONS ; i++ ) {
		player->weaponSelect--;
		if ( player->weaponSelect == -1 ) {
			player->weaponSelect = MAX_WEAPONS - 1;
		}
		if ( BG_Weapon( player->weaponSelect )->melee && cg_cyclePastGauntlet[localPlayerNum].integer ) {
			continue;		// never cycle to gauntlet
		}
		if ( CG_WeaponSelectable( ps, player->weaponSelect ) ) {
			break;
		}
	}
	if ( i == MAX_WEAPONS ) {
		player->weaponSelect = original;
	}
}

/*
===============
CG_WeaponInSlot

The weapon the player has in the slot (a weapon def's "weapon_slot"): the
one after the selected one when that's in the slot too, so pressing the key
again goes through them. WP_NONE for none.
===============
*/
static int CG_WeaponInSlot( playerState_t *ps, int slot, int selected ) {
	int i, w, start;

	start = ( BG_ValidWeapon( selected ) && BG_Weapon( selected )->slot == slot ) ? selected : 0;
	for ( i = 1; i < bg_numWeapons; i++ ) {
		w = ( start + i - 1 ) % ( bg_numWeapons - 1 ) + 1;
		if ( BG_Weapon( w )->slot == slot && BG_HasWeapon( ps, w ) ) {
			return w;
		}
	}
	return WP_NONE;
}

/*
===============
CG_Weapon_f

"weapon <slot>"
===============
*/
void CG_Weapon_f( int localPlayerNum ) {
	int		num;
	playerState_t	*ps;
	localPlayer_t	*player;

	if ( cg.localPlayers[localPlayerNum].playerNum == -1 ) {
		return;
	}

	ps = &cg.snap->pss[localPlayerNum];
	player = &cg.localPlayers[localPlayerNum];

	if ( ps->pm_flags & PMF_FOLLOW ) {
		return;
	}

	num = CG_WeaponInSlot( ps, atoi( CG_Argv( 1 ) ), player->weaponSelect );

	player->weaponSelectTime = cg.time;

	if ( num == WP_NONE ) {
		return;		// don't have the weapon
	}

	player->weaponSelect = num;
}


/*
===============
CG_WeaponToggle_f

select weapon and store old weapon or if already selecting the weapon
switch back to old weapon
===============
*/
void CG_WeaponToggle_f( int localPlayerNum ) {
	int		num;
	int		weapon, oldweapon;
	playerState_t	*ps;
	localPlayer_t	*player;

	if ( cg.localPlayers[localPlayerNum].playerNum == -1 ) {
		return;
	}

	ps = &cg.snap->pss[localPlayerNum];
	player = &cg.localPlayers[localPlayerNum];

	if ( ps->pm_flags & PMF_FOLLOW ) {
		return;
	}

	num = CG_WeaponInSlot( ps, atoi( CG_Argv( 1 ) ), player->weaponSelect );

	if ( num == WP_NONE ) {
		return;
	}

	player->weaponSelectTime = cg.time;

	if ( player->weaponSelect != num ) {
		weapon = num;
		oldweapon = player->weaponSelect;
	} else {
		weapon = player->weaponToggledFrom;
		oldweapon = WP_NONE;
	}

	if ( !BG_HasWeapon( ps, weapon ) ) {
		return;		// don't have the weapon
	}

	player->weaponSelect = weapon;
	player->weaponToggledFrom = oldweapon;
}

/*
===================
CG_OutOfAmmoChange

The current weapon has just run out of ammo
===================
*/
void CG_OutOfAmmoChange( int localPlayerNum ) {
	localPlayer_t	*player;
	playerState_t	*ps;
	int				i;

	if ( cg.localPlayers[localPlayerNum].playerNum == -1 ) {
		return;
	}

	player = &cg.localPlayers[localPlayerNum];
	ps = &cg.snap->pss[localPlayerNum];

	player->weaponSelectTime = cg.time;

	for ( i = MAX_WEAPONS-1 ; i > 0 ; i-- ) {
		if ( CG_WeaponSelectable( ps, i ) ) {
			player->weaponSelect = i;
			break;
		}
	}
}



/*
===================================================================================================

WEAPON EVENTS

===================================================================================================
*/

/*
================
CG_FireWeapon

Caused by an EV_FIRE_WEAPON event
================
*/
void CG_FireWeapon( centity_t *cent ) {
	entityState_t *ent;
	weaponInfo_t	*weap;
	sfxHandle_t		sfx;
	vec3_t			muzzle, forward;

	ent = &cent->currentState;
	if ( !BG_ValidWeapon( ent->weapon ) ) {
		return;
	}
	CG_RegisterWeapon( ent->weapon );
	weap = &cg_weapons[ ent->weapon ];

	// mark the entity as muzzle flashing, so when it is added it will
	// append the flash to the weapon model
	cent->muzzleFlashTime = cg.time;

	// play quad sound if needed
	if ( cent->currentState.powerups & ( 1 << PW_QUAD ) ) {
		trap_S_StartSound (NULL, cent->currentState.number, CHAN_ITEM, cgs.media.quadSound );
	}

	// play a sound
	sfx = CG_RandomSound( weap->flashSound );
	if ( sfx ) {
		trap_S_StartSound( NULL, ent->number, CHAN_WEAPON, sfx );
	}

	// the muzzle effect
	if ( weap->muzzleEffect && CG_CalcMuzzlePoint( ent->number, muzzle ) ) {
		AngleVectors( cent->lerpAngles, forward, NULL, NULL );
		CG_FX_PlayEffectID( weap->muzzleEffect, muzzle, forward );
	}
}


/*
================
CG_MissileBounce

Caused by an EV_GRENADE_BOUNCE event: the projectile's "fx_bounce" and
"snd_bounce", or Quake 3's grenade bounce.
================
*/
void CG_MissileBounce( centity_t *cent ) {
	const weaponInfo_t	*wi;
	sfxHandle_t			sfx;
	vec3_t				dir;

	CG_RegisterWeapon( cent->currentState.weapon );
	wi = &cg_weapons[ BG_ValidWeapon( cent->currentState.weapon ) ? cent->currentState.weapon : 0 ];
	if ( !wi->bounceEffect && !wi->bounceSound[0] ) {
		trap_S_StartSound( NULL, cent->currentState.number, CHAN_AUTO, ( rand() & 1 ) ? cgs.media.hgrenb1aSound : cgs.media.hgrenb2aSound );
		return;
	}
	if ( wi->bounceEffect ) {
		ByteToDir( cent->currentState.eventParm, dir );
		CG_FX_PlayEffectID( wi->bounceEffect, cent->lerpOrigin, dir );
	}
	sfx = CG_RandomSound( wi->bounceSound );
	if ( sfx ) {
		trap_S_StartSound( NULL, cent->currentState.number, CHAN_AUTO, sfx );
	}
}

/*
================
CG_AltFireWeapon

Caused by an EV_ALT_FIRE_WEAPON event
================
*/
void CG_AltFireWeapon( centity_t *cent ) {
	entityState_t	*ent;

	ent = &cent->currentState;
	if ( !BG_ValidWeapon( ent->weapon ) ) {
		return;
	}
	CG_RegisterWeapon( ent->weapon );
	if ( cg_weapons[ent->weapon].altFireSound ) {
		trap_S_StartSound( NULL, ent->number, CHAN_WEAPON, cg_weapons[ent->weapon].altFireSound );
	}
}


/*
=================
CG_MissileHitWall

Caused by an EV_MISSILE_MISS event, or directly by local bullet tracing:
the projectile's "fx_detonate" (or a hitscan shot's "fx_impact"), its sound
and its mark.
=================
*/
// water at the point, or just behind the surface it hit
static qboolean CG_InWater( const vec3_t origin, const vec3_t dir ) {
	vec3_t behind;

	VectorMA( origin, -4, dir, behind );
	return ( ( CG_PointContents( origin, 0 ) | CG_PointContents( behind, 0 ) ) & MASK_WATER ) != 0;
}

void CG_MissileHitWall( int weapon, int playerNum, vec3_t origin, vec3_t dir, impactSound_t soundType ) {
	const weaponInfo_t	*wi;
	qhandle_t		effect;
	sfxHandle_t		sfx;
	qboolean		hitscan;

	hitscan = BG_Weapon( weapon )->hitscan;
	CG_RegisterWeapon( weapon );
	wi = &cg_weapons[weapon];

	if ( hitscan ) {
		effect = soundType == IMPACTSOUND_FLESH ? wi->impactFleshEffect : wi->impactEffect;
		if ( !effect && soundType != IMPACTSOUND_FLESH ) {
			effect = cgs.media.fxBulletHit;
		}
		sfx = CG_RandomSound( wi->impactSound );
	} else if ( wi->waterEffect && CG_InWater( origin, dir ) ) {
		// gone off on (or in) water
		effect = wi->waterEffect;
		sfx = CG_RandomSound( wi->waterSound );
		if ( !sfx ) {
			sfx = CG_RandomSound( wi->detonateSound );
		}
	} else {
		effect = wi->detonateEffect;
		if ( !effect && BG_Weapon( weapon )->projectile && DictFindKey( BG_Weapon( weapon )->projectile, "def_splash_damage" ) ) {
			effect = cgs.media.fxExplosion;
		}
		sfx = soundType == IMPACTSOUND_FLESH ? CG_RandomSound( wi->detonateFleshSound ) : 0;
		if ( !sfx ) {
			sfx = CG_RandomSound( wi->detonateSound );
		}
	}

	if ( effect ) {
		CG_FX_PlayEffectID( effect, origin, dir );
	}
	if ( sfx ) {
		trap_S_StartSound( origin, ENTITYNUM_WORLD, CHAN_AUTO, sfx );
	}

	// impact mark
	if ( wi->markShader && soundType != IMPACTSOUND_FLESH ) {
		CG_ImpactMark( wi->markShader, origin, dir, random()*360, 1,1,1,1, qfalse, wi->markRadius, qfalse );
	}
}


/*
=================
CG_MissileHitPlayer
=================
*/
void CG_MissileHitPlayer( int weapon, vec3_t origin, vec3_t dir, int entityNum ) {
	CG_Bleed( origin, entityNum );
	CG_FX_PlayEffectID( cgs.media.fxBlood, origin, dir );

	CG_MissileHitWall( weapon, 0, origin, dir, IMPACTSOUND_FLESH );
}


/*
============================================================================

HITSCAN

============================================================================
*/

/*
================
CG_HitscanShot
================
*/
static void CG_HitscanShot( int weapon, vec3_t start, vec3_t end, int skipNum ) {
	trace_t		tr;
	int sourceContentType, destContentType;

	CG_Trace( &tr, start, NULL, NULL, end, skipNum, MASK_SHOT );

	sourceContentType = CG_PointContents( start, 0 );
	destContentType = CG_PointContents( tr.endpos, 0 );

	// FIXME: should probably move this cruft into CG_BubbleTrail
	if ( sourceContentType == destContentType ) {
		if ( sourceContentType & CONTENTS_WATER ) {
			CG_BubbleTrail( start, tr.endpos, 32 );
		}
	} else if ( sourceContentType & CONTENTS_WATER ) {
		trace_t trace;

		trap_CM_BoxTrace( &trace, end, start, NULL, NULL, 0, CONTENTS_WATER );
		CG_BubbleTrail( start, trace.endpos, 32 );
	} else if ( destContentType & CONTENTS_WATER ) {
		trace_t trace;

		trap_CM_BoxTrace( &trace, start, end, NULL, NULL, 0, CONTENTS_WATER );
		CG_BubbleTrail( tr.endpos, trace.endpos, 32 );
	}

	if ( tr.fraction == 1.0f || ( tr.surfaceFlags & SURF_NOIMPACT ) ) {
		// SURF_NOIMPACT will not make a flame puff or a mark
		return;
	}

	if ( cg_entities[tr.entityNum].currentState.eType == ET_PLAYER ) {
		CG_MissileHitPlayer( weapon, tr.endpos, tr.plane.normal, tr.entityNum );
	} else if ( tr.surfaceFlags & SURF_METALSTEPS ) {
		CG_MissileHitWall( weapon, 0, tr.endpos, tr.plane.normal, IMPACTSOUND_METAL );
	} else {
		CG_MissileHitWall( weapon, 0, tr.endpos, tr.plane.normal, IMPACTSOUND_DEFAULT );
	}
}

/*
================
CG_HitscanFire

An EV_HITSCAN event: the same shots the game made (G_HitscanPattern), from
the same seed, for their impacts.
================
*/
void CG_HitscanFire( entityState_t *es ) {
	const bgWeapon_t	*weapon;
	vec3_t		forward, right, up, dir, end;
	int			seed, i;

	weapon = BG_Weapon( es->weapon );
	if ( !weapon->hitscan ) {
		return;
	}

	// derive the right and up vectors from the forward vector, because
	// the client won't have any other information
	VectorNormalize2( es->origin2, forward );
	PerpendicularVector( right, forward );
	CrossProduct( forward, right, up );

	seed = es->eventParm;
	for ( i = 0; i < weapon->numProjectiles; i++ ) {
		BG_HitscanDir( forward, right, up, weapon->spread, &seed, dir );
		VectorMA( es->pos.trBase, weapon->range, dir, end );
		CG_HitscanShot( es->weapon, es->pos.trBase, end, es->otherEntityNum );
	}
}


/*
===============
CG_Tracer
===============
*/
void CG_Tracer( vec3_t source, vec3_t dest ) {
	vec3_t		forward, right;
	polyVert_t	verts[4];
	vec3_t		line;
	float		len, begin, end;
	vec3_t		start, finish;
	vec3_t		midpoint;

	// tracer
	VectorSubtract( dest, source, forward );
	len = VectorNormalize( forward );

	// start at least a little ways from the muzzle
	if ( len < 100 ) {
		return;
	}
	begin = 50 + random() * (len - 60);
	end = begin + cg_tracerLength.value;
	if ( end > len ) {
		end = len;
	}
	VectorMA( source, begin, forward, start );
	VectorMA( source, end, forward, finish );

	line[0] = DotProduct( forward, cg.refdef.viewaxis[1] );
	line[1] = DotProduct( forward, cg.refdef.viewaxis[2] );

	VectorScale( cg.refdef.viewaxis[1], line[1], right );
	VectorMA( right, -line[0], cg.refdef.viewaxis[2], right );
	VectorNormalize( right );

	VectorMA( finish, cg_tracerWidth.value, right, verts[0].xyz );
	verts[0].st[0] = 0;
	verts[0].st[1] = 1;
	verts[0].modulate[0] = 255;
	verts[0].modulate[1] = 255;
	verts[0].modulate[2] = 255;
	verts[0].modulate[3] = 255;

	VectorMA( finish, -cg_tracerWidth.value, right, verts[1].xyz );
	verts[1].st[0] = 1;
	verts[1].st[1] = 0;
	verts[1].modulate[0] = 255;
	verts[1].modulate[1] = 255;
	verts[1].modulate[2] = 255;
	verts[1].modulate[3] = 255;

	VectorMA( start, -cg_tracerWidth.value, right, verts[2].xyz );
	verts[2].st[0] = 1;
	verts[2].st[1] = 1;
	verts[2].modulate[0] = 255;
	verts[2].modulate[1] = 255;
	verts[2].modulate[2] = 255;
	verts[2].modulate[3] = 255;

	VectorMA( start, cg_tracerWidth.value, right, verts[3].xyz );
	verts[3].st[0] = 0;
	verts[3].st[1] = 0;
	verts[3].modulate[0] = 255;
	verts[3].modulate[1] = 255;
	verts[3].modulate[2] = 255;
	verts[3].modulate[3] = 255;

	trap_R_AddPolyToScene( cgs.media.tracerShader, 4, verts, 0, 0 );

	midpoint[0] = ( start[0] + finish[0] ) * 0.5;
	midpoint[1] = ( start[1] + finish[1] ) * 0.5;
	midpoint[2] = ( start[2] + finish[2] ) * 0.5;

	// add the tracer sound
	trap_S_StartSound( midpoint, ENTITYNUM_WORLD, CHAN_AUTO, cgs.media.tracerSound );

}


/*
======================
CG_CalcMuzzlePoint
======================
*/
static qboolean	CG_CalcMuzzlePoint( int entityNum, vec3_t muzzle ) {
	vec3_t		forward;
	centity_t	*cent;
	int			anim;
	playerState_t *ps;

	ps = CG_LocalPlayerState(entityNum);
	if ( ps ) {
		VectorCopy( ps->origin, muzzle );
		muzzle[2] += ps->viewheight;
		AngleVectors( ps->viewangles, forward, NULL, NULL );
		VectorMA( muzzle, 14, forward, muzzle );
		return qtrue;
	}

	cent = &cg_entities[entityNum];
	if ( !cent->currentValid ) {
		return qfalse;
	}

	VectorCopy( cent->currentState.pos.trBase, muzzle );

	AngleVectors( cent->currentState.apos.trBase, forward, NULL, NULL );
	anim = cent->currentState.legsAnim & ~ANIM_TOGGLEBIT;
	if ( anim == LEGS_WALKCR || anim == LEGS_IDLECR ) {
		muzzle[2] += CROUCH_VIEWHEIGHT;
	} else {
		muzzle[2] += DEFAULT_VIEWHEIGHT;
	}

	VectorMA( muzzle, 14, forward, muzzle );

	return qtrue;

}

/*
======================
CG_Bullet

Renders bullet effects.
======================
*/
void CG_Bullet( vec3_t end, int sourceEntityNum, vec3_t normal, qboolean flesh, int fleshEntityNum ) {
	trace_t trace;
	int sourceContentType, destContentType;
	vec3_t		start;

	// if the shooter is currently valid, calc a source point and possibly
	// do trail effects
	if ( sourceEntityNum >= 0 && cg_tracerChance.value > 0 ) {
		if ( CG_CalcMuzzlePoint( sourceEntityNum, start ) ) {
			sourceContentType = CG_PointContents( start, 0 );
			destContentType = CG_PointContents( end, 0 );

			// do a complete bubble trail if necessary
			if ( ( sourceContentType == destContentType ) && ( sourceContentType & CONTENTS_WATER ) ) {
				CG_BubbleTrail( start, end, 32 );
			}
			// bubble trail from water into air
			else if ( ( sourceContentType & CONTENTS_WATER ) ) {
				trap_CM_BoxTrace( &trace, end, start, NULL, NULL, 0, CONTENTS_WATER );
				CG_BubbleTrail( start, trace.endpos, 32 );
			}
			// bubble trail from air into water
			else if ( ( destContentType & CONTENTS_WATER ) ) {
				trap_CM_BoxTrace( &trace, start, end, NULL, NULL, 0, CONTENTS_WATER );
				CG_BubbleTrail( trace.endpos, end, 32 );
			}

			// draw a tracer
			if ( random() < cg_tracerChance.value ) {
				CG_Tracer( start, end );
			}
		}
	}

	// impact splash and mark
	if ( flesh ) {
		CG_Bleed( end, fleshEntityNum );
	} else {
		CG_MissileHitWall( WP_NONE, 0, end, normal, IMPACTSOUND_DEFAULT );
	}

}
