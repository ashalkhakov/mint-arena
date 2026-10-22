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
// cg_fireflies.c -- Daikatana's fireflies (FireFly.cpp): a swarm of glowing
// flares about a point, each darting out at random until it's its leash
// away, flying back, and out again, wobbling as it goes, now and then
// changing its mind ("personality"); their alpha pulses and their colour
// goes between two. Only looks: the cgame makes them from the map's
// monster_firefly entities (the game frees those), and steps them ten times
// a second, as Daikatana's think did, drawing them between the steps.
//
// Keys: "count" (1 to 10, 4), "distance" the leash (20 to 200, 75),
// "velocity" (1 to 500, 55), "scale" (0.5), "_color" (1 1 1), "color2" (none),
// "delta_alpha" its pulse a step (0, none), "alpha_level" (0.75), and the
// spawnflags the flare: 1 four-pronged, 2 its ring, 4 crossed, 8 crossed
// too, 16 eight-pronged, 32 its ring, 64 a ring, else round.

#include "cg_local.h"

#define MAX_FIREFLY_SWARMS	64
#define MAX_FIREFLIES		10		// a swarm's
#define FIREFLY_STEP		100		// msec a step

typedef struct {
	vec3_t		origin, oldOrigin;
	vec3_t		dir;				// where it heads, at the swarm's velocity
	float		personality;		// 0.25 to 1
	float		alpha;
	int			alphaCount, alphaWait;
	qboolean	alphaUp;
	float		colorLerp;
	qboolean	colorDir;
	vec3_t		color;
	qboolean	outward;			// heading away from the middle
	int			sine;
} firefly_t;

typedef struct {
	vec3_t		origin;
	int			count;
	float		leash, velocity, scale, deltaAlpha, alphaLevel;
	vec3_t		color, color2;
	qhandle_t	shader;
	int			nextStep;
	firefly_t	flies[MAX_FIREFLIES];
} fireflySwarm_t;

static fireflySwarm_t	cg_swarms[MAX_FIREFLY_SWARMS];
static int				cg_numSwarms;

// Daikatana's sin_tbl, twelve steps round
static const float ff_sin[12] = { 0.0f, 0.5f, 0.866f, 1.0f, 0.866f, 0.5f, 0.0f, -0.5f, -0.866f, -1.0f, -0.866f, -0.5f };

// a new mind half the time (FIREFLY_Personality_Generator)
static float CG_FireflyPersonality( float p ) {
	if ( random() > 0.5f ) {
		p = random();
		if ( p < 0.25f ) {
			p = 0.25f;
		}
	}
	return p;
}

void CG_ClearFireflies( void ) {
	cg_numSwarms = 0;
}

/*
QUAKED monster_firefly (0 1 0) (-4 -4 -4) (4 4 4) 4PRONG 4PRONGO 4PRONGX 4PRONGXO 8PRONG 8PRONGO O
Daikatana's fireflies: a swarm of glowing flares about here.
*/
void SP_monster_firefly( void ) {
	static const char *flares[] = { "dkfx/flare4+", "dkfx/flare4+o", "dkfx/flare4x", "dkfx/flare4x",
									"dkfx/flare8+", "dkfx/flare8+o", "dkfx/flareo" };
	fireflySwarm_t *s;
	firefly_t *f;
	int flags, i;

	if ( cg_numSwarms >= MAX_FIREFLY_SWARMS ) {
		return;
	}
	s = &cg_swarms[cg_numSwarms++];
	memset( s, 0, sizeof( *s ) );

	CG_SpawnVector( "origin", "0 0 0", s->origin );
	CG_SpawnInt( "count", "4", &s->count );
	s->count = Com_Clamp( 1, MAX_FIREFLIES, s->count );
	CG_SpawnFloat( "distance", "75", &s->leash );
	s->leash = Com_Clamp( 20, 200, s->leash );
	CG_SpawnFloat( "velocity", "55", &s->velocity );
	s->velocity = Com_Clamp( 1, 500, s->velocity );
	CG_SpawnFloat( "scale", "0.5", &s->scale );
	if ( s->scale <= 0 ) {
		s->scale = 1;
	}
	CG_SpawnVector( "_color", "1 1 1", s->color );
	CG_SpawnVector( "color2", "0 0 0", s->color2 );
	CG_SpawnFloat( "delta_alpha", "0", &s->deltaAlpha );
	CG_SpawnFloat( "alpha_level", "0.75", &s->alphaLevel );

	// the first flag set picks the flare, as Daikatana's if-else chain
	CG_SpawnInt( "spawnflags", "0", &flags );
	s->shader = 0;
	for ( i = 0; i < 7; i++ ) {
		if ( flags & ( 1 << i ) ) {
			s->shader = trap_R_RegisterShader( flares[i] );
			break;
		}
	}
	if ( !s->shader ) {
		s->shader = trap_R_RegisterShader( "dkfx/flare" );
	}

	for ( i = 0; i < s->count; i++ ) {
		f = &s->flies[i];
		VectorSet( f->origin, s->origin[0] + crandom() * 5, s->origin[1] + crandom() * 5, s->origin[2] + crandom() * 5 );
		VectorCopy( f->origin, f->oldOrigin );
		VectorSet( f->dir, crandom(), crandom(), crandom() );
		VectorScale( f->dir, s->velocity, f->dir );
		f->outward = qtrue;
		f->personality = CG_FireflyPersonality( 0.5f + random() * 0.5f );
		f->alpha = 0.75f;
		f->alphaWait = 5 * f->personality;
		VectorCopy( s->color, f->color );
		f->sine = rand() % 12;
	}
}

/*
================
CG_FireflyStep

one of Daikatana's thinks (FIREFLY_Think) and a tenth of a second's flight
================
*/
static void CG_FireflyStep( fireflySwarm_t *s, firefly_t *f ) {
	vec3_t	vel, end;
	trace_t	tr;
	float	dist, push;
	int		k;

	VectorCopy( f->dir, vel );

	// the pulse
	if ( s->deltaAlpha > 0 && ++f->alphaCount >= f->alphaWait ) {
		if ( !f->alphaUp ) {
			f->alpha -= s->deltaAlpha;
			if ( f->alpha < 0.01f ) {
				f->alphaUp = qtrue;
				f->alphaWait = 5 * f->personality;
			}
		} else {
			f->alpha += s->deltaAlpha;
			if ( f->alpha > s->alphaLevel ) {
				f->alphaUp = qfalse;
				f->alphaWait = 5 * f->personality;
			}
		}
		f->alphaCount = 0;
	}

	// between its two colours
	if ( !VectorCompare( s->color2, vec3_origin ) ) {
		for ( k = 0; k < 3; k++ ) {
			f->color[k] = f->colorDir ? s->color[k] + ( s->color2[k] - s->color[k] ) * f->colorLerp
									  : s->color2[k] + ( s->color[k] - s->color2[k] ) * f->colorLerp;
		}
		if ( f->colorLerp >= 1.0f ) {
			f->colorDir = !f->colorDir;
			f->colorLerp = 0.25f * f->personality;
		} else {
			f->colorLerp += 0.25f * f->personality;
		}
	}

	// back at its leash, out again near the middle
	dist = Distance( f->origin, s->origin );
	if ( dist >= s->leash ) {
		VectorSubtract( s->origin, f->origin, f->dir );
		VectorNormalize( f->dir );
		VectorScale( f->dir, s->velocity, f->dir );
		f->outward = qfalse;
		f->personality = CG_FireflyPersonality( f->personality );
	} else if ( !f->outward && dist <= 10 ) {
		VectorSet( f->dir, crandom(), crandom(), crandom() );
		VectorScale( f->dir, s->velocity, f->dir );
		f->outward = qtrue;
		f->personality = CG_FireflyPersonality( f->personality );
	}

	// the wobble
	push = s->velocity / ( 2 * f->personality );
	if ( random() > f->personality ) {
		vel[1] += ff_sin[( f->sine + 3 ) % 12] * push;
	} else {
		vel[0] += ff_sin[f->sine] * push;
	}
	vel[2] += ff_sin[f->sine] * push;
	f->sine = ( f->sine + 1 ) % 12;

	// a tenth of a second's flight; landing on something turns it back. In
	// something already (a swarm about a tree, in its clip), it flies on
	// through: they're only looks
	VectorCopy( f->origin, f->oldOrigin );
	VectorMA( f->origin, FIREFLY_STEP * 0.001f, vel, end );
	CG_Trace( &tr, f->origin, NULL, NULL, end, ENTITYNUM_NONE, MASK_SOLID );
	if ( tr.startsolid ) {
		VectorCopy( end, f->origin );
		return;
	}
	VectorCopy( tr.endpos, f->origin );
	if ( tr.fraction < 1.0f ) {
		VectorSubtract( s->origin, f->origin, f->dir );
		VectorNormalize( f->dir );
		VectorScale( f->dir, s->velocity, f->dir );
		f->outward = qfalse;
	}
}

/*
================
CG_AddFireflies

stepped to now, and drawn between their steps
================
*/
void CG_AddFireflies( void ) {
	fireflySwarm_t	*s;
	firefly_t		*f;
	refEntity_t		ent;
	float			frac;
	int				i, j;

	for ( i = 0; i < cg_numSwarms; i++ ) {
		s = &cg_swarms[i];
		if ( !s->nextStep || cg.time - s->nextStep > 1000 ) {
			s->nextStep = cg.time;		// first seen, or long unseen: no catching up
		}
		while ( cg.time >= s->nextStep ) {
			for ( j = 0; j < s->count; j++ ) {
				CG_FireflyStep( s, &s->flies[j] );
			}
			s->nextStep += FIREFLY_STEP;
		}
		frac = 1.0f - ( s->nextStep - cg.time ) / (float)FIREFLY_STEP;

		for ( j = 0; j < s->count; j++ ) {
			f = &s->flies[j];
			memset( &ent, 0, sizeof( ent ) );
			ent.reType = RT_SPRITE;
			VectorSubtract( f->origin, f->oldOrigin, ent.origin );
			VectorMA( f->oldOrigin, frac, ent.origin, ent.origin );
			ent.radius = 16 * s->scale;		// the flare's 32 pixels, at its scale
			ent.customShader = s->shader;
			ent.shaderRGBA[0] = Com_Clamp( 0, 1, f->color[0] ) * 255;
			ent.shaderRGBA[1] = Com_Clamp( 0, 1, f->color[1] ) * 255;
			ent.shaderRGBA[2] = Com_Clamp( 0, 1, f->color[2] ) * 255;
			ent.shaderRGBA[3] = Com_Clamp( 0, 1, f->alpha ) * 255;
			trap_R_AddRefEntityToScene( &ent );
		}
	}
}
