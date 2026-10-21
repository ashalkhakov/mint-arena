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
// g_weapon.c 
// perform the server side effects of a weapon firing

#include "g_local.h"

static	float	s_quadFactor;
static	vec3_t	forward, right, up;
static	vec3_t	muzzle;

/*
======================================================================

GRAPPLING HOOK

======================================================================
*/

void Weapon_HookFree (gentity_t *ent)
{
	ent->parent->player->hook = NULL;
	ent->parent->player->ps.pm_flags &= ~PMF_GRAPPLE_PULL;
	G_FreeEntity( ent );
}

void Weapon_HookThink (gentity_t *ent)
{
	ent->nextthink = level.time + FRAMETIME;

	if (ent->enemy && ent->enemy->player) {
		vec3_t v, oldorigin;

		if ( ent->enemy->player->ps.pm_type == PM_DEAD ) {
			Weapon_HookFree( ent );
			return;
		}

		VectorCopy(ent->r.currentOrigin, oldorigin);
		v[0] = ent->enemy->r.currentOrigin[0] + (ent->enemy->s.mins[0] + ent->enemy->s.maxs[0]) * 0.5;
		v[1] = ent->enemy->r.currentOrigin[1] + (ent->enemy->s.mins[1] + ent->enemy->s.maxs[1]) * 0.5;
		v[2] = ent->enemy->r.currentOrigin[2] + (ent->enemy->s.mins[2] + ent->enemy->s.maxs[2]) * 0.5;
		SnapVectorTowards( v, oldorigin );	// save net bandwidth

		G_SetOrigin( ent, v );
	}

	VectorCopy( ent->r.currentOrigin, ent->parent->player->ps.grapplePoint);
}

/*
======================================================================

HITSCAN

A hitscan weapon's shots are traced at once, from the muzzle to the
projectile's "range"; a melee weapon is one with a short range. The cgame
traces the same shots from the same seed for the impacts.

======================================================================
*/

/*
================
G_DamageDef

"damage" of a weapon's projectile's damage def (key "def_damage" or
"def_splash_damage"), and the splash "radius".
================
*/
static int G_DamageDef( const dict_t *projectile, const char *key, float *radius ) {
	const dict_t *damage;
	const char *name;

	if ( radius ) {
		*radius = 0;
	}
	if ( !projectile || !DictGetString( projectile, key, "", &name ) || !name[0] ) {
		return 0;
	}
	damage = EntityDef_Find( name );
	if ( !damage ) {
		G_Printf( S_COLOR_YELLOW "WARNING: %s: no damage def %s\n", DictGetStringReference( projectile, "classname", "?" ), name );
		return 0;
	}
	if ( radius ) {
		*radius = DictGetFloat( damage, "radius", "0" );
	}
	return DictGetInt( damage, "damage", "0" );
}

/*
================
G_HitscanShot

One shot; qtrue if it hit an enemy.
================
*/
static qboolean G_HitscanShot( gentity_t *ent, const bgWeapon_t *weapon, const vec3_t start, const vec3_t dir, int damage ) {
	trace_t		tr;
	vec3_t		end;
	gentity_t	*traceEnt;
	qboolean	hit = qfalse;
	int			zone;

	VectorMA( start, weapon->range, dir, end );
	G_TraceShot( &tr, start, NULL, NULL, end, ent->s.number, MASK_SHOT, &zone );
	if ( tr.surfaceFlags & SURF_NOIMPACT ) {
		return qfalse;
	}
	traceEnt = &g_entities[ tr.entityNum ];
	if ( traceEnt->takedamage ) {
		hit = LogAccuracyHit( traceEnt, ent );
		// a hitbox's zone scales the damage
		damage = damage * G_HitboxDamageScale( traceEnt, zone ) + 0.5f;
		G_Damage( traceEnt, ent, ent, (float *)dir, tr.endpos, damage, 0, MOD_FOR_WEAPON( ent->s.weapon, qfalse ) );
	}
	return hit;
}

/*
================
G_HitscanPattern

The weapon's shots from origin, aimed along origin2, as CG_HitscanPattern
makes them.
================
*/
static void G_HitscanPattern( gentity_t *ent, const bgWeapon_t *weapon, const vec3_t origin, const vec3_t origin2, int seed, int damage ) {
	vec3_t		fwd, rt, u, dir;
	qboolean	hitPlayer = qfalse;
	int			i;

	// derive the right and up vectors from the forward vector, because
	// the client won't have any other information
	VectorNormalize2( origin2, fwd );
	PerpendicularVector( rt, fwd );
	CrossProduct( fwd, rt, u );

	// backward-reconcile the other clients
	G_DoTimeShiftFor( ent );

	for ( i = 0; i < weapon->numProjectiles; i++ ) {
		BG_HitscanDir( fwd, rt, u, weapon->spread, &seed, dir );
		if ( G_HitscanShot( ent, weapon, origin, dir, damage ) ) {
			hitPlayer = qtrue;
		}
	}

	// put them back
	G_UndoTimeShiftFor( ent );

	if ( hitPlayer ) {
		ent->player->accuracy_hits++;
	}
}

static void Weapon_Hitscan( gentity_t *ent, const bgWeapon_t *weapon ) {
	gentity_t	*tent;
	int			damage;

	damage = G_DamageDef( weapon->projectile, "def_damage", NULL ) * s_quadFactor;

	// the cgame makes the impacts
	tent = G_TempEntity( muzzle, EV_HITSCAN );
	VectorScale( forward, 4096, tent->s.origin2 );
	SnapVector( tent->s.origin2 );
	tent->s.eventParm = rand() & 255;		// seed for the spread pattern
	tent->s.otherEntityNum = ent->s.number;
	tent->s.weapon = ent->s.weapon;

	G_HitscanPattern( ent, weapon, tent->s.pos.trBase, tent->s.origin2, tent->s.eventParm, damage );
}

/*
======================================================================

PROJECTILES

======================================================================
*/

static void Weapon_Projectile( gentity_t *ent, const bgWeapon_t *weapon ) {
	gentity_t	*m;
	vec3_t		dir;
	int			seed, i;

	seed = rand() & 255;
	for ( i = 0; i < weapon->numProjectiles; i++ ) {
		BG_HitscanDir( forward, right, up, weapon->spread, &seed, dir );
		m = fire_projectile( ent, weapon->projectile, ent->s.weapon, muzzle, dir );
		m->damage *= s_quadFactor;
		m->splashDamage *= s_quadFactor;
	}
}

/*
===============
LogAccuracyHit
===============
*/
qboolean LogAccuracyHit( gentity_t *target, gentity_t *attacker ) {
	if( !target->takedamage ) {
		return qfalse;
	}

	if ( target == attacker ) {
		return qfalse;
	}

	if( !target->player ) {
		return qfalse;
	}

	if( !attacker->player ) {
		return qfalse;
	}

	if( target->player->ps.stats[STAT_HEALTH] <= 0 ) {
		return qfalse;
	}

	if ( OnSameTeam( target, attacker ) ) {
		return qfalse;
	}

	return qtrue;
}


/*
===============
CalcMuzzlePoint

set muzzle location relative to pivoting eye
===============
*/
void CalcMuzzlePoint ( gentity_t *ent, int weapon, vec3_t forward, vec3_t right, vec3_t up, vec3_t muzzlePoint ) {
	VectorCopy( ent->s.pos.trBase, muzzlePoint );
	muzzlePoint[2] += ent->player->ps.viewheight;
	VectorMA( muzzlePoint, 14, forward, muzzlePoint );
	// snap to integer coordinates for more efficient network bandwidth usage
	SnapVector( muzzlePoint );
}

/*
===============
CalcMuzzlePointOrigin

set muzzle location relative to pivoting eye
===============
*/
void CalcMuzzlePointOrigin ( gentity_t *ent, int weapon, vec3_t origin, vec3_t forward, vec3_t right, vec3_t up, vec3_t muzzlePoint ) {
	VectorCopy( ent->s.pos.trBase, muzzlePoint );
	muzzlePoint[2] += ent->player->ps.viewheight;
	VectorMA( muzzlePoint, 14, forward, muzzlePoint );
	// snap to integer coordinates for more efficient network bandwidth usage
	SnapVector( muzzlePoint );
}



/*
===============
FireWeapon
===============
*/
void FireWeapon( gentity_t *ent ) {
	const bgWeapon_t *weapon;

	weapon = BG_Weapon( ent->s.weapon );
	if ( !weapon->projectile ) {
		return;
	}

	if ( g_instagib.integer ) {
		s_quadFactor = 100;
	} else if ( ent->player->ps.powerups[PW_QUAD] ) {
		s_quadFactor = g_quadfactor.value;
	} else {
		s_quadFactor = 1;
	}
#ifdef MISSIONPACK
	if( ent->player->persistantPowerup && ent->player->persistantPowerup->item && ent->player->persistantPowerup->item->giTag == PW_DOUBLER ) {
		s_quadFactor *= 2;
	}
#endif

	// track shots taken for accuracy tracking
	ent->player->accuracy_shots++;

	// set aiming directions
	AngleVectors (ent->player->ps.viewangles, forward, right, up);

	CalcMuzzlePointOrigin ( ent, ent->s.weapon, ent->player->oldOrigin, forward, right, up, muzzle );

	if ( weapon->hitscan ) {
		Weapon_Hitscan( ent, weapon );
	} else {
		Weapon_Projectile( ent, weapon );
	}
}


/*
===============
AltFireWeapon

The weapon's "altfire": "detonate" sets off the player's projectiles of it.
===============
*/
void AltFireWeapon( gentity_t *ent ) {
	gentity_t	*m;
	int			i;

	if ( BG_Weapon( ent->s.weapon )->altFire != ALTFIRE_DETONATE ) {
		return;
	}
	for ( i = MAX_CLIENTS, m = &g_entities[MAX_CLIENTS]; i < level.num_entities; i++, m++ ) {
		if ( m->inuse && m->s.eType == ET_MISSILE && m->r.ownerNum == ent->s.number
			&& m->s.weapon == ent->s.weapon ) {
			G_ExplodeMissile( m );
		}
	}
}


#ifdef MISSIONPACK

/*
===============
KamikazeRadiusDamage
===============
*/
static void KamikazeRadiusDamage( vec3_t origin, gentity_t *attacker, float damage, float radius ) {
	float		dist;
	gentity_t	*ent;
	int			entityList[MAX_GENTITIES];
	int			numListedEntities;
	vec3_t		mins, maxs;
	vec3_t		v;
	vec3_t		dir;
	int			i, e;

	if ( radius < 1 ) {
		radius = 1;
	}

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	numListedEntities = G_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		ent = &g_entities[entityList[ e ]];

		if (!ent->takedamage) {
			continue;
		}

		// don't hit things we have already hit
		if( ent->kamikazeTime > level.time ) {
			continue;
		}

		// find the distance from the edge of the bounding box
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( origin[i] < ent->r.absmin[i] ) {
				v[i] = ent->r.absmin[i] - origin[i];
			} else if ( origin[i] > ent->r.absmax[i] ) {
				v[i] = origin[i] - ent->r.absmax[i];
			} else {
				v[i] = 0;
			}
		}

		dist = VectorLength( v );
		if ( dist >= radius ) {
			continue;
		}

//		if( CanDamage (ent, origin) ) {
			VectorSubtract (ent->r.currentOrigin, origin, dir);
			// push the center of mass higher than the origin so players
			// get knocked into the air more
			dir[2] += 24;
			G_Damage( ent, NULL, attacker, dir, origin, damage, DAMAGE_RADIUS|DAMAGE_NO_TEAM_PROTECTION, MOD_KAMIKAZE );
			ent->kamikazeTime = level.time + 3000;
//		}
	}
}

/*
===============
KamikazeShockWave
===============
*/
static void KamikazeShockWave( vec3_t origin, gentity_t *attacker, float damage, float push, float radius ) {
	float		dist;
	gentity_t	*ent;
	int			entityList[MAX_GENTITIES];
	int			numListedEntities;
	vec3_t		mins, maxs;
	vec3_t		v;
	vec3_t		dir;
	int			i, e;

	if ( radius < 1 )
		radius = 1;

	for ( i = 0 ; i < 3 ; i++ ) {
		mins[i] = origin[i] - radius;
		maxs[i] = origin[i] + radius;
	}

	numListedEntities = G_EntitiesInBox( mins, maxs, entityList, MAX_GENTITIES );

	for ( e = 0 ; e < numListedEntities ; e++ ) {
		ent = &g_entities[entityList[ e ]];

		// don't hit things we have already hit
		if( ent->kamikazeShockTime > level.time ) {
			continue;
		}

		// find the distance from the edge of the bounding box
		for ( i = 0 ; i < 3 ; i++ ) {
			if ( origin[i] < ent->r.absmin[i] ) {
				v[i] = ent->r.absmin[i] - origin[i];
			} else if ( origin[i] > ent->r.absmax[i] ) {
				v[i] = origin[i] - ent->r.absmax[i];
			} else {
				v[i] = 0;
			}
		}

		dist = VectorLength( v );
		if ( dist >= radius ) {
			continue;
		}

//		if( CanDamage (ent, origin) ) {
			VectorSubtract (ent->r.currentOrigin, origin, dir);
			dir[2] += 24;
			G_Damage( ent, NULL, attacker, dir, origin, damage, DAMAGE_RADIUS|DAMAGE_NO_TEAM_PROTECTION, MOD_KAMIKAZE );
			//
			dir[2] = 0;
			VectorNormalize(dir);
			if ( ent->player ) {
				ent->player->ps.velocity[0] = dir[0] * push;
				ent->player->ps.velocity[1] = dir[1] * push;
				ent->player->ps.velocity[2] = 100;
			}
			ent->kamikazeShockTime = level.time + 3000;
//		}
	}
}

/*
===============
KamikazeDamage
===============
*/
static void KamikazeDamage( gentity_t *self ) {
	int i;
	float t;
	gentity_t *ent;
	vec3_t newangles;

	self->count += 100;

	if (self->count >= KAMI_SHOCKWAVE_STARTTIME) {
		// shockwave push back
		t = self->count - KAMI_SHOCKWAVE_STARTTIME;
		KamikazeShockWave(self->s.pos.trBase, self->activator, 25, 400,	(int) (float) t * KAMI_SHOCKWAVE_MAXRADIUS / (KAMI_SHOCKWAVE_ENDTIME - KAMI_SHOCKWAVE_STARTTIME) );
	}
	//
	if (self->count >= KAMI_EXPLODE_STARTTIME) {
		// do our damage
		t = self->count - KAMI_EXPLODE_STARTTIME;
		KamikazeRadiusDamage( self->s.pos.trBase, self->activator, 400,	(int) (float) t * KAMI_BOOMSPHERE_MAXRADIUS / (KAMI_IMPLODE_STARTTIME - KAMI_EXPLODE_STARTTIME) );
	}

	// either cycle or kill self
	if( self->count >= KAMI_SHOCKWAVE_ENDTIME ) {
		G_FreeEntity( self );
		return;
	}
	self->nextthink = level.time + 100;

	// add earth quake effect
	newangles[0] = crandom() * 2;
	newangles[1] = crandom() * 2;
	newangles[2] = 0;
	for (i = 0; i < MAX_CLIENTS; i++)
	{
		ent = &g_entities[i];
		if (!ent->inuse)
			continue;
		if (!ent->player)
			continue;

		if (ent->player->ps.groundEntityNum != ENTITYNUM_NONE) {
			ent->player->ps.velocity[0] += crandom() * 120;
			ent->player->ps.velocity[1] += crandom() * 120;
			ent->player->ps.velocity[2] = 30 + random() * 25;
		}

		ent->player->ps.delta_angles[0] += ANGLE2SHORT(newangles[0] - self->movedir[0]);
		ent->player->ps.delta_angles[1] += ANGLE2SHORT(newangles[1] - self->movedir[1]);
		ent->player->ps.delta_angles[2] += ANGLE2SHORT(newangles[2] - self->movedir[2]);
	}
	VectorCopy(newangles, self->movedir);
}

/*
===============
G_StartKamikaze
===============
*/
void G_StartKamikaze( gentity_t *ent ) {
	gentity_t	*explosion;
	gentity_t	*te;
	vec3_t		snapped;

	// start up the explosion logic
	explosion = G_Spawn();

	explosion->s.eType = ET_EVENTS + EV_KAMIKAZE;
	explosion->eventTime = level.time;

	if ( ent->player ) {
		VectorCopy( ent->s.pos.trBase, snapped );
	}
	else {
		VectorCopy( ent->activator->s.pos.trBase, snapped );
	}
	SnapVector( snapped );		// save network bandwidth
	G_SetOrigin( explosion, snapped );

	explosion->classname = "kamikaze";
	explosion->s.pos.trType = TR_STATIONARY;

	explosion->kamikazeTime = level.time;

	explosion->think = KamikazeDamage;
	explosion->nextthink = level.time + 100;
	explosion->count = 0;
	VectorClear(explosion->movedir);

	G_LinkEntity( explosion );

	if (ent->player) {
		//
		explosion->activator = ent;
		//
		ent->s.eFlags &= ~EF_KAMIKAZE;
		ent->player->ps.eFlags &= ~EF_KAMIKAZE;
		// nuke the guy that used it
		G_Damage( ent, ent, ent, NULL, NULL, 100000, DAMAGE_NO_PROTECTION, MOD_KAMIKAZE );
	}
	else {
		if ( !strcmp(ent->activator->classname, "bodyque") ) {
			explosion->activator = &g_entities[ent->activator->r.ownerNum];
		}
		else {
			explosion->activator = ent->activator;
		}
	}

	// play global sound at all clients
	te = G_TempEntity(snapped, EV_GLOBAL_TEAM_SOUND );
	te->r.svFlags |= SVF_BROADCAST;
	te->s.eventParm = GTS_KAMIKAZE;
}
#endif
