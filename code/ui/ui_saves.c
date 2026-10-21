/*
===========================================================================
Copyright (C) 2026 the Daikatana restoration project

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
//
// ui_saves.c -- the saved games, for the save and load menus: Daikatana's
// list of saves (dk_menu_loadgame.cpp, dk_menu_savegame.cpp), each with its
// map's picture, its episode and map, how the player was and when it was
// saved. The list is FEEDER_SAVEGAMES; what it shows of the selected save
// are UI_SAVE_* owner draws; uiScript LoadSaves, LoadSave, SaveGame and
// DeleteSave act on it. The saves are g_save.c's save/<name>.svg, their
// headers saveInfo_t.

#include "ui_local.h"

#define DC			( &uiInfo.uiDC )
#define MAX_SAVES	128

typedef struct {
	char		name[MAX_QPATH];
	saveInfo_t	info;
	qboolean	current;		// this build can load it
} uiSave_t;

static uiSave_t	ui_saves[MAX_SAVES];
static int		ui_numSaves;
static int		ui_saveIndex = -1;

// newest first; saves of no date (none should be) last
static int QDECL UI_SaveCompare( const void *a, const void *b ) {
	return -strcmp( ( (const uiSave_t *)a )->info.date, ( (const uiSave_t *)b )->info.date );
}

/*
================
UI_LoadSaves

Reads the headers of the saves in save/, for the menus' list.
================
*/
void UI_LoadSaves( void ) {
	char list[8192], *name;
	fileHandle_t f;
	uiSave_t *save;
	int count, i, len;

	ui_numSaves = 0;
	count = trap_FS_GetFileList( "save", ".svg", list, sizeof( list ) );
	name = list;
	for ( i = 0; i < count && ui_numSaves < MAX_SAVES; i++, name += len + 1 ) {
		len = strlen( name );
		save = &ui_saves[ui_numSaves];
		memset( save, 0, sizeof( *save ) );
		Q_strncpyz( save->name, name, sizeof( save->name ) );
		COM_StripExtension( save->name, save->name, sizeof( save->name ) );

		if ( trap_FS_FOpenFile( va( "save/%s", name ), &f, FS_READ ) < (int)sizeof( saveInfo_t ) ) {
			if ( f ) {
				trap_FS_FCloseFile( f );
			}
			continue;
		}
		trap_FS_Read( &save->info, sizeof( save->info ), f );
		trap_FS_FCloseFile( f );
		if ( save->info.magic != SAVEGAME_MAGIC ) {
			continue;
		}
		save->current = ( save->info.version == SAVEGAME_VERSION );
		if ( !save->current ) {
			memset( save->info.date, 0, sizeof( save->info.date ) );		// older headers have none
			memset( save->info.title, 0, sizeof( save->info.title ) );
		}
		save->info.date[sizeof( save->info.date ) - 1] = '\0';
		save->info.title[sizeof( save->info.title ) - 1] = '\0';
		save->info.map[sizeof( save->info.map ) - 1] = '\0';
		ui_numSaves++;
	}
	qsort( ui_saves, ui_numSaves, sizeof( ui_saves[0] ), UI_SaveCompare );

	// the newest selected
	ui_saveIndex = ui_numSaves ? 0 : -1;
	Menu_SetFeederSelection( NULL, FEEDER_SAVEGAMES, ui_saveIndex, NULL );
	UI_SavesSelect( ui_saveIndex );
}

void UI_SavesSelect( int index );

static const uiSave_t *UI_SelectedSave( void ) {
	return ui_saveIndex >= 0 && ui_saveIndex < ui_numSaves ? &ui_saves[ui_saveIndex] : NULL;
}

/*
================
The list (FEEDER_SAVEGAMES): column 0 the name, 1 when it was saved
================
*/
int UI_SavesCount( void ) {
	return ui_numSaves;
}

const char *UI_SavesItemText( int index, int column ) {
	if ( index < 0 || index >= ui_numSaves ) {
		return "";
	}
	if ( column == 1 ) {
		return ui_saves[index].current ? ui_saves[index].info.date : "(old)";
	}
	return ui_saves[index].name;
}

// a save chosen: the save menu's name is its, to save over it
void UI_SavesSelect( int index ) {
	ui_saveIndex = index >= 0 && index < ui_numSaves ? index : -1;
	if ( UI_SelectedSave() ) {
		trap_Cvar_Set( "ui_saveName", ui_saves[index].name );
	}
}

/*
================
UI_SavesOwnerDraw

What the menus show of the selected save; qfalse for others' owner draws.
================
*/
qboolean UI_SavesOwnerDraw( int ownerDraw, rectDef_t *rect, float scale, vec4_t color, int textStyle ) {
	const uiSave_t *save = UI_SelectedSave();
	const playerState_t *ps;
	char text[256];
	qhandle_t shot;
	float y, line;
	int seconds, gems;

	switch ( ownerDraw ) {
	case UI_SAVE_SHOT:
		// the map's levelshot, as the loading screen has
		if ( !save ) {
			return qtrue;
		}
		shot = DC->registerShaderNoMip( va( "levelshots/%s", save->info.map ) );
		if ( !shot ) {
			shot = DC->registerShaderNoMip( "menu/art/unknownmap" );
		}
		DC->setColor( NULL );
		DC->drawHandlePic( rect->x, rect->y, rect->w, rect->h, shot );
		return qtrue;

	case UI_SAVE_TITLE:
		// Daikatana's "Episode 1: the map"
		if ( !save ) {
			return qtrue;
		}
		if ( ( save->info.map[0] == 'e' || save->info.map[0] == 'E' ) && isdigit( save->info.map[1] ) ) {
			Com_sprintf( text, sizeof( text ), "Episode %c: %s", save->info.map[1],
				save->info.title[0] ? save->info.title : save->info.map );
		} else {
			Q_strncpyz( text, save->info.title[0] ? save->info.title : save->info.map, sizeof( text ) );
		}
		DC->drawText( rect->x, rect->y + rect->h, scale, color, text, 0, 0, textStyle );
		return qtrue;

	case UI_SAVE_INFO:
		// how the player was, the secrets found, the time on the map
		if ( !save ) {
			return qtrue;
		}
		line = DC->textHeight( "A", scale, 0 ) * 1.35f;
		y = rect->y + line;
		if ( !save->current ) {
			DC->drawText( rect->x, y, scale, color, "Saved by an older version", 0, 0, textStyle );
			return qtrue;
		}
		Com_sprintf( text, sizeof( text ), "Health %d   Armor %d", save->info.health, save->info.armor );
		DC->drawText( rect->x, y, scale, color, text, 0, 0, textStyle );
		y += line;
		Com_sprintf( text, sizeof( text ), "Secrets %d/%d", save->info.secretsFound, save->info.numSecrets );
		DC->drawText( rect->x, y, scale, color, text, 0, 0, textStyle );
		y += line;
		seconds = save->info.time / 1000;
		Com_sprintf( text, sizeof( text ), "Time %d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60 );
		DC->drawText( rect->x, y, scale, color, text, 0, 0, textStyle );
		y += line;
		Com_sprintf( text, sizeof( text ), "Saved %s", save->info.date );
		DC->drawText( rect->x, y, scale, color, text, 0, 0, textStyle );
		return qtrue;

	case UI_SAVE_GEMS:
		// Daikatana's save gems, what a save costs
		if ( trap_Cvar_VariableValue( "g_unlimitedSaves" ) ) {
			Q_strncpyz( text, "Save Gems: unlimited", sizeof( text ) );
		} else {
			ps = cg.snap ? &cg.snap->pss[0] : NULL;
			gems = 0;
			if ( ps ) {
				const gitem_t *gem = BG_FindItemByClassname( "item_savegem" );

				gems = gem ? BG_InvCount( ps, BG_ItemNumForItem( gem ) ) : 0;
			}
			Com_sprintf( text, sizeof( text ), "Save Gems: %d", gems );
		}
		DC->drawText( rect->x, rect->y + rect->h, scale, color, text, 0, 0, textStyle );
		return qtrue;

	default:
		return qfalse;
	}
}

/*
================
UI_SavesVisible

UI_SHOW_SAVE_SELECTED, when a save is selected that this build can load.
================
*/
qboolean UI_SavesVisible( int *flags ) {
	qboolean vis = qtrue;

	if ( *flags & UI_SHOW_SAVE_SELECTED ) {
		vis &= UI_SelectedSave() && UI_SelectedSave()->current;
		*flags &= ~UI_SHOW_SAVE_SELECTED;
	}
	return vis;
}

/*
================
UI_SavesScript

uiScript LoadSaves (the list read again, the newest selected), SaveName
(none selected, ui_saveName the map's name, Daikatana's default), LoadSave (the selected
one loaded), SaveGame (saved as ui_saveName), DeleteSave (the selected one
deleted); qfalse for other scripts.
================
*/
qboolean UI_SavesScript( const char *name ) {
	const uiSave_t *save = UI_SelectedSave();
	char saveName[MAX_QPATH];
	char *c;

	if ( !Q_stricmp( name, "LoadSaves" ) ) {
		UI_LoadSaves();
	} else if ( !Q_stricmp( name, "SaveName" ) ) {
		// none selected, so that picking any save names it
		ui_saveIndex = -1;
		Menu_SetFeederSelection( NULL, FEEDER_SAVEGAMES, -1, NULL );
		trap_Cvar_VariableStringBuffer( "mapname", saveName, sizeof( saveName ) );
		trap_Cvar_Set( "ui_saveName", saveName );
	} else if ( !Q_stricmp( name, "LoadSave" ) ) {
		if ( save && save->current ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "loadgame %s\n", save->name ) );
		}
	} else if ( !Q_stricmp( name, "SaveGame" ) ) {
		// a plain name, as savegame takes
		trap_Cvar_VariableStringBuffer( "ui_saveName", saveName, sizeof( saveName ) );
		for ( c = saveName; *c; c++ ) {
			if ( !isalnum( *c ) && *c != '_' && *c != '-' ) {
				*c = '_';
			}
		}
		if ( saveName[0] ) {
			trap_Cmd_ExecuteText( EXEC_APPEND, va( "savegame %s\n", saveName ) );
		}
	} else if ( !Q_stricmp( name, "DeleteSave" ) ) {
		if ( save ) {
			trap_FS_Delete( va( "save/%s.svg", save->name ) );
			UI_LoadSaves();
		}
	} else {
		return qfalse;
	}
	return qtrue;
}
