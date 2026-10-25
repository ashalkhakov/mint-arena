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

// cg_quests.c -- the player's quests as the game sends them (g_quests.c,
// the "quests" server command), kept for the screen's quests tab
// (ui_quests.c); what changed announced, Doom 3's newObjective and OpenJK's
// "new mission information": a line in the middle of the screen, a sound,
// and the tab marked till it's looked at (ui_questsNew).

#include "cg_local.h"

// a quest def's text: its "title", or its objective n's "obj<n>"
const char *CG_QuestText( const char *quest, int n ) {
	const dict_t *def = EntityDef_Find( quest );
	const char *text = "";

	if ( def ) {
		DictGetString( def, n ? va( "obj%d", n ) : "title", "", &text );
	}
	return text[0] ? text : quest;
}

static cgQuest_t *CG_QuestFind( cgQuest_t *list, int num, const char *name ) {
	int i;

	for ( i = 0; i < num; i++ ) {
		if ( !Q_stricmp( list[i].name, name ) ) {
			return &list[i];
		}
	}
	return NULL;
}

static void CG_QuestNotice( const char *text, qboolean failed ) {
	CG_Printf( "%s\n", text );
	CG_CenterPrint( 0, text, SCREEN_HEIGHT * 0.22f, 0.4f );
	trap_S_StartLocalSound( trap_S_RegisterSound( failed ? "sounds/menus/no_001.wav" : "sounds/menus/yes_001.wav", qfalse ), CHAN_LOCAL_SOUND );
	trap_Cvar_Set( "ui_questsNew", "1" );
}

/*
================
CG_ParseQuests

"quests <n>", then each quest's def, state, order and objectives
(QUEST_* characters); what's new against what was (none the first time,
a map's start, or when a save is loaded)
================
*/
void CG_ParseQuests( int start, qboolean loaded ) {
	cgQuest_t old[MAX_QUESTS];
	int numOld = cg.numQuests, i, j, n, arg;

	memcpy( old, cg.quests, sizeof( old ) );
	n = atoi( CG_Argv( start + 1 ) );
	cg.numQuests = 0;
	for ( i = 0, arg = start + 2; i < n && i < MAX_QUESTS; i++, arg += 4 ) {
		cgQuest_t *q = &cg.quests[cg.numQuests++];

		Q_strncpyz( q->name, CG_Argv( arg ), sizeof( q->name ) );
		q->state = CG_Argv( arg + 1 )[0];
		q->order = atoi( CG_Argv( arg + 2 ) );
		Q_strncpyz( q->obj, CG_Argv( arg + 3 ), sizeof( q->obj ) );
	}

	if ( !cg.questsReceived || loaded ) {
		cg.questsReceived = qtrue;
		return;
	}
	for ( i = 0; i < cg.numQuests; i++ ) {
		cgQuest_t *q = &cg.quests[i];
		cgQuest_t *was = CG_QuestFind( old, numOld, q->name );

		if ( !was ) {
			CG_QuestNotice( va( "New quest: %s", CG_QuestText( q->name, 0 ) ), qfalse );
		} else if ( was->state != q->state && q->state != 'a' ) {
			CG_QuestNotice( va( q->state == 'f' ? "Quest failed: %s" : "Quest complete: %s", CG_QuestText( q->name, 0 ) ),
							q->state == 'f' );
		}
		for ( j = 0; q->obj[j]; j++ ) {
			char now = q->obj[j], before = was && j < (int)strlen( was->obj ) ? was->obj[j] : '-';

			if ( now == before || now == '-' ) {
				continue;
			}
			if ( now == 'c' ) {
				CG_QuestNotice( va( "Done: %s", CG_QuestText( q->name, j + 1 ) ), qfalse );
			} else if ( now == 'f' ) {
				CG_QuestNotice( va( "Failed: %s", CG_QuestText( q->name, j + 1 ) ), qtrue );
			} else if ( before == '-' ) {
				CG_QuestNotice( va( "New objective: %s", CG_QuestText( q->name, j + 1 ) ), qfalse );
			}
		}
	}
}
