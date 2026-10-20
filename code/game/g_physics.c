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
// g_physics.c -- rigid body physics props
//
// The simulation is a port of Doom 3's idPhysics_RigidBody: impulse based,
// one collision per time step through the clip world's Motion (translation
// plus rotation of a trace model), contact points for friction and resting.
//
// Matrices follow Doom 3's idMat3 conventions, which match Quake 3 axis
// arrays: the rows are the forward, left and up vectors, "v * M" and "M * v"
// both mean v[0]*M[0] + v[1]*M[1] + v[2]*M[2] (MatrixRotateVector), and
// "A * B" is the regular product (MatrixMultiply).

#include "g_local.h"

#define STOP_SPEED				10.0f
#define MAX_INERTIA_SCALE		10.0f
#define PLAYER_MASS				200.0f		// what Quake 3's knockback assumes
#define MIN_STEP_MSEC			8

static rigidBody_t	g_bodies[MAX_PHYSICS_BODIES];

static vmCvar_t		g_physicsStep;
static vmCvar_t		g_propDensity;
static vmCvar_t		g_propBounce;
static vmCvar_t		g_propFriction;
static vmCvar_t		g_propPushMass;
static vmCvar_t		g_propKnockback;
static vmCvar_t		g_debugPhysics;
static vmCvar_t		g_debugCollision;
vmCvar_t				g_itemPhysics;

// impact sounds
#define IMPACT_SOUND_SPEED			60.0f		// slower impacts are silent
#define IMPACT_SOUND_HEAVY_SPEED	250.0f
#define IMPACT_SOUND_MSEC			120			// at most one sound per prop this often
#define NUM_LIGHT_IMPACT_SOUNDS		4

static int			lightImpactSounds[NUM_LIGHT_IMPACT_SOUNDS];
static int			heavyImpactSound;

static vec3_t		gravityVector;
static const vec3_t	gravityNormal = { 0.0f, 0.0f, -1.0f };

// statistics for the debug view
static int			physicsActiveBodies;
static int			physicsSteps;
static int			physicsCollisions;
static int			physicsLastStatsTime;

/*
===============================================================================

	Matrix helpers with Doom 3 idMat3 semantics

===============================================================================
*/

static ID_INLINE void M3_MulVec( const vec3_t m[3], const vec3_t v, vec3_t out ) {
	MatrixRotateVector( v, m, out );
}

static ID_INLINE void M3_Mul( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	MatrixMultiply( (const float (*)[3])a, (const float (*)[3])b, (float (*)[3])out );
}

static ID_INLINE void M3_Scale( vec3_t m[3], float s ) {
	VectorScale( m[0], s, m[0] );
	VectorScale( m[1], s, m[1] );
	VectorScale( m[2], s, m[2] );
}

static void M3_SkewSymmetric( const vec3_t v, vec3_t out[3] ) {
	VectorSet( out[0], 0.0f, -v[2], v[1] );
	VectorSet( out[1], v[2], 0.0f, -v[0] );
	VectorSet( out[2], -v[1], v[0], 0.0f );
}

// orientation^T * inverseInertiaTensor * orientation
static void RB_InverseWorldInertiaTensor( const rigidBody_t *body, const vec3_t orientation[3], vec3_t out[3] ) {
	vec3_t transposed[3], tmp[3];

	TransposeAxis( orientation, transposed );
	M3_Mul( (const vec3_t *)transposed, (const vec3_t *)body->inverseInertiaTensor, tmp );
	M3_Mul( (const vec3_t *)tmp, orientation, out );
}

static void RB_WorldCenterOfMass( const rigidBody_t *body, vec3_t out ) {
	vec3_t tmp;

	M3_MulVec( (const vec3_t *)body->current.i.orientation, body->centerOfMass, tmp );
	VectorAdd( body->current.i.position, tmp, out );
}

static void RB_LinearVelocity( const rigidBody_t *body, vec3_t out ) {
	VectorScale( body->current.i.linearMomentum, body->inverseMass, out );
}

static void RB_AngularVelocity( const rigidBody_t *body, vec3_t out ) {
	vec3_t invWorldInertia[3];

	RB_InverseWorldInertiaTensor( body, (const vec3_t *)body->current.i.orientation, invWorldInertia );
	M3_MulVec( (const vec3_t *)invWorldInertia, body->current.i.angularMomentum, out );
}

// r.Cross( n ), times the matrix, cross r, dot n: the rotational part of an impulse denominator
static float RB_AngularImpulseTerm( const vec3_t invInertia[3], const vec3_t r, const vec3_t n ) {
	vec3_t rxn, t, txr;

	CrossProduct( r, n, rxn );
	M3_MulVec( invInertia, rxn, t );
	CrossProduct( t, r, txr );
	return DotProduct( txr, n );
}

/*
===============================================================================

	Rigid body simulation

===============================================================================
*/

static void RB_Activate( rigidBody_t *body ) {
	if ( body->isStatic ) {
		return;
	}
	body->current.atRest = -1;
}

static void RB_Rest( rigidBody_t *body ) {
	body->current.atRest = level.time;
	VectorClear( body->current.i.linearMomentum );
	VectorClear( body->current.i.angularMomentum );
}

static qboolean RB_IsAtRest( const rigidBody_t *body ) {
	return body->current.atRest >= 0;
}

/*
================
RB_Derivatives

state->orientation is transposed while integrating, like Doom 3 does
================
*/
static void RB_Derivatives( const rigidBody_t *body, const rigidBodyIState_t *s, rigidBodyIState_t *d ) {
	vec3_t transposed[3], tmp[3], invWorldInertia[3], skew[3];
	vec3_t angularVelocity;

	// orientation * inverseInertiaTensor * orientation^T
	TransposeAxis( (const vec3_t *)s->orientation, transposed );
	M3_Mul( (const vec3_t *)s->orientation, (const vec3_t *)body->inverseInertiaTensor, tmp );
	M3_Mul( (const vec3_t *)tmp, (const vec3_t *)transposed, invWorldInertia );
	M3_MulVec( (const vec3_t *)invWorldInertia, s->angularMomentum, angularVelocity );

	// derivatives, the momentum entries hold force and torque
	VectorScale( s->linearMomentum, body->inverseMass, d->position );
	M3_SkewSymmetric( angularVelocity, skew );
	M3_Mul( (const vec3_t *)skew, (const vec3_t *)s->orientation, d->orientation );
	VectorScale( s->linearMomentum, -body->linearFriction, d->linearMomentum );
	VectorAdd( d->linearMomentum, body->current.externalForce, d->linearMomentum );
	VectorScale( s->angularMomentum, -body->angularFriction, d->angularMomentum );
	VectorAdd( d->angularMomentum, body->current.externalTorque, d->angularMomentum );
}

/*
================
RB_Integrate

Calculate next state from the current state using the euler integrator.
================
*/
static void RB_Integrate( rigidBody_t *body, float deltaTime, rigidBodyPState_t *next ) {
	rigidBodyIState_t cur, deriv;
	vec3_t tmp, transposed[3];
	int i;

	// integrate the center of mass with the transposed orientation
	cur = body->current.i;
	M3_MulVec( (const vec3_t *)cur.orientation, body->centerOfMass, tmp );
	VectorAdd( cur.position, tmp, cur.position );
	TransposeAxis( (const vec3_t *)body->current.i.orientation, cur.orientation );

	RB_Derivatives( body, &cur, &deriv );

	VectorMA( cur.position, deltaTime, deriv.position, next->i.position );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( cur.orientation[i], deltaTime, deriv.orientation[i], next->i.orientation[i] );
	}
	VectorMA( cur.linearMomentum, deltaTime, deriv.linearMomentum, next->i.linearMomentum );
	VectorMA( cur.angularMomentum, deltaTime, deriv.angularMomentum, next->i.angularMomentum );

	Mat3OrthoNormalizeSelf( next->i.orientation );

	// apply gravity
	VectorMA( next->i.linearMomentum, deltaTime * body->mass, gravityVector, next->i.linearMomentum );

	TransposeAxis( (const vec3_t *)next->i.orientation, transposed );
	AxisCopy( (const vec3_t *)transposed, next->i.orientation );

	// back from the center of mass to the trace model origin
	M3_MulVec( (const vec3_t *)next->i.orientation, body->centerOfMass, tmp );
	VectorSubtract( next->i.position, tmp, next->i.position );
	next->atRest = body->current.atRest;
}

/*
================
G_PhysicsImpactInfo

What a rigid body hitting ent needs to know about it.
================
*/
static void G_PhysicsImpactInfo( gentity_t *ent, const vec3_t point, impactInfo_t *info ) {
	memset( info, 0, sizeof( *info ) );

	if ( !ent ) {
		return;
	}

	if ( ent->body && ent->body->isStatic ) {
		// immovable, like the world
	} else if ( ent->body ) {
		rigidBody_t *body = ent->body;
		vec3_t linearVelocity, angularVelocity, massCenter;

		RB_LinearVelocity( body, linearVelocity );
		RB_InverseWorldInertiaTensor( body, (const vec3_t *)body->current.i.orientation, info->invInertiaTensor );
		M3_MulVec( (const vec3_t *)info->invInertiaTensor, body->current.i.angularMomentum, angularVelocity );
		info->invMass = body->inverseMass;
		RB_WorldCenterOfMass( body, massCenter );
		VectorSubtract( point, massCenter, info->position );
		CrossProduct( angularVelocity, info->position, info->velocity );
		VectorAdd( info->velocity, linearVelocity, info->velocity );
	} else if ( ent->player ) {
		// players don't give way to props, but their velocity counts
		VectorCopy( ent->player->ps.velocity, info->velocity );
	} else if ( ent->s.eType == ET_MOVER ) {
		BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, info->velocity );
	}
}

/*
================
G_PhysicsEntityImpulse

Applies the reaction of a collision to the entity a rigid body ran into.
================
*/
static void G_PhysicsEntityImpulse( gentity_t *ent, const vec3_t point, const vec3_t impulse ) {
	vec3_t kick;

	if ( ent->body ) {
		G_PhysicsApplyImpulse( ent, point, impulse );
	} else if ( ent->player ) {
		// only real impacts kick players, resting contact would just cause prediction errors
		if ( VectorLength( impulse ) < 150.0f * PLAYER_MASS ) {
			return;
		}
		// same mass convention as weapon knockback
		VectorScale( impulse, 1.0f / PLAYER_MASS, kick );
		kick[2] = Com_Clamp( -300.0f, 300.0f, kick[2] );
		VectorAdd( ent->player->ps.velocity, kick, ent->player->ps.velocity );
	}
}

/*
================
RB_CollisionImpulse

Calculates the collision impulse using the velocity relative to the collision object.
The current state should be set to the moment of impact.
================
*/
static void RB_CollisionImpulse( rigidBody_t *body, const cm_trace_t *collision, vec3_t impulse ) {
	vec3_t r, linearVelocity, angularVelocity, velocity, massCenter;
	vec3_t invWorldInertia[3];
	float impulseNumerator, impulseDenominator, vel;
	impactInfo_t info;
	gentity_t *ent;

	// get info from other entity involved
	ent = ( collision->c.entityNum >= 0 && collision->c.entityNum < ENTITYNUM_MAX_NORMAL ) ? &g_entities[collision->c.entityNum] : NULL;
	G_PhysicsImpactInfo( ent, collision->c.point, &info );

	// collision point relative to the body center of mass
	RB_WorldCenterOfMass( body, massCenter );
	VectorSubtract( collision->c.point, massCenter, r );

	// the velocity at the collision point
	RB_LinearVelocity( body, linearVelocity );
	RB_InverseWorldInertiaTensor( body, (const vec3_t *)body->current.i.orientation, invWorldInertia );
	M3_MulVec( (const vec3_t *)invWorldInertia, body->current.i.angularMomentum, angularVelocity );
	CrossProduct( angularVelocity, r, velocity );
	VectorAdd( velocity, linearVelocity, velocity );

	// subtract velocity of other entity
	VectorSubtract( velocity, info.velocity, velocity );

	// velocity in normal direction
	vel = DotProduct( velocity, collision->c.normal );

	if ( vel > -STOP_SPEED ) {
		impulseNumerator = STOP_SPEED;
	} else {
		impulseNumerator = -( 1.0f + body->bouncyness ) * vel;
	}
	impulseDenominator = body->inverseMass + RB_AngularImpulseTerm( (const vec3_t *)invWorldInertia, r, collision->c.normal );
	if ( info.invMass ) {
		impulseDenominator += info.invMass + RB_AngularImpulseTerm( (const vec3_t *)info.invInertiaTensor, info.position, collision->c.normal );
	}
	VectorScale( collision->c.normal, impulseNumerator / impulseDenominator, impulse );

	// update linear and angular momentum with impulse
	VectorAdd( body->current.i.linearMomentum, impulse, body->current.i.linearMomentum );
	CrossProduct( r, impulse, velocity );
	VectorAdd( body->current.i.angularMomentum, velocity, body->current.i.angularMomentum );

	// if no movement at all don't blow up
	if ( collision->fraction < 0.0001f ) {
		VectorScale( body->current.i.linearMomentum, 0.5f, body->current.i.linearMomentum );
		VectorScale( body->current.i.angularMomentum, 0.5f, body->current.i.angularMomentum );
	}

	// remember for the debug view
	VectorCopy( collision->c.point, body->lastImpactPoint );
	VectorCopy( collision->c.normal, body->lastImpactNormal );
	body->lastImpactSpeed = -vel;
	body->lastImpactTime = level.time;
	physicsCollisions++;
}

/*
================
RB_CheckForCollisions

Check for collisions between the current and next state.
If there is a collision the next state is set to the state at the moment of impact.
================
*/
static qboolean RB_CheckForCollisions( rigidBody_t *body, rigidBodyPState_t *next, cm_trace_t *collision ) {
	vec3_t transposed[3], axis[3];
	rotation_t rotation;

	// rotation from the current to the next orientation
	TransposeAxis( (const vec3_t *)body->current.i.orientation, transposed );
	M3_Mul( (const vec3_t *)transposed, (const vec3_t *)next->i.orientation, axis );
	Mat3ToRotation( (const vec3_t *)axis, &rotation );
	VectorCopy( body->current.i.position, rotation.origin );

	// if there was a collision
	if ( ClipMotion( &g_clip, collision, body->current.i.position, next->i.position, &rotation,
					&body->self->clipModel, (const vec3_t *)body->current.i.orientation, body->clipMask, (sharedEntity_t *)body->self ) ) {
		// set the next state to the state at the moment of impact
		VectorCopy( collision->endpos, next->i.position );
		AxisCopy( (const vec3_t *)collision->endAxis, next->i.orientation );
		VectorCopy( body->current.i.linearMomentum, next->i.linearMomentum );
		VectorCopy( body->current.i.angularMomentum, next->i.angularMomentum );
		return qtrue;
	}
	return qfalse;
}

/*
================
RB_ContactFriction

Does not solve friction for multiple simultaneous contacts but applies contact friction in isolation.
Uses absolute velocity at the contact points instead of the velocity relative to the contact object.
================
*/
static void RB_ContactFriction( rigidBody_t *body, float deltaTime ) {
	int i;
	float magnitude, impulseNumerator, impulseDenominator;
	vec3_t invWorldInertia[3];
	vec3_t linearVelocity, angularVelocity;
	vec3_t massCenter, r, velocity, normal, impulse, normalVelocity, torque;

	RB_InverseWorldInertiaTensor( body, (const vec3_t *)body->current.i.orientation, invWorldInertia );
	RB_WorldCenterOfMass( body, massCenter );

	for ( i = 0; i < body->numContacts; i++ ) {
		const contactInfo_t *contact = &body->contacts[i];

		VectorSubtract( contact->point, massCenter, r );

		// calculate velocity at contact point
		RB_LinearVelocity( body, linearVelocity );
		M3_MulVec( (const vec3_t *)invWorldInertia, body->current.i.angularMomentum, angularVelocity );
		CrossProduct( angularVelocity, r, velocity );
		VectorAdd( velocity, linearVelocity, velocity );

		// velocity along normal vector
		VectorScale( contact->normal, DotProduct( velocity, contact->normal ), normalVelocity );

		// calculate friction impulse
		VectorSubtract( normalVelocity, velocity, normal );
		magnitude = VectorNormalize( normal );
		impulseNumerator = body->contactFriction * magnitude;
		impulseDenominator = body->inverseMass + RB_AngularImpulseTerm( (const vec3_t *)invWorldInertia, r, normal );
		VectorScale( normal, impulseNumerator / impulseDenominator, impulse );

		// apply friction impulse
		VectorAdd( body->current.i.linearMomentum, impulse, body->current.i.linearMomentum );
		CrossProduct( r, impulse, torque );
		VectorAdd( body->current.i.angularMomentum, torque, body->current.i.angularMomentum );

		// if moving towards the surface at the contact point
		if ( DotProduct( normalVelocity, contact->normal ) < 0.0f ) {
			// calculate impulse
			VectorNegate( normalVelocity, normal );
			impulseNumerator = VectorNormalize( normal );
			impulseDenominator = body->inverseMass + RB_AngularImpulseTerm( (const vec3_t *)invWorldInertia, r, normal );
			VectorScale( normal, impulseNumerator / impulseDenominator, impulse );

			// apply impulse
			VectorAdd( body->current.i.linearMomentum, impulse, body->current.i.linearMomentum );
			CrossProduct( r, impulse, torque );
			VectorAdd( body->current.i.angularMomentum, torque, body->current.i.angularMomentum );
		}
	}
}

/*
================
RB_CornersInSolid

How many corners of the body's trace model are inside something solid at
the pose: exact point tests, without the contact epsilon of a contents
test of the whole trace model.
================
*/
static int RB_CornersInSolid( const rigidBody_t *body, const vec3_t position, const vec3_t orientation[3] ) {
	const traceModel_t *trm = ClipModelGetTraceModel( &body->self->clipModel );
	vec3_t point;
	int i, num = 0;

	for ( i = 0; i < trm->numVerts; i++ ) {
		MatrixRotateVector( trm->verts[i], orientation, point );
		VectorAdd( point, position, point );
		if ( G_PointContents( point, body->self->s.number ) & body->clipMask & ~CONTENTS_BODY ) {
			num++;
		}
	}
	return num;
}

/*
================
RB_KeepOutOfSolid

Doom 3's rotational collision misses a body that touches a surface, inside
the contact epsilon, turning a little further into it, and once a corner is
in, the surface no longer stops it: a statue rocking against a column goes
through it. When a step puts a corner into something solid that the body
wasn't in, keep the move without the turn, or failing that stay put, and
take the momentum the step would have spent going in.
================
*/
static void RB_KeepOutOfSolid( rigidBody_t *body, rigidBodyPState_t *next ) {
	if ( !RB_CornersInSolid( body, next->i.position, (const vec3_t *)next->i.orientation ) ) {
		return;
	}
	if ( RB_CornersInSolid( body, body->current.i.position, (const vec3_t *)body->current.i.orientation ) ) {
		return;		// already in, let it move out
	}

	VectorClear( next->i.angularMomentum );
	if ( !RB_CornersInSolid( body, next->i.position, (const vec3_t *)body->current.i.orientation ) ) {
		AxisCopy( (const vec3_t *)body->current.i.orientation, next->i.orientation );
		return;
	}
	VectorCopy( body->current.i.position, next->i.position );
	AxisCopy( (const vec3_t *)body->current.i.orientation, next->i.orientation );
	VectorClear( next->i.linearMomentum );
}

/*
================
RB_EvaluateContacts
================
*/
static qboolean RB_EvaluateContacts( rigidBody_t *body ) {
	vec6_t dir;
	vec3_t tmp;

	VectorMA( body->current.i.linearMomentum, body->current.lastTimeStep * body->mass, gravityVector, tmp );
	VectorNormalize( tmp );
	VectorCopy( tmp, dir );
	VectorCopy( body->current.i.angularMomentum, tmp );
	VectorNormalize( tmp );
	VectorCopy( tmp, dir + 3 );

	body->numContacts = ClipContacts( &g_clip, body->contacts, MAX_BODY_CONTACTS, body->current.i.position,
									dir, CONTACT_EPSILON, &body->self->clipModel, (const vec3_t *)body->current.i.orientation,
									body->clipMask, (sharedEntity_t *)body->self );

	return ( body->numContacts != 0 );
}

/*
================
RB_ConvexHull2D

Monotone chain hull of points projected on the plane orthogonal to gravity,
stands in for idFixedWinding::AddToConvexHull.
================
*/
static int RB_ConvexHull2D( float pts[][2], int n, float hull[][2] ) {
	int i, j, k;
	float tmp[2];

	// sort on x then y
	for ( i = 1; i < n; i++ ) {
		tmp[0] = pts[i][0];
		tmp[1] = pts[i][1];
		for ( j = i - 1; j >= 0 && ( pts[j][0] > tmp[0] || ( pts[j][0] == tmp[0] && pts[j][1] > tmp[1] ) ); j-- ) {
			pts[j+1][0] = pts[j][0];
			pts[j+1][1] = pts[j][1];
		}
		pts[j+1][0] = tmp[0];
		pts[j+1][1] = tmp[1];
	}

#define CROSS2D( o, a, b ) ( ( (a)[0] - (o)[0] ) * ( (b)[1] - (o)[1] ) - ( (a)[1] - (o)[1] ) * ( (b)[0] - (o)[0] ) )
	k = 0;
	for ( i = 0; i < n; i++ ) {
		while ( k >= 2 && CROSS2D( hull[k-2], hull[k-1], pts[i] ) <= 0.01f ) {
			k--;
		}
		hull[k][0] = pts[i][0];
		hull[k][1] = pts[i][1];
		k++;
	}
	for ( i = n - 2, j = k + 1; i >= 0; i-- ) {
		while ( k >= j && CROSS2D( hull[k-2], hull[k-1], pts[i] ) <= 0.01f ) {
			k--;
		}
		hull[k][0] = pts[i][0];
		hull[k][1] = pts[i][1];
		k++;
	}
	return k > 1 ? k - 1 : k;
}

static qboolean RB_PointInsideHull2D( float hull[][2], int n, const float p[2] ) {
	int i;

	for ( i = 0; i < n; i++ ) {
		if ( CROSS2D( hull[i], hull[(i+1)%n], p ) < 0.0f ) {
			return qfalse;
		}
	}
	return qtrue;
}
#undef CROSS2D

/*
================
RB_TestIfAtRest

Returns true if the body is considered at rest.
Does not catch all cases where the body is at rest but is generally good enough.
================
*/
static qboolean RB_TestIfAtRest( rigidBody_t *body ) {
	int i, numHull;
	float gv;
	vec3_t v, av, normal, point;
	float pts[MAX_BODY_CONTACTS][2], hull[MAX_BODY_CONTACTS * 2][2];

	if ( body->current.atRest >= 0 ) {
		return qtrue;
	}

	// need at least 3 contact points to come to rest
	if ( body->numContacts < 3 ) {
		return qfalse;
	}

	// get average contact plane normal
	VectorClear( normal );
	for ( i = 0; i < body->numContacts; i++ ) {
		VectorAdd( normal, body->contacts[i].normal, normal );
	}
	VectorNormalize( normal );

	// if on a too steep surface
	if ( DotProduct( normal, gravityNormal ) > -0.7f ) {
		return qfalse;
	}

	// create bounds for contact points, gravity is straight down so project on xy
	for ( i = 0; i < body->numContacts; i++ ) {
		pts[i][0] = body->contacts[i].point[0];
		pts[i][1] = body->contacts[i].point[1];
	}
	numHull = RB_ConvexHull2D( pts, body->numContacts, hull );

	// need at least 3 contact points to come to rest
	if ( numHull < 3 ) {
		return qfalse;
	}

	// if the center of mass is not inside the contact points
	RB_WorldCenterOfMass( body, point );
	if ( !RB_PointInsideHull2D( hull, numHull, point ) ) {
		return qfalse;
	}

	// linear velocity of body
	RB_LinearVelocity( body, v );
	// linear velocity in gravity direction
	gv = DotProduct( v, gravityNormal );
	// linear velocity orthogonal to gravity direction
	VectorMA( v, -gv, gravityNormal, v );

	// if too much velocity orthogonal to gravity direction
	if ( VectorLength( v ) > STOP_SPEED ) {
		return qfalse;
	}
	// if too much velocity in gravity direction
	if ( gv > 2.0f * STOP_SPEED || gv < -2.0f * STOP_SPEED ) {
		return qfalse;
	}

	// calculate rotational velocity
	RB_AngularVelocity( body, av );

	// if too much rotational velocity
	if ( VectorLengthSquared( av ) > STOP_SPEED ) {
		return qfalse;
	}

	return qtrue;
}

/*
================
RB_IsOutsideWorld
================
*/
static qboolean RB_IsOutsideWorld( const rigidBody_t *body ) {
	const clipModel_t *cm = &body->self->clipModel;
	int i;

	for ( i = 0; i < 3; i++ ) {
		if ( cm->absBounds[0][i] > g_clip.worldBounds[1][i] + 128.0f ||
			 cm->absBounds[1][i] < g_clip.worldBounds[0][i] - 128.0f ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
================
RB_ActivateContactEntities

Wakes up the bodies that rest on this one, like Doom 3's contact entities.
================
*/
static void RB_ActivateContactEntities( rigidBody_t *body ) {
	int i, j, num;

	num = body->self->s.number;
	for ( i = 0; i < MAX_PHYSICS_BODIES; i++ ) {
		rigidBody_t *other = &g_bodies[i];

		if ( !other->inUse || other == body || !RB_IsAtRest( other ) ) {
			continue;
		}
		for ( j = 0; j < other->numContacts; j++ ) {
			if ( other->contacts[j].entityNum == num ) {
				RB_Activate( other );
				break;
			}
		}
	}
}

/*
================
RB_Evaluate

Evaluate the impulse based rigid body physics.
When a collision occurs an impulse is applied at the moment of impact but
the remaining time after the collision is ignored.
================
*/
static qboolean RB_Evaluate( rigidBody_t *body, int timeStepMSec ) {
	rigidBodyPState_t next;
	cm_trace_t collision;
	vec3_t impulse;
	gentity_t *ent;
	float timeStep;
	qboolean collided, cameToRest = qfalse;

	timeStep = timeStepMSec * 0.001f;
	body->current.lastTimeStep = timeStep;

	// if the body is at rest
	if ( body->current.atRest >= 0 || timeStep <= 0.0f ) {
		return qfalse;
	}

	physicsSteps++;

	next = body->current;

	// calculate next position and orientation
	RB_Integrate( body, timeStep, &next );

	// check for collisions from the current to the next state
	memset( &collision, 0, sizeof( collision ) );
	collided = RB_CheckForCollisions( body, &next, &collision );
	RB_KeepOutOfSolid( body, &next );

	// set the new state
	body->current = next;

	if ( collided ) {
		// apply collision impulse
		RB_CollisionImpulse( body, &collision, impulse );

		// clank or thud, depending on how hard it hit
		if ( body->lastImpactSpeed > IMPACT_SOUND_SPEED && level.time - body->lastSoundTime >= IMPACT_SOUND_MSEC ) {
			if ( body->lastImpactSpeed > IMPACT_SOUND_HEAVY_SPEED ) {
				G_AddEvent( body->self, EV_GENERAL_SOUND, heavyImpactSound );
			} else {
				G_AddEvent( body->self, EV_GENERAL_SOUND, lightImpactSounds[rand() % NUM_LIGHT_IMPACT_SOUNDS] );
			}
			body->lastSoundTime = level.time;
		}
	}

	// update the position of the clip model
	G_PhysicsLinkProp( body->self );

	if ( !body->noContact ) {
		// get contacts
		RB_EvaluateContacts( body );

		// check if the body has come to rest
		if ( RB_TestIfAtRest( body ) ) {
			// put to rest
			RB_Rest( body );
			cameToRest = qtrue;
		} else {
			// apply contact friction
			RB_ContactFriction( body, timeStep );
		}
	}

	if ( body->current.atRest < 0 ) {
		RB_ActivateContactEntities( body );
	}

	if ( collided && collision.c.entityNum >= 0 && collision.c.entityNum < ENTITYNUM_MAX_NORMAL ) {
		// if the rigid body didn't come to rest or the other entity is not at rest
		ent = &g_entities[collision.c.entityNum];
		if ( ent->inuse && ( !cameToRest || !ent->body || !RB_IsAtRest( ent->body ) ) ) {
			// apply impact to other entity
			VectorNegate( impulse, impulse );
			G_PhysicsEntityImpulse( ent, collision.c.point, impulse );
		}
	}

	VectorClear( body->current.externalForce );
	VectorClear( body->current.externalTorque );

	if ( g_debugPhysics.integer & 64 ) {
		vec3_t lv, av;
		RB_LinearVelocity( body, lv ); RB_AngularVelocity( body, av );
		G_Printf( "PT %d t=%d z=%.3f v=%s |w|=%.3f col=%d frac=%.4f n=%s pt=(%.2f %.2f %.2f) type=%d ent=%d contacts=%d rest=%d\n", body->self->s.number, level.time,
			body->current.i.position[2], vtos( lv ), VectorLength( av ), collided, collision.fraction, vtos( collision.c.normal ),
			collision.c.point[0], collision.c.point[1], collision.c.point[2], collision.c.type,
			collision.c.entityNum, body->numContacts, body->current.atRest );
	}


	return qtrue;
}

/*
================
RB_SetClipModel

Sets up the trace model and derives mass, center of mass and inertia from it.
================
*/
static void RB_SetClipModel( rigidBody_t *body, const traceModel_t *trm, float density ) {
	vec3_t inertiaScale[3];
	int minIndex;
	float min;

	ClipModelInit( &body->self->clipModel );
	ClipModelLoadTraceModel( &body->self->clipModel, trm );

	// get mass properties from the trace model
	ClipModelGetMassProperties( &body->self->clipModel, density, &body->mass, body->centerOfMass, body->inertiaTensor );

	// check whether or not the clip model has valid mass properties
	if ( body->mass <= 0.0f || IS_NAN( body->mass ) ) {
		G_Printf( "physics prop %d: invalid mass\n", body->self->s.number );
		body->mass = 1.0f;
		VectorClear( body->centerOfMass );
		AxisClear( body->inertiaTensor );
	}

	// check whether or not the inertia tensor is balanced
	minIndex = 0;
	if ( body->inertiaTensor[1][1] < body->inertiaTensor[minIndex][minIndex] ) {
		minIndex = 1;
	}
	if ( body->inertiaTensor[2][2] < body->inertiaTensor[minIndex][minIndex] ) {
		minIndex = 2;
	}
	AxisClear( inertiaScale );
	inertiaScale[0][0] = body->inertiaTensor[0][0] / body->inertiaTensor[minIndex][minIndex];
	inertiaScale[1][1] = body->inertiaTensor[1][1] / body->inertiaTensor[minIndex][minIndex];
	inertiaScale[2][2] = body->inertiaTensor[2][2] / body->inertiaTensor[minIndex][minIndex];

	if ( inertiaScale[0][0] > MAX_INERTIA_SCALE || inertiaScale[1][1] > MAX_INERTIA_SCALE || inertiaScale[2][2] > MAX_INERTIA_SCALE ) {
		vec3_t scaled[3];

		min = body->inertiaTensor[minIndex][minIndex] * MAX_INERTIA_SCALE;
		inertiaScale[(minIndex+1)%3][(minIndex+1)%3] = min / body->inertiaTensor[(minIndex+1)%3][(minIndex+1)%3];
		inertiaScale[(minIndex+2)%3][(minIndex+2)%3] = min / body->inertiaTensor[(minIndex+2)%3][(minIndex+2)%3];
		M3_Mul( (const vec3_t *)body->inertiaTensor, (const vec3_t *)inertiaScale, scaled );
		AxisCopy( (const vec3_t *)scaled, body->inertiaTensor );
	}

	body->inverseMass = 1.0f / body->mass;
	if ( !Mat3Inverse( (const vec3_t *)body->inertiaTensor, body->inverseInertiaTensor ) ) {
		AxisClear( body->inverseInertiaTensor );
	}
	M3_Scale( body->inverseInertiaTensor, 1.0f / 6.0f );

	VectorClear( body->current.i.linearMomentum );
	VectorClear( body->current.i.angularMomentum );
}

/*
===============================================================================

	Prop entities

===============================================================================
*/

/*
================
G_InitPhysics
================
*/
void G_InitPhysics( void ) {
	int i;

	memset( g_bodies, 0, sizeof( g_bodies ) );

	for ( i = 0; i < NUM_LIGHT_IMPACT_SOUNDS; i++ ) {
		lightImpactSounds[i] = G_SoundIndex( va( "sound/player/footsteps/clank%d.wav", i + 1 ) );
	}
	heavyImpactSound = G_SoundIndex( "sound/player/land1.wav" );

	trap_Cvar_Register( &g_physicsStep, "g_physicsStep", "16", 0 );
	trap_Cvar_Register( &g_propDensity, "g_propDensity", "0.005", 0 );
	trap_Cvar_Register( &g_propBounce, "g_propBounce", "0.4", 0 );
	trap_Cvar_Register( &g_propFriction, "g_propFriction", "0.015", 0 );
	trap_Cvar_Register( &g_propPushMass, "g_propPushMass", "60", 0 );
	trap_Cvar_Register( &g_propKnockback, "g_propKnockback", "1", 0 );
	trap_Cvar_Register( &g_debugPhysics, "g_debugPhysics", "0", 0 );
	trap_Cvar_Register( &g_debugCollision, "g_debugCollision", "0", 0 );
	trap_Cvar_Register( &g_itemPhysics, "g_itemPhysics", "1", CVAR_SERVERINFO | CVAR_LATCH );

	physicsLastStatsTime = 0;
}

static rigidBody_t *G_AllocBody( gentity_t *ent ) {
	int i;

	for ( i = 0; i < MAX_PHYSICS_BODIES; i++ ) {
		if ( !g_bodies[i].inUse ) {
			memset( &g_bodies[i], 0, sizeof( g_bodies[i] ) );
			g_bodies[i].inUse = qtrue;
			g_bodies[i].self = ent;
			ent->body = &g_bodies[i];
			return &g_bodies[i];
		}
	}
	return NULL;
}

/*
================
G_PhysicsFreeBody

Called when the entity is freed, wakes up whatever rested on it.
================
*/
void G_PhysicsFreeBody( gentity_t *ent ) {
	rigidBody_t *body = ent->body;
	int i;

	if ( ent->partClipModels ) {
		for ( i = 0; i < ent->numPartClipModels; i++ ) {
			ClipModelFree( &ent->partClipModels[i] );
		}
		trap_HeapFree( ent->partClipModels );
		ent->partClipModels = NULL;
		ent->numPartClipModels = 0;
	}
	if ( !body ) {
		return;
	}
	RB_ActivateContactEntities( body );
	body->inUse = qfalse;
	body->self = NULL;
	ent->body = NULL;
}

/*
================
G_PhysicsSetParts

Makes a prop collide as up to maxParts convex pieces of its model
(BG_PropModelTraceModels), for concave scenery such as trees. Only for
props that don't move: the body still simulates with its one hull.
================
*/
void G_PhysicsSetParts( gentity_t *ent, const char *model, float scale, int frame, int maxParts ) {
	traceModel_t trms[MAX_PROP_PARTS];
	int i, numParts;

	if ( maxParts <= 1 || !ent->body ) {
		return;
	}
	numParts = BG_PropModelTraceModels( model, scale, frame, maxParts, trms );
	if ( numParts <= 1 ) {
		return;
	}
	ent->partClipModels = trap_HeapMalloc( numParts * sizeof( clipModel_t ) );
	for ( i = 0; i < numParts; i++ ) {
		ClipModelInitFromTraceModel( &ent->partClipModels[i], &trms[i] );
	}
	ent->numPartClipModels = numParts;
	ent->s.origin2[1] = maxParts;	// the cgame makes the same pieces
	G_PhysicsLinkProp( ent );
}

/*
================
G_PhysicsLinkProp

Links the rotated trace model at the body's position. G_LinkEntity comes
here for props, it would build an unrotated box otherwise.
================
*/
void G_PhysicsLinkProp( gentity_t *ent ) {
	rigidBody_t *body = ent->body;
	clipModel_t *cm = &ent->clipModel;

	VectorCopy( body->current.i.position, ent->r.currentOrigin );
	Mat3ToAngles( (const vec3_t *)body->current.i.orientation, ent->r.currentAngles );

	if ( ent->numPartClipModels ) {
		// a compound prop collides as its pieces
		int i;

		ClipModelUnlink( cm );
		ClearBounds( ent->r.absmin, ent->r.absmax );
		for ( i = 0; i < ent->numPartClipModels; i++ ) {
			clipModel_t *part = &ent->partClipModels[i];

			part->contents = cm->contents;
			ClipModelLink2( part, &g_clip, (sharedEntity_t *)ent, ent->s.number, body->current.i.position,
							(const vec3_t *)body->current.i.orientation, -1 );
			AddPointToBounds( part->absBounds[0], ent->r.absmin, ent->r.absmax );
			AddPointToBounds( part->absBounds[1], ent->r.absmin, ent->r.absmax );
		}
	} else {
		ClipModelLink2( cm, &g_clip, (sharedEntity_t *)ent, ent->s.number, body->current.i.position,
						(const vec3_t *)body->current.i.orientation, -1 );

		// the clip model bounds include the rotation and the epsilon expansion
		VectorCopy( cm->absBounds[0], ent->r.absmin );
		VectorCopy( cm->absBounds[1], ent->r.absmax );
	}

	trap_LinkEntity( ent );
}

/*
================
G_PhysicsUpdateEntityState

Sends the simulated position and orientation to the clients.
================
*/
static void G_PhysicsUpdateEntityState( gentity_t *ent ) {
	rigidBody_t *body = ent->body;
	trType_t trType;
	int i;

	trType = RB_IsAtRest( body ) ? TR_STATIONARY : TR_INTERPOLATE;

	// the position is extrapolated so client prediction collides where the
	// prop is now instead of a snapshot behind, like movers
	if ( RB_IsAtRest( body ) ) {
		ent->s.pos.trType = TR_STATIONARY;
	} else if ( body->numContacts ) {
		ent->s.pos.trType = TR_LINEAR;
	} else {
		ent->s.pos.trType = TR_GRAVITY;
	}
	ent->s.pos.trTime = level.time;
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	RB_LinearVelocity( body, ent->s.pos.trDelta );

	ent->s.apos.trType = trType;
	ent->s.apos.trTime = level.time;
	VectorCopy( ent->r.currentAngles, ent->s.apos.trBase );
	VectorClear( ent->s.apos.trDelta );

	VectorCopy( ent->r.currentOrigin, ent->s.origin );
	VectorCopy( ent->r.currentAngles, ent->s.angles );

	// what the prop rests on, movers carry everything standing on them
	ent->s.groundEntityNum = ENTITYNUM_NONE;
	for ( i = 0; i < body->numContacts; i++ ) {
		if ( -DotProduct( body->contacts[i].normal, gravityNormal ) > 0.7f ) {
			ent->s.groundEntityNum = body->contacts[i].entityNum;
			break;
		}
	}
}

/*
================
G_PhysicsUpdateCvars
================
*/
static void G_PhysicsUpdateCvars( void ) {
	trap_Cvar_Update( &g_physicsStep );
	trap_Cvar_Update( &g_propDensity );
	trap_Cvar_Update( &g_propBounce );
	trap_Cvar_Update( &g_propFriction );
	trap_Cvar_Update( &g_propPushMass );
	trap_Cvar_Update( &g_propKnockback );
	trap_Cvar_Update( &g_debugPhysics );
	trap_Cvar_Update( &g_debugCollision );
}

/*
================
G_RunPhysicsProp
================
*/
void G_RunPhysicsProp( gentity_t *ent ) {
	rigidBody_t *body = ent->body;
	int msec, stepMsec, numSteps, i;

	VectorSet( gravityVector, 0.0f, 0.0f, -g_gravity.value );

	if ( !RB_IsAtRest( body ) ) {
		physicsActiveBodies++;

		// run fixed steps, a whole server frame is too long for stable stacking
		msec = level.time - level.previousTime;
		stepMsec = MAX( g_physicsStep.integer, MIN_STEP_MSEC );
		numSteps = ( msec + stepMsec - 1 ) / stepMsec;
		for ( i = 0; i < numSteps && !RB_IsAtRest( body ); i++ ) {
			RB_Evaluate( body, MIN( stepMsec, msec - i * stepMsec ) );
		}

		// fell out of the map: items placed in it go back, the rest goes
		if ( RB_IsOutsideWorld( body ) ) {
			if ( ent->s.eType == ET_ITEM && !( ent->flags & FL_DROPPED_ITEM ) ) {
				G_PhysicsSetPose( ent, ent->pos1, ent->pos2 );
			} else {
				G_FreeEntity( ent );
				return;
			}
		}
	}

	G_PhysicsUpdateEntityState( ent );

	G_RunThink( ent );
}

/*
================
G_PhysicsApplyImpulse
================
*/
void G_PhysicsApplyImpulse( gentity_t *ent, const vec3_t point, const vec3_t impulse ) {
	rigidBody_t *body = ent->body;
	vec3_t massCenter, r, torque;

	if ( !body || body->noImpact ) {
		return;
	}

	RB_WorldCenterOfMass( body, massCenter );
	VectorSubtract( point, massCenter, r );
	CrossProduct( r, impulse, torque );

	VectorAdd( body->current.i.linearMomentum, impulse, body->current.i.linearMomentum );
	VectorAdd( body->current.i.angularMomentum, torque, body->current.i.angularMomentum );
	RB_Activate( body );
}

/*
================
G_PhysicsDamage

Weapons push props around with the same knockback players get.
================
*/
void G_PhysicsDamage( gentity_t *targ, gentity_t *inflictor, const vec3_t dir, const vec3_t point, int knockback, int dflags ) {
	rigidBody_t *body = targ->body;
	vec3_t impulse, massCenter, hitPoint, toPoint;
	float radius;

	if ( !body || !dir || !knockback ) {
		return;
	}

	// a player with the same knockback would get g_knockback * knockback / PLAYER_MASS
	VectorScale( dir, g_knockback.value * knockback * g_propKnockback.value, impulse );

	RB_WorldCenterOfMass( body, massCenter );
	if ( point && !( dflags & DAMAGE_RADIUS ) ) {
		// direct hits spin the prop around the hit point
		VectorCopy( point, hitPoint );
	} else {
		// splash pushes from the side facing the explosion, with a little spin
		radius = RadiusFromBounds( targ->s.mins, targ->s.maxs ) * 0.3f;
		if ( point ) {
			VectorSubtract( point, massCenter, toPoint );
			VectorNormalize( toPoint );
		} else {
			VectorNegate( dir, toPoint );
		}
		VectorMA( massCenter, radius, toPoint, hitPoint );
	}

	G_PhysicsApplyImpulse( targ, hitPoint, impulse );
}

/*
================
G_PlayerWishVelocity

The horizontal velocity the player's movement keys ask for.
================
*/
static void G_PlayerWishVelocity( gentity_t *player, vec3_t out ) {
	const usercmd_t *cmd = &player->player->pers.cmd;
	vec3_t forward, right, angles;
	float scale;

	VectorClear( out );
	if ( !cmd->forwardmove && !cmd->rightmove ) {
		return;
	}
	VectorSet( angles, 0, player->player->ps.viewangles[YAW], 0 );
	AngleVectors( angles, forward, right, NULL );
	VectorScale( forward, cmd->forwardmove, out );
	VectorMA( out, cmd->rightmove, right, out );
	scale = VectorNormalize( out );
	scale = MIN( scale / 127.0f, 1.0f ) * player->player->ps.speed;
	VectorScale( out, scale, out );
}

/*
================
G_PhysicsPushedByPlayer

Players walking into props push them, from the prop's touch function.
================
*/
void G_PhysicsPushedByPlayer( gentity_t *prop, gentity_t *player ) {
	rigidBody_t *body = prop->body;
	vec3_t dir, massCenter, propVelocity, point, impulse, wishVelocity;
	float speed, z;

	if ( !body || !player->player ) {
		return;
	}

	// standing on the prop doesn't push it
	if ( player->player->ps.groundEntityNum == prop->s.number ) {
		return;
	}

	RB_WorldCenterOfMass( body, massCenter );
	VectorSubtract( massCenter, player->r.currentOrigin, dir );
	dir[2] = 0.0f;
	if ( VectorNormalize( dir ) < 1.0f ) {
		return;
	}

	// leaning on a prop clips the velocity to zero every frame, so also use
	// the velocity the player is trying to move with
	G_PlayerWishVelocity( player, wishVelocity );
	RB_LinearVelocity( body, propVelocity );
	speed = MAX( DotProduct( player->pmoveStartVelocity, dir ), DotProduct( wishVelocity, dir ) );
	speed -= DotProduct( propVelocity, dir );
	if ( speed <= 0.0f ) {
		return;
	}

	// push at waist height, inside the prop's height
	z = Com_Clamp( prop->r.absmin[2] + 1.0f, prop->r.absmax[2] - 1.0f, player->r.currentOrigin[2] );
	VectorMA( player->r.currentOrigin, 16.0f, dir, point );
	point[2] = z;

	VectorScale( dir, speed * g_propPushMass.value, impulse );
	G_PhysicsApplyImpulse( prop, point, impulse );
}

/*
================
G_PhysicsPushedByMover

Movers carry props along. move2 and matrix come from G_TryPushingEntity,
so props move and turn exactly like the other pushed entities.
Returns qfalse if the prop is blocked.
================
*/
qboolean G_PhysicsPushedByMover( gentity_t *prop, const vec3_t move, const vec3_t move2, const vec3_t matrix[3] ) {
	rigidBody_t *body = prop->body;
	vec3_t oldPosition, oldOrientation[3], row, velocity;
	int i, msec;

	if ( !body || body->isStatic ) {
		return qtrue;
	}

	VectorCopy( body->current.i.position, oldPosition );
	AxisCopy( (const vec3_t *)body->current.i.orientation, oldOrientation );

	VectorAdd( body->current.i.position, move, body->current.i.position );
	VectorAdd( body->current.i.position, move2, body->current.i.position );

	// turn the prop with the pusher, G_RotatePoint on every axis
	for ( i = 0; i < 3; i++ ) {
		VectorCopy( body->current.i.orientation[i], row );
		body->current.i.orientation[i][0] = DotProduct( matrix[0], row );
		body->current.i.orientation[i][1] = DotProduct( matrix[1], row );
		body->current.i.orientation[i][2] = DotProduct( matrix[2], row );
	}

	G_PhysicsLinkProp( prop );

	if ( ClipContents( &g_clip, body->current.i.position, &prop->clipModel, (const vec3_t *)body->current.i.orientation,
						body->clipMask & ~CONTENTS_BODY, (sharedEntity_t *)prop ) ) {
		// blocked, go back
		VectorCopy( oldPosition, body->current.i.position );
		AxisCopy( (const vec3_t *)oldOrientation, body->current.i.orientation );
		G_PhysicsLinkProp( prop );
		return qfalse;
	}

	// keep the mover's velocity so the prop doesn't stop dead when it leaves
	msec = level.time - level.previousTime;
	if ( msec > 0 ) {
		VectorSubtract( body->current.i.position, oldPosition, velocity );
		VectorScale( velocity, body->mass * 1000.0f / msec, body->current.i.linearMomentum );
	}
	RB_Activate( body );
	return qtrue;
}

/*
================
G_PhysicsTouch
================
*/
static void G_PhysicsTouch( gentity_t *self, gentity_t *other, trace_t *trace ) {
	if ( other->player ) {
		G_PhysicsPushedByPlayer( self, other );
	}
}

/*
================
G_SpawnPhysicsProp
================
*/
gentity_t *G_SpawnPhysicsProp( propShape_t shape, const vec3_t origin, const vec3_t angles,
								const vec3_t mins, const vec3_t maxs, float density ) {
	traceModel_t trm;

	BG_PropTraceModel( shape, mins, maxs, &trm );
	return G_SpawnPhysicsPropTrm( shape, &trm, origin, angles, density );
}

/*
================
G_SpawnPhysicsPropTrm

A prop of any trace model; its bounds are the entity's mins and maxs.
================
*/
/*
================
G_PhysicsAttachBody

Makes the entity a rigid body of the trace model, at the pose; its own type,
contents and touch stay. It collides with clipMask.
================
*/
qboolean G_PhysicsAttachBody( gentity_t *ent, propShape_t shape, const traceModel_t *trm, const vec3_t origin,
								const vec3_t angles, float density, int clipMask ) {
	rigidBody_t *body;

	if ( !G_AllocBody( ent ) ) {
		G_Printf( "G_PhysicsAttachBody: more than %d bodies\n", MAX_PHYSICS_BODIES );
		return qfalse;
	}
	body = ent->body;

	VectorCopy( trm->bounds[0], ent->s.mins );
	VectorCopy( trm->bounds[1], ent->s.maxs );
	ent->s.collisionType = CT_AABB;

	body->shape = shape;
	body->linearFriction = 0.6f;
	body->angularFriction = 0.6f;
	body->contactFriction = Com_Clamp( 0.0f, 1.0f, g_propFriction.value );
	body->bouncyness = Com_Clamp( 0.0f, 1.0f, g_propBounce.value );
	body->clipMask = clipMask;
	body->current.atRest = -1;
	body->current.lastTimeStep = 0.016f;

	VectorCopy( origin, body->current.i.position );
	AnglesToAxis( angles, body->current.i.orientation );

	RB_SetClipModel( body, trm, density > 0.0f ? density : g_propDensity.value );

	G_PhysicsLinkProp( ent );

	body->startedInSolid = ( ClipContents( &g_clip, origin, &ent->clipModel, (const vec3_t *)body->current.i.orientation,
											body->clipMask, (sharedEntity_t *)ent ) != 0 );
	G_PhysicsUpdateEntityState( ent );
	return qtrue;
}

/*
================
G_PhysicsDensityForMass

The density that gives the trace model the mass.
================
*/
float G_PhysicsDensityForMass( const traceModel_t *trm, float mass ) {
	float volume;
	vec3_t centerOfMass, inertia[3];

	TraceModelGetMassProperties( trm, 1.0f, &volume, centerOfMass, inertia );
	return ( volume > 0.0f && mass > 0.0f ) ? mass / volume : 0.0f;
}

/*
================
G_PhysicsSetPose

Puts the body at the pose, at rest.
================
*/
void G_PhysicsSetPose( gentity_t *ent, const vec3_t origin, const vec3_t angles ) {
	rigidBody_t *body = ent->body;

	if ( !body ) {
		return;
	}
	VectorCopy( origin, body->current.i.position );
	AnglesToAxis( angles, body->current.i.orientation );
	VectorClear( body->current.i.linearMomentum );
	VectorClear( body->current.i.angularMomentum );
	body->current.atRest = body->isStatic ? level.time : -1;
	G_PhysicsLinkProp( ent );
	G_PhysicsUpdateEntityState( ent );
}

/*
================
G_PhysicsSetVelocity

Sets the body moving, angular velocity in degrees a second about each axis.
================
*/
void G_PhysicsSetVelocity( gentity_t *ent, const vec3_t linear, const vec3_t angular ) {
	rigidBody_t *body = ent->body;
	vec3_t radians, inertia[3];

	if ( !body || body->isStatic ) {
		return;
	}
	VectorScale( linear, body->mass, body->current.i.linearMomentum );
	if ( angular ) {
		VectorScale( angular, M_PI / 180.0f, radians );
		// the inertia the simulation uses, from its inverse; in the body's
		// frame, which is fine for setting something spinning
		if ( Mat3Inverse( (const vec3_t *)body->inverseInertiaTensor, inertia ) ) {
			M3_MulVec( (const vec3_t *)inertia, radians, body->current.i.angularMomentum );
		}
	}
	RB_Activate( body );
	G_PhysicsUpdateEntityState( ent );
}

/*
================
G_SpawnPhysicsPropTrm

A prop of any trace model; its bounds are the entity's mins and maxs.
================
*/
gentity_t *G_SpawnPhysicsPropTrm( propShape_t shape, const traceModel_t *trm, const vec3_t origin,
									const vec3_t angles, float density ) {
	gentity_t *ent;

	ent = G_Spawn();
	ent->classname = "func_physics";
	ent->s.eType = ET_PHYSICS_PROP;
	ent->s.modelindex = shape;
	ent->s.contents = CONTENTS_SOLID;
	ent->r.svFlags &= ~SVF_NOCLIENT;
	ent->takedamage = qtrue;
	ent->touch = G_PhysicsTouch;

	if ( !G_PhysicsAttachBody( ent, shape, trm, origin, angles, density, MASK_SOLID | CONTENTS_BODY ) ) {
		G_FreeEntity( ent );
		return NULL;
	}
	return ent;
}

/*
================
G_PropDie

A prop with health breaks: fires its targets, plays its "breaksound" and
goes, waking up whatever rested on it.
================
*/
static void G_PropDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath ) {
	gentity_t *te;
	vec3_t center;

	self->takedamage = qfalse;
	G_UseTargets( self, attacker );

	if ( self->soundPos2 ) {
		VectorAdd( self->r.absmin, self->r.absmax, center );
		VectorScale( center, 0.5f, center );
		te = G_TempEntity( center, EV_GENERAL_SOUND );
		te->s.eventParm = self->soundPos2;
	}

	G_FreeEntity( self );
}

/*
================
G_PhysicsUnstick

Lifts a prop out of the floor it was placed in, a little at a time, or
makes it static if that doesn't free it.
================
*/
void G_PhysicsUnstick( gentity_t *prop ) {
	rigidBody_t *body = prop->body;
	int i;

	for ( i = 0; i < 64 && body->startedInSolid; i++ ) {
		body->current.i.position[2] += 1.0f;
		G_PhysicsLinkProp( prop );
		body->startedInSolid = ( ClipContents( &g_clip, body->current.i.position, &prop->clipModel,
				(const vec3_t *)body->current.i.orientation, body->clipMask, (sharedEntity_t *)prop ) != 0 );
	}
	if ( body->startedInSolid ) {
		// stuck sideways: leave it where it was, as scenery
		body->current.i.position[2] -= i;
		G_PhysicsLinkProp( prop );
		body->current.atRest = level.time;
		body->isStatic = qtrue;
		body->noImpact = qtrue;
		G_DPrintf( "prop %d at %s is in solid, made static\n", prop->s.number, vtos( body->current.i.position ) );
	}
}

/*QUAKED func_physics (0 .5 .8) ?
A rigid body that falls, tumbles and gets pushed around.
"shape"			box, cylinder, cone, octahedron or dodecahedron (default box)
"size"			edge length, or use "mins"/"maxs" (default 32)
"model"			an MD3 to draw instead, colliding as a convex hull of its mesh, or as
				the shape when it has none
"modelscale"	the model's scale (default 1)
"frame"			the model's frame, drawn and collided with (default 0)
"density"		mass per cubic unit (default g_propDensity)
"mass"			overrides the density
"bouncyness"	0 to 1 (default g_propBounce)
"friction"		contact friction 0 to 1 (default g_propFriction)
"angles"		starting orientation
"static"		1 for one that never moves, solid scenery
"hull_parts"	how many convex pieces a static one's model may collide as
				(default 8, 1 for one hull around all of it)
"health"		breaks when it has taken that much damage (default 0, unbreakable)
"breaksound"	played when it breaks
*/
void SP_func_physics( gentity_t *ent ) {
	char *shapeName, *model, *noise;
	float size, density, mass, value, scale, volume;
	vec3_t mins, maxs, origin, angles, centerOfMass, inertia[3];
	traceModel_t trm;
	propShape_t shape;
	int isStatic, health, frame, parts;
	gentity_t *prop;

	G_SpawnString( "model", "", &model );
	G_SpawnFloat( "modelscale", "1", &scale );
	G_SpawnInt( "frame", "0", &frame );
	if ( model[0] && BG_PropModelTraceModel( model, scale, frame, &trm ) ) {
		shape = PROP_MODEL;
	} else {
		// a model without a hull, one that isn't there, collides as the shape
		if ( model[0] ) {
			G_DPrintf( "func_physics at %s: no collision hull for %s, colliding as its shape\n", vtos( ent->s.origin ), model );
		}
		G_SpawnString( "shape", "box", &shapeName );
		G_SpawnFloat( "size", "32", &size );
		VectorSet( mins, -size * 0.5f, -size * 0.5f, -size * 0.5f );
		VectorSet( maxs, size * 0.5f, size * 0.5f, size * 0.5f );
		G_SpawnVector( "mins", va( "%f %f %f", mins[0], mins[1], mins[2] ), mins );
		G_SpawnVector( "maxs", va( "%f %f %f", maxs[0], maxs[1], maxs[2] ), maxs );
		shape = BG_PropShapeForName( shapeName );
		BG_PropTraceModel( shape, mins, maxs, &trm );
	}

	G_SpawnFloat( "density", "0", &density );
	if ( G_SpawnFloat( "mass", "0", &mass ) && mass > 0.0f ) {
		TraceModelGetMassProperties( &trm, 1.0f, &volume, centerOfMass, inertia );
		if ( volume > 0.0f ) {
			density = mass / volume;
		}
	}
	VectorCopy( ent->s.origin, origin );
	VectorCopy( ent->s.angles, angles );

	// the map entity only carries the spawn arguments
	prop = G_SpawnPhysicsPropTrm( shape, &trm, origin, angles, density );
	if ( !prop ) {
		G_FreeEntity( ent );
		return;
	}

	if ( model[0] ) {
		prop->s.modelindex2 = G_ModelIndex( model );
		prop->s.origin2[0] = scale;
		prop->s.frame = frame;
		G_PhysicsUnstick( prop );
		G_PhysicsUpdateEntityState( prop );
	}
	if ( G_SpawnFloat( "bouncyness", "0", &value ) ) {
		prop->body->bouncyness = Com_Clamp( 0.0f, 1.0f, value );
	}
	if ( G_SpawnFloat( "friction", "0", &value ) ) {
		prop->body->contactFriction = Com_Clamp( 0.0f, 1.0f, value );
	}
	G_SpawnInt( "static", "0", &isStatic );
	if ( isStatic ) {
		prop->body->current.atRest = level.time;
		prop->body->isStatic = qtrue;
		prop->body->noImpact = qtrue;
		G_PhysicsUpdateEntityState( prop );
	}
	// scenery that doesn't move collides as convex pieces of its model
	if ( prop->body->isStatic && model[0] ) {
		G_SpawnInt( "hull_parts", va( "%d", MAX_PROP_PARTS ), &parts );
		G_PhysicsSetParts( prop, model, scale, frame, parts );
	}
	G_SpawnInt( "health", "0", &health );
	if ( health > 0 ) {
		prop->health = health;
		prop->die = G_PropDie;
		G_SpawnString( "breaksound", "", &noise );
		if ( noise[0] ) {
			prop->soundPos2 = G_SoundIndex( noise );
		}
	}
	prop->target = ent->target;
	prop->targetname = ent->targetname;

	G_FreeEntity( ent );
}

/*
================
G_PhysicsClear
================
*/
void G_PhysicsClear( void ) {
	int i;

	for ( i = 0; i < MAX_PHYSICS_BODIES; i++ ) {
		if ( g_bodies[i].inUse && g_bodies[i].self ) {
			G_FreeEntity( g_bodies[i].self );
		}
	}
}

/*
===============================================================================

	Client commands

===============================================================================
*/

/*
================
G_PropSpawnPoint

A spot in front of the player's eyes, pulled back from walls.
================
*/
static void G_PropSpawnPoint( gentity_t *ent, float distance, float halfSize, vec3_t out, vec3_t forward ) {
	vec3_t eye, end, mins, maxs;
	trace_t tr;

	VectorCopy( ent->player->ps.origin, eye );
	eye[2] += ent->player->ps.viewheight;
	AngleVectors( ent->player->ps.viewangles, forward, NULL, NULL );
	VectorMA( eye, distance, forward, end );

	VectorSet( mins, -halfSize, -halfSize, -halfSize );
	VectorSet( maxs, halfSize, halfSize, halfSize );
	G_Trace( &tr, eye, mins, maxs, end, ent->s.number, MASK_SOLID, TT_AABB );
	VectorCopy( tr.endpos, out );
}

static qboolean G_PropCommandAllowed( gentity_t *ent ) {
	if ( !ent->player || ent->player->sess.sessionTeam == TEAM_SPECTATOR || ent->health <= 0 ) {
		trap_SendServerCommand( ent - g_entities, "print \"Props can only be spawned while playing.\n\"" );
		return qfalse;
	}
	return qtrue;
}

/*
================
Cmd_Prop_f

prop [shape] [size]
================
*/
void Cmd_Prop_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	propShape_t shape = PROP_BOX;
	float size = 32.0f;
	vec3_t origin, forward, angles, mins, maxs;

	if ( !G_PropCommandAllowed( ent ) ) {
		return;
	}

	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		shape = BG_PropShapeForName( arg );
	}
	if ( trap_Argc() > 2 ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		size = Com_Clamp( 8.0f, 128.0f, atof( arg ) );
	}

	VectorSet( mins, -size * 0.5f, -size * 0.5f, -size * 0.5f );
	VectorSet( maxs, size * 0.5f, size * 0.5f, size * 0.5f );
	if ( shape == PROP_CYLINDER || shape == PROP_CONE ) {
		// barrels and cones are taller than wide
		mins[2] *= 1.5f;
		maxs[2] *= 1.5f;
	}

	G_PropSpawnPoint( ent, 64.0f + size, size * 0.9f, origin, forward );
	// a slight tilt makes it land on an edge and tumble
	VectorSet( angles, crandom() * 20.0f, ent->player->ps.viewangles[YAW], crandom() * 20.0f );
	G_SpawnPhysicsProp( shape, origin, angles, mins, maxs, 0.0f );
}

/*
================
Cmd_PropThrow_f

propthrow [shape] [speed]
================
*/
void Cmd_PropThrow_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	propShape_t shape = PROP_BOX;
	float speed = 600.0f;
	vec3_t origin, forward, angles, mins, maxs, spin;
	gentity_t *prop;

	if ( !G_PropCommandAllowed( ent ) ) {
		return;
	}

	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		shape = BG_PropShapeForName( arg );
	}
	if ( trap_Argc() > 2 ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		speed = Com_Clamp( 0.0f, 3000.0f, atof( arg ) );
	}

	VectorSet( mins, -12, -12, -12 );
	VectorSet( maxs, 12, 12, 12 );
	G_PropSpawnPoint( ent, 48.0f, 14.0f, origin, forward );
	VectorCopy( ent->player->ps.viewangles, angles );

	prop = G_SpawnPhysicsProp( shape, origin, angles, mins, maxs, 0.0f );
	if ( prop ) {
		rigidBody_t *body = prop->body;

		VectorScale( forward, speed * body->mass, body->current.i.linearMomentum );
		VectorMA( body->current.i.linearMomentum, body->mass, ent->player->ps.velocity, body->current.i.linearMomentum );
		// tumble a bit
		VectorSet( spin, crandom() * 4.0f, crandom() * 4.0f, crandom() * 4.0f );
		M3_MulVec( (const vec3_t *)body->inertiaTensor, spin, body->current.i.angularMomentum );
	}
}

/*
================
Cmd_PropStack_f

propstack [count] [size]
================
*/
void Cmd_PropStack_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	int count = 5, i;
	float size = 32.0f;
	vec3_t origin, forward, angles, mins, maxs, base, end;
	trace_t tr;

	if ( !G_PropCommandAllowed( ent ) ) {
		return;
	}

	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		count = Com_Clamp( 1, 20, atoi( arg ) );
	}
	if ( trap_Argc() > 2 ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		size = Com_Clamp( 8.0f, 96.0f, atof( arg ) );
	}

	VectorSet( mins, -size * 0.5f, -size * 0.5f, -size * 0.5f );
	VectorSet( maxs, size * 0.5f, size * 0.5f, size * 0.5f );

	// on the floor in front of the player
	G_PropSpawnPoint( ent, 64.0f + size, size * 0.5f, base, forward );
	VectorCopy( base, end );
	end[2] -= 1024.0f;
	G_Trace( &tr, base, mins, maxs, end, ent->s.number, MASK_SOLID, TT_AABB );
	VectorCopy( tr.endpos, base );

	VectorSet( angles, 0, ent->player->ps.viewangles[YAW], 0 );
	for ( i = 0; i < count; i++ ) {
		VectorCopy( base, origin );
		origin[2] += i * ( size + 1.0f );
		G_SpawnPhysicsProp( PROP_BOX, origin, angles, mins, maxs, 0.0f );
	}
}

/*
================
Cmd_PropPyramid_f

proppyramid [rows] [size]
================
*/
void Cmd_PropPyramid_f( gentity_t *ent ) {
	char arg[MAX_TOKEN_CHARS];
	int rows = 4, row, i;
	float size = 32.0f;
	vec3_t origin, forward, right, angles, mins, maxs, base, end;
	trace_t tr;

	if ( !G_PropCommandAllowed( ent ) ) {
		return;
	}

	if ( trap_Argc() > 1 ) {
		trap_Argv( 1, arg, sizeof( arg ) );
		rows = Com_Clamp( 1, 8, atoi( arg ) );
	}
	if ( trap_Argc() > 2 ) {
		trap_Argv( 2, arg, sizeof( arg ) );
		size = Com_Clamp( 8.0f, 64.0f, atof( arg ) );
	}

	VectorSet( mins, -size * 0.5f, -size * 0.5f, -size * 0.5f );
	VectorSet( maxs, size * 0.5f, size * 0.5f, size * 0.5f );

	G_PropSpawnPoint( ent, 96.0f + size * rows * 0.5f, size * 0.5f, base, forward );
	VectorCopy( base, end );
	end[2] -= 1024.0f;
	G_Trace( &tr, base, mins, maxs, end, ent->s.number, MASK_SOLID, TT_AABB );
	VectorCopy( tr.endpos, base );

	VectorSet( angles, 0, ent->player->ps.viewangles[YAW], 0 );
	AngleVectors( angles, NULL, right, NULL );
	for ( row = 0; row < rows; row++ ) {
		for ( i = 0; i < rows - row; i++ ) {
			VectorMA( base, ( i - ( rows - row - 1 ) * 0.5f ) * ( size + 1.0f ), right, origin );
			origin[2] += row * ( size + 1.0f );
			G_SpawnPhysicsProp( PROP_BOX, origin, angles, mins, maxs, 0.0f );
		}
	}
}

/*
================
Cmd_PropClear_f
================
*/
void Cmd_PropClear_f( gentity_t *ent ) {
	G_PhysicsClear();
}

/*
================
Svcmd_PropSpawn_f

propspawn <shape> <x> <y> <z> [size] [yaw pitch roll]
================
*/
void Svcmd_PropSpawn_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	vec3_t origin, angles, mins, maxs;
	float size = 32.0f;
	propShape_t shape;
	gentity_t *prop;
	int i;

	if ( trap_Argc() < 5 ) {
		G_Printf( "usage: propspawn <shape> <x> <y> <z> [size] [pitch yaw roll]\n" );
		return;
	}
	trap_Argv( 1, arg, sizeof( arg ) );
	shape = BG_PropShapeForName( arg );
	for ( i = 0; i < 3; i++ ) {
		trap_Argv( 2 + i, arg, sizeof( arg ) );
		origin[i] = atof( arg );
	}
	if ( trap_Argc() > 5 ) {
		trap_Argv( 5, arg, sizeof( arg ) );
		size = Com_Clamp( 8.0f, 256.0f, atof( arg ) );
	}
	VectorClear( angles );
	for ( i = 0; i < 3 && trap_Argc() > 6 + i; i++ ) {
		trap_Argv( 6 + i, arg, sizeof( arg ) );
		angles[i] = atof( arg );
	}
	VectorSet( mins, -size * 0.5f, -size * 0.5f, -size * 0.5f );
	VectorSet( maxs, size * 0.5f, size * 0.5f, size * 0.5f );

	prop = G_SpawnPhysicsProp( shape, origin, angles, mins, maxs, 0.0f );
	if ( prop ) {
		G_Printf( "prop %d: %s mass %.1f%s\n", prop->s.number, bg_propShapeNames[shape], prop->body->mass,
					prop->body->startedInSolid ? " (in solid)" : "" );
		if ( g_debugPhysics.integer & 64 ) {
		G_Printf( "  center of mass %s inertia %s %s %s inverse %s %s %s\n", vtos( prop->body->centerOfMass ),
			vtos( prop->body->inertiaTensor[0] ), vtos( prop->body->inertiaTensor[1] ), vtos( prop->body->inertiaTensor[2] ),
			va( "(%g %g %g)", prop->body->inverseInertiaTensor[0][0], prop->body->inverseInertiaTensor[0][1], prop->body->inverseInertiaTensor[0][2] ),
			va( "(%g %g %g)", prop->body->inverseInertiaTensor[1][0], prop->body->inverseInertiaTensor[1][1], prop->body->inverseInertiaTensor[1][2] ),
			va( "(%g %g %g)", prop->body->inverseInertiaTensor[2][0], prop->body->inverseInertiaTensor[2][1], prop->body->inverseInertiaTensor[2][2] ) );
		}
	}
}

/*
================
Svcmd_PropBlast_f

propblast <x> <y> <z>, a rocket's splash damage for testing without a player
================
*/
void Svcmd_PropBlast_f( void ) {
	char arg[MAX_TOKEN_CHARS];
	vec3_t org;
	int i;

	if ( trap_Argc() < 4 ) {
		G_Printf( "usage: propblast <x> <y> <z>\n" );
		return;
	}
	for ( i = 0; i < 3; i++ ) {
		trap_Argv( 1 + i, arg, sizeof( arg ) );
		org[i] = atof( arg );
	}
	G_RadiusDamage( org, &g_entities[ENTITYNUM_WORLD], 100, 120, NULL, MOD_BREAKABLE_SPLASH );
}

/*
================
Svcmd_PropList_f
================
*/
void Svcmd_PropList_f( void ) {
	int i, num = 0;
	vec3_t v;

	for ( i = 0; i < MAX_PHYSICS_BODIES; i++ ) {
		rigidBody_t *body = &g_bodies[i];

		if ( !body->inUse ) {
			continue;
		}
		RB_LinearVelocity( body, v );
		G_Printf( "prop %d %s%s at %s angles %s speed %.1f contacts %d %s%s\n", body->self->s.number,
					body->shape < PROP_NUM_SHAPES ? bg_propShapeNames[body->shape] : "model",
					body->self->numPartClipModels ? va( " in %d pieces", body->self->numPartClipModels ) : "",
					vtos( body->current.i.position ), vtos( body->self->r.currentAngles ), VectorLength( v ),
					body->numContacts, RB_IsAtRest( body ) ? "resting" : "active",
					RB_CornersInSolid( body, body->current.i.position, (const vec3_t *)body->current.i.orientation ) ? ", in solid" : "" );
		num++;
	}
	G_Printf( "%d props\n", num );
}

/*
================
Svcmd_PropClear_f
================
*/
void Svcmd_PropClear_f( void ) {
	G_PhysicsClear();
}

/*
===============================================================================

	Debug view

	g_debugPhysics bits:
		1	prop hulls, green while simulated, grey at rest, red if spawned in solid
		2	contact points and normals
		4	linear (red) and angular (cyan) velocity
		8	impacts of the last second
		16	center of mass and orientation
		32	print statistics every second
		64	print every simulation step of the active props

	g_debugCollision bits:
		1	world collision polygons around the player
		2	every clip model around the player
		4	player and prop bounding boxes

	Only visible on a listen server with the OpenGL2 renderer.

===============================================================================
*/

static const vec4_t colorActive = { 0.2f, 1.0f, 0.2f, 1.0f };
static const vec4_t colorResting = { 0.6f, 0.6f, 0.6f, 1.0f };
static const vec4_t colorSolid = { 1.0f, 0.2f, 0.2f, 1.0f };
static const vec4_t colorContact = { 1.0f, 1.0f, 0.0f, 1.0f };
static const vec4_t colorLinear = { 1.0f, 0.3f, 0.3f, 1.0f };
static const vec4_t colorAngular = { 0.2f, 0.9f, 1.0f, 1.0f };
static const vec4_t colorImpact = { 1.0f, 0.2f, 1.0f, 1.0f };
static const vec4_t colorBounds = { 1.0f, 0.6f, 0.1f, 1.0f };
static const vec4_t colorAxisX = { 1.0f, 0.0f, 0.0f, 1.0f };
static const vec4_t colorAxisY = { 0.0f, 1.0f, 0.0f, 1.0f };
static const vec4_t colorAxisZ = { 0.0f, 0.0f, 1.0f, 1.0f };

static void G_DrawTraceModel( const rigidBody_t *body, const clipModel_t *cm, const float *color ) {
	const traceModel_t *trm;
	vec3_t a, b, tmp;
	int i;

	trm = ClipModelGetCachedTraceModel( cm->traceModelIndex );
	for ( i = 1; i <= trm->numEdges; i++ ) {
		M3_MulVec( (const vec3_t *)body->current.i.orientation, trm->verts[trm->edges[i].v[0]], tmp );
		VectorAdd( tmp, body->current.i.position, a );
		M3_MulVec( (const vec3_t *)body->current.i.orientation, trm->verts[trm->edges[i].v[1]], tmp );
		VectorAdd( tmp, body->current.i.position, b );
		trap_Debug_Line( color, a, b, 0, qfalse );
	}
}

// the hull, or the pieces of a compound prop
static void G_DrawPropHull( const rigidBody_t *body, const float *color ) {
	const gentity_t *ent = body->self;
	int i;

	if ( !ent->numPartClipModels ) {
		G_DrawTraceModel( body, &ent->clipModel, color );
		return;
	}
	for ( i = 0; i < ent->numPartClipModels; i++ ) {
		G_DrawTraceModel( body, &ent->partClipModels[i], color );
	}
}

static void G_DrawPropDebug( const rigidBody_t *body, int flags ) {
	vec3_t start, end, v, massCenter;
	int i;

	RB_WorldCenterOfMass( body, massCenter );

	if ( flags & 1 ) {
		G_DrawPropHull( body, body->startedInSolid ? colorSolid : ( RB_IsAtRest( body ) ? colorResting : colorActive ) );
	}

	if ( flags & 2 ) {
		for ( i = 0; i < body->numContacts; i++ ) {
			const contactInfo_t *c = &body->contacts[i];
			VectorMA( c->point, 8.0f, c->normal, end );
			trap_Debug_Arrow( colorContact, c->point, end, 2, 0 );
		}
	}

	if ( flags & 4 ) {
		RB_LinearVelocity( body, v );
		if ( VectorLengthSquared( v ) > 1.0f ) {
			VectorMA( massCenter, 0.1f, v, end );
			trap_Debug_Arrow( colorLinear, massCenter, end, 2, 0 );
		}
		RB_AngularVelocity( body, v );
		if ( VectorLengthSquared( v ) > 0.01f ) {
			VectorMA( massCenter, 8.0f, v, end );
			trap_Debug_Arrow( colorAngular, massCenter, end, 2, 0 );
		}
	}

	if ( ( flags & 8 ) && body->lastImpactTime && level.time - body->lastImpactTime < 1000 ) {
		VectorMA( body->lastImpactPoint, 4.0f + body->lastImpactSpeed * 0.05f, body->lastImpactNormal, end );
		trap_Debug_Arrow( colorImpact, body->lastImpactPoint, end, 3, 0 );
	}

	if ( flags & 16 ) {
		VectorCopy( massCenter, start );
		VectorMA( start, 16.0f, body->current.i.orientation[0], end );
		trap_Debug_Line( colorAxisX, start, end, 0, qfalse );
		VectorMA( start, 16.0f, body->current.i.orientation[1], end );
		trap_Debug_Line( colorAxisY, start, end, 0, qfalse );
		VectorMA( start, 16.0f, body->current.i.orientation[2], end );
		trap_Debug_Line( colorAxisZ, start, end, 0, qfalse );
	}
}

/*
================
G_PhysicsDebugDraw

Runs once per server frame after the simulation.
================
*/
void G_PhysicsDebugDraw( void ) {
	static qboolean wasDrawing;
	gentity_t *viewer;
	vec3_t eye, bounds[2];
	int i, numBodies, numResting;

	// runs once per frame, after the props, picks up cvar changes for the next frame
	G_PhysicsUpdateCvars();

	if ( ( g_debugPhysics.integer & 32 ) && level.time - physicsLastStatsTime >= 1000 ) {
		for ( i = 0, numBodies = 0, numResting = 0; i < MAX_PHYSICS_BODIES; i++ ) {
			if ( g_bodies[i].inUse ) {
				numBodies++;
				if ( RB_IsAtRest( &g_bodies[i] ) ) {
					numResting++;
				}
			}
		}
		G_Printf( "physics: %d props, %d resting, %d steps, %d collisions in the last second\n",
					numBodies, numResting, physicsSteps, physicsCollisions );
		physicsLastStatsTime = level.time;
		physicsSteps = physicsCollisions = 0;
	}
	physicsActiveBodies = 0;

	if ( !g_debugPhysics.integer && !g_debugCollision.integer ) {
		if ( wasDrawing ) {
			trap_Debug_ClearLines( level.time );
			trap_Debug_ClearPolygons( level.time );
			wasDrawing = qfalse;
		}
		return;
	}
	wasDrawing = qtrue;

	// everything is redrawn every server frame
	trap_Debug_ClearLines( level.time );
	trap_Debug_ClearPolygons( level.time );

	for ( i = 0; i < MAX_PHYSICS_BODIES; i++ ) {
		if ( g_bodies[i].inUse ) {
			G_DrawPropDebug( &g_bodies[i], g_debugPhysics.integer );
		}
	}

	// the collision views follow the first player
	viewer = NULL;
	for ( i = 0; i < level.maxplayers; i++ ) {
		if ( g_entities[i].inuse && g_entities[i].player && !( g_entities[i].r.svFlags & SVF_BOT ) ) {
			viewer = &g_entities[i];
			break;
		}
	}
	if ( !viewer ) {
		return;
	}

	VectorCopy( viewer->player->ps.origin, eye );
	eye[2] += viewer->player->ps.viewheight;

	if ( g_debugCollision.integer & 1 ) {
		trap_CM_DrawModel( 0, vec3_origin, axisDefault, eye, 512.0f );
	}
	if ( g_debugCollision.integer & 2 ) {
		ClipDrawClipModels( &g_clip, eye, 512.0f, (sharedEntity_t *)viewer );
	}
	if ( g_debugCollision.integer & 4 ) {
		for ( i = 0; i < level.num_entities; i++ ) {
			gentity_t *ent = &g_entities[i];

			if ( !ent->inuse || !ent->r.linked || ent == viewer || !( ent->player || ent->body ) ) {
				continue;
			}
			VectorCopy( ent->r.absmin, bounds[0] );
			VectorCopy( ent->r.absmax, bounds[1] );
			trap_Debug_Bounds( colorBounds, (const vec3_t *)bounds, vec3_origin, 0 );
		}
	}
}

