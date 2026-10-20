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

/*****************************************************************************
 * name:		ai_weap.c
 *
 * desc:		weapon AI
 *
 * $Archive: /MissionPack/code/game/ai_weap.c $
 *
 *****************************************************************************/

#include "g_local.h"
#include "../botlib/botlib.h"
#include "../botlib/be_aas.h"
#include "../idlib/l_script.h"
#include "../idlib/l_precomp.h"
#include "ai_parse.h"
#include "../idlib/l_struct.h"
//
#include "ai_char.h"
#include "ai_chat_sys.h"
#include "ai_ea.h"
#include "ai_gen.h"
#include "ai_goal.h"
#include "ai_move.h"
#include "ai_weap.h"
#include "ai_weight.h"
//
#include "ai_main.h"
#include "ai_dmq3.h"
#include "ai_chat.h"
#include "ai_cmd.h"
#include "ai_vcmd.h"
#include "ai_dmnet.h"
#include "ai_team.h"
//
#include "chars.h"				//characteristics
#include "inv.h"				//indexes into the inventory
#include "syn.h"				//synonyms
#include "match.h"				//string matching types and vars


//#define DEBUG_AI_WEAP

//structure field offsets
#define WEAPON_OFS(x) (size_t)&(((weaponinfo_t *)0)->x)
#define PROJECTILE_OFS(x) (size_t)&(((projectileinfo_t *)0)->x)

//weapon definition
static fielddef_t weaponinfo_fields[] =
{
{"number", WEAPON_OFS(number), FT_INT},						//weapon number
{"name", WEAPON_OFS(name), FT_STRING},							//name of the weapon
{"level", WEAPON_OFS(level), FT_INT},
{"model", WEAPON_OFS(model), FT_STRING},						//model of the weapon
{"weaponindex", WEAPON_OFS(weaponindex), FT_INT},			//index of weapon in inventory
{"flags", WEAPON_OFS(flags), FT_INT},							//special flags
{"projectile", WEAPON_OFS(projectile), FT_STRING},			//projectile used by the weapon
{"numprojectiles", WEAPON_OFS(numprojectiles), FT_INT},	//number of projectiles
{"hspread", WEAPON_OFS(hspread), FT_FLOAT},					//horizontal spread of projectiles (degrees from middle)
{"vspread", WEAPON_OFS(vspread), FT_FLOAT},					//vertical spread of projectiles (degrees from middle)
{"speed", WEAPON_OFS(speed), FT_FLOAT},						//speed of the projectile (0 = instant hit)
{"acceleration", WEAPON_OFS(acceleration), FT_FLOAT},		//"acceleration" * time (in seconds) + "speed" = projectile speed
{"recoil", WEAPON_OFS(recoil), FT_FLOAT|FT_ARRAY, 3},		//amount of recoil the player gets from the weapon
{"offset", WEAPON_OFS(offset), FT_FLOAT|FT_ARRAY, 3},		//projectile start offset relative to eye and view angles
{"angleoffset", WEAPON_OFS(angleoffset), FT_FLOAT|FT_ARRAY, 3},//offset of the shoot angles relative to the view angles
{"extrazvelocity", WEAPON_OFS(extrazvelocity), FT_FLOAT},//extra z velocity the projectile gets
{"ammoamount", WEAPON_OFS(ammoamount), FT_INT},				//ammo amount used per shot
{"ammoindex", WEAPON_OFS(ammoindex), FT_INT},				//index of ammo in inventory
{"activate", WEAPON_OFS(activate), FT_FLOAT},				//time it takes to select the weapon
{"reload", WEAPON_OFS(reload), FT_FLOAT},						//time it takes to reload the weapon
{"spinup", WEAPON_OFS(spinup), FT_FLOAT},						//time it takes before first shot
{"spindown", WEAPON_OFS(spindown), FT_FLOAT},				//time it takes before weapon stops firing
{NULL, 0, 0, 0}
};

//projectile definition
static fielddef_t projectileinfo_fields[] =
{
{"name", PROJECTILE_OFS(name), FT_STRING},					//name of the projectile
{"model", PROJECTILE_OFS(model), FT_STRING},					//model of the projectile
{"flags", PROJECTILE_OFS(flags), FT_INT},						//special flags
{"gravity", PROJECTILE_OFS(gravity), FT_FLOAT},				//amount of gravity applied to the projectile [0,1]
{"damage", PROJECTILE_OFS(damage), FT_INT},					//damage of the projectile
{"radius", PROJECTILE_OFS(radius), FT_FLOAT},				//radius of damage
{"visdamage", PROJECTILE_OFS(visdamage), FT_INT},			//damage of the projectile to visible entities
{"damagetype", PROJECTILE_OFS(damagetype), FT_INT},		//type of damage (combination of the DAMAGETYPE_? flags)
{"healthinc", PROJECTILE_OFS(healthinc), FT_INT},			//health increase the owner gets
{"push", PROJECTILE_OFS(push), FT_FLOAT},						//amount a player is pushed away from the projectile impact
{"detonation", PROJECTILE_OFS(detonation), FT_FLOAT},		//time before projectile explodes after fire pressed
{"bounce", PROJECTILE_OFS(bounce), FT_FLOAT},				//amount the projectile bounces
{"bouncefric", PROJECTILE_OFS(bouncefric), FT_FLOAT}, 	//amount the bounce decreases per bounce
{"bouncestop", PROJECTILE_OFS(bouncestop), FT_FLOAT},		//minimum bounce value before bouncing stops
//recurive projectile definition??
{NULL, 0, 0, 0}
};

static structdef_t weaponinfo_struct =
{
	sizeof(weaponinfo_t), weaponinfo_fields
};
static structdef_t projectileinfo_struct =
{
	sizeof(projectileinfo_t), projectileinfo_fields
};

static bot_weaponstate_t botweaponstates[MAX_CLIENTS+1];
static weaponconfig_t weaponconfig;

//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
int BotValidWeaponNumber(int weaponnum)
{
	if (weaponnum <= 0 || weaponnum > weaponconfig.numweapons)
	{
		BotAI_Print(PRT_ERROR, "weapon number out of range\n");
		return qfalse;
	} //end if
	return qtrue;
} //end of the function BotValidWeaponNumber
//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
bot_weaponstate_t *BotWeaponStateFromHandle(int handle)
{
	if (handle <= 0 || handle > MAX_CLIENTS)
	{
		BotAI_Print(PRT_FATAL, "weapon state handle %d out of range\n", handle);
		return NULL;
	} //end if
	return &botweaponstates[handle];
} //end of the function BotWeaponStateFromHandle
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
#ifdef DEBUG_AI_WEAP
void DumpWeaponConfig(weaponconfig_t *wc)
{
	FILE *fp;
	int i;

	fp = Log_FileStruct();
	if (!fp) return;
	for (i = 0; i < wc->numprojectiles; i++)
	{
		WriteStructure(fp, &projectileinfo_struct, (char *) &wc->projectileinfo[i]);
		Log_Flush();
	} //end for
	for (i = 0; i < wc->numweapons; i++)
	{
		WriteStructure(fp, &weaponinfo_struct, (char *) &wc->weaponinfo[i]);
		Log_Flush();
	} //end for
} //end of the function DumpWeaponConfig
#endif //DEBUG_AI_WEAP
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
qboolean LoadWeaponConfig(char *filename)
{
	token_t token;
	char path[MAX_QPATH];
	int i, j;
	source_t *source;
	weaponconfig_t *wc;
	weaponinfo_t weaponinfo;

	Q_strncpyz(path, filename, sizeof(path));
	source = PC_LoadSource(path, BOTFILESBASEFOLDER, NULL);
	if (!source)
	{
		BotAI_Print(PRT_ERROR, "counldn't load %s\n", path);
		return qfalse;
	} //end if
	//initialize weapon config
	wc = &weaponconfig;
	memset(wc, 0, sizeof (weaponconfig_t) );
	wc->numweapons = MAX_WEAPONS;
	wc->numprojectiles = 0;
	//parse the source file
	while(BotPC_ReadToken(source, &token))
	{
		if (!strcmp(token.string, "weaponinfo"))
		{
			Com_Memset(&weaponinfo, 0, sizeof(weaponinfo_t));
			if (!ReadStructure(source, &weaponinfo_struct, (void *) &weaponinfo))
			{
				PC_FreeSource(source);
				return qfalse;
			} //end if
			if (weaponinfo.number < 0 || weaponinfo.number >= MAX_WEAPONS)
			{
				BotAI_Print(PRT_ERROR, "weapon info number %d out of range in %s\n", weaponinfo.number, path);
				PC_FreeSource(source);
				return qfalse;
			} //end if
			Com_Memcpy(&wc->weaponinfo[weaponinfo.number], &weaponinfo, sizeof(weaponinfo_t));
			wc->weaponinfo[weaponinfo.number].valid = qtrue;
		} //end if
		else if (!strcmp(token.string, "projectileinfo"))
		{
			if (wc->numprojectiles >= MAX_WEAPONS)
			{
				BotAI_Print(PRT_ERROR, "more than %d projectiles defined in %s\n", MAX_WEAPONS, path);
				PC_FreeSource(source);
				return qfalse;
			} //end if
			Com_Memset(&wc->projectileinfo[wc->numprojectiles], 0, sizeof(projectileinfo_t));
			if (!ReadStructure(source, &projectileinfo_struct, (void *) &wc->projectileinfo[wc->numprojectiles]))
			{
				PC_FreeSource(source);
				return qfalse;
			} //end if
			wc->numprojectiles++;
		} //end if
		else
		{
			BotAI_Print(PRT_ERROR, "unknown definition %s in %s\n", token.string, path);
			PC_FreeSource(source);
			return qfalse;
		} //end else
	} //end while
	PC_FreeSource(source);
	//fix up weapons
	for (i = 0; i < wc->numweapons; i++)
	{
		if (!wc->weaponinfo[i].valid) continue;
		if (!wc->weaponinfo[i].name[0])
		{
			BotAI_Print(PRT_ERROR, "weapon %d has no name in %s\n", i, path);
			return qfalse;
		} //end if
		if (!wc->weaponinfo[i].projectile[0])
		{
			BotAI_Print(PRT_ERROR, "weapon %s has no projectile in %s\n", wc->weaponinfo[i].name, path);
			return qfalse;
		} //end if
		//find the projectile info and copy it to the weapon info
		for (j = 0; j < wc->numprojectiles; j++)
		{
			if (!strcmp(wc->projectileinfo[j].name, wc->weaponinfo[i].projectile))
			{
				Com_Memcpy(&wc->weaponinfo[i].proj, &wc->projectileinfo[j], sizeof(projectileinfo_t));
				break;
			} //end if
		} //end for
		if (j == wc->numprojectiles)
		{
			BotAI_Print(PRT_ERROR, "weapon %s uses undefined projectile in %s\n", wc->weaponinfo[i].name, path);
			return qfalse;
		} //end if
	} //end for
	if (!wc->numweapons) BotAI_Print(PRT_WARNING, "no weapon info loaded\n");
	BotAI_Print(PRT_DEVELOPER, "loaded %s\n", path);
	wc->valid = qtrue;
	return qtrue;
} //end of the function LoadWeaponConfig
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void WeaponWeightIndex(const weaponconfig_t *wc, weightconfig_t *wwc, int *index)
{
	int i;

	for (i = 0; i < wc->numweapons; i++)
	{
		index[i] = FindFuzzyWeight(wwc, wc->weaponinfo[i].name);
	} //end for
	for ( ; i < MAX_WEAPONS; i++)
	{
		index[i] = -1;
	} //end for
} //end of the function WeaponWeightIndex
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotFreeWeaponWeights(int weaponstate)
{
	bot_weaponstate_t *ws;

	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return;
	if (ws->weaponweightconfig) FreeWeightConfig(ws->weaponweightconfig);
} //end of the function BotFreeWeaponWeights
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotLoadWeaponWeights(int weaponstate, char *filename)
{
	bot_weaponstate_t *ws;

	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return BLERR_CANNOTLOADWEAPONWEIGHTS;
	BotFreeWeaponWeights(weaponstate);
	//
	ws->weaponweightconfig = ReadWeightConfig(filename);
	if (!ws->weaponweightconfig)
	{
		BotAI_Print(PRT_FATAL, "couldn't load weapon config %s\n", filename);
		return BLERR_CANNOTLOADWEAPONWEIGHTS;
	} //end if
	WeaponWeightIndex(&weaponconfig, ws->weaponweightconfig, ws->weaponweightindex);
	return BLERR_NOERROR;
} //end of the function BotLoadWeaponWeights
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
// The weapons are entity defs now (bg_defs.c), so what bots know of one
// comes from its def and its projectile's, not from botfiles/weapons.c.
//===========================================================================
extern bot_state_t *botstates[MAX_CLIENTS];

static int BotDefDamage( const dict_t *projectile, const char *key, float *radius )
{
	const dict_t *damage;
	const char *name;

	if ( radius ) *radius = 0;
	if ( !projectile || !DictGetString( projectile, key, "", &name ) || !name[0] ) return 0;
	damage = BG_FindDef( name );
	if ( !damage ) return 0;
	if ( radius ) *radius = DictGetFloat( damage, "radius", "0" );
	return DictGetInt( damage, "damage", "0" );
}

void BotGetWeaponInfo(int weaponstate, int weapon, weaponinfo_t *weaponinfo)
{
	const bgWeapon_t *w;
	vec3_t velocity;
	float radius;

	(void)weaponstate;
	memset( weaponinfo, 0, sizeof( *weaponinfo ) );
	if ( !BG_ValidWeapon( weapon ) ) return;
	w = BG_Weapon( weapon );

	weaponinfo->valid = qtrue;
	weaponinfo->number = weapon;
	Q_strncpyz( weaponinfo->name, w->name, sizeof( weaponinfo->name ) );
	weaponinfo->numprojectiles = w->numProjectiles;
	weaponinfo->hspread = weaponinfo->vspread = w->spread;
	weaponinfo->ammoamount = w->ammoRequired;
	weaponinfo->ammoindex = weapon;
	weaponinfo->activate = w->raiseTime * 0.001f;
	weaponinfo->reload = w->fireTime * 0.001f;
	if ( !w->projectile ) return;

	// hitscan shots have no speed, so bots don't lead them
	if ( !w->hitscan ) {
		DictGetVector( w->projectile, "velocity", "900 0 0", velocity );
		weaponinfo->speed = velocity[0];
		weaponinfo->proj.gravity = DictGetFloat( w->projectile, "gravity", "0" ) ? 1.0f : 0.0f;
		weaponinfo->proj.bounce = DictGetFloat( w->projectile, "bounce", "0" );
	}
	weaponinfo->proj.damage = BotDefDamage( w->projectile, "def_damage", NULL );
	weaponinfo->proj.damagetype = DAMAGETYPE_IMPACT;
	if ( BotDefDamage( w->projectile, "def_splash_damage", &radius ) ) {
		weaponinfo->proj.damagetype |= DAMAGETYPE_RADIAL;
		weaponinfo->proj.radius = radius;
	}
} //end of the function BotGetWeaponInfo
//===========================================================================
// The best weapon the bot has ammo for: the weapon defs' "bot_rating"
// (Daikatana's Weapons.csv ratings), melee weapons only when the enemy is
// in reach, splash weapons not when it's too close. 0 rating: never.
//===========================================================================
int BotChooseBestFightWeapon(int weaponstate, int *inventory)
{
	bot_weaponstate_t *ws;
	int bestweapon, w, botNum;
	float rating, bestrating, dist, radius;
	playerState_t ps;

	ws = BotWeaponStateFromHandle(weaponstate);
	if (!ws) return 0;

	// whose weapon state this is
	for ( botNum = 0; botNum < MAX_CLIENTS; botNum++ ) {
		if ( botstates[botNum] && botstates[botNum]->inuse && botstates[botNum]->ws == weaponstate ) break;
	}
	if ( botNum == MAX_CLIENTS ) return 0;
	ps = botstates[botNum]->cur_ps;
	dist = inventory[ENEMY_HORIZONTAL_DIST];

	bestrating = 0;
	bestweapon = 0;
	for ( w = 1; w < bg_numWeapons; w++ ) {
		const bgWeapon_t *weapon = BG_Weapon( w );

		if ( !BG_HasWeapon( &ps, w ) || !BG_WeaponHasAmmo( &ps, w ) ) continue;
		rating = DictGetFloat( weapon->def, "bot_rating", va( "%d", weapon->slot * 100 ) );
		if ( rating <= 0 ) continue;
		if ( weapon->melee && dist > weapon->range * 2 ) {
			rating *= 0.1f;		// keep as a last resort
		}
		if ( weapon->projectile && !weapon->hitscan
			&& BotDefDamage( weapon->projectile, "def_splash_damage", &radius ) && dist < radius ) {
			rating *= 0.5f;		// would hurt itself
		}
		if ( rating > bestrating ) {
			bestrating = rating;
			bestweapon = w;
		}
	}
	return bestweapon;
} //end of the function BotChooseBestFightWeapon
//===========================================================================
// The best "bot_rating" of the weapons the bot has ammo for, of its ranged
// ones only or of all; 0 for none
//===========================================================================
float BotBestWeaponRating(bot_state_t *bs, qboolean ranged)
{
	float rating, best = 0;
	int w;

	for ( w = 1; w < bg_numWeapons; w++ ) {
		const bgWeapon_t *weapon = BG_Weapon( w );

		if ( !BG_HasWeapon( &bs->cur_ps, w ) || !BG_WeaponHasAmmo( &bs->cur_ps, w ) ) continue;
		if ( ranged && weapon->melee ) continue;
		rating = DictGetFloat( weapon->def, "bot_rating", va( "%d", weapon->slot * 100 ) );
		if ( rating > best ) best = rating;
	}
	return best;
} //end of the function BotBestWeaponRating
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotResetWeaponState(int weaponstate)
{
} //end of the function BotResetWeaponState
//========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//========================================================================
void BotFreeWeaponState(int handle)
{
	BotFreeWeaponWeights(handle);
} //end of the function BotFreeWeaponState
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
int BotSetupWeaponAI(void)
{
	if (!LoadWeaponConfig("weapons.c"))
	{
		BotAI_Print(PRT_FATAL, "couldn't load the weapon config\n");
		return BLERR_CANNOTLOADWEAPONCONFIG;
	} //end if

#ifdef DEBUG_AI_WEAP
	DumpWeaponConfig(weaponconfig);
#endif //DEBUG_AI_WEAP
	//
	return BLERR_NOERROR;
} //end of the function BotSetupWeaponAI
//===========================================================================
//
// Parameter:				-
// Returns:					-
// Changes Globals:		-
//===========================================================================
void BotShutdownWeaponAI(void)
{
	weaponconfig.valid = qfalse;
} //end of the function BotShutdownWeaponAI

