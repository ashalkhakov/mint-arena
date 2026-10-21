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

// bg_inventory.c -- the inventory grid, System Shock 2's: what a player
// carries are item stacks, each taking its item's cells ("inv_size") of a
// grid the size of the player's ("inv_grid"). The game packs it, players
// don't arrange it: an item goes in the first place it fits, and when there
// is none everything is packed again, biggest first. It lives in the
// playerState, so the client draws it and knows what fits.

#ifdef CGAME
#include "../cgame/cg_local.h"
#elif defined UI
#include "../ui/ui_local.h"
#else
#include "g_local.h"
#endif

#define INV_GRID_DEFAULT_W		10
#define INV_GRID_DEFAULT_H		6
#define INV_GRID_MAX			( 1 << INV_GRID_BITS )

const char *bg_invCategoryNames[INVCAT_NUM] = {
	"misc",
	"weapons",
	"ammo",
	"armor",
	"health",
	"artifacts",
	"keys",
	"quest"
};

// the slots worked on, a copy of the playerState's
typedef struct {
	int		width, height;
	int		item[MAX_INV_SLOTS];
	int		count[MAX_INV_SLOTS];
	int		cell[MAX_INV_SLOTS];
	int		value[MAX_INV_SLOTS];
} invGrid_t;

/*
================
BG_InvGridSize
================
*/
void BG_InvGridSize( const playerState_t *ps, int *width, int *height ) {
	int size = ps->stats[STAT_INV_GRID];

	*width = size & ( INV_GRID_MAX - 1 );
	*height = ( size >> INV_GRID_BITS ) & ( INV_GRID_MAX - 1 );
	if ( !*width || !*height ) {
		*width = INV_GRID_DEFAULT_W;
		*height = INV_GRID_DEFAULT_H;
	}
}

void BG_InvSetGridSize( playerState_t *ps, int width, int height ) {
	width = Com_Clamp( 1, INV_GRID_MAX - 1, width );
	height = Com_Clamp( 1, INV_GRID_MAX - 1, height );
	ps->stats[STAT_INV_GRID] = width | ( height << INV_GRID_BITS );
}

static void BG_InvItemSize( int itemNum, int *w, int *h ) {
	const gitem_t *item = BG_ItemForItemNum( itemNum );

	*w = item->invSize[0] > 0 ? item->invSize[0] : 1;
	*h = item->invSize[1] > 0 ? item->invSize[1] : 1;
}

static int BG_InvItemStack( int itemNum ) {
	const gitem_t *item = BG_ItemForItemNum( itemNum );

	return item->invStack > 0 ? item->invStack : 1;
}

/*
================
BG_InvSlotRect

Where a slot's stack is in the grid, in cells.
================
*/
void BG_InvSlotRect( const playerState_t *ps, int slot, int *x, int *y, int *w, int *h ) {
	*x = ps->invCell[slot] & ( INV_GRID_MAX - 1 );
	*y = ( ps->invCell[slot] >> INV_GRID_BITS ) & ( INV_GRID_MAX - 1 );
	BG_InvItemSize( ps->invItem[slot], w, h );
}

static void BG_InvRead( const playerState_t *ps, invGrid_t *grid ) {
	BG_InvGridSize( ps, &grid->width, &grid->height );
	memcpy( grid->item, ps->invItem, sizeof( grid->item ) );
	memcpy( grid->count, ps->invCount, sizeof( grid->count ) );
	memcpy( grid->cell, ps->invCell, sizeof( grid->cell ) );
	memcpy( grid->value, ps->invValue, sizeof( grid->value ) );
}

static void BG_InvWrite( const invGrid_t *grid, playerState_t *ps ) {
	memcpy( ps->invItem, grid->item, sizeof( grid->item ) );
	memcpy( ps->invCount, grid->count, sizeof( grid->count ) );
	memcpy( ps->invCell, grid->cell, sizeof( grid->cell ) );
	memcpy( ps->invValue, grid->value, sizeof( grid->value ) );
}

/*
================
BG_InvMark

Marks a slot's cells taken in a cell map, or tells whether they're free when
mark is qfalse.
================
*/
static qboolean BG_InvCellsFree( const qboolean taken[INV_GRID_MAX][INV_GRID_MAX], const invGrid_t *grid, int x, int y, int w, int h ) {
	int i, j;

	if ( x + w > grid->width || y + h > grid->height ) {
		return qfalse;
	}
	for ( j = y; j < y + h; j++ ) {
		for ( i = x; i < x + w; i++ ) {
			if ( taken[j][i] ) {
				return qfalse;
			}
		}
	}
	return qtrue;
}

static void BG_InvTakeCells( qboolean taken[INV_GRID_MAX][INV_GRID_MAX], int x, int y, int w, int h ) {
	int i, j;

	for ( j = y; j < y + h; j++ ) {
		for ( i = x; i < x + w; i++ ) {
			taken[j][i] = qtrue;
		}
	}
}

static void BG_InvTakenCells( const invGrid_t *grid, qboolean taken[INV_GRID_MAX][INV_GRID_MAX] ) {
	int slot, w, h;

	memset( taken, 0, sizeof( qboolean ) * INV_GRID_MAX * INV_GRID_MAX );
	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( !grid->item[slot] ) {
			continue;
		}
		BG_InvItemSize( grid->item[slot], &w, &h );
		BG_InvTakeCells( taken, grid->cell[slot] & ( INV_GRID_MAX - 1 ),
			( grid->cell[slot] >> INV_GRID_BITS ) & ( INV_GRID_MAX - 1 ), w, h );
	}
}

// the first place, row by row, a w by h item fits
static qboolean BG_InvFirstFit( const qboolean taken[INV_GRID_MAX][INV_GRID_MAX], const invGrid_t *grid, int w, int h, int *cell ) {
	int x, y;

	for ( y = 0; y + h <= grid->height; y++ ) {
		for ( x = 0; x + w <= grid->width; x++ ) {
			if ( BG_InvCellsFree( taken, grid, x, y, w, h ) ) {
				*cell = x | ( y << INV_GRID_BITS );
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
================
BG_InvPack

Packs every stack again, the biggest first (by cells, then height), each in
the first place it fits. Leaves the grid as it was when they don't all fit.
================
*/
static qboolean BG_InvPack( invGrid_t *grid ) {
	qboolean taken[INV_GRID_MAX][INV_GRID_MAX];
	int order[MAX_INV_SLOTS], cells[MAX_INV_SLOTS];
	int num = 0, i, j, slot, w, h, wi, hi, wj, hj;

	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( grid->item[slot] ) {
			order[num++] = slot;
		}
	}
	// insertion sort, biggest first; slots keep their order among equals
	for ( i = 1; i < num; i++ ) {
		slot = order[i];
		BG_InvItemSize( grid->item[slot], &wi, &hi );
		for ( j = i - 1; j >= 0; j-- ) {
			BG_InvItemSize( grid->item[order[j]], &wj, &hj );
			if ( wj * hj > wi * hi || ( wj * hj == wi * hi && hj >= hi ) ) {
				break;
			}
			order[j + 1] = order[j];
		}
		order[j + 1] = slot;
	}

	memset( taken, 0, sizeof( taken ) );
	for ( i = 0; i < num; i++ ) {
		BG_InvItemSize( grid->item[order[i]], &w, &h );
		if ( !BG_InvFirstFit( taken, grid, w, h, &cells[i] ) ) {
			return qfalse;
		}
		BG_InvTakeCells( taken, cells[i] & ( INV_GRID_MAX - 1 ), ( cells[i] >> INV_GRID_BITS ) & ( INV_GRID_MAX - 1 ), w, h );
	}
	for ( i = 0; i < num; i++ ) {
		grid->cell[order[i]] = cells[i];
	}
	return qtrue;
}

/*
================
BG_InvPlace

Puts a new stack in the grid: in the first free place, or with everything
packed again. qfalse when it doesn't fit.
================
*/
static qboolean BG_InvPlace( invGrid_t *grid, int itemNum, int count, int value ) {
	qboolean taken[INV_GRID_MAX][INV_GRID_MAX];
	int slot, w, h, cell;

	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( !grid->item[slot] ) {
			break;
		}
	}
	if ( slot == MAX_INV_SLOTS ) {
		return qfalse;
	}

	BG_InvItemSize( itemNum, &w, &h );
	BG_InvTakenCells( grid, taken );
	if ( !BG_InvFirstFit( taken, grid, w, h, &cell ) ) {
		invGrid_t packed = *grid;

		packed.item[slot] = itemNum;
		packed.count[slot] = count;
		packed.value[slot] = value;
		if ( !BG_InvPack( &packed ) ) {
			return qfalse;
		}
		*grid = packed;
		return qtrue;
	}

	grid->item[slot] = itemNum;
	grid->count[slot] = count;
	grid->cell[slot] = cell;
	grid->value[slot] = value;
	return qtrue;
}

/*
================
BG_InvAddToGrid

Adds what fits of count items: onto the stacks there are, then in new ones.
Returns how many went in.
================
*/
static int BG_InvAddToGrid( invGrid_t *grid, int itemNum, int count, int value ) {
	int stack, slot, added = 0, n;

	if ( itemNum <= 0 || itemNum >= bg_numItems || count <= 0 ) {
		return 0;
	}
	stack = BG_InvItemStack( itemNum );

	// stacks of it with room; one of a kind items (a stack of one) keep
	// their own value
	if ( stack > 1 ) {
		for ( slot = 0; slot < MAX_INV_SLOTS && added < count; slot++ ) {
			if ( grid->item[slot] != itemNum || grid->count[slot] >= stack ) {
				continue;
			}
			n = MIN( stack - grid->count[slot], count - added );
			grid->count[slot] += n;
			added += n;
		}
	}

	while ( added < count ) {
		n = MIN( stack, count - added );
		if ( !BG_InvPlace( grid, itemNum, n, value ) ) {
			break;
		}
		added += n;
	}
	return added;
}

/*
================
BG_InvRoomFor

How many of count items would fit.
================
*/
int BG_InvRoomFor( const playerState_t *ps, int itemNum, int count, int value ) {
	invGrid_t grid;

	BG_InvRead( ps, &grid );
	return BG_InvAddToGrid( &grid, itemNum, count, value );
}

/*
================
BG_InvAdd

Adds what fits of count items, returns how many went in.
================
*/
int BG_InvAdd( playerState_t *ps, int itemNum, int count, int value ) {
	invGrid_t grid;
	int added;

	BG_InvRead( ps, &grid );
	added = BG_InvAddToGrid( &grid, itemNum, count, value );
	if ( added ) {
		BG_InvWrite( &grid, ps );
	}
	return added;
}

/*
================
BG_InvRemoveSlot

Takes count from a slot's stack, emptying the slot when none are left. The
others stay where they are.
================
*/
void BG_InvRemoveSlot( playerState_t *ps, int slot, int count ) {
	if ( slot < 0 || slot >= MAX_INV_SLOTS || !ps->invItem[slot] ) {
		return;
	}
	ps->invCount[slot] -= count;
	if ( ps->invCount[slot] <= 0 ) {
		ps->invItem[slot] = 0;
		ps->invCount[slot] = 0;
		ps->invCell[slot] = 0;
		ps->invValue[slot] = 0;
	}
}

/*
================
BG_InvRemove

Takes count of an item, from its last stacks first. Returns how many there
were to take.
================
*/
int BG_InvRemove( playerState_t *ps, int itemNum, int count ) {
	int slot, n, removed = 0;

	for ( slot = MAX_INV_SLOTS - 1; slot >= 0 && removed < count; slot-- ) {
		if ( ps->invItem[slot] != itemNum ) {
			continue;
		}
		n = MIN( ps->invCount[slot], count - removed );
		BG_InvRemoveSlot( ps, slot, n );
		removed += n;
	}
	return removed;
}

/*
================
BG_InvCount
================
*/
int BG_InvCount( const playerState_t *ps, int itemNum ) {
	int slot, count = 0;

	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( ps->invItem[slot] == itemNum ) {
			count += ps->invCount[slot];
		}
	}
	return count;
}

/*
================
BG_InvFindSlot

An item's first slot, -1 for none.
================
*/
int BG_InvFindSlot( const playerState_t *ps, int itemNum ) {
	int slot;

	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( ps->invItem[slot] == itemNum ) {
			return slot;
		}
	}
	return -1;
}

void BG_InvClear( playerState_t *ps ) {
	memset( ps->invItem, 0, sizeof( ps->invItem ) );
	memset( ps->invCount, 0, sizeof( ps->invCount ) );
	memset( ps->invCell, 0, sizeof( ps->invCell ) );
	memset( ps->invValue, 0, sizeof( ps->invValue ) );
}

/*
================
BG_InvRepack

Packs the grid again, biggest first. qfalse, and nothing moved, when it
doesn't all fit (a grid made smaller).
================
*/
qboolean BG_InvRepack( playerState_t *ps ) {
	invGrid_t grid;

	BG_InvRead( ps, &grid );
	if ( !BG_InvPack( &grid ) ) {
		return qfalse;
	}
	BG_InvWrite( &grid, ps );
	return qtrue;
}
