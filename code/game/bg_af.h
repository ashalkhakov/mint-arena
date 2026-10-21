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
// bg_af.h -- articulated figures, a port of Doom 3's idPhysics_AF (ragdolls)
//
// Bodies (trace models) held together by constraints. A tree of primary
// constraints is solved in linear time (idAFTree); limits, contacts and
// friction are auxiliary, solved as a box LCP (idLCP_Symmetric, idlib/lcp.c).
// The game and the cgame both run it: the caller passes how the bodies
// collide (afClip_t). The figure comes from a Doom 3 .af file (bg_afdecl.c).

#ifndef __BG_AF_H__
#define __BG_AF_H__

#define AF_MAX_BODIES			32
#define AF_MAX_CONSTRAINTS		48
#define AF_MAX_CONTACTS			64			// per figure and step
#define AF_MAX_NAME				32

// idMatX and idVecX, no bigger than an articulated figure needs: a body's
// spatial matrices are 6x6, a constraint has up to 6 rows
typedef struct {
	int			rows, cols;
	float		m[6][6];
} afMat_t;

typedef struct {
	int			size;
	float		v[6];
} afVec_t;

typedef enum {
	CONSTRAINT_INVALID,
	CONSTRAINT_FIXED,
	CONSTRAINT_BALLANDSOCKETJOINT,
	CONSTRAINT_UNIVERSALJOINT,
	CONSTRAINT_HINGE,
	CONSTRAINT_HINGESTEERING,
	CONSTRAINT_SLIDER,
	CONSTRAINT_CYLINDRICALJOINT,
	CONSTRAINT_LINE,
	CONSTRAINT_PLANE,
	CONSTRAINT_SPRING,
	CONSTRAINT_CONTACT,
	CONSTRAINT_FRICTION,
	CONSTRAINT_CONELIMIT,
	CONSTRAINT_PYRAMIDLIMIT,
	CONSTRAINT_SUSPENSION
} afConstraintType_t;

typedef enum {
	AF_LIMIT_NONE,
	AF_LIMIT_CONE,
	AF_LIMIT_PYRAMID
} afLimitType_t;

// a joint's limit (idAFConstraint_ConeLimit, idAFConstraint_PyramidLimit):
// what it's set up with, a frame constraint is made of it each step
typedef struct {
	afLimitType_t	type;
	vec3_t		anchor;					// the top, in body2's space (world's without)
	vec3_t		axis;					// cone axis, in body2's space
	float		cosAngle, sinHalfAngle, cosHalfAngle;
	vec3_t		basis[3];				// pyramid basis, in body2's space
	float		cosAngles[2], sinHalfAngles[2], cosHalfAngles[2];
	vec3_t		body1Axis;				// in body1's space, kept in the cone or pyramid
	float		epsilon;				// -1 the default
} afLimit_t;

struct afBody_s;
struct afTree_s;
struct afPhysics_s;

typedef struct afConstraint_s {
	afConstraintType_t		type;
	char					name[AF_MAX_NAME];
	struct afBody_s			*body1;
	struct afBody_s			*body2;		// NULL for the world
	struct afPhysics_s		*physics;

	// set by Evaluate
	afMat_t					J1, J2;		// left hand side of the constraint equations
	afVec_t					c1, c2;		// right hand side
	afVec_t					lo, hi, e;	// low and high bounds and lcp epsilon
	struct afConstraint_s	*boxConstraint;
	int						boxIndex[6];

	// used during calculations
	afMat_t					invI;		// transformed inertia
	afMat_t					J;			// transformed constraint matrix
	afVec_t					s;			// temp solution
	afVec_t					lm;			// lagrange multipliers
	int						firstIndex;

	qboolean				allowPrimary;
	qboolean				frameConstraint;
	qboolean				noCollision;
	qboolean				isPrimary;
	qboolean				isZero;

	// the joints'
	vec3_t					anchor1, anchor2;	// in body1's and body2's space (world's without body2)
	vec3_t					shaft1, shaft2;		// universal joint
	vec3_t					axis1, axis2;		// universal joint's cardan axis, hinge's axis
	vec3_t					offset;				// fixed
	vec3_t					relAxis[3];			// fixed
	vec3_t					initialAxis[3];		// hinge
	float					friction;
	afLimit_t				limit;

	// a frame constraint's
	struct afConstraint_s	*joint;			// what a joint friction or limit belongs to
	contactInfo_t			contact;		// contact
} afConstraint_t;

typedef struct {
	vec3_t		worldOrigin;
	vec3_t		worldAxis[3];
	float		spatialVelocity[6];		// linear and angular velocity
	float		externalForce[6];		// force and torque
} afBodyPState_t;

typedef struct afBody_s {
	char					name[AF_MAX_NAME];
	int						id;
	struct afBody_s			*parent;
	struct afBody_s			*children[AF_MAX_BODIES];
	int						numChildren;
	traceModel_t			trm;			// about the center of mass
	int						contents;
	afConstraint_t			*primaryConstraint;
	afConstraint_t			*constraints[AF_MAX_CONSTRAINTS];
	int						numConstraints;
	struct afTree_s			*tree;
	float					linearFriction, angularFriction, contactFriction;
	float					bouncyness;
	int						clipMask;
	qboolean				clipMaskSet;
	qboolean				selfCollision;
	qboolean				spatialInertiaSparse;
	qboolean				useFrictionDir, useContactMotorDir;
	qboolean				isZero;
	qboolean				clipDisabled;	// while another body moves, for self collision
	unsigned int			noCollide;		// the bodies (bits by index) it doesn't collide with: near it in the figure
	vec3_t					frictionDir;
	vec3_t					contactMotorDir;
	float					contactMotorVelocity, contactMotorForce;

	float					mass, invMass;
	vec3_t					centerOfMass;
	vec3_t					inertiaTensor[3];
	vec3_t					inverseInertiaTensor[3];

	afBodyPState_t			state[2];
	afBodyPState_t			*current, *next;
	afBodyPState_t			saved;
	vec3_t					atRestOrigin;
	vec3_t					atRestAxis[3];

	afMat_t					inverseWorldSpatialInertia;
	afMat_t					I, invI;
	afMat_t					J;
	afVec_t					s;
	afVec_t					totalForce;
	afVec_t					auxForce;
	afVec_t					acceleration;
	float					*response;		// 8 floats a response
	int						*responseIndex;
	int						numResponses;
	int						maxAuxiliaryIndex;
	int						maxSubTreeAuxiliaryIndex;
} afBody_t;

typedef struct afTree_s {
	afBody_t				*sortedBodies[AF_MAX_BODIES];
	int						numBodies;
} afTree_t;

// how the bodies collide; body is the moving one's number. A self colliding
// figure's bodies collide with those not clipDisabled.
typedef struct {
	void		*ctx;
	// translation from start to end, then rotation about end; qtrue on a collision
	qboolean	(*motion)( void *ctx, const struct afPhysics_s *af, int body, cm_trace_t *results, const vec3_t start,
							const vec3_t end, const rotation_t *rotation, const traceModel_t *trm, const vec3_t axis[3], int mask );
	int			(*contents)( void *ctx, const struct afPhysics_s *af, int body, const vec3_t origin,
							const traceModel_t *trm, const vec3_t axis[3], int mask );
	int			(*contacts)( void *ctx, const struct afPhysics_s *af, int body, contactInfo_t *contacts, int maxContacts,
							const vec3_t origin, const vec6_t dir, float depth, const traceModel_t *trm, const vec3_t axis[3], int mask );
	// a collision's impact on what was hit, may be NULL
	void		(*impact)( void *ctx, const struct afPhysics_s *af, int body, const cm_trace_t *collision, const vec3_t impulse );
	// a collision model of a trace model to trace against, for self
	// collision (the engine's temporary one); NULL for no self collision
	cmHandle_t	(*trmModel)( void *ctx, const traceModel_t *trm );
} afClip_t;

typedef struct {
	int			atRest;					// >= 0 at rest, since then
	float		noMoveTime;
	float		activateTime;
	float		lastTimeStep;
	float		pushVelocity[6];
} afPState_t;

typedef struct afPhysics_s {
	afTree_t				trees[AF_MAX_BODIES];
	int						numTrees;
	afBody_t				bodies[AF_MAX_BODIES];
	int						numBodies;
	afConstraint_t			constraints[AF_MAX_CONSTRAINTS];
	int						numConstraints;
	afConstraint_t			*primaryConstraints[AF_MAX_CONSTRAINTS];
	int						numPrimaryConstraints;
	afConstraint_t			*auxiliaryConstraints[AF_MAX_CONSTRAINTS];	// then the frame constraints are added
	int						numAuxiliaryConstraints;
	contactInfo_t			contacts[AF_MAX_CONTACTS];
	int						contactBodies[AF_MAX_CONTACTS];
	int						numContacts;
	qboolean				changedAF;

	float					linearFriction, angularFriction, contactFriction;
	float					bouncyness;
	int						clipMask;
	float					totalMass, forceTotalMass;

	float					suspendVelocity[2], suspendAcceleration[2];
	float					noMoveTime, noMoveTranslation, noMoveRotation;
	float					minMoveTime, maxMoveTime;
	float					impulseThreshold;

	float					timeScale;
	float					jointFrictionScale;
	float					contactFrictionScale;

	qboolean				enableCollision;
	qboolean				selfCollision;
	qboolean				comeToRest;
	qboolean				noImpact;

	vec3_t					gravityVector;
	afPState_t				current;
	int						time;				// the caller's, for atRest

	afClip_t				clip;
} afPhysics_t;

// bg_af.c
void		AF_Init( afPhysics_t *af );
afBody_t	*AF_AddBody( afPhysics_t *af, const char *name, const traceModel_t *trm, float density,
						const vec3_t origin, const vec3_t axis[3], const vec3_t inertiaScale[3] );
afConstraint_t *AF_AddConstraint( afPhysics_t *af, afConstraintType_t type, const char *name, afBody_t *body1, afBody_t *body2 );
afBody_t	*AF_GetBody( const afPhysics_t *af, const char *name );
int			AF_GetBodyId( const afPhysics_t *af, const char *name );
void		AF_SetBodyFriction( afBody_t *body, float linear, float angular, float contact );

void		AF_SetAnchor( afConstraint_t *c, const vec3_t worldPosition );
void		AF_SetShafts( afConstraint_t *c, const vec3_t cardanShaft1, const vec3_t cardanShaft2 );
void		AF_SetHingeAxis( afConstraint_t *c, const vec3_t axis );
void		AF_InitFixedOffset( afConstraint_t *c );
void		AF_SetConeLimit( afConstraint_t *c, const vec3_t coneAxis, float coneAngle, const vec3_t body1Axis );
void		AF_SetPyramidLimit( afConstraint_t *c, const vec3_t pyramidAxis, const vec3_t baseAxis,
							float angle1, float angle2, const vec3_t body1Axis );
void		AF_SetNoLimit( afConstraint_t *c );

qboolean	AF_Evaluate( afPhysics_t *af, int timeStepMSec, int endTimeMSec );
void		AF_Activate( afPhysics_t *af );
void		AF_ExcludeNearBodies( afPhysics_t *af );
void		AF_ExcludeTouchingBodies( afPhysics_t *af );
void		AF_Rest( afPhysics_t *af );
qboolean	AF_IsAtRest( const afPhysics_t *af );
void		AF_ApplyImpulse( afPhysics_t *af, int id, const vec3_t point, const vec3_t impulse );
void		AF_AddForce( afPhysics_t *af, int id, const vec3_t point, const vec3_t force );
void		AF_GetImpactInfo( const afPhysics_t *af, int id, const vec3_t point, float *invMass,
							vec3_t invInertiaTensor[3], vec3_t position, vec3_t velocity );
void		AF_GetBounds( const afPhysics_t *af, vec3_t absBounds[2] );
float		AF_ImpactSpeed( const afPhysics_t *af, int body, const vec3_t impulse );
void		AF_DebugDraw( const afPhysics_t *af, void (*line)( const vec3_t start, const vec3_t end, int color ) );


//
// bg_afdecl.c: Doom 3's .af files, and idAF: the figure on a skeleton
//
#define AF_MAX_DECLS			128		// a ragdoll for every character and monster

typedef enum {
	AFVEC_COORDS,
	AFVEC_JOINT,
	AFVEC_BONECENTER,
	AFVEC_BONEDIR
} afVectorType_t;

typedef struct {
	afVectorType_t	type;
	char		joint1[AF_MAX_NAME], joint2[AF_MAX_NAME];
	vec3_t		vec;
	qboolean	negate;
} afVector_t;

typedef enum {
	AF_JOINTMOD_AXIS,
	AF_JOINTMOD_ORIGIN,
	AF_JOINTMOD_BOTH
} afJointMod_t;

typedef struct {
	char		name[AF_MAX_NAME];
	char		jointName[AF_MAX_NAME];
	afJointMod_t	jointMod;
	int			modelType;					// TRM_*
	afVector_t	v1, v2;
	float		width;						// a bone's
	int			numSides;
	afVector_t	origin;
	vec3_t		angles;
	int			contents, clipMask;
	qboolean	selfCollision;
	vec3_t		inertiaScale[3];
	float		linearFriction, angularFriction, contactFriction;
	float		density;
	afVector_t	frictionDirection;
	afVector_t	contactMotorDirection;
	char		containedJoints[256];
} afDeclBody_t;

typedef struct {
	char		name[AF_MAX_NAME];
	afConstraintType_t	type;
	char		body1[AF_MAX_NAME], body2[AF_MAX_NAME];
	float		friction;
	afVector_t	anchor, anchor2;
	afVector_t	shaft[2];
	afVector_t	axis;
	afLimitType_t	limit;
	afVector_t	limitAxis;
	float		limitAngles[3];
} afDeclConstraint_t;

typedef struct {
	char		name[MAX_QPATH];
	float		defaultLinearFriction, defaultAngularFriction, defaultContactFriction, defaultConstraintFriction;
	float		totalMass;
	float		suspendVelocity[2], suspendAcceleration[2];
	float		noMoveTime, noMoveTranslation, noMoveRotation;
	float		minMoveTime, maxMoveTime;
	int			contents, clipMask;
	qboolean	selfCollision;
	afDeclBody_t	bodies[AF_MAX_BODIES];
	int			numBodies;
	afDeclConstraint_t	constraints[AF_MAX_CONSTRAINTS];
	int			numConstraints;
} afDecl_t;

typedef struct {
	int			bodyId;
	int			joint;
	afJointMod_t	jointMod;
	vec3_t		jointBodyOrigin;			// the body relative to the joint
	vec3_t		jointBodyAxis[3];
} afJointModInfo_t;

// idAF: the figure of a skeletal model (bg_model.c)
typedef struct afFigure_s {
	const afDecl_t		*decl;
	const bgModel_t		*model;
	afPhysics_t			physics;
	afJointModInfo_t	jointMods[AF_MAX_BODIES];
	int					numJointMods;
	vec3_t				baseOrigin;			// the origin body in the AF pose's model space
	vec3_t				baseAxis[3];
	float				afPose[MAX_MODEL_JOINTS * 12];	// the pose the figure was made in, model space
	int					numJoints;
	float				scale;				// the model's: the figure is this much bigger
	int					poseTime;
	qboolean			isLoaded;
} afFigure_t;

const afDecl_t	*BG_FindAFDecl( const char *name );
qboolean	BG_AFLoad( afFigure_t *af, const afDecl_t *decl, const bgModel_t *model, const float *afPose, int numJoints, float scale );
void		BG_AFSetupPose( afFigure_t *af, const float *jointMats, const vec3_t origin, const vec3_t axis[3], int time );
void		BG_AFChangePose( afFigure_t *af, const float *jointMats, const vec3_t origin, const vec3_t axis[3], int time );
void		BG_AFTestSolid( afFigure_t *af );
void		BG_AFStart( afFigure_t *af, int time );
void		BG_AFPose( const afFigure_t *af, float *jointMats, vec3_t origin, vec3_t axis[3] );

// bg_snapshot.c: a figure in an entity's snapshot words (Doom 3's
// idPhysics_AF::WriteToSnapshot): the first body's origin goes in pos, the
// bodies' orientations in the words; enough for BG_AFPose
void		BG_WriteAFSnapshot( const afFigure_t *af, entityState_t *es );
qboolean	BG_ReadAFSnapshot( afFigure_t *af, const entityState_t *es, const entityState_t *next, float frac, const vec3_t origin );

#endif /* !__BG_AF_H__ */
