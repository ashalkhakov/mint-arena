/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company. 

This file is part of the Doom 3 GPL Source Code (?Doom 3 Source Code?).  

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

#ifndef __COLLISIONMODELMANAGER_H__
#define __COLLISIONMODELMANAGER_H__

#include "../idlib/idlib_public.h"
#include "../idlib/q_extramath.h"
#include "../idlib/mapfile.h"
#include "../idlib/bsp/bsp.h"

/*
===============================================================================

	Trace model vs. polygonal model collision detection.

	Short translations are the least expensive. Retrieving contact points is
	about as cheap as a short translation. Position tests are more expensive
	and rotations are most expensive.

	There is no position test at the start of a translation or rotation. In other
	words if a translation with start != end or a rotation with angle != 0 starts
	in solid, this goes unnoticed and the collision result is undefined.

	A translation with start == end or a rotation with angle == 0 performs
	a position test and fills in the trace_t structure accordingly.

===============================================================================
*/

// contact type
typedef enum {
	CONTACT_NONE,							// no contact
	CONTACT_EDGE,							// trace model edge hits model edge
	CONTACT_MODELVERTEX,					// model vertex hits trace model polygon
	CONTACT_TRMVERTEX						// trace model vertex hits model polygon
} contactType_t;

// contact info
typedef struct {
	contactType_t			type;			// contact type
	vec3_t					point;			// point of contact
	vec3_t					normal;			// contact plane normal
	float					dist;			// contact plane distance
	int						contents;		// contents at other side of surface
	qhandle_t        		material;		// surface material
	int						modelFeature;	// contact feature on model
	int						trmFeature;		// contact feature on trace model
	int						entityNum;		// entity the contact surface is a part of
	int						id;				// id of clip model the contact surface is part of
} contactInfo_t;

// trace result
typedef struct cm_trace_s {
	float					fraction;		// fraction of movement completed, 1.0 = didn't hit anything
	vec3_t					endpos;			// final position of trace model
	vec3_t					endAxis[3];		// final axis of trace model
	contactInfo_t			c;				// contact information, only valid if fraction < 1.0
} cm_trace_t;

typedef int cmHandle_t;

#define CM_CLIP_EPSILON		0.25f			// always stay this distance away from any model
#define CM_BOX_EPSILON		1.0f			// should always be larger than clip epsilon
#define CM_MAX_TRACE_DIST	4096.0f			// maximum distance a trace model may be traced, point traces are unlimited

#define	CM_API_VERSION		1

typedef struct cmimport_s cmimport_t;

//
// these are the functions exported by the CM module
//
typedef struct {
	void	(*Init)( void );
	void	(*Shutdown)( void );

	// Loads collision models from a map file.
	void	(*LoadMap)( const mapFile_t *mapFile );
	void	(*LoadBSP)( const char *fileName, int *checksum );
	// Frees all the collision models.
	void 	(*FreeMap)( void );

	// Gets the clip handle for a model.
	cmHandle_t	(*LoadModel)( const char *modelName, const qboolean precache );
	// Sets up a trace model for collision with other trace models.
	cmHandle_t	(*SetupTrmModel)( const traceModel_t *trm, qhandle_t material );
	// Creates a trace model from a collision model, returns true if succesfull.
	qboolean	(*TrmFromModel)( const char *modelName, traceModel_t *trm );

	// Gets the name of a model.
	const char	*(*GetModelName)( cmHandle_t model );
	// Gets the bounds of a model.
	qboolean	(*GetModelBounds)( cmHandle_t model, vec3_t bounds[2] );
	// Gets all contents flags of brushes and polygons of a model ored together.
	qboolean	(*GetModelContents)( cmHandle_t model, int *contents );
	// Gets a vertex of a model.
	qboolean	(*GetModelVertex)( cmHandle_t model, int vertexNum, vec3_t vertex );
	// Gets an edge of a model.
	qboolean	(*GetModelEdge)( cmHandle_t model, int edgeNum, vec3_t start, vec3_t end );
	// Gets a polygon of a model.
	qboolean	(*GetModelPolygon)( cmHandle_t model, int polygonNum, fixedWinding_t *winding );

	// Translates a trace model and reports the first collision if any.
	void		(*Translation)( cm_trace_t *results, const vec3_t start, const vec3_t end,
									const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
									cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
	// Rotates a trace model and reports the first collision if any.
	void		(*Rotation)( cm_trace_t *results, const vec3_t start, const rotation_t *rotation,
									const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
									cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
	// Returns the contents touched by the trace model or 0 if the trace model is in free space.
	int			(*Contents)( const vec3_t start,
								const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
								cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );
	// Stores all contact points of the trace model with the model, returns the number of contacts.
	int			(*Contacts)( contactInfo_t *contacts, const int maxContacts, const vec3_t start, const vec6_t dir, const float depth,
								const traceModel_t *trm, const vec3_t trmAxis[3], int contentMask,
								cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3] );

	// Tests collision detection.
	void		(*DebugOutput)( const vec3_t origin );
	// Draws a model.
	void		(*DrawModel)( cmHandle_t model, const vec3_t modelOrigin, const vec3_t modelAxis[3],
													const vec3_t viewOrigin, const float radius );
	// Prints model information, use -1 handle for accumulated model info.
	void		(*ModelInfo)( cmHandle_t model );
	// Lists all loaded models.
	void		(*ListModels)( void );
	// Writes a collision model file for the given map entity.
	qboolean	(*WriteCollisionModelForMapEntity)( const mapEntity_t *mapEnt, const char *filename, const qboolean testTraceModel );

	qhandle_t   (*CM_RegisterMaterial)( const char *name );
	void		(*CM_GetMaterialName)( qhandle_t hShader, char *buffer, int bufferSize );
	int			(*CM_GetMaterialContentFlags)( qhandle_t material );
    int         (*CM_GetMaterialSurfaceFlags)( qhandle_t material );

	//
	// PVS (cm1 compatibility)
	//

	int			(*NumClusters) (void);
	int			(*NumInlineModels)( void );
	char		*(*EntityString) (void);
	qboolean	(*GetEntityToken)( int *parseOffset, char *token, int size );

	byte		*(*ClusterPVS) (int cluster);

	int			(*PointLeafnum)( const vec3_t p );

	// only returns non-solid leafs
	// overflow if return listsize and if *lastLeaf != list[listsize-1]
	int			(*BoxLeafnums)( const vec3_t mins, const vec3_t maxs, int *list,
								int listsize, int *lastLeaf );

	int			(*LeafCluster) (int leafnum);
	int			(*LeafArea) (int leafnum);

	void		(*AdjustAreaPortalState)( int area1, int area2, qboolean open );
	qboolean	(*AreasConnected)( int area1, int area2 );

	int			(*WriteAreaBits)( byte *buffer, int area );

} cmexport_t;

//
// these are the functions imported by the CM module
//
typedef struct cmimport_s {
	idlib_import_t	ii;

	cvar_t	*(*Cvar_Get)( const char *name, const char *value, int flags );
	cvar_t	*(*Cvar_Set)( const char *name, const char *value );
	cvar_t	*(*Cvar_SetValue) (const char *name, float value);
	void	(*Cvar_CheckRange)( cvar_t *cv, float minVal, float maxVal, qboolean shouldBeIntegral );
	void	(*Cvar_SetDescription)( cvar_t *cv, const char *description );

	int		(*Cvar_VariableIntegerValue) (const char *var_name);

	void	(*Cvar_VariableStringBuffer) (const char *var_name, char *buffer, int bufsize);

	void	(*Cmd_AddCommand)( const char *name, void(*cmd)(void) );
	void	(*Cmd_RemoveCommand)( const char *name );

	int		(*Cmd_Argc) (void);
	char	*(*Cmd_Argv) (int i);

	void	(*Cmd_ExecuteText) (int exec_when, const char *text);

	void	( *R_DebugClearLines )( int time );
    void    ( *R_DebugLine )( const vec4_t color, const vec3_t start, const vec3_t end, const int lifetime, const qboolean depthTest );
	void	( *R_DebugArrow )( const vec4_t color, const vec3_t start, const vec3_t end, int size, const int lifetime );
	void	( *R_DebugWinding )( const vec4_t color, const fixedWinding_t *w, const vec3_t origin, const vec3_t axis[3], const int lifetime, const qboolean depthTest );
	void	( *R_DebugCircle )( const vec4_t color, const vec3_t origin, const vec3_t dir, const float radius, const int numSteps, const int lifetime, const qboolean depthTest );
	void	( *R_DebugBounds )( const vec4_t color, const vec3_t bounds[2], const vec3_t org, const int lifetime );
	void	( *R_DebugAxis )( const vec3_t origin, const vec3_t axis[3] );

	void	( *R_DebugClearPolygons )( int time );
	void	( *R_DebugPolygon )( const vec4_t color, const fixedWinding_t *winding, const int lifeTime, const qboolean depthTest );

} cmimport_t;

// this is the only function actually exported at the linker level
// If the module can't init to a valid rendering state, NULL will be
// returned.
#ifdef USE_COLLISIONMODEL_DLOPEN
typedef	cmexport_t* (QDECL *GetCollisionModelAPI_t) (int apiVersion, cmimport_t * cimp);
#else
cmexport_t*GetCollisionModelAPI( int apiVersion, cmimport_t *cimp );
#endif

#endif /* !__COLLISIONMODELMANAGER_H__ */
