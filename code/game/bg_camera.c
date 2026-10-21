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
// bg_camera.c -- RTCW's spline cameras (splines/splines.cpp: idCameraDef,
// idSplineList, the camera and target positions), in C; and keyframes
//
// cameras/<name>.camera hold "cameraPathDef { ... }" in RTCW's syntax: the
// camera's path (camera_fixed, camera_interpolated or camera_spline), what it
// looks at (target_*, switched by events), events (waits, target switches,
// FOV changes, fades, a stop) and its FOV. Its path takes "time" seconds, its
// waits more.
//
// camera_keys is ours, for Daikatana's cinematics and for editing: keys the
// camera passes through at their times, its position and its angles each on a
// cubic Hermite curve, as Daikatana's shots were. A key may give its
// velocities (a converted shot gives them all, so it plays as it did); else
// they come from its neighbours, as Daikatana's editor made them: along the
// average of the two chords' directions, at a speed between the two
// segments', still at the ends.
//
// RTCW moved its camera a frame at a time (distance so far, plus speed by
// the time since the last), so its time only went forward. Here the view is a
// function of the time: the distance along a path is its speed integrated up
// to then. So the server and every client agree, a cutscene can be skipped
// to its end or played from its middle, and an editor can scrub it.

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_camera.h"

static cameraDef_t	cam_loaded[CAM_MAX_LOADED];
static int			cam_numLoaded;

#define CAM_STEP		10.0f		// ms: speed is integrated in steps of this
#define CAM_RAMP		1000.0f		// ms a wait or a feather slows down or speeds up over

/*
===============================================================================

THE CURVE

===============================================================================
*/

/*
================
BG_CameraSpline

RTCW's uniform cubic B-spline basis (idSplineList::calcSpline)
================
*/
static float BG_CameraSpline( int step, float t ) {
	switch ( step ) {
	case 0: return ( 1 - t ) * ( 1 - t ) * ( 1 - t ) / 6;
	case 1: return ( 3 * t * t * t - 6 * t * t + 4 ) / 6;
	case 2: return ( -3 * t * t * t + 3 * t * t + 3 * t + 1 ) / 6;
	case 3: return t * t * t / 6;
	}
	return 0;
}

/*
================
BG_BuildPosition

its curve sampled, and how far along it each sample is
================
*/
static void BG_BuildPosition( camPosition_t *pos ) {
	float	t, g;
	int		i, j;

	pos->numSamples = 0;
	switch ( pos->kind ) {
	case CAMPOS_FIXED:
		if ( pos->numPoints > 0 ) {
			VectorCopy( pos->points[0], pos->samples[0] );
			pos->numSamples = 1;
		}
		break;
	case CAMPOS_INTERPOLATED:
		for ( i = 0; i < pos->numPoints && i < 2; i++ ) {
			VectorCopy( pos->points[i], pos->samples[i] );
		}
		pos->numSamples = i;
		break;
	case CAMPOS_SPLINE:
		g = pos->granularity > 0.001f ? pos->granularity : 0.025f;
		for ( i = 3; i < pos->numPoints; i++ ) {
			for ( t = 0; t < 1.001f && pos->numSamples < CAM_MAX_SAMPLES; t += g ) {
				float *s = pos->samples[pos->numSamples++];

				VectorClear( s );
				for ( j = 0; j < 4; j++ ) {
					VectorMA( s, BG_CameraSpline( j, t ), pos->points[i - 3 + j], s );
				}
			}
		}
		// too few points for a spline: through them in lines
		if ( !pos->numSamples ) {
			for ( i = 0; i < pos->numPoints; i++ ) {
				VectorCopy( pos->points[i], pos->samples[pos->numSamples++] );
			}
		}
		break;
	}

	pos->length = 0;
	for ( i = 0; i < pos->numSamples; i++ ) {
		if ( i ) {
			pos->length += Distance( pos->samples[i - 1], pos->samples[i] );
		}
		pos->dist[i] = pos->length;
	}
}

/*
================
BG_KeyTangent

a component's velocity at key i (positions, or angles turned the short
way), from its neighbours: Daikatana's editor's rule
================
*/
static void BG_KeyTangent( const camKey_t *keys, int num, int i, qboolean angles, qboolean easeIn, qboolean easeOut, vec3_t out ) {
	vec3_t	prev, next, a, b;
	float	sp, sn, tp, tn;

	VectorClear( out );
	if ( num < 2 || ( i == 0 && easeIn ) || ( i == num - 1 && easeOut ) ) {
		return;
	}
	#define KEYV( k ) ( angles ? keys[k].unwrapped : keys[k].pos )
	if ( i > 0 ) {
		VectorSubtract( KEYV( i ), KEYV( i - 1 ), prev );
		tp = MAX( 0.001f, keys[i].time - keys[i - 1].time );
		sp = VectorLength( prev ) / tp;
	}
	if ( i < num - 1 ) {
		VectorSubtract( KEYV( i + 1 ), KEYV( i ), next );
		tn = MAX( 0.001f, keys[i + 1].time - keys[i].time );
		sn = VectorLength( next ) / tn;
	}
	#undef KEYV

	if ( i > 0 && i < num - 1 ) {
		VectorNormalize2( prev, a );
		VectorNormalize2( next, b );
		VectorAdd( a, b, out );
		VectorNormalize( out );
		// a speed between the two, nearer the slower
		VectorScale( out, sp + sn > 0 ? 2 * sp * sn / ( sp + sn ) : 0, out );
		return;
	}

	// an end that moves: its neighbour's direction mirrored across its chord
	{
		vec3_t	chord, dir;
		float	speed = i == 0 ? sn : sp;

		VectorNormalize2( i == 0 ? next : prev, chord );
		if ( num == 2 ) {
			VectorScale( chord, speed, out );
			return;
		}
		BG_KeyTangent( keys, num, i == 0 ? 1 : num - 2, angles, qfalse, qfalse, dir );
		VectorNormalize( dir );
		VectorScale( chord, 2 * DotProduct( dir, chord ), out );
		VectorSubtract( out, dir, out );
		VectorNormalize( out );
		VectorScale( out, speed, out );
	}
}

/*
================
BG_BuildKeys

its angles turned the short way, and every key's velocities
================
*/
static void BG_BuildKeys( camPosition_t *pos ) {
	camKey_t	*k;
	vec3_t		v;
	int			i, j;

	for ( i = 0; i < pos->numKeys; i++ ) {
		k = &pos->keys[i];
		VectorCopy( k->angles, k->unwrapped );
		if ( i ) {
			for ( j = 0; j < 3; j++ ) {
				while ( k->unwrapped[j] - pos->keys[i - 1].unwrapped[j] > 180 ) {
					k->unwrapped[j] -= 360;
				}
				while ( k->unwrapped[j] - pos->keys[i - 1].unwrapped[j] < -180 ) {
					k->unwrapped[j] += 360;
				}
			}
		}
	}
	for ( i = 0; i < pos->numKeys; i++ ) {
		k = &pos->keys[i];
		BG_KeyTangent( pos->keys, pos->numKeys, i, qfalse, pos->easeIn, pos->easeOut, v );
		VectorCopy( k->flags & CAMKEY_VEL ? k->vel : v, k->outVel );
		VectorCopy( k->flags & CAMKEY_VELIN ? k->velIn : k->outVel, k->inVel );
		BG_KeyTangent( pos->keys, pos->numKeys, i, qtrue, pos->easeIn, pos->easeOut, v );
		VectorCopy( k->flags & CAMKEY_AVEL ? k->avel : v, k->outAvel );
		VectorCopy( k->flags & CAMKEY_AVELIN ? k->avelIn : k->outAvel, k->inAvel );
	}
	pos->length = 0;
	pos->numSamples = 0;
}

/*
================
BG_KeysAt

where keyframes have the camera, and its angles, sec seconds in: the cubic
Hermite between the keys it's between
================
*/
static void BG_KeysAt( const camPosition_t *pos, float sec, vec3_t origin, vec3_t angles ) {
	const camKey_t	*a, *b;
	float			T, x, h00, h10, h01, h11;
	int				i, j;

	if ( !pos->numKeys ) {
		VectorClear( origin );
		VectorClear( angles );
		return;
	}
	if ( pos->numKeys == 1 || sec <= pos->keys[0].time ) {
		VectorCopy( pos->keys[0].pos, origin );
		VectorCopy( pos->keys[0].unwrapped, angles );
		return;
	}
	for ( i = 0; i < pos->numKeys - 2 && sec > pos->keys[i + 1].time; i++ ) {
	}
	a = &pos->keys[i];
	b = &pos->keys[i + 1];
	T = MAX( 0.001f, b->time - a->time );
	x = Com_Clamp( 0, 1, ( sec - a->time ) / T );
	h00 = 2 * x * x * x - 3 * x * x + 1;
	h10 = x * x * x - 2 * x * x + x;
	h01 = -2 * x * x * x + 3 * x * x;
	h11 = x * x * x - x * x;
	for ( j = 0; j < 3; j++ ) {
		origin[j] = h00 * a->pos[j] + h10 * T * a->outVel[j] + h01 * b->pos[j] + h11 * T * b->inVel[j];
		angles[j] = h00 * a->unwrapped[j] + h10 * T * a->outAvel[j] + h01 * b->unwrapped[j] + h11 * T * b->inAvel[j];
	}
}

/*
================
BG_KeysValue

a key value (FOV, fade) sec seconds in: from the last key that sets it to
the next, linearly; qfalse if no key sets it
================
*/
static qboolean BG_KeysValue( const camPosition_t *pos, int flag, float sec, float *out, int n ) {
	const camKey_t	*prev = NULL, *next = NULL;
	const float		*pv, *nv;
	float			f;
	int				i, j;

	for ( i = 0; i < pos->numKeys; i++ ) {
		if ( !( pos->keys[i].flags & flag ) ) {
			continue;
		}
		if ( pos->keys[i].time <= sec ) {
			prev = &pos->keys[i];
		} else {
			next = &pos->keys[i];
			break;
		}
	}
	if ( !prev && !next ) {
		return qfalse;
	}
	if ( !prev ) {
		prev = next;
	}
	if ( !next ) {
		next = prev;
	}
	pv = flag == CAMKEY_FOV ? &prev->fov : prev->fade;
	nv = flag == CAMKEY_FOV ? &next->fov : next->fade;
	f = next->time > prev->time ? Com_Clamp( 0, 1, ( sec - prev->time ) / ( next->time - prev->time ) ) : 0;
	for ( j = 0; j < n; j++ ) {
		out[j] = pv[j] + ( nv[j] - pv[j] ) * f;
	}
	return qtrue;
}

/*
================
BG_CameraFade

the colour the view is blended with, keyframes' fade sec ms in; qfalse if
it has none
================
*/
qboolean BG_CameraFade( const cameraDef_t *cam, float time, vec4_t fade ) {
	if ( cam->position.kind != CAMPOS_KEYS ) {
		return qfalse;
	}
	return BG_KeysValue( &cam->position, CAMKEY_FADE, time * 0.001f, fade, 4 );
}

/*
================
BG_CameraPace

how fast the camera's path goes at a time, from 0 (stopped) to 1, by its
waits and its feathering (RTCW ramped its speed a second into and out of
each)
================
*/
static float BG_CameraPace( const cameraDef_t *cam, float t ) {
	float	pace = 1, start, end;
	int		i;

	for ( i = 0; i < cam->numEvents; i++ ) {
		const camEvent_t *ev = &cam->events[i];

		if ( ev->type == CAMEV_WAIT ) {
			start = ev->time;
			end = start + atof( ev->param ) * 1000;
			if ( t >= start && t <= end ) {
				return 0;
			}
			if ( t < start && t > start - CAM_RAMP ) {
				pace *= ( start - t ) / CAM_RAMP;
			} else if ( t > end && t < end + CAM_RAMP ) {
				pace *= ( t - end ) / CAM_RAMP;
			}
		} else if ( ev->type == CAMEV_FEATHER ) {
			if ( t < CAM_RAMP ) {
				pace *= t / CAM_RAMP;
			} else if ( t > cam->totalTime - CAM_RAMP ) {
				pace *= MAX( 0, cam->totalTime - t ) / CAM_RAMP;
			}
		}
	}
	return pace;
}

/*
================
BG_PositionSpeed

its speed (units a second), time ms into it: a velocity window's, else its
base speed, else its length over its time
================
*/
static float BG_PositionSpeed( const camPosition_t *pos, float t ) {
	int i;

	for ( i = pos->numVelocities - 1; i >= 0; i-- ) {
		const camVelocity_t *v = &pos->velocities[i];

		if ( t >= v->start && t < v->start + v->duration ) {
			return v->speed;
		}
	}
	if ( pos->baseVelocity > 0 ) {
		return pos->baseVelocity;
	}
	return pos->time > 0 ? pos->length / ( pos->time * 0.001f ) : 0;
}

/*
================
BG_CameraPosition

where a position is at a time (ms into the camera): as far along its curve as
its speed has taken it since it started
================
*/
void BG_CameraPosition( const camPosition_t *pos, const cameraDef_t *cam, float time, vec3_t out ) {
	float	t, local, d, step, frac;
	int		lo, hi, mid;

	if ( pos->kind == CAMPOS_KEYS ) {
		vec3_t angles;

		BG_KeysAt( pos, ( time - pos->startTime ) * 0.001f, out, angles );
		return;
	}
	if ( pos->numSamples <= 1 ) {
		if ( pos->numSamples ) {
			VectorCopy( pos->samples[0], out );
		} else {
			VectorClear( out );
		}
		return;
	}

	// the distance: its speed (paced by the camera's waits, if it's the
	// camera's own path) integrated
	local = MAX( 0, time - pos->startTime );
	d = 0;
	for ( t = 0; t < local && d < pos->length; t += CAM_STEP ) {
		step = MIN( CAM_STEP, local - t );
		frac = pos == &cam->position ? BG_CameraPace( cam, pos->startTime + t + step * 0.5f ) : 1;
		d += BG_PositionSpeed( pos, t ) * frac * step * 0.001f;
	}
	if ( d >= pos->length ) {
		VectorCopy( pos->samples[pos->numSamples - 1], out );
		return;
	}

	// the samples it's between
	lo = 0;
	hi = pos->numSamples - 1;
	while ( hi - lo > 1 ) {
		mid = ( lo + hi ) / 2;
		if ( pos->dist[mid] <= d ) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	frac = pos->dist[hi] > pos->dist[lo] ? ( d - pos->dist[lo] ) / ( pos->dist[hi] - pos->dist[lo] ) : 0;
	VectorSubtract( pos->samples[hi], pos->samples[lo], out );
	VectorMA( pos->samples[lo], frac, out, out );
}

/*
===============================================================================

THE CAMERA

===============================================================================
*/

/*
================
BG_BuildCamera

its curves, its length (its path's time and its waits, to its end or its
stop), and when each target is looked at: from its switch to the next
(idCameraDef::buildCamera)
================
*/
void BG_BuildCamera( cameraDef_t *cam ) {
	float	total = cam->baseTime * 1000, from, to;
	int		i, j, k;

	// in time order
	for ( i = 1; i < cam->numEvents; i++ ) {
		camEvent_t ev = cam->events[i];

		for ( j = i - 1; j >= 0 && cam->events[j].time > ev.time; j-- ) {
			cam->events[j + 1] = cam->events[j];
		}
		cam->events[j + 1] = ev;
	}

	for ( i = 0; i < cam->numEvents; i++ ) {
		if ( cam->events[i].type == CAMEV_WAIT ) {
			total += atof( cam->events[i].param ) * 1000;
		}
	}
	cam->totalTime = total;
	for ( i = 0; i < cam->numEvents; i++ ) {
		if ( cam->events[i].type == CAMEV_STOP && cam->events[i].time < cam->totalTime ) {
			cam->totalTime = cam->events[i].time;
		}
	}

	if ( cam->position.kind == CAMPOS_KEYS ) {
		BG_BuildKeys( &cam->position );
		if ( cam->position.numKeys ) {
			total = MAX( total, cam->position.keys[cam->position.numKeys - 1].time * 1000 );
			cam->baseTime = MAX( cam->baseTime, cam->position.keys[cam->position.numKeys - 1].time );
			cam->totalTime = MAX( cam->totalTime, total );
		}
	} else {
		BG_BuildPosition( &cam->position );
	}
	cam->position.startTime = 0;
	cam->position.time = cam->baseTime * 1000;

	for ( k = 0; k < cam->numTargets; k++ ) {
		camPosition_t *target = &cam->targets[k];

		if ( target->kind == CAMPOS_KEYS ) {
			BG_BuildKeys( target );
		} else {
			BG_BuildPosition( target );
		}
		// the first target, unless an event switches to another first
		target->startTime = 0;
		target->time = total;
		for ( i = 0; i < cam->numEvents; i++ ) {
			if ( cam->events[i].type != CAMEV_TARGET || Q_stricmp( cam->events[i].param, target->name ) ) {
				continue;
			}
			from = cam->events[i].time;
			to = total;
			for ( j = i + 1; j < cam->numEvents; j++ ) {
				if ( cam->events[j].type == CAMEV_TARGET ) {
					to = cam->events[j].time;
					break;
				}
			}
			target->startTime = from;
			target->time = MAX( 1, to - from );
			break;
		}
	}
}

/*
================
BG_CameraTarget

what it looks at, time ms into it: the last target switched to, else its
first; NULL if it has none
================
*/
static const camPosition_t *BG_CameraTarget( const cameraDef_t *cam, float time ) {
	const camPosition_t	*target = cam->numTargets ? &cam->targets[0] : NULL;
	int					i, k;

	for ( i = 0; i < cam->numEvents && cam->events[i].time <= time; i++ ) {
		if ( cam->events[i].type != CAMEV_TARGET ) {
			continue;
		}
		for ( k = 0; k < cam->numTargets; k++ ) {
			if ( !Q_stricmp( cam->targets[k].name, cam->events[i].param ) ) {
				target = &cam->targets[k];
			}
		}
	}
	return target;
}

/*
================
BG_CameraFov

its FOV, time ms into it: its "fov" block's, then each FOV event's change
from what it was then to its new FOV over its seconds
================
*/
static float BG_CameraFov( const cameraDef_t *cam, float time ) {
	float	from, to, start, len, fov;
	int		i;

	from = cam->fovStart;
	to = cam->fovEnd;
	start = 0;
	len = cam->fovTime;
	if ( from <= 0 || to <= 0 ) {
		from = to = cam->fov > 0 ? cam->fov : 90;
	}
	for ( i = 0; i < cam->numEvents && cam->events[i].time <= time; i++ ) {
		char	buf[64], *p = buf, *tok;
		float	nowFov;

		if ( cam->events[i].type != CAMEV_FOV ) {
			continue;
		}
		nowFov = len > 0 ? from + ( to - from ) * Com_Clamp( 0, 1, ( cam->events[i].time - start ) / len ) : to;
		Q_strncpyz( buf, cam->events[i].param, sizeof( buf ) );
		tok = COM_Parse( &p );
		fov = tok[0] ? atof( tok ) : 90;
		tok = COM_Parse( &p );
		from = nowFov;
		to = fov;
		start = cam->events[i].time;
		len = tok[0] ? atof( tok ) * 1000 : 0;
	}
	return len > 0 ? from + ( to - from ) * Com_Clamp( 0, 1, ( time - start ) / len ) : to;
}

/*
================
BG_CameraView

the view time ms into the camera: where it is, which way it looks (at its
target, or its last way), its FOV; qfalse once it's over
================
*/
qboolean BG_CameraView( const cameraDef_t *cam, float time, vec3_t origin, vec3_t angles, float *fov ) {
	const camPosition_t	*target;
	vec3_t				at, dir;

	VectorClear( angles );
	if ( cam->position.kind == CAMPOS_KEYS ) {
		BG_KeysAt( &cam->position, time * 0.001f, origin, angles );
	} else {
		BG_CameraPosition( &cam->position, cam, time, origin );
	}
	target = BG_CameraTarget( cam, time );
	if ( target ) {
		BG_CameraPosition( target, cam, time, at );
		VectorSubtract( at, origin, dir );
		if ( VectorNormalize( dir ) > 0.001f ) {
			vectoangles( dir, angles );
		}
	}
	if ( fov ) {
		*fov = BG_CameraFov( cam, time );
		if ( cam->position.kind == CAMPOS_KEYS ) {
			BG_KeysValue( &cam->position, CAMKEY_FOV, time * 0.001f, fov, 1 );
		}
	}
	return time <= cam->totalTime;
}

/*
================
BG_CameraEvents

its events from (excluding) to (including), ms into it: for what happens
outside its view (fades, another camera)
================
*/
int BG_CameraEvents( const cameraDef_t *cam, float from, float to, const camEvent_t **list, int max ) {
	int i, num = 0;

	for ( i = 0; i < cam->numEvents && num < max; i++ ) {
		if ( cam->events[i].time > from && cam->events[i].time <= to ) {
			list[num++] = &cam->events[i];
		}
	}
	return num;
}

/*
===============================================================================

THE FILES

===============================================================================
*/

/*
================
BG_ParsePosition

a position's block (idCameraPosition::parseToken and its kinds' parse)
================
*/
static qboolean BG_ParsePosition( camPosition_t *pos, camPosKind_t kind, char **p ) {
	char *tok;

	memset( pos, 0, sizeof( *pos ) );
	pos->kind = kind;
	pos->granularity = 0.025f;
	pos->easeIn = pos->easeOut = qtrue;
	tok = COM_Parse( p );
	if ( strcmp( tok, "{" ) ) {
		return qfalse;
	}
	while ( 1 ) {
		tok = COM_Parse( p );
		if ( !tok[0] ) {
			return qfalse;
		}
		if ( !strcmp( tok, "}" ) ) {
			return qtrue;
		}
		if ( !Q_stricmp( tok, "name" ) ) {
			Q_strncpyz( pos->name, COM_Parse( p ), sizeof( pos->name ) );
		} else if ( !Q_stricmp( tok, "time" ) || !Q_stricmp( tok, "type" ) ) {
			COM_Parse( p );		// (RTCW's: the camera sets them)
		} else if ( !Q_stricmp( tok, "baseVelocity" ) ) {
			pos->baseVelocity = atof( COM_Parse( p ) );
		} else if ( !Q_stricmp( tok, "velocity" ) ) {
			if ( pos->numVelocities < CAM_MAX_VELOCITIES ) {
				camVelocity_t *v = &pos->velocities[pos->numVelocities++];

				v->start = atof( COM_Parse( p ) );
				v->duration = atof( COM_Parse( p ) );
				v->speed = atof( COM_Parse( p ) );
			}
		} else if ( !Q_stricmp( tok, "easeIn" ) ) {
			pos->easeIn = atoi( COM_Parse( p ) ) != 0;
		} else if ( !Q_stricmp( tok, "easeOut" ) ) {
			pos->easeOut = atoi( COM_Parse( p ) ) != 0;
		} else if ( !Q_stricmp( tok, "key" ) ) {
			camKey_t skip, *k = pos->numKeys < CAM_MAX_KEYS ? &pos->keys[pos->numKeys++] : &skip;

			memset( k, 0, sizeof( *k ) );
			if ( strcmp( COM_Parse( p ), "{" ) ) {
				return qfalse;
			}
			while ( ( tok = COM_Parse( p ) )[0] && strcmp( tok, "}" ) ) {
				if ( !Q_stricmp( tok, "time" ) ) {
					k->time = atof( COM_Parse( p ) );
				} else if ( !Q_stricmp( tok, "pos" ) ) {
					Parse1DMatrix( p, 3, k->pos );
				} else if ( !Q_stricmp( tok, "angles" ) ) {
					Parse1DMatrix( p, 3, k->angles );
				} else if ( !Q_stricmp( tok, "vel" ) ) {
					Parse1DMatrix( p, 3, k->vel );
					k->flags |= CAMKEY_VEL;
				} else if ( !Q_stricmp( tok, "velIn" ) ) {
					Parse1DMatrix( p, 3, k->velIn );
					k->flags |= CAMKEY_VELIN;
				} else if ( !Q_stricmp( tok, "avel" ) ) {
					Parse1DMatrix( p, 3, k->avel );
					k->flags |= CAMKEY_AVEL;
				} else if ( !Q_stricmp( tok, "avelIn" ) ) {
					Parse1DMatrix( p, 3, k->avelIn );
					k->flags |= CAMKEY_AVELIN;
				} else if ( !Q_stricmp( tok, "fov" ) ) {
					k->fov = atof( COM_Parse( p ) );
					k->flags |= CAMKEY_FOV;
				} else if ( !Q_stricmp( tok, "fade" ) ) {
					Parse1DMatrix( p, 4, k->fade );
					k->flags |= CAMKEY_FADE;
				}
			}
		} else if ( !Q_stricmp( tok, "pos" ) || !Q_stricmp( tok, "startPos" ) || !Q_stricmp( tok, "endPos" ) ) {
			int i = !Q_stricmp( tok, "endPos" ) ? 1 : 0;

			Parse1DMatrix( p, 3, pos->points[i] );
			pos->numPoints = MAX( pos->numPoints, i + 1 );
		} else if ( !Q_stricmp( tok, "target" ) ) {
			// a spline's control points
			tok = COM_Parse( p );
			if ( strcmp( tok, "{" ) ) {
				return qfalse;
			}
			while ( 1 ) {
				tok = COM_Parse( p );
				if ( !tok[0] ) {
					return qfalse;
				}
				if ( !strcmp( tok, "}" ) ) {
					break;
				}
				if ( !Q_stricmp( tok, "granularity" ) ) {
					pos->granularity = atof( COM_Parse( p ) );
				} else if ( !strcmp( tok, "(" ) ) {
					vec3_t	skip;
					float	*v = pos->numPoints < CAM_MAX_POINTS ? pos->points[pos->numPoints++] : skip;
					int		k;

					for ( k = 0; k < 3; k++ ) {
						v[k] = atof( COM_Parse( p ) );
					}
					COM_MatchToken( p, ")" );
				}
			}
		}
	}
}

/*
================
BG_ParseCamera

a cameraPathDef (idCameraDef::parse), built
================
*/
qboolean BG_ParseCamera( cameraDef_t *cam, const char *name, char *text ) {
	char	*p = text, *tok;
	int		kind;

	memset( cam, 0, sizeof( *cam ) );
	Q_strncpyz( cam->name, name, sizeof( cam->name ) );
	cam->fov = 90;
	COM_BeginParseSession( name );
	tok = COM_Parse( &p );
	if ( Q_stricmp( tok, "cameraPathDef" ) || strcmp( COM_Parse( &p ), "{" ) ) {
		Com_Printf( S_COLOR_YELLOW "camera %s: no cameraPathDef\n", name );
		return qfalse;
	}
	while ( 1 ) {
		tok = COM_Parse( &p );
		if ( !tok[0] || !strcmp( tok, "}" ) ) {
			break;
		}
		kind = -1;
		if ( !Q_stricmpn( tok, "camera_", 7 ) || !Q_stricmpn( tok, "target_", 7 ) ) {
			if ( !Q_stricmp( tok + 7, "fixed" ) ) {
				kind = CAMPOS_FIXED;
			} else if ( !Q_stricmp( tok + 7, "interpolated" ) ) {
				kind = CAMPOS_INTERPOLATED;
			} else if ( !Q_stricmp( tok + 7, "spline" ) ) {
				kind = CAMPOS_SPLINE;
			} else if ( !Q_stricmp( tok + 7, "keys" ) ) {
				kind = CAMPOS_KEYS;
			}
		}
		if ( !Q_stricmp( tok, "time" ) ) {
			cam->baseTime = atof( COM_Parse( &p ) );
		} else if ( kind >= 0 && tok[0] == 'c' ) {
			if ( !BG_ParsePosition( &cam->position, kind, &p ) ) {
				break;
			}
		} else if ( kind >= 0 ) {
			camPosition_t skip, *pos = cam->numTargets < CAM_MAX_TARGETS ? &cam->targets[cam->numTargets++] : &skip;

			if ( !BG_ParsePosition( pos, kind, &p ) ) {
				break;
			}
		} else if ( !Q_stricmp( tok, "event" ) ) {
			camEvent_t skip, *ev = cam->numEvents < CAM_MAX_EVENTS ? &cam->events[cam->numEvents++] : &skip;

			memset( ev, 0, sizeof( *ev ) );
			if ( strcmp( COM_Parse( &p ), "{" ) ) {
				break;
			}
			while ( ( tok = COM_Parse( &p ) )[0] && strcmp( tok, "}" ) ) {
				if ( !Q_stricmp( tok, "type" ) ) {
					ev->type = Com_Clamp( 0, CAMEV_COUNT - 1, atoi( COM_Parse( &p ) ) );
				} else if ( !Q_stricmp( tok, "param" ) ) {
					Q_strncpyz( ev->param, COM_Parse( &p ), sizeof( ev->param ) );
				} else if ( !Q_stricmp( tok, "time" ) ) {
					ev->time = atof( COM_Parse( &p ) );
				}
			}
		} else if ( !Q_stricmp( tok, "fov" ) ) {
			if ( strcmp( COM_Parse( &p ), "{" ) ) {
				break;
			}
			while ( ( tok = COM_Parse( &p ) )[0] && strcmp( tok, "}" ) ) {
				if ( !Q_stricmp( tok, "fov" ) ) {
					cam->fov = atof( COM_Parse( &p ) );
				} else if ( !Q_stricmp( tok, "startFOV" ) ) {
					cam->fovStart = atof( COM_Parse( &p ) );
				} else if ( !Q_stricmp( tok, "endFOV" ) ) {
					cam->fovEnd = atof( COM_Parse( &p ) );
				} else if ( !Q_stricmp( tok, "time" ) ) {
					cam->fovTime = atof( COM_Parse( &p ) );
				}
			}
		} else {
			Com_Printf( S_COLOR_YELLOW "camera %s: unknown \"%s\"\n", name, tok );
		}
	}
	if ( strcmp( tok, "}" ) ) {
		Com_Printf( S_COLOR_YELLOW "camera %s: bad syntax near \"%s\"\n", name, tok );
		return qfalse;
	}
	if ( !cam->position.numPoints && !cam->position.numKeys ) {
		Com_Printf( S_COLOR_YELLOW "camera %s: no camera position\n", name );
		return qfalse;
	}
	BG_BuildCamera( cam );
	return qtrue;
}

/*
================
BG_LoadCamera

cameras/<name>.camera, loaded once (BG_FlushCameras forgets them, for an
edited one)
================
*/
cameraDef_t *BG_LoadCamera( const char *name ) {
	cameraDef_t	*cam;
	char		path[MAX_QPATH];
	void		*text;
	int			i;

	for ( i = 0; i < cam_numLoaded; i++ ) {
		if ( !Q_stricmp( cam_loaded[i].name, name ) ) {
			return &cam_loaded[i];
		}
	}
	Com_sprintf( path, sizeof( path ), "cameras/%s.camera", name );
	if ( BG_FS_ReadFile( path, &text ) <= 0 || !text ) {
		Com_Printf( S_COLOR_YELLOW "camera %s: no %s\n", name, path );
		return NULL;
	}
	// the oldest goes, if it's full
	if ( cam_numLoaded == CAM_MAX_LOADED ) {
		memmove( &cam_loaded[0], &cam_loaded[1], sizeof( cam_loaded[0] ) * ( CAM_MAX_LOADED - 1 ) );
		cam_numLoaded--;
	}
	cam = &cam_loaded[cam_numLoaded];
	if ( !BG_ParseCamera( cam, name, (char *)text ) ) {
		BG_FS_FreeFile( text );
		return NULL;
	}
	BG_FS_FreeFile( text );
	cam_numLoaded++;
	return cam;
}

void BG_FlushCameras( void ) {
	cam_numLoaded = 0;
}

/*
================
BG_WritePosition
================
*/
static int BG_WritePosition( const camPosition_t *pos, const char *what, char *buf, int size ) {
	static const char *kinds[] = { "fixed", "interpolated", "spline", "keys" };
	int i, len = strlen( buf );

	Q_strcat( buf, size, va( "\t%s_%s {\n\t\tname %s\n", what, kinds[pos->kind], pos->name[0] ? pos->name : "-" ) );
	if ( pos->baseVelocity > 0 ) {
		Q_strcat( buf, size, va( "\t\tbaseVelocity %f\n", pos->baseVelocity ) );
	}
	for ( i = 0; i < pos->numVelocities; i++ ) {
		Q_strcat( buf, size, va( "\t\tvelocity %.0f %.0f %f\n", pos->velocities[i].start, pos->velocities[i].duration, pos->velocities[i].speed ) );
	}
	switch ( pos->kind ) {
	case CAMPOS_FIXED:
		Q_strcat( buf, size, va( "\t\tpos ( %f %f %f )\n", pos->points[0][0], pos->points[0][1], pos->points[0][2] ) );
		break;
	case CAMPOS_INTERPOLATED:
		Q_strcat( buf, size, va( "\t\tstartPos ( %f %f %f )\n", pos->points[0][0], pos->points[0][1], pos->points[0][2] ) );
		Q_strcat( buf, size, va( "\t\tendPos ( %f %f %f )\n", pos->points[1][0], pos->points[1][1], pos->points[1][2] ) );
		break;
	case CAMPOS_SPLINE:
		Q_strcat( buf, size, va( "\t\ttarget {\n\t\t\tgranularity %f\n", pos->granularity ) );
		for ( i = 0; i < pos->numPoints; i++ ) {
			Q_strcat( buf, size, va( "\t\t\t( %f %f %f )\n", pos->points[i][0], pos->points[i][1], pos->points[i][2] ) );
		}
		Q_strcat( buf, size, "\t\t}\n" );
		break;
	case CAMPOS_KEYS:
		if ( !pos->easeIn ) {
			Q_strcat( buf, size, "\t\teaseIn 0\n" );
		}
		if ( !pos->easeOut ) {
			Q_strcat( buf, size, "\t\teaseOut 0\n" );
		}
		for ( i = 0; i < pos->numKeys; i++ ) {
			const camKey_t *k = &pos->keys[i];

			Q_strcat( buf, size, va( "\t\tkey { time %g pos ( %g %g %g ) angles ( %g %g %g )", k->time,
									 k->pos[0], k->pos[1], k->pos[2], k->angles[0], k->angles[1], k->angles[2] ) );
			if ( k->flags & CAMKEY_VEL ) {
				Q_strcat( buf, size, va( " vel ( %g %g %g )", k->vel[0], k->vel[1], k->vel[2] ) );
			}
			if ( k->flags & CAMKEY_VELIN ) {
				Q_strcat( buf, size, va( " velIn ( %g %g %g )", k->velIn[0], k->velIn[1], k->velIn[2] ) );
			}
			if ( k->flags & CAMKEY_AVEL ) {
				Q_strcat( buf, size, va( " avel ( %g %g %g )", k->avel[0], k->avel[1], k->avel[2] ) );
			}
			if ( k->flags & CAMKEY_AVELIN ) {
				Q_strcat( buf, size, va( " avelIn ( %g %g %g )", k->avelIn[0], k->avelIn[1], k->avelIn[2] ) );
			}
			if ( k->flags & CAMKEY_FOV ) {
				Q_strcat( buf, size, va( " fov %g", k->fov ) );
			}
			if ( k->flags & CAMKEY_FADE ) {
				Q_strcat( buf, size, va( " fade ( %g %g %g %g )", k->fade[0], k->fade[1], k->fade[2], k->fade[3] ) );
			}
			Q_strcat( buf, size, " }\n" );
		}
		break;
	}
	Q_strcat( buf, size, "\t}\n" );
	return strlen( buf ) - len;
}

/*
================
BG_WriteCamera

the camera as its file's text (idCameraDef::save), for an editor; the
length written
================
*/
int BG_WriteCamera( const cameraDef_t *cam, char *buf, int size ) {
	int i;

	buf[0] = 0;
	Q_strcat( buf, size, va( "cameraPathDef {\n\ttime %f\n", cam->baseTime ) );
	BG_WritePosition( &cam->position, "camera", buf, size );
	for ( i = 0; i < cam->numTargets; i++ ) {
		BG_WritePosition( &cam->targets[i], "target", buf, size );
	}
	for ( i = 0; i < cam->numEvents; i++ ) {
		Q_strcat( buf, size, va( "\tevent {\n\t\ttype %d\n\t\tparam \"%s\"\n\t\ttime %.0f\n\t}\n",
								 cam->events[i].type, cam->events[i].param, cam->events[i].time ) );
	}
	Q_strcat( buf, size, va( "\tfov {\n\t\tfov %f\n\t\tstartFOV %f\n\t\tendFOV %f\n\t\ttime %.0f\n\t}\n}\n",
							 cam->fov, cam->fovStart, cam->fovEnd, cam->fovTime ) );
	return strlen( buf );
}
