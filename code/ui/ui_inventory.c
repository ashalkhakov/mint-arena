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

// ui_inventory.c -- the inventory screen's owner draws, flags and script
// commands, for a menu script (dk_inventory.menu): the grid of what the
// player carries (bg_inventory.c) with one item selected, what that item
// is, the armor worn, and buttons that send the game's inventory commands
// (g_inventory.c) for it. Doom 3's PDA and The Dark Mod's inventory grid
// feed their GUIs the same way. The game goes on while it is open, so what
// is used or dropped shows at once.

#include "ui_local.h"

#define DC	( &uiInfo.uiDC )		// the menus' display context

static rectDef_t	uiInvGridRect;		// where the grid was last drawn, for the mouse
static int			uiInvCell;			// its cells' size there
static float		uiInvGridX, uiInvGridY;

static void UI_InvSelectNext( int dir );

static const playerState_t *UI_InvPlayerState( void ) {
	return cg.snap ? &cg.snap->pss[0] : NULL;
}

int UI_InvSelected( void ) {
	const playerState_t *ps = UI_InvPlayerState();
	int slot = (int)trap_Cvar_VariableValue( "ui_invSel" );

	if ( !ps || slot < 0 || slot >= MAX_INV_SLOTS || !ps->invItem[slot] ) {
		return -1;
	}
	return slot;
}

static void UI_InvSelect( int slot ) {
	trap_Cvar_Set( "ui_invSel", va( "%d", slot ) );
}

static const gitem_t *UI_InvSelectedItem( void ) {
	int slot = UI_InvSelected();

	return slot < 0 ? NULL : BG_ItemForItemNum( UI_InvPlayerState()->invItem[slot] );
}

// an item's picture for the grid: "inv_icon_grid", "inv_icon", its icon
static qhandle_t UI_InvItemIcon( const gitem_t *item ) {
	const char *name = "";

	if ( item->def ) {
		DictGetString( item->def, "inv_icon_grid", "", &name );
		if ( !name[0] ) {
			DictGetString( item->def, "inv_icon", "", &name );
		}
	}
	if ( !name[0] ) {
		name = item->icon;
	}
	return name && name[0] ? DC->registerShaderNoMip( name ) : 0;
}

/*
================
UI_InvDrawModel

An item's world model in a box of the screen (640x480), turned to yaw and
framed whole: back far enough that what it shows of the model fills the
box. Swing turns it more without framing it again, a little either way for
the selected item. qfalse when it has no model.
================
*/
static qboolean UI_InvDrawModel( int itemNum, float x, float y, float w, float h, float yaw, float swing ) {
	const gitem_t *item = BG_ItemForItemNum( itemNum );
	refdef_t refdef;
	refEntity_t ent;
	vec3_t mins, maxs, bounds[2], angles, axis[3], p, lo, hi, mid;
	float scale, dist;
	int i, j;

	if ( !cg_items[itemNum].registered ) {
		CG_RegisterItemVisuals( itemNum );
	}
	if ( !cg_items[itemNum].models[0] ) {
		return qfalse;
	}

	memset( &refdef, 0, sizeof( refdef ) );
	refdef.rdflags = RDF_NOWORLDMODEL;
	AxisClear( refdef.viewaxis );
	DC->adjustFrom640( &x, &y, &w, &h );
	refdef.x = x;
	refdef.y = y;
	refdef.width = w;
	refdef.height = h;
	refdef.fov_x = 30;
	refdef.fov_y = RAD2DEG( 2 * atan( tan( DEG2RAD( refdef.fov_x ) / 2 ) * h / w ) );
	refdef.time = DC->realTime;

	memset( &ent, 0, sizeof( ent ) );
	ent.hModel = cg_items[itemNum].models[0];
	DC->modelBounds( ent.hModel, mins, maxs, 0, 0, 0 );
	scale = BG_ItemModelScale( item, qfalse );
	VectorScale( mins, scale, bounds[0] );
	VectorScale( maxs, scale, bounds[1] );
	VectorSet( angles, 15, yaw, 0 );
	AnglesToAxis( angles, axis );

	// the box's corners as seen: x away from the eye, y left, z up
	VectorSet( lo, 99999, 99999, 99999 );
	VectorSet( hi, -99999, -99999, -99999 );
	for ( i = 0; i < 8; i++ ) {
		vec3_t c;

		VectorSet( c, bounds[i & 1][0], bounds[( i >> 1 ) & 1][1], bounds[( i >> 2 ) & 1][2] );
		VectorClear( p );
		for ( j = 0; j < 3; j++ ) {
			VectorMA( p, c[j], axis[j], p );
		}
		AddPointToBounds( p, lo, hi );
	}
	if ( hi[1] <= lo[1] || hi[2] <= lo[2] ) {
		return qfalse;
	}
	VectorAdd( lo, hi, mid );
	VectorScale( mid, 0.5f, mid );
	dist = 1.15f * MAX( ( hi[1] - lo[1] ) / 2 / tan( DEG2RAD( refdef.fov_x ) / 2 ),
		( hi[2] - lo[2] ) / 2 / tan( DEG2RAD( refdef.fov_y ) / 2 ) ) + ( hi[0] - lo[0] ) / 2;

	// turned more about its middle, which stays where it was framed
	if ( swing ) {
		vec3_t local;

		VectorAdd( bounds[0], bounds[1], local );
		VectorScale( local, 0.5f, local );
		angles[YAW] += swing;
		AnglesToAxis( angles, axis );
		VectorClear( mid );
		for ( j = 0; j < 3; j++ ) {
			VectorMA( mid, local[j], axis[j], mid );
		}
	}
	for ( i = 0; i < 3; i++ ) {
		VectorScale( axis[i], scale, ent.axis[i] );
	}
	ent.nonNormalizedAxes = ( scale != 1.0f );
	VectorSet( ent.origin, dist - mid[0], -mid[1], -mid[2] );
	VectorCopy( ent.origin, ent.oldorigin );
	VectorCopy( ent.origin, ent.lightingOrigin );
	ent.renderfx = RF_LIGHTING_ORIGIN | RF_NOSHADOW;

	DC->clearScene();
	DC->addRefEntityToScene( &ent );
	DC->renderScene( &refdef );
	return qtrue;
}

/*
================
UI_InvDrawGrid

The grid, square cells as big as the rect allows, each a cell picture (the
item's background) or a dark square; on it each stack's picture over its
cells, its count, an armor suit's points; the selected one framed.
================
*/
static void UI_InvDrawGrid( rectDef_t *rect, float scale, vec4_t color, qhandle_t shader, int textStyle ) {
	const playerState_t *ps = UI_InvPlayerState();
	vec4_t cellColor = { 0, 0, 0, 0.45f }, lineColor = { 0.81f, 0.61f, 0.39f, 0.35f };
	vec4_t selColor = { 1, 0.85f, 0.4f, 1 }, hoverColor = { 1, 1, 1, 0.12f };
	int gw, gh, cell, slot, x, y, w, h, sel = UI_InvSelected();
	float x0, y0, rx, ry, rw, rh, side;
	const gitem_t *item;
	qhandle_t icon;
	char text[32];

	if ( !ps ) {
		return;
	}
	// something selected while there's anything (the selected stack used up)
	if ( sel < 0 ) {
		UI_InvSelectNext( 1 );
		sel = UI_InvSelected();
	}
	BG_InvGridSize( ps, &gw, &gh );
	cell = (int)MIN( rect->w / gw, rect->h / gh );
	x0 = rect->x + ( rect->w - cell * gw ) / 2;
	y0 = rect->y + ( rect->h - cell * gh ) / 2;
	uiInvGridRect = *rect;
	uiInvCell = cell;
	uiInvGridX = x0;
	uiInvGridY = y0;

	for ( y = 0; y < gh; y++ ) {
		for ( x = 0; x < gw; x++ ) {
			if ( shader ) {
				DC->drawHandlePic( x0 + x * cell, y0 + y * cell, cell, cell, shader );
			} else {
				DC->fillRect( x0 + x * cell + 1, y0 + y * cell + 1, cell - 2, cell - 2, cellColor );
			}
		}
	}
	DC->drawRect( x0, y0, cell * gw, cell * gh, 1, lineColor );

	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( !ps->invItem[slot] ) {
			continue;
		}
		item = BG_ItemForItemNum( ps->invItem[slot] );
		BG_InvSlotRect( ps, slot, &x, &y, &w, &h );
		rx = x0 + x * cell + 2;
		ry = y0 + y * cell + 2;
		rw = w * cell - 4;
		rh = h * cell - 4;

		// under the mouse
		if ( DC->cursorx >= rx && DC->cursorx < rx + rw && DC->cursory >= ry && DC->cursory < ry + rh ) {
			DC->fillRect( rx, ry, rw, rh, hoverColor );
		}

		// its world model, the selected one turning; else its picture
		if ( UI_InvDrawModel( ps->invItem[slot], rx, ry, rw, rh, rw > rh * 1.5f ? 80 : 210,
				slot == sel ? 25 * sin( DC->realTime * 0.002 ) : 0 ) ) {
		} else if ( ( icon = UI_InvItemIcon( item ) ) != 0 ) {
			side = MIN( rw, rh );
			DC->setColor( NULL );
			DC->drawHandlePic( rx + ( rw - side ) / 2, ry + ( rh - side ) / 2, side, side, icon );
		} else {
			DC->drawText( rx + 2, ry + 12, scale * 0.8f, color, item->pickup_name, 0, (int)( rw / 6 ), textStyle );
		}

		text[0] = '\0';
		if ( item->giType == IT_ARMOR ) {
			Com_sprintf( text, sizeof( text ), "%d", ps->invValue[slot] );
		} else if ( ps->invCount[slot] > 1 ) {
			Com_sprintf( text, sizeof( text ), "%d", ps->invCount[slot] );
		}
		if ( text[0] ) {
			DC->drawText( rx + rw - DC->textWidth( text, scale, 0 ) - 2, ry + rh - 2, scale, color, text, 0, 0, textStyle );
		}

		if ( slot == sel ) {
			DC->drawRect( rx - 1, ry - 1, rw + 2, rh + 2, 2, selColor );
		}
	}
}

// the slot whose stack covers a point of the screen, -1 for none
static int UI_InvSlotAt( float px, float py ) {
	const playerState_t *ps = UI_InvPlayerState();
	int slot, x, y, w, h, cx, cy;

	if ( !ps || uiInvCell <= 0 ) {
		return -1;
	}
	cx = (int)floor( ( px - uiInvGridX ) / uiInvCell );
	cy = (int)floor( ( py - uiInvGridY ) / uiInvCell );
	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( !ps->invItem[slot] ) {
			continue;
		}
		BG_InvSlotRect( ps, slot, &x, &y, &w, &h );
		if ( cx >= x && cx < x + w && cy >= y && cy < y + h ) {
			return slot;
		}
	}
	return -1;
}

// the next stack in reading order from the selected one, dir 1 or -1
static void UI_InvSelectNext( int dir ) {
	const playerState_t *ps = UI_InvPlayerState();
	int sel = UI_InvSelected(), slot, best = -1, bestKey = 0, key, selKey, x, y, w, h;

	if ( !ps ) {
		return;
	}
	selKey = -1;
	if ( sel >= 0 ) {
		BG_InvSlotRect( ps, sel, &x, &y, &w, &h );
		selKey = y * 256 + x;
	}
	for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
		if ( !ps->invItem[slot] || slot == sel ) {
			continue;
		}
		BG_InvSlotRect( ps, slot, &x, &y, &w, &h );
		key = y * 256 + x;
		if ( dir > 0 ? ( selKey >= 0 && key <= selKey ) : ( selKey >= 0 && key >= selKey ) ) {
			continue;
		}
		if ( best < 0 || ( dir > 0 ? key < bestKey : key > bestKey ) ) {
			best = slot;
			bestKey = key;
		}
	}
	if ( best >= 0 ) {
		UI_InvSelect( best );
	}
}

// text wrapped to a width, line by line down from y
static void UI_InvDrawWrapped( rectDef_t *rect, float scale, vec4_t color, const char *text, int textStyle ) {
	char line[256];
	const char *p = text, *wordEnd;
	int len = 0, lineHeight = DC->textHeight( "Ay", scale, 0 ) + 4;
	float y = rect->y + lineHeight;

	line[0] = '\0';
	while ( *p && y <= rect->y + rect->h ) {
		char candidate[256];

		while ( *p == ' ' ) {
			p++;
		}
		for ( wordEnd = p; *wordEnd && *wordEnd != ' ' && *wordEnd != '\n'; wordEnd++ ) {
		}
		Com_sprintf( candidate, sizeof( candidate ), "%s%s%.*s", line, len ? " " : "", (int)( wordEnd - p ), p );
		if ( len && DC->textWidth( candidate, scale, 0 ) > rect->w ) {
			DC->drawText( rect->x, y, scale, color, line, 0, 0, textStyle );
			y += lineHeight;
			line[0] = '\0';
			len = 0;
			continue;
		}
		Q_strncpyz( line, candidate, sizeof( line ) );
		len = strlen( line );
		p = wordEnd;
		if ( *p == '\n' ) {
			DC->drawText( rect->x, y, scale, color, line, 0, 0, textStyle );
			y += lineHeight;
			line[0] = '\0';
			len = 0;
			p++;
		}
	}
	if ( len && y <= rect->y + rect->h ) {
		DC->drawText( rect->x, y, scale, color, line, 0, 0, textStyle );
	}
}

/*
================
UI_InventoryOwnerDraw

The inventory screen's owner draws; qfalse for others.
================
*/
qboolean UI_InventoryOwnerDraw( int ownerDraw, rectDef_t *rect, float scale, vec4_t color, qhandle_t shader, int textStyle ) {
	const playerState_t *ps = UI_InvPlayerState();
	const gitem_t *item = UI_InvSelectedItem();
	int slot = UI_InvSelected(), gw, gh, used, x, y, w, h;
	const char *desc;
	char text[256];

	switch ( ownerDraw ) {
	case UI_INV_GRID:
		UI_InvDrawGrid( rect, scale, color, shader, textStyle );
		return qtrue;

	case UI_INV_SELNAME:
		if ( item ) {
			DC->drawText( rect->x, rect->y + rect->h, scale, color, item->pickup_name, 0, 0, textStyle );
		}
		return qtrue;

	case UI_INV_SELDESC:
		if ( item && item->def && DictGetString( item->def, "inv_desc", "", &desc ) && desc[0] ) {
			UI_InvDrawWrapped( rect, scale, color, desc, textStyle );
		}
		return qtrue;

	case UI_INV_SELINFO:
		if ( !item || !ps ) {
			return qtrue;
		}
		switch ( item->giType ) {
		case IT_ARMOR:
			Com_sprintf( text, sizeof( text ), "%d of %d, takes %d%%",
				ps->invValue[slot], item->quantity, (int)( item->armorAbsorb * 100 + 0.5f ) );
			break;
		case IT_WEAPON:
			Com_sprintf( text, sizeof( text ), "Weapon, ammo %d", ps->ammo[item->giTag] );
			break;
		case IT_HEALTH:
			Com_sprintf( text, sizeof( text ), "Heals %d, %d carried", ps->invValue[slot] > 0 ? ps->invValue[slot] : item->quantity,
				BG_InvCount( ps, ps->invItem[slot] ) );
			break;
		default:
			Com_sprintf( text, sizeof( text ), "%s, %d carried", bg_invCategoryNames[item->invCategory],
				BG_InvCount( ps, ps->invItem[slot] ) );
			break;
		}
		DC->drawText( rect->x, rect->y + rect->h, scale, color, text, 0, 0, textStyle );
		return qtrue;

	case UI_INV_WORN:
		if ( !ps ) {
			return qtrue;
		}
		if ( ps->stats[STAT_ARMOR_ITEM] > 0 && ps->stats[STAT_ARMOR_ITEM] < BG_NumItems() ) {
			const gitem_t *worn = BG_ItemForItemNum( ps->stats[STAT_ARMOR_ITEM] );
			qhandle_t icon = UI_InvItemIcon( worn );
			float side = rect->h;

			if ( !UI_InvDrawModel( ps->stats[STAT_ARMOR_ITEM], rect->x, rect->y, side, side, 210, 0 ) && icon ) {
				DC->setColor( NULL );
				DC->drawHandlePic( rect->x, rect->y, side, side, icon );
			}
			DC->drawText( rect->x + side + 8, rect->y + rect->h * 0.45f, scale, color, worn->pickup_name, 0, 0, textStyle );
			Com_sprintf( text, sizeof( text ), "%d of %d, takes %d%%", ps->stats[STAT_ARMOR], worn->quantity,
				(int)( worn->armorAbsorb * 100 + 0.5f ) );
			DC->drawText( rect->x + side + 8, rect->y + rect->h * 0.9f, scale, color, text, 0, 0, textStyle );
		} else {
			DC->drawText( rect->x, rect->y + rect->h * 0.6f, scale, color, "No armor worn", 0, 0, textStyle );
		}
		return qtrue;

	case UI_INV_ROOM:
		if ( !ps ) {
			return qtrue;
		}
		BG_InvGridSize( ps, &gw, &gh );
		used = 0;
		for ( slot = 0; slot < MAX_INV_SLOTS; slot++ ) {
			if ( ps->invItem[slot] ) {
				BG_InvSlotRect( ps, slot, &x, &y, &w, &h );
				used += w * h;
			}
		}
		Com_sprintf( text, sizeof( text ), "%d of %d cells used", used, gw * gh );
		DC->drawText( rect->x, rect->y + rect->h, scale, color, text, 0, 0, textStyle );
		return qtrue;

	default:
		return qfalse;
	}
}

/*
================
UI_InventoryVisible

The inventory screen's ownerdrawflags, to show its buttons when they apply
to the selected item: qtrue when all of flags' hold. flags keeps the
others.
================
*/
qboolean UI_InventoryVisible( int *flags ) {
	const playerState_t *ps = UI_InvPlayerState();
	const gitem_t *item = UI_InvSelectedItem();
	qboolean vis = qtrue;
	const char *use;

	if ( *flags & UI_SHOW_INV_SELECTED ) {
		vis &= item != NULL;
		*flags &= ~UI_SHOW_INV_SELECTED;
	}
	if ( *flags & UI_SHOW_INV_USABLE ) {
		vis &= item && ( item->giType == IT_HEALTH || item->giType == IT_POWERUP || item->giType == IT_HOLDABLE
			|| item->giType == IT_WEAPON || ( item->def && DictGetString( item->def, "inv_use", "", &use ) && use[0] ) );
		*flags &= ~UI_SHOW_INV_USABLE;
	}
	if ( *flags & UI_SHOW_INV_WEARABLE ) {
		vis &= item && item->giType == IT_ARMOR;
		*flags &= ~UI_SHOW_INV_WEARABLE;
	}
	if ( *flags & UI_SHOW_INV_DROPPABLE ) {
		vis &= item && item->invDroppable;
		*flags &= ~UI_SHOW_INV_DROPPABLE;
	}
	if ( *flags & UI_SHOW_INV_WORN ) {
		vis &= ps && ps->stats[STAT_ARMOR_ITEM] > 0;
		*flags &= ~UI_SHOW_INV_WORN;
	}
	return vis;
}

/*
================
UI_InventoryHandleKey

The grid: a click selects the stack under the mouse, a double click uses
it; the arrows move through the stacks, enter uses the selected one.
================
*/
qboolean UI_InventoryHandleKey( int ownerDraw, int key ) {
	static int lastClickTime, lastClickSlot = -1;
	int slot;

	if ( ownerDraw != UI_INV_GRID ) {
		return qfalse;
	}
	switch ( key ) {
	case K_MOUSE1:
		slot = UI_InvSlotAt( DC->cursorx, DC->cursory );
		if ( slot < 0 ) {
			return qfalse;
		}
		if ( slot == lastClickSlot && DC->realTime - lastClickTime < 350 ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "invuse %d\n", slot ) );
			lastClickSlot = -1;
		} else {
			UI_InvSelect( slot );
			lastClickSlot = slot;
			lastClickTime = DC->realTime;
		}
		return qtrue;
	case K_RIGHTARROW:
	case K_DOWNARROW:
		UI_InvSelectNext( 1 );
		return qtrue;
	case K_LEFTARROW:
	case K_UPARROW:
		UI_InvSelectNext( -1 );
		return qtrue;
	case K_ENTER:
	case K_KP_ENTER:
		slot = UI_InvSelected();
		if ( slot >= 0 ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "invuse %d\n", slot ) );
		}
		return qtrue;
	default:
		return qfalse;
	}
}

/*
================
UI_InventoryBeltKey

A key bound to "belt <key>" pressed on the screen puts the selected item on
that key of the belt, Deus Ex's way, or takes it off when it's there.
================
*/
qboolean UI_InventoryBeltKey( const char *binding ) {
	const playerState_t *ps = UI_InvPlayerState();
	int slot = UI_InvSelected();
	int key;

	if ( Q_stricmpn( binding, "belt ", 5 ) ) {
		return qfalse;
	}
	key = atoi( binding + 5 );
	if ( !ps || slot < 0 || key < 0 || key >= MAX_BELT_SLOTS ) {
		return qtrue;
	}
	if ( ps->invBelt[( key + MAX_BELT_SLOTS - 1 ) % MAX_BELT_SLOTS] == ps->invItem[slot] ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, va( "invunbelt %d\n", key ) );
	} else {
		trap_Cmd_ExecuteText( EXEC_APPEND, va( "invbelt %d %d\n", key, slot ) );
	}
	DC->startLocalSound( DC->registerSound( "sounds/menus/button_006.wav", qfalse ), CHAN_LOCAL_SOUND );
	return qtrue;
}

/*
================
UI_InventoryCommand

uiScript InvCommand <command>: the game's inventory command for the
selected stack (invuse, invwear, invdrop), or on its own (invremove,
invdropworn).
================
*/
void UI_InventoryCommand( const char *cmd ) {
	int slot = UI_InvSelected();

	if ( !Q_stricmp( cmd, "invremove" ) || !Q_stricmp( cmd, "invdropworn" ) ) {
		trap_Cmd_ExecuteText( EXEC_APPEND, va( "%s\n", cmd ) );
		return;
	}
	if ( slot < 0 ) {
		return;
	}
	trap_Cmd_ExecuteText( EXEC_APPEND, va( "%s %d\n", cmd, slot ) );
}

/*
================
UI_InventoryOpen

The inventory screen, without pausing the game, the first stack selected
when none is; closed when it's open.
================
*/
void UI_InventoryOpen( void ) {
	const playerState_t *ps = UI_InvPlayerState();
	menuDef_t *menu = Menus_FindByName( "inventory" );

	// open: closed again
	if ( menu && ( menu->window.flags & WINDOW_VISIBLE ) && ( Key_GetCatcher() & KEYCATCH_UI ) ) {
		UI_ForceMenuOff();
		return;
	}

	Key_SetCatcher( KEYCATCH_UI );
	Menus_CloseAll();
	Menus_ActivateByName( "inventory" );
	if ( ps && UI_InvSelected() < 0 ) {
		UI_InvSelect( -1 );
		UI_InvSelectNext( 1 );
	}
}
