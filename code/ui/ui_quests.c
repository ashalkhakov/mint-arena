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

// ui_quests.c -- the player's screen (Fallout 4's Pip-Boy, OpenJK's
// datapad): three tabs, a menu each over the same panel (dk_status.menu,
// dk_inventory.menu, dk_quests.menu), the tab bar switching between them
// (PdaTab); the status tab's owner draws (the character, health, armor,
// boosts, weapon, where) and the quests tab's (cg_quests.c's quests, each
// with its objectives, coloured by how they stand), its tab marked while
// something's new (ui_questsNew).

#include "ui_local.h"

#define DC	( &uiInfo.uiDC )

static const char *uiPdaMenus[] = { "dk_status", "inventory", "dk_quests" };
static const char *uiPdaTabs[] = { "character", "inventory", "quests" };		// (their commands: "status" and "stats" are taken)
#define UI_PDA_TABS	ARRAY_LEN( uiPdaMenus )

static const playerState_t *UI_PdaPlayerState( void ) {
	return cg.snap ? &cg.snap->pss[0] : NULL;
}

/*
===============================================================================

	the tabs

===============================================================================
*/

static int UI_PdaTabNum( const char *tab ) {
	int i;

	for ( i = 0; i < (int)UI_PDA_TABS; i++ ) {
		if ( !Q_stricmp( tab, uiPdaTabs[i] ) || !Q_stricmp( tab, uiPdaMenus[i] ) ) {
			return i;
		}
	}
	return -1;
}

/*
================
UI_PdaOpen

the player's screen on a tab ("character", "inventory", "quests"; "" the last
one), not pausing the game; its key again (the tab shown) closes it
================
*/
void UI_PdaOpen( const char *tab ) {
	menuDef_t *menu;
	int i, n = UI_PdaTabNum( tab );

	if ( n < 0 ) {
		n = (int)trap_Cvar_VariableValue( "ui_pdaTab" );
		if ( n < 0 || n >= (int)UI_PDA_TABS ) {
			n = 1;
		}
	}
	// open on that tab: closed again
	menu = Menus_FindByName( uiPdaMenus[n] );
	if ( menu && ( menu->window.flags & WINDOW_VISIBLE ) && ( Key_GetCatcher() & KEYCATCH_UI ) ) {
		UI_ForceMenuOff();
		return;
	}
	// another tab open: that one instead
	for ( i = 0; i < (int)UI_PDA_TABS; i++ ) {
		menu = Menus_FindByName( uiPdaMenus[i] );
		if ( menu && ( menu->window.flags & WINDOW_VISIBLE ) ) {
			Menus_CloseByName( uiPdaMenus[i] );
		}
	}
	if ( n == 1 ) {
		UI_InventoryOpen();		// (its selection made)
	} else {
		Key_SetCatcher( KEYCATCH_UI );
		Menus_CloseAll();
		Menus_ActivateByName( uiPdaMenus[n] );
	}
	trap_Cvar_Set( "ui_pdaTab", va( "%d", n ) );
	if ( n == 2 ) {
		trap_Cvar_Set( "ui_questsNew", "0" );		// looked at
	}
}

/*
================
UI_PdaScript

the menus' "uiScript PdaTab <tab>": to that tab; qfalse for other scripts
================
*/
qboolean UI_PdaScript( const char *name, char **args ) {
	const char *tab;
	int i;

	if ( Q_stricmp( name, "PdaTab" ) ) {
		return qfalse;
	}
	if ( !String_Parse( args, &tab ) || UI_PdaTabNum( tab ) < 0 ) {
		return qtrue;
	}
	// (switching: the open one closed, so its key doesn't close the next)
	for ( i = 0; i < (int)UI_PDA_TABS; i++ ) {
		Menus_CloseByName( uiPdaMenus[i] );
	}
	UI_PdaOpen( tab );
	return qtrue;
}

/*
===============================================================================

	the owner draws

===============================================================================
*/

// a line of text at y (its baseline), wrapped to the rect's width from x;
// how far down it went
static float UI_PdaWrapped( float x, float y, float w, float scale, vec4_t color, const char *text, int textStyle ) {
	char line[256];
	const char *s = text;
	float lineHeight = DC->textHeight( "Ag", scale, 0 ) * 1.35f;
	int len;

	while ( *s ) {
		// as much as fits, broken at a space
		len = 0;
		while ( s[len] && len < (int)sizeof( line ) - 1 ) {
			Q_strncpyz( line, s, len + 2 );
			if ( DC->textWidth( line, scale, 0 ) > w ) {
				break;
			}
			len++;
		}
		if ( s[len] ) {
			int brk = len;

			while ( brk > 0 && s[brk] != ' ' ) {
				brk--;
			}
			if ( brk > 0 ) {
				len = brk;
			}
		}
		Q_strncpyz( line, s, len + 1 );
		DC->drawText( x, y, scale, color, line, 0, 0, textStyle );
		y += lineHeight;
		s += len;
		while ( *s == ' ' ) {
			s++;
		}
	}
	return y;
}

// the quests, the ones going on first, the most recently changed first
static int UI_QuestCompare( const void *a, const void *b ) {
	const cgQuest_t *qa = *(const cgQuest_t **)a, *qb = *(const cgQuest_t **)b;
	int ra = qa->state == 'a' ? 0 : 1, rb = qb->state == 'a' ? 0 : 1;

	if ( ra != rb ) {
		return ra - rb;
	}
	return qb->order - qa->order;
}

static void UI_DrawQuests( rectDef_t *rect, float scale, vec4_t color, int textStyle ) {
	static vec4_t done = { 0.55f, 0.85f, 0.45f, 1.0f }, failed = { 0.85f, 0.35f, 0.3f, 1.0f };
	static vec4_t dim = { 0.6f, 0.55f, 0.45f, 1.0f };
	const cgQuest_t *list[MAX_QUESTS];
	float y = rect->y, lineHeight = DC->textHeight( "Ag", scale, 0 ) * 1.35f;
	int i, j, n = 0;

	for ( i = 0; i < cg.numQuests; i++ ) {
		list[n++] = &cg.quests[i];
	}
	if ( !n ) {
		DC->drawText( rect->x, y + lineHeight, scale, dim, "Nothing to do yet.", 0, 0, textStyle );
		return;
	}
	qsort( list, n, sizeof( list[0] ), UI_QuestCompare );
	for ( i = 0; i < n && y < rect->y + rect->h; i++ ) {
		const cgQuest_t *q = list[i];
		const char *title = CG_QuestText( q->name, 0 ), *desc = "";
		const dict_t *def = EntityDef_Find( q->name );
		float *c = q->state == 'c' ? done : q->state == 'f' ? failed : color;

		y += lineHeight;
		DC->drawText( rect->x, y, scale * 1.15f, c,
					  q->state == 'c' ? va( "%s (complete)", title ) : q->state == 'f' ? va( "%s (failed)", title ) : title,
					  0, 0, textStyle );
		y += lineHeight * 0.3f;
		if ( def && DictGetString( def, "desc", "", &desc ) && desc[0] && q->state == 'a' ) {
			y = UI_PdaWrapped( rect->x + 12, y + lineHeight, rect->w - 12, scale * 0.85f, dim, desc, textStyle ) - lineHeight;
		}
		// its objectives: to do, done (ticked), failed (crossed); hidden ones not
		for ( j = 0; q->obj[j]; j++ ) {
			char s = q->obj[j];

			if ( s == '-' ) {
				continue;
			}
			y += lineHeight;
			DC->drawText( rect->x + 12, y, scale, s == 'c' ? done : s == 'f' ? failed : color,
						  s == 'c' ? "+" : s == 'f' ? "x" : "-", 0, 0, textStyle );
			y = UI_PdaWrapped( rect->x + 30, y, rect->w - 30, scale, s == 'c' ? done : s == 'f' ? failed : color,
							   CG_QuestText( q->name, j + 1 ), textStyle ) - lineHeight;
		}
		y += lineHeight * 0.6f;
	}
}

// a bar, full as much as frac, under a label
static void UI_PdaBar( rectDef_t *rect, float frac, vec4_t color ) {
	static vec4_t back = { 0.1f, 0.08f, 0.05f, 0.8f };
	float h = 6, y = rect->y + rect->h - h;

	DC->fillRect( rect->x, y, rect->w, h, back );
	DC->fillRect( rect->x, y, rect->w * Com_Clamp( 0, 1, frac ), h, color );
}

static void UI_DrawStatusModel( rectDef_t *rect ) {
	const playerState_t *ps = UI_PdaPlayerState();

	if ( ps ) {
		CG_DrawCharacterPortrait( rect->x, rect->y, rect->w, rect->h, ps->playerNum, ps->weapon, DC->realTime );
	}
}

/*
================
UI_PdaOwnerDraw

the status and quests tabs' owner draws; qfalse for others
================
*/
qboolean UI_PdaOwnerDraw( int ownerDraw, rectDef_t *rect, float scale, vec4_t color, qhandle_t shader, int textStyle ) {
	static vec4_t red = { 0.8f, 0.2f, 0.15f, 1.0f }, steel = { 0.55f, 0.6f, 0.7f, 1.0f };
	const playerState_t *ps = UI_PdaPlayerState();
	char text[256];
	int i;

	switch ( ownerDraw ) {
	case UI_QUEST_LIST:
		UI_DrawQuests( rect, scale, color, textStyle );
		return qtrue;

	case UI_STATUS_MODEL:
		UI_DrawStatusModel( rect );
		return qtrue;

	case UI_STATUS_NAME:
		DC->drawText( rect->x, rect->y + rect->h, scale, color, CG_Cvar_VariableString( "name" ), 0, 0, textStyle );
		return qtrue;

	case UI_STATUS_HEALTH:
		if ( ps ) {
			Com_sprintf( text, sizeof( text ), "Health  %d / %d", ps->stats[STAT_HEALTH], ps->stats[STAT_MAX_HEALTH] );
			DC->drawText( rect->x, rect->y + rect->h - 10, scale, color, text, 0, 0, textStyle );
			UI_PdaBar( rect, ps->stats[STAT_MAX_HEALTH] > 0 ? (float)ps->stats[STAT_HEALTH] / ps->stats[STAT_MAX_HEALTH] : 0, red );
		}
		return qtrue;

	case UI_STATUS_ARMOR:
		if ( !ps ) {
			return qtrue;
		}
		if ( ps->stats[STAT_ARMOR_ITEM] > 0 && ps->stats[STAT_ARMOR_ITEM] < BG_NumItems() ) {
			const gitem_t *worn = BG_ItemForItemNum( ps->stats[STAT_ARMOR_ITEM] );

			Com_sprintf( text, sizeof( text ), "%s  %d / %d, takes %d%%", worn->pickup_name, ps->stats[STAT_ARMOR], worn->quantity,
						 (int)( worn->armorAbsorb * 100 + 0.5f ) );
			DC->drawText( rect->x, rect->y + rect->h - 10, scale, color, text, 0, 0, textStyle );
			UI_PdaBar( rect, worn->quantity > 0 ? (float)ps->stats[STAT_ARMOR] / worn->quantity : 0, steel );
		} else {
			DC->drawText( rect->x, rect->y + rect->h - 10, scale, color, "No armor worn", 0, 0, textStyle );
		}
		return qtrue;

	case UI_STATUS_WEAPON:
		if ( ps && ps->weapon > WP_NONE ) {
			const gitem_t *item = BG_FindItemForWeapon( ps->weapon );
			const gitem_t *ammo = item ? BG_FindItemForAmmo( item->giTag ) : NULL;

			// its item's name, or its def's "inv_name" (the glove's, no item)
			const char *name = item ? item->pickup_name
				: BG_ValidWeapon( ps->weapon ) ? DictGetStringReference( BG_Weapon( ps->weapon )->def, "inv_name", BG_Weapon( ps->weapon )->name ) : "";

			Com_sprintf( text, sizeof( text ), ammo && ps->ammo[ps->weapon] >= 0 ? "%s, %d %s" : "%s", name,
						 ps->ammo[ps->weapon], ammo ? ammo->pickup_name : "" );
			DC->drawText( rect->x, rect->y + rect->h, scale, color, text, 0, 0, textStyle );
		} else {
			DC->drawText( rect->x, rect->y + rect->h, scale, color, "Empty handed", 0, 0, textStyle );
		}
		return qtrue;

	case UI_STATUS_BOOSTS:
		if ( !ps ) {
			return qtrue;
		}
		text[0] = '\0';
		for ( i = 1; i < MAX_POWERUPS; i++ ) {
			const gitem_t *item;

			if ( !ps->powerups[i] || !( item = BG_FindItemForPowerup( i ) ) ) {
				continue;
			}
			if ( ps->powerups[i] > cg.time && ps->powerups[i] - cg.time < 3600000 ) {
				Q_strcat( text, sizeof( text ), va( "%s%s %ds", text[0] ? ", " : "", item->pickup_name, ( ps->powerups[i] - cg.time ) / 1000 ) );
			} else {
				Q_strcat( text, sizeof( text ), va( "%s%s", text[0] ? ", " : "", item->pickup_name ) );
			}
		}
		UI_PdaWrapped( rect->x, rect->y + DC->textHeight( "Ag", scale, 0 ), rect->w, scale, color, text[0] ? text : "No boosts", textStyle );
		return qtrue;

	case UI_STATUS_PLACE:
		DC->drawText( rect->x, rect->y + rect->h, scale, color, CG_ConfigString( CS_MESSAGE ), 0, 0, textStyle );
		return qtrue;

	default:
		return qfalse;
	}
}

/*
================
UI_PdaVisible

the tab bar's ownerdrawflag (UI_SHOW_QUESTS_NEW: something new on the
quests tab): qtrue when flags' hold, the others kept
================
*/
qboolean UI_PdaVisible( int *flags ) {
	if ( *flags & UI_SHOW_QUESTS_NEW ) {
		*flags &= ~UI_SHOW_QUESTS_NEW;
		return trap_Cvar_VariableValue( "ui_questsNew" ) != 0;
	}
	return qtrue;
}
