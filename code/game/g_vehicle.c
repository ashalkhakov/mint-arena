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
// g_vehicle.c -- Doom 3's vehicles (idAFEntity_Vehicle, its FourWheels and
// SixWheels): an articulated figure (its "ragdoll", g_ragdoll.c, which the
// clients get as a ragdoll's) a player drives. The wheels don't roll: their
// contact motors push them along, their friction holds them across; the
// front ones' hinges steer. USE gets in (g_frob.c) and out.
//
// An entity def with "spawnclass" "vehicle":
//
//	entityDef vehicle_deathtruck {
//		"spawnclass"			"vehicle"
//		"model"					"model_vehicle_deathtruck"	// its "ragdoll" the figure
//		"eyesJoint"				"eyes"			// the driver's view
//		"wheelRadius"			"18"
//		"wheelBodyFrontLeft"	"wheel_fl"		// and FrontRight, (MiddleLeft,
//												// MiddleRight,) RearLeft, RearRight
//		"steeringHingeFrontLeft" "steering_fl"	// and FrontRight; RearLeft and
//												// RearRight steer the other way
//		"snd_start"				"sounds/e1/truckstart.wav"		// getting in: the engine starts,
//		"snd_idle"				"sounds/e1/truckidleloop.wav"	// runs while he's in,
//		"snd_stop"				"sounds/e1/truckshutoff.wav"	// and stops as he gets out
//	}

#include "g_local.h"
#include "bg_af.h"

#define MAX_VEHICLE_WHEELS		6
#define MAX_VEHICLE_STEERING	4
#define VEHICLE_STEER_MSEC		16		// Doom 3's steerSpeed is a frame's, USERCMD_MSEC

typedef struct gVehicle_s {
	int				numWheels;
	afBody_t		*wheels[MAX_VEHICLE_WHEELS];		// left and right, front to back
	afConstraint_t	*steering[MAX_VEHICLE_STEERING];	// front left, front right, rear left, rear right
	float			steerAngle, steerSpeed;
	vec3_t			eyesOffset;		// where the driver sees from, in the first body's frame
	gentity_t		*driver;
	int				sndStart, sndIdle, sndStop;		// the engine's
	qboolean		engineOn;
	float			scriptForward, scriptSteer;		// a script driving it (vehicleDrive), no one in it
} gVehicle_t;

/*
================
G_VehicleEngine

the engine starts (its start sound, then running) or stops
================
*/
void G_VehicleEngine( gentity_t *ent, qboolean on ) {
	gVehicle_t *v = ent->vehicle;

	if ( !v || v->engineOn == on ) {
		return;
	}
	v->engineOn = on;
	if ( on ) {
		if ( v->sndStart ) {
			G_AddEvent( ent, EV_GENERAL_SOUND, v->sndStart );
		}
		ent->s.loopSound = v->sndIdle;
	} else {
		ent->s.loopSound = 0;
		if ( v->sndStop ) {
			G_AddEvent( ent, EV_GENERAL_SOUND, v->sndStop );
		}
	}
}

/*
================
G_VehicleScriptDrive

a script drives it (a cinematic's actors driving off, vehicleDrive):
forward -1 to 1 (back, still, forward), steer degrees (+ to the right), as a
driver's keys would; 0, 0 lets it be
================
*/
void G_VehicleScriptDrive( gentity_t *ent, float forward, float steer ) {
	gVehicle_t *v = ent->vehicle;

	if ( !v ) {
		return;
	}
	v->scriptForward = Com_Clamp( -1.0f, 1.0f, forward );
	v->scriptSteer = Com_Clamp( -30.0f, 30.0f, steer );
	if ( v->scriptForward != 0.0f || v->scriptSteer != 0.0f ) {
		G_RagdollPhysics( ent )->comeToRest = qfalse;
		AF_Activate( G_RagdollPhysics( ent ) );
	} else if ( !v->driver ) {
		G_RagdollPhysics( ent )->comeToRest = qtrue;
	}
}

/*
================
G_VehicleEyes

where the driver sees from: the eyes joint, carried by the first body
================
*/
static void G_VehicleEyes( gentity_t *ent, vec3_t eyes ) {
	const afBody_t *body = &G_RagdollPhysics( ent )->bodies[0];
	int i;

	VectorCopy( body->current->worldOrigin, eyes );
	for ( i = 0; i < 3; i++ ) {
		VectorMA( eyes, ent->vehicle->eyesOffset[i], body->current->worldAxis[i], eyes );
	}
}

/*
================
G_VehicleSeat

a driving player kept where the driver sees from (each think of his, before
his move: PM_FREEZE keeps him there, his view his own)
================
*/
void G_VehicleSeat( gentity_t *player ) {
	gentity_t *vehicle = player->player->driving;
	vec3_t eyes;

	G_VehicleEyes( vehicle, eyes );
	eyes[2] -= player->player->ps.viewheight;
	VectorCopy( eyes, player->player->ps.origin );
	VectorClear( player->player->ps.velocity );
}

/*
================
G_VehicleExitSpot

where the driver can stand getting out: beside his seat along dir (the
truck's left, right, back, front, up), a line from his eyes to it clear (no
wall between), room for him there, on the ground under it; qfalse if
there's none that way
================
*/
static qboolean G_VehicleExitSpot( gentity_t *vehicle, gentity_t *player, const vec3_t eyes, const vec3_t dir, vec3_t spot ) {
	static const float dists[2] = { 64.0f, 96.0f };
	trace_t tr;
	vec3_t end;
	int i;

	for ( i = 0; i < 2; i++ ) {
		VectorMA( eyes, dists[i], dir, spot );
		G_Trace( &tr, eyes, NULL, NULL, spot, vehicle->s.number, MASK_SOLID, TT_AABB );
		if ( tr.fraction < 1.0f ) {
			continue;		// a wall between
		}
		G_Trace( &tr, spot, player->s.mins, player->s.maxs, spot, player->s.number, MASK_PLAYERSOLID, TT_AABB );
		if ( tr.startsolid || tr.allsolid ) {
			continue;		// no room (the truck, the walls)
		}
		VectorCopy( spot, end );
		end[2] -= 128.0f;
		G_Trace( &tr, spot, player->s.mins, player->s.maxs, end, player->s.number, MASK_PLAYERSOLID, TT_AABB );
		if ( tr.fraction < 1.0f ) {
			VectorCopy( tr.endpos, spot );
		}
		return qtrue;
	}
	return qfalse;
}

/*
================
G_VehicleExit

idAFEntity_Vehicle::Use, the driver getting out: beside his seat (the
truck's left, then right, back, front, above), and the truck comes to
rest; with no room anywhere he stays in (unless he has to go: dead,
gone, the truck going)
================
*/
qboolean G_VehicleExit( gentity_t *vehicle, qboolean force ) {
	gentity_t *player = vehicle->vehicle->driver;
	const afBody_t *body = &G_RagdollPhysics( vehicle )->bodies[0];
	static const float sides[5][3] = { { 0, 1, 0 }, { 0, -1, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 } };
	vec3_t eyes, spot, side;
	qboolean found = qfalse;
	int i, k;

	if ( player && player->inuse && player->player ) {
		G_VehicleEyes( vehicle, eyes );
		for ( i = 0; i < 5 && !found; i++ ) {
			// (the truck's own sides: its first body's axes)
			VectorClear( side );
			for ( k = 0; k < 3; k++ ) {
				VectorMA( side, sides[i][k], body->current->worldAxis[k], side );
			}
			found = G_VehicleExitSpot( vehicle, player, eyes, side, spot );
		}
		if ( !found && !force ) {
			trap_SendServerCommand( player - g_entities, "cp \"No room to get out\"" );
			return qfalse;
		}
	}

	vehicle->vehicle->driver = NULL;
	G_RagdollPhysics( vehicle )->comeToRest = qtrue;
	G_VehicleEngine( vehicle, qfalse );
	if ( !player || !player->inuse || !player->player ) {
		return qtrue;
	}
	G_DPrintf( "%s gets out of %s %d at %s\n", player->player->pers.netname, vehicle->classname, vehicle->s.number,
			   vtos( vehicle->r.currentOrigin ) );
	player->player->driving = NULL;
	player->player->ps.stats[STAT_VEHICLE] = 0;
	player->player->ps.eFlags &= ~EF_NODRAW;
	player->s.contents = CONTENTS_BODY;
	if ( !found ) {
		VectorCopy( player->player->ps.origin, spot );		// (forced out: where he sat)
	}
	VectorCopy( spot, player->player->ps.origin );
	VectorClear( player->player->ps.velocity );
	player->player->ps.eFlags ^= EF_TELEPORT_BIT;
	G_SetOrigin( player, spot );
	G_LinkEntity( player );
	return qtrue;
}

/*
================
G_VehicleUse

idAFEntity_Vehicle::Use: a player gets in (it's frobable), the truck wakes
================
*/
static void G_VehicleUse( gentity_t *self, gentity_t *other, gentity_t *activator ) {
	gVehicle_t *v = self->vehicle;

	if ( !activator || !activator->player || v->driver || activator->player->driving
		|| activator->player->ps.stats[STAT_HEALTH] <= 0 ) {
		return;
	}
	v->driver = activator;
	activator->player->driving = self;
	activator->player->ps.stats[STAT_VEHICLE] = self->s.number + 1;
	activator->player->ps.eFlags |= EF_NODRAW;
	activator->s.contents = 0;
	G_LinkEntity( activator );
	G_RagdollPhysics( self )->comeToRest = qfalse;
	AF_Activate( G_RagdollPhysics( self ) );
	v->scriptForward = v->scriptSteer = 0.0f;		// (he drives now)
	G_VehicleEngine( self, qtrue );
	G_DPrintf( "%s gets into %s %d at %s\n", activator->player->pers.netname, self->classname, self->s.number,
			   vtos( self->r.currentOrigin ) );
}

/*
================
G_VehicleSteerAngle

idAFEntity_Vehicle::GetSteerAngle: toward where the driver steers (strafing),
steerSpeed degrees a 16 msec frame at most
================
*/
static float G_VehicleSteerAngle( gVehicle_t *v, int msec ) {
	float ideal, delta, speed = v->steerSpeed * msec / VEHICLE_STEER_MSEC;

	ideal = v->driver ? v->driver->player->pers.cmd.rightmove * ( 30.0f / 128.0f ) : v->scriptSteer;
	delta = ideal - v->steerAngle;
	if ( delta > speed ) {
		v->steerAngle += speed;
	} else if ( delta < -speed ) {
		v->steerAngle -= speed;
	} else {
		v->steerAngle = ideal;
	}
	return v->steerAngle;
}

/*
================
G_VehicleThink

idAFEntity_VehicleSixWheels::Think (FourWheels' for four): the driver's
forward and back the wheels' contact motors (the inner side's at half, as
it turns: no differential), his strafing the front hinges' steering (the
rear ones' the other way), then the figure runs
================
*/
static void G_VehicleThink( gentity_t *ent ) {
	gVehicle_t *v = ent->vehicle;
	float force = 0.0f, velocity = 0.0f, steerAngle = 0.0f;
	int i, msec = MAX( level.time - level.previousTime, 1 );

	if ( v->driver && ( !v->driver->inuse || !v->driver->player || v->driver->player->driving != ent
		|| v->driver->player->ps.stats[STAT_HEALTH] <= 0 ) ) {
		G_VehicleExit( ent, qtrue );
	}
	if ( v->driver ) {
		const usercmd_t *cmd = &v->driver->player->pers.cmd;

		velocity = cmd->forwardmove < 0 ? -g_vehicleVelocity.value : g_vehicleVelocity.value;
		force = fabs( cmd->forwardmove * g_vehicleForce.value ) * ( 1.0f / 128.0f );
		steerAngle = G_VehicleSteerAngle( v, msec );
		G_RagdollPhysics( ent )->comeToRest = qfalse;
		if ( force > 0.0f || steerAngle != 0.0f ) {
			AF_Activate( G_RagdollPhysics( ent ) );
		}
	} else if ( v->scriptForward != 0.0f || v->scriptSteer != 0.0f || v->steerAngle != 0.0f ) {
		// a script's driving it
		velocity = v->scriptForward < 0.0f ? -g_vehicleVelocity.value : g_vehicleVelocity.value;
		force = fabs( v->scriptForward ) * g_vehicleForce.value;
		steerAngle = G_VehicleSteerAngle( v, msec );
		if ( v->scriptForward != 0.0f ) {
			AF_Activate( G_RagdollPhysics( ent ) );
		}
	}

	for ( i = 0; i < v->numWheels; i++ ) {
		v->wheels[i]->contactMotorVelocity = velocity;
		v->wheels[i]->contactMotorForce = force;
	}
	if ( steerAngle < 0.0f ) {
		for ( i = 0; i < v->numWheels; i += 2 ) {
			v->wheels[i]->contactMotorVelocity = velocity * 0.5f;		// the left side's
		}
	} else if ( steerAngle > 0.0f ) {
		for ( i = 1; i < v->numWheels; i += 2 ) {
			v->wheels[i]->contactMotorVelocity = velocity * 0.5f;		// the right side's
		}
	}
	for ( i = 0; i < MAX_VEHICLE_STEERING; i++ ) {
		if ( v->steering[i] ) {
			AF_SetSteerAngle( v->steering[i], i < 2 ? steerAngle : -steerAngle );
			AF_SetSteerSpeed( v->steering[i], 3.0f );
		}
	}

	G_RunRagdoll( ent );
	ent->nextthink = level.time + 1;
}

/*
================
SP_vehicle

idAFEntity_Vehicle::Spawn: its model def's figure, started in the pose the
map puts it in, solid (players walk into its box, not through it)
================
*/
void SP_vehicle( gentity_t *ent ) {
	static const char *wheelKeys[MAX_VEHICLE_WHEELS] = {
		"FrontLeft", "FrontRight", "MiddleLeft", "MiddleRight", "RearLeft", "RearRight" };
	static const char *steeringKeys[MAX_VEHICLE_STEERING] = { "FrontLeft", "FrontRight", "RearLeft", "RearRight" };
	const dict_t *def = EntityDef_Find( ent->classname );
	const bgModel_t *model;
	afPhysics_t *phys;
	gVehicle_t *v;
	float bindPose[MAX_MODEL_JOINTS * 12];
	orientation_t tag;
	vec3_t axis[3], eyes, d;
	const char *name;
	int i, k, joint;

	name = def ? DictGetStringReference( def, "model", "" ) : "";
	ent->s.modelindex2 = BG_ModelForName( name );
	if ( !BG_ValidModel( ent->s.modelindex2 ) ) {
		G_Printf( S_COLOR_YELLOW "WARNING: %s at %s: no model def \"%s\"\n", ent->classname, vtos( ent->s.origin ), name );
		G_FreeEntity( ent );
		return;
	}
	model = &bg_models[ent->s.modelindex2];
	ent->s.eType = ET_CHARACTER;
	ent->characterDef = ent->s.modelindex2;
	ent->s.modelindex = G_ModelIndex( (char *)model->mesh );
	// its size: Daikatana's "scale" (e1m4a's truck, 1.5), or "modelscale"
	if ( !G_SpawnFloat( "scale", "1", &ent->s.origin2[0] ) ) {
		G_SpawnFloat( "modelscale", "1", &ent->s.origin2[0] );
	}
	if ( ent->s.origin2[0] <= 0.0f ) {
		ent->s.origin2[0] = 1.0f;
	}
	ent->s.collisionType = CT_AABB;
	G_SetOrigin( ent, ent->s.origin );
	VectorCopy( ent->s.angles, ent->s.apos.trBase );
	VectorCopy( ent->s.angles, ent->r.currentAngles );
	ent->s.legsAnim = 0;
	ent->s.time = level.time;
	G_LinkEntity( ent );

	if ( !G_StartRagdoll( ent ) ) {
		G_Printf( S_COLOR_YELLOW "WARNING: %s at %s: %s has no figure (\"ragdoll\")\n", ent->classname, vtos( ent->s.origin ), name );
		G_FreeEntity( ent );
		return;
	}
	phys = G_RagdollPhysics( ent );
	ent->s.contents = CONTENTS_SOLID;
	ent->takedamage = qtrue;			// shots push it
	ent->health = 1 << 30;

	v = ent->vehicle = trap_HeapMalloc( sizeof( *v ) );
	memset( v, 0, sizeof( *v ) );
	v->steerSpeed = DictGetFloat( def, "steerSpeed", "5" );
	name = DictGetStringReference( def, "snd_start", "" );
	v->sndStart = name[0] ? G_SoundIndex( (char *)name ) : 0;
	name = DictGetStringReference( def, "snd_idle", "" );
	v->sndIdle = name[0] ? G_SoundIndex( (char *)name ) : 0;
	name = DictGetStringReference( def, "snd_stop", "" );
	v->sndStop = name[0] ? G_SoundIndex( (char *)name ) : 0;
	for ( i = 0; i < MAX_VEHICLE_WHEELS; i++ ) {
		name = DictGetStringReference( def, va( "wheelBody%s", wheelKeys[i] ), "" );
		if ( name[0] && ( v->wheels[v->numWheels] = AF_GetBody( phys, name ) ) ) {
			v->numWheels++;
		} else if ( name[0] ) {
			G_Printf( S_COLOR_YELLOW "WARNING: %s: no wheel body %s\n", ent->classname, name );
		}
	}
	if ( v->numWheels != 4 && v->numWheels != 6 ) {
		G_Printf( S_COLOR_YELLOW "WARNING: %s: %d wheels, a vehicle has 4 or 6\n", ent->classname, v->numWheels );
	}
	for ( i = 0; i < MAX_VEHICLE_STEERING; i++ ) {
		name = DictGetStringReference( def, va( "steeringHinge%s", steeringKeys[i] ), "" );
		if ( name[0] && !( v->steering[i] = AF_GetConstraint( phys, name ) ) ) {
			G_Printf( S_COLOR_YELLOW "WARNING: %s: no steering hinge %s\n", ent->classname, name );
		}
	}

	// the eyes in the first body's frame, as the map puts it
	BG_ModelBindPose( model, bindPose );
	joint = BG_ModelJointIndex( model, DictGetStringReference( def, "eyesJoint", "eyes" ) );
	AnglesToAxis( ent->r.currentAngles, axis );
	VectorCopy( ent->r.currentOrigin, eyes );
	if ( joint >= 0 ) {
		BG_JointOrientation( bindPose, joint, &tag );
		for ( k = 0; k < 3; k++ ) {
			VectorMA( eyes, tag.origin[k] * ent->s.origin2[0], axis[k], eyes );
		}
	}
	VectorSubtract( eyes, phys->bodies[0].current->worldOrigin, d );
	for ( k = 0; k < 3; k++ ) {
		v->eyesOffset[k] = DotProduct( d, phys->bodies[0].current->worldAxis[k] );
	}

	ent->frobable = qtrue;
	ent->use = G_VehicleUse;
	ent->think = G_VehicleThink;
	ent->nextthink = level.time + 1;
	G_LinkEntity( ent );		// (its bodies, now it's a vehicle)
}

/*
================
G_FreeVehicle

a vehicle going: its driver out first
================
*/
void G_FreeVehicle( gentity_t *ent ) {
	if ( !ent->vehicle ) {
		return;
	}
	if ( ent->vehicle->driver && ent->ragdoll ) {
		G_VehicleExit( ent, qtrue );
	} else if ( ent->vehicle->driver && ent->vehicle->driver->player ) {
		ent->vehicle->driver->player->driving = NULL;
		ent->vehicle->driver->player->ps.stats[STAT_VEHICLE] = 0;
	}
	trap_HeapFree( ent->vehicle );
	ent->vehicle = NULL;
}
