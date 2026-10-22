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
===========================================================================
*/
// g_monster.c -- monsters, as Doom 3's idAI: a character (bg_model.c) whose
// behaviour is its script object's. The game moves it (on the ground with
// gravity and steps, or flying with momentum, like a drone), sees for it,
// hurts and kills it; its script's states (setState()) decide what it does,
// through the events of g_script_events.c, in a thread of its own run each
// think (idActor::UpdateScript), the AI_* fields of its object kept as they
// are (Doom 3's idScriptBool links).
//
// A monster is an entity def with "spawnclass" "monster":
//
//	entityDef monster_froginator {
//		"spawnclass"	"monster"
//		"model"			"model_froginator"	// the model def: mesh, animations
//		"scriptobject"	"monster_froginator"
//		"health"		"30"
//		"mins"			"-8 -8 -12"
//		"maxs"			"8 8 12"
//		"modelscale"	"0.65"
//		"run_speed"		"220"				// what its moves go at (setMoveSpeed changes it)
//		"turn_rate"		"360"				// degrees a second
//		"fov"			"270"
//		"sight_range"	"1500"
//		"fly"			"1"					// it flies: "fly_speed", "fly_accel", "fly_offset"
//		"impact_speed"	"300"				// hitting the world faster than this hurts it
//		"impact_damage"	"5"					// per 100 u/s above impact_speed
//		"impact_bounce"	"0.5"				// how much of its speed it bounces off with
//		"leap_damage"	"damage_frog_leap"	// a damage def: what its leaps do to whom they hit ("push" them back)
//		"skill_scale"	"1"					// Daikatana's: health and speeds x0.75 easy, x1.25 hard
//		"snd_sight"		"monster_frog_sight"	// sound shaders, for startSound()
//	}

#include "g_local.h"

#define MONSTER_STEPSIZE		18
#define MONSTER_MAX_STATES		20		// state changes in a frame (idActor::UpdateScript's)
#define MONSTER_REACHED			16		// a move is done this close to where it goes

void	G_CharacterRecord( gentity_t *ent );		// g_character.c
static qboolean G_MonsterUnstick( gentity_t *ent, vec3_t origin );

/*
================
G_MonsterKey

its spawn arg, else its def's
================
*/
static const char *G_MonsterKey( gentity_t *ent, const char *key, const char *defaultValue ) {
	char *s;

	// as it spawns, the map's keys aren't its spawn args yet
	if ( level.spawning && G_SpawnString( key, "", &s ) ) {
		return s;
	}
	return G_ScriptGetSpawnArg( ent, key, defaultValue );
}

static float G_MonsterFloat( gentity_t *ent, const char *key, const char *defaultValue ) {
	return atof( G_MonsterKey( ent, key, defaultValue ) );
}

/*
================
G_IsMonster
================
*/
qboolean G_IsMonster( const gentity_t *ent ) {
	return ent && ent->inuse && ( ent->monster.flags & MONF_MONSTER );
}

/*
===============================================================================

the script's view of it: its object's AI_* fields

===============================================================================
*/

static void G_MonsterSetField( gentity_t *ent, const char *name, qboolean value ) {
	int *field = (int *)ScriptObject_GetVariable( &ent->scriptObject, name, ev_boolean );

	if ( field ) {
		*field = value != 0;
	}
}

static void G_MonsterUpdateFields( gentity_t *ent ) {
	monsterState_t *m = &ent->monster;

	if ( !ScriptObject_HasObject( &ent->scriptObject ) ) {
		return;
	}
	G_MonsterSetField( ent, "AI_ONGROUND", m->flags & MONF_ONGROUND );
	G_MonsterSetField( ent, "AI_PAIN", m->flags & MONF_PAIN );
	G_MonsterSetField( ent, "AI_DEAD", m->flags & MONF_DEAD );
	G_MonsterSetField( ent, "AI_ENEMY_VISIBLE", m->flags & MONF_ENEMY_VISIBLE );
	G_MonsterSetField( ent, "AI_ENEMY_IN_FOV", m->flags & MONF_ENEMY_IN_FOV );
	G_MonsterSetField( ent, "AI_MOVE_DONE", m->moveStatus != MOVE_STATUS_MOVING );
	G_MonsterSetField( ent, "AI_BLOCKED", m->flags & MONF_BLOCKED );
	G_MonsterSetField( ent, "AI_IMPACT", m->flags & MONF_IMPACT );
	G_MonsterSetField( ent, "AI_LEAP_HIT", m->flags & MONF_LEAP_HIT );
	G_MonsterSetField( ent, "AI_IN_WATER", ent->waterlevel > 1 );
}

/*
===============================================================================

seeing

===============================================================================
*/

/*
================
G_MonsterEye
================
*/
void G_MonsterEye( const gentity_t *ent, vec3_t eye ) {
	if ( ent->monster.flags & MONF_PERCHED ) {
		// out from what it's on (a ceiling's under it)
		VectorMA( ent->r.currentOrigin, ent->monster.moveMaxs[2] * 0.75f, ent->monster.perchNormal, eye );
		return;
	}
	VectorCopy( ent->r.currentOrigin, eye );
	eye[2] += ent->monster.moveMaxs[2] * 0.75f;
}

void G_TargetEye( const gentity_t *ent, vec3_t eye ) {
	VectorCopy( ent->r.currentOrigin, eye );
	if ( ent->player ) {
		eye[2] += ent->player->ps.viewheight;
	} else {
		eye[2] += ( ent->s.mins[2] + ent->s.maxs[2] ) * 0.5f;
	}
}

/*
================
G_MonsterCanSee

nothing opaque between its eye and the other's; a player with Daikatana's
wraith orb is never seen
================
*/
qboolean G_MonsterCanSee( gentity_t *ent, gentity_t *other ) {
	trace_t	tr;
	vec3_t	start, end;

	if ( !other || !other->inuse ) {
		return qfalse;
	}
	if ( other->player && other->player->ps.powerups[PW_INVIS] ) {
		return qfalse;
	}
	G_MonsterEye( ent, start );
	G_TargetEye( other, end );
	if ( !trap_InPVS( start, end ) ) {
		return qfalse;
	}
	G_TraceBox( &tr, start, NULL, NULL, end, ent->s.number, MASK_OPAQUE );
	return tr.fraction == 1.0f || tr.entityNum == other->s.number;
}

/*
================
G_MonsterInFov
================
*/
static qboolean G_MonsterInFov( const gentity_t *ent, const vec3_t pos ) {
	vec3_t	dir;
	float	yaw, delta;

	if ( ent->monster.fov >= 360.0f || ( ent->monster.flags & MONF_PERCHED ) ) {
		return qtrue;		// (perched, it sees what's off its wall, wherever its head points)
	}
	VectorSubtract( pos, ent->r.currentOrigin, dir );
	yaw = vectoyaw( dir );
	delta = fabs( AngleSubtract( yaw, ent->r.currentAngles[YAW] ) );
	return delta <= ent->monster.fov * 0.5f;
}

/*
================
G_MonsterFindEnemy

the closest living player it sees (in its field of view), within its sight
================
*/
gentity_t *G_MonsterFindEnemy( gentity_t *ent, qboolean useFov ) {
	gentity_t	*best = NULL, *other;
	float		bestDist, dist;
	int			i;

	bestDist = ent->monster.sightRange;
	for ( i = 0; i < level.maxplayers; i++ ) {
		other = &g_entities[i];
		if ( !other->inuse || !other->player || other->health <= 0 || ( other->flags & FL_NOTARGET ) ) {
			continue;
		}
		if ( other->player->sess.sessionTeam == TEAM_SPECTATOR ) {
			continue;
		}
		dist = Distance( other->r.currentOrigin, ent->r.currentOrigin );
		if ( dist > bestDist ) {
			continue;
		}
		if ( useFov && !G_MonsterInFov( ent, other->r.currentOrigin ) ) {
			continue;
		}
		if ( !G_MonsterCanSee( ent, other ) ) {
			continue;
		}
		best = other;
		bestDist = dist;
	}
	return best;
}

/*
================
G_MonsterEnemy
================
*/
gentity_t *G_MonsterEnemy( gentity_t *ent ) {
	gentity_t *enemy;

	if ( ent->monster.enemy == ENTITYNUM_NONE ) {
		return NULL;
	}
	enemy = &g_entities[ ent->monster.enemy ];
	if ( !enemy->inuse || enemy->health <= 0 ) {
		return NULL;
	}
	return enemy;
}

/*
================
G_MonsterSense

whether it sees its enemy, and where it saw it last
================
*/
static void G_MonsterSense( gentity_t *ent ) {
	monsterState_t	*m = &ent->monster;
	gentity_t		*enemy = G_MonsterEnemy( ent );

	m->flags &= ~( MONF_ENEMY_VISIBLE | MONF_ENEMY_IN_FOV );
	if ( !enemy ) {
		m->enemy = ENTITYNUM_NONE;
		return;
	}
	if ( G_MonsterCanSee( ent, enemy ) ) {
		m->flags |= MONF_ENEMY_VISIBLE;
		VectorCopy( enemy->r.currentOrigin, m->lastEnemyPos );
		m->lastEnemySeen = level.time;
		if ( G_MonsterInFov( ent, enemy->r.currentOrigin ) ) {
			m->flags |= MONF_ENEMY_IN_FOV;
		}
	}
}

/*
===============================================================================

moving

===============================================================================
*/

/*
================
G_MonsterMoveGoal

where its move goes, qfalse if nowhere
================
*/
static qboolean G_MonsterMoveGoal( gentity_t *ent, vec3_t goal ) {
	monsterState_t	*m = &ent->monster;
	gentity_t		*target;

	switch ( m->moveCommand ) {
		case MOVE_TO_ENEMY:
			target = G_MonsterEnemy( ent );
			if ( !target ) {
				return qfalse;
			}
			// where it is, or where it was seen
			if ( m->flags & MONF_ENEMY_VISIBLE ) {
				VectorCopy( target->r.currentOrigin, goal );
			} else {
				VectorCopy( m->lastEnemyPos, goal );
			}
			return qtrue;
		case MOVE_TO_ENTITY:
			if ( m->moveEntity < 0 || !g_entities[ m->moveEntity ].inuse ) {
				return qfalse;
			}
			VectorCopy( g_entities[ m->moveEntity ].r.currentOrigin, goal );
			return qtrue;
		case MOVE_TO_POSITION:
		case MOVE_WANDER:
			VectorCopy( m->moveDest, goal );
			return qtrue;
		default:
			return qfalse;
	}
}

/*
================
G_MonsterClipVelocity

off a plane it hit: as a slide, or a bounce for a hard hit; qtrue a hard one
================
*/
static qboolean G_MonsterClipVelocity( gentity_t *ent, vec3_t vel, const vec3_t normal ) {
	monsterState_t	*m = &ent->monster;
	float			into, bounce, damage;

	into = -DotProduct( vel, normal );
	if ( into <= 0 ) {
		return qfalse;
	}

	// a wall (not a floor it lands on), hit hard
	if ( normal[2] < 0.7f && m->impactSpeed > 0 && into > m->impactSpeed ) {
		bounce = m->impactBounce;
		VectorMA( vel, ( 1.0f + bounce ) * into, normal, vel );
		m->flags |= MONF_IMPACT;
		m->impactTime = level.time;
		if ( m->impactDamage > 0 ) {
			damage = ( into - m->impactSpeed ) * 0.01f * m->impactDamage;
			if ( damage >= 1 ) {
				m->pendingImpactDamage += damage;
			}
		}
		return qtrue;
	}

	// slide along it
	VectorMA( vel, into * 1.001f, normal, vel );
	return qfalse;
}

/*
================
G_MonsterHitBody

it ran into someone (a player, a monster): no crash for it, but a leap
hurts them ("leap_damage", once a leap) and knocks them back, and it
bounces off; qfalse if it isn't a body
================
*/
static qboolean G_MonsterHitBody( gentity_t *ent, gentity_t *other, vec3_t vel, const vec3_t normal ) {
	monsterState_t	*m = &ent->monster;
	const char		*damageDef;
	vec3_t			dir;
	float			into, speed;
	int				damage;

	if ( !other->inuse || !( other->player || G_IsMonster( other ) || other->s.eType == ET_CHARACTER ) ) {
		return qfalse;
	}
	into = -DotProduct( vel, normal );
	speed = VectorLength( vel );

	if ( ( m->flags & MONF_LEAPING ) && speed > 150 && other->takedamage && !G_IsMonster( other ) ) {
		damageDef = G_ScriptGetSpawnArg( ent, "leap_damage", "" );
		if ( damageDef[0] && EntityDef_Find( damageDef ) ) {
			const dict_t *def = EntityDef_Find( damageDef );

			damage = DictGetInt( def, "damage", "0" ) + (int)( random() * DictGetInt( def, "damage_random", "0" ) );
			VectorNormalize2( vel, dir );
			G_Damage( other, ent, ent, dir, ent->r.currentOrigin, damage, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN );
			if ( other->player ) {
				// knocked back by its weight
				VectorMA( other->player->ps.velocity, DictGetFloat( def, "push", "150" ), dir, other->player->ps.velocity );
				other->player->ps.velocity[2] += 50;
			}
			m->flags = ( m->flags & ~MONF_LEAPING ) | MONF_LEAP_HIT;
		}
	}

	// off them, losing most of its speed
	if ( into > 0 ) {
		VectorMA( vel, into * 1.3f, normal, vel );
		VectorScale( vel, 0.5f, vel );
	}
	return qtrue;
}

/*
================
G_MonsterSlideMove

as far as it goes this frame, sliding along (or bouncing off) what it hits;
qtrue if it was blocked
================
*/
static qboolean G_MonsterSlideMove( gentity_t *ent, vec3_t origin, vec3_t vel, float dt, int mask ) {
	trace_t		tr;
	vec3_t		end;
	float		left = dt;
	qboolean	blocked = qfalse;
	int			bump;

	for ( bump = 0; bump < 4 && left > 0; bump++ ) {
		VectorMA( origin, left, vel, end );
		G_TraceBox( &tr, origin, ent->monster.moveMins, ent->monster.moveMaxs, end, ent->s.number, mask );
		if ( tr.allsolid ) {
			// stuck: it stays
			vel[2] = MAX( vel[2], 0 );
			return qtrue;
		}
		VectorCopy( tr.endpos, origin );
		if ( tr.fraction == 1.0f ) {
			break;
		}
		left -= left * tr.fraction;
		blocked = qtrue;
		if ( tr.entityNum < ENTITYNUM_MAX_NORMAL ) {
			ent->monster.blockedBy = tr.entityNum;
			if ( G_MonsterHitBody( ent, &g_entities[ tr.entityNum ], vel, tr.plane.normal ) ) {
				continue;
			}
		}
		G_MonsterClipVelocity( ent, vel, tr.plane.normal );
	}
	return blocked;
}

/*
================
G_MonsterGroundCheck
================
*/
static void G_MonsterGroundCheck( gentity_t *ent, const vec3_t origin, int mask ) {
	trace_t	tr;
	vec3_t	end;

	ent->monster.flags &= ~MONF_ONGROUND;
	if ( ent->monster.velocity[2] > 50 ) {
		return;		// on its way up
	}
	VectorCopy( origin, end );
	end[2] -= 0.5f;
	G_TraceBox( &tr, origin, ent->monster.moveMins, ent->monster.moveMaxs, end, ent->s.number, mask );
	if ( tr.fraction < 1.0f && tr.entityNum < ENTITYNUM_MAX_NORMAL && ( g_entities[ tr.entityNum ].player || g_entities[ tr.entityNum ].s.eType == ET_CHARACTER ) ) {
		// on someone's head (a leap that came down on them): off it, away from their middle
		gentity_t	*under = &g_entities[ tr.entityNum ];
		vec3_t		away;

		VectorSubtract( origin, under->r.currentOrigin, away );
		away[2] = 0;
		if ( VectorNormalize( away ) < 1 ) {
			VectorSet( away, crandom(), crandom(), 0 );
			VectorNormalize( away );
		}
		if ( DotProduct( ent->monster.velocity, away ) < 150 ) {
			VectorMA( ent->monster.velocity, 150, away, ent->monster.velocity );
		}
		ent->s.groundEntityNum = ENTITYNUM_NONE;
		return;
	}
	if ( tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f ) {
		ent->monster.flags |= MONF_ONGROUND;
		ent->s.groundEntityNum = tr.entityNum;
	} else {
		ent->s.groundEntityNum = ENTITYNUM_NONE;
	}
}

/*
================
G_MonsterGroundMove

Doom 3's idPhysics_Monster: it walks where its move goes, stepping up and
down steps; in the air it falls (and flies off the way it was thrown: a jump)
================
*/
static void G_MonsterGroundMove( gentity_t *ent, float dt ) {
	monsterState_t	*m = &ent->monster;
	vec3_t			origin, vel, goal, dir, start, stepVel, stepOrigin, down;
	trace_t			tr;
	qboolean		moving = qfalse;
	int				mask = MASK_PLAYERSOLID | CONTENTS_MONSTERCLIP;
	float			dist, speed;

	VectorCopy( ent->r.currentOrigin, origin );
	VectorCopy( m->velocity, vel );

	if ( ( m->flags & MONF_ONGROUND ) && !( m->flags & MONF_DEAD ) && m->moveStatus == MOVE_STATUS_MOVING && G_MonsterMoveGoal( ent, goal ) ) {
		VectorSubtract( goal, origin, dir );
		dir[2] = 0;
		dist = VectorNormalize( dir );
		if ( dist < MONSTER_REACHED + ent->monster.moveMaxs[0] && m->moveCommand != MOVE_TO_ENEMY ) {
			m->moveStatus = MOVE_STATUS_DONE;
		} else if ( !( m->flags & MONF_NOMOVE ) ) {
			// at its speed, slower as it gets there
			speed = MIN( m->moveSpeed, dist * 4 );
			vel[0] = dir[0] * speed;
			vel[1] = dir[1] * speed;
			moving = qtrue;
		}
	}
	if ( ( m->flags & MONF_ONGROUND ) && !moving ) {
		// friction
		float f = MAX( 0, 1.0f - dt * 8 );

		vel[0] *= f;
		vel[1] *= f;
	}
	if ( !( m->flags & MONF_ONGROUND ) && !( m->flags & MONF_NOGRAVITY ) ) {
		vel[2] -= g_gravity.value * dt;
	} else if ( m->flags & MONF_ONGROUND ) {
		vel[2] = MAX( vel[2], 0 );
	}

	VectorCopy( origin, start );
	VectorCopy( vel, stepVel );
	if ( G_MonsterSlideMove( ent, origin, vel, dt, mask ) && ( m->flags & MONF_ONGROUND ) ) {
		// blocked on the ground: try it a step up
		VectorCopy( start, stepOrigin );
		VectorCopy( start, down );
		down[2] += MONSTER_STEPSIZE;
		G_TraceBox( &tr, start, ent->monster.moveMins, ent->monster.moveMaxs, down, ent->s.number, mask );
		if ( !tr.allsolid ) {
			VectorCopy( tr.endpos, stepOrigin );
			G_MonsterSlideMove( ent, stepOrigin, stepVel, dt, mask );
			VectorCopy( stepOrigin, down );
			down[2] -= MONSTER_STEPSIZE;
			G_TraceBox( &tr, stepOrigin, ent->monster.moveMins, ent->monster.moveMaxs, down, ent->s.number, mask );
			if ( !tr.allsolid && tr.plane.normal[2] >= 0.7f
				&& DistanceSquared( start, tr.endpos ) > DistanceSquared( start, origin ) + 1 ) {
				// further that way
				VectorCopy( tr.endpos, origin );
				VectorCopy( stepVel, vel );
				vel[2] = 0;
			}
		}
	}
	m->flags = ( m->flags & ~MONF_BLOCKED ) | ( moving && Distance( start, origin ) < m->moveSpeed * dt * 0.1f ? MONF_BLOCKED : 0 );

	VectorCopy( vel, m->velocity );
	G_MonsterGroundCheck( ent, origin, mask );
	if ( m->flags & MONF_ONGROUND ) {
		m->flags &= ~MONF_LEAPING;
	}

	// stay on the ground walking down slopes and steps
	if ( !( m->flags & MONF_ONGROUND ) && moving && m->velocity[2] <= 0 ) {
		VectorCopy( origin, down );
		down[2] -= MONSTER_STEPSIZE;
		G_TraceBox( &tr, origin, ent->monster.moveMins, ent->monster.moveMaxs, down, ent->s.number, mask );
		if ( !tr.allsolid && tr.fraction < 1.0f && tr.plane.normal[2] >= 0.7f ) {
			VectorCopy( tr.endpos, origin );
			m->flags |= MONF_ONGROUND;
			m->velocity[2] = 0;
		}
	}

	G_SetOrigin( ent, origin );
}

/*
================
G_MonsterFlySteer

where a flyer heads for, to get to goal: there if it flies there in a line,
else the next waypoint of its route through the air (planned again when the
goal moved, or a while on), the farthest it sees on it
================
*/
static void G_MonsterFlySteer( gentity_t *ent, const vec3_t origin, const vec3_t goal, vec3_t steer ) {
	monsterState_t	*m = &ent->monster;
	int				n;

	if ( G_AirClear( origin, goal, ent->monster.moveMins, ent->monster.moveMaxs, ent->s.number ) ) {
		m->routeLen = 0;
		VectorCopy( goal, steer );
		return;
	}

	if ( m->routeLen <= 0 || m->routeIndex >= m->routeLen || level.time - m->routeTime > 2000
		|| Distance( goal, m->routeGoal ) > 96 ) {
		n = G_AirRoute( origin, goal, ent->monster.moveMins, ent->monster.moveMaxs, ent->s.number, m->route, MONSTER_ROUTE );
		if ( n >= 0 ) {
			// (-1: the searches of the frame are done; this one's next frame)
			m->routeLen = n;
			m->routeIndex = 0;
			m->routeTime = level.time;
			VectorCopy( goal, m->routeGoal );
			if ( g_debugMove.integer ) {
				int k;

				G_Printf( "%s %d: a route of %d to %s:", ent->classname, ent->s.number, n, vtos( goal ) );
				for ( k = 0; k < n; k++ ) {
					G_Printf( " %s", vtos( m->route[k] ) );
				}
				G_Printf( "\n" );
			}
		}
	}
	if ( m->routeLen <= 0 ) {
		VectorCopy( goal, steer );		// no way: at it, and bounce
		return;
	}

	// on to the farthest waypoint it sees
	while ( m->routeIndex < m->routeLen - 1
			&& ( Distance( origin, m->route[m->routeIndex] ) < MONSTER_REACHED * 2
				 || G_AirClear( origin, m->route[m->routeIndex + 1], ent->monster.moveMins, ent->monster.moveMaxs, ent->s.number ) ) ) {
		m->routeIndex++;
	}
	VectorCopy( m->route[m->routeIndex], steer );
}

/*
================
G_MonsterPerchBox

a box turned by the axis it's perched with: the box that holds it
================
*/
static void G_MonsterPerchBox( vec3_t axis[3], const vec3_t mins, const vec3_t maxs, vec3_t outMins, vec3_t outMaxs ) {
	vec3_t	p;
	int		i, k;

	ClearBounds( outMins, outMaxs );
	for ( i = 0; i < 8; i++ ) {
		VectorClear( p );
		for ( k = 0; k < 3; k++ ) {
			VectorMA( p, ( i & ( 1 << k ) ) ? maxs[k] : mins[k], axis[k], p );
		}
		AddPointToBounds( p, outMins, outMaxs );
	}
}

/*
================
G_MonsterPerch

a flyer lands at pos on what faces normal (a wall, a ceiling, the floor):
its belly to it, its head up a wall (somewhat to one side) or as it faced,
its boxes turned with it. qfalse if there's no room for it there
================
*/
qboolean G_MonsterPerch( gentity_t *ent, const vec3_t pos, const vec3_t normal ) {
	monsterState_t	*m = &ent->monster;
	vec3_t			axis[3], n, origin, mins, maxs, hitMins, hitMaxs, up = { 0, 0, 1 }, side;
	trace_t			tr;
	float			a;
	int				i;

	if ( !( m->flags & MONF_FLY ) || ( m->flags & ( MONF_DEAD | MONF_PERCHED ) ) ) {
		return qfalse;
	}
	VectorNormalize2( normal, n );

	// forward along the surface: up it on a wall, as it faced on a floor or a ceiling
	if ( fabs( n[2] ) < 0.7f ) {
		CrossProduct( up, n, side );
		VectorNormalize( side );
		VectorMA( up, -DotProduct( up, n ), n, axis[0] );
		VectorNormalize( axis[0] );
		a = DEG2RAD( crandom() * 35 );
		VectorScale( axis[0], cos( a ), axis[0] );
		VectorMA( axis[0], sin( a ), side, axis[0] );
	} else {
		AngleVectors( ent->r.currentAngles, axis[0], NULL, NULL );
		VectorMA( axis[0], -DotProduct( axis[0], n ), n, axis[0] );
		if ( VectorNormalize( axis[0] ) < 0.01f ) {
			PerpendicularVector( axis[0], n );
		}
	}
	VectorCopy( n, axis[2] );
	CrossProduct( axis[2], axis[0], axis[1] );

	// a little off the surface, as far as its turned box needs
	G_MonsterPerchBox( axis, m->moveMins, m->moveMaxs, mins, maxs );
	VectorMA( pos, 0.5f, n, origin );
	for ( i = 0; i < 8; i++ ) {
		G_TraceBox( &tr, origin, mins, maxs, origin, ent->s.number, ent->clipmask );
		if ( !tr.startsolid ) {
			break;
		}
		VectorMA( origin, 2, n, origin );
	}
	if ( tr.startsolid ) {
		return qfalse;
	}

	// its angles from its axis: forward, left, up
	m->perchAngles[YAW] = RAD2DEG( atan2( axis[0][1], axis[0][0] ) );
	m->perchAngles[PITCH] = RAD2DEG( atan2( -axis[0][2], sqrt( axis[0][0] * axis[0][0] + axis[0][1] * axis[0][1] ) ) );
	m->perchAngles[ROLL] = RAD2DEG( atan2( axis[1][2], axis[2][2] ) );
	VectorCopy( n, m->perchNormal );

	VectorCopy( m->hitMins, m->flyHitMins );
	VectorCopy( m->hitMaxs, m->flyHitMaxs );
	G_MonsterPerchBox( axis, m->hitMins, m->hitMaxs, hitMins, hitMaxs );
	VectorCopy( hitMins, m->hitMins );
	VectorCopy( hitMaxs, m->hitMaxs );
	VectorCopy( mins, ent->s.mins );
	VectorCopy( maxs, ent->s.maxs );

	m->flags |= MONF_PERCHED;
	VectorClear( m->velocity );
	m->roll = 0;
	m->moveStatus = MOVE_STATUS_DONE;
	G_SetOrigin( ent, origin );
	trap_LinkEntity( ent );
	return qtrue;
}

/*
================
G_MonsterTakeOff

off what it's perched on, pushing away from it
================
*/
static void G_MonsterTakeOff( gentity_t *ent ) {
	monsterState_t	*m = &ent->monster;
	vec3_t			origin;

	m->flags &= ~MONF_PERCHED;
	VectorCopy( m->moveMins, ent->s.mins );
	VectorCopy( m->moveMaxs, ent->s.maxs );
	VectorCopy( m->flyHitMins, m->hitMins );
	VectorCopy( m->flyHitMaxs, m->hitMaxs );
	VectorScale( m->perchNormal, 120, m->velocity );
	VectorCopy( ent->r.currentOrigin, origin );
	G_MonsterUnstick( ent, origin );
	G_SetOrigin( ent, origin );
	trap_LinkEntity( ent );
}

/*
================
G_MonsterFlyMove

flying as a drone does: it wants a velocity toward where it goes (above it
by its fly offset, bobbing), its thrust gets it there no faster than its
acceleration, it keeps its momentum, banks into turns and bounces off walls
================
*/
static void G_MonsterFlyMove( gentity_t *ent, float dt ) {
	monsterState_t	*m = &ent->monster;
	vec3_t			origin, vel, goal, dir, wish, change, right;
	float			dist, speed, accel, len, lateral, roll;
	int				mask = MASK_PLAYERSOLID | CONTENTS_MONSTERCLIP;

	if ( m->flags & MONF_PERCHED ) {
		// it stays, until it's told to go somewhere (or thrown)
		if ( m->moveStatus != MOVE_STATUS_MOVING && VectorLengthSquared( m->velocity ) < 1 ) {
			return;
		}
		G_MonsterTakeOff( ent );
	}

	VectorCopy( ent->r.currentOrigin, origin );
	VectorCopy( m->velocity, vel );

	VectorClear( wish );
	if ( !( m->flags & ( MONF_DEAD | MONF_NOMOVE ) ) && m->moveStatus == MOVE_STATUS_MOVING && G_MonsterMoveGoal( ent, goal ) ) {
		vec3_t steer;

		goal[2] += m->flyOffset + sin( level.time * 0.003f + ent->s.number ) * m->flyBob;
		dist = Distance( goal, origin );
		if ( dist < MONSTER_REACHED && m->moveCommand != MOVE_TO_ENEMY ) {
			m->moveStatus = MOVE_STATUS_DONE;
		} else {
			// straight there, or along its way through the air
			G_MonsterFlySteer( ent, origin, goal, steer );
			VectorSubtract( steer, origin, dir );
			VectorNormalize( dir );
			// at its speed, slowing to arrive
			speed = MIN( m->flySpeed, dist * 2 );
			VectorScale( dir, speed, wish );
		}
	}

	// the thrust: from its velocity to the one it wants, no faster than it accelerates
	VectorSubtract( wish, vel, change );
	len = VectorLength( change );
	accel = m->flyAccel * dt;
	if ( len > accel ) {
		VectorScale( change, accel / len, change );
	}
	VectorAdd( vel, change, vel );

	// banking: the sideways part of its thrust
	AngleVectors( ent->r.currentAngles, NULL, right, NULL );
	lateral = dt > 0 ? DotProduct( change, right ) / dt : 0;
	roll = Com_Clamp( -45, 45, lateral * 0.05f );
	m->roll += ( roll - m->roll ) * MIN( 1.0f, dt * 5 );

	if ( G_MonsterSlideMove( ent, origin, vel, dt, mask ) ) {
		m->flags |= MONF_BLOCKED;
	} else {
		m->flags &= ~MONF_BLOCKED;
	}
	if ( g_debugMove.integer && ( level.framenum % 20 ) == ( ent->s.number % 20 ) ) {
		trace_t tr;

		G_TraceBox( &tr, origin, ent->monster.moveMins, ent->monster.moveMaxs, origin, ent->s.number, mask );
		G_Printf( "fly %d: at %s wish %s vel %s status %d route %d/%d%s\n", ent->s.number, vtos( origin ), vtos( wish ), vtos( vel ),
				  m->moveStatus, m->routeIndex, m->routeLen, tr.startsolid ? " IN SOLID" : "" );
	}
	VectorCopy( vel, m->velocity );
	m->flags &= ~MONF_ONGROUND;
	G_SetOrigin( ent, origin );
}

/*
================
G_MonsterLegsRate

its legs' animation, if it goes over the ground (a walk, a run: "anim_speed"),
played as much faster as it's moving faster than that, so its feet keep to
the ground (as a player's: BG_PlayerLegsRate). The animation's time is moved
so a change doesn't jump its frame
================
*/
static void G_MonsterLegsRate( gentity_t *ent ) {
	const bgModel_t	*model;
	const bgAnim_t	*anim;
	float			speed, rate, old;
	int				q;

	if ( !BG_ValidModel( ent->s.modelindex2 ) ) {
		return;
	}
	model = &bg_models[ent->s.modelindex2];
	if ( ent->s.legsAnim < 0 || ent->s.legsAnim >= model->numAnims ) {
		return;
	}
	anim = &model->anims[ent->s.legsAnim];
	rate = 1.0f;
	speed = sqrt( ent->monster.velocity[0] * ent->monster.velocity[0] + ent->monster.velocity[1] * ent->monster.velocity[1] );
	if ( anim->speed > 0 && anim->loop && speed > 1 ) {
		rate = Com_Clamp( 0.5f, 3.0f, speed / anim->speed );
	}
	q = (int)( rate * 16 + 0.5f );
	if ( q == ( ent->s.frame > 0 ? ent->s.frame : 16 ) ) {
		return;
	}
	old = CHARACTER_LEGS_RATE( &ent->s );
	ent->s.time = level.time - (int)( ( level.time - ent->s.time ) * old / ( q / 16.0f ) );
	ent->s.frame = q == 16 ? 0 : q;
	G_CharacterRecord( ent );
}

/*
================
G_MonsterTurn

toward what it faces, at its turn rate; a flyer pitches with its climb and
rolls with its banking
================
*/
static void G_MonsterTurn( gentity_t *ent, float dt ) {
	monsterState_t	*m = &ent->monster;
	gentity_t		*target = NULL;
	vec3_t			dir, angles;
	float			delta, step;

	switch ( m->faceCommand ) {
		case FACE_ENEMY:
			target = G_MonsterEnemy( ent );
			if ( target ) {
				VectorSubtract( target->r.currentOrigin, ent->r.currentOrigin, dir );
				m->idealYaw = vectoyaw( dir );
			}
			break;
		case FACE_ENTITY:
			if ( m->faceEntity >= 0 && g_entities[ m->faceEntity ].inuse ) {
				VectorSubtract( g_entities[ m->faceEntity ].r.currentOrigin, ent->r.currentOrigin, dir );
				m->idealYaw = vectoyaw( dir );
			}
			break;
		case FACE_MOVE:
			if ( VectorLengthSquared( m->velocity ) > 100 && m->moveStatus == MOVE_STATUS_MOVING ) {
				m->idealYaw = vectoyaw( m->velocity );
			}
			break;
		default:
			break;
	}

	if ( m->flags & MONF_PERCHED ) {
		// its belly to what it's on
		VectorCopy( m->perchAngles, angles );
		VectorCopy( angles, ent->r.currentAngles );
		VectorCopy( angles, ent->s.apos.trBase );
		VectorCopy( angles, ent->s.angles );
		return;
	}

	VectorCopy( ent->r.currentAngles, angles );
	if ( !( m->flags & MONF_DEAD ) ) {
		delta = AngleSubtract( m->idealYaw, angles[YAW] );
		step = m->turnRate * dt;
		if ( delta > step ) {
			delta = step;
		} else if ( delta < -step ) {
			delta = -step;
		}
		angles[YAW] = AngleMod( angles[YAW] + delta );
	}

	if ( m->flags & MONF_FLY ) {
		float pitch = 0;

		if ( !( m->flags & MONF_DEAD ) && VectorLength( m->velocity ) > 10 ) {
			vec3_t va;

			vectoangles( m->velocity, va );
			pitch = Com_Clamp( -30, 30, AngleNormalize180( va[PITCH] ) );
		}
		angles[PITCH] = pitch;
		angles[ROLL] = m->roll;
	} else {
		angles[PITCH] = 0;
		angles[ROLL] = 0;
	}

	VectorCopy( angles, ent->r.currentAngles );
	VectorCopy( angles, ent->s.apos.trBase );
	VectorCopy( angles, ent->s.angles );
}

/*
===============================================================================

its script: Doom 3's states

===============================================================================
*/

/*
================
G_MonsterSetState

Doom 3's idActor::Event_SetState: the state its script goes on with, from
its next think (the same one: it starts it again)
================
*/
void G_MonsterSetState( gentity_t *ent, const char *name ) {
	scriptThread_t *thread;

	Q_strncpyz( ent->monster.idealState, name, sizeof( ent->monster.idealState ) );
	if ( !Q_stricmp( ent->monster.idealState, ent->monster.state ) ) {
		ent->monster.state[0] = '\0';
	}
	thread = Thread_GetThread( ent->monster.thread );
	if ( thread && thread == Thread_CurrentThread() ) {
		thread->interpreter.doneProcessing = qtrue;
	}
}

/*
================
G_MonsterUpdateScript

idActor::UpdateScript: the state its script is in, run until it waits; a
few state changes a frame at most
================
*/
static void G_MonsterUpdateScript( gentity_t *ent ) {
	monsterState_t		*m = &ent->monster;
	scriptThread_t		*thread;
	const function_t	*func;
	int					i;

	thread = Thread_GetThread( m->thread );
	if ( !thread || !ScriptObject_HasObject( &ent->scriptObject ) ) {
		return;
	}

	for ( i = 0; i < MONSTER_MAX_STATES; i++ ) {
		if ( m->idealState[0] && Q_stricmp( m->idealState, m->state ) ) {
			func = ScriptObject_GetFunction( &ent->scriptObject, m->idealState );
			if ( !func ) {
				G_Printf( S_COLOR_YELLOW "WARNING: %s has no state %s\n", ScriptObject_GetTypeName( &ent->scriptObject ), m->idealState );
				m->idealState[0] = '\0';
				break;
			}
			Q_strncpyz( m->state, m->idealState, sizeof( m->state ) );
			Thread_CallObjectFunction( thread, ent, func, qtrue );
		}

		// don't call script until it's done waiting
		if ( Thread_IsWaiting( thread ) ) {
			break;
		}

		Thread_Execute( thread );
		if ( !ent->inuse ) {
			return;		// it removed itself
		}
		if ( !m->idealState[0] || !Q_stricmp( m->idealState, m->state ) ) {
			break;
		}
	}
}

/*
===============================================================================

the entity

===============================================================================
*/

/*
================
G_MonsterThink
================
*/
/*
================
G_MonsterFrameCommands

Doom 3's frame commands, the game's: as its legs' animation passes a frame,
that frame's "melee <damage def>" (at its enemy, if in reach), "weapon
<weapon def>" (from its "muzzle" at its enemy) and "call <function>" (of
its script object). The cgame plays the sounds and effects
================
*/
static void G_MonsterFrameCommands( gentity_t *ent ) {
	monsterState_t		*m = &ent->monster;
	const bgModel_t		*model;
	const bgAnim_t		*a;
	const bgFrameCommand_t *fc;
	vec3_t				offset;
	int					anim = ent->s.legsAnim, frame, from, f, fr, i;

	if ( !BG_ValidModel( ent->s.modelindex2 ) ) {
		return;
	}
	model = &bg_models[ent->s.modelindex2];
	if ( !model->numFrameCommands || anim < 0 || anim >= model->numAnims ) {
		return;
	}
	a = &model->anims[anim];
	if ( a->numFrames <= 0 || a->fps <= 0 ) {
		return;
	}
	frame = ( level.time - ent->s.time ) * a->fps / 1000;
	if ( !a->loop && frame >= a->numFrames ) {
		frame = a->numFrames - 1;
	}

	// a new animation: from its start
	if ( m->fcAnim != anim || m->fcTime != ent->s.time ) {
		m->fcAnim = anim;
		m->fcTime = ent->s.time;
		m->fcFrame = -1;
	}
	from = m->fcFrame;
	if ( frame <= from ) {
		return;
	}
	m->fcFrame = frame;

	for ( f = from + 1; f <= frame; f++ ) {
		fr = f % a->numFrames;
		for ( i = 0; i < model->numFrameCommands; i++ ) {
			fc = &model->frameCommands[i];
			if ( fc->frame != fr || Q_stricmp( fc->anim, a->name ) ) {
				continue;
			}
			if ( !Q_stricmp( fc->command, "melee" ) ) {
				G_MonsterAttackMelee( ent, fc->arg );
			} else if ( !Q_stricmp( fc->command, "weapon" ) ) {
				VectorClear( offset );
				sscanf( G_ScriptGetSpawnArg( ent, "muzzle", "0 0 0" ), "%f %f %f", &offset[0], &offset[1], &offset[2] );
				G_MonsterAttackWeapon( ent, fc->arg, offset );
			} else if ( !Q_stricmp( fc->command, "call" ) ) {
				G_ScriptCallObjectFunction( ent, fc->arg, NULL, NULL );
			}
		}
	}
}

void G_MonsterThink( gentity_t *ent ) {
	monsterState_t	*m = &ent->monster;
	float			dt;
	int				damage;

	ent->nextthink = level.time + 1;
	dt = MAX( level.time - level.previousTime, 1 ) * 0.001f;

	if ( ent->ragdoll ) {
		// limp
		G_RunRagdoll( ent );
		G_MonsterUpdateScript( ent );
		return;
	}

	G_MonsterSense( ent );
	G_MonsterUpdateFields( ent );
	G_MonsterUpdateScript( ent );
	if ( !ent->inuse ) {
		return;
	}
	if ( !( m->flags & MONF_DEAD ) ) {
		G_MonsterFrameCommands( ent );
	}

	// a frame's impact is seen by the next one
	m->flags &= ~( MONF_PAIN | MONF_IMPACT | MONF_LEAP_HIT );

	// in something (another monster flew into it, a mover closed on it): out
	if ( !( m->flags & ( MONF_DEAD | MONF_PERCHED ) ) ) {
		vec3_t origin;

		VectorCopy( ent->r.currentOrigin, origin );
		if ( G_MonsterUnstick( ent, origin ) ) {
			G_SetOrigin( ent, origin );
		}
	}

	if ( m->flags & MONF_FLY && !( m->flags & MONF_DEAD ) ) {
		G_MonsterFlyMove( ent, dt );
	} else {
		G_MonsterGroundMove( ent, dt );
	}
	G_MonsterTurn( ent, dt );
	G_MonsterLegsRate( ent );
	ent->s.pos.trType = TR_INTERPOLATE;		// (G_SetOrigin makes it stationary)
	ent->s.apos.trType = TR_INTERPOLATE;

	// what its crashes did to it
	if ( m->pendingImpactDamage >= 1 && ent->takedamage ) {
		damage = (int)m->pendingImpactDamage;
		m->pendingImpactDamage = 0;
		G_Damage( ent, NULL, NULL, NULL, NULL, damage, DAMAGE_NO_KNOCKBACK, MOD_FALLING );
		if ( !ent->inuse ) {
			return;
		}
	}
	m->pendingImpactDamage = 0;

	G_CharacterRecord( ent );
	G_LinkEntity( ent );
}

/*
================
G_MonsterPain
================
*/
void G_MonsterPain( gentity_t *self, gentity_t *attacker, int damage ) {
	self->monster.flags |= MONF_PAIN;
	self->monster.painTime = level.time;

	// it knows who hurt it
	if ( attacker && attacker->player && self->monster.enemy == ENTITYNUM_NONE ) {
		self->monster.enemy = attacker->s.number;
		VectorCopy( attacker->r.currentOrigin, self->monster.lastEnemyPos );
	}
}

/*
================
G_MonsterDie

Doom 3's idAI::Killed: AI_DEAD, its script's state_Killed, limp if its model
has a figure; a flyer falls
================
*/
void G_MonsterDie( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int meansOfDeath ) {
	monsterState_t *m = &self->monster;

	if ( m->flags & MONF_DEAD ) {
		return;
	}
	m->flags |= MONF_DEAD;
	m->flags &= ~( MONF_NOGRAVITY | MONF_PAIN );
	m->moveStatus = MOVE_STATUS_DONE;
	m->moveCommand = MOVE_NONE;
	self->takedamage = qfalse;
	self->s.contents = CONTENTS_CORPSE;

	if ( G_StartRagdoll( self ) ) {
		self->takedamage = qtrue;		// shots still push it
		self->health = 1 << 30;
	}

	// Daikatana's: what its death triggers, and what it leaves
	{
		const char	*name = G_MonsterKey( self, "deathtarget", "" );
		gentity_t	*t = NULL;
		gitem_t		*item;
		vec3_t		up = { 0, 0, 200 };

		while ( name[0] && ( t = G_Find( t, FOFS( targetname ), name ) ) != NULL ) {
			if ( t != self && t->use ) {
				G_DPrintf( "%s %d dies: uses %d %s\n", self->classname, self->s.number, t->s.number, t->classname );
				t->use( t, self, attacker );
			}
		}
		name = G_MonsterKey( self, "spawnname", "" );
		if ( name[0] && ( item = BG_FindItemByClassname( name ) ) != NULL ) {
			gentity_t *drop = LaunchItem( item, self->r.currentOrigin, up );

			G_DPrintf( "%s %d dies: leaves %s\n", self->classname, self->s.number, name );

			drop->think = NULL;		// it stays, as a map's item
			drop->nextthink = 0;
		}
	}

	G_MonsterUpdateFields( self );
	if ( ScriptObject_GetFunction( &self->scriptObject, "state_Killed" ) ) {
		G_MonsterSetState( self, "state_Killed" );
	} else {
		int anim = BG_ModelAnimForName( &bg_models[ self->s.modelindex2 ], "death" );

		if ( anim >= 0 && !self->ragdoll ) {
			G_CharacterSetAnim( self, ANIMCHANNEL_LEGS, anim );
		}
	}
}

/*
================
G_MonsterTouch

what it runs into while it's thrown through the air (a frog's jump)
================
*/
void G_MonsterTouch( gentity_t *self, gentity_t *other, trace_t *trace ) {
}

/*
================
G_MonsterStartThread

its script's thread, run by its thinks
================
*/
static void G_MonsterStartThread( gentity_t *ent ) {
	scriptThread_t *thread;

	thread = Thread_New();
	Thread_SetThreadName( thread, ent->targetname ? ent->targetname : va( "%s_%d", ent->classname, ent->s.number ) );
	Thread_ManualDelete( thread );
	thread->manualControl = qtrue;
	thread->executeTime = -1;
	thread->interpreter.threadDying = qtrue;		// nothing to run until a state
	ent->monster.thread = thread->threadNum;
}

/*
================
G_MonsterUnstick

out of what it's in (a map's monster a little in a wall or the floor): the
nearest place its box fits, up to 32 away
================
*/
static qboolean G_MonsterUnstick( gentity_t *ent, vec3_t origin ) {
	static const float	dirs[][3] = { { 0, 0, 1 }, { 0, 0, -1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 },
									  { 1, 1, 1 }, { -1, 1, 1 }, { 1, -1, 1 }, { -1, -1, 1 } };
	trace_t				tr;
	vec3_t				p;
	int					d, i;

	int					in;

	G_TraceBox( &tr, origin, ent->monster.moveMins, ent->monster.moveMaxs, origin, ent->s.number, ent->clipmask );
	if ( !tr.startsolid ) {
		return qfalse;
	}
	in = tr.entityNum;
	for ( d = 2; d <= 32; d += 2 ) {
		for ( i = 0; i < (int)ARRAY_LEN( dirs ); i++ ) {
			VectorMA( origin, d, dirs[i], p );
			G_TraceBox( &tr, p, ent->monster.moveMins, ent->monster.moveMaxs, p, ent->s.number, ent->clipmask );
			if ( !tr.startsolid ) {
				G_DPrintf( "%s %d moved %d out of %s at %s\n", ent->classname, ent->s.number, d,
						   in >= 0 && in < ENTITYNUM_MAX_NORMAL ? g_entities[in].classname : "the world", vtos( origin ) );
				VectorCopy( p, origin );
				return qtrue;
			}
		}
	}
	return qfalse;
}

/*
================
SP_monster

a monster of its def ("spawnclass" "monster")
================
*/
void SP_monster( gentity_t *ent ) {
	const bgModel_t	*model;
	monsterState_t	*m = &ent->monster;
	const char		*name;

	name = G_MonsterKey( ent, "model", "" );
	ent->s.modelindex2 = BG_ModelForName( name );
	if ( !BG_ValidModel( ent->s.modelindex2 ) ) {
		G_Printf( S_COLOR_YELLOW "WARNING: %s at %s: no model def \"%s\"\n", ent->classname, vtos( ent->s.origin ), name );
		G_FreeEntity( ent );
		return;
	}
	model = &bg_models[ent->s.modelindex2];

	memset( m, 0, sizeof( *m ) );
	m->flags = MONF_MONSTER;
	m->enemy = ENTITYNUM_NONE;
	m->moveEntity = -1;
	m->faceEntity = -1;
	m->faceCommand = FACE_MOVE;
	m->moveStatus = MOVE_STATUS_DONE;

	ent->s.eType = ET_CHARACTER;
	ent->characterDef = ent->s.modelindex2;
	ent->s.modelindex = G_ModelIndex( (char *)model->mesh );
	ent->characterHistory = G_AllocCharacterHistory();

	// the box it moves with and others meet: its own "mins" and "maxs" (the
	// map's, its def's), else its model def's (what the rigger measured),
	// scaled; and the one shots meet, as far as its model reaches ("hit_mins",
	// "hit_maxs": a skeet's wings), as Doom 3's render model is apart from its
	// clip model (G_TraceShot looks for it)
	ent->s.origin2[0] = G_MonsterFloat( ent, "modelscale", "1" );
	if ( ent->s.origin2[0] <= 0 ) {
		ent->s.origin2[0] = 1;
	}
	if ( G_MonsterKey( ent, "mins", NULL ) && G_MonsterKey( ent, "maxs", NULL ) ) {
		sscanf( G_MonsterKey( ent, "mins", "" ), "%f %f %f", &m->moveMins[0], &m->moveMins[1], &m->moveMins[2] );
		sscanf( G_MonsterKey( ent, "maxs", "" ), "%f %f %f", &m->moveMaxs[0], &m->moveMaxs[1], &m->moveMaxs[2] );
	} else {
		DictGetVector( model->def, "mins", "-16 -16 -24", m->moveMins );
		DictGetVector( model->def, "maxs", "16 16 32", m->moveMaxs );
		VectorScale( m->moveMins, ent->s.origin2[0], m->moveMins );
		VectorScale( m->moveMaxs, ent->s.origin2[0], m->moveMaxs );
	}
	VectorCopy( m->moveMins, ent->s.mins );
	VectorCopy( m->moveMaxs, ent->s.maxs );
	if ( DictFindKey( model->def, "hit_mins" ) && DictFindKey( model->def, "hit_maxs" ) ) {
		vec3_t hitMins, hitMaxs;
		int k;

		DictGetVector( model->def, "hit_mins", "0 0 0", hitMins );
		DictGetVector( model->def, "hit_maxs", "0 0 0", hitMaxs );
		for ( k = 0; k < 3; k++ ) {
			m->hitMins[k] = MIN( m->moveMins[k], hitMins[k] * ent->s.origin2[0] );
			m->hitMaxs[k] = MAX( m->moveMaxs[k], hitMaxs[k] * ent->s.origin2[0] );
		}
		if ( !VectorCompare( m->hitMins, m->moveMins ) || !VectorCompare( m->hitMaxs, m->moveMaxs ) ) {
			m->flags |= MONF_HIT_REACH;
		}
	}
	ent->s.contents = CONTENTS_BODY;
	ent->s.collisionType = CT_AABB;
	ent->clipmask = MASK_PLAYERSOLID | CONTENTS_MONSTERCLIP;

	ent->health = G_MonsterFloat( ent, "health", "100" );
	ent->takedamage = qtrue;
	ent->pain = G_MonsterPain;
	ent->die = G_MonsterDie;
	ent->touch = G_MonsterTouch;

	m->moveSpeed = G_MonsterFloat( ent, "run_speed", "200" );
	m->turnRate = G_MonsterFloat( ent, "turn_rate", "360" );
	m->fov = G_MonsterFloat( ent, "fov", "180" );
	m->sightRange = G_MonsterFloat( ent, "sight_range", "2048" );
	m->impactSpeed = G_MonsterFloat( ent, "impact_speed", "0" );
	m->impactDamage = G_MonsterFloat( ent, "impact_damage", "0" );
	m->impactBounce = G_MonsterFloat( ent, "impact_bounce", "0.5" );
	if ( G_MonsterFloat( ent, "fly", "0" ) ) {
		m->flags |= MONF_FLY | MONF_NOGRAVITY;
		m->flySpeed = G_MonsterFloat( ent, "fly_speed", "200" );
		m->flyAccel = G_MonsterFloat( ent, "fly_accel", "400" );
		m->flyOffset = G_MonsterFloat( ent, "fly_offset", "0" );
		m->flyBob = G_MonsterFloat( ent, "fly_bob", "8" );
	}
	m->idealYaw = ent->s.angles[YAW];
	if ( G_MonsterUnstick( ent, ent->s.origin ) ) {
		G_SetOrigin( ent, ent->s.origin );
	}

	// Daikatana's skill: tougher and faster on hard ("skill_scale" none: 0)
	if ( g_gametype.integer == GT_CAMPAIGN && G_MonsterFloat( ent, "skill_scale", "1" ) ) {
		static const float scale[3] = { 0.75f, 1.0f, 1.25f };
		float s = scale[ (int)Com_Clamp( 0, 2, g_skill.integer ) ];

		ent->health = MAX( 1, (int)( ent->health * s ) );
		m->moveSpeed *= s;
		m->flySpeed *= s;
	}

	G_SetOrigin( ent, ent->s.origin );
	ent->s.pos.trType = TR_INTERPOLATE;
	ent->s.apos.trType = TR_INTERPOLATE;
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorCopy( ent->s.angles, ent->r.currentAngles );

	ent->s.legsAnim = MAX( 0, BG_ModelAnimForName( model, "idle" ) );
	ent->s.time = level.time;

	G_MonsterStartThread( ent );

	ent->think = G_MonsterThink;
	ent->nextthink = level.time + FRAMETIME;
	G_CharacterRecord( ent );
	G_LinkEntity( ent );

	// Daikatana's: the action script it starts with (SPAWN_CallInitFunction)
	name = G_MonsterKey( ent, "aiscript", "" );
	if ( name[0] ) {
		G_RunAiScript( name, ent, qtrue );
	}
}
