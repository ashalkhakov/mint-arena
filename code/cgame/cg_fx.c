/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.
Copyright (C) 2000-2013 Raven Software, Jedi Knight Galaxies, OpenJK contributors

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
// cg_fx.c -- data driven effects, a C version of the OpenJK effects system
//
// Effects are text files, effects/<name>.efx, in the format Jedi Academy
// uses: a list of primitives (particles, lines, lights, sounds...) each with
// ranges for count, life, motion, colour, alpha and size, and the curves
// those follow over the primitive's life.
//
// The cgame registers an effect once and plays it by id. Everything lives in
// fixed pools so this runs in the QVM too.

#include "cg_local.h"

#define MAX_FX_TEMPLATES		256		// effect files
#define MAX_FX_PRIMS			1024	// primitives of all effect files
#define MAX_FX_PRIMS_PER_FILE	24
#define MAX_FX_MEDIA			4096	// shader, model, sound and effect handles
#define MAX_FX_SCHEDULED		1024	// delayed primitives
#define MAX_FX_EFFECTS			1800	// live primitives
#define MAX_FX_FILE				65536
#define MAX_FX_PENDING			64		// effect names one file refers to

#define FX_MAX_TRACE_DIST		16384
#define FX_EMIT_RATE			12		// emitters think at about 80 Hz

// primitive types, the order of fx_primNames
typedef enum {
	FXP_PARTICLE,
	FXP_LINE,
	FXP_TAIL,
	FXP_SOUND,
	FXP_CYLINDER,
	FXP_ELECTRICITY,
	FXP_EMITTER,
	FXP_DECAL,
	FXP_ORIENTEDPARTICLE,
	FXP_RUNNER,
	FXP_LIGHT,
	FXP_CAMERASHAKE,
	FXP_FLASH,

	FXP_NUM
} fxPrimType_t;

static const char *fx_primNames[FXP_NUM] = {
	"particle", "line", "tail", "sound", "cylinder", "electricity", "emitter",
	"decal", "orientedparticle", "fxrunner", "light", "cameraShake", "flash"
};

// how size, rgb, alpha, length and size2 change over a primitive's life,
// 4 bits each at these shifts (OpenJK's values)
#define FX_LINEAR				0x1
#define FX_RAND					0x2
#define FX_NONLINEAR			0x4
#define FX_WAVE					0x8
#define FX_CLAMP				0xC
#define FX_PARM_MASK			0xC

#define FX_ALPHA_SHIFT			0
#define FX_RGB_SHIFT			4
#define FX_SIZE_SHIFT			8
#define FX_LENGTH_SHIFT			12
#define FX_SIZE2_SHIFT			16

// primitive flags
#define FX_PAPER_PHYSICS		0x00010000	// emitters only, shared with size2
#define FX_GROWING				0x00020000	// electricity only, shared with size2: it reaches its end as it goes
#define FX_DEPTH_HACK			0x00100000
#define FX_RELATIVE				0x00200000
#define FX_SET_SHADER_TIME		0x00400000
#define FX_EXPENSIVE_PHYSICS	0x00800000
#define FX_ATTACHED_MODEL		0x01000000
#define FX_APPLY_PHYSICS		0x02000000
#define FX_USE_BBOX				0x04000000
#define FX_USE_ALPHA			0x08000000
#define FX_EMIT_FX				0x10000000
#define FX_DEATH_RUNS_FX		0x20000000
#define FX_KILL_ON_IMPACT		0x40000000
#define FX_IMPACT_RUNS_FX		0x80000000

// spawn flags
#define FX_ORG_ON_SPHERE		0x00001
#define FX_AXIS_FROM_SPHERE		0x00002
#define FX_ORG_ON_CYLINDER		0x00004
#define FX_ORG2_FROM_TRACE		0x00010
#define FX_TRACE_IMPACT_FX		0x00020
#define FX_ORG2_IS_OFFSET		0x00040
#define FX_CHEAP_ORG_CALC		0x00100
#define FX_CHEAP_ORG2_CALC		0x00200
#define FX_VEL_IS_ABSOLUTE		0x00400
#define FX_ACCEL_IS_ABSOLUTE	0x00800
#define FX_RAND_ROT_AROUND_FWD	0x01000
#define FX_EVEN_DISTRIBUTION	0x02000
#define FX_RGB_COMPONENT_INTERP	0x04000
#define FX_AFFECTED_BY_WIND		0x10000

typedef struct {
	const char	*name;
	int			bits;
} fxFlagName_t;

static const fxFlagName_t fx_flagNames[] = {
	{ "useModel",			FX_ATTACHED_MODEL },
	{ "useBBox",			FX_USE_BBOX },
	{ "usePhysics",			FX_APPLY_PHYSICS },
	{ "expensivePhysics",	FX_EXPENSIVE_PHYSICS },
	{ "ghoul2Collision",	FX_APPLY_PHYSICS | FX_EXPENSIVE_PHYSICS },	// no Ghoul2, plain traces
	{ "ghoul2Decals",		0 },
	{ "impactKills",		FX_KILL_ON_IMPACT },
	{ "impactFx",			FX_IMPACT_RUNS_FX },
	{ "deathFx",			FX_DEATH_RUNS_FX },
	{ "useAlpha",			FX_USE_ALPHA },
	{ "growing",			FX_GROWING },
	{ "emitFx",				FX_EMIT_FX },
	{ "depthHack",			FX_DEPTH_HACK },
	{ "relative",			FX_RELATIVE },
	{ "setShaderTime",		FX_SET_SHADER_TIME },
	{ "paperPhysics",		FX_PAPER_PHYSICS },
	{ "localizedFlash",		0 },
	{ "playerView",			0 },
	{ NULL, 0 }
};

static const fxFlagName_t fx_spawnFlagNames[] = {
	{ "org2fromTrace",				FX_ORG2_FROM_TRACE },
	{ "traceImpactFx",				FX_TRACE_IMPACT_FX },
	{ "org2isOffset",				FX_ORG2_IS_OFFSET },
	{ "cheapOrgCalc",				FX_CHEAP_ORG_CALC },
	{ "cheapOrg2Calc",				FX_CHEAP_ORG2_CALC },
	{ "absoluteVel",				FX_VEL_IS_ABSOLUTE },
	{ "absoluteAccel",				FX_ACCEL_IS_ABSOLUTE },
	{ "orgOnSphere",				FX_ORG_ON_SPHERE },
	{ "orgOnCylinder",				FX_ORG_ON_CYLINDER },
	{ "axisFromSphere",				FX_AXIS_FROM_SPHERE },
	{ "randrotaroundfwd",			FX_RAND_ROT_AROUND_FWD },
	{ "evenDistribution",			FX_EVEN_DISTRIBUTION },
	{ "rgbComponentInterpolation",	FX_RGB_COMPONENT_INTERP },
	{ "affectedByWind",				FX_AFFECTED_BY_WIND },
	{ NULL, 0 }
};

static const fxFlagName_t fx_curveFlagNames[] = {
	{ "linear",		FX_LINEAR },
	{ "nonlinear",	FX_NONLINEAR },
	{ "wave",		FX_WAVE },
	{ "random",		FX_RAND },
	{ "clamp",		FX_CLAMP },
	{ NULL, 0 }
};

typedef struct {
	float	min, max;
} fxRange_t;

typedef struct {
	short	first, count;
} fxMedia_t;

// how one value changes over the life of a primitive
typedef struct {
	fxRange_t	start, end, parm;
} fxCurve_t;

typedef struct {
	fxPrimType_t	type;
	int				flags, spawnFlags;

	fxRange_t		count, life, delay;
	fxRange_t		elasticity;
	vec3_t			mins, maxs;
	fxRange_t		origin1[3], origin2[3];
	fxRange_t		radius, height;
	fxRange_t		rotation, rotationDelta;
	fxRange_t		angle[3], angleDelta[3];
	fxRange_t		vel[3], accel[3];
	fxRange_t		gravity;
	fxRange_t		density, variance;

	fxRange_t		rgbStart[3], rgbEnd[3], rgbParm;
	fxCurve_t		alpha, size, size2, length;

	fxMedia_t		media;			// shaders, models or sounds
	fxMedia_t		impactFx, deathFx, emitFx, playFx;
} fxPrimTemplate_t;

typedef struct {
	char			name[MAX_QPATH];
	int				firstPrim, numPrims;
} fxEffectTemplate_t;

typedef struct {
	qboolean		inUse;
	int				prim;
	int				startTime;
	vec3_t			origin;
	vec3_t			axis[3];
} fxScheduled_t;

typedef struct fxEffect_s {
	qboolean		inUse;
	fxPrimType_t	type;
	int				flags;
	int				timeStart, timeEnd;

	vec3_t			origin, origin2;
	vec3_t			vel, accel;
	vec3_t			axis[3];		// oriented particles: axis[0] is the normal; emitters: model axis
	vec3_t			mins, maxs;
	float			elasticity;

	vec3_t			rgbStart, rgbEnd;
	float			rgbParm;
	float			alphaStart, alphaEnd, alphaParm;
	float			sizeStart, sizeEnd, sizeParm;
	float			lengthStart, lengthEnd, lengthParm;
	float			rotation, rotationDelta;

	qhandle_t		media;			// shader or model
	int				impactFx, deathFx, emitFx;

	// emitters
	vec3_t			angles, angleDelta;
	float			density, variance;
	int				oldTime;
	vec3_t			oldOrigin, oldVel;
} fxEffect_t;

typedef struct {
	int					numTemplates;
	fxEffectTemplate_t	templates[MAX_FX_TEMPLATES];
	int					numPrims;
	fxPrimTemplate_t	prims[MAX_FX_PRIMS];
	int					numMedia;
	int					media[MAX_FX_MEDIA];

	fxScheduled_t		scheduled[MAX_FX_SCHEDULED];
	fxEffect_t			effects[MAX_FX_EFFECTS];
	int					numActive;		// effects[] in use are below this

	int					time, oldTime, frameTime;
	float				realTime;		// frame time in seconds
	qboolean			warned[FXP_NUM];
} fxSystem_t;

static fxSystem_t	fx;
static char			fx_fileBuffer[MAX_FX_FILE];

static void FX_CreatePrim( const fxPrimTemplate_t *prim, const vec3_t origin, vec3_t axis[3], int lateTime );


/*
===============================================================================

RANGES AND CURVES

===============================================================================
*/

static float FX_RandFloat( float min, float max ) {
	return min + random() * ( max - min );
}

static float FX_RangeVal( const fxRange_t *r ) {
	if ( r->min == r->max ) {
		return r->min;
	}
	return FX_RandFloat( r->min, r->max );
}

static void FX_SetRange( fxRange_t *r, float v ) {
	r->min = r->max = v;
}

/*
==================
FX_CurvePerc

How far between start (1) and end (0) a value is now, for the curve bits of
one channel (OpenJK's CParticle::UpdateSize and friends). parm was turned
into a time, or a frequency for waves, when the primitive was made.
==================
*/
static float FX_CurvePerc( int bits, float parm, int timeStart, int timeEnd, int now ) {
	float perc1 = 1.0f, perc2 = 1.0f;

	if ( bits & FX_LINEAR ) {
		perc1 = 1.0f - (float)( now - timeStart ) / (float)( timeEnd - timeStart );
	}

	switch ( bits & FX_PARM_MASK ) {
	case FX_NONLINEAR:
		if ( now > parm ) {
			perc2 = 1.0f - (float)( now - parm ) / (float)( timeEnd - parm );
		}
		perc1 = ( bits & FX_LINEAR ) ? perc1 * 0.5f + perc2 * 0.5f : perc2;
		break;
	case FX_WAVE:
		perc1 = perc1 * cos( ( now - timeStart ) * parm );
		break;
	case FX_CLAMP:
		perc2 = ( now < parm ) ? (float)( parm - now ) / (float)( parm - timeStart ) : 0.0f;
		perc1 = ( bits & FX_LINEAR ) ? perc1 * 0.5f + perc2 * 0.5f : perc2;
		break;
	}

	return perc1;
}

static float FX_Curve( const fxEffect_t *e, int shift, float start, float end, float parm ) {
	int		bits = ( e->flags >> shift ) & 0xF;
	float	perc = FX_CurvePerc( bits, parm, e->timeStart, e->timeEnd, fx.time );

	if ( bits & FX_RAND ) {
		perc = FX_RandFloat( 0.0f, perc );
	}
	return start * perc + end * ( 1.0f - perc );
}

/*
==================
FX_ParmTime

A curve's parm is a percentage of the life (or a frequency for waves);
store it as the time it refers to.
==================
*/
static float FX_ParmTime( int flags, int shift, float parm, int life ) {
	int bits = ( flags >> shift ) & FX_PARM_MASK;

	if ( bits == FX_WAVE ) {
		return parm * M_PI * 0.001f;
	}
	if ( bits ) {
		return parm * 0.01f * life + fx.time;
	}
	return 0;
}

// colour and alpha of an effect now, alpha folded into rgb unless useAlpha
static void FX_EffectColor( const fxEffect_t *e, byte rgba[4] ) {
	int		bits, i;
	float	perc, alpha;
	vec3_t	rgb;

	bits = ( e->flags >> FX_RGB_SHIFT ) & 0xF;
	perc = FX_CurvePerc( bits, e->rgbParm, e->timeStart, e->timeEnd, fx.time );
	if ( bits & FX_RAND ) {
		perc = FX_RandFloat( 0.0f, perc );
	}
	for ( i = 0; i < 3; i++ ) {
		rgb[i] = e->rgbStart[i] * perc + e->rgbEnd[i] * ( 1.0f - perc );
	}

	bits = ( e->flags >> FX_ALPHA_SHIFT ) & 0xF;
	perc = FX_CurvePerc( bits, e->alphaParm, e->timeStart, e->timeEnd, fx.time );
	alpha = Com_Clamp( 0.0f, 1.0f, e->alphaStart * perc + e->alphaEnd * ( 1.0f - perc ) );
	if ( bits & FX_RAND ) {
		alpha = FX_RandFloat( 0.0f, alpha );
	}

	if ( !( e->flags & FX_USE_ALPHA ) ) {
		// fade by darkening, right for additive shaders
		VectorScale( rgb, alpha, rgb );
		alpha = 1.0f;
	}

	for ( i = 0; i < 3; i++ ) {
		rgba[i] = (byte)( Com_Clamp( 0.0f, 1.0f, rgb[i] ) * 255.0f );
	}
	rgba[3] = (byte)( alpha * 255.0f );
}


/*
===============================================================================

PARSING

The .efx syntax (Raven's generic parser): "name { ... }" groups,
"name [ a b c ]" lists, and "name value..." pairs whose value runs to the end
of the line. // and /* comments.

===============================================================================
*/

typedef struct {
	char		*p;
	const char	*file;
	int			line;
} fxParser_t;

static void FX_SkipSpace( fxParser_t *ps, qboolean newlines ) {
	for ( ;; ) {
		char c = *ps->p;

		if ( c == '\n' ) {
			if ( !newlines ) {
				return;
			}
			ps->line++;
			ps->p++;
		} else if ( c == ' ' || c == '\t' || c == '\r' ) {
			ps->p++;
		} else if ( c == '/' && ps->p[1] == '/' ) {
			while ( *ps->p && *ps->p != '\n' ) {
				ps->p++;
			}
		} else if ( c == '/' && ps->p[1] == '*' ) {
			ps->p += 2;
			while ( *ps->p && !( ps->p[0] == '*' && ps->p[1] == '/' ) ) {
				if ( *ps->p == '\n' ) {
					ps->line++;
				}
				ps->p++;
			}
			if ( *ps->p ) {
				ps->p += 2;
			}
		} else {
			return;
		}
	}
}

// a word, a quoted string, or one of { } [ ]; "" at the end of the file
static const char *FX_Token( fxParser_t *ps, qboolean newlines ) {
	static char	token[MAX_STRING_CHARS];
	int			len = 0;

	FX_SkipSpace( ps, newlines );
	token[0] = 0;

	if ( !*ps->p || ( !newlines && *ps->p == '\n' ) ) {
		return token;
	}

	if ( strchr( "{}[]", *ps->p ) ) {
		token[0] = *ps->p++;
		token[1] = 0;
		return token;
	}

	if ( *ps->p == '"' ) {
		ps->p++;
		while ( *ps->p && *ps->p != '"' && *ps->p != '\n' ) {
			if ( len < (int)sizeof( token ) - 1 ) {
				token[len++] = *ps->p;
			}
			ps->p++;
		}
		if ( *ps->p == '"' ) {
			ps->p++;
		}
	} else {
		while ( *ps->p && !strchr( " \t\r\n{}[]", *ps->p ) ) {
			if ( len < (int)sizeof( token ) - 1 ) {
				token[len++] = *ps->p;
			}
			ps->p++;
		}
	}
	token[len] = 0;
	return token;
}

// the rest of the line after a key, stopping at a closing brace
static const char *FX_RestOfLine( fxParser_t *ps ) {
	static char	value[MAX_STRING_CHARS];
	int			len = 0;

	FX_SkipSpace( ps, qfalse );
	while ( *ps->p && *ps->p != '\n' && *ps->p != '}' ) {
		if ( ps->p[0] == '/' && ( ps->p[1] == '/' || ps->p[1] == '*' ) ) {
			break;
		}
		if ( len < (int)sizeof( value ) - 1 ) {
			value[len++] = *ps->p;
		}
		ps->p++;
	}
	while ( len > 0 && ( value[len - 1] == ' ' || value[len - 1] == '\t' || value[len - 1] == '\r' ) ) {
		len--;
	}
	value[len] = 0;
	return value;
}

static void FX_Warning( const fxParser_t *ps, const char *msg, const char *arg ) {
	CG_Printf( S_COLOR_YELLOW "WARNING: %s:%d: %s %s\n", ps->file, ps->line, msg, arg );
}

// skip a group or list whose opening token was just read
static void FX_SkipBlock( fxParser_t *ps, char open ) {
	char		close = ( open == '{' ) ? '}' : ']';
	int			depth = 1;
	const char	*tok;

	while ( depth > 0 ) {
		tok = FX_Token( ps, qtrue );
		if ( !tok[0] ) {
			return;
		}
		if ( tok[0] == open && !tok[1] ) {
			depth++;
		} else if ( tok[0] == close && !tok[1] ) {
			depth--;
		}
	}
}

static int FX_ParseFloats( const char *s, float *out, int max ) {
	int n = 0;

	while ( n < max ) {
		while ( *s == ' ' || *s == '\t' ) {
			s++;
		}
		if ( !*s ) {
			break;
		}
		out[n++] = atof( s );
		while ( *s && *s != ' ' && *s != '\t' ) {
			s++;
		}
	}
	return n;
}

// "a" or "a b"
static void FX_ParseRange( const char *s, fxRange_t *r ) {
	float	v[2];
	int		n = FX_ParseFloats( s, v, 2 );

	if ( n == 1 ) {
		FX_SetRange( r, v[0] );
	} else if ( n == 2 ) {
		r->min = v[0];
		r->max = v[1];
	}
}

// "x y z" or "x y z X Y Z"
static void FX_ParseVectorRange( const char *s, fxRange_t r[3] ) {
	float	v[6];
	int		i, n = FX_ParseFloats( s, v, 6 );

	if ( n == 3 ) {
		for ( i = 0; i < 3; i++ ) {
			FX_SetRange( &r[i], v[i] );
		}
	} else if ( n == 6 ) {
		for ( i = 0; i < 3; i++ ) {
			r[i].min = v[i];
			r[i].max = v[i + 3];
		}
	}
}

static int FX_ParseFlagWords( const fxParser_t *ps, const char *s, const fxFlagName_t *names ) {
	char	word[64];
	int		bits = 0, i, len;

	for ( ;; ) {
		while ( *s == ' ' || *s == '\t' || *s == '|' ) {
			s++;
		}
		if ( !*s ) {
			return bits;
		}
		for ( len = 0; *s && *s != ' ' && *s != '\t' && *s != '|'; s++ ) {
			if ( len < (int)sizeof( word ) - 1 ) {
				word[len++] = *s;
			}
		}
		word[len] = 0;

		for ( i = 0; names[i].name; i++ ) {
			if ( !Q_stricmp( word, names[i].name ) ) {
				bits |= names[i].bits;
				break;
			}
		}
		if ( !names[i].name ) {
			FX_Warning( ps, "unknown flag", word );
		}
	}
}

typedef enum {
	FXM_SHADER,
	FXM_MODEL,
	FXM_SOUND
} fxMediaType_t;

static int FX_RegisterMedia( fxMediaType_t type, const char *name ) {
	switch ( type ) {
	case FXM_SHADER:	return trap_R_RegisterShader( name );
	case FXM_MODEL:		return trap_R_RegisterModel( name );
	default:			return trap_S_RegisterSound( name, qfalse );
	}
}

// a single name, or a [ list ] that was just opened
static void FX_ParseMedia( fxParser_t *ps, fxMediaType_t type, fxMedia_t *media, qboolean list, const char *single ) {
	const char *tok;

	media->first = fx.numMedia;
	media->count = 0;

	for ( ;; ) {
		tok = list ? FX_Token( ps, qtrue ) : single;
		if ( !tok[0] || ( tok[0] == ']' && !tok[1] ) ) {
			break;
		}
		if ( fx.numMedia >= MAX_FX_MEDIA ) {
			FX_Warning( ps, "too many media", tok );
		} else {
			fx.media[fx.numMedia++] = FX_RegisterMedia( type, tok );
			media->count++;
		}
		if ( !list ) {
			break;
		}
	}
}

// effect names one file refers to, registered once the file is parsed
typedef struct {
	int		prim;
	int		which;		// 0 impact, 1 death, 2 emit, 3 play
	char	name[MAX_QPATH];
} fxPending_t;

typedef struct {
	int			count;
	fxPending_t	list[MAX_FX_PENDING];
} fxPendingList_t;

static void FX_ParseEffectNames( fxParser_t *ps, fxPendingList_t *pending, int prim, int which, qboolean list, const char *single ) {
	const char *tok;

	for ( ;; ) {
		tok = list ? FX_Token( ps, qtrue ) : single;
		if ( !tok[0] || ( tok[0] == ']' && !tok[1] ) ) {
			break;
		}
		if ( pending->count >= MAX_FX_PENDING ) {
			FX_Warning( ps, "too many effect names", tok );
		} else {
			fxPending_t *p = &pending->list[pending->count++];

			p->prim = prim;
			p->which = which;
			Q_strncpyz( p->name, tok, sizeof( p->name ) );
		}
		if ( !list ) {
			break;
		}
	}
}

// rgb, alpha, size, size2 and length groups
static void FX_ParseCurveGroup( fxParser_t *ps, fxPrimTemplate_t *prim, fxCurve_t *curve, qboolean rgb, int shift ) {
	const char	*tok;
	char		key[64];

	for ( ;; ) {
		tok = FX_Token( ps, qtrue );
		if ( !tok[0] || ( tok[0] == '}' && !tok[1] ) ) {
			return;
		}
		Q_strncpyz( key, tok, sizeof( key ) );

		if ( !Q_stricmp( key, "start" ) ) {
			if ( rgb ) {
				FX_ParseVectorRange( FX_RestOfLine( ps ), prim->rgbStart );
			} else {
				FX_ParseRange( FX_RestOfLine( ps ), &curve->start );
			}
		} else if ( !Q_stricmp( key, "end" ) ) {
			if ( rgb ) {
				FX_ParseVectorRange( FX_RestOfLine( ps ), prim->rgbEnd );
			} else {
				FX_ParseRange( FX_RestOfLine( ps ), &curve->end );
			}
		} else if ( !Q_stricmp( key, "parm" ) || !Q_stricmp( key, "parms" ) ) {
			FX_ParseRange( FX_RestOfLine( ps ), rgb ? &prim->rgbParm : &curve->parm );
		} else if ( !Q_stricmp( key, "flag" ) || !Q_stricmp( key, "flags" ) ) {
			prim->flags |= FX_ParseFlagWords( ps, FX_RestOfLine( ps ), fx_curveFlagNames ) << shift;
		} else {
			FX_Warning( ps, "unknown key", key );
			FX_RestOfLine( ps );
		}
	}
}

static void FX_InitPrimTemplate( fxPrimTemplate_t *prim, fxPrimType_t type ) {
	int i;

	memset( prim, 0, sizeof( *prim ) );
	prim->type = type;

	// OpenJK's defaults
	FX_SetRange( &prim->life, 50 );
	FX_SetRange( &prim->count, 1 );
	FX_SetRange( &prim->radius, 10 );
	FX_SetRange( &prim->height, 10 );
	FX_SetRange( &prim->elasticity, 0.1f );
	FX_SetRange( &prim->density, 10 );
	for ( i = 0; i < 3; i++ ) {
		FX_SetRange( &prim->rgbStart[i], 1 );
		FX_SetRange( &prim->rgbEnd[i], 1 );
	}
	FX_SetRange( &prim->alpha.start, 1 );
	FX_SetRange( &prim->alpha.end, 1 );
	FX_SetRange( &prim->size.start, 1 );
	FX_SetRange( &prim->size.end, 1 );
	FX_SetRange( &prim->size2.start, 1 );
	FX_SetRange( &prim->size2.end, 1 );
	FX_SetRange( &prim->length.start, 1 );
	FX_SetRange( &prim->length.end, 1 );
}

// the contents of a primitive group, after its opening brace
static void FX_ParsePrimitive( fxParser_t *ps, fxPrimTemplate_t *prim, int primNum, fxPendingList_t *pending ) {
	const char	*tok;
	char		key[64];
	qboolean	list;
	int			which;

	for ( ;; ) {
		tok = FX_Token( ps, qtrue );
		if ( !tok[0] || ( tok[0] == '}' && !tok[1] ) ) {
			return;
		}
		Q_strncpyz( key, tok, sizeof( key ) );

		// a group?
		FX_SkipSpace( ps, qtrue );
		if ( *ps->p == '{' ) {
			ps->p++;
			if ( !Q_stricmp( key, "rgb" ) ) {
				FX_ParseCurveGroup( ps, prim, NULL, qtrue, FX_RGB_SHIFT );
			} else if ( !Q_stricmp( key, "alpha" ) ) {
				FX_ParseCurveGroup( ps, prim, &prim->alpha, qfalse, FX_ALPHA_SHIFT );
			} else if ( !Q_stricmp( key, "size" ) || !Q_stricmp( key, "width" ) ) {
				FX_ParseCurveGroup( ps, prim, &prim->size, qfalse, FX_SIZE_SHIFT );
			} else if ( !Q_stricmp( key, "size2" ) || !Q_stricmp( key, "width2" ) ) {
				FX_ParseCurveGroup( ps, prim, &prim->size2, qfalse, FX_SIZE2_SHIFT );
			} else if ( !Q_stricmp( key, "length" ) || !Q_stricmp( key, "height" ) ) {
				FX_ParseCurveGroup( ps, prim, &prim->length, qfalse, FX_LENGTH_SHIFT );
			} else {
				FX_Warning( ps, "unknown group", key );
				FX_SkipBlock( ps, '{' );
			}
			continue;
		}

		// a list or a single name for the media keys
		list = ( *ps->p == '[' );
		if ( list ) {
			ps->p++;
		}

		which = -1;
		if ( !Q_stricmp( key, "shader" ) || !Q_stricmp( key, "shaders" ) ) {
			FX_ParseMedia( ps, FXM_SHADER, &prim->media, list, list ? "" : FX_RestOfLine( ps ) );
			continue;
		} else if ( !Q_stricmp( key, "model" ) || !Q_stricmp( key, "models" ) ) {
			FX_ParseMedia( ps, FXM_MODEL, &prim->media, list, list ? "" : FX_RestOfLine( ps ) );
			continue;
		} else if ( !Q_stricmp( key, "sound" ) || !Q_stricmp( key, "sounds" ) ) {
			FX_ParseMedia( ps, FXM_SOUND, &prim->media, list, list ? "" : FX_RestOfLine( ps ) );
			continue;
		} else if ( !Q_stricmp( key, "impactfx" ) ) {
			which = 0;
		} else if ( !Q_stricmp( key, "deathfx" ) ) {
			which = 1;
		} else if ( !Q_stricmp( key, "emitfx" ) ) {
			which = 2;
		} else if ( !Q_stricmp( key, "playfx" ) ) {
			which = 3;
		}
		if ( which >= 0 ) {
			FX_ParseEffectNames( ps, pending, primNum, which, list, list ? "" : FX_RestOfLine( ps ) );
			continue;
		}
		if ( list ) {
			FX_Warning( ps, "unknown list", key );
			FX_SkipBlock( ps, '[' );
			continue;
		}

		// key value pairs
		tok = FX_RestOfLine( ps );
		if ( !Q_stricmp( key, "count" ) ) {
			FX_ParseRange( tok, &prim->count );
		} else if ( !Q_stricmp( key, "life" ) ) {
			FX_ParseRange( tok, &prim->life );
		} else if ( !Q_stricmp( key, "delay" ) ) {
			FX_ParseRange( tok, &prim->delay );
		} else if ( !Q_stricmp( key, "bounce" ) || !Q_stricmp( key, "intensity" ) ) {
			FX_ParseRange( tok, &prim->elasticity );
		} else if ( !Q_stricmp( key, "min" ) ) {
			FX_ParseFloats( tok, prim->mins, 3 );
		} else if ( !Q_stricmp( key, "max" ) ) {
			FX_ParseFloats( tok, prim->maxs, 3 );
		} else if ( !Q_stricmp( key, "angle" ) || !Q_stricmp( key, "angles" ) ) {
			FX_ParseVectorRange( tok, prim->angle );
		} else if ( !Q_stricmp( key, "angleDelta" ) ) {
			FX_ParseVectorRange( tok, prim->angleDelta );
		} else if ( !Q_stricmp( key, "velocity" ) || !Q_stricmp( key, "vel" ) ) {
			FX_ParseVectorRange( tok, prim->vel );
		} else if ( !Q_stricmp( key, "acceleration" ) || !Q_stricmp( key, "accel" ) ) {
			FX_ParseVectorRange( tok, prim->accel );
		} else if ( !Q_stricmp( key, "gravity" ) ) {
			FX_ParseRange( tok, &prim->gravity );
		} else if ( !Q_stricmp( key, "density" ) ) {
			FX_ParseRange( tok, &prim->density );
		} else if ( !Q_stricmp( key, "variance" ) ) {
			FX_ParseRange( tok, &prim->variance );
		} else if ( !Q_stricmp( key, "origin" ) ) {
			FX_ParseVectorRange( tok, prim->origin1 );
		} else if ( !Q_stricmp( key, "origin2" ) ) {
			FX_ParseVectorRange( tok, prim->origin2 );
		} else if ( !Q_stricmp( key, "radius" ) ) {
			FX_ParseRange( tok, &prim->radius );
		} else if ( !Q_stricmp( key, "height" ) ) {
			FX_ParseRange( tok, &prim->height );
		} else if ( !Q_stricmp( key, "rotation" ) ) {
			FX_ParseRange( tok, &prim->rotation );
		} else if ( !Q_stricmp( key, "rotationDelta" ) ) {
			FX_ParseRange( tok, &prim->rotationDelta );
		} else if ( !Q_stricmp( key, "flag" ) || !Q_stricmp( key, "flags" ) ) {
			prim->flags |= FX_ParseFlagWords( ps, tok, fx_flagNames );
		} else if ( !Q_stricmp( key, "spawnFlag" ) || !Q_stricmp( key, "spawnFlags" ) ) {
			prim->spawnFlags |= FX_ParseFlagWords( ps, tok, fx_spawnFlagNames );
		} else if ( !Q_stricmp( key, "name" ) || !Q_stricmp( key, "wind" ) || !Q_stricmp( key, "materialImpact" )
			|| !Q_stricmp( key, "cullrange" ) ) {
			// not used
		} else {
			FX_Warning( ps, "unknown key", key );
		}
	}
}

// the effect id for each name of a list, appended to the media pool
static void FX_ResolveEffectNames( const fxPendingList_t *pending ) {
	int		ids[MAX_FX_PENDING];
	int		i, j;

	// registering may load more files, which append to the pools, so
	// look everything up before writing the lists
	for ( i = 0; i < pending->count; i++ ) {
		ids[i] = CG_FX_RegisterEffect( pending->list[i].name );
	}

	for ( i = 0; i < pending->count; i = j ) {
		const fxPending_t	*p = &pending->list[i];
		fxPrimTemplate_t	*prim = &fx.prims[p->prim];
		fxMedia_t			*media;

		switch ( p->which ) {
		case 0:		media = &prim->impactFx; break;
		case 1:		media = &prim->deathFx; break;
		case 2:		media = &prim->emitFx; break;
		default:	media = &prim->playFx; break;
		}
		media->first = fx.numMedia;
		media->count = 0;

		for ( j = i; j < pending->count && pending->list[j].prim == p->prim && pending->list[j].which == p->which; j++ ) {
			if ( ids[j] && fx.numMedia < MAX_FX_MEDIA ) {
				fx.media[fx.numMedia++] = ids[j];
				media->count++;
			}
		}
	}
}

static qboolean FX_ParseEffectFile( fxEffectTemplate_t *eff, char *text, const char *filename ) {
	fxParser_t		ps;
	fxPendingList_t	pending;
	const char		*tok;
	char			key[64];
	int				type;

	ps.p = text;
	ps.file = filename;
	ps.line = 1;
	pending.count = 0;

	eff->firstPrim = fx.numPrims;
	eff->numPrims = 0;

	for ( ;; ) {
		tok = FX_Token( &ps, qtrue );
		if ( !tok[0] ) {
			break;
		}
		Q_strncpyz( key, tok, sizeof( key ) );

		FX_SkipSpace( &ps, qtrue );
		if ( *ps.p != '{' ) {
			// top level pairs: repeatDelay, ignored as nothing loops yet
			FX_RestOfLine( &ps );
			continue;
		}
		ps.p++;

		for ( type = 0; type < FXP_NUM; type++ ) {
			if ( !Q_stricmp( key, fx_primNames[type] ) ) {
				break;
			}
		}
		if ( type == FXP_NUM ) {
			FX_Warning( &ps, "unknown primitive", key );
			FX_SkipBlock( &ps, '{' );
			continue;
		}
		if ( eff->numPrims >= MAX_FX_PRIMS_PER_FILE || fx.numPrims >= MAX_FX_PRIMS ) {
			FX_Warning( &ps, "too many primitives", key );
			FX_SkipBlock( &ps, '{' );
			continue;
		}

		FX_InitPrimTemplate( &fx.prims[fx.numPrims], type );
		FX_ParsePrimitive( &ps, &fx.prims[fx.numPrims], fx.numPrims, &pending );
		fx.numPrims++;
		eff->numPrims++;
	}

	FX_ResolveEffectNames( &pending );
	return eff->numPrims > 0;
}


/*
===============================================================================

REGISTRATION AND PLAYING

===============================================================================
*/

/*
==================
CG_FX_RegisterEffect

Loads effects/<name>.efx once; returns its id, 0 if it can't be loaded.
==================
*/
int CG_FX_RegisterEffect( const char *name ) {
	char				path[MAX_QPATH], base[MAX_QPATH];
	fileHandle_t		f;
	int					len, i;
	fxEffectTemplate_t	*eff;

	if ( !name || !name[0] ) {
		return 0;
	}

	COM_StripExtension( name, base, sizeof( base ) );
	for ( i = 0; i < fx.numTemplates; i++ ) {
		if ( !Q_stricmp( fx.templates[i].name, base ) ) {
			return i + 1;
		}
	}

	if ( fx.numTemplates >= MAX_FX_TEMPLATES ) {
		CG_Printf( S_COLOR_YELLOW "WARNING: too many effects, %s not loaded\n", name );
		return 0;
	}

	Com_sprintf( path, sizeof( path ), "effects/%s.efx", base );
	len = trap_FS_FOpenFile( path, &f, FS_READ );
	if ( len <= 0 ) {
		if ( fx_debug.integer ) {
			CG_Printf( S_COLOR_YELLOW "WARNING: effect %s not found\n", path );
		}
		if ( f ) {
			trap_FS_FCloseFile( f );
		}
		return 0;
	}
	if ( len >= MAX_FX_FILE ) {
		CG_Printf( S_COLOR_YELLOW "WARNING: effect %s too large\n", path );
		trap_FS_FCloseFile( f );
		return 0;
	}

	// claim the slot first so effects that refer back to this one find it
	eff = &fx.templates[fx.numTemplates++];
	Q_strncpyz( eff->name, base, sizeof( eff->name ) );

	// one static buffer (the QVM stack is small): effects this file names
	// are only loaded once it has been parsed
	trap_FS_Read( fx_fileBuffer, len, f );
	fx_fileBuffer[len] = 0;
	trap_FS_FCloseFile( f );

	FX_ParseEffectFile( eff, fx_fileBuffer, path );
	return eff - fx.templates + 1;
}

static void FX_AxisFromForward( const vec3_t fwd, vec3_t axis[3] ) {
	VectorNormalize2( fwd, axis[0] );
	if ( VectorLength( axis[0] ) == 0 ) {
		VectorSet( axis[0], 0, 0, 1 );
	}
	MakeNormalVectors( axis[0], axis[1], axis[2] );
}

static void FX_Schedule( int prim, int startTime, const vec3_t origin, vec3_t axis[3] ) {
	int i;

	for ( i = 0; i < MAX_FX_SCHEDULED; i++ ) {
		fxScheduled_t *s = &fx.scheduled[i];

		if ( !s->inUse ) {
			s->inUse = qtrue;
			s->prim = prim;
			s->startTime = startTime;
			VectorCopy( origin, s->origin );
			AxisCopy( axis, s->axis );
			return;
		}
	}
}

/*
==================
CG_FX_PlayEffectIDAxis

Plays an effect at origin; axis[0] is its forward direction.
==================
*/
void CG_FX_PlayEffectIDAxis( int id, const vec3_t origin, vec3_t axis[3] ) {
	const fxEffectTemplate_t	*eff;
	int							i, j, count, delay;
	float						scale;

	if ( id <= 0 || id > fx.numTemplates || fx.frameTime < 1 ) {
		// effects can't be added while the game is paused
		return;
	}
	eff = &fx.templates[id - 1];

	scale = fx_countScale.value < 1.0f ? fx_countScale.value : 1.0f;

	for ( i = 0; i < eff->numPrims; i++ ) {
		const fxPrimTemplate_t *prim = &fx.prims[eff->firstPrim + i];

		if ( prim->count.max - prim->count.min < 1 ) {
			count = prim->count.max;
		} else {
			count = (int)( FX_RangeVal( &prim->count ) * scale + 0.5f );
		}

		for ( j = 0; j < count; j++ ) {
			if ( prim->spawnFlags & FX_EVEN_DISTRIBUTION ) {
				delay = (int)( j * fabs( prim->delay.max - prim->delay.min ) / count );
			} else {
				delay = (int)FX_RangeVal( &prim->delay );
			}

			if ( delay < 1 ) {
				FX_CreatePrim( prim, origin, axis, 0 );
			} else {
				FX_Schedule( eff->firstPrim + i, fx.time + delay, origin, axis );
			}
		}
	}
}

void CG_FX_PlayEffectID( int id, const vec3_t origin, const vec3_t fwd ) {
	vec3_t axis[3];

	FX_AxisFromForward( fwd, axis );
	CG_FX_PlayEffectIDAxis( id, origin, axis );
}

void CG_FX_PlayEffect( const char *name, const vec3_t origin, const vec3_t fwd ) {
	CG_FX_PlayEffectID( CG_FX_RegisterEffect( name ), origin, fwd );
}

// a line's or electricity's end, given (CG_FX_PlayEffectLine): in place of
// its origin2
static qboolean	fx_lineEndGiven;
static vec3_t	fx_lineEnd;

/*
================
CG_FX_PlayEffectLine

an effect from start to end: its lines and electricity reach end
================
*/
void CG_FX_PlayEffectLine( const char *name, const vec3_t start, const vec3_t end ) {
	vec3_t fwd;

	VectorSubtract( end, start, fwd );
	if ( VectorNormalize( fwd ) == 0 ) {
		VectorSet( fwd, 0, 0, 1 );
	}
	fx_lineEndGiven = qtrue;
	VectorCopy( end, fx_lineEnd );
	CG_FX_PlayEffect( name, start, fwd );
	fx_lineEndGiven = qfalse;
}

static int FX_PickMedia( const fxMedia_t *m ) {
	if ( m->count <= 0 ) {
		return 0;
	}
	return fx.media[m->first + ( rand() % m->count )];
}


/*
===============================================================================

PRIMITIVES

===============================================================================
*/

static fxEffect_t *FX_AllocEffect( fxPrimType_t type, int flags, int life ) {
	int i;

	for ( i = 0; i < MAX_FX_EFFECTS; i++ ) {
		fxEffect_t *e = &fx.effects[i];

		if ( !e->inUse ) {
			memset( e, 0, sizeof( *e ) );
			e->inUse = qtrue;
			e->type = type;
			e->flags = flags;
			e->timeStart = fx.time;
			e->timeEnd = fx.time + ( life > 1 ? life : 1 );
			if ( i >= fx.numActive ) {
				fx.numActive = i + 1;
			}
			return e;
		}
	}
	return NULL;
}

static void FX_GetColors( const fxPrimTemplate_t *prim, vec3_t start, vec3_t end ) {
	int		i;
	float	perc;

	if ( prim->spawnFlags & FX_RGB_COMPONENT_INTERP ) {
		// a colour on the line between the two corners of the range
		perc = random();
		for ( i = 0; i < 3; i++ ) {
			start[i] = prim->rgbStart[i].min + perc * ( prim->rgbStart[i].max - prim->rgbStart[i].min );
		}
		perc = random();
		for ( i = 0; i < 3; i++ ) {
			end[i] = prim->rgbEnd[i].min + perc * ( prim->rgbEnd[i].max - prim->rgbEnd[i].min );
		}
	} else {
		for ( i = 0; i < 3; i++ ) {
			start[i] = FX_RangeVal( &prim->rgbStart[i] );
			end[i] = FX_RangeVal( &prim->rgbEnd[i] );
		}
	}
}

// a vector from three ranges, along axis unless absolute
static void FX_AxisVector( const fxRange_t r[3], vec3_t axis[3], qboolean absolute, vec3_t out ) {
	if ( absolute ) {
		VectorSet( out, FX_RangeVal( &r[0] ), FX_RangeVal( &r[1] ), FX_RangeVal( &r[2] ) );
	} else {
		VectorScale( axis[0], FX_RangeVal( &r[0] ), out );
		VectorMA( out, FX_RangeVal( &r[1] ), axis[1], out );
		VectorMA( out, FX_RangeVal( &r[2] ), axis[2], out );
	}
}

/*
==================
FX_CreatePrim

One primitive of an effect (OpenJK's CFxScheduler::CreateEffect).
==================
*/
static void FX_CreatePrim( const fxPrimTemplate_t *prim, const vec3_t origin, vec3_t axisIn[3], int lateTime ) {
	vec3_t		org, org2, temp, vel, accel, sRGB, eRGB, ax[3];
	trace_t		tr;
	fxEffect_t	*e;
	int			life, flags = prim->flags & ~FX_RELATIVE;	// nothing to bolt to here
	int			i;

	AxisCopy( axisIn, ax );
	VectorClear( org2 );

	if ( prim->spawnFlags & FX_RAND_ROT_AROUND_FWD ) {
		RotatePointAroundVector( ax[1], ax[0], axisIn[1], FX_RandFloat( 0, 360 ) );
		CrossProduct( ax[0], ax[1], ax[2] );
	}

	FX_AxisVector( prim->origin1, ax, ( prim->spawnFlags & FX_CHEAP_ORG_CALC ) != 0, org );
	VectorAdd( org, origin, org );

	if ( prim->spawnFlags & FX_ORG_ON_SPHERE ) {
		float x = DEG2RAD( FX_RandFloat( 0, 360 ) );
		float y = DEG2RAD( FX_RandFloat( 0, 180 ) );
		float width = FX_RangeVal( &prim->radius );
		float height = FX_RangeVal( &prim->height );

		VectorSet( temp, sin( x ) * width * sin( y ), cos( x ) * width * sin( y ), cos( y ) * height );
		VectorAdd( org, temp, org );
		if ( prim->spawnFlags & FX_AXIS_FROM_SPHERE ) {
			VectorNormalize2( temp, ax[0] );
			MakeNormalVectors( ax[0], ax[1], ax[2] );
		}
	} else if ( prim->spawnFlags & FX_ORG_ON_CYLINDER ) {
		vec3_t pt;

		VectorScale( ax[1], FX_RangeVal( &prim->radius ), pt );
		VectorMA( pt, crandom() * 0.5f * FX_RangeVal( &prim->height ), ax[0], pt );
		RotatePointAroundVector( temp, ax[0], pt, FX_RandFloat( 0, 360 ) );
		VectorAdd( org, temp, org );
		if ( prim->spawnFlags & FX_AXIS_FROM_SPHERE ) {
			vec3_t up = { 0, 0, 1 };

			VectorNormalize2( temp, ax[0] );
			if ( ax[0][2] == 1.0f ) {
				VectorSet( up, 0, 1, 0 );
			}
			CrossProduct( up, ax[0], ax[1] );
			CrossProduct( ax[0], ax[1], ax[2] );
		}
	}

	VectorClear( vel );
	VectorClear( accel );
	if ( prim->type == FXP_PARTICLE || prim->type == FXP_ORIENTEDPARTICLE || prim->type == FXP_TAIL || prim->type == FXP_EMITTER ) {
		FX_AxisVector( prim->vel, ax, ( prim->spawnFlags & FX_VEL_IS_ABSOLUTE ) != 0, vel );
		FX_AxisVector( prim->accel, ax, ( prim->spawnFlags & FX_ACCEL_IS_ABSOLUTE ) != 0, accel );
		accel[2] += FX_RangeVal( &prim->gravity );

		// catch up for primitives that were scheduled and started late
		if ( lateTime > 0 ) {
			float ftime = lateTime * 0.001f;
			float time2 = ftime * ftime * 0.5f;

			VectorMA( vel, ftime, accel, vel );
			for ( i = 0; i < 3; i++ ) {
				org[i] += ftime * vel[i] + time2 * vel[i];
			}
		}
	}

	if ( prim->type == FXP_LINE || prim->type == FXP_ELECTRICITY ) {
		if ( prim->spawnFlags & FX_ORG2_FROM_TRACE ) {
			VectorMA( org, FX_MAX_TRACE_DIST, ax[0], temp );
			if ( prim->spawnFlags & FX_ORG2_IS_OFFSET ) {
				FX_AxisVector( prim->origin2, ax, ( prim->spawnFlags & FX_CHEAP_ORG2_CALC ) != 0, org2 );
				VectorAdd( temp, org2, temp );
			}
			CG_Trace( &tr, org, NULL, NULL, temp, -1, MASK_SOLID );
			VectorCopy( tr.endpos, org2 );
			if ( prim->spawnFlags & FX_TRACE_IMPACT_FX ) {
				CG_FX_PlayEffectID( FX_PickMedia( &prim->impactFx ), org2, tr.plane.normal );
			}
		} else if ( fx_lineEndGiven ) {
			VectorCopy( fx_lineEnd, org2 );
		} else {
			FX_AxisVector( prim->origin2, ax, ( prim->spawnFlags & FX_CHEAP_ORG2_CALC ) != 0, org2 );
			VectorAdd( org2, origin, org2 );
		}
	}

	life = (int)FX_RangeVal( &prim->life );

	switch ( prim->type ) {
	case FXP_SOUND:
		trap_S_StartSound( org, ENTITYNUM_WORLD, CHAN_AUTO, FX_PickMedia( &prim->media ) );
		return;

	case FXP_RUNNER:
		CG_FX_PlayEffectIDAxis( FX_PickMedia( &prim->playFx ), org, ax );
		return;

	case FXP_DECAL:
		FX_GetColors( prim, sRGB, eRGB );
		CG_ImpactMark( FX_PickMedia( &prim->media ), org, ax[0], FX_RangeVal( &prim->rotation ),
			sRGB[0], sRGB[1], sRGB[2], FX_RangeVal( &prim->alpha.start ), qtrue, FX_RangeVal( &prim->size.start ), qfalse );
		return;

	case FXP_CAMERASHAKE:
		// JKA's: "size" its intensity (degrees), "radius" how far it's felt, "life" how long
		{
			float radius = FX_RangeVal( &prim->radius );
			float dist = Distance( cg.refdef.vieworg, org );

			if ( radius <= 0 || dist < radius ) {
				CG_StartViewShake( FX_RangeVal( &prim->size.start ) * ( radius > 0 ? 1 - dist / radius : 1 ), life );
			}
		}
		return;

	case FXP_CYLINDER:
	case FXP_FLASH:
		if ( fx_debug.integer && !fx.warned[prim->type] ) {
			fx.warned[prim->type] = qtrue;
			CG_Printf( S_COLOR_YELLOW "WARNING: %s effects are not supported yet\n", fx_primNames[prim->type] );
		}
		return;

	default:
		break;
	}

	e = FX_AllocEffect( prim->type, flags, life );
	if ( !e ) {
		return;
	}

	VectorCopy( org, e->origin );
	VectorCopy( org2, e->origin2 );
	VectorCopy( vel, e->vel );
	VectorCopy( accel, e->accel );
	AxisCopy( ax, e->axis );
	VectorCopy( prim->mins, e->mins );
	VectorCopy( prim->maxs, e->maxs );
	e->elasticity = FX_RangeVal( &prim->elasticity );

	FX_GetColors( prim, e->rgbStart, e->rgbEnd );
	e->rgbParm = FX_ParmTime( flags, FX_RGB_SHIFT, FX_RangeVal( &prim->rgbParm ), life );
	e->alphaStart = FX_RangeVal( &prim->alpha.start );
	e->alphaEnd = FX_RangeVal( &prim->alpha.end );
	e->alphaParm = FX_ParmTime( flags, FX_ALPHA_SHIFT, FX_RangeVal( &prim->alpha.parm ), life );
	e->sizeStart = FX_RangeVal( &prim->size.start );
	e->sizeEnd = FX_RangeVal( &prim->size.end );
	e->sizeParm = FX_ParmTime( flags, FX_SIZE_SHIFT, FX_RangeVal( &prim->size.parm ), life );
	e->lengthStart = FX_RangeVal( &prim->length.start );
	e->lengthEnd = FX_RangeVal( &prim->length.end );
	e->lengthParm = FX_ParmTime( flags, FX_LENGTH_SHIFT, FX_RangeVal( &prim->length.parm ), life );
	e->rotation = FX_RangeVal( &prim->rotation );
	e->rotationDelta = FX_RangeVal( &prim->rotationDelta );

	e->media = FX_PickMedia( &prim->media );
	e->impactFx = FX_PickMedia( &prim->impactFx );
	e->deathFx = FX_PickMedia( &prim->deathFx );
	e->emitFx = FX_PickMedia( &prim->emitFx );

	if ( prim->type == FXP_ORIENTEDPARTICLE ) {
		// the quad faces along the effect's forward direction
		VectorCopy( ax[0], e->axis[0] );
	} else if ( prim->type == FXP_EMITTER ) {
		vec3_t angles;

		for ( i = 0; i < 3; i++ ) {
			e->angles[i] = FX_RangeVal( &prim->angle[i] );
			e->angleDelta[i] = FX_RangeVal( &prim->angleDelta[i] );
		}
		vectoangles( ax[0], angles );
		VectorAdd( e->angles, angles, e->angles );
		AnglesToAxis( e->angles, e->axis );
		e->density = FX_RangeVal( &prim->density );
		e->variance = FX_RangeVal( &prim->variance );
		e->oldTime = fx.time;
		VectorCopy( e->origin, e->oldOrigin );
		VectorCopy( e->vel, e->oldVel );
	}
}

static void FX_FreeEffect( fxEffect_t *e ) {
	e->inUse = qfalse;
}

/*
==================
FX_Move

Moves a primitive through one frame, bouncing or dying on impact for
physics primitives. Returns qfalse when it should die.
==================
*/
static qboolean FX_Move( fxEffect_t *e ) {
	vec3_t	newOrigin;
	trace_t	tr;
	float	dot;

	VectorMA( e->vel, fx.realTime, e->accel, e->vel );
	VectorMA( e->origin, fx.realTime, e->vel, newOrigin );

	if ( ( e->flags & FX_APPLY_PHYSICS ) && ( e->flags & FX_EXPENSIVE_PHYSICS ) ) {
		if ( e->flags & FX_USE_BBOX ) {
			CG_Trace( &tr, e->origin, e->mins, e->maxs, newOrigin, -1, MASK_SOLID );
		} else {
			CG_Trace( &tr, e->origin, NULL, NULL, newOrigin, -1, MASK_SOLID );
		}

		if ( tr.startsolid || tr.allsolid ) {
			VectorClear( e->vel );
			VectorClear( e->accel );
			e->flags &= ~( FX_APPLY_PHYSICS | FX_IMPACT_RUNS_FX );
			return qtrue;
		}

		if ( tr.fraction < 1.0f ) {
			if ( ( e->flags & FX_IMPACT_RUNS_FX ) && !( tr.surfaceFlags & SURF_NOIMPACT ) ) {
				CG_FX_PlayEffectID( e->impactFx, tr.endpos, tr.plane.normal );
			}
			if ( e->flags & FX_KILL_ON_IMPACT ) {
				return qfalse;
			}

			// bounce off, losing energy
			VectorMA( e->vel, fx.realTime * tr.fraction, e->accel, e->vel );
			dot = DotProduct( e->vel, tr.plane.normal );
			VectorMA( e->vel, -2.0f * dot, tr.plane.normal, e->vel );
			VectorScale( e->vel, e->elasticity, e->vel );
			e->elasticity *= 0.5f;

			if ( VectorLengthSquared( e->vel ) < 100.0f ) {
				VectorClear( e->vel );
				VectorClear( e->accel );
				e->flags &= ~( FX_APPLY_PHYSICS | FX_IMPACT_RUNS_FX );
			}

			VectorMA( tr.endpos, 1.0f, tr.plane.normal, e->origin );
			return qtrue;
		}
	}

	VectorCopy( newOrigin, e->origin );
	return qtrue;
}

// emitters drop their emitfx along the path they travel
static void FX_Emit( fxEffect_t *e ) {
	vec3_t	org, v;
	float	ftime, time2, step;
	int		t, dif = 0;

	step = e->density + FX_RandFloat( -e->variance, e->variance );
	step *= step;

	for ( t = e->oldTime; t <= fx.time; t += FX_EMIT_RATE ) {
		dif += FX_EMIT_RATE;

		VectorMA( e->oldVel, dif * 0.001f, e->accel, v );
		ftime = dif * 0.001f;
		time2 = ftime * ftime * 0.5f;
		org[0] = e->oldOrigin[0] + ftime * v[0] + time2 * v[0];
		org[1] = e->oldOrigin[1] + ftime * v[1] + time2 * v[1];
		org[2] = e->oldOrigin[2] + ftime * v[2] + time2 * v[2];

		if ( DistanceSquared( org, e->oldOrigin ) >= step ) {
			step = e->density + FX_RandFloat( -e->variance, e->variance );
			step *= step;

			CG_FX_PlayEffectIDAxis( e->emitFx, org, e->axis );
			VectorCopy( org, e->oldOrigin );
			VectorCopy( v, e->oldVel );
			dif = 0;
			e->oldTime = t;
		}
	}
}

/*
==================
CG_FX_Update

Once a frame: starts scheduled primitives, moves and ages the live ones.
==================
*/
void CG_FX_Update( void ) {
	int i;

	fx.oldTime = fx.time;
	fx.time = cg.time;
	fx.frameTime = fx.time - fx.oldTime;
	if ( fx.frameTime < 0 || fx.frameTime > 1000 ) {
		// map change or a long hitch
		fx.frameTime = 0;
	}
	fx.realTime = fx.frameTime * 0.001f;

	if ( fx.frameTime < 1 ) {
		return;
	}

	for ( i = 0; i < MAX_FX_SCHEDULED; i++ ) {
		fxScheduled_t *s = &fx.scheduled[i];

		if ( s->inUse && s->startTime <= fx.time ) {
			s->inUse = qfalse;
			FX_CreatePrim( &fx.prims[s->prim], s->origin, s->axis, fx.time - s->startTime );
		}
	}

	for ( i = 0; i < fx.numActive; i++ ) {
		fxEffect_t *e = &fx.effects[i];

		if ( !e->inUse ) {
			continue;
		}

		if ( e->timeEnd <= fx.time || e->timeStart > fx.time ) {
			if ( ( e->flags & FX_DEATH_RUNS_FX ) && !( e->flags & FX_KILL_ON_IMPACT ) ) {
				vec3_t dir;

				VectorSet( dir, crandom(), crandom(), crandom() );
				CG_FX_PlayEffectID( e->deathFx, e->origin, dir );
			}
			FX_FreeEffect( e );
			continue;
		}

		switch ( e->type ) {
		case FXP_PARTICLE:
		case FXP_ORIENTEDPARTICLE:
		case FXP_TAIL:
		case FXP_LIGHT:
			if ( !FX_Move( e ) ) {
				FX_FreeEffect( e );
				continue;
			}
			e->rotation += fx.frameTime * 0.01f * e->rotationDelta;
			e->rotationDelta *= 1.0f - fx.frameTime * 0.0007f;
			break;

		case FXP_EMITTER:
			VectorCopy( e->origin, e->oldOrigin );
			VectorCopy( e->vel, e->oldVel );
			if ( !FX_Move( e ) ) {
				FX_FreeEffect( e );
				continue;
			}
			if ( VectorCompare( e->oldOrigin, e->origin ) ) {
				// at rest, slow the spin down
				VectorScale( e->angleDelta, 0.7f, e->angleDelta );
			}
			VectorMA( e->angles, fx.realTime, e->angleDelta, e->angles );
			AnglesToAxis( e->angles, e->axis );
			if ( e->flags & FX_EMIT_FX ) {
				FX_Emit( e );
			}
			break;

		default:
			break;
		}
	}

	while ( fx.numActive > 0 && !fx.effects[fx.numActive - 1].inUse ) {
		fx.numActive--;
	}
}

/*
===============================================================================

DRAWING

===============================================================================
*/

static qboolean FX_Cull( const fxEffect_t *e, const vec3_t org ) {
	vec3_t dir;

	VectorSubtract( org, cg.refdef.vieworg, dir );
	if ( DotProduct( cg.refdef.viewaxis[0], dir ) < 0 ) {
		return qtrue;
	}
	if ( !( e->flags & FX_DEPTH_HACK ) && VectorLengthSquared( dir ) < fx_nearCull.value ) {
		return qtrue;
	}
	return qfalse;
}

static void FX_SetVert( polyVert_t *v, const vec3_t xyz, float s, float t, const byte rgba[4] ) {
	VectorCopy( xyz, v->xyz );
	v->st[0] = s;
	v->st[1] = t;
	v->modulate[0] = rgba[0];
	v->modulate[1] = rgba[1];
	v->modulate[2] = rgba[2];
	v->modulate[3] = rgba[3];
}

// a quad from start to end facing the viewer, width wide (OpenJK's DoLine)
static void FX_AddLineQuad( qhandle_t shader, const vec3_t start, const vec3_t end, float width, const byte rgba[4] ) {
	vec3_t		right, v1, v2, corner;
	polyVert_t	verts[4];

	VectorSubtract( start, cg.refdef.vieworg, v1 );
	VectorSubtract( end, cg.refdef.vieworg, v2 );
	CrossProduct( v1, v2, right );
	if ( VectorNormalize( right ) == 0 ) {
		return;
	}
	width *= 0.5f;

	VectorMA( start, width, right, corner );
	FX_SetVert( &verts[0], corner, 0, 0, rgba );
	VectorMA( end, width, right, corner );
	FX_SetVert( &verts[1], corner, 1, 0, rgba );
	VectorMA( end, -width, right, corner );
	FX_SetVert( &verts[2], corner, 1, 1, rgba );
	VectorMA( start, -width, right, corner );
	FX_SetVert( &verts[3], corner, 0, 1, rgba );

	trap_R_AddPolyToScene( shader, 4, verts, 0, 0 );
}

// a quad lying in the plane whose normal is the effect's axis[0]
static void FX_AddOrientedQuad( qhandle_t shader, const vec3_t org, const vec3_t normal, float rotation, float radius, const byte rgba[4] ) {
	vec3_t		right, up, r, u, corner;
	polyVert_t	verts[4];

	MakeNormalVectors( normal, right, up );
	if ( rotation ) {
		RotatePointAroundVector( r, normal, right, rotation );
		CrossProduct( normal, r, u );
	} else {
		VectorCopy( right, r );
		VectorCopy( up, u );
	}

	VectorMA( org, radius, r, corner );
	VectorMA( corner, radius, u, corner );
	FX_SetVert( &verts[0], corner, 0, 0, rgba );
	VectorMA( org, -radius, r, corner );
	VectorMA( corner, radius, u, corner );
	FX_SetVert( &verts[1], corner, 1, 0, rgba );
	VectorMA( org, -radius, r, corner );
	VectorMA( corner, -radius, u, corner );
	FX_SetVert( &verts[2], corner, 1, 1, rgba );
	VectorMA( org, radius, r, corner );
	VectorMA( corner, -radius, u, corner );
	FX_SetVert( &verts[3], corner, 0, 1, rgba );

	trap_R_AddPolyToScene( shader, 4, verts, 0, 0 );
}

/*
==================
CG_FX_AddToScene

For each view: adds the live primitives to the scene.
==================
*/
void CG_FX_AddToScene( void ) {
	int			i;
	byte		rgba[4];
	float		size, length;
	refEntity_t	ent;
	vec3_t		end, dir;

	for ( i = 0; i < fx.numActive; i++ ) {
		fxEffect_t *e = &fx.effects[i];

		if ( !e->inUse || e->timeStart > fx.time ) {
			continue;
		}

		size = FX_Curve( e, FX_SIZE_SHIFT, e->sizeStart, e->sizeEnd, e->sizeParm );

		switch ( e->type ) {
		case FXP_PARTICLE:
			if ( FX_Cull( e, e->origin ) ) {
				break;
			}
			FX_EffectColor( e, rgba );
			memset( &ent, 0, sizeof( ent ) );
			ent.reType = RT_SPRITE;
			VectorCopy( e->origin, ent.origin );
			ent.radius = size;
			ent.rotation = e->rotation;
			ent.customShader = e->media;
			Byte4Copy( rgba, ent.shaderRGBA );
			if ( e->flags & FX_DEPTH_HACK ) {
				ent.renderfx |= RF_DEPTHHACK;
			}
			if ( e->flags & FX_SET_SHADER_TIME ) {
				ent.shaderTime = e->timeStart;
			}
			trap_R_AddRefEntityToScene( &ent );
			break;

		case FXP_ORIENTEDPARTICLE:
			if ( FX_Cull( e, e->origin ) ) {
				break;
			}
			FX_EffectColor( e, rgba );
			FX_AddOrientedQuad( e->media, e->origin, e->axis[0], e->rotation, size, rgba );
			break;

		case FXP_LINE:
			FX_EffectColor( e, rgba );
			FX_AddLineQuad( e->media, e->origin, e->origin2, size, rgba );
			break;

		case FXP_ELECTRICITY:
			// JKA's: a jagged bolt from origin to origin2, its kinks anew each
			// frame, as far off the straight as a twentieth of its length
			{
				vec3_t	dir, right, up, prev, next;
				float	len, dev;
				int		i, segs;

				FX_EffectColor( e, rgba );
				VectorSubtract( e->origin2, e->origin, dir );
				len = VectorNormalize( dir );
				if ( e->flags & FX_GROWING ) {
					// from its start to its end over the first half of its life
					len *= Com_Clamp( 0, 1, 2.0f * ( fx.time - e->timeStart ) / MAX( 1, e->timeEnd - e->timeStart ) );
				}
				PerpendicularVector( right, dir );
				CrossProduct( dir, right, up );
				segs = Com_Clamp( 4, 24, len / 32 );
				dev = len * 0.05f;
				VectorCopy( e->origin, prev );
				for ( i = 1; i <= segs; i++ ) {
					VectorMA( e->origin, len * i / segs, dir, next );
					if ( i < segs ) {
						VectorMA( next, crandom() * dev, right, next );
						VectorMA( next, crandom() * dev, up, next );
					}
					FX_AddLineQuad( e->media, prev, next, size, rgba );
					VectorCopy( next, prev );
				}
			}
			break;

		case FXP_TAIL:
			if ( FX_Cull( e, e->origin ) ) {
				break;
			}
			// a streak trailing behind the direction of travel
			length = FX_Curve( e, FX_LENGTH_SHIFT, e->lengthStart, e->lengthEnd, e->lengthParm );
			VectorNormalize2( e->vel, dir );
			VectorMA( e->origin, -length, dir, end );
			FX_EffectColor( e, rgba );
			FX_AddLineQuad( e->media, e->origin, end, size, rgba );
			break;

		case FXP_LIGHT:
			FX_EffectColor( e, rgba );
			trap_R_AddLightToScene( e->origin, size, 1.0f, rgba[0] / 255.0f, rgba[1] / 255.0f, rgba[2] / 255.0f, 0 );
			break;

		case FXP_EMITTER:
			if ( ( e->flags & FX_ATTACHED_MODEL ) && e->media ) {
				memset( &ent, 0, sizeof( ent ) );
				ent.reType = RT_MODEL;
				ent.hModel = e->media;
				VectorCopy( e->origin, ent.origin );
				VectorCopy( e->origin, ent.oldorigin );
				VectorScale( e->axis[0], size, ent.axis[0] );
				VectorScale( e->axis[1], size, ent.axis[1] );
				VectorScale( e->axis[2], size, ent.axis[2] );
				ent.nonNormalizedAxes = qtrue;
				trap_R_AddRefEntityToScene( &ent );
			}
			break;

		default:
			break;
		}
	}
}

/*
==================
CG_FX_Init

Forgets every effect; call when the cgame starts.
==================
*/
void CG_FX_Init( void ) {
	memset( &fx, 0, sizeof( fx ) );
	fx.time = fx.oldTime = cg.time;
}

/*
==================
CG_FX_Stop

Kills the live and scheduled primitives, keeping the registered effects.
==================
*/
void CG_FX_Stop( void ) {
	memset( fx.scheduled, 0, sizeof( fx.scheduled ) );
	memset( fx.effects, 0, sizeof( fx.effects ) );
	fx.numActive = 0;
}
