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
// cg_cinematic.c -- the view in a cinematic (the game's g_cinematic.c): from
// its camera, worked out each frame from CS_CINEMATIC (as RTCW's client played
// its cameras, CG_CalcViewValues), letterboxed at Daikatana's 1.85:1 with no
// HUD; and Doom 3's fades (idPlayerView::Fade), from CS_FADE

#include "cg_local.h"
#include "../game/bg_camera.h"

#define CINEMATIC_ASPECT	1.85f

typedef enum {
	CINE_NONE,
	CINE_SPLINE,			// a camera file, from a time
	CINE_VIEW				// a fixed view
} cineType_t;

static struct {
	cineType_t	type;
	char		camera[MAX_QPATH];
	int			startTime;
	qboolean	played;		// it's at its end
	qboolean	skip;
	vec3_t		origin, angles;
	float		fov;

	vec4_t		fadeColor;
	float		fadeFrom;
	int			fadeStart, fadeDuration;
} cine;

/*
================
CG_ParseCinematic

CS_CINEMATIC: "" | "spline <camera> <start> <played>" | "view <origin>
<angles> <fov>", and " skip" while it's skipped
================
*/
void CG_ParseCinematic( void ) {
	const char	*s = CG_ConfigString( CS_CINEMATIC );
	char		type[16];

	memset( type, 0, sizeof( type ) );
	cine.skip = strstr( s, " skip" ) != NULL;
	if ( !s[0] || sscanf( s, "%15s", type ) != 1 ) {
		cine.type = CINE_NONE;
	} else if ( !Q_stricmp( type, "spline" ) ) {
		int played = 0;

		cine.type = CINE_SPLINE;
		sscanf( s, "%*s %63s %d %d", cine.camera, &cine.startTime, &played );
		cine.played = played;
		BG_LoadCamera( cine.camera );
	} else if ( !Q_stricmp( type, "view" ) ) {
		cine.type = CINE_VIEW;
		sscanf( s, "%*s %f %f %f %f %f %f %f", &cine.origin[0], &cine.origin[1], &cine.origin[2],
				&cine.angles[0], &cine.angles[1], &cine.angles[2], &cine.fov );
	} else {
		cine.type = CINE_NONE;
	}
	if ( cine.skip ) {
		trap_S_StopAllSounds();
	}
}

/*
================
CG_ParseFade

CS_FADE: "<r g b> <from alpha> <to alpha> <start> <ms>"
================
*/
void CG_ParseFade( void ) {
	const char *s = CG_ConfigString( CS_FADE );

	memset( cine.fadeColor, 0, sizeof( cine.fadeColor ) );
	cine.fadeFrom = 0;
	cine.fadeStart = cine.fadeDuration = 0;
	if ( s[0] ) {
		sscanf( s, "%f %f %f %f %f %d %d", &cine.fadeColor[0], &cine.fadeColor[1], &cine.fadeColor[2],
				&cine.fadeFrom, &cine.fadeColor[3], &cine.fadeStart, &cine.fadeDuration );
	}
}

qboolean CG_InCinematic( void ) {
	return cine.type != CINE_NONE;
}

/*
================
CG_CinematicView

in a cinematic, the view from its camera: origin, angles and FOV in
cg.refdef; qfalse if there's none
================
*/
qboolean CG_CinematicView( void ) {
	vec3_t	origin, angles;
	float	fov = 90, fovY;

	if ( cine.type == CINE_SPLINE ) {
		cameraDef_t *cam = BG_LoadCamera( cine.camera );

		if ( !cam ) {
			return qfalse;
		}
		BG_CameraView( cam, cine.played ? cam->totalTime : MAX( 0, cg.time - cine.startTime ), origin, angles, &fov );
	} else if ( cine.type == CINE_VIEW ) {
		VectorCopy( cine.origin, origin );
		VectorCopy( cine.angles, angles );
		fov = cine.fov;
	} else {
		return qfalse;
	}

	VectorCopy( origin, cg.refdef.vieworg );
	VectorCopy( angles, cg.refdefViewAngles );
	AnglesToAxis( cg.refdefViewAngles, cg.refdef.viewaxis );

	// the FOV across, its height from the picture's shape
	fov = Com_Clamp( 1, 170, fov );
	fovY = atan2( cg.refdef.height, cg.refdef.width / tan( DEG2RAD( fov ) * 0.5f ) );
	cg.refdef.fov_x = fov;
	cg.refdef.fov_y = RAD2DEG( fovY ) * 2;
	cg.refdef.weapon_fov_x = cg.refdef.fov_x;
	cg.refdef.weapon_fov_y = cg.refdef.fov_y;
	cg.cur_lc->renderingThirdPerson = qtrue;
	return qtrue;
}

/*
================
CG_DrawFade

the screen blended with the fade's colour, and a keyframe camera's
================
*/
static void CG_DrawFade( void ) {
	vec4_t	c;
	float	f;

	VectorCopy( cine.fadeColor, c );
	if ( cine.fadeDuration > 0 && cg.time < cine.fadeStart + cine.fadeDuration ) {
		f = Com_Clamp( 0, 1, (float)( cg.time - cine.fadeStart ) / cine.fadeDuration );
		c[3] = cine.fadeFrom + ( cine.fadeColor[3] - cine.fadeFrom ) * f;
	} else {
		c[3] = cine.fadeColor[3];
	}
	CG_SetScreenPlacement( PLACE_STRETCH, PLACE_STRETCH );
	if ( c[3] > 0 ) {
		CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, c );
	}
	if ( cine.type == CINE_SPLINE ) {
		cameraDef_t *cam = BG_LoadCamera( cine.camera );

		if ( cam && BG_CameraFade( cam, cine.played ? cam->totalTime : MAX( 0, cg.time - cine.startTime ), c ) && c[3] > 0 ) {
			CG_FillRect( 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, c );
		}
	}
	CG_PopScreenPlacement();
}

/*
================
CG_DrawCinematic

over the 3D view: the letterbox in a cinematic (Daikatana's 1.85:1, the view
cut to it), then the fades. qtrue in a cinematic: no HUD
================
*/
qboolean CG_DrawCinematicView( void ) {
	if ( cine.type != CINE_NONE ) {
		float aspect = (float)cg.viewportWidth / MAX( 1, cg.viewportHeight );

		if ( aspect < CINEMATIC_ASPECT ) {
			float bar = SCREEN_HEIGHT * ( 1 - aspect / CINEMATIC_ASPECT ) * 0.5f;

			CG_SetScreenPlacement( PLACE_STRETCH, PLACE_STRETCH );
			CG_FillRect( 0, 0, SCREEN_WIDTH, bar, colorBlack );
			CG_FillRect( 0, SCREEN_HEIGHT - bar, SCREEN_WIDTH, bar, colorBlack );
			CG_PopScreenPlacement();
		}
	}
	CG_DrawFade();
	return cine.type != CINE_NONE;
}
