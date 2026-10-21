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
// bg_pmove.c -- both games player movement code
// takes a playerstate and a usercmd as input and returns a modifed playerstate

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_local.h"

pmove_t		*pm;
pml_t		pml;

// movement parameters
float	pm_stopspeed = 100.0f;
float	pm_duckScale = 0.25f;
float	pm_swimScale = 0.50f;
float	pm_ladderScale = 1.0f;

#define LADDER_CLIMB_SPEED	50		// a quarter of Daikatana's 200 (pmove.cpp): its climb animation climbs ~37 a second
#define LADDER_SIDE_SPEED	20		// and across, a quarter of its 75
#define LADDER_VIEW_YAW		70		// how far the view turns from a ladder
#define LADDER_HOLD_REACH	4		// a ladder this far ahead is climbed into
#define LADDER_USE_REACH	16		// and this far, +activate takes hold of
#define LADDER_USE_ANGLE	30		// this far either side of the view too
#define LADDER_DOWN_PITCH	30		// looking this far down, forward climbs down
#define LADDER_OVER_RISE	32		// how far up over a ladder's top a ledge may be
#define LADDER_FROM_ABOVE	40		// how far down from an edge a ladder's taken hold of
#define LADDER_MOVE_MSEC	500		// onto a ladder from its top, or over it
#define LADDER_ABOVE_STEPS	7		// out from the edge 8 more units a try, to clear a ladder's box

float	pm_accelerate = 10.0f;
float	pm_airaccelerate = 1.0f;
float	pm_wateraccelerate = 4.0f;
float	pm_flyaccelerate = 8.0f;
float	pm_ladderAccelerate = 3000;

float	pm_friction = 6.0f;
float	pm_waterfriction = 1.0f;
float	pm_flightfriction = 3.0f;
float	pm_spectatorfriction = 5.0f;
float	pm_ladderfriction = 3000;

int		c_pmove = 0;


/* [QUARANTINE] - Weapon Animations
===================
PM_StartWeaponAnim, PM_ContinueWeaponAnim
===================
*/
static void PM_StartWeaponAnim(int anim)
{
	if (pm->ps->pm_type >= PM_DEAD) {
		return;
	}

	if ( pm->ps->weapAnimTimer > 0 ) {
		return;     // a high priority animation is running
	}

	pm->ps->weaponAnim = ((pm->ps->weaponAnim & ANIM_TOGGLEBIT) ^ ANIM_TOGGLEBIT) | anim;
}

static void PM_ContinueWeaponAnim(int anim)
{
	if ((pm->ps->weaponAnim & ~ANIM_TOGGLEBIT) == anim) {
		return;
	}

	PM_StartWeaponAnim( anim );
}


/*
===============
PM_AddEvent

===============
*/
void PM_AddEvent( int newEvent ) {
	BG_AddPredictableEventToPlayerstate( newEvent, 0, pm->ps );
}

/*
===============
PM_AddTouchEnt
===============
*/
void PM_AddTouchEnt( int entityNum ) {
	int		i;

	if ( entityNum == ENTITYNUM_WORLD ) {
		return;
	}
	if ( pm->numtouch == MAXTOUCH ) {
		return;
	}

	// see if it is already added
	for ( i = 0 ; i < pm->numtouch ; i++ ) {
		if ( pm->touchents[ i ] == entityNum ) {
			return;
		}
	}

	// add it
	pm->touchents[pm->numtouch] = entityNum;
	pm->numtouch++;
}

/*
===================
PM_StartTorsoAnim
===================
*/
static void PM_StartTorsoAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	pm->ps->torsoAnim = ( ( pm->ps->torsoAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}
static void PM_StartLegsAnim( int anim ) {
	if ( pm->ps->pm_type >= PM_DEAD ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	pm->ps->legsAnim = ( ( pm->ps->legsAnim & ANIM_TOGGLEBIT ) ^ ANIM_TOGGLEBIT )
		| anim;
}

static void PM_ContinueLegsAnim( int anim ) {
	if ( ( pm->ps->legsAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->legsTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartLegsAnim( anim );
}

static void PM_ContinueTorsoAnim( int anim ) {
	if ( ( pm->ps->torsoAnim & ~ANIM_TOGGLEBIT ) == anim ) {
		return;
	}
	if ( pm->ps->torsoTimer > 0 ) {
		return;		// a high priority animation is running
	}
	PM_StartTorsoAnim( anim );
}

static void PM_ForceLegsAnim( int anim ) {
	pm->ps->legsTimer = 0;
	PM_StartLegsAnim( anim );
}


/*
==================
PM_ClipVelocity

Slide off of the impacting surface
==================
*/
void PM_ClipVelocity( vec3_t in, vec3_t normal, vec3_t out, float overbounce ) {
	float	backoff;
	float	change;
	int		i;
	
	backoff = DotProduct (in, normal);
	
	if ( backoff < 0 ) {
		backoff *= overbounce;
	} else {
		backoff /= overbounce;
	}

	for ( i=0 ; i<3 ; i++ ) {
		change = normal[i]*backoff;
		out[i] = in[i] - change;
	}
}


/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
static void PM_Friction( void ) {
	vec3_t	vec;
	float	*vel;
	float	speed, newspeed, control;
	float	drop;
	
	vel = pm->ps->velocity;
	
	VectorCopy( vel, vec );
	if ( pml.walking ) {
		vec[2] = 0;	// ignore slope movement
	}

	speed = VectorLength(vec);
	if (speed < 1) {
		vel[0] = 0;
		vel[1] = 0;		// allow sinking underwater
		// FIXME: still have z friction underwater?
		return;
	}

	drop = 0;

	// apply ground friction
	if ( pm->waterlevel <= 1 ) {
		if ( pml.walking && !(pml.groundTrace.surfaceFlags & SURF_SLICK) ) {
			// if getting knocked back, no friction
			if ( ! (pm->ps->pm_flags & PMF_TIME_KNOCKBACK) ) {
				control = speed < pm_stopspeed ? pm_stopspeed : speed;
				drop += control*pm_friction*pml.frametime;
			}
		}
	}

	if ( pml.ladder ) { // If they're on a ladder... 
		drop += speed*pm_ladderfriction*pml.frametime;  // Add ladder friction! 
	}	

	// apply water friction even if just wading
	if ( pm->waterlevel ) {
		drop += speed*pm_waterfriction*pm->waterlevel*pml.frametime;
	}

	// apply flying friction
	if ( pm->ps->powerups[PW_FLIGHT]) {
		drop += speed*pm_flightfriction*pml.frametime;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR) {
		drop += speed*pm_spectatorfriction*pml.frametime;
	}

	// scale the velocity
	newspeed = speed - drop;
	if (newspeed < 0) {
		newspeed = 0;
	}
	newspeed /= speed;

	vel[0] = vel[0] * newspeed;
	vel[1] = vel[1] * newspeed;
	vel[2] = vel[2] * newspeed;
}


/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate( vec3_t wishdir, float wishspeed, float accel ) {
#if 1
	// q2 style
	int			i;
	float		addspeed, accelspeed, currentspeed;

	currentspeed = DotProduct (pm->ps->velocity, wishdir);
	addspeed = wishspeed - currentspeed;
	if (addspeed <= 0) {
		return;
	}
	accelspeed = accel*pml.frametime*wishspeed;
	if (accelspeed > addspeed) {
		accelspeed = addspeed;
	}
	
	for (i=0 ; i<3 ; i++) {
		pm->ps->velocity[i] += accelspeed*wishdir[i];	
	}
#else
	// proper way (avoids strafe jump maxspeed bug), but feels bad
	vec3_t		wishVelocity;
	vec3_t		pushDir;
	float		pushLen;
	float		canPush;

	VectorScale( wishdir, wishspeed, wishVelocity );
	VectorSubtract( wishVelocity, pm->ps->velocity, pushDir );
	pushLen = VectorNormalize( pushDir );

	canPush = accel*pml.frametime*wishspeed;
	if (canPush > pushLen) {
		canPush = pushLen;
	}

	VectorMA( pm->ps->velocity, canPush, pushDir, pm->ps->velocity );
#endif
}



/*
============
PM_CmdScale

Returns the scale factor to apply to cmd movements
This allows the clients to use axial -127 to 127 values for all directions
without getting a sqrt(2) distortion in speed.
============
*/
static float PM_CmdScale( usercmd_t *cmd ) {
	int		max;
	float	total;
	float	scale;

	max = abs( cmd->forwardmove );
	if ( abs( cmd->rightmove ) > max ) {
		max = abs( cmd->rightmove );
	}
	if ( abs( cmd->upmove ) > max ) {
		max = abs( cmd->upmove );
	}
	if ( !max ) {
		return 0;
	}

	total = sqrt( cmd->forwardmove * cmd->forwardmove
		+ cmd->rightmove * cmd->rightmove + cmd->upmove * cmd->upmove );
	scale = (float)pm->ps->speed * max / ( 127.0 * total );

	// walking: the character's "walk_scale" of its run, not Quake 3's half
	// (the walk button halves the move, 64 of 127)
	if ( ( cmd->buttons & BUTTON_WALKING ) && BG_ValidModel( pm->playerModel ) ) {
		scale *= bg_models[pm->playerModel].walkScale * 127.0f / 64.0f;
	}

	return scale;
}


/*
================
PM_SetMovementDir

Determine the rotation of the legs relative
to the facing dir
================
*/
static void PM_SetMovementDir( void ) {
	if ( pm->cmd.forwardmove || pm->cmd.rightmove ) {
		if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 0;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 2;
		} else if ( pm->cmd.rightmove < 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 3;
		} else if ( pm->cmd.rightmove == 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 4;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove < 0 ) {
			pm->ps->movementDir = 5;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove == 0 ) {
			pm->ps->movementDir = 6;
		} else if ( pm->cmd.rightmove > 0 && pm->cmd.forwardmove > 0 ) {
			pm->ps->movementDir = 7;
		}
	} else {
		// if they aren't actively going directly sideways,
		// change the animation to the diagonal so they
		// don't stop too crooked
		if ( pm->ps->movementDir == 2 ) {
			pm->ps->movementDir = 1;
		} else if ( pm->ps->movementDir == 6 ) {
			pm->ps->movementDir = 7;
		} 
	}
}


/*
=============
PM_CheckJump
=============
*/
static qboolean PM_CheckJump( void ) {
	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return qfalse;		// don't allow jump until all buttons are up
	}

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		return qfalse;
	}

	// must wait for jump to be released
	if ( pm->ps->pm_flags & PMF_JUMP_HELD ) {
		// clear upmove so cmdscale doesn't lower running speed
		pm->cmd.upmove = 0;
		return qfalse;
	}

	pml.groundPlane = qfalse;		// jumping away
	pml.walking = qfalse;
	pm->ps->pm_flags |= PMF_JUMP_HELD;

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pm->ps->velocity[2] = JUMP_VELOCITY;
	PM_AddEvent( EV_JUMP );

	if ( pm->cmd.forwardmove >= 0 ) {
		PM_ForceLegsAnim( LEGS_JUMP );
		pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
	} else {
		PM_ForceLegsAnim( LEGS_JUMPB );
		pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
	}

	return qtrue;
}

/*
=============
PM_CheckWaterJump
=============
*/
static qboolean	PM_CheckWaterJump( void ) {
	vec3_t	spot;
	int		cont;
	vec3_t	flatforward;

	if (pm->ps->pm_time) {
		return qfalse;
	}

	// check for water jump
	if ( pm->waterlevel != 2 ) {
		return qfalse;
	}

	flatforward[0] = pml.forward[0];
	flatforward[1] = pml.forward[1];
	flatforward[2] = 0;
	VectorNormalize (flatforward);

	VectorMA (pm->ps->origin, 30, flatforward, spot);
	spot[2] += 4;
	cont = pm->pointcontents (spot, pm->ps->playerNum );
	if ( !(cont & CONTENTS_SOLID) ) {
		return qfalse;
	}

	spot[2] += 16;
	cont = pm->pointcontents (spot, pm->ps->playerNum );
	if ( cont & (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_BODY) ) {
		return qfalse;
	}

	// jump out of water
	VectorScale (pml.forward, 200, pm->ps->velocity);
	pm->ps->velocity[2] = 350;

	pm->ps->pm_flags |= PMF_TIME_WATERJUMP;
	pm->ps->pm_time = 2000;

	return qtrue;
}

//============================================================================


/*
===================
PM_WaterJumpMove

Flying out of the water
===================
*/
static void PM_WaterJumpMove( void ) {
	// waterjump has no control, but falls

	PM_StepSlideMove( qtrue );

	pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	if (pm->ps->velocity[2] < 0) {
		// cancel as soon as we are falling down again
		pm->ps->pm_flags &= ~PMF_ALL_TIMES;
		pm->ps->pm_time = 0;
	}
}

/*
===================
PM_WaterMove

===================
*/
#define SWIM_TREAD_RATE		0.35f	// strokes treading water, of swimming's
#define SWIM_GLIDE			0.55f	// of the swim speed between pulls
#define SWIM_SURGE			1.35f	// and more at the pull's height (about the swim speed on the whole)
#define SWIM_TURN_RATE		150.0f	// degrees a second the body turns toward where the swimmer looks

/*
===================
PM_Swimming

Swimming, not wading: the head under, or waist deep off the bottom. Waist
deep on it, the player wades (walks); ankle deep, nothing changes.
===================
*/
static qboolean PM_Swimming( void ) {
	return pm->waterlevel == 3 || ( pm->waterlevel == 2 && pm->ps->groundEntityNum == ENTITYNUM_NONE );
}

/*
===================
PM_SwimAdvance

The stroke goes on, rate of a stroke's time.
===================
*/
static void PM_SwimAdvance( float rate ) {
	int stroke = BG_SwimStrokeMsec( pm->playerModel );

	pm->ps->swimPhase = ( pm->ps->swimPhase + (int)( (float)SWIM_PHASE * pml.msec * rate / stroke ) ) & ( SWIM_PHASE - 1 );
}

/*
===================
PM_SwimTread

Not swimming anywhere: treading water, the strokes slow and weak.
===================
*/
static void PM_SwimTread( void ) {
	PM_SwimAdvance( SWIM_TREAD_RATE );
}

/*
===================
PM_SwimStroke

Swimming somewhere: the stroke goes on, and pulls; between pulls the
swimmer glides. Where the swimmer goes turns toward where it means to at
most SWIM_TURN_RATE, so it swims in arcs rather than strafing about.
===================
*/
static void PM_SwimStroke( vec3_t wishdir, float *wishspeed ) {
	vec3_t heading, axis;
	float speed, angle, turn, c;

	PM_SwimAdvance( 1.0f );
	*wishspeed *= SWIM_GLIDE + SWIM_SURGE * BG_SwimStrokePull( pm->ps->swimPhase );

	// the body's heading: where it's going, if it's going
	VectorCopy( pm->ps->velocity, heading );
	speed = VectorNormalize( heading );
	if ( speed < 20 ) {
		return;
	}
	c = DotProduct( heading, wishdir );
	angle = RAD2DEG( acos( Com_Clamp( -1, 1, c ) ) );
	turn = SWIM_TURN_RATE * pml.frametime;
	if ( angle <= turn ) {
		return;
	}
	// turned from the heading toward the wish by turn degrees
	CrossProduct( heading, wishdir, axis );
	if ( VectorNormalize( axis ) < 0.001f ) {
		return;		// right back: the turn's anywhere, let the water stop it
	}
	RotatePointAroundVector( wishdir, axis, heading, turn );
	// and the body with it, so the turn builds up from frame to frame
	VectorScale( wishdir, speed, pm->ps->velocity );
}

#define SWIM_FLOAT_SPEED	50		// rising to the surface, not swimming
#define SWIM_DIVE_PITCH		25		// degrees down from the surface that dive in
#define SWIM_LEAP_SPEED		220		// up, out of the water

/*
===================
PM_SwimSurface

The swimmer's buoyancy and the surface. Under, not swimming anywhere, it
floats up. At the surface (the head out, water level 2) it stays there
swimming along it, unless it looks well down or crouches (diving in); a
jump there leaps out. qtrue when it leapt (the move's done).
===================
*/
static qboolean PM_SwimSurface( vec3_t wishvel, float scale ) {
	float pitch = AngleNormalize180( pm->ps->viewangles[PITCH] );

	if ( !scale ) {
		// floating: up to the surface, then held there
		wishvel[2] = pm->waterlevel == 3 ? SWIM_FLOAT_SPEED : 0;
		if ( pm->waterlevel < 3 && pm->ps->velocity[2] < 0 ) {
			pm->ps->velocity[2] *= 0.5f;
		}
		return qfalse;
	}
	if ( pm->waterlevel != 2 ) {
		return qfalse;		// under: swimming wherever it looks
	}

	// a jump at the surface leaps out (once a press)
	if ( pm->cmd.upmove > 0 && !( pm->ps->pm_flags & PMF_JUMP_HELD ) ) {
		pm->ps->pm_flags |= PMF_JUMP_HELD;
		pm->ps->velocity[2] = SWIM_LEAP_SPEED;
		PM_AddEvent( EV_JUMP );
		return qtrue;
	}
	if ( pm->cmd.upmove <= 0 ) {
		pm->ps->pm_flags &= ~PMF_JUMP_HELD;
	}

	// diving in: looking well down, or crouching
	if ( pitch > SWIM_DIVE_PITCH || pm->cmd.upmove < 0 ) {
		return qfalse;
	}
	// along the surface
	if ( wishvel[2] < 0 ) {
		wishvel[2] = 0;
	}
	if ( pm->ps->velocity[2] < 0 ) {
		pm->ps->velocity[2] *= 0.5f;
	}
	return qfalse;
}

static void PM_WaterMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;
	float	vel;

	if ( PM_CheckWaterJump() ) {
		PM_WaterJumpMove();
		return;
	}
#if 0
	// jump = head for surface
	if ( pm->cmd.upmove >= 10 ) {
		if (pm->ps->velocity[2] > -300) {
			if ( pm->watertype & CONTENTS_WATER ) {
				pm->ps->velocity[2] = 100;
			} else if ( pm->watertype & CONTENTS_SLIME ) {
				pm->ps->velocity[2] = 80;
			} else {
				pm->ps->velocity[2] = 50;
			}
		}
	}
#endif
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = -60;		// sink towards bottom
	} else {
		for (i=0 ; i<3 ; i++)
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;

		wishvel[2] += scale * pm->cmd.upmove;
	}

	// Turok's water: the body floats up, at the surface it swims along it
	// (looking down, or crouching, dives in), and jumping there leaps out
	if ( PM_SwimSurface( wishvel, scale ) ) {
		return;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if ( wishspeed > pm->ps->speed * pm_swimScale ) {
		wishspeed = pm->ps->speed * pm_swimScale;
	}

	// swimming as Turok swims: by strokes, each pull a surge and a glide after
	// it, and a body that turns only so fast (PM_SwimStroke)
	if ( scale ) {
		PM_SwimStroke( wishdir, &wishspeed );
	} else {
		PM_SwimTread();
	}

	PM_Accelerate (wishdir, wishspeed, pm_wateraccelerate);

	// make sure we can go up slopes easily under water
	if ( pml.groundPlane && DotProduct( pm->ps->velocity, pml.groundTrace.plane.normal ) < 0 ) {
		vel = VectorLength(pm->ps->velocity);
		// slide along the ground plane
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );

		// don't decrease velocity when going up or down a slope
		if ( pm->pmove_overbounce || VectorLength(pm->ps->velocity) > 1 ) {
			VectorNormalize(pm->ps->velocity);
			VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
		}
	}

	PM_SlideMove( qfalse );
}

/*
===================
PM_LadderMove()

Right now all I know is that this works for VERTICAL ladders. 
Ladders with angles on them (urban2 for AQ2) haven't been tested.
===================
*/
/*
===================
PM_LadderClimbSpeed

Climbing goes rung to rung, not steadily: a pull, then the next hand
reaching, at most LADDER_CLIMB_PEAK and at least LADDER_CLIMB_EASE of
LADDER_CLIMB_SPEED, by where the climber is between grips (half of the
model's "ladder_climb" each: the climb's frames go by height too, so it
pulls and pauses with them).
===================
*/
#define LADDER_CLIMB_EASE	0.5f	// of the speed while the next hand reaches
#define LADDER_CLIMB_PEAK	2.2f	// at the pull's height
#define LADDER_CLIMB_PULL	0.6f	// of a grip's height the pull takes

static float PM_LadderClimbSpeed( void ) {
	float grip = 13, f;

	if ( BG_ValidModel( pm->playerModel ) && bg_models[pm->playerModel].ladderClimb > 0 ) {
		grip = bg_models[pm->playerModel].ladderClimb * 0.5f;
	}
	f = pm->ps->origin[2] / grip;
	f -= floor( f );
	if ( f >= LADDER_CLIMB_PULL ) {
		return LADDER_CLIMB_SPEED * LADDER_CLIMB_EASE;
	}
	return LADDER_CLIMB_SPEED * ( LADDER_CLIMB_EASE + ( LADDER_CLIMB_PEAK - LADDER_CLIMB_EASE ) * sin( M_PI * f / LADDER_CLIMB_PULL ) );
}

static void PM_LadderMove( void ) {
	int i;
	vec3_t wishvel;
	float wishspeed;
	vec3_t wishdir;
	float scale;
	float vel, side, climb;

	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );

	// Daikatana's ladders (pmove.cpp, PM_AddCurrents): up or down at a fixed
	// speed, by looking up or down while going forward, or with jump and
	// crouch; across slowly
	for ( i = 0; i < 2; i++ ) {
		wishvel[i] = scale * ( pml.forward[i] * pm->cmd.forwardmove + pml.right[i] * pm->cmd.rightmove );
	}
	side = sqrt( wishvel[0] * wishvel[0] + wishvel[1] * wishvel[1] );
	if ( side > LADDER_SIDE_SPEED ) {
		wishvel[0] *= LADDER_SIDE_SPEED / side;
		wishvel[1] *= LADDER_SIDE_SPEED / side;
	}
	// held: forward (or jump) climbs, back (or crouch, or forward looking
	// well down) climbs down; across slowly; never off it
	climb = PM_LadderClimbSpeed();
	if ( pm->cmd.upmove > 0 || ( pm->cmd.forwardmove > 0 && pm->ps->viewangles[PITCH] < LADDER_DOWN_PITCH ) ) {
		wishvel[2] = climb;
	} else if ( pm->cmd.upmove < 0 || pm->cmd.forwardmove < 0
				|| ( pm->cmd.forwardmove > 0 && pm->ps->viewangles[PITCH] >= LADDER_DOWN_PITCH ) ) {
		wishvel[2] = -climb;
	} else {
		wishvel[2] = 0;
	}
	// across it only (pressing in or out does nothing), and against it
	side = DotProduct( wishvel, pml.ladderNormal );
	wishvel[0] -= side * pml.ladderNormal[0];
	wishvel[1] -= side * pml.ladderNormal[1];
	wishvel[0] -= pml.ladderNormal[0] * 10;
	wishvel[1] -= pml.ladderNormal[1] * 10;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	if ( wishspeed > climb * pm_ladderScale ) {
		wishspeed = climb * pm_ladderScale;
	}

	PM_Accelerate (wishdir, wishspeed, pm_ladderAccelerate);

	// This SHOULD help us with sloped ladders, but it remains untested.
	if ( pml.groundPlane && DotProduct( pm->ps->velocity,
		pml.groundTrace.plane.normal ) < 0 ) {
		vel = VectorLength(pm->ps->velocity);
		// slide along the ground plane [the ladder section under our feet] 
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );

		VectorNormalize(pm->ps->velocity);
		VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
	}

	PM_SlideMove( qfalse ); // move without gravity
}


/*
=============
PM_LadderAhead

Whether the box at origin, moved dist along dir, touches a ladder.
=============
*/
static qboolean PM_LadderAhead( const vec3_t origin, const vec3_t dir, float dist, trace_t *tr ) {
	vec3_t end;

	VectorMA( origin, dist, dir, end );
	pm->trace( tr, origin, pm->ps->mins, pm->ps->maxs, end, pm->ps->playerNum, MASK_PLAYERSOLID );
	return tr->fraction < 1 && !tr->startsolid && ( tr->surfaceFlags & SURF_LADDER );
}

/*
=============
PM_LadderNear

A ladder +activate can take hold of: within LADDER_USE_REACH of the box,
along the view or LADDER_USE_ANGLE to either side.
=============
*/
static qboolean PM_LadderNear( trace_t *tr ) {
	static const float turns[3] = { 0, LADDER_USE_ANGLE, -LADDER_USE_ANGLE };
	vec3_t dir;
	float yaw;
	int i;

	for ( i = 0; i < 3; i++ ) {
		yaw = DEG2RAD( pm->ps->viewangles[YAW] + turns[i] );
		VectorSet( dir, cos( yaw ), sin( yaw ), 0 );
		if ( PM_LadderAhead( pm->ps->origin, dir, LADDER_USE_REACH, tr ) ) {
			return qtrue;
		}
	}
	return qfalse;
}

/*
=============
PM_LadderHold

Taking hold of the ladder whose face's normal is normal (out of it).
=============
*/
static void PM_LadderHold( const vec3_t normal ) {
	pm->ps->pm_flags |= PMF_LADDER;
	pm->ps->ladderYaw = ANGLE2SHORT( RAD2DEG( atan2( -normal[1], -normal[0] ) ) ) & 0xffff;
	VectorClear( pm->ps->velocity );
}

/*
=============
PM_LadderFits

Whether the player's box fits at spot, standing or else crouched (then
crouched says so); qfalse when neither.
=============
*/
static qboolean PM_LadderFits( const vec3_t spot, qboolean *crouched ) {
	vec3_t mins, maxs;
	trace_t tr;

	BG_PlayerBox( pm->playerModel, qfalse, mins, maxs, NULL );
	pm->trace( &tr, spot, mins, maxs, spot, pm->ps->playerNum, MASK_PLAYERSOLID );
	if ( !tr.startsolid && !tr.allsolid ) {
		*crouched = qfalse;
		return qtrue;
	}
	BG_PlayerBox( pm->playerModel, qtrue, mins, maxs, NULL );
	pm->trace( &tr, spot, mins, maxs, spot, pm->ps->playerNum, MASK_PLAYERSOLID );
	*crouched = qtrue;
	return !tr.startsolid && !tr.allsolid;
}

/*
=============
PM_LadderBeginMove

Moving onto a ladder or over its top, along two legs over LADDER_MOVE_MSEC:
first by first (out over the edge, or up past the top), then on to end. It
goes through nothing (the way was traced); off says it ends off the ladder.
=============
*/
static void PM_LadderBeginMove( const vec3_t first, const vec3_t end, qboolean off ) {
	pm->ps->pm_flags |= PMF_LADDER | PMF_LADDER_MOVE;
	if ( off ) {
		pm->ps->pm_flags |= PMF_LADDER_OFF;
	} else {
		pm->ps->pm_flags &= ~PMF_LADDER_OFF;
	}
	VectorCopy( end, pm->ps->grapplePoint );
	pm->ps->pm_time = LADDER_MOVE_MSEC;
	VectorScale( first, 1.0f / ( LADDER_MOVE_MSEC * 0.5f * 0.001f ), pm->ps->velocity );		// the first half
}

/*
=============
PM_LadderMoving

The move onto a ladder or over its top, while it lasts: the first half at
its velocity, the second straight to its end; then held, or off it.
=============
*/
static void PM_LadderMoving( void ) {
	float seconds;

	if ( pm->ps->pm_time <= 0 ) {
		VectorCopy( pm->ps->grapplePoint, pm->ps->origin );
		VectorClear( pm->ps->velocity );
		pm->ps->pm_flags &= ~PMF_LADDER_MOVE;
		if ( pm->ps->pm_flags & PMF_LADDER_OFF ) {
			pm->ps->pm_flags &= ~( PMF_LADDER | PMF_LADDER_OFF );
		}
		return;
	}
	if ( pm->ps->pm_time <= LADDER_MOVE_MSEC / 2 ) {
		seconds = pm->ps->pm_time * 0.001f;
		VectorSubtract( pm->ps->grapplePoint, pm->ps->origin, pm->ps->velocity );
		VectorScale( pm->ps->velocity, 1.0f / seconds, pm->ps->velocity );
	}
	VectorMA( pm->ps->origin, pml.frametime, pm->ps->velocity, pm->ps->origin );
}

/*
=============
PM_LadderClimbOver

At a ladder's top: onto the ledge over it, crouched where the ceiling's low.
qfalse when there's no room.
=============
*/
static qboolean PM_LadderClimbOver( const vec3_t normal ) {
	vec3_t spot, ground, mins, maxs;
	float reach = pm->ps->maxs[0] * fabs( normal[0] ) + pm->ps->maxs[1] * fabs( normal[1] );
	qboolean crouched;
	trace_t tr;
	int h;

	for ( h = 0; h <= LADDER_OVER_RISE; h += 4 ) {
		VectorCopy( pm->ps->origin, spot );
		spot[2] += h;
		VectorMA( spot, -( 2 * reach + 4 ), normal, spot );
		if ( !PM_LadderFits( spot, &crouched ) ) {
			continue;
		}
		// something to stand on there
		BG_PlayerBox( pm->playerModel, crouched, mins, maxs, NULL );
		VectorCopy( spot, ground );
		ground[2] -= LADDER_OVER_RISE;
		pm->trace( &tr, spot, mins, maxs, ground, pm->ps->playerNum, MASK_PLAYERSOLID );
		if ( tr.fraction == 1 ) {
			continue;
		}
		// up, then over onto it (PM_LadderMoving)
		if ( crouched ) {
			pm->ps->pm_flags |= PMF_DUCKED;
		}
		VectorSet( ground, 0, 0, h );
		PM_LadderBeginMove( ground, spot, qtrue );
		return qtrue;
	}
	return qfalse;
}

/*
=============
PM_LadderFromAbove

Taking hold of a ladder from its top: over the edge the player faces, or
any other, down onto the ladder on the edge's face, facing it.
=============
*/
static qboolean PM_LadderFromAbove( qboolean take, int numDirs ) {
	vec3_t dir, back, over, spot;
	float yaw, reach;
	trace_t tr;
	int i;

	for ( i = 0; i < numDirs * LADDER_ABOVE_STEPS; i++ ) {
		yaw = pm->ps->viewangles[YAW] + ( i % numDirs ) * ( 360 / numDirs );
		VectorSet( dir, cos( DEG2RAD( yaw ) ), sin( DEG2RAD( yaw ) ), 0 );
		reach = pm->ps->maxs[0] * fabs( dir[0] ) + pm->ps->maxs[1] * fabs( dir[1] );
		// over the edge (and past the top of the ladder's box)...
		VectorMA( pm->ps->origin, 2 * reach + 4 + ( i / numDirs ) * 8, dir, over );
		pm->trace( &tr, pm->ps->origin, pm->ps->mins, pm->ps->maxs, over, pm->ps->playerNum, MASK_PLAYERSOLID );
		if ( tr.fraction < 1 ) {
			continue;
		}
		// ...down a body's height...
		VectorCopy( over, spot );
		spot[2] -= LADDER_FROM_ABOVE;
		pm->trace( &tr, over, pm->ps->mins, pm->ps->maxs, spot, pm->ps->playerNum, MASK_PLAYERSOLID );
		if ( tr.fraction < 1 ) {
			continue;		// a floor (or the ladder's top): not over the edge yet
		}
		// ...the ladder back under the edge
		VectorScale( dir, -1, back );
		if ( !PM_LadderAhead( spot, back, 2 * reach + 8 + ( i / numDirs ) * 8, &tr ) ) {
			continue;
		}
		if ( !take ) {
			return qtrue;		// there is one (the prompt)
		}
		// out over the edge, then down onto it (PM_LadderMoving)
		PM_LadderHold( tr.plane.normal );
		VectorSubtract( over, pm->ps->origin, spot );
		PM_LadderBeginMove( spot, tr.endpos, qfalse );
		// facing it
		yaw = SHORT2ANGLE( pm->ps->ladderYaw );
		pm->ps->delta_angles[YAW] += ANGLE2SHORT( yaw - pm->ps->viewangles[YAW] );
		pm->ps->viewangles[YAW] = yaw;
		AngleVectors( pm->ps->viewangles, pml.forward, pml.right, pml.up );
		return qtrue;
	}
	return qfalse;
}

/*
=============
CheckLadder

Ladders are held, never walked into: +activate takes hold of the one ahead
(or, at the top of one, lowers the player onto it) and lets go again; the
cgame shows when it can (PMF_LADDER_AHEAD, PMF_LADDER_BELOW). Held, the
player can only climb, and moving can't pull it off: at the top it goes
over onto the ledge, at the foot steps off.
=============
*/
void CheckLadder( void ) {
	vec3_t flatforward, normal;
	qboolean activate, climbUp;
	trace_t trace;

	pml.ladder = qfalse;
	activate = ( pm->cmd.buttons & BUTTON_ACTIVATE ) && !( pm->ps->pm_flags & PMF_ACTIVATE_HELD );
	// use pressed on something looked at takes or uses that instead (g_frob.c)
	if ( pm->ps->stats[STAT_FROB_ENTITY] > 0 && pm->ps->stats[STAT_FROB_ENTITY] != ENTITYNUM_NONE ) {
		activate = qfalse;
	}
	if ( pm->cmd.buttons & BUTTON_ACTIVATE ) {
		pm->ps->pm_flags |= PMF_ACTIVATE_HELD;
	} else {
		pm->ps->pm_flags &= ~PMF_ACTIVATE_HELD;
	}
	if ( pm->ps->pm_type != PM_NORMAL || pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->ps->pm_flags &= ~PMF_LADDER;
		return;
	}
	climbUp = pm->cmd.upmove > 0 || ( pm->cmd.forwardmove > 0 && pm->ps->viewangles[PITCH] < LADDER_DOWN_PITCH );

	// moving onto it or over its top: nothing to decide till it's done
	if ( pm->ps->pm_flags & PMF_LADDER_MOVE ) {
		pml.ladder = qtrue;
		VectorSet( pml.ladderNormal, -cos( DEG2RAD( SHORT2ANGLE( pm->ps->ladderYaw ) ) ),
				   -sin( DEG2RAD( SHORT2ANGLE( pm->ps->ladderYaw ) ) ), 0 );
		return;
	}

	if ( pm->ps->pm_flags & PMF_LADDER ) {
		VectorSet( normal, -cos( DEG2RAD( SHORT2ANGLE( pm->ps->ladderYaw ) ) ),
				   -sin( DEG2RAD( SHORT2ANGLE( pm->ps->ladderYaw ) ) ), 0 );
		if ( activate ) {
			pm->ps->pm_flags &= ~PMF_LADDER;		// let go
			return;
		}
		VectorScale( normal, -1, flatforward );
		if ( !PM_LadderAhead( pm->ps->origin, flatforward, LADDER_HOLD_REACH, &trace ) ) {
			// past its top: over onto the ledge if there's room, else hold
			// on at the top; going down or across, off it
			if ( climbUp && !PM_LadderClimbOver( normal ) ) {
				pml.ladder = qtrue;
				VectorCopy( normal, pml.ladderNormal );
				return;
			}
			pm->ps->pm_flags &= ~PMF_LADDER;
			return;
		}
		// at its foot, going down: off it
		if ( pml.groundPlane && !climbUp && ( pm->cmd.forwardmove || pm->cmd.upmove < 0 ) ) {
			pm->ps->pm_flags &= ~PMF_LADDER;
			return;
		}
		pml.ladder = qtrue;
		VectorCopy( normal, pml.ladderNormal );
		return;
	}

	// taking hold: the one ahead (+activate reaches further, and a little to
	// the sides), or from its top
	pm->ps->pm_flags &= ~( PMF_LADDER_AHEAD | PMF_LADDER_BELOW );
	flatforward[0] = pml.forward[0];
	flatforward[1] = pml.forward[1];
	flatforward[2] = 0;
	VectorNormalize( flatforward );
	if ( PM_LadderNear( &trace ) ) {
		if ( activate ) {
			VectorCopy( trace.endpos, pm->ps->origin );		// up against it
			PM_LadderHold( trace.plane.normal );
		} else {
			pm->ps->pm_flags |= PMF_LADDER_AHEAD;
		}
	} else if ( activate ) {
		PM_LadderFromAbove( qtrue, 8 );
	} else if ( pml.groundPlane && PM_LadderFromAbove( qfalse, 1 ) ) {
		pm->ps->pm_flags |= PMF_LADDER_BELOW;
	}
	if ( pm->ps->pm_flags & PMF_LADDER ) {
		pml.ladder = qtrue;
		VectorSet( pml.ladderNormal, -cos( DEG2RAD( SHORT2ANGLE( pm->ps->ladderYaw ) ) ),
				   -sin( DEG2RAD( SHORT2ANGLE( pm->ps->ladderYaw ) ) ), 0 );
	}
}

/*
=============
PM_LadderView

On a ladder the view turns only so far from it (as Resident Evil 2's remake
and the newer Wolfensteins have it; Daikatana let you turn and shoot): the
yaw is held within LADDER_VIEW_YAW of facing it, the way RTCW holds a
mounted gun's, through delta_angles so the client predicts it too.
=============
*/
static void PM_LadderView( void ) {
	float ladderYaw, diff, held;

	if ( !pml.ladder ) {
		return;
	}
	ladderYaw = RAD2DEG( atan2( -pml.ladderNormal[1], -pml.ladderNormal[0] ) );
	diff = AngleNormalize180( pm->ps->viewangles[YAW] - ladderYaw );
	if ( diff > LADDER_VIEW_YAW ) {
		held = LADDER_VIEW_YAW;
	} else if ( diff < -LADDER_VIEW_YAW ) {
		held = -LADDER_VIEW_YAW;
	} else {
		return;
	}
	pm->ps->delta_angles[YAW] += ANGLE2SHORT( held - diff );
	pm->ps->viewangles[YAW] = AngleMod( ladderYaw + held );
	AngleVectors( pm->ps->viewangles, pml.forward, pml.right, pml.up );
}


#ifdef MISSIONPACK
/*
===================
PM_InvulnerabilityMove

Only with the invulnerability powerup
===================
*/
static void PM_InvulnerabilityMove( void ) {
	pm->cmd.forwardmove = 0;
	pm->cmd.rightmove = 0;
	pm->cmd.upmove = 0;
	VectorClear(pm->ps->velocity);
}
#endif

/*
===================
PM_FlyMove

Only with the flight powerup
===================
*/
static void PM_FlyMove( void ) {
	int		i;
	vec3_t	wishvel;
	float	wishspeed;
	vec3_t	wishdir;
	float	scale;

	// normal slowdown
	PM_Friction ();

	scale = PM_CmdScale( &pm->cmd );
	//
	// user intentions
	//
	if ( !scale ) {
		wishvel[0] = 0;
		wishvel[1] = 0;
		wishvel[2] = 0;
	} else {
		for (i=0 ; i<3 ; i++) {
			wishvel[i] = scale * pml.forward[i]*pm->cmd.forwardmove + scale * pml.right[i]*pm->cmd.rightmove;
		}

		wishvel[2] += scale * pm->cmd.upmove;
	}

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);

	PM_Accelerate (wishdir, wishspeed, pm_flyaccelerate);

	PM_StepSlideMove( qfalse );
}


/*
===================
PM_AirMove

===================
*/
static void PM_AirMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;

	PM_Friction();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so players can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 2 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// not on ground, so little effect on velocity
	PM_Accelerate (wishdir, wishspeed, pm_airaccelerate);

	// we may have a ground plane that is very steep, even
	// though we don't have a groundentity
	// slide along the steep plane
	if ( pml.groundPlane ) {
		PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
			pm->ps->velocity, OVERCLIP );
	}

#if 0
	//ZOID:  If we are on the grapple, try stair-stepping
	//this allows a player to use the grapple to pull himself
	//over a ledge
	if (pm->ps->pm_flags & PMF_GRAPPLE_PULL)
		PM_StepSlideMove ( qtrue );
	else
		PM_SlideMove ( qtrue );
#endif

	PM_StepSlideMove ( qtrue );
}

/*
===================
PM_GrappleMove

===================
*/
static void PM_GrappleMove( void ) {
	vec3_t vel, v;
	float vlen;

	VectorScale(pml.forward, -16, v);
	VectorAdd(pm->ps->grapplePoint, v, v);
	VectorSubtract(v, pm->ps->origin, vel);
	vlen = VectorLength(vel);
	VectorNormalize( vel );

	if (vlen <= 100)
		VectorScale(vel, 10 * vlen, vel);
	else
		VectorScale(vel, 800, vel);

	VectorCopy(vel, pm->ps->velocity);

	pml.groundPlane = qfalse;
}

/*
===================
PM_WalkMove

===================
*/
static void PM_WalkMove( void ) {
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;
	usercmd_t	cmd;
	float		accelerate;
	float		vel;

	if ( pm->waterlevel > 2 && DotProduct( pml.forward, pml.groundTrace.plane.normal ) > 0 ) {
		// begin swimming
		PM_WaterMove();
		return;
	}


	if ( PM_CheckJump () ) {
		// jumped away
		if ( pm->waterlevel > 1 ) {
			PM_WaterMove();
		} else {
			PM_AirMove();
		}
		return;
	}

	PM_Friction ();

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;

	cmd = pm->cmd;
	scale = PM_CmdScale( &cmd );

	// set the movementDir so players can rotate the legs for strafing
	PM_SetMovementDir();

	// project moves down to flat plane
	pml.forward[2] = 0;
	pml.right[2] = 0;

	// project the forward and right directions onto the ground plane
	PM_ClipVelocity (pml.forward, pml.groundTrace.plane.normal, pml.forward, OVERCLIP );
	PM_ClipVelocity (pml.right, pml.groundTrace.plane.normal, pml.right, OVERCLIP );
	//
	VectorNormalize (pml.forward);
	VectorNormalize (pml.right);

	for ( i = 0 ; i < 3 ; i++ ) {
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	}
	// when going up or down slopes the wish velocity should Not be zero
//	wishvel[2] = 0;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	// clamp the speed lower if ducking
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		if ( wishspeed > pm->ps->speed * pm_duckScale ) {
			wishspeed = pm->ps->speed * pm_duckScale;
		}
	}

	// clamp the speed lower if wading or walking on the bottom
	if ( pm->waterlevel ) {
		float	waterScale;

		waterScale = pm->waterlevel / 3.0;
		waterScale = 1.0 - ( 1.0 - pm_swimScale ) * waterScale;
		// waist deep it's wading: a walk at most
		if ( pm->waterlevel >= 2 ) {
			waterScale = MIN( waterScale, BG_ValidModel( pm->playerModel ) ? bg_models[pm->playerModel].walkScale : 0.5f );
		}
		if ( wishspeed > pm->ps->speed * waterScale ) {
			wishspeed = pm->ps->speed * waterScale;
		}
	}

	// when a player gets hit, they temporarily lose
	// full control, which allows them to be moved a bit
	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		accelerate = pm_airaccelerate;
	} else {
		accelerate = pm_accelerate;
	}

	PM_Accelerate (wishdir, wishspeed, accelerate);

	//Com_Printf("velocity = %1.1f %1.1f %1.1f\n", pm->ps->velocity[0], pm->ps->velocity[1], pm->ps->velocity[2]);
	//Com_Printf("velocity1 = %1.1f\n", VectorLength(pm->ps->velocity));

	if ( ( pml.groundTrace.surfaceFlags & SURF_SLICK ) || pm->ps->pm_flags & PMF_TIME_KNOCKBACK ) {
		pm->ps->velocity[2] -= pm->ps->gravity * pml.frametime;
	} else {
		// don't reset the z velocity for slopes
//		pm->ps->velocity[2] = 0;
	}

	vel = VectorLength(pm->ps->velocity);

	// slide along the ground plane
	PM_ClipVelocity (pm->ps->velocity, pml.groundTrace.plane.normal, 
		pm->ps->velocity, OVERCLIP );

	// don't decrease velocity when going up or down a slope
	if ( pm->pmove_overbounce || VectorLength(pm->ps->velocity) > 1 ) {
		VectorNormalize(pm->ps->velocity);
		VectorScale(pm->ps->velocity, vel, pm->ps->velocity);
	}

	// don't do anything if standing still
	if (!pm->ps->velocity[0] && !pm->ps->velocity[1]) {
		return;
	}

	PM_StepSlideMove( qfalse );

	//Com_Printf("velocity2 = %1.1f\n", VectorLength(pm->ps->velocity));

}


/*
==============
PM_DeadMove
==============
*/
static void PM_DeadMove( void ) {
	float	forward;

	if ( !pml.walking ) {
		return;
	}

	// extra friction

	forward = VectorLength (pm->ps->velocity);
	forward -= 20;
	if ( forward <= 0 ) {
		VectorClear (pm->ps->velocity);
	} else {
		VectorNormalize (pm->ps->velocity);
		VectorScale (pm->ps->velocity, forward, pm->ps->velocity);
	}
}


/*
===============
PM_NoclipMove
===============
*/
static void PM_NoclipMove( void ) {
	float	speed, drop, friction, control, newspeed;
	int			i;
	vec3_t		wishvel;
	float		fmove, smove;
	vec3_t		wishdir;
	float		wishspeed;
	float		scale;

	pm->ps->viewheight = DEFAULT_VIEWHEIGHT;

	// friction

	speed = VectorLength (pm->ps->velocity);
	if (speed < 1)
	{
		VectorCopy (vec3_origin, pm->ps->velocity);
	}
	else
	{
		drop = 0;

		friction = pm_friction*1.5;	// extra friction
		control = speed < pm_stopspeed ? pm_stopspeed : speed;
		drop += control*friction*pml.frametime;

		// scale the velocity
		newspeed = speed - drop;
		if (newspeed < 0)
			newspeed = 0;
		newspeed /= speed;

		VectorScale (pm->ps->velocity, newspeed, pm->ps->velocity);
	}

	// accelerate
	scale = PM_CmdScale( &pm->cmd );

	fmove = pm->cmd.forwardmove;
	smove = pm->cmd.rightmove;
	
	for (i=0 ; i<3 ; i++)
		wishvel[i] = pml.forward[i]*fmove + pml.right[i]*smove;
	wishvel[2] += pm->cmd.upmove;

	VectorCopy (wishvel, wishdir);
	wishspeed = VectorNormalize(wishdir);
	wishspeed *= scale;

	PM_Accelerate( wishdir, wishspeed, pm_accelerate );

	// move
	VectorMA (pm->ps->origin, pml.frametime, pm->ps->velocity, pm->ps->origin);
}

//============================================================================

/*
================
PM_FootstepForSurface

Returns an event number appropriate for the groundsurface
================
*/
static int PM_FootstepForSurface( void ) {
	if ( pml.groundTrace.surfaceFlags & SURF_NOSTEPS ) {
		return 0;
	}
	if ( pml.groundTrace.surfaceFlags & SURF_METALSTEPS ) {
		return EV_FOOTSTEP_METAL;
	}
	return EV_FOOTSTEP;
}


/*
=================
PM_CrashLand

Check for hard landings that generate sound events
=================
*/
static void PM_CrashLand( void ) {
	float		delta;
	float		dist;
	float		vel, acc;
	float		t;
	float		a, b, c, den;

	// decide which landing animation to use
	if ( pm->ps->pm_flags & PMF_BACKWARDS_JUMP ) {
		PM_ForceLegsAnim( LEGS_LANDB );
	} else {
		PM_ForceLegsAnim( LEGS_LAND );
	}

	pm->ps->legsTimer = TIMER_LAND;

	// calculate the exact velocity on landing
	dist = pm->ps->origin[2] - pml.previous_origin[2];
	vel = pml.previous_velocity[2];
	acc = -pm->ps->gravity;

	a = acc / 2;
	b = vel;
	c = -dist;

	den =  b * b - 4 * a * c;
	if ( den < 0 ) {
		return;
	}
	t = (-b - sqrt( den ) ) / ( 2 * a );

	delta = vel + t * acc;
	delta = delta*delta * 0.0001;

	// ducking while falling doubles damage
	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		delta *= 2;
	}

	// never take falling damage if completely underwater
	if ( pm->waterlevel == 3 ) {
		return;
	}

	// reduce falling damage if there is standing water
	if ( pm->waterlevel == 2 ) {
		delta *= 0.25;
	}
	if ( pm->waterlevel == 1 ) {
		delta *= 0.5;
	}

	if ( delta < 1 ) {
		return;
	}

	// create a local entity event to play the sound

	// SURF_NODAMAGE is used for bounce pads where you don't ever
	// want to take damage or play a crunch sound
	if ( !(pml.groundTrace.surfaceFlags & SURF_NODAMAGE) )  {
		if ( delta > 60 ) {
			PM_AddEvent( EV_FALL_FAR );
		} else if ( delta > 40 ) {
			// this is a pain grunt, so don't play it if dead
			if ( pm->ps->stats[STAT_HEALTH] > 0 ) {
				PM_AddEvent( EV_FALL_MEDIUM );
			}
		} else if ( delta > 7 ) {
			PM_AddEvent( EV_FALL_SHORT );
		} else {
			PM_AddEvent( PM_FootstepForSurface() );
		}
	}

	// start footstep cycle over
	pm->ps->bobCycle = 0;
}

/*
=============
PM_CheckStuck
=============
*/
/*
void PM_CheckStuck(void) {
	trace_t trace;

	pm->trace (&trace, pm->ps->origin, pm->ps->mins, pm->ps->maxs, pm->ps->origin, pm->ps->playerNum, pm->tracemask);
	if (trace.allsolid) {
		//int shit = qtrue;
	}
}
*/

/*
=============
PM_CorrectAllSolid
=============
*/
static int PM_CorrectAllSolid( trace_t *trace ) {
	int			i, j, k;
	vec3_t		point;

	if ( pm->debugLevel ) {
		Com_Printf("%i:allsolid\n", c_pmove);
	}

	// jitter around
	for (i = -1; i <= 1; i++) {
		for (j = -1; j <= 1; j++) {
			for (k = -1; k <= 1; k++) {
				VectorCopy(pm->ps->origin, point);
				point[0] += (float) i;
				point[1] += (float) j;
				point[2] += (float) k;
				pm->trace (trace, point, pm->ps->mins, pm->ps->maxs, point, pm->ps->playerNum, pm->tracemask);
				if ( !trace->allsolid ) {
					point[0] = pm->ps->origin[0];
					point[1] = pm->ps->origin[1];
					point[2] = pm->ps->origin[2] - 0.25;

					pm->trace (trace, pm->ps->origin, pm->ps->mins, pm->ps->maxs, point, pm->ps->playerNum, pm->tracemask);
					pml.groundTrace = *trace;
					return qtrue;
				}
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;

	return qfalse;
}


/*
=============
PM_GroundTraceMissed

The ground trace didn't hit a surface, so we are in freefall
=============
*/
static void PM_GroundTraceMissed( void ) {
	trace_t		trace;
	vec3_t		point;

	if ( pm->ps->groundEntityNum != ENTITYNUM_NONE ) {
		// we just transitioned into freefall
		if ( pm->debugLevel ) {
			Com_Printf("%i:lift\n", c_pmove);
		}

		// if they aren't in a jumping animation and the ground is a ways away, force into it
		// if we didn't do the trace, the player would be backflipping down staircases
		VectorCopy( pm->ps->origin, point );
		point[2] -= 64;

		pm->trace (&trace, pm->ps->origin, pm->ps->mins, pm->ps->maxs, point, pm->ps->playerNum, pm->tracemask);
		if ( trace.fraction == 1.0 ) {
			if ( pm->cmd.forwardmove >= 0 ) {
				PM_ForceLegsAnim( LEGS_JUMP );
				pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
			} else {
				PM_ForceLegsAnim( LEGS_JUMPB );
				pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
			}
		}
	}

	pm->ps->groundEntityNum = ENTITYNUM_NONE;
	pml.groundPlane = qfalse;
	pml.walking = qfalse;
}


/*
=============
PM_GroundTrace
=============
*/
static void PM_GroundTrace( void ) {
	vec3_t		point;
	trace_t		trace;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] - 0.25;

	pm->trace (&trace, pm->ps->origin, pm->ps->mins, pm->ps->maxs, point, pm->ps->playerNum, pm->tracemask);
	pml.groundTrace = trace;

	// do something corrective if the trace starts in a solid...
	if ( trace.allsolid ) {
		if ( !PM_CorrectAllSolid(&trace) )
			return;
	}

	// if the trace didn't hit anything, we are in free fall
	if ( trace.fraction == 1.0 ) {
		PM_GroundTraceMissed();
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}

	// check if getting thrown off the ground
	if ( pm->ps->velocity[2] > 0 && DotProduct( pm->ps->velocity, trace.plane.normal ) > 10 ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:kickoff\n", c_pmove);
		}
		// go into jump animation
		if ( pm->cmd.forwardmove >= 0 ) {
			PM_ForceLegsAnim( LEGS_JUMP );
			pm->ps->pm_flags &= ~PMF_BACKWARDS_JUMP;
		} else {
			PM_ForceLegsAnim( LEGS_JUMPB );
			pm->ps->pm_flags |= PMF_BACKWARDS_JUMP;
		}

		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qfalse;
		pml.walking = qfalse;
		return;
	}
	
	// slopes that are too steep will not be considered onground
	if ( trace.plane.normal[2] < MIN_WALK_NORMAL ) {
		if ( pm->debugLevel ) {
			Com_Printf("%i:steep\n", c_pmove);
		}
		// FIXME: if they can't slide down the slope, let them
		// walk (sharp crevices)
		pm->ps->groundEntityNum = ENTITYNUM_NONE;
		pml.groundPlane = qtrue;
		pml.walking = qfalse;
		return;
	}

	pml.groundPlane = qtrue;
	pml.walking = qtrue;

	// hitting solid ground will end a waterjump
	if (pm->ps->pm_flags & PMF_TIME_WATERJUMP)
	{
		pm->ps->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND);
		pm->ps->pm_time = 0;
	}

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {
		// just hit the ground
		if ( pm->debugLevel ) {
			Com_Printf("%i:Land\n", c_pmove);
		}
		
		PM_CrashLand();

		// don't do landing time if we were just going down a slope
		if ( pml.previous_velocity[2] < -200 ) {
			// don't allow another jump for a little while
			pm->ps->pm_flags |= PMF_TIME_LAND;
			pm->ps->pm_time = 250;
		}
	}

	pm->ps->groundEntityNum = trace.entityNum;

	// don't reset the z velocity for slopes
//	pm->ps->velocity[2] = 0;

	PM_AddTouchEnt( trace.entityNum );
}


/*
=============
PM_SetWaterLevel	FIXME: avoid this twice?  certainly if not moving
=============
*/
static void PM_SetWaterLevel( void ) {
	vec3_t		point;
	int			cont;
	int			sample1;
	int			sample2;

	//
	// get waterlevel, accounting for ducking
	//
	pm->waterlevel = 0;
	pm->watertype = 0;

	point[0] = pm->ps->origin[0];
	point[1] = pm->ps->origin[1];
	point[2] = pm->ps->origin[2] + MINS_Z + 1;	
	cont = pm->pointcontents( point, pm->ps->playerNum );

	if ( cont & MASK_WATER ) {
		sample2 = pm->ps->viewheight - MINS_Z;
		sample1 = sample2 / 2;

		pm->watertype = cont;
		pm->waterlevel = 1;
		point[2] = pm->ps->origin[2] + MINS_Z + sample1;
		cont = pm->pointcontents (point, pm->ps->playerNum );
		if ( cont & MASK_WATER ) {
			pm->waterlevel = 2;
			point[2] = pm->ps->origin[2] + MINS_Z + sample2;
			cont = pm->pointcontents (point, pm->ps->playerNum );
			if ( cont & MASK_WATER ){
				pm->waterlevel = 3;
			}
		}
	}

}

/*
==============
PM_CheckDuck

Sets mins, maxs, and pm->ps->viewheight
==============
*/
static void PM_CheckDuck (void)
{
	trace_t	trace;

	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		if ( pm->ps->pm_flags & PMF_INVULEXPAND ) {
			// invulnerability sphere has a 42 units radius
			VectorSet( pm->ps->mins, -42, -42, -42 );
			VectorSet( pm->ps->maxs, 42, 42, 42 );
		}
		else {
			VectorSet( pm->ps->mins, -15, -15, MINS_Z );
			VectorSet( pm->ps->maxs, 15, 15, 16 );
		}
		pm->ps->pm_flags |= PMF_DUCKED;
		pm->ps->viewheight = CROUCH_VIEWHEIGHT;
		return;
	}
	pm->ps->pm_flags &= ~PMF_INVULEXPAND;

	// the box is the player model's (Doom 3's actors have theirs)
	BG_PlayerBox( pm->playerModel, qfalse, pm->ps->mins, pm->ps->maxs, NULL );

	if (pm->ps->pm_type == PM_DEAD)
	{
		pm->ps->maxs[2] = -8;
		pm->ps->viewheight = DEAD_VIEWHEIGHT;
		return;
	}

	if (pm->cmd.upmove < 0)
	{	// duck
		pm->ps->pm_flags |= PMF_DUCKED;
	}
	else
	{	// stand up if possible
		if (pm->ps->pm_flags & PMF_DUCKED)
		{
			// try to stand up
			pm->trace (&trace, pm->ps->origin, pm->ps->mins, pm->ps->maxs, pm->ps->origin, pm->ps->playerNum, pm->tracemask );
			if (!trace.allsolid)
				pm->ps->pm_flags &= ~PMF_DUCKED;
		}
	}

	BG_PlayerBox( pm->playerModel, ( pm->ps->pm_flags & PMF_DUCKED ) != 0, pm->ps->mins, pm->ps->maxs, &pm->ps->viewheight );
}



//===================================================================


/*
===============
PM_Footsteps
===============
*/
static void PM_Footsteps( void ) {
	float		bobmove;
	int			old;
	qboolean	footstep;

	//
	// calculate speed and cycle to be used for
	// all cyclic walking effects
	//
	pm->xyspeed = sqrt( pm->ps->velocity[0] * pm->ps->velocity[0]
		+  pm->ps->velocity[1] * pm->ps->velocity[1] );

	// on a ladder, unless standing at its foot: climbing (its frames go by
	// height, bg_ik.c), up or down, holding on as it was
	if ( pml.ladder && !( pml.walking && fabs( pm->ps->velocity[2] ) < 1 ) ) {
		if ( pm->ps->velocity[2] < -1 ) {
			PM_ContinueLegsAnim( LEGS_CLIMBDOWN );
		} else if ( pm->ps->velocity[2] > 1 || ( pm->ps->legsAnim & ~ANIM_TOGGLEBIT ) != LEGS_CLIMBDOWN ) {
			PM_ContinueLegsAnim( LEGS_CLIMB );
		}
		return;
	}

	if ( pm->ps->groundEntityNum == ENTITYNUM_NONE ) {

		if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
			PM_ContinueLegsAnim( LEGS_IDLECR );
		}
		// airborne leaves position in cycle intact, but doesn't advance
		if ( pm->waterlevel > 1 ) {
			PM_ContinueLegsAnim( LEGS_SWIM );
		}
		return;
	}

	// under, even on the bottom: swimming
	if ( PM_Swimming() ) {
		PM_ContinueLegsAnim( LEGS_SWIM );
		return;
	}

	// if not trying to move
	if ( !pm->cmd.forwardmove && !pm->cmd.rightmove ) {
		if (  pm->xyspeed < 5 ) {
			pm->ps->bobCycle = 0;	// start at beginning of cycle again
			if ( pm->ps->pm_flags & PMF_DUCKED ) {
				PM_ContinueLegsAnim( LEGS_IDLECR );
			} else {
				PM_ContinueLegsAnim( LEGS_IDLE );
			}
		}
		return;
	}
	

	footstep = qfalse;

	if ( pm->ps->pm_flags & PMF_DUCKED ) {
		bobmove = 0.5;	// ducked characters bob much faster
		if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
			PM_ContinueLegsAnim( LEGS_BACKCR );
		}
		else {
			PM_ContinueLegsAnim( LEGS_WALKCR );
		}
		// ducked characters never play footsteps
	/*
	} else 	if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) ) {
			bobmove = 0.4;	// faster speeds bob faster
			footstep = qtrue;
		} else {
			bobmove = 0.3;
		}
		PM_ContinueLegsAnim( LEGS_BACK );
	*/
	} else {
		// wading (waist deep, on the bottom) is walking, at walking's pace
		if ( !( pm->cmd.buttons & BUTTON_WALKING ) && pm->waterlevel < 2 ) {
			bobmove = 0.4f;	// faster speeds bob faster
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_RUN );
			}
			footstep = qtrue;
		} else {
			bobmove = 0.3f;	// walking bobs slow
			if ( pm->ps->pm_flags & PMF_BACKWARDS_RUN ) {
				PM_ContinueLegsAnim( LEGS_BACKWALK );
			}
			else {
				PM_ContinueLegsAnim( LEGS_WALK );
			}
		}
	}

	// check for footstep / splash sounds
	old = pm->ps->bobCycle;
	pm->ps->bobCycle = (int)( old + bobmove * pml.msec ) & 255;

	// if we just crossed a cycle boundary, play an appropriate footstep event
	if ( ( ( old + 64 ) ^ ( pm->ps->bobCycle + 64 ) ) & 128 ) {
		if ( pm->waterlevel == 0 ) {
			// on ground will only play sounds if running
			if ( footstep && !pm->noFootsteps ) {
				PM_AddEvent( PM_FootstepForSurface() );
			}
		} else if ( pm->waterlevel == 1 ) {
			// splashing
			PM_AddEvent( EV_FOOTSPLASH );
		} else if ( pm->waterlevel == 2 ) {
			// wading / swimming at surface
			PM_AddEvent( EV_SWIM );
		} else if ( pm->waterlevel == 3 ) {
			// no sound when completely underwater

		}
	}
}

/*
==============
PM_WaterEvents

Generate sound events for entering and leaving water
==============
*/
static void PM_WaterEvents( void ) {		// FIXME?
	//
	// if just entered a water volume, play a sound
	//
	if (!pml.previous_waterlevel && pm->waterlevel) {
		PM_AddEvent( EV_WATER_TOUCH );
	}

	//
	// if just completely exited a water volume, play a sound
	//
	if (pml.previous_waterlevel && !pm->waterlevel) {
		PM_AddEvent( EV_WATER_LEAVE );
	}

	//
	// check for head just going under water
	//
	if (pml.previous_waterlevel != 3 && pm->waterlevel == 3) {
		PM_AddEvent( EV_WATER_UNDER );
	}

	//
	// check for head just coming out of water
	//
	if (pml.previous_waterlevel == 3 && pm->waterlevel != 3) {
		PM_AddEvent( EV_WATER_CLEAR );
	}
}


/*
===============
PM_BeginWeaponChange
===============
*/
static void PM_BeginWeaponChange( int weapon ) {
	if ( !BG_ValidWeapon( weapon ) ) {
		return;
	}

	if ( !BG_HasWeapon( pm->ps, weapon ) ) {
		return;
	}
	
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		return;
	}

	PM_AddEvent( EV_CHANGE_WEAPON );
	pm->ps->weaponstate = WEAPON_DROPPING;
	pm->ps->weaponTime += BG_Weapon( pm->ps->weapon )->lowerTime;
	PM_StartWeaponAnim( WP_ANIM_DISARM );
	PM_StartTorsoAnim( TORSO_DROP );
}


/*
===============
PM_FinishWeaponChange
===============
*/
static void PM_FinishWeaponChange( void ) {
	int		weapon;

	BG_DecomposeUserCmdValue( pm->cmd.stateValue, &weapon );
	if ( !BG_ValidWeapon( weapon ) ) {
		weapon = WP_NONE;
	}

	if ( !BG_HasWeapon( pm->ps, weapon ) ) {
		weapon = WP_NONE;
	}

	pm->ps->weapon = weapon;
	pm->ps->weaponstate = WEAPON_RAISING;
	pm->ps->weaponTime += BG_Weapon( weapon )->raiseTime;
	PM_StartWeaponAnim( WP_ANIM_ACTIVATE );
	PM_StartTorsoAnim( TORSO_RAISE );
}


/*
==============
PM_TorsoAnimation

==============
*/
static void PM_TorsoAnimation( void ) {
	if ( pm->ps->weaponstate == WEAPON_READY ) {
		if ( BG_Weapon( pm->ps->weapon )->melee ) {
			PM_ContinueTorsoAnim( TORSO_STAND2 );
		} else {
			PM_ContinueTorsoAnim( TORSO_STAND );
		}
		PM_ContinueWeaponAnim( WP_ANIM_IDLE );
		return;
	}
}


/*
==============
PM_WeaponAddTime

A shot's time, for haste and the like.
==============
*/
static int PM_WeaponAddTime( int addTime ) {
#ifdef MISSIONPACK
	if( BG_ItemForItemNum( pm->ps->stats[STAT_PERSISTANT_POWERUP] )->giTag == PW_SCOUT ) {
		addTime /= 1.5;
	}
	else
	if( BG_ItemForItemNum( pm->ps->stats[STAT_PERSISTANT_POWERUP] )->giTag == PW_AMMOREGEN ) {
		addTime /= 1.3;
	}
	else
#endif
	if ( pm->ps->powerups[PW_HASTE] ) {
		addTime /= 1.3;
	}
	return addTime;
}

/*
==============
PM_TakeAmmo

A shot's ammo, from the clip too if the weapon has one.
==============
*/
static void PM_TakeAmmo( const bgWeapon_t *weapon ) {
	if ( weapon->ammoRequired <= 0 ) {
		return;
	}
	if ( pm->ps->ammo[ pm->ps->weapon ] != -1 ) {
		pm->ps->ammo[ pm->ps->weapon ] -= weapon->ammoRequired;
	}
	if ( weapon->clipSize > 0 ) {
		pm->ps->ammoClip[ pm->ps->weapon ] -= weapon->ammoRequired;
		if ( pm->ps->ammoClip[ pm->ps->weapon ] < 0 ) {
			pm->ps->ammoClip[ pm->ps->weapon ] = 0;
		}
	}
}

/*
==============
PM_CanReload

Whether the weapon's clip can take more of the ammo carried.
==============
*/
static qboolean PM_CanReload( void ) {
	const bgWeapon_t *weapon = BG_Weapon( pm->ps->weapon );
	int w = pm->ps->weapon;

	if ( !BG_ValidWeapon( w ) || weapon->clipSize <= 0 || pm->ps->ammoClip[w] >= weapon->clipSize ) {
		return qfalse;
	}
	return pm->ps->ammo[w] == -1 || pm->ps->ammo[w] > pm->ps->ammoClip[w];
}

/*
==============
PM_BeginReload

Doom 3's reload: the reload animation and sound, the clip filled when it's
over ("reloadRate"). Changing weapons cancels it.
==============
*/
static void PM_BeginReload( void ) {
	pm->ps->weaponstate = WEAPON_RELOADING;
	pm->ps->weaponTime = BG_Weapon( pm->ps->weapon )->reloadTime;
	PM_StartWeaponAnim( WP_ANIM_RELOAD );
	PM_StartTorsoAnim( TORSO_RAISE );
	PM_AddEvent( EV_RELOAD_WEAPON );
}

/*
==============
PM_FireBurstShot

The next shot of a burst ("burstCount"), until the burst or the ammo is out.
==============
*/
static void PM_FireBurstShot( void ) {
	const bgWeapon_t *weapon = BG_Weapon( pm->ps->weapon );
	int addTime;

	if ( !BG_WeaponHasAmmo( pm->ps, pm->ps->weapon ) || !BG_WeaponClipLoaded( pm->ps, pm->ps->weapon ) ) {
		pm->ps->weaponBurst = 0;
		pm->ps->weaponTime += PM_WeaponAddTime( weapon->fireTime - ( weapon->burstCount - 1 ) * weapon->burstTime );
		return;
	}
	PM_TakeAmmo( weapon );
	PM_AddEvent( EV_FIRE_WEAPON );
	pm->ps->weaponstate = WEAPON_FIRING;

	pm->ps->weaponBurst--;
	if ( pm->ps->weaponBurst > 0 ) {
		addTime = weapon->burstTime;
	} else {
		// the burst's whole time is "fireRate"
		addTime = weapon->fireTime - ( weapon->burstCount - 1 ) * weapon->burstTime;
	}
	pm->ps->weaponTime += PM_WeaponAddTime( addTime );
}

/*
==============
PM_Weapon

Generates weapon events and modifes the weapon counter
==============
*/
static void PM_Weapon( void ) {
	const bgWeapon_t	*weapon;
	int		addTime;
	int		newWeapon;

	// don't allow attack until all buttons are up
	if ( pm->ps->pm_flags & PMF_RESPAWNED ) {
		return;
	}

	// ignore if spectator
	if ( pm->ps->persistant[PERS_TEAM] == TEAM_SPECTATOR ) {
		return;
	}

	// check for dead player
	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->ps->weapon = WP_NONE;
		pm->ps->weaponBurst = 0;
		return;
	}

	// the hands are on the ladder: no shooting (the cgame puts the weapon
	// away)
	if ( pml.ladder ) {
		pm->ps->weaponBurst = 0;
		return;
	}

	// check for item using
	if ( pm->cmd.buttons & BUTTON_USE_HOLDABLE ) {
		if ( ! ( pm->ps->pm_flags & PMF_USE_ITEM_HELD ) ) {
			if ( BG_ItemForItemNum( pm->ps->stats[STAT_HOLDABLE_ITEM] )->giTag == HI_MEDKIT
				&& pm->ps->stats[STAT_HEALTH] >= (pm->ps->stats[STAT_MAX_HEALTH] + 25) ) {
				// don't use medkit if at max health
			} else {
				pm->ps->pm_flags |= PMF_USE_ITEM_HELD;
				PM_AddEvent( EV_USE_ITEM0 + BG_ItemForItemNum( pm->ps->stats[STAT_HOLDABLE_ITEM] )->giTag );
				pm->ps->stats[STAT_HOLDABLE_ITEM] = 0;
			}
			return;
		}
	} else {
		pm->ps->pm_flags &= ~PMF_USE_ITEM_HELD;
	}


	// make weapon function
	if ( pm->ps->weaponTime > 0 ) {
		pm->ps->weaponTime -= pml.msec;
	}

	// the rest of a burst fires whatever the buttons
	if ( pm->ps->weaponBurst > 0 ) {
		if ( pm->ps->weaponTime > 0 ) {
			return;
		}
		PM_FireBurstShot();
		return;
	}

	// check for weapon change
	// can't change if weapon is firing, but can change
	// again if lowering or raising
	if ( pm->ps->weaponTime <= 0 || pm->ps->weaponstate != WEAPON_FIRING ) {
		BG_DecomposeUserCmdValue( pm->cmd.stateValue, &newWeapon );
		if ( pm->ps->weapon != newWeapon ) {
			PM_BeginWeaponChange( newWeapon );
		} else {
			//Elder: temp hack
			if (pm->ps->weaponstate == WEAPON_READY) {
				PM_ContinueWeaponAnim(WP_ANIM_IDLE);
			}
		}
	}

	if ( pm->ps->weaponTime > 0 ) {
		return;
	}

	// change weapon if time
	if ( pm->ps->weaponstate == WEAPON_DROPPING ) {
		PM_FinishWeaponChange();
		return;
	}

	// the reload's over: the clip's full
	if ( pm->ps->weaponstate == WEAPON_RELOADING ) {
		BG_FillClip( pm->ps, pm->ps->weapon );
		pm->ps->weaponstate = WEAPON_READY;
		PM_StartWeaponAnim( WP_ANIM_IDLE );
		return;
	}

	// an empty clip reloads by itself (Doom 3's auto reload), or on "+reload"
	if ( PM_CanReload() && ( !BG_WeaponClipLoaded( pm->ps, pm->ps->weapon )
							 || ( ( pm->cmd.buttons & BUTTON_RELOAD ) && pm->ps->weaponstate != WEAPON_RAISING ) ) ) {
		PM_BeginReload();
		return;
	}

	if ( pm->ps->weaponstate == WEAPON_RAISING ) {
		pm->ps->weaponstate = WEAPON_READY;
		if ( BG_Weapon( pm->ps->weapon )->melee ) {
			PM_StartTorsoAnim( TORSO_STAND2 );
		} else {
			PM_StartTorsoAnim( TORSO_STAND );
		}
		// temp hack
		PM_StartWeaponAnim( WP_ANIM_IDLE );
		return;
	}

	// check for alt fire
	if ( ( pm->cmd.buttons & BUTTON_ATTACK2 ) && BG_Weapon( pm->ps->weapon )->altFire != ALTFIRE_NONE ) {
		PM_StartWeaponAnim( WP_ANIM_EXTRA1 );
		PM_AddEvent( EV_ALT_FIRE_WEAPON );
		pm->ps->weaponstate = WEAPON_FIRING;
		pm->ps->weaponTime += BG_Weapon( pm->ps->weapon )->altFireTime;
		return;
	}

	// check for fire
	if ( ! (pm->cmd.buttons & BUTTON_ATTACK) ) {
		pm->ps->weaponTime = 0;
		pm->ps->weaponstate = WEAPON_READY;
		return;
	}

	weapon = BG_Weapon( pm->ps->weapon );

	// start the animation even if out of ammo
	if ( weapon->melee ) {
		PM_StartTorsoAnim( TORSO_ATTACK2 );
	} else {
		PM_StartTorsoAnim( TORSO_ATTACK );
	}
	PM_StartWeaponAnim( WP_ANIM_FIRE );

	pm->ps->weaponstate = WEAPON_FIRING;

	// check for out of ammo
	if ( !BG_WeaponHasAmmo( pm->ps, pm->ps->weapon ) ) {
		PM_AddEvent( EV_NOAMMO );
		pm->ps->weaponTime += 500;
		return;
	}

	// take the ammo away if not infinite
	PM_TakeAmmo( weapon );

	// fire weapon
	PM_AddEvent( EV_FIRE_WEAPON );

	addTime = weapon->fireTime;
	if ( weapon->burstCount > 1 ) {
		// the rest of the burst follows, the last shot then waits out the rest
		pm->ps->weaponBurst = weapon->burstCount - 1;
		addTime = weapon->burstTime;
	}

	addTime = PM_WeaponAddTime( addTime );

	pm->ps->weaponTime += addTime;
}

/*
================
PM_Animate
================
*/

static void PM_Animate( void ) {
	if ( pm->cmd.buttons & BUTTON_GESTURE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GESTURE );
			pm->ps->torsoTimer = TIMER_GESTURE;
			PM_AddEvent( EV_TAUNT );
		}
#ifdef MISSIONPACK
	} else if ( pm->cmd.buttons & BUTTON_GETFLAG ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GETFLAG );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_GUARDBASE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_GUARDBASE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_PATROL ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_PATROL );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_FOLLOWME ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_FOLLOWME );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_AFFIRMATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_AFFIRMATIVE);
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
	} else if ( pm->cmd.buttons & BUTTON_NEGATIVE ) {
		if ( pm->ps->torsoTimer == 0 ) {
			PM_StartTorsoAnim( TORSO_NEGATIVE );
			pm->ps->torsoTimer = 600;	//TIMER_GESTURE;
		}
#endif
	}
}


/*
================
PM_DropTimers
================
*/
static void PM_DropTimers( void ) {
	// drop misc timing counter
	if ( pm->ps->pm_time ) {
		if ( pml.msec >= pm->ps->pm_time ) {
			pm->ps->pm_flags &= ~PMF_ALL_TIMES;
			pm->ps->pm_time = 0;
		} else {
			pm->ps->pm_time -= pml.msec;
		}
	}

	// drop animation counter
	if ( pm->ps->legsTimer > 0 ) {
		pm->ps->legsTimer -= pml.msec;
		if ( pm->ps->legsTimer < 0 ) {
			pm->ps->legsTimer = 0;
		}
	}

	if ( pm->ps->torsoTimer > 0 ) {
		pm->ps->torsoTimer -= pml.msec;
		if ( pm->ps->torsoTimer < 0 ) {
			pm->ps->torsoTimer = 0;
		}
	}

	// first person weapon counter
	if ( pm->ps->weapAnimTimer > 0 ) {
		pm->ps->weapAnimTimer -= pml.msec;
		if ( pm->ps->weapAnimTimer < 0 ) {
			pm->ps->weapAnimTimer = 0;
		}
	}
}

/*
================
PM_UpdateViewAngles

This can be used as another entry point when only the viewangles
are being updated instead of a full move
================
*/
void PM_UpdateViewAngles( playerState_t *ps, const usercmd_t *cmd ) {
	short		temp;
	int		i;

	if ( ps->pm_type == PM_INTERMISSION || ps->pm_type == PM_SPINTERMISSION) {
		return;		// no view changes at all
	}

	if ( ps->pm_type != PM_SPECTATOR && ps->stats[STAT_HEALTH] <= 0 ) {
		return;		// no view changes at all
	}

	// circularly clamp the angles with deltas
	for (i=0 ; i<3 ; i++) {
		temp = cmd->angles[i] + ps->delta_angles[i];
		if ( i == PITCH ) {
			// don't let the player look up or down more than 90 degrees
			if ( temp > 16000 ) {
				ps->delta_angles[i] = 16000 - cmd->angles[i];
				temp = 16000;
			} else if ( temp < -16000 ) {
				ps->delta_angles[i] = -16000 - cmd->angles[i];
				temp = -16000;
			}
		}
		ps->viewangles[i] = SHORT2ANGLE(temp);
	}

}


/*
================
PmoveSingle

================
*/
void PmoveSingle (pmove_t *pmove) {
	pm = pmove;

	// this counter lets us debug movement problems with a journal
	// by setting a conditional breakpoint fot the previous frame
	c_pmove++;

	// clear results
	pm->numtouch = 0;
	pm->watertype = 0;
	pm->waterlevel = 0;

	if ( pm->ps->stats[STAT_HEALTH] <= 0 ) {
		pm->tracemask &= ~CONTENTS_BODY;	// corpses can fly through bodies
	}

	// make sure walking button is clear if they are running, to avoid
	// proxy no-footsteps cheats
	if ( abs( pm->cmd.forwardmove ) > 64 || abs( pm->cmd.rightmove ) > 64 ) {
		pm->cmd.buttons &= ~BUTTON_WALKING;
	}

	// set the talk balloon flag
	if ( pm->cmd.buttons & BUTTON_TALK ) {
		pm->ps->eFlags |= EF_TALK;
	} else {
		pm->ps->eFlags &= ~EF_TALK;
	}

	// set the firing flag for continuous beam weapons
	if ( !(pm->ps->pm_flags & PMF_RESPAWNED) && pm->ps->pm_type != PM_INTERMISSION && pm->ps->pm_type != PM_NOCLIP
		&& ( pm->cmd.buttons & BUTTON_ATTACK ) && BG_WeaponHasAmmo( pm->ps, pm->ps->weapon ) ) {
		pm->ps->eFlags |= EF_FIRING;
	} else {
		pm->ps->eFlags &= ~EF_FIRING;
	}

	// clear the respawned flag if attack and use are cleared
	if ( pm->ps->stats[STAT_HEALTH] > 0 && 
		!( pm->cmd.buttons & (BUTTON_ATTACK | BUTTON_USE_HOLDABLE) ) ) {
		pm->ps->pm_flags &= ~PMF_RESPAWNED;
	}

	// if talk button is down, dissallow all other input
	// this is to prevent any possible intercept proxy from
	// adding fake talk balloons
	if ( pmove->cmd.buttons & BUTTON_TALK ) {
		// keep the talk button set tho for when the cmd.serverTime > 66 msec
		// and the same cmd is used multiple times in Pmove
		pmove->cmd.buttons = BUTTON_TALK;
		pmove->cmd.forwardmove = 0;
		pmove->cmd.rightmove = 0;
		pmove->cmd.upmove = 0;
	}

	// clear all pmove local vars
	memset (&pml, 0, sizeof(pml));

	// determine the time
	pml.msec = pmove->cmd.serverTime - pm->ps->commandTime;
	if ( pml.msec < 1 ) {
		pml.msec = 1;
	} else if ( pml.msec > 200 ) {
		pml.msec = 200;
	}
	pm->ps->commandTime = pmove->cmd.serverTime;

	// save old org in case we get stuck
	VectorCopy (pm->ps->origin, pml.previous_origin);

	// save old velocity for crashlanding
	VectorCopy (pm->ps->velocity, pml.previous_velocity);

	pml.frametime = pml.msec * 0.001;

	// update the viewangles
	PM_UpdateViewAngles( pm->ps, &pm->cmd );

	AngleVectors (pm->ps->viewangles, pml.forward, pml.right, pml.up);

	if ( pm->cmd.upmove < 10 ) {
		// not holding jump
		pm->ps->pm_flags &= ~PMF_JUMP_HELD;
	}

	// decide if backpedaling animations should be used
	if ( pm->cmd.forwardmove < 0 ) {
		pm->ps->pm_flags |= PMF_BACKWARDS_RUN;
	} else if ( pm->cmd.forwardmove > 0 || ( pm->cmd.forwardmove == 0 && pm->cmd.rightmove ) ) {
		pm->ps->pm_flags &= ~PMF_BACKWARDS_RUN;
	}

	if ( pm->ps->pm_type >= PM_DEAD ) {
		pm->cmd.forwardmove = 0;
		pm->cmd.rightmove = 0;
		pm->cmd.upmove = 0;
	}

	if ( pm->ps->pm_type == PM_SPECTATOR ) {
		PM_CheckDuck ();
		PM_FlyMove ();
		PM_DropTimers ();
		return;
	}

	if ( pm->ps->pm_type == PM_NOCLIP ) {
		PM_NoclipMove ();
		PM_DropTimers ();
		return;
	}

	if (pm->ps->pm_type == PM_FREEZE) {
		return;		// no movement at all
	}

	if ( pm->ps->pm_type == PM_INTERMISSION || pm->ps->pm_type == PM_SPINTERMISSION) {
		return;		// no movement at all
	}

	// set watertype, and waterlevel
	PM_SetWaterLevel();
	pml.previous_waterlevel = pmove->waterlevel;

	// set mins, maxs, and viewheight
	PM_CheckDuck ();

	// set groundentity
	PM_GroundTrace();

	if ( pm->ps->pm_type == PM_DEAD ) {
		PM_DeadMove ();
	}

	PM_DropTimers();
	CheckLadder();
	PM_LadderView();

#ifdef MISSIONPACK
	if ( pm->ps->powerups[PW_INVULNERABILITY] ) {
		PM_InvulnerabilityMove();
	} else
#endif
	if ( pm->ps->pm_flags & PMF_GRAPPLE_PULL ) {
		PM_GrappleMove();
		// We can wiggle a bit
		PM_AirMove();
	} else if ( pm->ps->powerups[PW_FLIGHT] ) {
		// flight powerup doesn't allow jump and has different friction
		PM_FlyMove();
	} else if (pm->ps->pm_flags & PMF_TIME_WATERJUMP) {
		PM_WaterJumpMove();
	} else if ( pm->ps->pm_flags & PMF_LADDER_MOVE ) {
		// onto a ladder or over its top
		PM_LadderMoving();
	} else if ( pml.ladder ) {
		// on a ladder, even one standing in water (Daikatana's are)
		PM_LadderMove();
	} else if ( PM_Swimming() ) {
		// swimming
		PM_WaterMove();
	} else if ( pml.walking ) {
		// walking on ground
		PM_WalkMove();
	} else {
		// airborne
		PM_AirMove();
	}

	PM_Animate();

	// set groundentity, watertype, and waterlevel
	PM_GroundTrace();
	PM_SetWaterLevel();

	// weapons
	PM_Weapon();

	// torso animation
	PM_TorsoAnimation();

	// footstep events / legs animations
	PM_Footsteps();

	// entering / leaving water splashes
	PM_WaterEvents();

	// snap some parts of playerstate to save network bandwidth
	trap_SnapVector( pm->ps->velocity );
}


/*
================
Pmove

Can be called by either the server or the client
================
*/
void Pmove (pmove_t *pmove) {
	int			finalTime;

	finalTime = pmove->cmd.serverTime;

	if ( finalTime < pmove->ps->commandTime ) {
		return;	// should not happen
	}

	if ( finalTime > pmove->ps->commandTime + 1000 ) {
		pmove->ps->commandTime = finalTime - 1000;
	}

	pmove->ps->pmove_framecount = (pmove->ps->pmove_framecount+1) & ((1<<PS_PMOVEFRAMECOUNTBITS)-1);

	// chop the move up if it is too long, to prevent framerate
	// dependent behavior
	while ( pmove->ps->commandTime != finalTime ) {
		int		msec;

		msec = finalTime - pmove->ps->commandTime;

		if ( pmove->pmove_fixed ) {
			if ( msec > pmove->pmove_msec ) {
				msec = pmove->pmove_msec;
			}
		}
		else {
			if ( msec > 66 ) {
				msec = 66;
			}
		}
		pmove->cmd.serverTime = pmove->ps->commandTime + msec;
		PmoveSingle( pmove );

		if ( pmove->ps->pm_flags & PMF_JUMP_HELD ) {
			pmove->cmd.upmove = 20;
		}
	}

	//PM_CheckStuck();

}

