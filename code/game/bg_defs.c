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

// bg_defs.c -- what the game and the cgame build from entity defs
// (idlib/decl_entitydef.c): the weapons, the items and their models

#ifdef CGAME
#include "../cgame/cg_local.h"
#elif defined UI
#include "../ui/ui_local.h"
#else
#include "g_local.h"
#endif

/*
===============================================================================

WEAPONS

A weapon is an entity def with a "weapon_slot", the number key that selects
it. Weapons are numbered from 1 by slot, then by name, the same in the game
and the cgame. Its keys (Doom 3's names where Doom 3 has one):
	"weapon_slot" N			the number key
	"fireRate" S			seconds between shots
	"ammoRequired" N		ammo a shot takes, 0 for none
	"maxAmmo" N				the most ammo it holds
	"raiseTime" S, "lowerTime" S	seconds to raise and put it away
	"melee" 1				fires only at something in reach
	"burstCount" N			shots a pull of the trigger fires, "burstRate"
							seconds apart; "fireRate" is then the whole burst's
	"clipSize" N			shots loaded at once (Doom 3's); empty, it reloads,
							"reloadRate" seconds (and on "+reload")
	"altfire" "detonate"	the alt fire button sets off its projectiles
	"altFireRate" S			seconds after that
	"def_projectile" "<def>"	what it fires
	"numProjectiles" N		that many a shot
	"spread" D				degrees off aim, at most
A projectile with "hitscan" "1" is traced at once, to its "range".
A weapon is often an item too, Doom 3 style, with "inv_weapon" naming itself.

===============================================================================
*/

bgWeapon_t	bg_weapons[MAX_WEAPONS];
int			bg_numWeapons = 1;

typedef struct {
	const dict_t	*def;
	const char		*name;
	int				slot;
} bgWeaponSort_t;

static int QDECL BG_CompareWeapons( const void *a, const void *b ) {
	const bgWeaponSort_t *wa = a, *wb = b;

	if ( wa->slot != wb->slot ) {
		return wa->slot - wb->slot;
	}
	return Q_stricmp( wa->name, wb->name );
}

static int BG_DefMsec( const dict_t *dict, const char *key, const char *defaultSeconds ) {
	return (int)( DictGetFloat( dict, key, defaultSeconds ) * 1000.0f + 0.5f );
}

/*
================
BG_InitWeapons
================
*/
static void BG_InitWeapons( void ) {
	static bgWeaponSort_t sorted[MAX_WEAPONS];
	bgWeapon_t *w;
	const dict_t *dict;
	const char *projectile;
	int i, num;

	num = 0;
	for ( i = 0; i < EntityDef_Num(); i++ ) {
		dict = EntityDef_ForNum( i );
		if ( !DictFindKey( dict, "weapon_slot" ) ) {
			continue;
		}
		if ( num == MAX_WEAPONS - 1 ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: more than %d weapons, %s left out\n", MAX_WEAPONS - 1, EntityDef_Name( i ) );
			break;
		}
		sorted[num].def = dict;
		sorted[num].name = EntityDef_Name( i );
		sorted[num].slot = DictGetInt( dict, "weapon_slot", "0" );
		num++;
	}
	qsort( sorted, num, sizeof( sorted[0] ), BG_CompareWeapons );

	memset( bg_weapons, 0, sizeof( bg_weapons ) );
	bg_weapons[WP_NONE].name = "";
	for ( i = 0; i < num; i++ ) {
		w = &bg_weapons[i + 1];
		dict = sorted[i].def;
		w->def = dict;
		w->name = sorted[i].name;
		w->slot = sorted[i].slot;
		w->fireTime = BG_DefMsec( dict, "fireRate", "0.5" );
		if ( w->fireTime < 1 ) {
			w->fireTime = 1;
		}
		w->ammoRequired = DictGetInt( dict, "ammoRequired", "1" );
		w->maxAmmo = DictGetInt( dict, "maxAmmo", "200" );
		w->raiseTime = BG_DefMsec( dict, "raiseTime", "0.25" );
		w->lowerTime = BG_DefMsec( dict, "lowerTime", "0.2" );
		w->melee = DictGetBool( dict, "melee", "0" );
		w->burstCount = DictGetInt( dict, "burstCount", "1" );
		if ( w->burstCount < 1 ) {
			w->burstCount = 1;
		}
		w->burstTime = BG_DefMsec( dict, "burstRate", "0.1" );
		w->clipSize = DictGetInt( dict, "clipSize", "0" );
		w->reloadTime = BG_DefMsec( dict, "reloadRate", "1" );
		if ( DictGetString( dict, "altfire", "", &projectile ) && !Q_stricmp( projectile, "detonate" ) ) {
			w->altFire = ALTFIRE_DETONATE;
		} else if ( projectile[0] ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: weapon %s: unknown altfire \"%s\"\n", w->name, projectile );
		}
		w->altFireTime = BG_DefMsec( dict, "altFireRate", "0.5" );
		w->numProjectiles = DictGetInt( dict, "numProjectiles", "1" );
		w->spread = DictGetFloat( dict, "spread", "0" );
		if ( DictGetString( dict, "def_projectile", "", &projectile ) && projectile[0] ) {
			w->projectile = EntityDef_Find( projectile );
			if ( !w->projectile ) {
				Com_Printf( S_COLOR_YELLOW "WARNING: weapon %s: no projectile def %s\n", w->name, projectile );
			}
		}
		if ( w->projectile ) {
			w->hitscan = DictGetBool( w->projectile, "hitscan", "0" );
			w->range = DictGetFloat( w->projectile, "range", "8192" );
		}
	}
	bg_numWeapons = num + 1;
}

/*
================
BG_WeaponForName

The number of the weapon def, WP_NONE for none.
================
*/
int BG_WeaponForName( const char *name ) {
	int i;

	if ( !name || !name[0] ) {
		return WP_NONE;
	}
	for ( i = 1; i < bg_numWeapons; i++ ) {
		if ( !Q_stricmp( bg_weapons[i].name, name ) ) {
			return i;
		}
	}
	return WP_NONE;
}

/*
================
BG_HitscanDir

One shot of a hitscan weapon, at most spread degrees off forward. The game
and the cgame make the same shots from the same seed.
================
*/
void BG_HitscanDir( const vec3_t forward, const vec3_t right, const vec3_t up, float spread, int *seed, vec3_t dir ) {
	float r, u, s;

	if ( spread <= 0 ) {
		VectorCopy( forward, dir );
		return;
	}
	s = tan( DEG2RAD( spread ) );
	r = Q_crandom( seed ) * s;
	u = Q_crandom( seed ) * s;
	VectorMA( forward, r, right, dir );
	VectorMA( dir, u, up, dir );
	VectorNormalize( dir );
}

/*
===============================================================================

ITEMS

Items are entity defs with "spawnclass" "item":

	entityDef item_health_25 {
		"spawnclass"	"item"
		"inv_health"	"25"
		"model"			"models/e1/a1_hlth.md3"
		"snd_acquire"	"sounds/global/a_hpick.wav"
	}

What an item gives comes from Doom 3 style keys, naming things rather than
numbering them:
	"inv_health" N						health
	"inv_armor" N						armor
	"inv_weapon" "<weapon item>"		that weapon, with "inv_ammo" N
	"inv_ammo" N, "ammo_type" "<weapon item>"	ammo for that weapon
	"inv_powerup" "<powerup>"			that powerup (bg_powerupNames: "quad",
										"haste"...), for "time" seconds
	"inv_persistant" "<powerup>"		a persistant powerup
	"inv_holdable" "<holdable>"			that holdable (bg_holdableNames)
	"inv_item" 1						kept in the inventory, up to "inv_max"
	"scriptobject" "<object>"			what its script object's pickup() does
										(none of the keys above: g_items.c)
Looks: "model" (and "model2" to "model4"), "model_scale", "icon",
"snd_acquire", "inv_name", "sounds" (what else to precache). Physics in the game: "density", "mass",
"bouncyness", "friction", "nophysics".

The game and the cgame build the same list from the same files, the items
numbered by name.

===============================================================================
*/

static gitem_t	bg_defItems[MAX_ITEMS + 1];

/*
================
BG_ItemTag

The number of the powerup or holdable a key names, in names.
================
*/
static qboolean BG_ItemTag( const dict_t *dict, const char *key, const char **names, int numNames, int *tag ) {
	const char *name;
	int i;

	if ( !DictGetString( dict, key, "", &name ) || !name[0] ) {
		return qfalse;
	}
	for ( i = 1; i < numNames; i++ ) {
		if ( !Q_stricmp( names[i], name ) ) {
			*tag = i;
			return qtrue;
		}
	}
	Com_Printf( S_COLOR_YELLOW "WARNING: item %s: %s \"%s\" is unknown\n",
				DictGetStringReference( dict, "classname", "?" ), key, name );
	return qfalse;
}

/*
================
BG_WeaponTag

The number of the weapon a key names.
================
*/
static qboolean BG_WeaponTag( const dict_t *dict, const char *key, int *tag ) {
	const char *name;

	if ( !DictGetString( dict, key, "", &name ) || !name[0] ) {
		return qfalse;
	}
	*tag = BG_WeaponForName( name );
	if ( *tag == WP_NONE ) {
		Com_Printf( S_COLOR_YELLOW "WARNING: item %s: %s \"%s\" isn't a weapon\n",
					DictGetStringReference( dict, "classname", "?" ), key, name );
		return qfalse;
	}
	return qtrue;
}

static char *BG_DefString( const dict_t *dict, const char *key ) {
	const char *value;

	if ( !DictGetString( dict, key, "", &value ) || !value[0] ) {
		return NULL;
	}
	return (char *)value;
}

/*
================
BG_ItemInventoryFromDef

How an item is carried: the cells it takes ("inv_size w h", one by
default), how many a slot holds ("inv_stack"), its category ("inv_category",
by its kind when not given), whether it can be dropped ("inv_droppable"),
and an armor suit's share of the damage it takes ("armor_absorb").
================
*/
static void BG_ItemInventoryFromDef( const dict_t *dict, gitem_t *item ) {
	const char *value;
	int i, stack = 1;
	invCategory_t category = INVCAT_MISC;

	item->invSize[0] = item->invSize[1] = 1;
	if ( DictGetString( dict, "inv_size", "", &value ) && value[0] ) {
		sscanf( value, "%d %d", &item->invSize[0], &item->invSize[1] );
		item->invSize[0] = Com_Clamp( 1, ( 1 << INV_GRID_BITS ) - 1, item->invSize[0] );
		item->invSize[1] = Com_Clamp( 1, ( 1 << INV_GRID_BITS ) - 1, item->invSize[1] );
	}

	switch ( item->giType ) {
	case IT_WEAPON:		category = INVCAT_WEAPONS; break;
	case IT_AMMO:		category = INVCAT_AMMO; stack = 100; break;
	case IT_ARMOR:		category = INVCAT_ARMOR; break;
	case IT_HEALTH:		category = INVCAT_HEALTH; stack = 5; break;
	case IT_POWERUP:
	case IT_PERSISTANT_POWERUP:
	case IT_HOLDABLE:	category = INVCAT_ARTIFACTS; stack = 3; break;
	case IT_INVENTORY:	category = INVCAT_QUEST; stack = MAX( 1, DictGetInt( dict, "inv_max", "1" ) ); break;
	default:			break;
	}
	if ( DictGetString( dict, "inv_category", "", &value ) && value[0] ) {
		for ( i = 0; i < INVCAT_NUM; i++ ) {
			if ( !Q_stricmp( value, bg_invCategoryNames[i] ) ) {
				category = i;
				break;
			}
		}
	}
	item->invCategory = category;
	item->invStack = MAX( 1, DictGetInt( dict, "inv_stack", va( "%d", stack ) ) );
	item->invDroppable = DictGetBool( dict, "inv_droppable", "1" );
	item->armorAbsorb = Com_Clamp( 0.0f, 1.0f, DictGetFloat( dict, "armor_absorb", "0.66" ) );
}

/*
================
BG_ItemFromDef
================
*/
static qboolean BG_ItemFromDef( const dict_t *dict, gitem_t *item ) {
	int i, tag = 0;

	memset( item, 0, sizeof( *item ) );
	item->def = dict;
	item->classname = BG_DefString( dict, "classname" );
	for ( i = 0; i < MAX_ITEM_MODELS; i++ ) {
		item->world_model[i] = BG_DefString( dict, i ? va( "model%d", i + 1 ) : "model" );
	}
	item->pickup_sound = BG_DefString( dict, "snd_acquire" );
	item->icon = BG_DefString( dict, "icon" );
	item->pickup_name = BG_DefString( dict, "inv_name" );
	item->sounds = BG_DefString( dict, "sounds" );
	if ( !item->sounds ) {
		item->sounds = "";
	}
	// Quake 3's code takes these for granted
	if ( !item->icon ) {
		item->icon = "";
	}
	if ( !item->pickup_name ) {
		item->pickup_name = item->classname;
	}
	if ( !item->world_model[0] ) {
		item->world_model[0] = "";
	}

	if ( DictFindKey( dict, "inv_health" ) ) {
		item->giType = IT_HEALTH;
		item->quantity = DictGetInt( dict, "inv_health", "0" );
	} else if ( DictFindKey( dict, "inv_armor" ) ) {
		item->giType = IT_ARMOR;
		item->quantity = DictGetInt( dict, "inv_armor", "0" );
	} else if ( BG_WeaponTag( dict, "inv_weapon", &tag ) ) {
		item->giType = IT_WEAPON;
		item->quantity = DictGetInt( dict, "inv_ammo", "0" );
	} else if ( BG_WeaponTag( dict, "ammo_type", &tag ) ) {
		item->giType = IT_AMMO;
		item->quantity = DictGetInt( dict, "inv_ammo", "0" );
	} else if ( BG_ItemTag( dict, "inv_powerup", bg_powerupNames, PW_NUM_POWERUPS, &tag ) ) {
		item->giType = IT_POWERUP;
		item->quantity = DictGetInt( dict, "time", "30" );
	} else if ( BG_ItemTag( dict, "inv_persistant", bg_powerupNames, PW_NUM_POWERUPS, &tag ) ) {
		item->giType = IT_PERSISTANT_POWERUP;
	} else if ( BG_ItemTag( dict, "inv_holdable", bg_holdableNames, HI_NUM_HOLDABLE, &tag ) ) {
		item->giType = IT_HOLDABLE;
	} else if ( DictFindKey( dict, "scriptobject" ) && !DictGetInt( dict, "inv_item", "0" ) ) {
		// what the item does is its script's
		item->giType = IT_SCRIPT;
	} else if ( DictGetInt( dict, "inv_item", "0" ) ) {
		// Doom 3's: into the inventory
		item->giType = IT_INVENTORY;
		item->quantity = DictGetInt( dict, "inv_count", "1" );
	} else {
		Com_Printf( S_COLOR_YELLOW "WARNING: item %s gives nothing, left out\n", item->classname );
		return qfalse;
	}
	item->giTag = tag;
	BG_ItemInventoryFromDef( dict, item );
	return qtrue;
}

static int QDECL BG_CompareItemClass( const void *a, const void *b ) {
	return Q_stricmp( ( (const gitem_t *)a )->classname, ( (const gitem_t *)b )->classname );
}

/*
================
BG_InitItems

Loads the entity defs and makes the item list from them.
================
*/
void BG_InitItems( void ) {
	const dict_t *dict;
	const char *spawnclass;
	int i, num;

	EntityDef_Clear();
	EntityDef_LoadAll();
	BG_InitSoundShaders();
	BG_InitWeapons();
	BG_InitModels();

	// item 0 is none
	memset( bg_defItems, 0, sizeof( bg_defItems ) );
	num = 1;
	for ( i = 0; i < EntityDef_Num(); i++ ) {
		dict = EntityDef_ForNum( i );
		if ( !DictGetString( dict, "spawnclass", "", &spawnclass ) || Q_stricmp( spawnclass, "item" ) ) {
			continue;
		}
		if ( num == MAX_ITEMS ) {
			Com_Printf( S_COLOR_YELLOW "WARNING: more than %d items, %s left out\n", MAX_ITEMS - 1, EntityDef_Name( i ) );
			break;
		}
		if ( BG_ItemFromDef( dict, &bg_defItems[num] ) ) {
			num++;
		}
	}
	qsort( bg_defItems + 1, num - 1, sizeof( bg_defItems[0] ), BG_CompareItemClass );

	bg_itemlist = bg_defItems;
	bg_numItems = num;

	Com_DPrintf( "%d entity defs, %d items, %d weapons\n", EntityDef_Num(), num - 1, bg_numWeapons - 1 );
}

/*
================
BG_ItemDef

The item's entity def, NULL for none.
================
*/
const dict_t *BG_ItemDef( const gitem_t *item ) {
	return item ? item->def : NULL;
}

/*
================
BG_ItemModelScale

How much bigger than its model an item is drawn, and collides in the game:
its def's "model_scale", 1 by default (Quake 3 drew weapons at 1.5). A map's
own model for the item ("model" key) is drawn as it is.
================
*/
float BG_ItemModelScale( const gitem_t *item, qboolean customModel ) {
	if ( customModel || !item ) {
		return 1.0f;
	}
	if ( item->def ) {
		return DictGetFloat( item->def, "model_scale", "1" );
	}
	return 1.0f;
}
