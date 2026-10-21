/*
===========================================================================
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

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
// g_cinematic.c -- Doom 3's cinematic mode (idGameLocal::SetCamera, the
// cameras of Camera.cpp) with RTCW's spline cameras (bg_camera.c)
//
// sys.setCamera( $cam ) puts every player's view on a camera; until
// sys.firstPerson(), the players are hidden, frozen and unhurt, and see it
// letterboxed with no HUD. A player's attack, use or jump skips: the
// scripts' waits end at once and the cameras run out, so the cutscene's
// script runs through to its end (Doom 3 ran whole frames on quickly).
//
// Cameras: func_cameraspline plays cameras/<"camera">.camera (an RTCW path or
// keyframes); func_cameraview is Doom 3's, a fixed view from where it is, at
// its angles or at its "cameraTarget". Clients are told the camera in
// CS_CINEMATIC and work out the view themselves, each frame; the server does
// the same, so what's around the camera is sent to them (a portal entity at
// each player, seeing from the camera, as RTCW's).
//
// sys.fadeIn( color, secs ), fadeOut and fadeTo are Doom 3's fades, in
// CS_FADE.

#include "g_local.h"
#include "bg_camera.h"

#define CINEMATIC_MAX_SKIP		10000	// ms a skip may take before the cinematic is ended for it

/*
===============================================================================

CAMERAS

===============================================================================
*/

/*
================
G_CameraView

where a camera is, which way it looks and its FOV now; qfalse when a spline
camera has run out (it stays at its end)
================
*/
qboolean G_CameraView( gentity_t *cam, vec3_t origin, vec3_t angles, float *fov ) {
	if ( cam->cameraName ) {
		cameraDef_t	*def = BG_LoadCamera( cam->cameraName );
		float		t = cam->cameraPlaying ? level.time - cam->cameraStartTime : 0;

		if ( !def ) {
			VectorCopy( cam->r.currentOrigin, origin );
			VectorCopy( cam->r.currentAngles, angles );
			*fov = 90;
			return qfalse;
		}
		if ( !cam->cameraPlaying && cam->cameraStartTime ) {
			t = def->totalTime;		// played
		}
		return BG_CameraView( def, t, origin, angles, fov ) && cam->cameraPlaying;
	}

	// Doom 3's idCameraView: from where it is (or what it's attached to), at
	// its angles or toward its target
	VectorCopy( cam->r.currentOrigin, origin );
	VectorCopy( cam->r.currentAngles, angles );
	if ( cam->cameraTarget && ( !cam->target_ent || !cam->target_ent->inuse ) ) {
		cam->target_ent = G_Find( NULL, FOFS( targetname ), cam->cameraTarget );
	}
	if ( cam->target_ent && cam->target_ent->inuse ) {
		vec3_t dir;

		VectorSubtract( cam->target_ent->r.currentOrigin, origin, dir );
		vectoangles( dir, angles );
	}
	*fov = cam->cameraFov > 0 ? cam->cameraFov : 90;
	return qtrue;
}

/*
================
G_CameraStart / G_CameraStop

a spline camera plays from now / stops: those waiting for it (sys.waitFor)
go on, and its targets are used, as Doom 3's idCameraAnim::Stop does. The
view stays on it, at its end, until the script puts it elsewhere
================
*/
void G_CameraStart( gentity_t *cam ) {
	if ( !cam->cameraName ) {
		return;
	}
	cam->cameraPlaying = qtrue;
	cam->cameraStartTime = level.time;
	cam->nextthink = level.time + 1;
	if ( level.camera == cam ) {
		G_CinematicConfigString();
	}
}

void G_CameraStop( gentity_t *cam ) {
	if ( !cam->cameraPlaying ) {
		return;
	}
	cam->cameraPlaying = qfalse;
	if ( !cam->cameraStartTime ) {
		cam->cameraStartTime = level.time;
	}
	if ( level.camera == cam ) {
		G_CinematicConfigString();
	}
	Thread_EntityMoveDone( cam );
	G_UseTargets( cam, cam );
}

static void G_CameraThink( gentity_t *cam ) {
	cameraDef_t *def = BG_LoadCamera( cam->cameraName );

	if ( !cam->cameraPlaying ) {
		return;
	}
	if ( !def || level.skipCinematic || level.time - cam->cameraStartTime > def->totalTime ) {
		G_CameraStop( cam );
		return;
	}
	cam->nextthink = level.time + 1;
}

static void G_CameraUse( gentity_t *cam, gentity_t *other, gentity_t *activator ) {
	// Doom 3: a camera that's used is the view (or isn't, if it was)
	if ( level.camera == cam ) {
		G_SetCamera( NULL );
	} else {
		G_SetCamera( cam );
	}
}

/*QUAKED func_cameraspline (0 .5 .8) (-8 -8 -8) (8 8 8)
A camera on a path: cameras/<camera>.camera (RTCW's camera paths, or keyframes).
"camera"	its file's name
Used, it's the view (sys.setCamera); it plays from then. Its targets are used when it ends.
*/
void SP_func_cameraspline( gentity_t *ent ) {
	char *name;

	G_SpawnString( "camera", "", &name );
	if ( !name[0] ) {
		G_Printf( S_COLOR_YELLOW "func_cameraspline at %s: no camera\n", vtos( ent->s.origin ) );
	}
	ent->cameraName = G_NewString( name );
	BG_LoadCamera( ent->cameraName );
	ent->think = G_CameraThink;
	ent->use = G_CameraUse;
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
}

/*QUAKED func_cameraview (0 .5 .8) (-8 -8 -8) (8 8 8)
Doom 3's camera: a fixed view from where it is.
"angles"		which way it looks
"cameraTarget"	or at that entity
"fov"			its FOV (90)
Used, it's the view (or isn't, if it was).
*/
void SP_func_cameraview( gentity_t *ent ) {
	char *target;

	G_SpawnFloat( "fov", "90", &ent->cameraFov );
	G_SpawnString( "cameraTarget", "", &target );
	if ( target[0] ) {
		ent->cameraTarget = G_NewString( target );		// found when it's needed
	}
	ent->use = G_CameraUse;
	ent->r.svFlags |= SVF_NOCLIENT;
	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->r.currentAngles );
}

/*
===============================================================================

CINEMATIC MODE

===============================================================================
*/

/*
================
G_CinematicConfigString

CS_CINEMATIC, from which clients see as the camera does: "" for their own
eyes; "spline <camera> <start time> <0: playing, 1: played>"; "view <origin>
<angles> <fov>". A trailing "skip" while it's skipped
================
*/
void G_CinematicConfigString( void ) {
	gentity_t	*cam = level.camera;
	char		buf[MAX_STRING_CHARS];

	if ( !cam ) {
		trap_SetConfigstring( CS_CINEMATIC, "" );
		return;
	}
	if ( cam->cameraName ) {
		Com_sprintf( buf, sizeof( buf ), "spline %s %d %d", cam->cameraName, cam->cameraStartTime, !cam->cameraPlaying );
	} else {
		vec3_t	origin, angles;
		float	fov;

		G_CameraView( cam, origin, angles, &fov );
		VectorCopy( origin, level.cameraLastOrigin );
		VectorCopy( angles, level.cameraLastAngles );
		Com_sprintf( buf, sizeof( buf ), "view %.1f %.1f %.1f %.2f %.2f %.2f %.1f", origin[0], origin[1], origin[2],
					 angles[0], angles[1], angles[2], fov );
	}
	if ( level.skipCinematic ) {
		Q_strcat( buf, sizeof( buf ), " skip" );
	}
	trap_SetConfigstring( CS_CINEMATIC, buf );
}

/*
================
G_CinematicPlayers

each player as the cinematic wants them: hidden, not solid, and seeing from
the camera (a portal entity at them, only for them); or back
================
*/
static void G_CinematicPlayers( qboolean in ) {
	gentity_t	*ent, *eye;
	int			i;

	for ( i = 0; i < level.maxplayers; i++ ) {
		ent = &g_entities[i];
		eye = level.cinematicEyes[i];
		if ( !in || !ent->inuse || !ent->player || ent->player->pers.connected != CON_CONNECTED ) {
			if ( eye ) {
				G_FreeEntity( eye );
				level.cinematicEyes[i] = NULL;
			}
			if ( !in && ent->inuse && ent->player && ( ent->player->ps.eFlags & EF_NODRAW ) && ent->player->cinematicHidden ) {
				ent->player->ps.eFlags &= ~EF_NODRAW;
				ent->player->cinematicHidden = qfalse;
				if ( ent->health > 0 ) {
					ent->s.contents = CONTENTS_BODY;
					trap_LinkEntity( ent );
				}
			}
			continue;
		}
		if ( !ent->player->cinematicHidden ) {
			ent->player->cinematicHidden = qtrue;
			ent->player->ps.eFlags |= EF_NODRAW;
			ent->s.contents = 0;
			trap_LinkEntity( ent );
		}
		if ( !eye ) {
			eye = level.cinematicEyes[i] = G_Spawn();
			eye->classname = "cinematic_eye";
			eye->s.eType = ET_INVISIBLE;
			eye->r.svFlags = SVF_PORTAL | SVF_PLAYERMASK;
			Com_ClientListClear( &eye->r.sendPlayers );
			Com_ClientListAdd( &eye->r.sendPlayers, i );
		}
		G_SetOrigin( eye, ent->r.currentOrigin );
		VectorCopy( level.cameraLastOrigin, eye->s.origin2 );
		trap_LinkEntity( eye );
	}
}

/*
================
G_SetCamera

Doom 3's idGameLocal::SetCamera: every player sees from the camera, a
cinematic, until NULL gives them their own eyes back. A spline camera that
isn't playing starts
================
*/
void G_SetCamera( gentity_t *cam ) {
	if ( cam && cam->cameraName && !cam->cameraPlaying && !cam->cameraStartTime ) {
		G_CameraStart( cam );
	}
	level.camera = cam;
	if ( !cam ) {
		level.skipCinematic = qfalse;
		G_CinematicPlayers( qfalse );
	} else {
		vec3_t	angles;
		float	fov;

		G_CameraView( cam, level.cameraLastOrigin, angles, &fov );
		if ( !level.inCinematic ) {
			level.cinematicStartTime = level.time;
		}
		G_CinematicPlayers( qtrue );
	}
	level.inCinematic = cam != NULL;
	G_CinematicConfigString();
}

/*
================
G_SkipCinematic

a player skipped it: the scripts' waits end at once (Thread_WaitMS) and the
cameras run out, so the cutscene's script gets to its end
================
*/
void G_SkipCinematic( void ) {
	if ( !level.inCinematic || level.skipCinematic ) {
		return;
	}
	level.skipCinematic = qtrue;
	level.cinematicSkipTime = level.time;
	G_SetFade( colorBlack, 0, 0 );
	G_CinematicConfigString();
}

/*
================
G_CinematicPlayerThink

in a cinematic, a player doesn't move or shoot; attack, use or jump skips
================
*/
void G_CinematicPlayerThink( gentity_t *ent, usercmd_t *ucmd ) {
	gplayer_t	*player = ent->player;
	int			pressed = ucmd->buttons & ~player->cinematicOldButtons;

	if ( ( pressed & ( BUTTON_ATTACK | BUTTON_USE_HOLDABLE | BUTTON_ACTIVATE ) )
		|| ( ucmd->upmove > 0 && player->cinematicOldUp <= 0 ) ) {
		if ( level.time - level.cinematicStartTime > 500 ) {		// not the key that started it
			G_SkipCinematic();
		}
	}
	player->cinematicOldUp = ucmd->upmove;
	player->cinematicOldButtons = ucmd->buttons;
	player->ps.pm_type = PM_FREEZE;
	ucmd->forwardmove = ucmd->rightmove = ucmd->upmove = 0;
	ucmd->buttons = 0;
	VectorClear( player->ps.velocity );
}

/*
================
G_RunCinematic

each frame: the camera's view for what's sent, a moving view camera's view
for the clients, and a skip that runs too long ended
================
*/
void G_RunCinematic( void ) {
	vec3_t	origin, angles;
	float	fov;

	if ( !level.inCinematic ) {
		return;
	}
	if ( !level.camera || !level.camera->inuse ) {
		G_SetCamera( NULL );
		return;
	}
	if ( level.skipCinematic && level.time - level.cinematicSkipTime > CINEMATIC_MAX_SKIP ) {
		G_Printf( S_COLOR_YELLOW "a skipped cinematic didn't end: ended\n" );
		G_SetCamera( NULL );
		return;
	}
	G_CameraView( level.camera, origin, angles, &fov );
	if ( !level.camera->cameraName && ( Distance( origin, level.cameraLastOrigin ) > 1 ||
		fabs( AngleSubtract( angles[YAW], level.cameraLastAngles[YAW] ) ) > 1 || fabs( AngleSubtract( angles[PITCH], level.cameraLastAngles[PITCH] ) ) > 1 ) ) {
		G_CinematicConfigString();		// (it sets the last view)
	}
	VectorCopy( origin, level.cameraLastOrigin );
	G_CinematicPlayers( qtrue );
}

/*
===============================================================================

FADES

===============================================================================
*/

/*
================
G_FadeAlpha

the fade's alpha now
================
*/
static float G_FadeAlpha( void ) {
	float f;

	if ( level.fadeDuration <= 0 || level.time >= level.fadeStartTime + level.fadeDuration ) {
		return level.fadeColor[3];
	}
	f = (float)( level.time - level.fadeStartTime ) / level.fadeDuration;
	return level.fadeFromAlpha + ( level.fadeColor[3] - level.fadeFromAlpha ) * Com_Clamp( 0, 1, f );
}

/*
================
G_SetFade

Doom 3's fades (idPlayerView::Fade): the screen to the colour at alpha, from
how it is now, over ms. CS_FADE: "<r g b> <from alpha> <to alpha> <start>
<ms>"
================
*/
void G_SetFade( const vec4_t color, float alpha, int ms ) {
	level.fadeFromAlpha = G_FadeAlpha();
	VectorCopy( color, level.fadeColor );
	level.fadeColor[3] = alpha;
	level.fadeStartTime = level.time;
	level.fadeDuration = level.skipCinematic ? 0 : ms;
	if ( level.fadeDuration <= 0 ) {
		level.fadeFromAlpha = alpha;
	}
	trap_SetConfigstring( CS_FADE, va( "%.3f %.3f %.3f %.3f %.3f %d %d", level.fadeColor[0], level.fadeColor[1], level.fadeColor[2],
									   level.fadeFromAlpha, alpha, level.fadeStartTime, level.fadeDuration ) );
}
