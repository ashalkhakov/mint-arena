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
// g_physics.h -- rigid body physics, a port of Doom 3's idPhysics_RigidBody

#ifndef __G_PHYSICS_H__
#define __G_PHYSICS_H__

#define MAX_PHYSICS_BODIES		256
#define MAX_BODY_CONTACTS		10
#define CONTACT_EPSILON			0.25f		// maximum contact seperation distance

// integrated state of a rigid body
typedef struct {
	vec3_t			position;				// position of trace model
	vec3_t			orientation[3];			// orientation of trace model
	vec3_t			linearMomentum;			// translational momentum relative to center of mass
	vec3_t			angularMomentum;		// rotational momentum relative to center of mass
} rigidBodyIState_t;

typedef struct {
	int				atRest;					// set when simulation is suspended
	float			lastTimeStep;			// length of last time step
	vec3_t			externalForce;			// external force relative to center of mass
	vec3_t			externalTorque;			// external torque relative to center of mass
	rigidBodyIState_t	i;					// state used for integration
} rigidBodyPState_t;

// what a collision needs to know about the other entity
typedef struct {
	float			invMass;				// inverse mass, 0 for immovable objects
	vec3_t			invInertiaTensor[3];	// inverse inertia tensor
	vec3_t			position;				// impact position relative to center of mass
	vec3_t			velocity;				// velocity at the impact position
} impactInfo_t;

typedef struct rigidBody_s {
	qboolean		inUse;
	gentity_t		*self;

	rigidBodyPState_t	current;

	propShape_t		shape;

	// rigid body properties
	float			linearFriction;			// translational friction
	float			angularFriction;		// rotational friction
	float			contactFriction;		// friction with contact surfaces
	float			bouncyness;				// bouncyness
	int				clipMask;

	// derived properties
	float			mass;					// mass of body
	float			inverseMass;			// 1 / mass
	vec3_t			centerOfMass;			// center of mass of trace model
	vec3_t			inertiaTensor[3];		// mass distribution
	vec3_t			inverseInertiaTensor[3];	// inverse inertia tensor

	qboolean		noImpact;				// if true do not activate when another object collides
	qboolean		noContact;				// if true do not determine contacts and no contact friction

	contactInfo_t	contacts[MAX_BODY_CONTACTS];
	int				numContacts;

	// for the debug view
	vec3_t			lastImpactPoint;
	vec3_t			lastImpactNormal;
	float			lastImpactSpeed;
	int				lastImpactTime;
	qboolean		startedInSolid;
} rigidBody_t;

// g_physics.c
void		G_InitPhysics( void );
void		G_RunPhysicsProp( gentity_t *ent );
void		G_PhysicsLinkProp( gentity_t *ent );
void		G_PhysicsFreeBody( gentity_t *ent );
void		G_PhysicsApplyImpulse( gentity_t *ent, const vec3_t point, const vec3_t impulse );
void		G_PhysicsDamage( gentity_t *targ, gentity_t *inflictor, const vec3_t dir, const vec3_t point, int knockback, int dflags );
void		G_PhysicsPushedByPlayer( gentity_t *prop, gentity_t *player );
qboolean	G_PhysicsPushedByMover( gentity_t *prop, const vec3_t move, const vec3_t move2, const vec3_t matrix[3] );
void		G_PhysicsDebugDraw( void );
void		G_PhysicsClear( void );

gentity_t	*G_SpawnPhysicsProp( propShape_t shape, const vec3_t origin, const vec3_t angles,
								 const vec3_t mins, const vec3_t maxs, float density );

void		SP_func_physics( gentity_t *ent );

// client commands
void		Cmd_Prop_f( gentity_t *ent );
void		Cmd_PropStack_f( gentity_t *ent );
void		Cmd_PropPyramid_f( gentity_t *ent );
void		Cmd_PropThrow_f( gentity_t *ent );
void		Cmd_PropClear_f( gentity_t *ent );

// server commands
void		Svcmd_PropSpawn_f( void );
void		Svcmd_PropList_f( void );
void		Svcmd_PropClear_f( void );
void		Svcmd_PropBlast_f( void );

#endif /* !__G_PHYSICS_H__ */
