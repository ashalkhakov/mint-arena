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
#include "g_local.h"

#define	MISSILE_PRESTEP_TIME	50

/*
================
G_BounceMissile

================
*/
void G_BounceMissile( gentity_t *ent, trace_t *trace ) {
	vec3_t	velocity;
	float	dot;
	int		hitTime;

	// reflect the velocity on the trace plane
	hitTime = level.previousTime + ( level.time - level.previousTime ) * trace->fraction;
	BG_EvaluateTrajectoryDelta( &ent->s.pos, hitTime, velocity );
	dot = DotProduct( velocity, trace->plane.normal );
	VectorMA( velocity, -2*dot, trace->plane.normal, ent->s.pos.trDelta );
	VectorScale( ent->s.pos.trDelta, ent->physicsBounce, ent->s.pos.trDelta );

	// check for stop
	if ( ent->s.pos.trType == TR_GRAVITY ? ( trace->plane.normal[2] > 0.2 && VectorLength( ent->s.pos.trDelta ) < 40 )
		: VectorLength( ent->s.pos.trDelta ) < 1 ) {
		G_SetOrigin( ent, trace->endpos );
		ent->s.time = level.time / 4;
		return;
	}

	VectorAdd( ent->r.currentOrigin, trace->plane.normal, ent->r.currentOrigin);
	VectorCopy( ent->r.currentOrigin, ent->s.pos.trBase );
	ent->s.pos.trTime = level.time;
}


/*
================
G_ExplodeMissile

Explode a missile without an impact
================
*/
void G_ExplodeMissile( gentity_t *ent ) {
	vec3_t		dir;
	vec3_t		origin;

	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );
	SnapVector( origin );
	G_SetOrigin( ent, origin );

	// we don't have a valid direction, so just point straight up
	dir[0] = dir[1] = 0;
	dir[2] = 1;

	ent->s.eType = ET_GENERAL;
	G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( dir ) );

	ent->freeAfterEvent = qtrue;

	// splash damage
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( ent->r.currentOrigin, ent->parent, ent->splashDamage, ent->splashRadius, ent
			, ent->splashMethodOfDeath ) ) {
			g_entities[ent->r.ownerNum].player->accuracy_hits++;
		}
	}

	G_LinkEntity( ent );
}


/*
================
G_StickMissile

Stops it where it hit, facing out of the surface, until it detonates
(Daikatana's C4).
================
*/
static void G_StickMissile( gentity_t *ent, trace_t *trace ) {
	SnapVectorTowards( trace->endpos, ent->s.pos.trBase );
	G_SetOrigin( ent, trace->endpos );
	VectorClear( ent->s.pos.trDelta );
	vectoangles( trace->plane.normal, ent->s.angles );
	ent->s.angles[PITCH] += 90;
	ent->s.apos.trType = TR_STATIONARY;
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorCopy( ent->s.angles, ent->r.currentAngles );
	ent->s.groundEntityNum = trace->entityNum;
	G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
	G_LinkEntity( ent );
}

/*
================
G_MissileImpact
================
*/
void G_MissileImpact( gentity_t *ent, trace_t *trace ) {
	gentity_t		*other;
	qboolean		hitPlayer = qfalse;

	other = &g_entities[trace->entityNum];

	// check for sticking
	if ( ( ent->missileFlags & MISSILE_STICK )
		&& !( ent->missileFlags & ( other->takedamage ? MISSILE_DETONATE_ON_ACTOR : MISSILE_DETONATE_ON_WORLD ) ) ) {
		G_StickMissile( ent, trace );
		return;
	}

	// check for bounce
	if ( !( ent->missileFlags & ( other->takedamage ? MISSILE_DETONATE_ON_ACTOR : MISSILE_DETONATE_ON_WORLD ) )
		&& ( !( ent->missileFlags & MISSILE_MAX_BOUNCES ) || ent->count-- > 0 ) ) {
		G_BounceMissile( ent, trace );
		G_AddEvent( ent, EV_GRENADE_BOUNCE, 0 );
		return;
	}

	// impact damage
	if (other->takedamage) {
		// FIXME: wrong damage direction?
		if ( ent->damage ) {
			vec3_t	velocity;

			if( LogAccuracyHit( other, &g_entities[ent->r.ownerNum] ) ) {
				g_entities[ent->r.ownerNum].player->accuracy_hits++;
				hitPlayer = qtrue;
			}
			BG_EvaluateTrajectoryDelta( &ent->s.pos, level.time, velocity );
			if ( VectorLength( velocity ) == 0 ) {
				velocity[2] = 1;	// stepped on a grenade
			}
			G_Damage (other, ent, &g_entities[ent->r.ownerNum], velocity,
				ent->s.origin, ent->damage, 
				0, ent->methodOfDeath);
		}
	}

	// is it cheaper in bandwidth to just remove this ent and create a new
	// one, rather than changing the missile into the explosion?

	if ( other->takedamage && other->player ) {
		G_AddEvent( ent, EV_MISSILE_HIT, DirToByte( trace->plane.normal ) );
		ent->s.otherEntityNum = other->s.number;
	} else if( trace->surfaceFlags & SURF_METALSTEPS ) {
		G_AddEvent( ent, EV_MISSILE_MISS_METAL, DirToByte( trace->plane.normal ) );
	} else {
		G_AddEvent( ent, EV_MISSILE_MISS, DirToByte( trace->plane.normal ) );
	}

	ent->freeAfterEvent = qtrue;

	// change over to a normal entity right at the point of impact
	ent->s.eType = ET_GENERAL;

	SnapVectorTowards( trace->endpos, ent->s.pos.trBase );	// save net bandwidth

	G_SetOrigin( ent, trace->endpos );

	// splash damage (doesn't apply to person directly hit)
	if ( ent->splashDamage ) {
		if( G_RadiusDamage( trace->endpos, ent->parent, ent->splashDamage, ent->splashRadius, 
			other, ent->splashMethodOfDeath ) ) {
			if( !hitPlayer ) {
				g_entities[ent->r.ownerNum].player->accuracy_hits++;
			}
		}
	}

	G_LinkEntity( ent );
}

/*
================
G_RunMissile
================
*/
void G_RunMissile( gentity_t *ent ) {
	vec3_t		origin;
	trace_t		tr;
	int			passent;

	// stuck to something, it only waits
	if ( ent->s.pos.trType == TR_STATIONARY && ( ent->missileFlags & MISSILE_STICK ) ) {
		G_RunThink( ent );
		return;
	}

	// get current position
	BG_EvaluateTrajectory( &ent->s.pos, level.time, origin );

	// if this missile bounced off an invulnerability sphere
	if ( ent->target_ent ) {
		passent = ent->target_ent->s.number;
	} else {
		// ignore interactions with the missile owner
		passent = ent->r.ownerNum;
	}
	// trace a line from the previous position to the current position
	G_Trace( &tr, ent->r.currentOrigin, ent->s.mins, ent->s.maxs, origin, passent, ent->clipmask, TT_AABB );

	if ( tr.startsolid || tr.allsolid ) {
		// make sure the tr.entityNum is set to the entity we're stuck in
		G_Trace( &tr, ent->r.currentOrigin, ent->s.mins, ent->s.maxs, ent->r.currentOrigin, passent, ent->clipmask, TT_AABB );
		tr.fraction = 0;
	}
	else {
		VectorCopy( tr.endpos, ent->r.currentOrigin );
	}

	G_LinkEntity( ent );

	if ( tr.fraction != 1 ) {
		// never explode or bounce on sky
		if ( tr.surfaceFlags & SURF_NOIMPACT ) {
			G_FreeEntity( ent );
			return;
		}
		G_MissileImpact( ent, &tr );
		if ( ent->s.eType != ET_MISSILE ) {
			return;		// exploded
		}
	}
	// check think function after bouncing
	G_RunThink( ent );
}


//=============================================================================

/*
=================
G_DefDamage

"damage" of the damage def a projectile's key names, and its "radius".
=================
*/
static int G_DefDamage( const dict_t *projectile, const char *key, int *radius ) {
	const dict_t *damage;
	const char *name;

	if ( !DictGetString( projectile, key, "", &name ) || !name[0] ) {
		return 0;
	}
	damage = BG_FindDef( name );
	if ( !damage ) {
		G_Printf( S_COLOR_YELLOW "WARNING: %s: no damage def %s\n", DictGetStringReference( projectile, "classname", "?" ), name );
		return 0;
	}
	if ( radius ) {
		*radius = DictGetInt( damage, "radius", "0" );
	}
	return DictGetInt( damage, "damage", "0" );
}

/*
=================
fire_projectile

A projectile of the def (Doom 3's keys): "velocity" (forward, right, up),
"gravity" (non-zero falls), "bounce" (speed kept by a bounce), "fuse"
(seconds), "detonate_on_fuse", "detonate_on_world", "detonate_on_actor",
"max_bounces" (then it detonates), "stick" (stops where it hits instead), "def_damage" and "def_splash_damage"
(damage defs with "damage" and "radius"), "mins" and "maxs".
=================
*/
gentity_t *fire_projectile( gentity_t *self, const dict_t *def, int weapon, vec3_t start, vec3_t dir ) {
	gentity_t	*bolt;
	vec3_t		velocity, right, up;
	float		fuse;

	VectorNormalize (dir);

	bolt = G_Spawn();
	bolt->classname = DictGetStringReference( def, "classname", "projectile" );
	fuse = DictGetFloat( def, "fuse", "10" );
	if ( fuse <= 0 ) {
		fuse = 10;
	}
	bolt->nextthink = level.time + fuse * 1000;
	bolt->missileFlags = 0;
	if ( DictGetBool( def, "detonate_on_fuse", "0" ) ) {
		bolt->missileFlags |= MISSILE_DETONATE_ON_FUSE;
		bolt->think = G_ExplodeMissile;
	} else {
		bolt->think = G_FreeEntity;
	}
	if ( DictGetBool( def, "detonate_on_world", "1" ) ) {
		bolt->missileFlags |= MISSILE_DETONATE_ON_WORLD;
	}
	if ( DictGetBool( def, "detonate_on_actor", "1" ) ) {
		bolt->missileFlags |= MISSILE_DETONATE_ON_ACTOR;
	}
	bolt->physicsBounce = DictGetFloat( def, "bounce", "0" );
	if ( DictGetBool( def, "stick", "0" ) ) {
		bolt->missileFlags |= MISSILE_STICK;
	}
	if ( DictFindKey( def, "max_bounces" ) ) {
		bolt->missileFlags |= MISSILE_MAX_BOUNCES;
		bolt->count = DictGetInt( def, "max_bounces", "0" );
	}
	bolt->s.eType = ET_MISSILE;
	bolt->r.svFlags = SVF_USE_CURRENT_ORIGIN;
	bolt->s.weapon = weapon;
	bolt->r.ownerNum = self->s.number;
	bolt->parent = self;
	bolt->damage = G_DefDamage( def, "def_damage", NULL );
	bolt->splashDamage = G_DefDamage( def, "def_splash_damage", &bolt->splashRadius );
	bolt->methodOfDeath = MOD_FOR_WEAPON( weapon, qfalse );
	bolt->splashMethodOfDeath = MOD_FOR_WEAPON( weapon, qtrue );
	bolt->clipmask = MASK_SHOT;
	bolt->target_ent = NULL;
	DictGetVector( def, "mins", "0 0 0", bolt->s.mins );
	DictGetVector( def, "maxs", "0 0 0", bolt->s.maxs );

	bolt->s.pos.trType = DictGetFloat( def, "gravity", "0" ) ? TR_GRAVITY : TR_LINEAR;
	bolt->s.pos.trTime = level.time - MISSILE_PRESTEP_TIME;		// move a bit on the very first frame
	VectorCopy( start, bolt->s.pos.trBase );
	DictGetVector( def, "velocity", "900 0 0", velocity );
	PerpendicularVector( right, dir );
	CrossProduct( dir, right, up );
	VectorScale( dir, velocity[0], bolt->s.pos.trDelta );
	VectorMA( bolt->s.pos.trDelta, -velocity[1], right, bolt->s.pos.trDelta );	// y is left
	VectorMA( bolt->s.pos.trDelta, velocity[2], up, bolt->s.pos.trDelta );
	SnapVector( bolt->s.pos.trDelta );			// save net bandwidth
	VectorCopy (start, bolt->r.currentOrigin);

	if ( self->player ) {
		bolt->s.team = self->player->sess.sessionTeam;
	} else {
		bolt->s.team = TEAM_FREE;
	}

	return bolt;
}
