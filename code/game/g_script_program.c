/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

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
// g_script_program.c -- Doom 3's Script_Program.cpp: the types, variables,
// functions and statements of the scripts

#include "g_local.h"

// simple types.  function types are dynamically allocated
typeDef_t	type_void				= { ev_void, "void", 0, NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_void };
typeDef_t	type_scriptevent		= { ev_scriptevent, "scriptevent", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_scriptevent };
typeDef_t	type_namespace			= { ev_namespace, "namespace", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_namespace };
typeDef_t	type_string				= { ev_string, "string", MAX_STRING_LEN, NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_string };
typeDef_t	type_float				= { ev_float, "float", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_float };
typeDef_t	type_vector				= { ev_vector, "vector", E_EVENT_SIZEOF_VEC, NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_vector };
typeDef_t	type_entity				= { ev_entity, "entity", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_entity };	// stored as entity number pointer
typeDef_t	type_field				= { ev_field, "field", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_field };
typeDef_t	type_function			= { ev_function, "function", sizeof( intptr_t ), &type_void, 0, 0, NULL, NULL, 0, 0, NULL, &def_function };
typeDef_t	type_virtualfunction	= { ev_virtualfunction, "virtual function", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_virtualfunction };
typeDef_t	type_pointer			= { ev_pointer, "pointer", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_pointer };
typeDef_t	type_object				= { ev_object, "object", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_object };	// stored as entity number pointer
typeDef_t	type_jumpoffset			= { ev_jumpoffset, "<jump>", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_jumpoffset };	// only used for jump opcodes
typeDef_t	type_argsize			= { ev_argsize, "<argsize>", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_argsize };	// only used for function call and thread opcodes
typeDef_t	type_boolean			= { ev_boolean, "boolean", sizeof( intptr_t ), NULL, 0, 0, NULL, NULL, 0, 0, NULL, &def_boolean };

#define BASIC_DEF( t )	{ 0, { NULL }, NULL, 0, DEF_UNINITIALIZED, &t, NULL, NULL }

varDef_t	def_void				= BASIC_DEF( type_void );
varDef_t	def_scriptevent			= BASIC_DEF( type_scriptevent );
varDef_t	def_namespace			= BASIC_DEF( type_namespace );
varDef_t	def_string				= BASIC_DEF( type_string );
varDef_t	def_float				= BASIC_DEF( type_float );
varDef_t	def_vector				= BASIC_DEF( type_vector );
varDef_t	def_entity				= BASIC_DEF( type_entity );
varDef_t	def_field				= BASIC_DEF( type_field );
varDef_t	def_function			= BASIC_DEF( type_function );
varDef_t	def_virtualfunction		= BASIC_DEF( type_virtualfunction );
varDef_t	def_pointer				= BASIC_DEF( type_pointer );
varDef_t	def_object				= BASIC_DEF( type_object );
varDef_t	def_jumpoffset			= BASIC_DEF( type_jumpoffset );		// only used for jump opcodes
varDef_t	def_argsize				= BASIC_DEF( type_argsize );
varDef_t	def_boolean				= BASIC_DEF( type_boolean );

/*
===============================================================================

memory

===============================================================================
*/

void *Script_Alloc( int size ) {
	return trap_HeapMalloc( size );		// cleared
}

void Script_Free( void *ptr ) {
	if ( ptr ) {
		trap_HeapFree( ptr );
	}
}

char *Script_CopyString( const char *s ) {
	char	*out;
	int		len;

	len = strlen( s ) + 1;
	out = Script_Alloc( len );
	memcpy( out, s, len );
	return out;
}

/*
================
Script_Grow

a list with room for one more than num
================
*/
static void *Script_Grow( void *list, int *max, int num, int elemSize ) {
	void	*newList;
	int		newMax;

	if ( num < *max ) {
		return list;
	}
	newMax = *max ? *max * 2 : 16;
	newList = Script_Alloc( newMax * elemSize );
	if ( list ) {
		memcpy( newList, list, num * elemSize );
		Script_Free( list );
	}
	*max = newMax;
	return newList;
}

/*
===============================================================================

typeDef_t

===============================================================================
*/

/*
================
Type_Init
================
*/
void Type_Init( typeDef_t *self, etype_t etype, varDef_t *edef, const char *ename, int esize, typeDef_t *aux ) {
	memset( self, 0, sizeof( *self ) );
	self->name		= Script_CopyString( ename );
	self->type		= etype;
	self->def		= edef;
	self->size		= esize;
	self->auxType	= aux;
}

/*
================
Type_Copy

self is a fresh one
================
*/
void Type_Copy( typeDef_t *self, const typeDef_t *other ) {
	int i;

	memset( self, 0, sizeof( *self ) );
	self->type		= other->type;
	self->def		= other->def;
	self->name		= Script_CopyString( other->name );
	self->size		= other->size;
	self->auxType	= other->auxType;

	if ( other->numParms ) {
		self->maxParms = self->numParms = other->numParms;
		self->parmTypes = Script_Alloc( self->maxParms * sizeof( self->parmTypes[0] ) );
		self->parmNames = Script_Alloc( self->maxParms * sizeof( self->parmNames[0] ) );
		for( i = 0; i < other->numParms; i++ ) {
			self->parmTypes[ i ] = other->parmTypes[ i ];
			self->parmNames[ i ] = Script_CopyString( other->parmNames[ i ] );
		}
	}
	if ( other->numFunctions ) {
		self->maxFunctions = self->numFunctions = other->numFunctions;
		self->functions = Script_Alloc( self->maxFunctions * sizeof( self->functions[0] ) );
		memcpy( self->functions, other->functions, self->numFunctions * sizeof( self->functions[0] ) );
	}
}

/*
================
Type_Free

what Type_Init, Type_Copy and the Add functions allocated
================
*/
void Type_Free( typeDef_t *self ) {
	int i;

	for( i = 0; i < self->numParms; i++ ) {
		Script_Free( self->parmNames[ i ] );
	}
	Script_Free( self->parmTypes );
	Script_Free( self->parmNames );
	Script_Free( (void *)self->functions );
	Script_Free( self->name );
	self->parmTypes = NULL;
	self->parmNames = NULL;
	self->functions = NULL;
	self->name = NULL;
	self->numParms = self->maxParms = 0;
	self->numFunctions = self->maxFunctions = 0;
}

/*
================
Type_Inherits

Returns true if basetype is an ancestor of this type.
================
*/
qboolean Type_Inherits( const typeDef_t *self, const typeDef_t *basetype ) {
	typeDef_t *superType;

	if ( self->type != ev_object ) {
		return qfalse;
	}

	if ( self == basetype ) {
		return qtrue;
	}
	for( superType = self->auxType; superType != NULL; superType = superType->auxType ) {
		if ( superType == basetype ) {
			return qtrue;
		}
	}

	return qfalse;
}

/*
================
Type_MatchesType

Returns true if both types' base types and parameters match
================
*/
qboolean Type_MatchesType( const typeDef_t *self, const typeDef_t *matchtype ) {
	int i;

	if ( self == matchtype ) {
		return qtrue;
	}

	if ( ( self->type != matchtype->type ) || ( self->auxType != matchtype->auxType ) ) {
		return qfalse;
	}

	if ( self->numParms != matchtype->numParms ) {
		return qfalse;
	}

	for( i = 0; i < matchtype->numParms; i++ ) {
		if ( self->parmTypes[ i ] != matchtype->parmTypes[ i ] ) {
			return qfalse;
		}
	}

	return qtrue;
}

/*
================
Type_MatchesVirtualFunction

Returns true if both functions' base types and parameters match
================
*/
qboolean Type_MatchesVirtualFunction( const typeDef_t *self, const typeDef_t *matchfunc ) {
	int i;

	if ( self == matchfunc ) {
		return qtrue;
	}

	if ( ( self->type != matchfunc->type ) || ( self->auxType != matchfunc->auxType ) ) {
		return qfalse;
	}

	if ( self->numParms != matchfunc->numParms ) {
		return qfalse;
	}

	if ( self->numParms > 0 ) {
		if ( !Type_Inherits( self->parmTypes[ 0 ], matchfunc->parmTypes[ 0 ] ) ) {
			return qfalse;
		}
	}

	for( i = 1; i < matchfunc->numParms; i++ ) {
		if ( self->parmTypes[ i ] != matchfunc->parmTypes[ i ] ) {
			return qfalse;
		}
	}

	return qtrue;
}

/*
================
Type_AddParm
================
*/
static void Type_AddParm( typeDef_t *self, typeDef_t *parmtype, const char *name ) {
	int max = self->maxParms;

	self->parmTypes = Script_Grow( self->parmTypes, &max, self->numParms, sizeof( self->parmTypes[0] ) );
	self->parmNames = Script_Grow( self->parmNames, &self->maxParms, self->numParms, sizeof( self->parmNames[0] ) );
	self->parmTypes[ self->numParms ] = parmtype;
	self->parmNames[ self->numParms ] = Script_CopyString( name );
	self->numParms++;
}

/*
================
Type_AddFunctionParm

Adds a new parameter for a function type.
================
*/
void Type_AddFunctionParm( typeDef_t *self, typeDef_t *parmtype, const char *name ) {
	if ( self->type != ev_function ) {
		Script_CompileError( "Type_AddFunctionParm : tried to add parameter on non-function type" );
	}

	Type_AddParm( self, parmtype, name );
}

/*
================
Type_AddField

Adds a new field to an object type.
================
*/
void Type_AddField( typeDef_t *self, typeDef_t *fieldtype, const char *name ) {
	if ( self->type != ev_object ) {
		Script_CompileError( "Type_AddField : tried to add field to non-object type" );
	}

	Type_AddParm( self, fieldtype, name );

	if ( Type_Inherits( Type_FieldType( fieldtype ), &type_object ) ) {
		self->size += type_object.size;
	} else {
		self->size += Type_FieldType( fieldtype )->size;
	}
}

/*
================
Type_SetName
================
*/
void Type_SetName( typeDef_t *self, const char *newname ) {
	Script_Free( self->name );
	self->name = Script_CopyString( newname );
}

/*
================
Type_SuperClass

If type is an object, then returns the object's superclass
================
*/
typeDef_t *Type_SuperClass( const typeDef_t *self ) {
	if ( self->type != ev_object ) {
		Script_CompileError( "Type_SuperClass : tried to get superclass of a non-object type" );
	}

	return self->auxType;
}

/*
================
Type_ReturnType

If type is a function, then returns the function's return type
================
*/
typeDef_t *Type_ReturnType( const typeDef_t *self ) {
	if ( self->type != ev_function ) {
		Script_CompileError( "Type_ReturnType: tried to get return type on non-function type" );
	}

	return self->auxType;
}

/*
================
Type_SetReturnType

If type is a function, then sets the function's return type
================
*/
void Type_SetReturnType( typeDef_t *self, typeDef_t *returntype ) {
	if ( self->type != ev_function ) {
		Script_CompileError( "Type_SetReturnType: tried to set return type on non-function type" );
	}

	self->auxType = returntype;
}

/*
================
Type_FieldType

If type is a field, then returns it's type
================
*/
typeDef_t *Type_FieldType( const typeDef_t *self ) {
	if ( self->type != ev_field ) {
		Script_CompileError( "Type_FieldType: tried to get field type on non-field type" );
	}

	return self->auxType;
}

/*
================
Type_SetFieldType

If type is a field, then sets the function's return type
================
*/
void Type_SetFieldType( typeDef_t *self, typeDef_t *fieldtype ) {
	if ( self->type != ev_field ) {
		Script_CompileError( "Type_SetFieldType: tried to set return type on non-function type" );
	}

	self->auxType = fieldtype;
}

/*
================
Type_PointerType

If type is a pointer, then returns the type it points to
================
*/
typeDef_t *Type_PointerType( const typeDef_t *self ) {
	if ( self->type != ev_pointer ) {
		Script_CompileError( "Type_PointerType: tried to get pointer type on non-pointer" );
	}

	return self->auxType;
}

/*
================
Type_SetPointerType

If type is a pointer, then sets the pointer's type
================
*/
void Type_SetPointerType( typeDef_t *self, typeDef_t *pointertype ) {
	if ( self->type != ev_pointer ) {
		Script_CompileError( "Type_SetPointerType: tried to set type on non-pointer" );
	}

	self->auxType = pointertype;
}

/*
================
Type_GetFunctionNumber
================
*/
int Type_GetFunctionNumber( const typeDef_t *self, const function_t *func ) {
	int i;

	for( i = 0; i < self->numFunctions; i++ ) {
		if ( self->functions[ i ] == func ) {
			return i;
		}
	}
	return -1;
}

/*
================
Type_GetFunction
================
*/
const function_t *Type_GetFunction( const typeDef_t *self, int funcNumber ) {
	assert( funcNumber >= 0 );
	assert( funcNumber < self->numFunctions );
	return self->functions[ funcNumber ];
}

/*
================
Type_AddFunction
================
*/
void Type_AddFunction( typeDef_t *self, const function_t *func ) {
	int i;

	for( i = 0; i < self->numFunctions; i++ ) {
		if ( !strcmp( VarDef_Name( self->functions[ i ]->def ), VarDef_Name( func->def ) ) ) {
			if ( Type_MatchesVirtualFunction( func->def->typeDef, self->functions[ i ]->def->typeDef ) ) {
				self->functions[ i ] = func;
				return;
			}
		}
	}
	self->functions = (const function_t **)Script_Grow( (void *)self->functions, &self->maxFunctions, self->numFunctions, sizeof( self->functions[0] ) );
	self->functions[ self->numFunctions++ ] = func;
}

/*
===============================================================================

varDef_t

===============================================================================
*/

/*
============
VarDef_Name
============
*/
const char *VarDef_Name( const varDef_t *self ) {
	return self->name ? self->name->name : "";
}

/*
============
VarDef_GlobalName
============
*/
const char *VarDef_GlobalName( const varDef_t *self ) {
	if ( self->scope && self->scope != &def_namespace ) {
		return va( "%s::%s", VarDef_GlobalName( self->scope ), VarDef_Name( self ) );
	} else {
		return VarDef_Name( self );
	}
}

/*
============
VarDef_DepthOfScope
============
*/
int VarDef_DepthOfScope( const varDef_t *self, const varDef_t *otherScope ) {
	const varDef_t *def;
	int depth;

	depth = 1;
	for( def = otherScope; def != NULL; def = def->scope ) {
		if ( def == self->scope ) {
			return depth;
		}
		depth++;
	}

	return 0;
}

/*
============
VarDef_SetFunction
============
*/
void VarDef_SetFunction( varDef_t *self, function_t *func ) {
	assert( self->typeDef );
	self->initialized = DEF_INITIALIZEDCONSTANT;
	assert( self->typeDef->type == ev_function );
	self->value.functionPtr = func;
}

/*
============
VarDef_SetObject
============
*/
void VarDef_SetObject( varDef_t *self, void *object ) {
	assert( self->typeDef );
	assert( Type_Inherits( self->typeDef, &type_object ) );
	*self->value.objectPtrPtr = object;
}

/*
============
VarDef_SetValue
============
*/
void VarDef_SetValue( varDef_t *self, const eval_t *_value, qboolean constant ) {
	assert( self->typeDef );
	if ( constant ) {
		self->initialized = DEF_INITIALIZEDCONSTANT;
	} else {
		self->initialized = DEF_INITIALIZEDVARIABLE;
	}

	switch( self->typeDef->type ) {
	case ev_pointer :
	case ev_boolean :
	case ev_field :
		*self->value.intPtr = _value->_int;
		break;

	case ev_jumpoffset :
		self->value.jumpOffset = _value->_int;
		break;

	case ev_argsize :
		self->value.argSize = _value->_int;
		break;

	case ev_entity :
		*self->value.entityNumberPtr = _value->entity;
		break;

	case ev_string :
		Q_strncpyz( self->value.stringPtr, _value->stringPtr, MAX_STRING_LEN );
		break;

	case ev_float :
		*self->value.floatPtr = _value->_float;
		break;

	case ev_vector :
		self->value.vectorPtr[ 0 ] = _value->vector[ 0 ];
		self->value.vectorPtr[ 1 ] = _value->vector[ 1 ];
		self->value.vectorPtr[ 2 ] = _value->vector[ 2 ];
		break;

	case ev_function :
		self->value.functionPtr = _value->function;
		break;

	case ev_virtualfunction :
		self->value.virtualFunction = _value->_int;
		break;

	case ev_object :
		*self->value.entityNumberPtr = _value->entity;
		break;

	default :
		Script_CompileError( "weird type on '%s'", VarDef_Name( self ) );
		break;
	}
}

/*
============
VarDef_SetString
============
*/
void VarDef_SetString( varDef_t *self, const char *string, qboolean constant ) {
	if ( constant ) {
		self->initialized = DEF_INITIALIZEDCONSTANT;
	} else {
		self->initialized = DEF_INITIALIZEDVARIABLE;
	}

	assert( self->typeDef && ( self->typeDef->type == ev_string ) );
	Q_strncpyz( self->value.stringPtr, string, MAX_STRING_LEN );
}

/*
============
VarDef_PrintInfo
============
*/
static void VarDef_PrintInfo( const varDef_t *self, fileHandle_t file, int instructionPointer ) {
	statement_t	*jumpst;
	int			jumpto;
	etype_t		etype;
	int			i;
	int			len;
	const char	*ch;
	char		text[ 1024 ];

	text[ 0 ] = '\0';
	if ( self->initialized == DEF_INITIALIZEDCONSTANT ) {
		Q_strcat( text, sizeof( text ), "const " );
	}

	etype = self->typeDef->type;
	switch( etype ) {
	case ev_jumpoffset :
		jumpto = instructionPointer + self->value.jumpOffset;
		jumpst = Program_GetStatement( jumpto );
		Q_strcat( text, sizeof( text ), va( "address %d [%s(%d)]", jumpto, Program_GetFilename( jumpst->file ), jumpst->linenumber ) );
		break;

	case ev_function :
		if ( self->value.functionPtr->eventdef ) {
			Q_strcat( text, sizeof( text ), va( "event %s", VarDef_GlobalName( self ) ) );
		} else {
			Q_strcat( text, sizeof( text ), va( "function %s", VarDef_GlobalName( self ) ) );
		}
		break;

	case ev_field :
		Q_strcat( text, sizeof( text ), va( "field %d", self->value.ptrOffset ) );
		break;

	case ev_argsize:
		Q_strcat( text, sizeof( text ), va( "args %d", self->value.argSize ) );
		break;

	default:
		Q_strcat( text, sizeof( text ), va( "%s ", self->typeDef->name ) );
		if ( self->initialized == DEF_INITIALIZEDCONSTANT ) {
			switch( etype ) {
			case ev_string :
				Q_strcat( text, sizeof( text ), "\"" );
				len = strlen( self->value.stringPtr );
				ch = self->value.stringPtr;
				for( i = 0; i < len; i++, ch++ ) {
					if ( *ch >= ' ' && *ch < 127 ) {
						Q_strcat( text, sizeof( text ), va( "%c", *ch ) );
					} else if ( *ch == '\n' ) {
						Q_strcat( text, sizeof( text ), "\\n" );
					} else {
						Q_strcat( text, sizeof( text ), va( "\\x%.2x", (int)( *ch ) ) );
					}
				}
				Q_strcat( text, sizeof( text ), "\"" );
				break;

			case ev_vector :
				Q_strcat( text, sizeof( text ), va( "'%s'", vtos( self->value.vectorPtr ) ) );
				break;

			case ev_float :
				Q_strcat( text, sizeof( text ), va( "%f", *self->value.floatPtr ) );
				break;

			case ev_virtualfunction :
				Q_strcat( text, sizeof( text ), va( "vtable[ %d ]", self->value.virtualFunction ) );
				break;

			default :
				Q_strcat( text, sizeof( text ), va( "%d", *self->value.intPtr ) );
				break;
			}
		} else if ( self->initialized == DEF_STACKVARIABLE ) {
			Q_strcat( text, sizeof( text ), va( "stack[%d]", self->value.stackOffset ) );
		} else {
			Q_strcat( text, sizeof( text ), va( "global[%d]", self->num ) );
		}
		break;
	}
	trap_FS_Write( text, strlen( text ), file );
}

/*
============
VarDefName_AddDef / RemoveDef
============
*/
static void VarDefName_AddDef( varDefName_t *self, varDef_t *def ) {
	assert( def->next == NULL );
	def->name = self;
	def->next = self->defs;
	self->defs = def;
}

static void VarDefName_RemoveDef( varDefName_t *self, varDef_t *def ) {
	varDef_t *d;

	if ( self->defs == def ) {
		self->defs = def->next;
	} else {
		for ( d = self->defs; d->next != NULL; d = d->next ) {
			if ( d->next == def ) {
				d->next = def->next;
				break;
			}
		}
	}
	def->next = NULL;
	def->name = NULL;
}

/*
===============================================================================

scriptObject_t

===============================================================================
*/

/*
============
ScriptObject_Free
============
*/
void ScriptObject_Free( scriptObject_t *self ) {
	Script_Free( self->data );
	self->data = NULL;
	self->type = NULL;
}

/*
============
ScriptObject_SetType

Allocates an object and initializes memory.
============
*/
qboolean ScriptObject_SetType( scriptObject_t *self, const char *typeName ) {
	typeDef_t *newtype;

	// lookup the type
	newtype = Program_FindType( typeName );

	// only allocate memory if the object type changes
	if ( !self->type || newtype != self->type ) {
		ScriptObject_Free( self );
		if ( !newtype ) {
			G_Printf( S_COLOR_YELLOW "WARNING: ScriptObject_SetType: Unknown type '%s'\n", typeName );
			return qfalse;
		}

		if ( !Type_Inherits( newtype, &type_object ) ) {
			G_Printf( S_COLOR_YELLOW "WARNING: ScriptObject_SetType: Can't create object of type '%s'.  Must be an object type.\n", newtype->name );
			return qfalse;
		}

		// set the type
		self->type = newtype;

		// allocate the memory
		self->data = Script_Alloc( MAX( self->type->size, 1 ) );
	}

	// init object memory
	ScriptObject_ClearObject( self );

	return qtrue;
}

/*
============
ScriptObject_ClearObject

Resets the memory for the script object without changing its type.
============
*/
void ScriptObject_ClearObject( scriptObject_t *self ) {
	if ( self->type && self->type != &type_object ) {
		memset( self->data, 0, self->type->size );
	}
}

/*
============
ScriptObject_HasObject
============
*/
qboolean ScriptObject_HasObject( const scriptObject_t *self ) {
	return ( self->type && self->type != &type_object );
}

/*
============
ScriptObject_GetTypeName
============
*/
const char *ScriptObject_GetTypeName( const scriptObject_t *self ) {
	return self->type ? self->type->name : type_object.name;
}

/*
============
ScriptObject_GetConstructor / GetDestructor
============
*/
const function_t *ScriptObject_GetConstructor( const scriptObject_t *self ) {
	return ScriptObject_GetFunction( self, "init" );
}

const function_t *ScriptObject_GetDestructor( const scriptObject_t *self ) {
	return ScriptObject_GetFunction( self, "destroy" );
}

/*
============
ScriptObject_GetFunction
============
*/
const function_t *ScriptObject_GetFunction( const scriptObject_t *self, const char *name ) {
	if ( !ScriptObject_HasObject( self ) ) {
		return NULL;
	}
	return Program_FindObjectFunction( name, self->type );
}

/*
============
ScriptObject_GetVariable
============
*/
byte *ScriptObject_GetVariable( const scriptObject_t *self, const char *name, etype_t etype ) {
	int				i;
	int				pos;
	const typeDef_t	*t;
	const typeDef_t	*parm;

	if ( !ScriptObject_HasObject( self ) ) {
		return NULL;
	}

	t = self->type;
	do {
		if ( Type_SuperClass( t ) != &type_object ) {
			pos = Type_SuperClass( t )->size;
		} else {
			pos = 0;
		}
		for( i = 0; i < t->numParms; i++ ) {
			parm = t->parmTypes[ i ];
			if ( !strcmp( t->parmNames[ i ], name ) ) {
				if ( etype != Type_FieldType( parm )->type ) {
					return NULL;
				}
				return &self->data[ pos ];
			}

			if ( Type_Inherits( Type_FieldType( parm ), &type_object ) ) {
				pos += type_object.size;
			} else {
				pos += Type_FieldType( parm )->size;
			}
		}
		t = Type_SuperClass( t );
	} while( t && ( t != &type_object ) );

	return NULL;
}

/*
===============================================================================

the program

===============================================================================
*/

#define VARDEF_HASH_SIZE	4096

static char				**fileList;
static int				numFiles, maxFiles;
static char				program_filename[ MAX_QPATH ];
static int				program_filenum;

static int				numVariables;
static intptr_t			variablesStore[ ( MAX_GLOBALS + sizeof( intptr_t ) - 1 ) / sizeof( intptr_t ) ];
#define variables		( ( byte * )variablesStore )
static byte				*variableDefaults;
static int				numVariableDefaults;

static function_t		functions[ MAX_FUNCS ];
static int				numFunctions;
static statement_t		statements[ MAX_STATEMENTS ];
static int				numStatements;

static typeDef_t		**types;
static int				numTypes, maxTypes;

static varDefName_t		**varDefNames;
static int				numVarDefNames, maxVarDefNames;
static varDefName_t		*varDefNameHash[ VARDEF_HASH_SIZE ];
static varDef_t			**varDefs;
static int				numVarDefs, maxVarDefs;

static varDef_t			*sysDef;

static int				top_functions;
static int				top_statements;
static int				top_types;
static int				top_defs;
static int				top_files;

varDef_t				*program_returnDef;
varDef_t				*program_returnStringDef;

/*
================
Program_AllocType
================
*/
typeDef_t *Program_AllocType( const typeDef_t *type ) {
	typeDef_t *newtype;

	newtype = Script_Alloc( sizeof( *newtype ) );
	Type_Copy( newtype, type );
	types = Script_Grow( types, &maxTypes, numTypes, sizeof( types[0] ) );
	types[ numTypes++ ] = newtype;

	return newtype;
}

/*
================
Program_AllocNewType
================
*/
typeDef_t *Program_AllocNewType( etype_t etype, varDef_t *edef, const char *ename, int esize, typeDef_t *aux ) {
	typeDef_t *newtype;

	newtype = Script_Alloc( sizeof( *newtype ) );
	Type_Init( newtype, etype, edef, ename, esize, aux );
	types = Script_Grow( types, &maxTypes, numTypes, sizeof( types[0] ) );
	types[ numTypes++ ] = newtype;

	return newtype;
}

/*
================
Program_GetType

Returns a preexisting complex type that matches the parm, or allocates
a new one and copies it out.
================
*/
typeDef_t *Program_GetType( const typeDef_t *type, qboolean allocate ) {
	int i;

	//FIXME: linear search == slow
	for( i = numTypes - 1; i >= 0; i-- ) {
		if ( Type_MatchesType( types[ i ], type ) && !strcmp( types[ i ]->name, type->name ) ) {
			return types[ i ];
		}
	}

	if ( !allocate ) {
		return NULL;
	}

	// allocate a new one
	return Program_AllocType( type );
}

/*
================
Program_FindType

Returns a preexisting complex type that matches the name, or returns NULL if not found
================
*/
typeDef_t *Program_FindType( const char *name ) {
	typeDef_t	*check;
	int			i;

	for( i = numTypes - 1; i >= 0; i-- ) {
		check = types[ i ];
		if ( !strcmp( check->name, name ) ) {
			return check;
		}
	}

	return NULL;
}

/*
================
Program_FindDefName
================
*/
static varDefName_t *Program_FindDefName( const char *name, int *hashOut ) {
	varDefName_t	*n;
	int				hash;

	hash = StrHash( name ) & ( VARDEF_HASH_SIZE - 1 );
	if ( hashOut ) {
		*hashOut = hash;
	}
	for ( n = varDefNameHash[ hash ]; n; n = n->hashNext ) {
		if ( !strcmp( n->name, name ) ) {
			return n;
		}
	}
	return NULL;
}

/*
================
Program_GetDefList
================
*/
varDef_t *Program_GetDefList( const char *name ) {
	varDefName_t *n;

	n = Program_FindDefName( name, NULL );
	return n ? n->defs : NULL;
}

/*
================
Program_AddDefToNameList
================
*/
static void Program_AddDefToNameList( varDef_t *def, const char *name ) {
	varDefName_t	*n;
	int				hash;

	n = Program_FindDefName( name, &hash );
	if ( !n ) {
		n = Script_Alloc( sizeof( *n ) );
		n->name = Script_CopyString( name );
		n->hashNext = varDefNameHash[ hash ];
		varDefNameHash[ hash ] = n;
		varDefNames = Script_Grow( varDefNames, &maxVarDefNames, numVarDefNames, sizeof( varDefNames[0] ) );
		varDefNames[ numVarDefNames++ ] = n;
	}
	VarDefName_AddDef( n, def );
}

/*
================
Program_ReserveMem

reserves memory for global variables and returns the starting pointer
================
*/
static byte *Program_ReserveMem( int size ) {
	byte *res = &variables[ numVariables ];
	numVariables += size;
	if ( numVariables > MAX_GLOBALS ) {
		Script_CompileError( "Exceeded global memory size (%d bytes)", MAX_GLOBALS );
	}

	memset( res, 0, size );

	return res;
}

/*
================
Program_AllocVarDef
================
*/
static varDef_t *Program_AllocVarDef( typeDef_t *type, const char *name, varDef_t *scope ) {
	varDef_t *def;

	def = Script_Alloc( sizeof( *def ) );
	def->typeDef	= type;
	def->scope		= scope;
	def->numUsers	= 1;
	def->initialized = DEF_UNINITIALIZED;
	varDefs = Script_Grow( varDefs, &maxVarDefs, numVarDefs, sizeof( varDefs[0] ) );
	def->num		= numVarDefs;
	varDefs[ numVarDefs++ ] = def;

	// add the def to the list with defs with this name and set the name pointer
	Program_AddDefToNameList( def, name );

	return def;
}

/*
================
Program_DeleteVarDef
================
*/
static void Program_DeleteVarDef( varDef_t *def ) {
	if ( def->name ) {
		VarDefName_RemoveDef( def->name, def );
	}
	Script_Free( def );
}

/*
================
Program_AllocDef
================
*/
varDef_t *Program_AllocDef( typeDef_t *type, const char *name, varDef_t *scope, qboolean constant ) {
	varDef_t	*def;
	char		element[ MAX_STRING_CHARS ];
	varDef_t	*def_x;
	varDef_t	*def_y;
	varDef_t	*def_z;

	// allocate a new def
	def = Program_AllocVarDef( type, name, scope );

	if ( ( type->type == ev_vector ) || ( ( type->type == ev_field ) && ( Type_FieldType( type )->type == ev_vector ) ) ) {
		//
		// vector
		//
		if ( !strcmp( name, RESULT_STRING ) ) {
			// <RESULT> vector defs don't need the _x, _y and _z components
			assert( scope->typeDef->type == ev_function );
			def->value.stackOffset	= scope->value.functionPtr->locals;
			def->initialized		= DEF_STACKVARIABLE;
			scope->value.functionPtr->locals += type->size;
		} else if ( Type_Inherits( scope->typeDef, &type_object ) ) {
			typeDef_t	newtype;
			typeDef_t	*ftype;

			Type_Init( &newtype, ev_field, NULL, "float field", 0, &type_float );
			ftype = Program_GetType( &newtype, qtrue );
			Type_Free( &newtype );

			// set the value to the variable's position in the object
			def->value.ptrOffset = scope->typeDef->size;

			// make automatic defs for the vectors elements
			// origin can be accessed as origin_x, origin_y, and origin_z
			Com_sprintf( element, sizeof( element ), "%s_x", VarDef_Name( def ) );
			def_x = Program_AllocDef( ftype, element, scope, constant );

			Com_sprintf( element, sizeof( element ), "%s_y", VarDef_Name( def ) );
			def_y = Program_AllocDef( ftype, element, scope, constant );
			def_y->value.ptrOffset = def_x->value.ptrOffset + sizeof( float );

			Com_sprintf( element, sizeof( element ), "%s_z", VarDef_Name( def ) );
			def_z = Program_AllocDef( ftype, element, scope, constant );
			def_z->value.ptrOffset = def_y->value.ptrOffset + sizeof( float );
		} else {
			typeDef_t	newtype;
			typeDef_t	*ftype;

			Type_Init( &newtype, ev_float, &def_float, "vector float", 0, NULL );
			ftype = Program_GetType( &newtype, qtrue );
			Type_Free( &newtype );

			// make automatic defs for the vectors elements
			// origin can be accessed as origin_x, origin_y, and origin_z
			Com_sprintf( element, sizeof( element ), "%s_x", VarDef_Name( def ) );
			def_x = Program_AllocVarDef( ftype, element, scope );

			Com_sprintf( element, sizeof( element ), "%s_y", VarDef_Name( def ) );
			def_y = Program_AllocVarDef( ftype, element, scope );

			Com_sprintf( element, sizeof( element ), "%s_z", VarDef_Name( def ) );
			def_z = Program_AllocVarDef( ftype, element, scope );

			// get the memory for the full vector and point the _x, _y and _z
			// defs at the vector member offsets
			if ( scope->typeDef->type == ev_function ) {
				// vector on stack
				def->value.stackOffset	= scope->value.functionPtr->locals;
				def->initialized		= DEF_STACKVARIABLE;
				scope->value.functionPtr->locals += type->size;

				def_x->value.stackOffset = def->value.stackOffset;
				def_y->value.stackOffset = def_x->value.stackOffset + sizeof( float );
				def_z->value.stackOffset = def_y->value.stackOffset + sizeof( float );
			} else {
				// global vector
				def->value.bytePtr		= Program_ReserveMem( type->size );
				def_x->value.bytePtr	= def->value.bytePtr;
				def_y->value.bytePtr	= def_x->value.bytePtr + sizeof( float );
				def_z->value.bytePtr	= def_y->value.bytePtr + sizeof( float );
			}

			def_x->initialized = def->initialized;
			def_y->initialized = def->initialized;
			def_z->initialized = def->initialized;
		}
	} else if ( Type_Inherits( scope->typeDef, &type_object ) ) {
		//
		// object variable
		//
		// set the value to the variable's position in the object
		def->value.ptrOffset = scope->typeDef->size;
	} else if ( scope->typeDef->type == ev_function ) {
		//
		// stack variable
		//
		// since we don't know how many local variables there are,
		// we have to have them go backwards on the stack
		def->value.stackOffset	= scope->value.functionPtr->locals;
		def->initialized		= DEF_STACKVARIABLE;

		if ( Type_Inherits( type, &type_object ) ) {
			// objects only have their entity number on the stack, not the entire object
			scope->value.functionPtr->locals += type_object.size;
		} else {
			scope->value.functionPtr->locals += type->size;
		}
	} else {
		//
		// global variable
		//
		def->value.bytePtr = Program_ReserveMem( def->typeDef->size );
	}

	return def;
}

/*
================
Program_GetDef

If type is NULL, it will match any type
================
*/
varDef_t *Program_GetDef( const typeDef_t *type, const char *name, const varDef_t *scope ) {
	varDef_t		*def;
	varDef_t		*bestDef;
	int				bestDepth;
	int				depth;

	bestDepth = 0;
	bestDef = NULL;
	for( def = Program_GetDefList( name ); def != NULL; def = def->next ) {
		if ( def->scope->typeDef->type == ev_namespace ) {
			depth = VarDef_DepthOfScope( def, scope );
			if ( !depth ) {
				// not in the same namespace
				continue;
			}
		} else if ( def->scope != scope ) {
			// in a different function
			continue;
		} else {
			depth = 1;
		}

		if ( !bestDef || ( depth < bestDepth ) ) {
			bestDepth = depth;
			bestDef = def;
		}
	}

	// see if the name is already in use for another type
	if ( bestDef && type && ( bestDef->typeDef != type ) ) {
		Script_CompileError( "Type mismatch on redeclaration of %s", name );
	}

	return bestDef;
}

/*
================
Program_FreeDef
================
*/
void Program_FreeDef( varDef_t *def, const varDef_t *scope ) {
	varDef_t	*e;
	int			i;

	if ( def->typeDef->type == ev_vector ) {
		char name[ MAX_STRING_CHARS ];

		Com_sprintf( name, sizeof( name ), "%s_x", VarDef_Name( def ) );
		e = Program_GetDef( NULL, name, scope );
		if ( e ) {
			Program_FreeDef( e, scope );
		}

		Com_sprintf( name, sizeof( name ), "%s_y", VarDef_Name( def ) );
		e = Program_GetDef( NULL, name, scope );
		if ( e ) {
			Program_FreeDef( e, scope );
		}

		Com_sprintf( name, sizeof( name ), "%s_z", VarDef_Name( def ) );
		e = Program_GetDef( NULL, name, scope );
		if ( e ) {
			Program_FreeDef( e, scope );
		}
	}

	memmove( &varDefs[ def->num ], &varDefs[ def->num + 1 ], ( numVarDefs - def->num - 1 ) * sizeof( varDefs[0] ) );
	numVarDefs--;
	for( i = def->num; i < numVarDefs; i++ ) {
		varDefs[ i ]->num = i;
	}

	Program_DeleteVarDef( def );
}

/*
================
Program_FindFreeResultDef
================
*/
varDef_t *Program_FindFreeResultDef( typeDef_t *type, const char *name, varDef_t *scope, const varDef_t *a, const varDef_t *b ) {
	varDef_t *def;

	for( def = Program_GetDefList( name ); def != NULL; def = def->next ) {
		if ( def == a || def == b ) {
			continue;
		}
		if ( def->typeDef != type ) {
			continue;
		}
		if ( def->scope != scope ) {
			continue;
		}
		if ( def->numUsers <= 1 ) {
			continue;
		}
		return def;
	}

	return Program_AllocDef( type, name, scope, qfalse );
}

/*
================
Program_NumVarDefs / GetVarDef

for the script debugger: every variable, in the order they were defined
================
*/
int Program_NumVarDefs( void ) {
	return numVarDefs;
}

varDef_t *Program_GetVarDef( int index ) {
	return varDefs[ index ];
}

/*
================
Program_FindFunction

Searches for the specified function in the currently loaded script.  A full namespace should be
specified if not in the global namespace.

Returns 0 if function not found.
Returns >0 if function found.
================
*/
function_t *Program_FindFunction( const char *name ) {
	int			start;
	const char	*pos;
	varDef_t	*namespaceDef;
	varDef_t	*def;
	char		namespaceName[ MAX_STRING_CHARS ];

	assert( name );

	start = 0;
	namespaceDef = &def_namespace;
	do {
		pos = strstr( name + start, "::" );
		if ( !pos ) {
			break;
		}

		Q_strncpyz( namespaceName, name + start, MIN( (int)sizeof( namespaceName ), (int)( pos - ( name + start ) ) + 1 ) );
		def = Program_GetDef( NULL, namespaceName, namespaceDef );
		if ( !def ) {
			// couldn't find namespace
			return NULL;
		}
		namespaceDef = def;

		// skip past the ::
		start = pos - name + 2;
	} while( def->typeDef->type == ev_namespace );

	def = Program_GetDef( NULL, name + start, namespaceDef );
	if ( !def ) {
		// couldn't find function
		return NULL;
	}

	if ( ( def->typeDef->type == ev_function ) && ( def->value.functionPtr->eventdef == NULL ) ) {
		return def->value.functionPtr;
	}

	// is not a function, or is an eventdef
	return NULL;
}

/*
================
Program_FindObjectFunction

Searches for the specified object function in the currently loaded script.

Returns 0 if function not found.
Returns >0 if function found.
================
*/
function_t *Program_FindObjectFunction( const char *name, const typeDef_t *type ) {
	const varDef_t	*tdef;
	const varDef_t	*def;

	// look for the function
	def = NULL;
	for( tdef = type->def; tdef != &def_object; tdef = Type_SuperClass( tdef->typeDef )->def ) {
		def = Program_GetDef( NULL, name, tdef );
		if ( def ) {
			return def->value.functionPtr;
		}
	}

	return NULL;
}

/*
================
Program_AllocFunction
================
*/
function_t *Program_AllocFunction( varDef_t *def ) {
	function_t *func;

	if ( numFunctions >= MAX_FUNCS ) {
		Script_CompileError( "Exceeded maximum allowed number of functions (%d)", MAX_FUNCS );
	}

	// fill in the dfunction
	func = &functions[ numFunctions++ ];
	memset( func, 0, sizeof( *func ) );
	func->eventdef			= NULL;
	func->def				= def;
	func->type				= def->typeDef;
	func->firstStatement	= 0;
	func->numStatements		= 0;
	func->parmTotal			= 0;
	func->locals			= 0;
	func->filenum			= program_filenum;
	func->name				= Script_CopyString( VarDef_GlobalName( def ) );

	VarDef_SetFunction( def, func );

	return func;
}

/*
================
Program_ClearFunction
================
*/
static void Program_ClearFunction( function_t *func ) {
	Script_Free( func->name );
	Script_Free( func->parmSize );
	memset( func, 0, sizeof( *func ) );
}

function_t *Program_GetFunction( int index ) {
	return &functions[ index ];
}

int Program_GetFunctionIndex( const function_t *func ) {
	return func - &functions[0];
}

int Program_NumFunctions( void ) {
	return numFunctions;
}

/*
================
Program_SetEntity
================
*/
void Program_SetEntity( const char *name, gentity_t *ent ) {
	varDef_t	*def;

	def = Program_GetDef( &type_entity, va( "$%s", name ), &def_namespace );
	if ( def && ( def->initialized != DEF_STACKVARIABLE ) ) {
		// 0 is reserved for NULL entity
		*def->value.entityNumberPtr = Interp_EntityNumber( ent );
	}
}

/*
================
Program_AllocStatement
================
*/
statement_t *Program_AllocStatement( void ) {
	statement_t *ret;

	if ( numStatements >= MAX_STATEMENTS ) {
		Script_CompileError( "Exceeded maximum allowed number of statements (%d)", MAX_STATEMENTS );
	}
	ret = &statements[ numStatements++ ];
	memset( ret, 0, sizeof( *ret ) );
	return ret;
}

statement_t *Program_GetStatement( int index ) {
	return &statements[ index ];
}

int Program_NumStatements( void ) {
	return numStatements;
}

/*
================
Program_Return*
================
*/
int Program_GetReturnedInteger( void ) {
	return *program_returnDef->value.intPtr;
}

void Program_ReturnFloat( float value ) {
	*program_returnDef->value.floatPtr = value;
}

void Program_ReturnInteger( int value ) {
	*program_returnDef->value.intPtr = value;
}

void Program_ReturnVector( const vec3_t vec ) {
	VectorCopy( vec, program_returnDef->value.vectorPtr );
}

void Program_ReturnString( const char *string ) {
	Q_strncpyz( program_returnStringDef->value.stringPtr, string, MAX_STRING_LEN );
}

void Program_ReturnEntity( gentity_t *ent ) {
	*program_returnDef->value.entityNumberPtr = Interp_EntityNumber( ent );
}

/*
================
Program_GetFilename and the like
================
*/
const char *Program_GetFilename( int num ) {
	if ( num < 0 || num >= numFiles ) {
		return "";
	}
	return fileList[ num ];
}

int Program_NumFilenames( void ) {
	return numFiles;
}

int Program_GetLineNumberForStatement( int index ) {
	return statements[ index ].linenumber;
}

const char *Program_GetFilenameForStatement( int index ) {
	return Program_GetFilename( statements[ index ].file );
}

/*
==============
Program_GetFilenum
==============
*/
int Program_GetFilenum( const char *name ) {
	int i;

	if ( !strcmp( program_filename, name ) ) {
		return program_filenum;
	}

	for ( i = 0; i < numFiles; i++ ) {
		if ( !Q_stricmp( fileList[ i ], name ) ) {
			break;
		}
	}
	if ( i == numFiles ) {
		fileList = Script_Grow( fileList, &maxFiles, numFiles, sizeof( fileList[0] ) );
		fileList[ numFiles++ ] = Script_CopyString( name );
	}
	program_filenum = i;

	// save the name so that we don't have to look it up every time
	Q_strncpyz( program_filename, name, sizeof( program_filename ) );

	return program_filenum;
}

/*
==============
Program_BeginCompilation

called before compiling a batch of scripts
==============
*/
void Program_BeginCompilation( void ) {
	statement_t	*statement;

	Program_FreeData();

	// make the first statement a return for a "NULL" function
	statement = Program_AllocStatement();
	statement->linenumber	= 0;
	statement->file			= 0;
	statement->op			= OP_RETURN;
	statement->a			= NULL;
	statement->b			= NULL;
	statement->c			= NULL;

	// define the return def
	program_returnDef = Program_AllocDef( &type_vector, "<RETURN>", &def_namespace, qfalse );

	// define the return def for strings
	program_returnStringDef = Program_AllocDef( &type_string, "<RETURN>", &def_namespace, qfalse );

	// define the sys object
	sysDef = Program_AllocDef( &type_void, "sys", &def_namespace, qtrue );
}

/*
==============
Program_DisassembleStatement
==============
*/
static void Program_DisassembleStatement( fileHandle_t file, int instructionPointer ) {
	const opcode_t		*op;
	const statement_t	*statement;
	const char			*s;

	statement = &statements[ instructionPointer ];
	op = &opcodes[ statement->op ];
	s = va( "%20s(%d):\t%6d: %15s\t", Program_GetFilename( statement->file ), statement->linenumber, instructionPointer, op->opname );
	trap_FS_Write( s, strlen( s ), file );

	if ( statement->a ) {
		trap_FS_Write( "\ta: ", 4, file );
		VarDef_PrintInfo( statement->a, file, instructionPointer );
	}

	if ( statement->b ) {
		trap_FS_Write( "\tb: ", 4, file );
		VarDef_PrintInfo( statement->b, file, instructionPointer );
	}

	if ( statement->c ) {
		trap_FS_Write( "\tc: ", 4, file );
		VarDef_PrintInfo( statement->c, file, instructionPointer );
	}

	trap_FS_Write( "\n", 1, file );
}

/*
==============
Program_Disassemble
==============
*/
void Program_Disassemble( void ) {
	int					i;
	int					instructionPointer;
	const function_t	*func;
	fileHandle_t		file;
	const char			*s;

	if ( trap_FS_FOpenFile( "script/disasm.txt", &file, FS_WRITE ) < 0 || !file ) {
		return;
	}

	for( i = 0; i < numFunctions; i++ ) {
		func = &functions[ i ];
		if ( func->eventdef ) {
			// skip eventdefs
			continue;
		}

		s = va( "\nfunction %s() %d stack used, %d parms, %d locals {\n", func->name, func->locals, func->parmTotal, func->locals - func->parmTotal );
		trap_FS_Write( s, strlen( s ), file );

		for( instructionPointer = 0; instructionPointer < func->numStatements; instructionPointer++ ) {
			Program_DisassembleStatement( file, func->firstStatement + instructionPointer );
		}

		trap_FS_Write( "}\n", 2, file );
	}

	trap_FS_FCloseFile( file );
}

/*
==============
Program_FinishCompilation

Called after all files are compiled to check for errors
==============
*/
void Program_FinishCompilation( void ) {
	top_functions	= numFunctions;
	top_statements	= numStatements;
	top_types		= numTypes;
	top_defs		= numVarDefs;
	top_files		= numFiles;

	Script_Free( variableDefaults );
	numVariableDefaults = numVariables;
	variableDefaults = Script_Alloc( MAX( numVariables, 1 ) );
	memcpy( variableDefaults, variables, numVariables );
}

/*
==============
Program_CompileStats

called after all files are compiled to report memory usage.
==============
*/
static void Program_CompileStats( void ) {
	G_DPrintf( "----- Compile stats -----\n" );
	G_DPrintf( "       Files: %d\n", numFiles );
	G_DPrintf( "  Statements: %d, %d bytes\n", numStatements, (int)( numStatements * sizeof( statement_t ) ) );
	G_DPrintf( "   Functions: %d\n", numFunctions );
	G_DPrintf( "   Variables: %d bytes\n", numVariables );
	G_DPrintf( "       Types: %d, defs: %d\n", numTypes, numVarDefs );
	G_DPrintf( " Thread size: %d bytes\n", (int)sizeof( scriptThread_t ) );
}

/*
================
Program_CompileText
================
*/
qboolean Program_CompileText( const char *source, const char *text, qboolean console ) {
	const char	*error;
	varDef_t	*def;
	int			i;

	program_filenum = Program_GetFilenum( source );

	error = NULL;
	if ( !Compiler_CompileFile( text, source, console ) ) {
		error = Compiler_LastError();
	} else {
		// check to make sure all functions prototyped have code
		for( i = 0; i < numVarDefs; i++ ) {
			def = varDefs[ i ];
			if ( ( def->typeDef->type == ev_function ) && ( ( def->scope->typeDef->type == ev_namespace ) || Type_Inherits( def->scope->typeDef, &type_object ) ) ) {
				if ( !def->value.functionPtr->eventdef && !def->value.functionPtr->firstStatement ) {
					error = va( "function %s was not defined\n", VarDef_GlobalName( def ) );
					break;
				}
			}
		}
	}

	if ( error ) {
		if ( console ) {
			G_Printf( "%s\n", error );
			return qfalse;
		} else {
			G_Error( "%s\n", error );
		}
	}

	if ( !console ) {
		Program_CompileStats();
	}

	return qtrue;
}

/*
================
Program_CompileFunction
================
*/
const function_t *Program_CompileFunction( const char *functionName, const char *text ) {
	qboolean result;

	result = Program_CompileText( functionName, text, qfalse );

	if ( g_disasm.integer ) {
		Program_Disassemble();
	}

	if ( !result ) {
		G_Error( "Compile failed." );
	}

	return Program_FindFunction( functionName );
}

/*
================
Program_CompileFile
================
*/
void Program_CompileFile( const char *filename ) {
	fileHandle_t	f;
	char			*src;
	int				len;
	qboolean		result;

	len = trap_FS_FOpenFile( filename, &f, FS_READ );
	if ( len < 0 || !f ) {
		G_Error( "Couldn't load %s\n", filename );
	}
	src = Script_Alloc( len + 1 );
	trap_FS_Read( src, len, f );
	src[ len ] = '\0';
	trap_FS_FCloseFile( f );

	result = Program_CompileText( filename, src, qfalse );

	Script_Free( src );

	if ( g_disasm.integer ) {
		Program_Disassemble();
	}

	if ( !result ) {
		G_Error( "Compile failed in file %s.", filename );
	}
}

/*
================
Program_FreeData
================
*/
void Program_FreeData( void ) {
	int i;

	// free the defs
	for ( i = 0; i < numVarDefs; i++ ) {
		Script_Free( varDefs[ i ] );
	}
	Script_Free( varDefs );
	varDefs = NULL;
	numVarDefs = maxVarDefs = 0;

	for ( i = 0; i < numVarDefNames; i++ ) {
		Script_Free( varDefNames[ i ]->name );
		Script_Free( varDefNames[ i ] );
	}
	Script_Free( varDefNames );
	varDefNames = NULL;
	numVarDefNames = maxVarDefNames = 0;
	memset( varDefNameHash, 0, sizeof( varDefNameHash ) );

	program_returnDef		= NULL;
	program_returnStringDef	= NULL;
	sysDef					= NULL;

	// free any special types we've created
	for ( i = 0; i < numTypes; i++ ) {
		Type_Free( types[ i ] );
		Script_Free( types[ i ] );
	}
	Script_Free( types );
	types = NULL;
	numTypes = maxTypes = 0;

	// the basic types' allocated lists (type_pointer's pointer type is all that changes)
	type_pointer.auxType = NULL;

	program_filenum = 0;
	numVariables = 0;
	memset( variablesStore, 0, sizeof( variablesStore ) );
	Script_Free( variableDefaults );
	variableDefaults = NULL;
	numVariableDefaults = 0;

	// clear all the strings in the functions so that it doesn't look like we're leaking memory.
	for( i = 0; i < numFunctions; i++ ) {
		Program_ClearFunction( &functions[ i ] );
	}
	numFunctions = 0;
	numStatements = 0;

	for ( i = 0; i < numFiles; i++ ) {
		Script_Free( fileList[ i ] );
	}
	Script_Free( fileList );
	fileList = NULL;
	numFiles = maxFiles = 0;
	program_filename[ 0 ] = '\0';

	top_functions	= 0;
	top_statements	= 0;
	top_types		= 0;
	top_defs		= 0;
	top_files		= 0;
}

/*
================
Program_Startup
================
*/
void Program_Startup( const char *defaultScript ) {
	G_Printf( "Initializing scripts\n" );

	// make sure all data is freed up
	Thread_Restart();

	// get ready for loading scripts
	Program_BeginCompilation();

	// load the default script
	if ( defaultScript && *defaultScript ) {
		Program_CompileFile( defaultScript );
	}

	Program_FinishCompilation();
}

/*
================
Program_Restart

Restores all variables to their initial value
================
*/
void Program_Restart( void ) {
	int i;

	Thread_Restart();

	//
	// since there may have been a script loaded by the map or the user may
	// have typed "script" from the console, free up any types and vardefs that
	// have been allocated after the initial startup
	//
	for( i = top_types; i < numTypes; i++ ) {
		Type_Free( types[ i ] );
		Script_Free( types[ i ] );
	}
	numTypes = top_types;

	for( i = numVarDefs - 1; i >= top_defs; i-- ) {
		Program_DeleteVarDef( varDefs[ i ] );
	}
	numVarDefs = top_defs;

	for( i = top_functions; i < numFunctions; i++ ) {
		Program_ClearFunction( &functions[ i ] );
	}
	numFunctions = top_functions;

	numStatements = top_statements;

	for ( i = top_files; i < numFiles; i++ ) {
		Script_Free( fileList[ i ] );
	}
	numFiles = top_files;
	program_filename[ 0 ] = '\0';

	// reset the variables to their default values
	numVariables = numVariableDefaults;
	memcpy( variables, variableDefaults, numVariables );
}

/*
================
Program_CalculateChecksum

so a save is loaded only with the scripts it was made with
================
*/
int Program_CalculateChecksum( void ) {
	typedef struct {
		int				op;
		int				a;
		int				b;
		int				c;
		int				linenumber;
		int				file;
	} statementBlock_t;
	statementBlock_t	*statementList;
	int					i, result;

	statementList = Script_Alloc( MAX( numStatements, 1 ) * sizeof( statementBlock_t ) );

	// Copy info into new list, using the variable numbers instead of a pointer to the variable
	for( i = 0; i < numStatements; i++ ) {
		statementList[i].op = statements[i].op;
		statementList[i].a = statements[i].a ? statements[i].a->num : -1;
		statementList[i].b = statements[i].b ? statements[i].b->num : -1;
		statementList[i].c = statements[i].c ? statements[i].c->num : -1;
		statementList[i].linenumber = statements[i].linenumber;
		statementList[i].file = statements[i].file;
	}

	result = Com_BlockChecksum( statementList, sizeof( statementBlock_t ) * numStatements );

	Script_Free( statementList );

	return result;
}

/*
================
Program_Save

the variables that changed since the scripts were compiled, and the checksum
of the code (g_save.c's G_SaveBytes)
================
*/
void Program_Save( void ) {
	int i, num;

	for ( i = 0; i < numVariableDefaults; i++ ) {
		if ( variables[ i ] != variableDefaults[ i ] ) {
			G_SaveBytes( &i, sizeof( i ) );
			G_SaveBytes( &variables[ i ], 1 );
		}
	}

	// Mark the end of the diff with default variables with -1
	num = -1;
	G_SaveBytes( &num, sizeof( num ) );

	G_SaveBytes( &numVariables, sizeof( numVariables ) );
	if ( numVariables > numVariableDefaults ) {
		G_SaveBytes( &variables[ numVariableDefaults ], numVariables - numVariableDefaults );
	}

	num = Program_CalculateChecksum();
	G_SaveBytes( &num, sizeof( num ) );
}

/*
================
Program_Restore

the variables as they were saved, over their defaults; qfalse if the code
isn't that of the save (the variables are then left as they were)
================
*/
qboolean Program_Restore( void ) {
	static byte	saved[ MAX_GLOBALS ];
	int			num, index;
	int			saved_checksum;

	memcpy( saved, variableDefaults, numVariableDefaults );

	G_LoadBytes( &index, sizeof( index ) );
	while( index >= 0 ) {
		if ( index >= numVariableDefaults ) {
			G_SaveFailed();
			return qfalse;
		}
		G_LoadBytes( &saved[ index ], 1 );
		G_LoadBytes( &index, sizeof( index ) );
	}

	G_LoadBytes( &num, sizeof( num ) );
	if ( num < numVariableDefaults || num > MAX_GLOBALS ) {
		G_SaveFailed();
		return qfalse;
	}
	G_LoadBytes( &saved[ numVariableDefaults ], num - numVariableDefaults );

	G_LoadBytes( &saved_checksum, sizeof( saved_checksum ) );
	if ( saved_checksum != Program_CalculateChecksum() || num != numVariables ) {
		G_Printf( S_COLOR_YELLOW "WARNING: the scripts aren't those the game was saved with: their state isn't loaded\n" );
		return qfalse;
	}

	memcpy( variables, saved, numVariables );
	return qtrue;
}
