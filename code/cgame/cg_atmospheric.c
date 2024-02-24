/*
===========================================================================
Copyright (C) 1997-2001 Id Software, Inc.
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

/*
**
**	cg_atmospheric.c
**
**	Add atmospheric effects (e.g. rain, snow etc.) to view.
**
**	Current supported effects are rain and snow.
**
*/

#include "cg_local.h"

#define MAX_ATMOSPHERIC_HEIGHT          MAX_MAP_SIZE    // maximum world height
#define MIN_ATMOSPHERIC_HEIGHT          -MAX_MAP_SIZE   // minimum world height

//int getgroundtime, getskytime, rendertime, checkvisibletime, generatetime;
//int n_getgroundtime, n_getskytime, n_rendertime, n_checkvisibletime, n_generatetime;

#define MAX_ATMOSPHERIC_PARTICLES       4000    // maximum # of particles
#define MAX_ATMOSPHERIC_DISTANCE        1000    // maximum distance from refdef origin that particles are visible
#define MAX_ATMOSPHERIC_EFFECTSHADERS   6       // maximum different effectshaders for an atmospheric effect
#define ATMOSPHERIC_DROPDELAY           1000
#define ATMOSPHERIC_CUTHEIGHT           800

#define ATMOSPHERIC_RAIN_SPEED      ( 1.1f * DEFAULT_GRAVITY )
#define ATMOSPHERIC_RAIN_HEIGHT     150

#define ATMOSPHERIC_SNOW_SPEED      ( 0.1f * DEFAULT_GRAVITY )
#define ATMOSPHERIC_SNOW_HEIGHT     3

typedef enum {
	ATM_NONE,
	ATM_RAIN,
	ATM_SNOW
} atmFXType_t;

static void CG_AddPolyToPool( qhandle_t shader, const polyVert_t *verts ) {
	int firstIndex;
	int firstVertex;
	int i;

	polyBuffer_t* pPolyBuffer = CG_PB_FindFreePolyBuffer( shader, 3, 3 );
	if ( !pPolyBuffer ) {
		return;
	}

	firstIndex = pPolyBuffer->numIndicies;
	firstVertex = pPolyBuffer->numVerts;

	for ( i = 0; i < 3; i++ ) {
		VectorCopy( verts[i].xyz, pPolyBuffer->xyz[firstVertex + i] );

		pPolyBuffer->st[firstVertex + i][0] = verts[i].st[0];
		pPolyBuffer->st[firstVertex + i][1] = verts[i].st[1];
		pPolyBuffer->color[firstVertex + i][0] = verts[i].modulate[0];
		pPolyBuffer->color[firstVertex + i][1] = verts[i].modulate[1];
		pPolyBuffer->color[firstVertex + i][2] = verts[i].modulate[2];
		pPolyBuffer->color[firstVertex + i][3] = verts[i].modulate[3];

		pPolyBuffer->indicies[firstIndex + i] = firstVertex + i;

	}

	pPolyBuffer->numIndicies += 3;
	pPolyBuffer->numVerts += 3;
}

/*
**	CG_AtmosphericKludge
*/

static qboolean kludgeChecked, kludgeResult;
qboolean CG_AtmosphericKludge(void) {
	// Activate rain for specified kludge maps that don't
	// have it specified for them.

	if ( kludgeChecked ) {
		return( kludgeResult );
	}
	kludgeChecked = qtrue;
	kludgeResult = qfalse;

	/*if( !Q_stricmp( cgs.mapname, "maps/trainyard.bsp" ) )
	{
		//CG_EffectParse( "T=RAIN,B=5 10,C=0.5 2,G=0.5 2,BV=30 100,GV=20 80,W=1 2,D=1000 1000" );
		CG_EffectParse( "T=RAIN,B=5 10,C=0.5,G=0.5 2,BV=50 50,GV=200 200,W=1 2,D=1000" );
		return( kludgeResult = qtrue );
	}*/
/*	if( !Q_stricmp( cgs.mapname, "maps/mp_railgun.bsp" ) )
	{
		//CG_EffectParse( "T=RAIN,B=5 10,C=0.5 2,G=0.5 2,BV=30 100,GV=20 80,W=1 2,D=1000 1000" );
//		CG_EffectParse( "T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=50 50,GV=30 80,W=1 2,D=5000" );

		// snow storm, quite horizontally
		//CG_EffectParse( "T=SNOW,B=20 30,C=0.8,G=0.5 8,BV=100 100,GV=70 150,W=3 5,D=5000" );

		// mild snow storm, quite vertically - likely go for this
		//CG_EffectParse( "T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=20 30,GV=25 40,W=3 5,D=5000" );
		CG_EffectParse( "T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=20 30,GV=25 40,W=3 5,D=2000" );

		// cpu-cheap press event effect
		//CG_EffectParse( "T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=20 30,GV=25 40,W=3 5,D=500" );
//		CG_EffectParse( "T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=20 30,GV=25 40,W=3 5,D=750" );
		return( kludgeResult = qtrue );
	}*/

	/*if( !Q_stricmp( cgs.mapname, "maps/mp_goliath.bsp" ) ) {
		//CG_EffectParse( "T=SNOW,B=5 7,C=0.2,G=0.1 5,BV=15 25,GV=25 40,W=3 5,D=400" );
		CG_EffectParse( "T=SNOW,B=5 7,C=0.2,G=0.1 5,BV=15 25,GV=25 40,W=3 5,H=512,D=2000" );
		return( kludgeResult = qtrue );
	}*/
	/*if( !Q_stricmp( cgs.rawmapname, "sp_bruck_test006" ) ) {
		//T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=20 30,GV=25 40,W=3 5,H=608,D=2000
		CG_EffectParse( "T=SNOW,B=5 10,C=0.5,G=0.3 2,BV=20 30,GV=25 40,W=3 5,H=512,D=2000 4000" );
		//CG_EffectParse( "T=SNOW,B=5 7,C=0.2,G=0.1 5,BV=15 25,GV=25 40,W=3 5,H=512,D=2000" );
		return( kludgeResult = qtrue );
	}*/

	return( kludgeResult = qfalse );
}

typedef enum {
	ACT_NOT,
	ACT_FALLING
} active_t;

typedef struct cg_atmosphericParticle_s {
	vec3_t pos, delta, deltaNormalized, colour;
	float height, weight;
	active_t active;
	int nextDropTime;
	qhandle_t *effectshader;
} cg_atmosphericParticle_t;

typedef enum cg_particle_type_s {
    PARTICLE_RAIN = 0,
    PARTICLE_SPLASH1,
    PARTICLE_SPLASH2,
    PARTICLE_SPLASH3,

	NUM_PARTICLETYPES
} cg_particle_type_t;

typedef struct cg_particleType_s {
	float	s1, t1, s2, t2;
	float	scale;
} cg_particleType_t;

#define	PARTICLE_GRAVITY	-250

typedef	void (*partFunc_t) (struct cg_particle_s *p);

typedef struct cg_particle_s
{
	cg_effect_t				*effect;	
	struct cg_particle_s	*next;

	int						time;

	vec3_t					org;
	vec3_t					lastorg;
	vec3_t					vel;
	vec3_t					accel;
	vec3_t					color_rgb;
	float					colorvel;
	float					alpha;
	float					alphavel;

	cg_particle_type_t		type;
	int						dieTime;

	float					pscale;			//	scale size of particle
	float					scalevel;

	partFunc_t				func;			//	movement function for this particle (allows complex particle physics)
	short					flags;          //
} cg_particle_t;

#define MAX_PARTICLES 8192

typedef struct cg_atmosphericEffect_s {
	cg_atmosphericParticle_t atmoParticles[MAX_ATMOSPHERIC_PARTICLES];
	qhandle_t effectshaders[MAX_ATMOSPHERIC_EFFECTSHADERS];
	int lastRainTime, numDrops;
	int gustStartTime, gustEndTime;
	int baseStartTime, baseEndTime;
	int gustMinTime, gustMaxTime;
	int changeMinTime, changeMaxTime;
	int baseMinTime, baseMaxTime;
	float baseWeight, gustWeight;
	int baseDrops, gustDrops;
	int baseHeightOffset;
	int numEffectShaders;
	vec3_t baseVec, gustVec;

	vec3_t viewDir;

	void ( *ParticleUpdate )( int max, vec3_t currvec, float currweight );

	int dropsActive, oldDropsActive;
	int dropsRendered, dropsCreated, dropsSkipped;

	qhandle_t			particleShader;

	cg_particle_t		*activeParticles, *freeParticles;
	int         		numParticles;

	cg_particle_t		particles[MAX_PARTICLES]; 
	cg_particleType_t	particleTypes[NUM_PARTICLETYPES];

} cg_atmosphericEffect_t;

static cg_atmosphericEffect_t cg_atmFx;

static void CG_InitParticleType( cg_particleType_t *t, int s1, int t1, int s2, int t2, float scale ) {
	t->s1 = ( float )s1;
	t->t1 = ( float )t1;
	t->s2 = ( float )s2;
	t->t2 = ( float )t2;
	t->scale = scale;
}

void CG_InitParticles( void ) {
	int					i, w, h, numParticleTypes;
	cg_particleType_t	*p;

	cg_atmFx.particleShader = trap_R_RegisterShader( "gfx/misc/particles" );

	p = &cg_atmFx.particleTypes[PARTICLE_RAIN];
	CG_InitParticleType( p, 225, 32, 255, 63, 2.0f );
	p = &cg_atmFx.particleTypes[PARTICLE_SPLASH1];
	CG_InitParticleType( p, 96, 64, 127, 91, 4.0f );
	p = &cg_atmFx.particleTypes[PARTICLE_SPLASH2];
	CG_InitParticleType( p, 128, 64, 159, 91, 4.0f );
	p = &cg_atmFx.particleTypes[PARTICLE_SPLASH3];
	CG_InitParticleType( p, 160, 64, 191, 91, 4.0f );

	numParticleTypes = sizeof( cg_atmFx.particleTypes ) / sizeof( cg_atmFx.particleTypes[0] );

	w = 256;
	h = 128;
	for( i = 0; i < numParticleTypes; i++ )
	{
		p = &cg_atmFx.particleTypes[i];
		p->s1 /= w;
	    p->s2 /= w;
	    p->t1 /= h;
	    p->t2 /= h;

	    p->s1 += 1.0 / ( w * 2.0 );
	    p->s2 += 1.0 / ( w * 2.0 );
	    p->t1 += 1.0 / ( h * 2.0 );
	    p->t2 += 1.0 / ( h * 2.0 );
	}

	Com_Memset( cg_atmFx.particles, 0, sizeof( cg_atmFx.particles ) );

    cg_atmFx.freeParticles = &cg_atmFx.particles[0];
    cg_atmFx.activeParticles = NULL;
	cg_atmFx.numParticles = MAX_PARTICLES;

    for ( i = 0; i < cg_atmFx.numParticles; i++ )
    {
	    cg_atmFx.particles[i].next = &cg_atmFx.particles[i+1];
        cg_atmFx.particles[i].func = NULL;
    }
	cg_atmFx.particles[cg_atmFx.numParticles - 1].next = NULL;
}

static qboolean CG_SetParticleActive( cg_atmosphericParticle_t *particle, active_t active ) {
	particle->active = active;
	return active ? qtrue : qfalse;
}

#define	EFFECT_CLIP_DISTANCE		1024
#define	EFFECT_HALF_CLIP_DISTANCE	512

static qboolean CG_CullEffectParticles( cg_effect_t *effect ) {
	int			i;
	float		dotProduct = 0.0, distance, clipDistance, minDistance;
	vec3_t		forward, origin, vecToOrigin;
	qboolean	rain = qfalse;

	// get the forward facing vector
	VectorCopy( cg.refdef.viewaxis[0], forward );

	// copy the view origin
	// clear z
	VectorSet( origin, cg.refdef.vieworg[0], cg.refdef.vieworg[1], 0.0f );

	// rain clips closer because it's more dense up close
	if ( effect->type == EFFECT_RAIN )
	{
		rain = qtrue;
		minDistance = clipDistance = EFFECT_HALF_CLIP_DISTANCE;
	}
	else
	{
		minDistance = clipDistance = EFFECT_CLIP_DISTANCE;
	}

	//	clip against all 4 corners of the bounding box
	//	it's 4 corners because we're zeroing the z value, so
	//	it is as if the box is totally flat in the z dimension
	//	this handles larger particle volumes a bit better

	//  loop through the volume's precalculated corners
	for ( i = 0 ; i < 4 ; i++ ) {
		// get a vector from the corners to the view origin
		VectorSubtract( effect->corners[i], origin, vecToOrigin );

		// first, see if this point is in front of us
		dotProduct = DotProduct( forward, vecToOrigin );

		// this point is in front of the vieworg, don't cull, check the clip
		if ( dotProduct >= 0.0f ) {
			// if at any time the distance is less then the clip distance
			// then it is close enough so exit quickly
			distance = VectorLength( vecToOrigin );

			if ( distance < clipDistance ) {
				// so... this point is in front of us and within the 
				// clip distance so return false so that it is sent
				// to the renderer
				
				// if it's a rain volume, let's check the four corners and
				// return the smallest distance
				if ( !rain )
				{
					return qfalse;
				}
				else
				{
					// for rain, record the distance and go on to the next corner
					if (distance < minDistance)
						minDistance = distance;
				}
			}
		}
	}

	// if this is a rain volume and the minDistance is less than our clipDistance,
	// then it means we found the closest corner to us and it was in view
	if ( rain && ( minDistance < clipDistance ) )
	{
		effect->dist = minDistance;
		return qfalse;
	}

	return qtrue;
}

static void CG_GenerateEffectParticles( cg_effect_t *effect ) {
	int i, count;
	cg_particle_t *p;
	int width, height, depth;
	vec3_t velocity, pos;

	width = ( int )( effect->maxs[0] - effect->mins[0] );
	depth = ( int )( effect->maxs[1] - effect->mins[1] );
	height = ( int )( effect->maxs[2] - effect->mins[2] );

	if ( width <= 0 || depth <= 0 || height <= 0 ) {
		return;
	}

	VectorSet( velocity, 0, 0, -400 );

	// volumes that are closer get full density rain... ones farther
	// than half the clip distance get half density rain
	if ( effect->dist < EFFECT_HALF_CLIP_DISTANCE ) {
        effect->maxActive = ( width * depth )  * 0.01;
	} else {
        effect->maxActive = ( width * depth ) * 0.005;
	}
	if ( effect->numActive >= effect->maxActive ) {
		return;
	}

	if ( effect->numActive < effect->maxActive >> 2 )
		// fill the particle volume if it has few particles!
		count = effect->maxActive >> 2;
	else
		count = 1;

	// make sure we don't generate more particles then the volume maxes out at
	if ( ( count + effect->numActive ) > effect->maxActive )
		count = effect->maxActive - effect->numActive;

	for ( i = 0 ; i < count ; i++ )
	{
		if ( !cg_atmFx.freeParticles ) {
			return;
		}

		p = cg_atmFx.freeParticles;
		cg_atmFx.freeParticles = p->next;
		p->next = cg_atmFx.activeParticles;
		cg_atmFx.activeParticles = p;

		p->org[0] = effect->mins[0] + ( (rand() % width ) - 8); 
		p->org[1] = effect->mins[1] + ( (rand() % depth ) - 8); 
		p->org[2] = effect->mins[2] + ( (rand() % height ) - 8);

		VectorCopy( velocity, p->vel );

		//	actual distance particle will fall
		float fallHeight = height - (effect->maxs[2] - p->org[2]);
		p->time = cg.time;
		//	time particle should die
		p->dieTime = cg.time - ((fallHeight / p->vel[2]) * 1000);

		//	no acceleration via gravity
		VectorClear( p->accel );

		p->alpha = 0.4;
		p->alphavel = -0.1;

		p->effect = effect;
		p->effect->numActive++;

		// set the particle type and the directional flags
		p->type = PARTICLE_RAIN;
		//p->flags |= particleFlags;
		VectorSet( p->color_rgb, 1, 1, 1 );
	}

	// add the splash sprites (again, make closer particle volumes more dense)
	// TODO: dist is the distance of eye to the volume
	int splashSprites;
	if ( effect->dist < EFFECT_HALF_CLIP_DISTANCE ) {
		splashSprites = (width * depth) >> 13; 
	} else {
        splashSprites = (width * depth) >> 14; 
	}
	
	// add the rain particle splashes
	for ( i = 0 ; i < splashSprites ; i++ )
	{
		if ( !cg_atmFx.freeParticles )
			return;

		pos[0]	= effect->mins[0] + (rand() % width ); 
		pos[1]	= effect->mins[1] + (rand() % depth ); 
		pos[2]	= effect->mins[2];

		p = cg_atmFx.freeParticles;
		cg_atmFx.freeParticles = p->next;
		p->next = cg_atmFx.activeParticles;
		cg_atmFx.activeParticles = p;

		VectorCopy( pos, p->org );

		// not associated with a particle effect
		p->effect = NULL;
		//	no acceleration via gravity or velocity
		VectorClear( p->accel );
		VectorClear( p->vel );

		//	time particle should die
		p->dieTime = cg.time + 100;

		p->flags = 0;
		p->alpha = 0.40f;
		p->alphavel = -0.1f;

        p->type = PARTICLE_SPLASH1 + (rand()&2);
		VectorSet( p->color_rgb, 1.0f, 1.0f, 1.0f );
		p->pscale = 1.0f;
	}
}

static void CG_UpdateParticles( void ) {
    cg_particle_t	*p, *next;
    float           alpha, pscale;
    float           time, time2;
    vec3_t         	org;
    cg_particle_t	*active, *tail;

    active = NULL;
    tail = NULL;

	if ( cg_atmosphericEffectsPause.integer ) {
		// debug aid
		return;
	}

    for ( p = cg_atmFx.activeParticles ; p ; p = next ) {
        next = p->next;

        time = ( cg.time - p->time ) * 0.001f;
        alpha = p->alpha + time * p->alphavel;
        if ( alpha <= 0 && p->dieTime == 0 ) {
            if ( p->effect && p->effect->numActive > 0 ) {
				p->effect->numActive--;
			}

            // faded out
            p->func = NULL;
            p->next = cg_atmFx.freeParticles;
            p->effect = NULL;
            cg_atmFx.freeParticles = p;

            continue;
        }

        if ( p->dieTime > 0 && p->dieTime <= cg.time ) {
            if ( p->effect && p->effect->numActive > 0 ) {
				p->effect->numActive--;
			}

			p->func = NULL;
			p->dieTime = 0;
			p->next = cg_atmFx.freeParticles;
			p->effect = NULL;
			cg_atmFx.freeParticles = p;
			continue;
        } 

        p->next = NULL;
        if ( !tail ) {
		    active = tail = p;
        } else {
            tail->next = p;
            tail = p;
        }

        if ( alpha > 1.0 ) {
		    alpha = 1;
        }

		pscale = p->pscale;

		time2 = time*time;

		org[0] = p->org[0] + p->vel[0]*time + ( p->accel[0] * time2 * 0.5 );
		org[1] = p->org[1] + p->vel[1]*time + ( p->accel[1] * time2 * 0.5 );
		org[2] = p->org[2] + p->vel[2]*time + ( p->accel[2] * time2 * 0.5 );

		// update particle state
		VectorCopy(org, p->org);
		p->alpha = alpha;
		p->pscale = pscale;
    }

    cg_atmFx.activeParticles = active;
}

static void CG_DrawParticles( void ) {
	polyVert_t		verts[3];
	cg_particle_t	*p, *next;
	float			scale, alpha;
	vec3_t			newpt, endpt, normal, viewNormal, org, forward, up, right;
	byte			color[4];
	int				i;

    for ( p = cg_atmFx.activeParticles; p; p = next ) {
        next = p->next;

		scale = ( p->org[0] - cg.refdef.vieworg[0] ) * cg.refdef.viewaxis[0][0] + 
			    ( p->org[1] - cg.refdef.vieworg[1] ) * cg.refdef.viewaxis[0][1] +
			    ( p->org[2] - cg.refdef.vieworg[2] ) * cg.refdef.viewaxis[0][2];

		if ( p->type == PARTICLE_RAIN ) {
			// TODO: lots of rain particles will have the same velocity,
			// so precalculate these vectors

			VectorMA( cg.refdef.vieworg, 5.0f, cg.refdef.viewaxis[0], newpt );
			VectorAdd( newpt, p->vel, endpt );
			VectorSubtract( endpt, newpt, normal );
			VectorSubtract( newpt, cg.refdef.vieworg, viewNormal );

			CrossProduct( viewNormal, normal, right );
			float dist = VectorLength( right );
			if ( dist < 1.001 ) {
				continue;
			}
			for ( i = 0 ; i < 3 ; i++ ) {
				right[i] = right[i] * ( -1.0 / dist ) * 0.8;
			}
			VectorNormalize( normal );
			VectorScale( normal, 20.0f, forward );
			
			VectorScale( forward, cg_atmFx.particleTypes[p->type].scale * 0.8, up );
			right[0] = right[0] * 0.8 * cg_atmFx.particleTypes[p->type].scale;
			right[1] = right[1] * 0.8 * cg_atmFx.particleTypes[p->type].scale;
			right[2] = right[2] * 0.8 * cg_atmFx.particleTypes[p->type].scale;

			if ( scale < 0 )
			{
				if ( scale > -20 ) {
					scale = -1;
				} else {
					scale = -1 + scale * 0.004;
				}
			}
			else
			{
				if ( scale < 20 ) {
					scale = 1;
				} else {
					scale = 1 + scale * 0.004;
				}
			}
			if ( p->pscale ) {
				scale = scale * p->pscale;
			}
			alpha = p->alpha * 1.0 / scale;
		} else {
			VectorScale( cg.refdef.viewaxis[2], cg_atmFx.particleTypes[p->type].scale, up );
			VectorScale( cg.refdef.viewaxis[1], cg_atmFx.particleTypes[p->type].scale, right );

			if ( scale < 20 )
				scale = 1;
			else
				scale = 1 + scale * 0.004;

			if( p->pscale ) {
				scale = scale * p->pscale;
			}
			alpha = p->alpha;
		}

		if( p->pscale > 1.0 ) {
			scale = p->pscale;
		} else {
			if (scale < 20) {
				scale = 1;
			} else {
				scale = 1 + scale * 0.004;
			}
		}

		org[0] = p->org[0] - ( up[0] + right[0] ) * scale * 0.33;
		org[1] = p->org[1] - ( up[1] + right[1] ) * scale * 0.33;
		org[2] = p->org[2] - ( up[2] + right[2] ) * scale * 0.33;

		color[0] = ( byte )( Com_Clamp( 0.0f, 1.0f, p->color_rgb[0] ) * 255.0f );
		color[1] = ( byte )( Com_Clamp( 0.0f, 1.0f, p->color_rgb[1] ) * 255.0f );
		color[2] = ( byte )( Com_Clamp( 0.0f, 1.0f, p->color_rgb[2] ) * 255.0f );
		color[3] = ( byte )( Com_Clamp( 0.0f, 1.0f, alpha ) * 255.0f );

		VectorCopy( org, verts[0].xyz );
		verts[0].st[0] = cg_atmFx.particleTypes[p->type].s1;
		verts[0].st[1] = cg_atmFx.particleTypes[p->type].t1;
		Com_Memcpy( verts[0].modulate, color, sizeof(color) );

		verts[1].st[0] = cg_atmFx.particleTypes[p->type].s2;
		verts[1].st[1] = cg_atmFx.particleTypes[p->type].t1;
		VectorMA( org, scale, up, verts[1].xyz );
		Com_Memcpy( verts[1].modulate, color, sizeof(color) );

		verts[2].st[0] = cg_atmFx.particleTypes[p->type].s1;
		verts[2].st[1] = cg_atmFx.particleTypes[p->type].t2;
		VectorMA( org, scale, right, verts[2].xyz );
		Com_Memcpy( verts[2].modulate, color, sizeof(color) );

		CG_AddPolyToPool( cg_atmFx.particleShader, verts );
	}
}

/*
**	Raindrop management functions
*/

static qboolean CG_RainParticleGenerate( cg_atmosphericParticle_t *particle, vec3_t currvec, float currweight ) {
	// Attempt to 'spot' a raindrop somewhere below a sky texture.

	float angle, distance;
	float groundHeight, skyHeight;
//	int msec = trap_Milliseconds();

//	n_generatetime++;

	angle = random() * 2 * M_PI;
	distance = 20 + MAX_ATMOSPHERIC_DISTANCE * random();

	particle->pos[0] = cg.refdef.vieworg[0] + sin( angle ) * distance;
	particle->pos[1] = cg.refdef.vieworg[1] + cos( angle ) * distance;

	// ydnar: choose a spawn point randomly between sky and ground
	skyHeight = BG_GetSkyHeightAtPoint( particle->pos );
	if ( skyHeight == MAX_ATMOSPHERIC_HEIGHT ) {
		return qfalse;
	}
	groundHeight = BG_GetSkyGroundHeightAtPoint( particle->pos );
	if ( groundHeight >= skyHeight ) {
		return qfalse;
	}
	particle->pos[2] = groundHeight + random() * ( skyHeight - groundHeight );

	// make sure it doesn't fall from too far cause it then will go over our heads ('lower the ceiling')
	if ( cg_atmFx.baseHeightOffset > 0 ) {
		if ( particle->pos[2] - cg.refdef.vieworg[2] > cg_atmFx.baseHeightOffset ) {
			particle->pos[2] = cg.refdef.vieworg[2] + cg_atmFx.baseHeightOffset;

			if ( particle->pos[2] < groundHeight ) {
				return qfalse;
			}
		}
	}

	// ydnar: rain goes in bursts
	{
		float maxActiveDrops;

		// every 10 seconds allow max raindrops
		maxActiveDrops = 0.50 * cg_atmFx.numDrops + 0.001 * cg_atmFx.numDrops * ( 10000 - ( cg.time % 10000 ) );
		if ( cg_atmFx.oldDropsActive > maxActiveDrops ) {
			return qfalse;
		}
	}

	CG_SetParticleActive( particle, ACT_FALLING );
	particle->colour[0] = 0.6 + 0.2 * random() * 0xFF;
	particle->colour[1] = 0.6 + 0.2 * random() * 0xFF;
	particle->colour[2] = 0.6 + 0.2 * random() * 0xFF;
	VectorCopy( currvec, particle->delta );
	particle->delta[2] += crandom() * 100;
	VectorCopy( particle->delta, particle->deltaNormalized );
	VectorNormalizeFast( particle->deltaNormalized );
	particle->height = ATMOSPHERIC_RAIN_HEIGHT + crandom() * 100;
	particle->weight = currweight;

	if (cg_atmFx.numEffectShaders > 1) {
		particle->effectshader = &cg_atmFx.effectshaders[ rand()%cg_atmFx.numEffectShaders ];
	} else {
		particle->effectshader = &cg_atmFx.effectshaders[0];
	}

//	generatetime += trap_Milliseconds() - msec;
	return( qtrue );
}

static qboolean CG_RainParticleCheckVisible( cg_atmosphericParticle_t *particle ) {
	// Check the raindrop is visible and still going, wrapping if necessary.

	float moved;
	vec2_t distance;
//	int msec = trap_Milliseconds();

	if ( !particle || particle->active == ACT_NOT ) {
//		checkvisibletime += trap_Milliseconds() - msec;
		return( qfalse );
	}

	moved = ( cg.time - cg_atmFx.lastRainTime ) * 0.001;  // Units moved since last frame
	VectorMA( particle->pos, moved, particle->delta, particle->pos );
	if ( particle->pos[2] + particle->height < BG_GetSkyGroundHeightAtPoint( particle->pos ) ) {
//		checkvisibletime += trap_Milliseconds() - msec;
		return CG_SetParticleActive( particle, ACT_NOT );
	}

	distance[0] = particle->pos[0] - cg.refdef.vieworg[0];
	distance[1] = particle->pos[1] - cg.refdef.vieworg[1];
	if ( ( distance[0] * distance[0] + distance[1] * distance[1] ) > Square( MAX_ATMOSPHERIC_DISTANCE ) ) {
		// ydnar: just nuke this particle, let it respawn
		return CG_SetParticleActive( particle, ACT_NOT );

		/*
		// Attempt to respot the particle at our other side
		particle->pos[0] -= 1.85f * distance[0];
		particle->pos[1] -= 1.85f * distance[1];

		// Valid spot?
		pointHeight = BG_GetSkyHeightAtPoint( particle->pos );
		if( pointHeight == MAX_ATMOSPHERIC_HEIGHT ) {
//			checkvisibletime += trap_Milliseconds() - msec;
			return CG_SetParticleActive( particle, ACT_NOT );
		}

		pointHeight = BG_GetSkyGroundHeightAtPoint( particle->pos );
		if( pointHeight == MAX_ATMOSPHERIC_HEIGHT || pointHeight >= particle->pos[2] ) {
//			checkvisibletime += trap_Milliseconds() - msec;
			return CG_SetParticleActive( particle, ACT_NOT );
		}
		*/
	}

//	checkvisibletime += trap_Milliseconds() - msec;
	return( qtrue );
}

static void CG_RainParticleRender( cg_atmosphericParticle_t *particle ) {
	// Draw a raindrop

	vec3_t forward, right;
	polyVert_t verts[3];
	vec2_t line;
	float len, dist;
	vec3_t start, finish;
	float groundHeight;
//	int			msec = trap_Milliseconds();

//	n_rendertime++;

	if ( particle->active == ACT_NOT ) {
//		rendertime += trap_Milliseconds() - msec;
		return;
	}

	if ( CG_CullPoint( particle->pos ) ) {
		return;
	}

	VectorCopy( particle->pos, start );

	dist = DistanceSquared( particle->pos, cg.refdef.vieworg );

	// Make sure it doesn't clip through surfaces
	groundHeight = BG_GetSkyGroundHeightAtPoint( start );
	len = particle->height;
	if ( start[2] <= groundHeight ) {
		// Stop snow going through surfaces.
		len = particle->height - groundHeight + start[2];
		VectorMA( start, len - particle->height, particle->deltaNormalized, start );
	}

	if ( len <= 0 ) {
//		rendertime += trap_Milliseconds() - msec;
		return;
	}

	// fade nearby rain particles
	if ( dist < Square( 128.f ) ) {
		dist = .25f + .75f * ( dist / Square( 128.f ) );
	} else {
		dist = 1.0f;
	}

	VectorCopy( particle->deltaNormalized, forward );
	VectorMA( start, -len, forward, finish );

	line[0] = DotProduct( forward, cg.refdef.viewaxis[1] );
	line[1] = DotProduct( forward, cg.refdef.viewaxis[2] );

	VectorScale( cg.refdef.viewaxis[1], line[1], right );
	VectorMA( right, -line[0], cg.refdef.viewaxis[2], right );
	VectorNormalize( right );

	// dist = 1.0;

	VectorCopy( finish, verts[0].xyz );
	verts[0].st[0] = 0.5f;
	verts[0].st[1] = 0;
	verts[0].modulate[0] = particle->colour[0];
	verts[0].modulate[1] = particle->colour[1];
	verts[0].modulate[2] = particle->colour[2];
	verts[0].modulate[3] = 100 * dist;

	VectorMA( start, -particle->weight, right, verts[1].xyz );
	verts[1].st[0] = 0;
	verts[1].st[1] = 1;
	verts[1].modulate[0] = particle->colour[0];
	verts[1].modulate[1] = particle->colour[1];
	verts[2].modulate[2] = particle->colour[2];
	verts[1].modulate[3] = 200 * dist;

	VectorMA( start, particle->weight, right, verts[2].xyz );
	verts[2].st[0] = 1;
	verts[2].st[1] = 1;
	verts[2].modulate[0] = particle->colour[0];
	verts[2].modulate[1] = particle->colour[1];
	verts[2].modulate[2] = particle->colour[2];
	verts[2].modulate[3] = 200 * dist;

	CG_AddPolyToPool( *particle->effectshader, verts );

//	rendertime += trap_Milliseconds() - msec;
}

static void CG_RainParticleUpdate( int max, vec3_t currvec, float currweight ) {
	int curr;
	cg_atmosphericParticle_t *particle;

	for ( curr = 0; curr < max; curr++ )
	{
		particle = &cg_atmFx.atmoParticles[curr];
		if ( !CG_RainParticleCheckVisible( particle ) ) {
			if ( !CG_RainParticleGenerate( particle, currvec, currweight ) ) {
				// Ensure it doesn't attempt to generate every frame, to prevent
				// 'clumping' when there's only a small sky area available.
				particle->nextDropTime = cg.time + ATMOSPHERIC_DROPDELAY;
				continue;
			} else {
				cg_atmFx.dropsCreated++;
			}
		}

		CG_RainParticleRender( particle );
		cg_atmFx.dropsActive++;
	}
}

/*
**	Snow management functions
*/

static qboolean CG_SnowParticleGenerate( cg_atmosphericParticle_t *particle, vec3_t currvec, float currweight ) {
	// Attempt to 'spot' a snowflake somewhere below a sky texture.

	float angle, distance;
	float groundHeight, skyHeight;
//	int msec = trap_Milliseconds();

//	n_generatetime++;

	angle = random() * 2 * M_PI;
	distance = 20 + MAX_ATMOSPHERIC_DISTANCE * random();

	particle->pos[0] = cg.refdef.vieworg[0] + sin( angle ) * distance;
	particle->pos[1] = cg.refdef.vieworg[1] + cos( angle ) * distance;

	// ydnar: choose a spawn point randomly between sky and ground
	skyHeight = BG_GetSkyHeightAtPoint( particle->pos );
	if ( skyHeight == MAX_ATMOSPHERIC_HEIGHT ) {
		return qfalse;
	}
	groundHeight = BG_GetSkyGroundHeightAtPoint( particle->pos );
	if ( groundHeight >= skyHeight ) {
		return qfalse;
	}
	particle->pos[2] = groundHeight + random() * ( skyHeight - groundHeight );

	// make sure it doesn't fall from too far cause it then will go over our heads ('lower the ceiling')
	if ( cg_atmFx.baseHeightOffset > 0 ) {
		if ( particle->pos[2] - cg.refdef.vieworg[2] > cg_atmFx.baseHeightOffset ) {
			particle->pos[2] = cg.refdef.vieworg[2] + cg_atmFx.baseHeightOffset;
			if ( particle->pos[2] < groundHeight ) {
				return qfalse;
			}
		}
	}

	CG_SetParticleActive( particle, ACT_FALLING );
	VectorCopy( currvec, particle->delta );
	particle->delta[2] += crandom() * 25;
	VectorCopy( particle->delta, particle->deltaNormalized );
	VectorNormalizeFast( particle->deltaNormalized );
	particle->height = ATMOSPHERIC_SNOW_HEIGHT + random() * 2;
	particle->weight = particle->height * 0.5f;

	if (cg_atmFx.numEffectShaders > 1) {
		particle->effectshader = &cg_atmFx.effectshaders[ rand()%cg_atmFx.numEffectShaders ];
	} else {
		particle->effectshader = &cg_atmFx.effectshaders[0];
	}

//	generatetime += trap_Milliseconds() - msec;
	return( qtrue );
}

static qboolean CG_SnowParticleCheckVisible( cg_atmosphericParticle_t *particle ) {
	// Check the snowflake is visible and still going, wrapping if necessary.

	float moved;
	vec2_t distance;
//	int msec = trap_Milliseconds();

//	n_checkvisibletime++;

	if ( !particle || particle->active == ACT_NOT ) {
//		checkvisibletime += trap_Milliseconds() - msec;
		return( qfalse );
	}

	moved = ( cg.time - cg_atmFx.lastRainTime ) * 0.001;  // Units moved since last frame
	VectorMA( particle->pos, moved, particle->delta, particle->pos );
	if ( particle->pos[2] < BG_GetSkyGroundHeightAtPoint( particle->pos ) ) {
//		checkvisibletime += trap_Milliseconds() - msec;
		return CG_SetParticleActive( particle, ACT_NOT );
	}

	distance[0] = particle->pos[0] - cg.refdef.vieworg[0];
	distance[1] = particle->pos[1] - cg.refdef.vieworg[1];
	if ( ( distance[0] * distance[0] + distance[1] * distance[1] ) > Square( MAX_ATMOSPHERIC_DISTANCE ) ) {
		// ydnar: just nuke this particle, let it respawn
		return CG_SetParticleActive( particle, ACT_NOT );

		/*
		// Attempt to respot the particle at our other side
		particle->pos[0] -= 1.85f * distance[0];
		particle->pos[1] -= 1.85f * distance[1];

		// ydnar: place particle in random position between ground and sky
		groundHeight = BG_GetSkyGroundHeightAtPoint( particle->pos );
		skyHeight = BG_GetSkyHeightAtPoint( particle->pos );
		if( skyHeight == MAX_ATMOSPHERIC_HEIGHT )
			return CG_SetParticleActive( particle, ACT_NOT );
		particle->pos[ 2 ] = groundHeight + random() * (skyHeight - groundHeight);

		// ydnar: valid spot?
		if( particle->pos[ 2 ] <= groundHeight || particle->pos[ 2 ] >= skyHeight )
			return CG_SetParticleActive( particle, ACT_NOT );
		*/
	}

//	checkvisibletime += trap_Milliseconds() - msec;
	return( qtrue );
}

static void CG_SnowParticleRender( cg_atmosphericParticle_t *particle ) {
	// Draw a snowflake

	vec3_t forward, right;
	polyVert_t verts[3];
	vec2_t line;
	float len, sinTumbling, cosTumbling, particleWidth, dist;
	vec3_t start, finish;
	float groundHeight;
//	int			msec = trap_Milliseconds();

//	n_rendertime++;

	if ( particle->active == ACT_NOT ) {
//		rendertime += trap_Milliseconds() - msec;
		return;
	}

	if ( CG_CullPoint( particle->pos ) ) {
		return;
	}

	VectorCopy( particle->pos, start );

	sinTumbling = sin( particle->pos[2] * 0.03125f * ( 0.5f * particle->weight ) );
	cosTumbling = cos( ( particle->pos[2] + particle->pos[1] ) * 0.03125f * ( 0.5f * particle->weight ) );
	start[0] += 24 * ( 1 - particle->deltaNormalized[2] ) * sinTumbling;
	start[1] += 24 * ( 1 - particle->deltaNormalized[2] ) * cosTumbling;

	// Make sure it doesn't clip through surfaces
	groundHeight = BG_GetSkyGroundHeightAtPoint( start );
	len = particle->height;
	if ( start[2] <= groundHeight ) {
		// Stop snow going through surfaces.
		len = particle->height - groundHeight + start[2];
		VectorMA( start, len - particle->height, particle->deltaNormalized, start );
	}

	if ( len <= 0 ) {
//		rendertime += trap_Milliseconds() - msec;
		return;
	}

	line[0] = particle->pos[0] - cg.refdef.vieworg[0];
	line[1] = particle->pos[1] - cg.refdef.vieworg[1];

	dist = DistanceSquared( particle->pos, cg.refdef.vieworg );
	// dist becomes scale
	if ( dist > Square( 500.f ) ) {
		dist = 1.f + ( ( dist - Square( 500.f ) ) * ( 10.f / Square( 2000.f ) ) );
	} else {
		dist = 1.f;
	}

	len *= dist;

	VectorCopy( particle->deltaNormalized, forward );
	VectorMA( start, -( len /** sinTumbling*/ ), forward, finish );

	line[0] = DotProduct( forward, cg.refdef.viewaxis[1] );
	line[1] = DotProduct( forward, cg.refdef.viewaxis[2] );

	VectorScale( cg.refdef.viewaxis[1], line[1], right );
	VectorMA( right, -line[0], cg.refdef.viewaxis[2], right );
	VectorNormalize( right );

	particleWidth = dist * ( /*cosTumbling **/ particle->weight );

	VectorMA( finish, -particleWidth, right, verts[0].xyz );
	verts[0].st[0] = 0;
	verts[0].st[1] = 0;
	verts[0].modulate[0] = 255;
	verts[0].modulate[1] = 255;
	verts[0].modulate[2] = 255;
	verts[0].modulate[3] = 255;

	VectorMA( start, -particleWidth, right, verts[1].xyz );
	verts[1].st[0] = 0;
	verts[1].st[1] = 1;
	verts[1].modulate[0] = 255;
	verts[1].modulate[1] = 255;
	verts[1].modulate[2] = 255;
	verts[1].modulate[3] = 255;

	VectorMA( start, particleWidth, right, verts[2].xyz );
	verts[2].st[0] = 1;
	verts[2].st[1] = 1;
	verts[2].modulate[0] = 255;
	verts[2].modulate[1] = 255;
	verts[2].modulate[2] = 255;
	verts[2].modulate[3] = 255;

	CG_AddPolyToPool( *particle->effectshader, verts );

//	rendertime += trap_Milliseconds() - msec;
}

static void CG_SnowParticleUpdate( int max, vec3_t currvec, float currweight ) {
	int curr;
	cg_atmosphericParticle_t *particle;

	for ( curr = 0; curr < max; curr++ )
	{
		particle = &cg_atmFx.atmoParticles[curr];
		if ( !CG_SnowParticleCheckVisible( particle ) ) {
			if ( !CG_SnowParticleGenerate( particle, currvec, currweight ) ) {
				// Ensure it doesn't attempt to generate every frame, to prevent
				// 'clumping' when there's only a small sky area available.
				particle->nextDropTime = cg.time + ATMOSPHERIC_DROPDELAY;
				continue;
			} else {
				cg_atmFx.dropsCreated++;
			}
		}

		CG_SnowParticleRender( particle );
		cg_atmFx.dropsActive++;
	}
}

/*
**	Set up gust parameters.
*/

static void CG_EffectGust(void) {
	// Generate random values for the next gust

	int diff;

	cg_atmFx.baseEndTime        = cg.time                   + cg_atmFx.baseMinTime      + ( rand() % ( cg_atmFx.baseMaxTime - cg_atmFx.baseMinTime ) );
	diff                        = cg_atmFx.changeMaxTime    - cg_atmFx.changeMinTime;
	cg_atmFx.gustStartTime      = cg_atmFx.baseEndTime      + cg_atmFx.changeMinTime    + ( diff ? ( rand() % diff ) : 0 );
	diff                        = cg_atmFx.gustMaxTime      - cg_atmFx.gustMinTime;
	cg_atmFx.gustEndTime        = cg_atmFx.gustStartTime    + cg_atmFx.gustMinTime      + ( diff ? ( rand() % diff ) : 0 );
	diff                        = cg_atmFx.changeMaxTime    - cg_atmFx.changeMinTime;
	cg_atmFx.baseStartTime      = cg_atmFx.gustEndTime      + cg_atmFx.changeMinTime    + ( diff ? ( rand() % diff ) : 0 );
}

static qboolean CG_EffectGustCurrent( vec3_t curr, float *weight, int *num ) {
	// Calculate direction for new drops.

	vec3_t temp;
	float frac;

	if ( cg.time < cg_atmFx.baseEndTime ) {
		VectorCopy( cg_atmFx.baseVec, curr );
		*weight = cg_atmFx.baseWeight;
		*num = cg_atmFx.baseDrops;
	} else {
		VectorSubtract( cg_atmFx.gustVec, cg_atmFx.baseVec, temp );
		if ( cg.time < cg_atmFx.gustStartTime ) {
			frac = ( (float)( cg.time - cg_atmFx.baseEndTime ) ) / ( (float)( cg_atmFx.gustStartTime - cg_atmFx.baseEndTime ) );
			VectorMA( cg_atmFx.baseVec, frac, temp, curr );
			*weight = cg_atmFx.baseWeight + ( cg_atmFx.gustWeight - cg_atmFx.baseWeight ) * frac;
			*num = cg_atmFx.baseDrops + ( (float)( cg_atmFx.gustDrops - cg_atmFx.baseDrops ) ) * frac;
		} else if ( cg.time < cg_atmFx.gustEndTime )    {
			VectorCopy( cg_atmFx.gustVec, curr );
			*weight = cg_atmFx.gustWeight;
			*num = cg_atmFx.gustDrops;
		} else
		{
			frac = 1.0 - ( (float)( cg.time - cg_atmFx.gustEndTime ) ) / ( (float)( cg_atmFx.baseStartTime - cg_atmFx.gustEndTime ) );
			VectorMA( cg_atmFx.baseVec, frac, temp, curr );
			*weight = cg_atmFx.baseWeight + ( cg_atmFx.gustWeight - cg_atmFx.baseWeight ) * frac;
			*num = cg_atmFx.baseDrops + ( (float)( cg_atmFx.gustDrops - cg_atmFx.baseDrops ) ) * frac;
			if ( cg.time >= cg_atmFx.baseStartTime ) {
				return( qtrue );
			}
		}
	}
	return( qfalse );
}

static void CG_EP_ParseFloats( char *floatstr, float *f1, float *f2 ) {
	// Parse the float or floats

	char *middleptr;
	char buff[64];

	Q_strncpyz( buff, floatstr, sizeof( buff ) );
	for ( middleptr = buff; *middleptr && *middleptr != ' '; middleptr++ ) ;
	if ( *middleptr ) {
		*middleptr++ = 0;
		*f1 = atof( floatstr );
		*f2 = atof( middleptr );
	} else {
		*f1 = *f2 = atof( floatstr );
	}
}

static void CG_EP_ParseInts( char *intstr, int *i1, int *i2 ) {
	// Parse the int or ints

	char *middleptr;
	char buff[64];

	Q_strncpyz( buff, intstr, sizeof( buff ) );
	for ( middleptr = buff; *middleptr && *middleptr != ' '; middleptr++ ) ;
	if ( *middleptr ) {
		*middleptr++ = 0;
		*i1 = atof( intstr );
		*i2 = atof( middleptr );
	} else {
		*i1 = *i2 = atof( intstr );
	}
}

void CG_EffectParse( const char *effectstr ) {
	// Split the string into it's component parts.

	float bmin, bmax, cmin, cmax, gmin, gmax, bdrop, gdrop /*, wsplash, lsplash*/;
	int count, bheight;
	char *startptr, *eqptr, *endptr;
	char workbuff[128];
	atmFXType_t atmFXType = ATM_NONE;
	int i;

	if ( CG_AtmosphericKludge() ) {
		return;
	}

	// Set up some default values
	cg_atmFx.baseVec[0] = cg_atmFx.baseVec[1] = 0;
	cg_atmFx.gustVec[0] = cg_atmFx.gustVec[1] = 100;
	bmin = 5;
	bmax = 10;
	cmin = 1;
	cmax = 1;
	gmin = 0;
	gmax = 2;
	bdrop = gdrop = 300;
	cg_atmFx.baseWeight = 0.7f;
	cg_atmFx.gustWeight = 1.5f;
	bheight = 0;

	// Parse the parameter string
	Q_strncpyz( workbuff, effectstr, sizeof( workbuff ) );
	for ( startptr = workbuff; *startptr; )
	{
		for ( eqptr = startptr; *eqptr && *eqptr != '=' && *eqptr != ','; eqptr++ ) ;
		if ( !*eqptr ) {
			break;          // No more string
		}
		if ( *eqptr == ',' ) {
			startptr = eqptr + 1;   // Bad argument, continue
			continue;
		}
		*eqptr++ = 0;
		for ( endptr = eqptr; *endptr && *endptr != ','; endptr++ ) ;
		if ( *endptr ) {
			*endptr++ = 0;
		}

		if ( atmFXType == ATM_NONE ) {
			if ( Q_stricmp( startptr, "T" ) ) {
				cg_atmFx.numDrops = 0;
				CG_Printf( "Atmospheric effect must start with a type.\n" );
				return;
			}
			if ( !Q_stricmp( eqptr, "RAIN" ) ) {
				atmFXType = ATM_RAIN;
				cg_atmFx.ParticleUpdate = &CG_RainParticleUpdate;

				cg_atmFx.baseVec[2] = cg_atmFx.gustVec[2] = -ATMOSPHERIC_RAIN_SPEED;
			} else if ( !Q_stricmp( eqptr, "SNOW" ) ) {
				atmFXType = ATM_SNOW;
				cg_atmFx.ParticleUpdate = &CG_SnowParticleUpdate;

				cg_atmFx.baseVec[2] = cg_atmFx.gustVec[2] = -ATMOSPHERIC_SNOW_SPEED;
			} else {
				cg_atmFx.numDrops = 0;
				CG_Printf( "Only effect type 'rain' and 'snow' are supported.\n" );
				return;
			}
		} else {
			if ( !Q_stricmp( startptr, "B" ) ) {
				CG_EP_ParseFloats( eqptr, &bmin, &bmax );
			} else if ( !Q_stricmp( startptr, "C" ) ) {
				CG_EP_ParseFloats( eqptr, &cmin, &cmax );
			} else if ( !Q_stricmp( startptr, "G" ) ) {
				CG_EP_ParseFloats( eqptr, &gmin, &gmax );
			} else if ( !Q_stricmp( startptr, "BV" ) ) {
				CG_EP_ParseFloats( eqptr, &cg_atmFx.baseVec[0], &cg_atmFx.baseVec[1] );
			} else if ( !Q_stricmp( startptr, "GV" ) ) {
				CG_EP_ParseFloats( eqptr, &cg_atmFx.gustVec[0], &cg_atmFx.gustVec[1] );
			} else if ( !Q_stricmp( startptr, "W" ) ) {
				CG_EP_ParseFloats( eqptr, &cg_atmFx.baseWeight, &cg_atmFx.gustWeight );
			} else if ( !Q_stricmp( startptr, "D" ) ) {
				CG_EP_ParseFloats( eqptr, &bdrop, &gdrop );
			} else if ( !Q_stricmp( startptr, "H" ) ) {
				CG_EP_ParseInts( eqptr, &bheight, &bheight );
			} else { CG_Printf( "Unknown effect key '%s'.\n", startptr );}
		}
		startptr = endptr;
	}

	if ( atmFXType == ATM_NONE || !BG_LoadTraceMap( cgs.mapname, cg.mapcoordsMins, cg.mapcoordsMaxs ) ) {
		// No effects
		cg_atmFx.numDrops = -1;
		return;
	}

	cg_atmFx.baseHeightOffset = bheight;
	if ( cg_atmFx.baseHeightOffset < 0 ) {
		cg_atmFx.baseHeightOffset = 0;
	}
	cg_atmFx.baseMinTime = 1000 * bmin;
	cg_atmFx.baseMaxTime = 1000 * bmax;
	cg_atmFx.changeMinTime = 1000 * cmin;
	cg_atmFx.changeMaxTime = 1000 * cmax;
	cg_atmFx.gustMinTime = 1000 * gmin;
	cg_atmFx.gustMaxTime = 1000 * gmax;
	cg_atmFx.baseDrops = bdrop;
	cg_atmFx.gustDrops = gdrop;

	cg_atmFx.numDrops = ( cg_atmFx.baseDrops > cg_atmFx.gustDrops ) ? cg_atmFx.baseDrops : cg_atmFx.gustDrops;
	if ( cg_atmFx.numDrops > MAX_ATMOSPHERIC_PARTICLES ) {
		cg_atmFx.numDrops = MAX_ATMOSPHERIC_PARTICLES;
	}

	// Load graphics
	if ( atmFXType == ATM_RAIN ) {
		// Rain
		cg_atmFx.effectshaders[0] = trap_R_RegisterShader( "gfx/misc/raindrop" );
		cg_atmFx.numEffectShaders = 1;

		for (i = 1; i < MAX_ATMOSPHERIC_EFFECTSHADERS; i++)
		{
			cg_atmFx.effectshaders[i] = trap_R_RegisterShader( va("gfx/misc/raindrop%d", i) );
			if (cg_atmFx.effectshaders[i]) {
				cg_atmFx.numEffectShaders++;
			} else {
				break;
			}
		}
	} else if ( atmFXType == ATM_SNOW ) {
		// Snow
		cg_atmFx.effectshaders[0] = trap_R_RegisterShader( "gfx/misc/snow" );
		cg_atmFx.numEffectShaders = 1;

		for (i = 1; i < MAX_ATMOSPHERIC_EFFECTSHADERS; i++)
		{
			cg_atmFx.effectshaders[i] = trap_R_RegisterShader( va("gfx/misc/snow%d", i) );
			if (cg_atmFx.effectshaders[i]) {
				cg_atmFx.numEffectShaders++;
			} else {
				break;
			}
		}
	} else {
		// This really should never happen
		cg_atmFx.numEffectShaders = 0;
	}

	if ( !( cg_atmFx.effectshaders[0] ) ) {
		cg_atmFx.effectshaders[0] = -1;
		cg_atmFx.numEffectShaders = 0;
	}

	// Initialise atmospheric effect to prevent all particles falling at the start
	for ( count = 0; count < cg_atmFx.numDrops; count++ )
		cg_atmFx.atmoParticles[count].nextDropTime = ATMOSPHERIC_DROPDELAY + ( rand() % ATMOSPHERIC_DROPDELAY );

	CG_EffectGust();
}

/*
** Main render loop
*/

void CG_AddAtmosphericEffects(void) {
	// Add atmospheric effects (e.g. rain, snow etc.) to view

	int max, currnum;
	vec3_t currvec;
	float currweight;
	int i;

	for ( i = 0 ; i < cg.numEffects; i++ ) {
		cg_effect_t *effect = &cgs.effects[i];

		if ( CG_CullEffectParticles( effect ) ) {
			continue;
		}

		CG_GenerateEffectParticles( effect );

		effect->visible = !CG_CullBounds( effect->mins, effect->maxs );
	}

	CG_UpdateParticles( );
	CG_DrawParticles( );

	if ( cg_atmFx.numDrops <= 0 || cg_atmFx.numEffectShaders == 0 || cg_atmosphericEffects.value <= 0 ) {
		return;
	}

	max = cg_atmosphericEffects.value < 1 ? cg_atmosphericEffects.value * cg_atmFx.numDrops : cg_atmFx.numDrops;
	if ( CG_EffectGustCurrent( currvec, &currweight, &currnum ) ) {
		CG_EffectGust();            // Recalculate gust parameters
	}
	// ydnar: allow parametric management of drop count for swelling/waning precip
	cg_atmFx.oldDropsActive = cg_atmFx.dropsActive;
	cg_atmFx.dropsActive = 0;

	cg_atmFx.dropsRendered = cg_atmFx.dropsCreated = cg_atmFx.dropsSkipped = 0;

//	getgroundtime = getskytime = rendertime = checkvisibletime = generatetime = 0;
//	n_getgroundtime = n_getskytime = n_rendertime = n_checkvisibletime = n_generatetime = 0;

	VectorSet( cg_atmFx.viewDir, cg.refdef.viewaxis[0][0], cg.refdef.viewaxis[0][1], 0.f );

	cg_atmFx.ParticleUpdate( max, currvec, currweight );
	
	cg_atmFx.lastRainTime = cg.time;

//	CG_Printf( "Active: %d Generated: %d Rendered: %d Skipped: %d\n", cg_atmFx.dropsActive, cg_atmFx.dropsCreated, cg_atmFx.dropsRendered, cg_atmFx.dropsSkipped );
//	CG_Printf( "gg: %i gs: %i rt: %i cv: %i ge: %i\n", getgroundtime, getskytime, rendertime, checkvisibletime, generatetime );
//	CG_Printf( "\\-> %i \\-> %i \\-> %i \\-> %i \\-> %i\n", n_getgroundtime, n_getskytime, n_rendertime, n_checkvisibletime, n_generatetime );
}
