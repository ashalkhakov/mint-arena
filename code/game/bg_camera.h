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
// bg_camera.h -- RTCW's spline cameras (splines/splines.cpp, idCameraDef):
// a camera path and what it looks at, over time, from cameras/<name>.camera;
// and keyframes, Daikatana's kind of camera path

#define CAM_MAX_POINTS		64		// a spline's control points
#define CAM_MAX_SAMPLES		1024	// its curve, sampled
#define CAM_MAX_TARGETS		8
#define CAM_MAX_EVENTS		32
#define CAM_MAX_VELOCITIES	16
#define CAM_MAX_LOADED		8
#define CAM_MAX_KEYS		64

typedef enum {
	CAMPOS_FIXED,			// one point
	CAMPOS_INTERPOLATED,	// a line from startPos to endPos
	CAMPOS_SPLINE,			// a uniform cubic B-spline (near its control points, not through them)
	CAMPOS_KEYS				// keyframes: through each key's position and angles at its time (Daikatana's)
} camPosKind_t;

// a keyframe: where the camera is at its time and which way it looks, on
// cubic Hermite curves from key to key. A key's velocities are worked out
// from its neighbours (as Daikatana's editor did), unless it gives them;
// its FOV and fade are reached at its time, from the last key's
#define CAMKEY_VEL			1		// vel given (units a second)
#define CAMKEY_VELIN		2		// velIn given: a different one arriving
#define CAMKEY_AVEL			4		// avel given (degrees a second)
#define CAMKEY_AVELIN		8
#define CAMKEY_FOV			16
#define CAMKEY_FADE			32

typedef struct {
	float			time;			// seconds from the start
	vec3_t			pos;
	vec3_t			angles;
	vec3_t			vel, velIn;
	vec3_t			avel, avelIn;
	float			fov;
	vec4_t			fade;			// colour and alpha, 0..1
	int				flags;

	// built: the velocities it leaves and arrives with, its angles turned the short way from the last key's
	vec3_t			outVel, inVel, outAvel, inAvel, unwrapped;
} camKey_t;

// RTCW's idCameraEvent types, by their numbers in the files
typedef enum {
	CAMEV_NA,
	CAMEV_WAIT,				// stops a while: param seconds (it slows into it and out of it)
	CAMEV_TARGETWAIT,
	CAMEV_SPEED,
	CAMEV_TARGET,			// looks at the target of that name from now on
	CAMEV_SNAPTARGET,
	CAMEV_FOV,				// to "fov [seconds]"
	CAMEV_CMD,
	CAMEV_TRIGGER,			// (nothing, as in RTCW)
	CAMEV_STOP,				// the camera ends here
	CAMEV_CAMERA,			// "[n] name": another camera (the game's to play)
	CAMEV_FADEOUT,			// to black over param seconds
	CAMEV_FADEIN,			// from black over param seconds
	CAMEV_FEATHER,			// eases into its move and out of it, a second each
	CAMEV_COUNT
} camEventType_t;

typedef struct {
	float			start;			// ms into its time
	float			duration;		// ms
	float			speed;			// units a second, in place of its base speed
} camVelocity_t;

typedef struct {
	camPosKind_t	kind;
	char			name[32];
	vec3_t			points[CAM_MAX_POINTS];
	int				numPoints;
	float			granularity;	// a spline's sampling, per control point
	float			baseVelocity;	// units a second; 0: its length over its time
	camVelocity_t	velocities[CAM_MAX_VELOCITIES];
	int				numVelocities;
	camKey_t		keys[CAM_MAX_KEYS];
	int				numKeys;
	qboolean		easeIn, easeOut;	// keys: it starts and stops still (unless its ends say how fast)

	// built
	vec3_t			samples[CAM_MAX_SAMPLES];
	float			dist[CAM_MAX_SAMPLES];	// along the curve to each sample
	int				numSamples;
	float			length;
	float			startTime;		// ms into the camera it starts (a target: when it's switched to)
	float			time;			// ms it takes
} camPosition_t;

typedef struct {
	camEventType_t	type;
	float			time;			// ms into the camera
	char			param[64];
} camEvent_t;

typedef struct {
	char			name[MAX_QPATH];
	float			baseTime;		// seconds its path takes, waits aside
	camPosition_t	position;
	camPosition_t	targets[CAM_MAX_TARGETS];
	int				numTargets;
	camEvent_t		events[CAM_MAX_EVENTS];	// in time order
	int				numEvents;
	float			fov, fovStart, fovEnd, fovTime;	// its "fov" block

	// built
	float			totalTime;		// ms, waits and all, to its end or its stop
} cameraDef_t;

cameraDef_t		*BG_LoadCamera( const char *name );
void			BG_FlushCameras( void );
qboolean		BG_ParseCamera( cameraDef_t *cam, const char *name, char *text );
void			BG_BuildCamera( cameraDef_t *cam );
qboolean		BG_CameraView( const cameraDef_t *cam, float time, vec3_t origin, vec3_t angles, float *fov );
qboolean		BG_CameraFade( const cameraDef_t *cam, float time, vec4_t fade );
void			BG_CameraPosition( const camPosition_t *pos, const cameraDef_t *cam, float time, vec3_t out );
int				BG_CameraEvents( const cameraDef_t *cam, float from, float to, const camEvent_t **list, int max );
int				BG_WriteCamera( const cameraDef_t *cam, char *buf, int size );
