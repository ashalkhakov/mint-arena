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
// bg_afdecl.c -- Doom 3's articulated figure files (framework/DeclAF.cpp) and
// idAF (game/AF.cpp): a figure made on a skeletal model's pose, and the pose
// it gives the model back
//
// af/*.af hold "articulatedFigure <name> { settings { } body "x" { } ... }"
// in Doom 3's syntax: vectors are coordinates, joint( "j" ), bonecenter( "a",
// "b" ) or bonedir( "a", "b" ), taken from the model in its "af_pose" (its
// first animation without one). The body on joint "origin" is the base: it
// moves the model's root joint.

#include "../idlib/q_shared.h"
#include "bg_public.h"
#include "bg_af.h"

static afDecl_t		af_decls[AF_MAX_DECLS];
static int			af_numDecls;
static qboolean		af_declsLoaded;

/*
===============================================================================

	A token reader with Doom 3's punctuation

===============================================================================
*/

typedef struct {
	const char	*p;
	const char	*fileName;
	char		token[MAX_TOKEN_CHARS];
	qboolean	unread;
	qboolean	error;
} afLexer_t;

static qboolean AF_ReadToken( afLexer_t *lex ) {
	const char *p = lex->p;
	int len = 0;

	if ( lex->unread ) {
		lex->unread = qfalse;
		return qtrue;
	}

	// skip white space and comments
	for ( ;; ) {
		while ( *p && (unsigned char)*p <= ' ' ) {
			p++;
		}
		if ( p[0] == '/' && p[1] == '/' ) {
			while ( *p && *p != '\n' ) {
				p++;
			}
			continue;
		}
		if ( p[0] == '/' && p[1] == '*' ) {
			p += 2;
			while ( *p && !( p[0] == '*' && p[1] == '/' ) ) {
				p++;
			}
			if ( *p ) {
				p += 2;
			}
			continue;
		}
		break;
	}

	if ( !*p ) {
		lex->p = p;
		lex->token[0] = '\0';
		return qfalse;
	}

	if ( *p == '"' ) {
		p++;
		while ( *p && *p != '"' && len < MAX_TOKEN_CHARS - 1 ) {
			lex->token[len++] = *p++;
		}
		if ( *p == '"' ) {
			p++;
		}
	} else if ( strchr( "(){},;", *p ) || ( *p == '-' && !( ( p[1] >= '0' && p[1] <= '9' ) || p[1] == '.' ) ) ) {
		lex->token[len++] = *p++;
	} else {
		while ( *p && (unsigned char)*p > ' ' && !strchr( "(){},;\"", *p ) && len < MAX_TOKEN_CHARS - 1 ) {
			lex->token[len++] = *p++;
		}
	}
	lex->token[len] = '\0';
	lex->p = p;
	return qtrue;
}

static void AF_UnreadToken( afLexer_t *lex ) {
	lex->unread = qtrue;
}

static void AF_Error( afLexer_t *lex, const char *fmt, const char *arg ) {
	if ( !lex->error ) {
		Com_Printf( S_COLOR_RED "%s: ", lex->fileName );
		Com_Printf( fmt, arg );
		Com_Printf( "\n" );
	}
	lex->error = qtrue;
}

static qboolean AF_Expect( afLexer_t *lex, const char *s ) {
	if ( !AF_ReadToken( lex ) || strcmp( lex->token, s ) ) {
		AF_Error( lex, "expected %s", s );
		return qfalse;
	}
	return qtrue;
}

static qboolean AF_Check( afLexer_t *lex, const char *s ) {
	if ( !AF_ReadToken( lex ) ) {
		return qfalse;
	}
	if ( strcmp( lex->token, s ) ) {
		AF_UnreadToken( lex );
		return qfalse;
	}
	return qtrue;
}

static float AF_ParseFloat( afLexer_t *lex ) {
	float sign = 1.0f;

	if ( !AF_ReadToken( lex ) ) {
		AF_Error( lex, "expected a number%s", "" );
		return 0.0f;
	}
	if ( !strcmp( lex->token, "-" ) ) {
		sign = -1.0f;
		AF_ReadToken( lex );
	}
	return sign * atof( lex->token );
}

static void AF_ParseString( afLexer_t *lex, char *out, int size ) {
	AF_ReadToken( lex );
	Q_strncpyz( out, lex->token, size );
}

/*
================
AF_ParseVector

idAFVector::Parse
================
*/
static qboolean AF_ParseVector( afLexer_t *lex, afVector_t *v ) {
	memset( v, 0, sizeof( *v ) );

	if ( !AF_ReadToken( lex ) ) {
		return qfalse;
	}
	if ( !strcmp( lex->token, "-" ) ) {
		v->negate = qtrue;
		if ( !AF_ReadToken( lex ) ) {
			return qfalse;
		}
	}

	if ( !strcmp( lex->token, "(" ) ) {
		v->type = AFVEC_COORDS;
		v->vec[0] = AF_ParseFloat( lex );
		AF_Expect( lex, "," );
		v->vec[1] = AF_ParseFloat( lex );
		AF_Expect( lex, "," );
		v->vec[2] = AF_ParseFloat( lex );
		AF_Expect( lex, ")" );
	} else if ( !Q_stricmp( lex->token, "joint" ) ) {
		v->type = AFVEC_JOINT;
		AF_Expect( lex, "(" );
		AF_ParseString( lex, v->joint1, sizeof( v->joint1 ) );
		AF_Expect( lex, ")" );
	} else if ( !Q_stricmp( lex->token, "bonecenter" ) || !Q_stricmp( lex->token, "bonedir" ) ) {
		v->type = !Q_stricmp( lex->token, "bonecenter" ) ? AFVEC_BONECENTER : AFVEC_BONEDIR;
		AF_Expect( lex, "(" );
		AF_ParseString( lex, v->joint1, sizeof( v->joint1 ) );
		AF_Expect( lex, "," );
		AF_ParseString( lex, v->joint2, sizeof( v->joint2 ) );
		AF_Expect( lex, ")" );
	} else {
		AF_Error( lex, "unknown token %s in vector", lex->token );
		return qfalse;
	}
	return !lex->error;
}

static void AF_CoordsVector( afVector_t *v, float x, float y, float z ) {
	memset( v, 0, sizeof( *v ) );
	v->type = AFVEC_COORDS;
	VectorSet( v->vec, x, y, z );
}

/*
================
AF_ParseContents

idDeclAF::ContentsFromString
================
*/
static int AF_ParseContents( afLexer_t *lex ) {
	int c = 0;

	while ( AF_ReadToken( lex ) ) {
		if ( !Q_stricmp( lex->token, "none" ) ) {
			c = 0;
		} else if ( !Q_stricmp( lex->token, "solid" ) ) {
			c |= CONTENTS_SOLID;
		} else if ( !Q_stricmp( lex->token, "body" ) ) {
			c |= CONTENTS_BODY;
		} else if ( !Q_stricmp( lex->token, "corpse" ) ) {
			c |= CONTENTS_CORPSE;
		} else if ( !Q_stricmp( lex->token, "playerclip" ) ) {
			c |= CONTENTS_PLAYERCLIP;
		} else if ( !Q_stricmp( lex->token, "monsterclip" ) ) {
			c |= CONTENTS_MONSTERCLIP;
		} else {
			AF_UnreadToken( lex );
			break;
		}
		if ( !AF_Check( lex, "," ) ) {
			break;
		}
	}
	return c;
}

static void AF_DeclBodyDefault( const afDecl_t *decl, afDeclBody_t *body ) {
	memset( body, 0, sizeof( *body ) );
	Q_strncpyz( body->name, "noname", sizeof( body->name ) );
	body->modelType = TRM_BOX;
	AF_CoordsVector( &body->v1, -10, -10, -10 );
	AF_CoordsVector( &body->v2, 10, 10, 10 );
	body->numSides = 3;
	AF_CoordsVector( &body->origin, 0, 0, 0 );
	body->density = 0.2f;
	AxisClear( body->inertiaScale );
	body->linearFriction = decl->defaultLinearFriction;
	body->angularFriction = decl->defaultAngularFriction;
	body->contactFriction = decl->defaultContactFriction;
	body->contents = decl->contents;
	body->clipMask = decl->clipMask;
	body->selfCollision = decl->selfCollision;
	AF_CoordsVector( &body->frictionDirection, 0, 0, 0 );
	AF_CoordsVector( &body->contactMotorDirection, 0, 0, 0 );
	Q_strncpyz( body->jointName, "origin", sizeof( body->jointName ) );
	body->jointMod = AF_JOINTMOD_AXIS;
	Q_strncpyz( body->containedJoints, "*origin", sizeof( body->containedJoints ) );
}

static void AF_DeclConstraintDefault( const afDecl_t *decl, afDeclConstraint_t *c ) {
	memset( c, 0, sizeof( *c ) );
	Q_strncpyz( c->name, "noname", sizeof( c->name ) );
	c->type = CONSTRAINT_UNIVERSALJOINT;
	Q_strncpyz( c->body1, decl->numBodies ? decl->bodies[0].name : "world", sizeof( c->body1 ) );
	Q_strncpyz( c->body2, "world", sizeof( c->body2 ) );
	c->friction = decl->defaultConstraintFriction;
	AF_CoordsVector( &c->anchor, 0, 0, 0 );
	AF_CoordsVector( &c->anchor2, 0, 0, 0 );
	AF_CoordsVector( &c->axis, 1, 0, 0 );
	AF_CoordsVector( &c->shaft[0], 0, 0, -1 );
	AF_CoordsVector( &c->shaft[1], 0, 0, 1 );
	c->limit = AF_LIMIT_NONE;
	AF_CoordsVector( &c->limitAxis, 0, 0, -1 );
}

/*
================
AF_ParseBody

idDeclAF::ParseBody
================
*/
static qboolean AF_ParseBody( afLexer_t *lex, afDecl_t *decl ) {
	afDeclBody_t *body;
	qboolean hasJoint = qfalse;
	afVector_t angles;

	if ( decl->numBodies >= AF_MAX_BODIES ) {
		AF_Error( lex, "more than %s bodies", va( "%d", AF_MAX_BODIES ) );
		return qfalse;
	}
	body = &decl->bodies[decl->numBodies++];
	AF_DeclBodyDefault( decl, body );

	AF_ParseString( lex, body->name, sizeof( body->name ) );
	if ( !Q_stricmp( body->name, "origin" ) || !Q_stricmp( body->name, "world" ) ) {
		AF_Error( lex, "a body may not be named \"%s\"", body->name );
		return qfalse;
	}
	if ( !AF_Expect( lex, "{" ) ) {
		return qfalse;
	}

	while ( AF_ReadToken( lex ) && !lex->error ) {
		if ( !Q_stricmp( lex->token, "model" ) ) {
			AF_ReadToken( lex );
			if ( !Q_stricmp( lex->token, "box" ) || !Q_stricmp( lex->token, "octahedron" ) || !Q_stricmp( lex->token, "dodecahedron" ) ) {
				body->modelType = !Q_stricmp( lex->token, "box" ) ? TRM_BOX : !Q_stricmp( lex->token, "octahedron" ) ? TRM_OCTAHEDRON : TRM_DODECAHEDRON;
				AF_Expect( lex, "(" );
				AF_ParseVector( lex, &body->v1 );
				AF_Expect( lex, "," );
				AF_ParseVector( lex, &body->v2 );
				AF_Expect( lex, ")" );
			} else if ( !Q_stricmp( lex->token, "cylinder" ) || !Q_stricmp( lex->token, "cone" ) ) {
				body->modelType = !Q_stricmp( lex->token, "cylinder" ) ? TRM_CYLINDER : TRM_CONE;
				AF_Expect( lex, "(" );
				AF_ParseVector( lex, &body->v1 );
				AF_Expect( lex, "," );
				AF_ParseVector( lex, &body->v2 );
				AF_Expect( lex, "," );
				body->numSides = AF_ParseFloat( lex );
				AF_Expect( lex, ")" );
			} else if ( !Q_stricmp( lex->token, "bone" ) ) {
				body->modelType = TRM_BONE;
				AF_Expect( lex, "(" );
				AF_ParseVector( lex, &body->v1 );
				AF_Expect( lex, "," );
				AF_ParseVector( lex, &body->v2 );
				AF_Expect( lex, "," );
				body->width = AF_ParseFloat( lex );
				AF_Expect( lex, ")" );
			} else {
				AF_Error( lex, "unknown model type %s", lex->token );
				return qfalse;
			}
		} else if ( !Q_stricmp( lex->token, "origin" ) ) {
			AF_ParseVector( lex, &body->origin );
		} else if ( !Q_stricmp( lex->token, "angles" ) ) {
			AF_ParseVector( lex, &angles );
			VectorCopy( angles.vec, body->angles );
		} else if ( !Q_stricmp( lex->token, "joint" ) ) {
			AF_ParseString( lex, body->jointName, sizeof( body->jointName ) );
			hasJoint = qtrue;
		} else if ( !Q_stricmp( lex->token, "mod" ) ) {
			AF_ReadToken( lex );
			if ( !Q_stricmp( lex->token, "position" ) ) {
				body->jointMod = AF_JOINTMOD_ORIGIN;
			} else if ( !Q_stricmp( lex->token, "both" ) ) {
				body->jointMod = AF_JOINTMOD_BOTH;
			} else {
				body->jointMod = AF_JOINTMOD_AXIS;
			}
		} else if ( !Q_stricmp( lex->token, "density" ) ) {
			body->density = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "inertiaScale" ) ) {
			int i;
			AF_Expect( lex, "(" );
			for ( i = 0; i < 9; i++ ) {
				body->inertiaScale[i / 3][i % 3] = AF_ParseFloat( lex );
			}
			AF_Expect( lex, ")" );
		} else if ( !Q_stricmp( lex->token, "friction" ) ) {
			body->linearFriction = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			body->angularFriction = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			body->contactFriction = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "contents" ) ) {
			body->contents = AF_ParseContents( lex );
		} else if ( !Q_stricmp( lex->token, "clipMask" ) ) {
			body->clipMask = AF_ParseContents( lex );
		} else if ( !Q_stricmp( lex->token, "selfCollision" ) ) {
			body->selfCollision = AF_ParseFloat( lex ) != 0.0f;
		} else if ( !Q_stricmp( lex->token, "containedjoints" ) ) {
			AF_ParseString( lex, body->containedJoints, sizeof( body->containedJoints ) );
		} else if ( !Q_stricmp( lex->token, "frictionDirection" ) ) {
			AF_ParseVector( lex, &body->frictionDirection );
		} else if ( !Q_stricmp( lex->token, "contactMotorDirection" ) ) {
			AF_ParseVector( lex, &body->contactMotorDirection );
		} else if ( !strcmp( lex->token, "}" ) ) {
			break;
		} else {
			AF_Error( lex, "unknown token %s in body", lex->token );
			return qfalse;
		}
	}

	if ( !hasJoint ) {
		AF_Error( lex, "no joint set for body %s", body->name );
		return qfalse;
	}
	return !lex->error;
}

/*
================
AF_ParseConstraint

idDeclAF::ParseFixed, ParseBallAndSocketJoint, ParseUniversalJoint, ParseHinge
================
*/
static qboolean AF_ParseConstraint( afLexer_t *lex, afDecl_t *decl, afConstraintType_t type ) {
	afDeclConstraint_t *c;

	if ( decl->numConstraints >= AF_MAX_CONSTRAINTS ) {
		AF_Error( lex, "more than %s constraints", va( "%d", AF_MAX_CONSTRAINTS ) );
		return qfalse;
	}
	c = &decl->constraints[decl->numConstraints++];
	AF_DeclConstraintDefault( decl, c );

	AF_ParseString( lex, c->name, sizeof( c->name ) );
	if ( !AF_Expect( lex, "{" ) ) {
		return qfalse;
	}

	c->type = type;
	if ( type != CONSTRAINT_FIXED ) {
		c->limit = AF_LIMIT_NONE;
		c->friction = 0.5f;
		AF_CoordsVector( &c->anchor, 0, 0, 0 );
		AF_CoordsVector( &c->shaft[0], 0, 0, 0 );
		AF_CoordsVector( &c->shaft[1], 0, 0, 0 );
		if ( type == CONSTRAINT_HINGE ) {
			AF_CoordsVector( &c->axis, 0, 0, 0 );
		}
	}

	while ( AF_ReadToken( lex ) && !lex->error ) {
		if ( !Q_stricmp( lex->token, "body1" ) ) {
			AF_ParseString( lex, c->body1, sizeof( c->body1 ) );
		} else if ( !Q_stricmp( lex->token, "body2" ) ) {
			AF_ParseString( lex, c->body2, sizeof( c->body2 ) );
		} else if ( !strcmp( lex->token, "}" ) ) {
			break;
		} else if ( type == CONSTRAINT_FIXED ) {
			AF_Error( lex, "unknown token %s in fixed constraint", lex->token );
			return qfalse;
		} else if ( !Q_stricmp( lex->token, "anchor" ) ) {
			AF_ParseVector( lex, &c->anchor );
		} else if ( !Q_stricmp( lex->token, "friction" ) ) {
			c->friction = AF_ParseFloat( lex );
		} else if ( type == CONSTRAINT_UNIVERSALJOINT && !Q_stricmp( lex->token, "shafts" ) ) {
			AF_ParseVector( lex, &c->shaft[0] );
			AF_Expect( lex, "," );
			AF_ParseVector( lex, &c->shaft[1] );
		} else if ( type != CONSTRAINT_HINGE && !Q_stricmp( lex->token, "conelimit" ) ) {
			AF_ParseVector( lex, &c->limitAxis );
			AF_Expect( lex, "," );
			c->limitAngles[0] = AF_ParseFloat( lex );
			if ( type == CONSTRAINT_BALLANDSOCKETJOINT ) {
				AF_Expect( lex, "," );
				AF_ParseVector( lex, &c->shaft[0] );
			}
			c->limit = AF_LIMIT_CONE;
		} else if ( type != CONSTRAINT_HINGE && !Q_stricmp( lex->token, "pyramidlimit" ) ) {
			AF_ParseVector( lex, &c->limitAxis );
			AF_Expect( lex, "," );
			c->limitAngles[0] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			c->limitAngles[1] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			c->limitAngles[2] = AF_ParseFloat( lex );
			if ( type == CONSTRAINT_BALLANDSOCKETJOINT ) {
				AF_Expect( lex, "," );
				AF_ParseVector( lex, &c->shaft[0] );
			}
			c->limit = AF_LIMIT_PYRAMID;
		} else if ( type == CONSTRAINT_HINGE && !Q_stricmp( lex->token, "axis" ) ) {
			AF_ParseVector( lex, &c->axis );
		} else if ( type == CONSTRAINT_HINGE && !Q_stricmp( lex->token, "limit" ) ) {
			c->limitAngles[0] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			c->limitAngles[1] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			c->limitAngles[2] = AF_ParseFloat( lex );
			c->limit = AF_LIMIT_CONE;
		} else {
			AF_Error( lex, "unknown token %s in constraint", lex->token );
			return qfalse;
		}
	}
	return !lex->error;
}

/*
================
AF_ParseSettings
================
*/
static qboolean AF_ParseSettings( afLexer_t *lex, afDecl_t *decl ) {
	char unused[MAX_QPATH];

	if ( !AF_Expect( lex, "{" ) ) {
		return qfalse;
	}

	while ( AF_ReadToken( lex ) && !lex->error ) {
		if ( !Q_stricmp( lex->token, "mesh" ) || !Q_stricmp( lex->token, "anim" ) || !Q_stricmp( lex->token, "model" )
			|| !Q_stricmp( lex->token, "skin" ) ) {
			AF_ParseString( lex, unused, sizeof( unused ) );
		} else if ( !Q_stricmp( lex->token, "friction" ) ) {
			decl->defaultLinearFriction = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			decl->defaultAngularFriction = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			decl->defaultContactFriction = AF_ParseFloat( lex );
			if ( AF_Check( lex, "," ) ) {
				decl->defaultConstraintFriction = AF_ParseFloat( lex );
			}
		} else if ( !Q_stricmp( lex->token, "totalMass" ) ) {
			decl->totalMass = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "suspendSpeed" ) ) {
			decl->suspendVelocity[0] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			decl->suspendVelocity[1] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			decl->suspendAcceleration[0] = AF_ParseFloat( lex );
			AF_Expect( lex, "," );
			decl->suspendAcceleration[1] = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "noMoveTime" ) ) {
			decl->noMoveTime = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "noMoveTranslation" ) ) {
			decl->noMoveTranslation = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "noMoveRotation" ) ) {
			decl->noMoveRotation = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "minMoveTime" ) ) {
			decl->minMoveTime = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "maxMoveTime" ) ) {
			decl->maxMoveTime = AF_ParseFloat( lex );
		} else if ( !Q_stricmp( lex->token, "contents" ) ) {
			decl->contents = AF_ParseContents( lex );
		} else if ( !Q_stricmp( lex->token, "clipMask" ) ) {
			decl->clipMask = AF_ParseContents( lex );
		} else if ( !Q_stricmp( lex->token, "selfCollision" ) ) {
			decl->selfCollision = AF_ParseFloat( lex ) != 0.0f;
		} else if ( !strcmp( lex->token, "}" ) ) {
			break;
		} else {
			AF_Error( lex, "unknown token %s in settings", lex->token );
			return qfalse;
		}
	}
	return !lex->error;
}

/*
================
AF_ParseDecl

idDeclAF::Parse, after "articulatedFigure <name>"
================
*/
static qboolean AF_ParseDecl( afLexer_t *lex, afDecl_t *decl ) {
	int i;

	// idDeclAF::FreeData's defaults
	decl->defaultLinearFriction = 0.01f;
	decl->defaultAngularFriction = 0.01f;
	decl->defaultContactFriction = 0.8f;
	decl->defaultConstraintFriction = 0.5f;
	decl->totalMass = -1;
	decl->suspendVelocity[0] = 20.0f;
	decl->suspendVelocity[1] = 30.0f;
	decl->suspendAcceleration[0] = 40.0f;
	decl->suspendAcceleration[1] = 60.0f;
	decl->noMoveTime = 1.0f;
	decl->noMoveTranslation = 10.0f;
	decl->noMoveRotation = 10.0f;
	decl->minMoveTime = -1.0f;
	decl->maxMoveTime = -1.0f;
	decl->selfCollision = qtrue;
	decl->contents = CONTENTS_CORPSE;
	decl->clipMask = CONTENTS_SOLID | CONTENTS_CORPSE;

	if ( !AF_Expect( lex, "{" ) ) {
		return qfalse;
	}

	while ( AF_ReadToken( lex ) && !lex->error ) {
		if ( !Q_stricmp( lex->token, "settings" ) ) {
			AF_ParseSettings( lex, decl );
		} else if ( !Q_stricmp( lex->token, "body" ) ) {
			AF_ParseBody( lex, decl );
		} else if ( !Q_stricmp( lex->token, "fixed" ) ) {
			AF_ParseConstraint( lex, decl, CONSTRAINT_FIXED );
		} else if ( !Q_stricmp( lex->token, "ballAndSocketJoint" ) ) {
			AF_ParseConstraint( lex, decl, CONSTRAINT_BALLANDSOCKETJOINT );
		} else if ( !Q_stricmp( lex->token, "universalJoint" ) ) {
			AF_ParseConstraint( lex, decl, CONSTRAINT_UNIVERSALJOINT );
		} else if ( !Q_stricmp( lex->token, "hinge" ) ) {
			AF_ParseConstraint( lex, decl, CONSTRAINT_HINGE );
		} else if ( !strcmp( lex->token, "}" ) ) {
			break;
		} else {
			AF_Error( lex, "unknown keyword %s (only ragdoll constraints are supported)", lex->token );
		}
	}
	if ( lex->error ) {
		return qfalse;
	}

	// make sure the body which modifies the origin comes first
	for ( i = 0; i < decl->numBodies; i++ ) {
		if ( !Q_stricmp( decl->bodies[i].jointName, "origin" ) ) {
			if ( i != 0 ) {
				afDeclBody_t b = decl->bodies[0];
				decl->bodies[0] = decl->bodies[i];
				decl->bodies[i] = b;
			}
			break;
		}
	}
	return qtrue;
}

static void AF_ParseFile( const char *fileName, const char *text ) {
	afLexer_t lex;

	memset( &lex, 0, sizeof( lex ) );
	lex.p = text;
	lex.fileName = fileName;

	while ( AF_ReadToken( &lex ) ) {
		if ( Q_stricmp( lex.token, "articulatedFigure" ) ) {
			AF_Error( &lex, "expected articulatedFigure, found %s", lex.token );
			return;
		}
		if ( af_numDecls >= AF_MAX_DECLS ) {
			Com_Printf( S_COLOR_YELLOW "%s: more than %d articulated figures\n", fileName, AF_MAX_DECLS );
			return;
		}
		memset( &af_decls[af_numDecls], 0, sizeof( af_decls[0] ) );
		AF_ParseString( &lex, af_decls[af_numDecls].name, sizeof( af_decls[0].name ) );
		if ( !AF_ParseDecl( &lex, &af_decls[af_numDecls] ) ) {
			return;
		}
		af_numDecls++;
	}
}

static void AF_LoadDecls( void ) {
	char fileList[8192], fileName[MAX_QPATH], *name;
	static char text[65536];
	fileHandle_t f;
	int numFiles, i, len;

	af_declsLoaded = qtrue;
	af_numDecls = 0;

	numFiles = trap_FS_GetFileList( "af", ".af", fileList, sizeof( fileList ) );
	name = fileList;
	for ( i = 0; i < numFiles; i++, name += strlen( name ) + 1 ) {
		Com_sprintf( fileName, sizeof( fileName ), "af/%s", name );
		len = trap_FS_FOpenFile( fileName, &f, FS_READ );
		if ( len <= 0 ) {
			continue;
		}
		if ( len >= (int)sizeof( text ) ) {
			Com_Printf( S_COLOR_YELLOW "%s is too long\n", fileName );
			trap_FS_FCloseFile( f );
			continue;
		}
		trap_FS_Read( text, len, f );
		text[len] = '\0';
		trap_FS_FCloseFile( f );
		AF_ParseFile( fileName, text );
	}
}

/*
================
BG_FindAFDecl

an articulated figure of the files in af/
================
*/
const afDecl_t *BG_FindAFDecl( const char *name ) {
	int i;

	if ( !af_declsLoaded ) {
		AF_LoadDecls();
	}
	for ( i = 0; i < af_numDecls; i++ ) {
		if ( !Q_stricmp( af_decls[i].name, name ) ) {
			return &af_decls[i];
		}
	}
	return NULL;
}

/*
===============================================================================

	idAF

===============================================================================
*/

// a joint of a pose (3x4 rows, the joint's axes in the columns) as Doom 3's
// origin and axis (the axes in the rows)
static void AF_JointTransform( const float *jointMats, int joint, vec3_t origin, vec3_t axis[3] ) {
	const float *m = &jointMats[12 * joint];
	int r, c;

	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			axis[c][r] = m[r * 4 + c];
		}
		origin[r] = m[r * 4 + 3];
	}
}

static void AF_SetJointTransform( float *jointMats, int joint, const vec3_t origin, const vec3_t axis[3] ) {
	float *m = &jointMats[12 * joint];
	int r, c;

	for ( r = 0; r < 3; r++ ) {
		for ( c = 0; c < 3; c++ ) {
			m[r * 4 + c] = axis[c][r];
		}
		m[r * 4 + 3] = origin[r];
	}
}

// v * M, M * v, a * b, a' * b, a * b' with Doom 3's meaning
static ID_INLINE void AFD_VectorMat( const vec3_t v, const vec3_t m[3], vec3_t out ) {
	vec3_t t;

	t[0] = m[0][0] * v[0] + m[1][0] * v[1] + m[2][0] * v[2];
	t[1] = m[0][1] * v[0] + m[1][1] * v[1] + m[2][1] * v[2];
	t[2] = m[0][2] * v[0] + m[1][2] * v[1] + m[2][2] * v[2];
	VectorCopy( t, out );
}

static ID_INLINE void AFD_VectorMatT( const vec3_t v, const vec3_t m[3], vec3_t out ) {
	vec3_t t;

	t[0] = DotProduct( m[0], v );
	t[1] = DotProduct( m[1], v );
	t[2] = DotProduct( m[2], v );
	VectorCopy( t, out );
}

static void AFD_Mat3Mul( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	vec3_t t[3];
	int i, j;

	for ( i = 0; i < 3; i++ ) {
		for ( j = 0; j < 3; j++ ) {
			t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j];
		}
	}
	AxisCopy( t, out );
}

static void AFD_Mat3TransposeMul( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	vec3_t t[3];

	TransposeAxis( a, t );
	AFD_Mat3Mul( (const vec3_t *)t, b, out );
}

static void AFD_Mat3MulTranspose( const vec3_t a[3], const vec3_t b[3], vec3_t out[3] ) {
	vec3_t t[3];

	TransposeAxis( b, t );
	AFD_Mat3Mul( a, (const vec3_t *)t, out );
}

// the joint a decl names: "origin" is the model's root
static int AF_JointIndex( const bgModel_t *model, const char *name ) {
	int i;

	if ( !Q_stricmp( name, "origin" ) ) {
		for ( i = 0; i < model->numJoints; i++ ) {
			if ( model->joints[i].parent < 0 ) {
				return i;
			}
		}
		return -1;
	}
	return BG_ModelJointIndex( model, name );
}

/*
================
AF_ScalePose

a pose's joints this much further apart: the scaled model's
================
*/
static void AF_ScalePose( float *jointMats, int numJoints, float scale ) {
	int j, r;

	if ( scale == 1.0f ) {
		return;
	}
	for ( j = 0; j < numJoints; j++ ) {
		for ( r = 0; r < 3; r++ ) {
			jointMats[12 * j + r * 4 + 3] *= scale;
		}
	}
}

/*
================
AF_FinishVector

idAFVector::Finish, in the AF pose
================
*/
static void AF_FinishVector( const afFigure_t *af, const afVector_t *v, vec3_t out ) {
	vec3_t start, end, axis[3];
	int j1, j2;

	switch ( v->type ) {
		case AFVEC_COORDS:
			// (the joints' are in the scaled pose already)
			VectorScale( v->vec, af->scale, out );
			break;
		case AFVEC_JOINT:
			j1 = AF_JointIndex( af->model, v->joint1 );
			if ( j1 < 0 ) {
				Com_Printf( S_COLOR_YELLOW "invalid joint %s in joint() in '%s'\n", v->joint1, af->decl->name );
				VectorClear( out );
				break;
			}
			AF_JointTransform( af->afPose, j1, out, axis );
			break;
		case AFVEC_BONECENTER:
		case AFVEC_BONEDIR:
			j1 = AF_JointIndex( af->model, v->joint1 );
			j2 = AF_JointIndex( af->model, v->joint2 );
			if ( j1 < 0 || j2 < 0 ) {
				Com_Printf( S_COLOR_YELLOW "invalid joint %s or %s in '%s'\n", v->joint1, v->joint2, af->decl->name );
				VectorClear( out );
				break;
			}
			AF_JointTransform( af->afPose, j1, start, axis );
			AF_JointTransform( af->afPose, j2, end, axis );
			if ( v->type == AFVEC_BONECENTER ) {
				VectorAdd( start, end, out );
				VectorScale( out, 0.5f, out );
			} else {
				VectorSubtract( end, start, out );
			}
			break;
		default:
			VectorClear( out );
			break;
	}
	if ( v->negate ) {
		VectorNegate( out, out );
	}
}

/*
================
AF_AddJointMod

idAF::AddBody: the body relative to its joint in the AF pose
================
*/
static void AF_AddJointMod( afFigure_t *af, afBody_t *body, int joint, afJointMod_t mod ) {
	afJointModInfo_t *jm;
	vec3_t origin, axis[3], d;

	if ( af->numJointMods >= AF_MAX_BODIES ) {
		return;
	}
	AF_JointTransform( af->afPose, joint, origin, axis );

	jm = &af->jointMods[af->numJointMods++];
	jm->bodyId = body->id;
	jm->joint = joint;
	jm->jointMod = mod;
	VectorSubtract( body->current->worldOrigin, origin, d );
	AFD_VectorMatT( d, (const vec3_t *)axis, jm->jointBodyOrigin );
	AFD_Mat3MulTranspose( (const vec3_t *)body->current->worldAxis, (const vec3_t *)axis, jm->jointBodyAxis );
}

/*
================
AF_LoadBody

idAF::LoadBody
================
*/
static qboolean AF_LoadBody( afFigure_t *af, const afDeclBody_t *fb ) {
	traceModel_t trm;
	vec3_t origin, axis[3], bounds[2], centerOfMass, inertiaTensor[3], t;
	float length, mass;
	afBody_t *body;
	int joint;
	qboolean scaled;

	AF_FinishVector( af, &fb->origin, origin );
	AnglesToAxis( fb->angles, axis );
	AF_FinishVector( af, &fb->v1, bounds[0] );
	AF_FinishVector( af, &fb->v2, bounds[1] );

	memset( &trm, 0, sizeof( trm ) );
	trm.type = TRM_INVALID;

	switch ( fb->modelType ) {
		case TRM_BOX:
			TraceModelSetupBox( &trm, (const vec3_t *)bounds );
			break;
		case TRM_OCTAHEDRON:
			TraceModelSetupOctahedron( &trm, (const vec3_t *)bounds );
			break;
		case TRM_DODECAHEDRON:
			TraceModelSetupDodecahedron( &trm, (const vec3_t *)bounds );
			break;
		case TRM_CYLINDER:
			TraceModelSetupCylinder( &trm, (const vec3_t *)bounds, fb->numSides );
			break;
		case TRM_CONE:
			// place the apex at the origin
			bounds[0][2] -= bounds[1][2];
			bounds[1][2] = 0.0f;
			TraceModelSetupCone( &trm, (const vec3_t *)bounds, fb->numSides );
			break;
		case TRM_BONE:
			// direction of bone
			VectorSubtract( bounds[1], bounds[0], axis[2] );
			length = VectorNormalize( axis[2] );
			// axis of bone trace model
			NormalVectors( axis[2], axis[0], axis[1] );
			VectorNegate( axis[1], axis[1] );
			// create bone trace model
			TraceModelSetupBone( &trm, length, fb->width * af->scale );
			break;
		default:
			return qfalse;
	}

	TraceModelGetMassProperties( &trm, 1.0f, &mass, centerOfMass, inertiaTensor );
	VectorNegate( centerOfMass, t );
	TraceModelTranslate( &trm, t );
	AFD_VectorMat( centerOfMass, (const vec3_t *)axis, t );
	VectorAdd( origin, t, origin );

	scaled = !( fb->inertiaScale[0][0] == 1.0f && fb->inertiaScale[1][1] == 1.0f && fb->inertiaScale[2][2] == 1.0f
				&& fb->inertiaScale[0][1] == 0.0f && fb->inertiaScale[0][2] == 0.0f && fb->inertiaScale[1][0] == 0.0f
				&& fb->inertiaScale[1][2] == 0.0f && fb->inertiaScale[2][0] == 0.0f && fb->inertiaScale[2][1] == 0.0f );

	body = AF_AddBody( &af->physics, fb->name, &trm, fb->density, origin, (const vec3_t *)axis,
						scaled ? (const vec3_t *)fb->inertiaScale : NULL );
	if ( !body ) {
		return qfalse;
	}
	body->contents = fb->contents;

	if ( fb->linearFriction != -1.0f ) {
		AF_SetBodyFriction( body, fb->linearFriction, fb->angularFriction, fb->contactFriction );
	}
	body->clipMask = fb->clipMask;
	body->clipMaskSet = qtrue;
	body->selfCollision = fb->selfCollision;

	joint = AF_JointIndex( af->model, fb->jointName );
	if ( joint < 0 ) {
		Com_Printf( S_COLOR_YELLOW "articulated figure '%s' modifies unknown joint '%s'\n", af->decl->name, fb->jointName );
		return qtrue;
	}
	if ( !Q_stricmp( fb->jointName, "origin" ) ) {
		// idAF::SetBase
		VectorCopy( body->current->worldOrigin, af->baseOrigin );
		AxisCopy( body->current->worldAxis, af->baseAxis );
		AF_AddJointMod( af, body, joint, AF_JOINTMOD_AXIS );
	} else {
		AF_AddJointMod( af, body, joint, fb->jointMod );
	}

	if ( fb->frictionDirection.type != AFVEC_COORDS || !VectorCompare( fb->frictionDirection.vec, vec3_origin ) ) {
		AF_FinishVector( af, &fb->frictionDirection, t );
		AFD_VectorMatT( t, (const vec3_t *)body->current->worldAxis, body->frictionDir );
		body->useFrictionDir = qtrue;
	}
	if ( fb->contactMotorDirection.type != AFVEC_COORDS || !VectorCompare( fb->contactMotorDirection.vec, vec3_origin ) ) {
		AF_FinishVector( af, &fb->contactMotorDirection, t );
		AFD_VectorMatT( t, (const vec3_t *)body->current->worldAxis, body->contactMotorDir );
		body->useContactMotorDir = qtrue;
	}
	return qtrue;
}

/*
================
AF_LoadConstraint

idAF::LoadConstraint
================
*/
static qboolean AF_LoadConstraint( afFigure_t *af, const afDeclConstraint_t *fc ) {
	afBody_t *body1, *body2;
	afConstraint_t *c;
	vec3_t anchor, v1, v2, limitAxis, shaft, angles, axis[3], left, up;
	rotation_t rot;

	body1 = AF_GetBody( &af->physics, fc->body1 );
	body2 = Q_stricmp( fc->body2, "world" ) ? AF_GetBody( &af->physics, fc->body2 ) : NULL;
	if ( !body1 ) {
		Com_Printf( S_COLOR_YELLOW "constraint '%s' of '%s': no body '%s'\n", fc->name, af->decl->name, fc->body1 );
		return qfalse;
	}

	c = AF_AddConstraint( &af->physics, fc->type, fc->name, body1, body2 );
	if ( !c ) {
		return qfalse;
	}

	if ( fc->type == CONSTRAINT_FIXED ) {
		return qtrue;
	}

	AF_FinishVector( af, &fc->anchor, anchor );
	AF_SetAnchor( c, anchor );
	c->friction = fc->friction;

	switch ( fc->type ) {
		case CONSTRAINT_BALLANDSOCKETJOINT:
			AF_FinishVector( af, &fc->limitAxis, limitAxis );
			AF_FinishVector( af, &fc->shaft[0], shaft );
			if ( fc->limit == AF_LIMIT_CONE ) {
				AF_SetConeLimit( c, limitAxis, fc->limitAngles[0], shaft );
			} else if ( fc->limit == AF_LIMIT_PYRAMID ) {
				vectoangles( limitAxis, angles );
				angles[ROLL] = fc->limitAngles[2];
				AnglesToAxis( angles, axis );
				AF_SetPyramidLimit( c, axis[0], axis[1], fc->limitAngles[0], fc->limitAngles[1], shaft );
			}
			break;

		case CONSTRAINT_UNIVERSALJOINT:
			AF_FinishVector( af, &fc->shaft[0], v1 );
			AF_FinishVector( af, &fc->shaft[1], v2 );
			AF_SetShafts( c, v1, v2 );
			// the limit's body1 axis is shaft1, in the world
			AFD_VectorMat( c->shaft1, (const vec3_t *)body1->current->worldAxis, shaft );
			AF_FinishVector( af, &fc->limitAxis, limitAxis );
			if ( fc->limit == AF_LIMIT_CONE ) {
				AF_SetConeLimit( c, limitAxis, fc->limitAngles[0], shaft );
			} else if ( fc->limit == AF_LIMIT_PYRAMID ) {
				vectoangles( limitAxis, angles );
				angles[ROLL] = fc->limitAngles[2];
				AnglesToAxis( angles, axis );
				AF_SetPyramidLimit( c, axis[0], axis[1], fc->limitAngles[0], fc->limitAngles[1], shaft );
			}
			break;

		case CONSTRAINT_HINGE:
			AF_FinishVector( af, &fc->axis, v1 );
			AF_SetHingeAxis( c, v1 );
			if ( fc->limit == AF_LIMIT_CONE ) {
				VectorNormalize( v1 );
				VectorOrthogonalBasis( v1, left, up );
				memset( &rot, 0, sizeof( rot ) );
				RotationFromOriginAndVector( &rot, vec3_origin, v1, fc->limitAngles[0] );
				RotationToMat3( &rot );
				RotationRotatePoint2( &rot, left, limitAxis );
				memset( &rot, 0, sizeof( rot ) );
				RotationFromOriginAndVector( &rot, vec3_origin, v1, fc->limitAngles[2] );
				RotationToMat3( &rot );
				RotationRotatePoint2( &rot, left, shaft );
				AF_SetConeLimit( c, limitAxis, fc->limitAngles[1], shaft );
			}
			break;

		default:
			break;
	}
	return qtrue;
}

/*
================
BG_AFLoad

idAF::Load: the figure made in the AF pose (model space jointMats of the
model's skeleton)
================
*/
qboolean BG_AFLoad( afFigure_t *af, const afDecl_t *decl, const bgModel_t *model, const float *afPose, int numJoints, float scale ) {
	int i;

	memset( af, 0, sizeof( *af ) );
	af->decl = decl;
	af->model = model;
	af->numJoints = Com_Clamp( 0, MAX_MODEL_JOINTS, numJoints );
	af->scale = scale > 0.0f ? scale : 1.0f;
	memcpy( af->afPose, afPose, af->numJoints * 12 * sizeof( float ) );
	AF_ScalePose( af->afPose, af->numJoints, af->scale );

	if ( !decl->numBodies || Q_stricmp( decl->bodies[0].jointName, "origin" ) ) {
		Com_Printf( S_COLOR_YELLOW "BG_AFLoad: articulated figure '%s' has no body which modifies the origin joint.\n", decl->name );
		return qfalse;
	}

	// initialize articulated figure physics
	AF_Init( &af->physics );
	af->physics.clipMask = decl->clipMask;
	af->physics.linearFriction = decl->defaultLinearFriction;
	af->physics.angularFriction = decl->defaultAngularFriction;
	af->physics.contactFriction = decl->defaultContactFriction;
	af->physics.suspendVelocity[0] = decl->suspendVelocity[0];
	af->physics.suspendVelocity[1] = decl->suspendVelocity[1];
	af->physics.suspendAcceleration[0] = decl->suspendAcceleration[0];
	af->physics.suspendAcceleration[1] = decl->suspendAcceleration[1];
	af->physics.noMoveTime = decl->noMoveTime;
	af->physics.noMoveTranslation = decl->noMoveTranslation;
	af->physics.noMoveRotation = decl->noMoveRotation;
	af->physics.minMoveTime = decl->minMoveTime;
	af->physics.maxMoveTime = decl->maxMoveTime;
	af->physics.selfCollision = decl->selfCollision;

	for ( i = 0; i < decl->numBodies; i++ ) {
		if ( !AF_LoadBody( af, &decl->bodies[i] ) ) {
			return qfalse;
		}
	}
	for ( i = 0; i < decl->numConstraints; i++ ) {
		AF_LoadConstraint( af, &decl->constraints[i] );
	}

	af->physics.forceTotalMass = decl->totalMass;
	af->physics.changedAF = qtrue;
	af->isLoaded = qtrue;
	return qtrue;
}

/*
================
BG_AFSetupPose

idAF::SetupPose: the bodies where the model's joints are (model space
jointMats, the model at origin with axis)
================
*/
void BG_AFSetupPose( afFigure_t *af, const float *jointMats, const vec3_t origin, const vec3_t axis[3], int time ) {
	afJointModInfo_t *jm;
	afBody_t *body;
	vec3_t jointOrigin, jointAxis[3], t, m[3];
	int i;

	if ( !af->isLoaded ) {
		return;
	}
	af->poseTime = time;

	for ( i = 0; i < af->numJointMods; i++ ) {
		jm = &af->jointMods[i];
		body = &af->physics.bodies[jm->bodyId];
		AF_JointTransform( jointMats, jm->joint, jointOrigin, jointAxis );
		VectorScale( jointOrigin, af->scale, jointOrigin );		// the scaled model's

		// renderOrigin + ( origin + jointBodyOrigin * axis ) * renderAxis
		AFD_VectorMat( jm->jointBodyOrigin, (const vec3_t *)jointAxis, t );
		VectorAdd( jointOrigin, t, t );
		AFD_VectorMat( t, axis, t );
		VectorAdd( origin, t, body->current->worldOrigin );

		// jointBodyAxis * axis * renderAxis
		AFD_Mat3Mul( (const vec3_t *)jm->jointBodyAxis, (const vec3_t *)jointAxis, m );
		AFD_Mat3Mul( (const vec3_t *)m, axis, body->current->worldAxis );
	}
}

/*
================
BG_AFChangePose

idAF::ChangePose: as BG_AFSetupPose, the bodies' velocities from the last pose
================
*/
void BG_AFChangePose( afFigure_t *af, const float *jointMats, const vec3_t origin, const vec3_t axis[3], int time ) {
	vec3_t lastOrigins[AF_MAX_BODIES];
	float invDelta;
	afBody_t *body;
	int i;

	if ( !af->isLoaded || time <= af->poseTime ) {
		return;
	}
	invDelta = 1.0f / ( ( time - af->poseTime ) * 0.001f );

	for ( i = 0; i < af->physics.numBodies; i++ ) {
		VectorCopy( af->physics.bodies[i].current->worldOrigin, lastOrigins[i] );
	}

	BG_AFSetupPose( af, jointMats, origin, axis, time );

	for ( i = 0; i < af->numJointMods; i++ ) {
		body = &af->physics.bodies[af->jointMods[i].bodyId];
		VectorSubtract( body->current->worldOrigin, lastOrigins[body->id], body->current->spatialVelocity );
		VectorScale( body->current->spatialVelocity, invDelta, body->current->spatialVelocity );
	}
}

/*
================
BG_AFTestSolid

idAF::TestSolid: bodies stuck in something are pushed out of it
================
*/
void BG_AFTestSolid( afFigure_t *af ) {
	afPhysics_t *phys = &af->physics;
	afBody_t *body;
	cm_trace_t trace;
	rotation_t rot;
	float depth;
	int i;

	if ( !af->isLoaded || !phys->clip.motion ) {
		return;
	}

	memset( &rot, 0, sizeof( rot ) );
	for ( i = 0; i < phys->numBodies; i++ ) {
		body = &phys->bodies[i];
		body->clipDisabled = qfalse;
		VectorCopy( body->current->worldOrigin, rot.origin );
		VectorSet( rot.vec, 0, 0, 1 );
		rot.angle = 0.0f;
		if ( phys->clip.motion( phys->clip.ctx, phys, i, &trace, body->current->worldOrigin, body->current->worldOrigin,
								&rot, &body->trm, (const vec3_t *)body->current->worldAxis, body->clipMask )
			&& trace.fraction < 1.0f ) {
			depth = fabs( DotProduct( trace.c.point, trace.c.normal ) - trace.c.dist );
			VectorMA( body->current->worldOrigin, depth + 8.0f, trace.c.normal, body->current->worldOrigin );
		}
	}
}

/*
================
BG_AFStart

idAF::Start: off it goes
================
*/
void BG_AFStart( afFigure_t *af, int time ) {
	if ( !af->isLoaded ) {
		return;
	}
	af->physics.time = time;
	AF_Activate( &af->physics );
}

/*
================
BG_AFPose

idAF::UpdateAnimation and idAnimator's AF pose: the model's joints from the
bodies (model space jointMats) and where the model is (origin, axis). Joints
no body moves keep their place under their parents from the AF pose.
================
*/
void BG_AFPose( const afFigure_t *af, float *jointMats, vec3_t renderOrigin, vec3_t renderAxis[3] ) {
	const afPhysics_t *phys = &af->physics;
	const afBody_t *body;
	vec3_t modOrigin[MAX_MODEL_JOINTS], modAxis[MAX_MODEL_JOINTS][3];
	int mods[MAX_MODEL_JOINTS];
	vec3_t t, m[3], m2[3], renderAxisT[3];
	vec3_t jo, ja[3], po, pa[3], cpo, cpa[3], localOrigin, localAxis[3];
	int i, j, p;

	if ( !af->isLoaded || !phys->numBodies ) {
		return;
	}

	// get the render position
	AFD_Mat3TransposeMul( (const vec3_t *)af->baseAxis, (const vec3_t *)phys->bodies[0].current->worldAxis, renderAxis );
	AFD_VectorMat( af->baseOrigin, (const vec3_t *)renderAxis, t );
	VectorSubtract( phys->bodies[0].current->worldOrigin, t, renderOrigin );
	TransposeAxis( (const vec3_t *)renderAxis, renderAxisT );

	// the joints the bodies move, in model space
	for ( j = 0; j < af->numJoints; j++ ) {
		mods[j] = -1;
	}
	for ( i = 0; i < af->numJointMods; i++ ) {
		const afJointModInfo_t *jm = &af->jointMods[i];

		body = &phys->bodies[jm->bodyId];

		// axis = jointBodyAxis' * ( bodyAxis * renderAxis' )
		AFD_Mat3Mul( (const vec3_t *)body->current->worldAxis, (const vec3_t *)renderAxisT, m );
		AFD_Mat3TransposeMul( (const vec3_t *)jm->jointBodyAxis, (const vec3_t *)m, m2 );
		AxisCopy( m2, modAxis[jm->joint] );

		// origin = ( bodyOrigin - renderOrigin ) * renderAxis' - jointBodyOrigin * axis: BG_AFSetupPose
		// undone (Doom 3 takes jointBodyOrigin * axis off in the world, right only unturned)
		VectorSubtract( body->current->worldOrigin, renderOrigin, t );
		AFD_VectorMatT( t, (const vec3_t *)renderAxis, modOrigin[jm->joint] );
		AFD_VectorMat( jm->jointBodyOrigin, (const vec3_t *)m2, t );
		VectorSubtract( modOrigin[jm->joint], t, modOrigin[jm->joint] );

		mods[jm->joint] = jm->jointMod;
	}

	// down the hierarchy, parents first
	for ( j = 0; j < af->numJoints; j++ ) {
		AF_JointTransform( af->afPose, j, jo, ja );
		p = af->model->joints[j].parent;

		if ( p >= 0 && p < j ) {
			// the joint under its parent in the AF pose
			AF_JointTransform( af->afPose, p, po, pa );
			AFD_Mat3MulTranspose( (const vec3_t *)ja, (const vec3_t *)pa, localAxis );
			VectorSubtract( jo, po, t );
			AFD_VectorMatT( t, (const vec3_t *)pa, localOrigin );

			// under the parent now
			AF_JointTransform( jointMats, p, cpo, cpa );
			AFD_Mat3Mul( (const vec3_t *)localAxis, (const vec3_t *)cpa, ja );
			AFD_VectorMat( localOrigin, (const vec3_t *)cpa, t );
			VectorAdd( cpo, t, jo );
		}

		if ( mods[j] == AF_JOINTMOD_AXIS || mods[j] == AF_JOINTMOD_BOTH ) {
			AxisCopy( modAxis[j], ja );
		}
		if ( mods[j] == AF_JOINTMOD_ORIGIN || mods[j] == AF_JOINTMOD_BOTH || ( mods[j] >= 0 && p < 0 ) ) {
			VectorCopy( modOrigin[j], jo );
		}

		AF_SetJointTransform( jointMats, j, jo, (const vec3_t *)ja );
	}

	// back to the model's own size: the caller draws it scaled
	AF_ScalePose( jointMats, af->numJoints, 1.0f / af->scale );
}
