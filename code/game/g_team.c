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

#include "g_local.h"


int OtherTeam(int team) {
	if (team==TEAM_RED)
		return TEAM_BLUE;
	else if (team==TEAM_BLUE)
		return TEAM_RED;
	return team;
}

const char *TeamName(int team)  {
	if (team==TEAM_RED)
		return "RED";
	else if (team==TEAM_BLUE)
		return "BLUE";
	else if (team==TEAM_SPECTATOR)
		return "SPECTATOR";
	return "FREE";
}

const char *TeamColorString(int team) {
	if (team==TEAM_RED)
		return S_COLOR_RED;
	else if (team==TEAM_BLUE)
		return S_COLOR_BLUE;
	else if (team==TEAM_SPECTATOR)
		return S_COLOR_YELLOW;
	return S_COLOR_WHITE;
}

/*
==============
AddTeamScore

 for gametype GT_TEAM this is called in AddScore in g_combat.c (checks fraglimit to win)
 for gametype > GT_TEAM this is called when gametype-specific scoring happens (checks capturelimit to win)
        (e.g. capture flag, kill obelisk, return skulls)
==============
*/
void AddTeamScore(vec3_t origin, int team, int score) {
	int			eventParm;
	int			otherTeam;
	gentity_t	*te;

	if ( score == 0 ) {
		return;
	}

	eventParm = -1;
	otherTeam = OtherTeam( team );

	if ( level.teamScores[ team ] + score == level.teamScores[ otherTeam ] ) {
		//teams are tied sound
		eventParm = GTS_TEAMS_ARE_TIED;
	} else if ( level.teamScores[ team ] >= level.teamScores[ otherTeam ] &&
				level.teamScores[ team ] + score < level.teamScores[ otherTeam ] ) {
		// other team took the lead sound (negative score)
		eventParm = ( otherTeam == TEAM_RED ) ? GTS_REDTEAM_TOOK_LEAD : GTS_BLUETEAM_TOOK_LEAD;
	} else if ( level.teamScores[ team ] <= level.teamScores[ otherTeam ] &&
				level.teamScores[ team ] + score > level.teamScores[ otherTeam ] ) {
		// this team took the lead sound
		eventParm = ( team == TEAM_RED ) ? GTS_REDTEAM_TOOK_LEAD : GTS_BLUETEAM_TOOK_LEAD;
	} else if ( score > 0 && g_gametype.integer != GT_TEAM ) {
		// team scored sound
		eventParm = ( team == TEAM_RED ) ? GTS_REDTEAM_SCORED : GTS_BLUETEAM_SCORED;
	}

	if ( eventParm != -1 ) {
		te = G_TempEntity(origin, EV_GLOBAL_TEAM_SOUND );
		te->r.svFlags |= SVF_BROADCAST;
		te->s.eventParm = eventParm;
	}

	level.teamScores[ team ] += score;
}

/*
==============
OnSameTeam
==============
*/
qboolean OnSameTeam( gentity_t *ent1, gentity_t *ent2 ) {
	if ( !ent1 || !ent1->player || !ent2 || !ent2->player ) {
		return qfalse;
	}

	if ( g_gametype.integer < GT_TEAM ) {
		return qfalse;
	}

	if ( ent1->player->sess.sessionTeam == ent2->player->sess.sessionTeam ) {
		return qtrue;
	}

	return qfalse;
}


/*
===========
Team_GetLocation

Report a location for the player. Uses placed nearby target_location entities
============
*/
gentity_t *Team_GetLocation(gentity_t *ent)
{
	gentity_t		*eloc, *best;
	float			bestlen, len;
	vec3_t			origin;

	if ( !ent ) {
		return NULL;
	}

	best = NULL;
	bestlen = 3*8192.0*8192.0;

	VectorCopy( ent->r.currentOrigin, origin );

	for (eloc = level.locationHead; eloc; eloc = eloc->nextTrain) {
		len = ( origin[0] - eloc->r.currentOrigin[0] ) * ( origin[0] - eloc->r.currentOrigin[0] )
			+ ( origin[1] - eloc->r.currentOrigin[1] ) * ( origin[1] - eloc->r.currentOrigin[1] )
			+ ( origin[2] - eloc->r.currentOrigin[2] ) * ( origin[2] - eloc->r.currentOrigin[2] );

		if ( len > bestlen ) {
			continue;
		}

		if ( !trap_InPVS( origin, eloc->r.currentOrigin ) ) {
			continue;
		}

		bestlen = len;
		best = eloc;
	}

	return best;
}


/*
===========
Team_GetLocation

Report a location for the player. Uses placed nearby target_location entities
============
*/
qboolean Team_GetLocationMsg(gentity_t *ent, char *loc, int loclen)
{
	gentity_t *best;

	best = Team_GetLocation( ent );
	
	if (!best)
		return qfalse;

	if (best->count) {
		if (best->count < 0)
			best->count = 0;
		if (best->count > 7)
			best->count = 7;
		Com_sprintf(loc, loclen, "%c%c%s" S_COLOR_WHITE, Q_COLOR_ESCAPE, best->count + '0', best->message );
	} else
		Com_sprintf(loc, loclen, "%s", best->message);

	return qtrue;
}


/*---------------------------------------------------------------------------*/

static int QDECL SortPlayers( const void *a, const void *b ) {
	return *(int *)a - *(int *)b;
}


/*
==================
TeamplayLocationsMessage

Format:
	playerNum location health armor weapon powerups

==================
*/
void TeamplayInfoMessage( gentity_t *ent ) {
	char		entry[1024];
	char		string[8192];
	int			stringlength;
	int			i, j;
	gentity_t	*player;
	int			cnt;
	int			h, a;
	int			players[TEAM_MAXOVERLAY];
	int			team;

	if ( ! ent->player->pers.teamInfo )
		return;

	// send team info to spectator for team of followed player
	if (ent->player->sess.sessionTeam == TEAM_SPECTATOR) {
		if ( ent->player->sess.spectatorState != SPECTATOR_FOLLOW
			|| ent->player->sess.spectatorPlayer < 0 ) {
			return;
		}
		team = g_entities[ ent->player->sess.spectatorPlayer ].player->sess.sessionTeam;
	} else {
		team = ent->player->sess.sessionTeam;
	}

	if (team != TEAM_RED && team != TEAM_BLUE) {
		return;
	}

	// figure out what player should be on the display
	// we are limited to 8, but we want to use the top eight players
	// but in player order (so they don't keep changing position on the overlay)
	for (i = 0, cnt = 0; i < g_maxplayers.integer && cnt < TEAM_MAXOVERLAY; i++) {
		player = g_entities + level.sortedPlayers[i];
		if (player->inuse && player->player->sess.sessionTeam == team ) {
			players[cnt++] = level.sortedPlayers[i];
		}
	}

	// We have the top eight players, sort them by playerNum
	qsort( players, cnt, sizeof( players[0] ), SortPlayers );

	// send the latest information on all players
	string[0] = 0;
	stringlength = 0;

	for (i = 0, cnt = 0; i < g_maxplayers.integer && cnt < TEAM_MAXOVERLAY; i++) {
		player = g_entities + i;
		if (player->inuse && player->player->sess.sessionTeam == team ) {

			h = player->player->ps.stats[STAT_HEALTH];
			a = player->player->ps.stats[STAT_ARMOR];
			if (h < 0) h = 0;
			if (a < 0) a = 0;

			Com_sprintf (entry, sizeof(entry),
				" %i %i %i %i %i %i", 
//				level.sortedPlayers[i], player->player->pers.teamState.location, h, a, 
				i, player->player->pers.teamState.location, h, a, 
				player->player->ps.weapon, player->s.powerups);
			j = strlen(entry);
			if (stringlength + j >= sizeof(string))
				break;
			strcpy (string + stringlength, entry);
			stringlength += j;
			cnt++;
		}
	}

	trap_SendServerCommand( ent-g_entities, va("tinfo %i %i %s", team, cnt, string) );
}

void CheckTeamStatus(void) {
	int i;
	gentity_t *loc, *ent;

	if (level.time - level.lastTeamLocationTime > TEAM_LOCATION_UPDATE_TIME) {

		level.lastTeamLocationTime = level.time;

		for (i = 0; i < g_maxplayers.integer; i++) {
			ent = g_entities + i;

			if ( ent->player->pers.connected != CON_CONNECTED ) {
				continue;
			}

			if (ent->inuse && (ent->player->sess.sessionTeam == TEAM_RED ||	ent->player->sess.sessionTeam == TEAM_BLUE)) {
				loc = Team_GetLocation( ent );
				if (loc)
					ent->player->pers.teamState.location = loc->health;
				else
					ent->player->pers.teamState.location = 0;
			}
		}

		for (i = 0; i < g_maxplayers.integer; i++) {
			ent = g_entities + i;

			if ( ent->player->pers.connected != CON_CONNECTED ) {
				continue;
			}

			if (ent->inuse) {
				TeamplayInfoMessage( ent );
			}
		}
	}
}
