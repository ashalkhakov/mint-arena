/*
===========================================================================
Copyright (C) 2026 Spearmint Source Code contributors.

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

// g_inventory.c -- what players carry, in the inventory grid (bg_inventory.c):
// taking items into it, using, wearing and dropping them. Everything a
// player picks up goes in it, System Shock 2's way, and is used from it.
// Armor is worn, one suit at a time (STAT_ARMOR_ITEM), each suit keeping
// its own points.
//
// Weapons and ammo stay where the rest of the game looks for them, the
// STAT_WEAPONS bits and ps->ammo: G_InventorySync keeps them and the grid
// the same. A weapon with a pickup item is had when it is in the grid, and
// a weapon's ammo, when it has an ammo item, is that item's stacks.

#include "g_local.h"

#define	RESPAWN_ARMOR		25
#define	RESPAWN_HEALTH		35
#define	RESPAWN_AMMO		40
#define	RESPAWN_HOLDABLE	60
#define	RESPAWN_MEGAHEALTH	35
#define	RESPAWN_POWERUP		120

// each weapon's pickup and ammo items, 0 for none
static int	g_weaponItem[MAX_WEAPONS];
static int	g_ammoItem[MAX_WEAPONS];
static qboolean	g_weaponItemsFound;

static void G_InventoryFindWeaponItems( void ) {
	gitem_t *item;
	int w;

	if ( g_weaponItemsFound ) {
		return;
	}
	for ( w = 1; w < bg_numWeapons && w < MAX_WEAPONS; w++ ) {
		item = BG_FindItemForWeapon( w );
		g_weaponItem[w] = item ? BG_ItemNumForItem( item ) : 0;
		item = BG_FindAmmoItemForWeapon( w );
		g_ammoItem[w] = item ? BG_ItemNumForItem( item ) : 0;
	}
	g_weaponItemsFound = qtrue;
}

static qboolean G_InventorySlotValid( gentity_t *ent, int slot );
static void G_InventoryBeltSync( gentity_t *ent );

/*
================
G_InventorySpawn

A player placed in the world: the grid the "player" def gives ("inv_grid w
h"), kept from before in the campaign, emptied elsewhere.
================
*/
void G_InventorySpawn( gentity_t *ent, const int *savedItem, const int *savedCount,
						const int *savedCell, const int *savedValue ) {
	gplayer_t *player = ent->player;
	const dict_t *def = EntityDef_Find( "player" );
	const char *value;
	int w = 10, h = 6;

	if ( def && DictGetString( def, "inv_grid", "", &value ) && value[0] ) {
		sscanf( value, "%d %d", &w, &h );
	}
	BG_InvSetGridSize( &player->ps, w, h );

	if ( g_gametype.integer == GT_CAMPAIGN && savedItem ) {
		memcpy( player->ps.invItem, savedItem, sizeof( player->ps.invItem ) );
		memcpy( player->ps.invCount, savedCount, sizeof( player->ps.invCount ) );
		memcpy( player->ps.invCell, savedCell, sizeof( player->ps.invCell ) );
		memcpy( player->ps.invValue, savedValue, sizeof( player->ps.invValue ) );
	} else {
		BG_InvClear( &player->ps );
		memset( player->pers.invFlags, 0, sizeof( player->pers.invFlags ) );
		memset( player->pers.invSeen, 0, sizeof( player->pers.invSeen ) );
	}
	player->ps.stats[STAT_ARMOR_ITEM] = 0;
	player->ps.stats[STAT_ARMOR] = 0;
	player->ps.stats[STAT_FROB_ENTITY] = ENTITYNUM_NONE;
}

/*
================
G_InventorySync

Makes the grid and the weapons and ammo the rest of the game uses agree,
after whatever changed them: a weapon given (the start ones, "give", a
script) goes in the grid when there's room; one in the grid is had; ammo
fired or loaded comes off its stacks, and ammo given goes on them. Ammo with
no room is lost.
================
*/
void G_InventorySync( gentity_t *ent ) {
	playerState_t *ps;
	int w, have, added;

	if ( !ent->player ) {
		return;
	}
	ps = &ent->player->ps;
	G_InventoryFindWeaponItems();

	for ( w = 1; w < bg_numWeapons && w < MAX_WEAPONS; w++ ) {
		if ( g_weaponItem[w] ) {
			have = BG_InvCount( ps, g_weaponItem[w] );
			if ( BG_HasWeapon( ps, w ) && !have ) {
				BG_InvAdd( ps, g_weaponItem[w], 1, 0 );	// no room: had without being carried
			} else if ( !BG_HasWeapon( ps, w ) && have ) {
				BG_GiveWeapon( ps, w );
			}
		}

		if ( g_ammoItem[w] && ps->ammo[w] >= 0 ) {
			have = BG_InvCount( ps, g_ammoItem[w] );
			if ( ps->ammo[w] > have ) {
				added = BG_InvAdd( ps, g_ammoItem[w], ps->ammo[w] - have, 0 );
				ps->ammo[w] = have + added;
			} else if ( ps->ammo[w] < have ) {
				BG_InvRemove( ps, g_ammoItem[w], have - ps->ammo[w] );
			}
		}
	}

	G_InventoryBeltSync( ent );
}

/*
================
G_InventoryBeltSync

The quick-use belt, as Deus Ex's: an item that leaves the inventory leaves
the belt; one newly carried that can be used goes on it, a weapon at its
"weapon_slot" key when that's free, anything else at the first free key.
================
*/
static void G_InventoryBeltSync( gentity_t *ent ) {
	playerState_t *ps = &ent->player->ps;
	int *seen = ent->player->pers.invSeen;
	int carried[MAX_ITEMS / 32];
	int i, n, slot, key;
	gitem_t *item;

	memset( carried, 0, sizeof( carried ) );
	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		n = ps->invItem[slot];
		if ( n > 0 && n < MAX_ITEMS ) {
			carried[n >> 5] |= 1 << ( n & 31 );
		}
	}

	for ( i = 0; i < MAX_BELT_SLOTS; i++ ) {
		n = ps->invBelt[i];
		if ( n <= 0 || n >= MAX_ITEMS || !( carried[n >> 5] & ( 1 << ( n & 31 ) ) ) ) {
			ps->invBelt[i] = 0;
		}
	}

	for ( n = 1; n < bg_numItems && n < MAX_ITEMS; n++ ) {
		if ( !( carried[n >> 5] & ( 1 << ( n & 31 ) ) ) || ( seen[n >> 5] & ( 1 << ( n & 31 ) ) ) ) {
			continue;
		}
		item = BG_ItemForItemNum( n );
		if ( item->invCategory != INVCAT_WEAPONS && item->invCategory != INVCAT_HEALTH
			&& item->invCategory != INVCAT_ARTIFACTS ) {
			continue;
		}
		for ( i = 0; i < MAX_BELT_SLOTS && ps->invBelt[i] != n; i++ ) {
		}
		if ( i < MAX_BELT_SLOTS ) {
			continue;		// on it already
		}
		key = -1;
		if ( item->giType == IT_WEAPON && BG_ValidWeapon( item->giTag ) ) {
			i = ( BG_Weapon( item->giTag )->slot + MAX_BELT_SLOTS - 1 ) % MAX_BELT_SLOTS;
			if ( BG_Weapon( item->giTag )->slot > 0 && !ps->invBelt[i] ) {
				key = i;
			}
		}
		for ( i = 0; key < 0 && i < MAX_BELT_SLOTS; i++ ) {
			if ( !ps->invBelt[i] ) {
				key = i;
			}
		}
		if ( key >= 0 ) {
			ps->invBelt[key] = n;
		}
	}

	memcpy( seen, carried, sizeof( carried ) );
}

/*
================
G_InventoryBelt

Puts an inventory slot's item on a key of the belt (0 for key 1 ... 9 for
key 0), taking it off any other; a slot below 0 empties the key.
================
*/
static void G_InventoryBelt( gentity_t *ent, int key, int slot ) {
	playerState_t *ps = &ent->player->ps;
	int i;

	if ( key < 0 || key >= MAX_BELT_SLOTS ) {
		return;
	}
	if ( slot < 0 ) {
		ps->invBelt[key] = 0;
		return;
	}
	if ( !G_InventorySlotValid( ent, slot ) ) {
		return;
	}
	for ( i = 0; i < MAX_BELT_SLOTS; i++ ) {
		if ( ps->invBelt[i] == ps->invItem[slot] ) {
			ps->invBelt[i] = 0;
		}
	}
	ps->invBelt[key] = ps->invItem[slot];
}

/*
================
G_InventoryRoomFor

How many of an item there's room for.
================
*/
static int G_InventoryRoomFor( gentity_t *other, gitem_t *item, int count, int value ) {
	return BG_InvRoomFor( &other->player->ps, BG_ItemNumForItem( item ), count, value );
}

/*
================
G_InventoryTakeItem

Takes a world item into a player's inventory, the way The Dark Mod's
CInventory::PutItem does. Returns its respawn seconds, like the Pickup_*
functions; 0 when there was no room (nothing taken), or INV_TAKEN_SOME when
only some of a stack was taken and the rest stays.
================
*/
int G_InventoryTakeItem( gentity_t *ent, gentity_t *other ) {
	gitem_t	*item = ent->item;
	playerState_t *ps = &other->player->ps;
	int itemNum = BG_ItemNumForItem( item );
	int count, room, value, w;

	G_InventoryFindWeaponItems();
	count = ent->count > 0 ? ent->count : ( item->quantity > 0 ? item->quantity : 1 );

	switch ( item->giType ) {
	case IT_WEAPON:
		w = item->giTag;
		if ( !BG_HasWeapon( ps, w ) ) {
			// the weapon, loaded, and its ammo where there's room
			if ( G_InventoryRoomFor( other, item, 1, 0 ) < 1 ) {
				return 0;
			}
			BG_InvAdd( ps, itemNum, 1, 0 );
			BG_GiveWeapon( ps, w );
			BG_FillClip( ps, w );
		} else if ( !g_ammoItem[w] || G_InventoryRoomFor( other, BG_ItemForItemNum( g_ammoItem[w] ), 1, 0 ) < 1 ) {
			return 0;		// had, and no room for its ammo
		}
		if ( ent->count >= 0 && ps->ammo[w] >= 0 ) {
			ps->ammo[w] = MIN( ps->ammo[w] + ( ent->count > 0 ? ent->count : item->quantity ), BG_Weapon( w )->maxAmmo );
		}
		G_InventorySync( other );
		return g_gametype.integer == GT_TEAM ? g_weaponTeamRespawn.integer : g_weaponRespawn.integer;

	case IT_AMMO:
		w = item->giTag;
		if ( ps->ammo[w] < 0 ) {
			return 0;		// infinite
		}
		room = MIN( count, BG_Weapon( w )->maxAmmo - ps->ammo[w] );
		room = MIN( room, G_InventoryRoomFor( other, item, room, 0 ) );
		if ( room <= 0 ) {
			return 0;
		}
		ps->ammo[w] += room;
		G_InventorySync( other );
		if ( room < count ) {
			ent->count = count - room;
			return INV_TAKEN_SOME;
		}
		return RESPAWN_AMMO;

	case IT_ARMOR:
		// a suit, with the points it has left (a dropped one's) or a fresh one's
		value = ent->invValue > 0 ? ent->invValue : item->quantity;
		if ( !BG_InvAdd( ps, itemNum, 1, value ) ) {
			return 0;
		}
		return RESPAWN_ARMOR;

	case IT_HEALTH:
	case IT_POWERUP:
	case IT_PERSISTANT_POWERUP:
	case IT_HOLDABLE:
		// one, keeping a map's or a dropped one's own amount
		if ( !BG_InvAdd( ps, itemNum, 1, ent->invValue > 0 ? ent->invValue : MAX( ent->count, 0 ) ) ) {
			return 0;
		}
		if ( item->giType == IT_HEALTH ) {
			return item->quantity == 100 ? RESPAWN_MEGAHEALTH : RESPAWN_HEALTH;
		}
		return item->giType == IT_HOLDABLE ? RESPAWN_HOLDABLE : RESPAWN_POWERUP;

	case IT_INVENTORY:
		// Doom 3's inventory items (keys, quest items, save gems), as many
		// as one carries ("inv_max"), with Daikatana's key flags
		{
			int max = item->def ? DictGetInt( item->def, "inv_max", "0" ) : 0;

			if ( max > 0 && BG_InvCount( ps, itemNum ) + count > max ) {
				return 0;
			}
			if ( G_InventoryRoomFor( other, item, count, 0 ) < count ) {
				return 0;
			}
			G_InventoryAdd( other, item->classname, count, ent->spawnflags & 7 );
		}
		return -1;

	default:
		return 0;
	}
}

/*
================
G_InventoryRefuse

Tells a player there's no room for an item, a sound and a centre print,
not more often than every second.
================
*/
void G_InventoryRefuse( gentity_t *other, gitem_t *item ) {
	if ( level.time < other->player->invRefuseTime ) {
		return;
	}
	other->player->invRefuseTime = level.time + 1000;
	trap_SendServerCommand( other - g_entities, va( "cp \"No room for %s\"", item->pickup_name ) );
	G_AddEvent( other, EV_INVENTORY_FULL, BG_ItemNumForItem( item ) );
}

/*
===============================================================================

	Using, wearing and dropping

===============================================================================
*/

static qboolean G_InventorySlotValid( gentity_t *ent, int slot ) {
	return ent->player && slot >= 0 && slot < MAX_INV_SLOTS && ent->player->ps.invItem[slot];
}

/*
================
G_InventoryWear

Puts on the suit in a slot; the one worn goes back in the grid with its
points. Refused when the grid has no room for it.
================
*/
qboolean G_InventoryWear( gentity_t *ent, int slot ) {
	playerState_t *ps = &ent->player->ps;
	int itemNum, value, worn, wornValue;

	if ( !G_InventorySlotValid( ent, slot ) || BG_ItemForItemNum( ps->invItem[slot] )->giType != IT_ARMOR ) {
		return qfalse;
	}
	itemNum = ps->invItem[slot];
	value = ps->invValue[slot];
	worn = ps->stats[STAT_ARMOR_ITEM];
	wornValue = ps->stats[STAT_ARMOR];

	BG_InvRemoveSlot( ps, slot, 1 );
	if ( worn && wornValue > 0 && !BG_InvAdd( ps, worn, 1, wornValue ) ) {
		BG_InvAdd( ps, itemNum, 1, value );		// as it was
		trap_SendServerCommand( ent - g_entities, va( "cp \"No room to take off %s\"", BG_ItemForItemNum( worn )->pickup_name ) );
		return qfalse;
	}
	ps->stats[STAT_ARMOR_ITEM] = itemNum;
	ps->stats[STAT_ARMOR] = value;
	G_AddEvent( ent, EV_ITEM_PICKUP, itemNum );
	return qtrue;
}

/*
================
G_InventoryTakeOff

The worn suit goes back in the grid.
================
*/
qboolean G_InventoryTakeOff( gentity_t *ent ) {
	playerState_t *ps = &ent->player->ps;
	int worn = ps->stats[STAT_ARMOR_ITEM];

	if ( !worn ) {
		return qfalse;
	}
	if ( ps->stats[STAT_ARMOR] > 0 && !BG_InvAdd( ps, worn, 1, ps->stats[STAT_ARMOR] ) ) {
		trap_SendServerCommand( ent - g_entities, va( "cp \"No room for %s\"", BG_ItemForItemNum( worn )->pickup_name ) );
		return qfalse;
	}
	ps->stats[STAT_ARMOR_ITEM] = 0;
	ps->stats[STAT_ARMOR] = 0;
	return qtrue;
}

/*
================
G_InventoryApply

What using one of an item does: health, a powerup, a holdable's effect.
value is that one's own amount (0: the item's). qfalse when it does nothing
now (full health).
================
*/
static qboolean G_InventoryApply( gentity_t *ent, gitem_t *item, int value ) {
	gplayer_t *player = ent->player;
	int max, quantity = value > 0 ? value : item->quantity;

	switch ( item->giType ) {
	case IT_HEALTH:
		// the big ones go over the max
		max = player->ps.stats[STAT_MAX_HEALTH] * ( item->quantity == 100 || item->quantity == 5 ? 2 : 1 );
		if ( ent->health >= max ) {
			return qfalse;
		}
		ent->health = MIN( ent->health + quantity, max );
		player->ps.stats[STAT_HEALTH] = ent->health;
		return qtrue;

	case IT_POWERUP:
		if ( !player->ps.powerups[item->giTag] ) {
			player->ps.powerups[item->giTag] = level.time - ( level.time % 1000 );
		}
		player->ps.powerups[item->giTag] += quantity * 1000;
		return qtrue;

	case IT_HOLDABLE:
		switch ( item->giTag ) {
		case HI_MEDKIT:
			if ( ent->health >= player->ps.stats[STAT_MAX_HEALTH] + 25 ) {
				return qfalse;
			}
			ent->health = player->ps.stats[STAT_HEALTH] = player->ps.stats[STAT_MAX_HEALTH] + 25;
			return qtrue;
		case HI_TELEPORTER:
			{
				vec3_t origin, angles;

				SelectSpawnPoint( player->ps.origin, origin, angles, qfalse );
				TeleportPlayer( ent, origin, angles );
			}
			return qtrue;
		default:
			return qfalse;
		}

	default:
		return qfalse;
	}
}

/*
================
G_InventoryUse

Uses one of a slot's stack, by its kind: drinks, eats or turns on what
gives something, wears armor, takes a weapon in hand. Other items run the
script function their def's "inv_use" names (The Dark Mod's inventoryUse),
with the player as activator, or do nothing.
================
*/
qboolean G_InventoryUse( gentity_t *ent, int slot ) {
	playerState_t *ps;
	gitem_t *item;

	if ( !G_InventorySlotValid( ent, slot ) || ent->health <= 0 ) {
		return qfalse;
	}
	ps = &ent->player->ps;
	item = BG_ItemForItemNum( ps->invItem[slot] );

	switch ( item->giType ) {
	case IT_ARMOR:
		return G_InventoryWear( ent, slot );

	case IT_WEAPON:
		if ( !BG_HasWeapon( ps, item->giTag ) ) {
			return qfalse;
		}
		// the client chooses the weapon held (cg_servercmds.c)
		trap_SendServerCommand( ent - g_entities, va( "invweapon %d", item->giTag ) );
		return qtrue;

	case IT_HEALTH:
	case IT_POWERUP:
	case IT_HOLDABLE:
		if ( !G_InventoryApply( ent, item, ps->invValue[slot] ) ) {
			trap_SendServerCommand( ent - g_entities, va( "cp \"You don't need %s now\"", item->pickup_name ) );
			return qfalse;
		}
		BG_InvRemoveSlot( ps, slot, 1 );
		G_AddEvent( ent, EV_ITEM_PICKUP, BG_ItemNumForItem( item ) );
		return qtrue;

	default:
		{
			const char *func;

			if ( item->def && DictGetString( item->def, "inv_use", "", &func ) && func[0] ) {
				G_ScriptCallFunction( func, ent );
				return qtrue;
			}
		}
		trap_SendServerCommand( ent - g_entities, va( "cp \"%s can't be used like that\"", item->pickup_name ) );
		return qfalse;
	}
}

/*
================
G_InventoryDropItem

Puts count of an item in the world in front of the player, tossed from the
eye where nothing's in the way (The Dark Mod's drop test, simplified), with
its own state: a suit's points, a stack's count.
================
*/
static gentity_t *G_InventoryDropItem( gentity_t *ent, gitem_t *item, int count, int value ) {
	vec3_t forward, start, end, velocity, angles;
	vec3_t mins = { -8, -8, -8 }, maxs = { 8, 8, 8 };
	trace_t tr;
	gentity_t *dropped;

	VectorCopy( ent->player->ps.viewangles, angles );
	angles[PITCH] = Com_Clamp( -30, 30, angles[PITCH] );
	AngleVectors( angles, forward, NULL, NULL );
	VectorCopy( ent->player->ps.origin, start );
	start[2] += ent->player->ps.viewheight;
	VectorMA( start, 32, forward, end );
	G_Trace( &tr, start, mins, maxs, end, ent->s.number, MASK_SOLID, TT_AABB );
	VectorScale( forward, 120, velocity );
	velocity[2] += 80;

	dropped = LaunchItem( item, tr.endpos, velocity );
	dropped->count = item->giType == IT_AMMO ? count : 0;
	dropped->invValue = value;
	// one's own things stay until taken, in the campaign
	if ( g_gametype.integer == GT_CAMPAIGN ) {
		dropped->think = NULL;
		dropped->nextthink = 0;
	}
	return dropped;
}

/*
================
G_InventoryDrop

Drops count of a slot's stack (all of it for 0). Dropping a weapon drops
its loaded rounds with it, and the player takes another in hand.
================
*/
qboolean G_InventoryDrop( gentity_t *ent, int slot, int count ) {
	playerState_t *ps;
	gitem_t *item;
	int w;

	if ( !G_InventorySlotValid( ent, slot ) || ent->health <= 0 ) {
		return qfalse;
	}
	ps = &ent->player->ps;
	item = BG_ItemForItemNum( ps->invItem[slot] );
	if ( !item->invDroppable ) {
		trap_SendServerCommand( ent - g_entities, va( "cp \"You can't drop %s\"", item->pickup_name ) );
		return qfalse;
	}
	if ( count <= 0 || count > ps->invCount[slot] ) {
		count = ps->invCount[slot];
	}

	switch ( item->giType ) {
	case IT_WEAPON:
		w = item->giTag;
		G_InventoryDropItem( ent, item, 0, 0 )->count = -1;	// no ammo comes with it
		BG_InvRemoveSlot( ps, slot, 1 );
		BG_TakeWeapon( ps, w );
		ps->ammoClip[w] = 0;
		if ( ps->weapon == w ) {
			trap_SendServerCommand( ent - g_entities, "invweapon -1" );	// the best one left
		}
		break;

	case IT_AMMO:
		G_InventoryDropItem( ent, item, count, 0 );
		BG_InvRemoveSlot( ps, slot, count );
		ps->ammo[item->giTag] = BG_InvCount( ps, BG_ItemNumForItem( item ) );
		break;

	default:
		for ( w = 0; w < count; w++ ) {
			G_InventoryDropItem( ent, item, 1, ps->invValue[slot] );
		}
		BG_InvRemoveSlot( ps, slot, count );
		break;
	}
	return qtrue;
}

/*
================
G_InventoryDropWorn

Takes off the suit worn and drops it, with its points.
================
*/
qboolean G_InventoryDropWorn( gentity_t *ent ) {
	playerState_t *ps = &ent->player->ps;

	if ( !ps->stats[STAT_ARMOR_ITEM] || ent->health <= 0 ) {
		return qfalse;
	}
	if ( ps->stats[STAT_ARMOR] > 0 ) {
		G_InventoryDropItem( ent, BG_ItemForItemNum( ps->stats[STAT_ARMOR_ITEM] ), 1, ps->stats[STAT_ARMOR] );
	}
	ps->stats[STAT_ARMOR_ITEM] = 0;
	ps->stats[STAT_ARMOR] = 0;
	return qtrue;
}

/*
================
G_InventoryBestOf

The slot of a category's item best used now: the biggest health when
hurt, the suit with the most points... -1 for none.
================
*/
static int G_InventoryBestOf( gentity_t *ent, int category ) {
	playerState_t *ps = &ent->player->ps;
	gitem_t *item;
	int slot, best = -1, bestRating = 0, rating;

	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( !ps->invItem[slot] ) {
			continue;
		}
		item = BG_ItemForItemNum( ps->invItem[slot] );
		if ( item->invCategory != category ) {
			continue;
		}
		rating = item->giType == IT_ARMOR ? ps->invValue[slot] : ( ps->invValue[slot] > 0 ? ps->invValue[slot] : item->quantity );
		// health: the smallest that heals what's missing, else the biggest
		if ( item->giType == IT_HEALTH ) {
			int missing = ps->stats[STAT_MAX_HEALTH] - ent->health;

			rating = rating >= missing ? 1000 - rating : rating;
		}
		if ( best < 0 || rating > bestRating ) {
			best = slot;
			bestRating = rating;
		}
	}
	return best;
}

/*
================
G_InventoryBotThink

Bots don't open the inventory: they wear the best suit when theirs is gone,
use health when hurt, and turn on artifacts at once.
================
*/
void G_InventoryBotThink( gentity_t *ent ) {
	playerState_t *ps = &ent->player->ps;
	int slot;

	if ( ent->health <= 0 || level.time < ent->player->invBotTime ) {
		return;
	}
	ent->player->invBotTime = level.time + 500;

	if ( !ps->stats[STAT_ARMOR_ITEM] && ( slot = G_InventoryBestOf( ent, INVCAT_ARMOR ) ) >= 0 ) {
		G_InventoryWear( ent, slot );
	}
	if ( ent->health < ps->stats[STAT_MAX_HEALTH] * 0.6f && ( slot = G_InventoryBestOf( ent, INVCAT_HEALTH ) ) >= 0 ) {
		G_InventoryUse( ent, slot );
	}
	if ( ( slot = G_InventoryBestOf( ent, INVCAT_ARTIFACTS ) ) >= 0 ) {
		G_InventoryUse( ent, slot );
	}
}

/*
===============================================================================

	Commands

===============================================================================
*/

// a slot by its number, or the first of an item by its def name
static int G_InventorySlotArg( gentity_t *ent, int arg ) {
	char buf[MAX_QPATH];
	gitem_t *item;

	trap_Argv( arg, buf, sizeof( buf ) );
	if ( !buf[0] ) {
		return -1;
	}
	if ( buf[0] >= '0' && buf[0] <= '9' ) {
		return atoi( buf );
	}
	item = BG_FindItemByClassname( buf );
	return item ? BG_InvFindSlot( &ent->player->ps, BG_ItemNumForItem( item ) ) : -1;
}

/*
================
Cmd_Inventory_f

The inventory's commands, for the inventory screen and binds:
  invuse <slot>, invwear <slot>, invremove, invdrop <slot> [count],
  (a slot by its number or by an item's def name)
  invdropworn, invquick <category>, invlist,
  invbelt <key> <slot>, invunbelt <key> (keys 1 to 9 and 0)
================
*/
void Cmd_Inventory_f( gentity_t *ent, const char *cmd ) {
	playerState_t *ps = &ent->player->ps;
	int slot, x, y, w, h, i;
	char buf[MAX_TOKEN_CHARS];

	if ( !Q_stricmp( cmd, "invuse" ) ) {
		G_InventoryUse( ent, G_InventorySlotArg( ent, 1 ) );
	} else if ( !Q_stricmp( cmd, "invwear" ) ) {
		G_InventoryWear( ent, G_InventorySlotArg( ent, 1 ) );
	} else if ( !Q_stricmp( cmd, "invremove" ) ) {
		G_InventoryTakeOff( ent );
	} else if ( !Q_stricmp( cmd, "invdrop" ) ) {
		trap_Argv( 2, buf, sizeof( buf ) );
		G_InventoryDrop( ent, G_InventorySlotArg( ent, 1 ), atoi( buf ) );
	} else if ( !Q_stricmp( cmd, "invbelt" ) ) {
		trap_Argv( 1, buf, sizeof( buf ) );
		G_InventoryBelt( ent, ( atoi( buf ) + MAX_BELT_SLOTS - 1 ) % MAX_BELT_SLOTS, G_InventorySlotArg( ent, 2 ) );
	} else if ( !Q_stricmp( cmd, "invunbelt" ) ) {
		trap_Argv( 1, buf, sizeof( buf ) );
		G_InventoryBelt( ent, ( atoi( buf ) + MAX_BELT_SLOTS - 1 ) % MAX_BELT_SLOTS, -1 );
	} else if ( !Q_stricmp( cmd, "invdropworn" ) ) {
		G_InventoryDropWorn( ent );
	} else if ( !Q_stricmp( cmd, "invquick" ) ) {
		trap_Argv( 1, buf, sizeof( buf ) );
		for ( i = 0; i < INVCAT_NUM; i++ ) {
			if ( !Q_stricmp( buf, bg_invCategoryNames[i] ) ) {
				break;
			}
		}
		if ( i == INVCAT_NUM ) {
			trap_SendServerCommand( ent - g_entities, "print \"usage: invquick <health|armor|artifacts|...>\n\"" );
			return;
		}
		slot = G_InventoryBestOf( ent, i );
		if ( slot < 0 ) {
			trap_SendServerCommand( ent - g_entities, va( "cp \"No %s\"", bg_invCategoryNames[i] ) );
			return;
		}
		G_InventoryUse( ent, slot );
	} else if ( !Q_stricmp( cmd, "invlist" ) ) {
		BG_InvGridSize( ps, &w, &h );
		G_Printf( "inventory of %s, %dx%d: worn %s %d\n", ent->player->pers.netname, w, h,
			ps->stats[STAT_ARMOR_ITEM] ? BG_ItemForItemNum( ps->stats[STAT_ARMOR_ITEM] )->classname : "none", ps->stats[STAT_ARMOR] );
		for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
			if ( !ps->invItem[slot] ) {
				continue;
			}
			BG_InvSlotRect( ps, slot, &x, &y, &w, &h );
			G_Printf( "  slot %d: %s x%d value %d at %d,%d size %dx%d\n", slot,
				BG_ItemForItemNum( ps->invItem[slot] )->classname, ps->invCount[slot], ps->invValue[slot], x, y, w, h );
		}
		for ( i = 0; i < MAX_BELT_SLOTS; i++ ) {
			if ( ps->invBelt[i] ) {
				G_Printf( "  belt %d: %s\n", ( i + 1 ) % MAX_BELT_SLOTS, BG_ItemForItemNum( ps->invBelt[i] )->classname );
			}
		}
	}
}
