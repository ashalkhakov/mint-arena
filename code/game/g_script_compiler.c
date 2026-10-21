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
// g_script_compiler.c -- Doom 3's Script_Compiler.cpp: the scripts' compiler,
// on the precompiler (l_precomp.c) Doom 3's idParser grew from.
//
// idCompileError's throw is a longjmp back to Compiler_CompileFile.

#include "g_local.h"
#include "../idlib/l_script.h"
#include "../idlib/l_precomp.h"
#include <setjmp.h>

#define FUNCTION_PRIORITY	2
#define INT_PRIORITY		2
#define NOT_PRIORITY		5
#define TILDE_PRIORITY		5
#define TOP_PRIORITY		7

static const char *punctuation[] = {
	"+=", "-=", "*=", "/=", "%=", "&=", "|=", "++", "--",
	"&&", "||", "<=", ">=", "==", "!=", "::", ";",  ",",
	"~",  "!",  "*",  "/",  "%",  "(",   ")",  "-", "+",
	"=",  "[",  "]",  ".",  "<",  ">" ,  "&",  "|", ":",  NULL
};

const opcode_t opcodes[] = {
	{ "<RETURN>", "RETURN", -1, qfalse, &def_void, &def_void, &def_void },

	{ "++", "UINC_F", 1, qtrue, &def_float, &def_void, &def_void },
	{ "++", "UINCP_F", 1, qtrue, &def_object, &def_field, &def_float },
	{ "--", "UDEC_F", 1, qtrue, &def_float, &def_void, &def_void },
	{ "--", "UDECP_F", 1, qtrue, &def_object, &def_field, &def_float },

	{ "~", "COMP_F", -1, qfalse, &def_float, &def_void, &def_float },

	{ "*", "MUL_F", 3, qfalse, &def_float, &def_float, &def_float },
	{ "*", "MUL_V", 3, qfalse, &def_vector, &def_vector, &def_float },
	{ "*", "MUL_FV", 3, qfalse, &def_float, &def_vector, &def_vector },
	{ "*", "MUL_VF", 3, qfalse, &def_vector, &def_float, &def_vector },

	{ "/", "DIV", 3, qfalse, &def_float, &def_float, &def_float },
	{ "%", "MOD_F",	3, qfalse, &def_float, &def_float, &def_float },

	{ "+", "ADD_F", 4, qfalse, &def_float, &def_float, &def_float },
	{ "+", "ADD_V", 4, qfalse, &def_vector, &def_vector, &def_vector },
	{ "+", "ADD_S", 4, qfalse, &def_string, &def_string, &def_string },
	{ "+", "ADD_FS", 4, qfalse, &def_float, &def_string, &def_string },
	{ "+", "ADD_SF", 4, qfalse, &def_string, &def_float, &def_string },
	{ "+", "ADD_VS", 4, qfalse, &def_vector, &def_string, &def_string },
	{ "+", "ADD_SV", 4, qfalse, &def_string, &def_vector, &def_string },

	{ "-", "SUB_F", 4, qfalse, &def_float, &def_float, &def_float },
	{ "-", "SUB_V", 4, qfalse, &def_vector, &def_vector, &def_vector },

	{ "==", "EQ_F", 5, qfalse, &def_float, &def_float, &def_float },
	{ "==", "EQ_V", 5, qfalse, &def_vector, &def_vector, &def_float },
	{ "==", "EQ_S", 5, qfalse, &def_string, &def_string, &def_float },
	{ "==", "EQ_E", 5, qfalse, &def_entity, &def_entity, &def_float },
	{ "==", "EQ_EO", 5, qfalse, &def_entity, &def_object, &def_float },
	{ "==", "EQ_OE", 5, qfalse, &def_object, &def_entity, &def_float },
	{ "==", "EQ_OO", 5, qfalse, &def_object, &def_object, &def_float },

	{ "!=", "NE_F", 5, qfalse, &def_float, &def_float, &def_float },
	{ "!=", "NE_V", 5, qfalse, &def_vector, &def_vector, &def_float },
	{ "!=", "NE_S", 5, qfalse, &def_string, &def_string, &def_float },
	{ "!=", "NE_E", 5, qfalse, &def_entity, &def_entity, &def_float },
	{ "!=", "NE_EO", 5, qfalse, &def_entity, &def_object, &def_float },
	{ "!=", "NE_OE", 5, qfalse, &def_object, &def_entity, &def_float },
	{ "!=", "NE_OO", 5, qfalse, &def_object, &def_object, &def_float },

	{ "<=", "LE", 5, qfalse, &def_float, &def_float, &def_float },
	{ ">=", "GE", 5, qfalse, &def_float, &def_float, &def_float },
	{ "<", "LT", 5, qfalse, &def_float, &def_float, &def_float },
	{ ">", "GT", 5, qfalse, &def_float, &def_float, &def_float },

	{ ".", "INDIRECT_F", 1, qfalse, &def_object, &def_field, &def_float },
	{ ".", "INDIRECT_V", 1, qfalse, &def_object, &def_field, &def_vector },
	{ ".", "INDIRECT_S", 1, qfalse, &def_object, &def_field, &def_string },
	{ ".", "INDIRECT_E", 1, qfalse, &def_object, &def_field, &def_entity },
	{ ".", "INDIRECT_BOOL", 1, qfalse, &def_object, &def_field, &def_boolean },
	{ ".", "INDIRECT_OBJ", 1, qfalse, &def_object, &def_field, &def_object },

	{ ".", "ADDRESS", 1, qfalse, &def_entity, &def_field, &def_pointer },

	{ ".", "EVENTCALL", 2, qfalse, &def_entity, &def_function, &def_void },
	{ ".", "OBJECTCALL", 2, qfalse, &def_object, &def_function, &def_void },
	{ ".", "SYSCALL", 2, qfalse, &def_void, &def_function, &def_void },

	{ "=", "STORE_F", 6, qtrue, &def_float, &def_float, &def_float },
	{ "=", "STORE_V", 6, qtrue, &def_vector, &def_vector, &def_vector },
	{ "=", "STORE_S", 6, qtrue, &def_string, &def_string, &def_string },
	{ "=", "STORE_ENT", 6, qtrue, &def_entity, &def_entity, &def_entity },
	{ "=", "STORE_BOOL", 6, qtrue, &def_boolean, &def_boolean, &def_boolean },
	{ "=", "STORE_OBJENT", 6, qtrue, &def_object, &def_entity, &def_object },
	{ "=", "STORE_OBJ", 6, qtrue, &def_object, &def_object, &def_object },
	{ "=", "STORE_OBJENT", 6, qtrue, &def_entity, &def_object, &def_object },

	{ "=", "STORE_FTOS", 6, qtrue, &def_string, &def_float, &def_string },
	{ "=", "STORE_BTOS", 6, qtrue, &def_string, &def_boolean, &def_string },
	{ "=", "STORE_VTOS", 6, qtrue, &def_string, &def_vector, &def_string },
	{ "=", "STORE_FTOBOOL", 6, qtrue, &def_boolean, &def_float, &def_boolean },
	{ "=", "STORE_BOOLTOF", 6, qtrue, &def_float, &def_boolean, &def_float },

	{ "=", "STOREP_F", 6, qtrue, &def_pointer, &def_float, &def_float },
	{ "=", "STOREP_V", 6, qtrue, &def_pointer, &def_vector, &def_vector },
	{ "=", "STOREP_S", 6, qtrue, &def_pointer, &def_string, &def_string },
	{ "=", "STOREP_ENT", 6, qtrue, &def_pointer, &def_entity, &def_entity },
	{ "=", "STOREP_FLD", 6, qtrue, &def_pointer, &def_field, &def_field },
	{ "=", "STOREP_BOOL", 6, qtrue, &def_pointer, &def_boolean, &def_boolean },
	{ "=", "STOREP_OBJ", 6, qtrue, &def_pointer, &def_object, &def_object },
	{ "=", "STOREP_OBJENT", 6, qtrue, &def_pointer, &def_object, &def_object },

	{ "<=>", "STOREP_FTOS", 6, qtrue, &def_pointer, &def_float, &def_string },
	{ "<=>", "STOREP_BTOS", 6, qtrue, &def_pointer, &def_boolean, &def_string },
	{ "<=>", "STOREP_VTOS", 6, qtrue, &def_pointer, &def_vector, &def_string },
	{ "<=>", "STOREP_FTOBOOL", 6, qtrue, &def_pointer, &def_float, &def_boolean },
	{ "<=>", "STOREP_BOOLTOF", 6, qtrue, &def_pointer, &def_boolean, &def_float },

	{ "*=", "UMUL_F", 6, qtrue, &def_float, &def_float, &def_void },
	{ "*=", "UMUL_V", 6, qtrue, &def_vector, &def_float, &def_void },
	{ "/=", "UDIV_F", 6, qtrue, &def_float, &def_float, &def_void },
	{ "/=", "UDIV_V", 6, qtrue, &def_vector, &def_float, &def_void },
	{ "%=", "UMOD_F", 6, qtrue, &def_float, &def_float, &def_void },
	{ "+=", "UADD_F", 6, qtrue, &def_float, &def_float, &def_void },
	{ "+=", "UADD_V", 6, qtrue, &def_vector, &def_vector, &def_void },
	{ "-=", "USUB_F", 6, qtrue, &def_float, &def_float, &def_void },
	{ "-=", "USUB_V", 6, qtrue, &def_vector, &def_vector, &def_void },
	{ "&=", "UAND_F", 6, qtrue, &def_float, &def_float, &def_void },
	{ "|=", "UOR_F", 6, qtrue, &def_float, &def_float, &def_void },

	{ "!", "NOT_BOOL", -1, qfalse, &def_boolean, &def_void, &def_float },
	{ "!", "NOT_F", -1, qfalse, &def_float, &def_void, &def_float },
	{ "!", "NOT_V", -1, qfalse, &def_vector, &def_void, &def_float },
	{ "!", "NOT_S", -1, qfalse, &def_vector, &def_void, &def_float },
	{ "!", "NOT_ENT", -1, qfalse, &def_entity, &def_void, &def_float },

	{ "<NEG_F>", "NEG_F", -1, qfalse, &def_float, &def_void, &def_float },
	{ "<NEG_V>", "NEG_V", -1, qfalse, &def_vector, &def_void, &def_vector },

	{ "int", "INT_F", -1, qfalse, &def_float, &def_void, &def_float },

	{ "<IF>", "IF", -1, qfalse, &def_float, &def_jumpoffset, &def_void },
	{ "<IFNOT>", "IFNOT", -1, qfalse, &def_float, &def_jumpoffset, &def_void },

	// calls returns REG_RETURN
	{ "<CALL>", "CALL", -1, qfalse, &def_function, &def_argsize, &def_void },
	{ "<THREAD>", "THREAD", -1, qfalse, &def_function, &def_argsize, &def_void },
	{ "<THREAD>", "OBJTHREAD", -1, qfalse, &def_function, &def_argsize, &def_void },

	{ "<PUSH>", "PUSH_F", -1, qfalse, &def_float, &def_float, &def_void },
	{ "<PUSH>", "PUSH_V", -1, qfalse, &def_vector, &def_vector, &def_void },
	{ "<PUSH>", "PUSH_S", -1, qfalse, &def_string, &def_string, &def_void },
	{ "<PUSH>", "PUSH_ENT", -1, qfalse, &def_entity, &def_entity, &def_void },
	{ "<PUSH>", "PUSH_OBJ", -1, qfalse, &def_object, &def_object, &def_void },
	{ "<PUSH>", "PUSH_OBJENT", -1, qfalse, &def_entity, &def_object, &def_void },
	{ "<PUSH>", "PUSH_FTOS", -1, qfalse, &def_string, &def_float, &def_void },
	{ "<PUSH>", "PUSH_BTOF", -1, qfalse, &def_float, &def_boolean, &def_void },
	{ "<PUSH>", "PUSH_FTOB", -1, qfalse, &def_boolean, &def_float, &def_void },
	{ "<PUSH>", "PUSH_VTOS", -1, qfalse, &def_string, &def_vector, &def_void },
	{ "<PUSH>", "PUSH_BTOS", -1, qfalse, &def_string, &def_boolean, &def_void },

	{ "<GOTO>", "GOTO", -1, qfalse, &def_jumpoffset, &def_void, &def_void },

	{ "&&", "AND", 7, qfalse, &def_float, &def_float, &def_float },
	{ "&&", "AND_BOOLF", 7, qfalse, &def_boolean, &def_float, &def_float },
	{ "&&", "AND_FBOOL", 7, qfalse, &def_float, &def_boolean, &def_float },
	{ "&&", "AND_BOOLBOOL", 7, qfalse, &def_boolean, &def_boolean, &def_float },
	{ "||", "OR", 7, qfalse, &def_float, &def_float, &def_float },
	{ "||", "OR_BOOLF", 7, qfalse, &def_boolean, &def_float, &def_float },
	{ "||", "OR_FBOOL", 7, qfalse, &def_float, &def_boolean, &def_float },
	{ "||", "OR_BOOLBOOL", 7, qfalse, &def_boolean, &def_boolean, &def_float },

	{ "&", "BITAND", 3, qfalse, &def_float, &def_float, &def_float },
	{ "|", "BITOR", 3, qfalse, &def_float, &def_float, &def_float },

	{ "<BREAK>", "BREAK", -1, qfalse, &def_float, &def_void, &def_void },
	{ "<CONTINUE>", "CONTINUE", -1, qfalse, &def_float, &def_void, &def_void },

	{ NULL }
};

// the compiler's state (idCompiler's members)
static source_t		*source;
static token_t		token;
static typeDef_t	*immediateType;
static eval_t		immediate;

static qboolean		eof;
static qboolean		console;
static qboolean		callthread;
static int			braceDepth;
static int			loopDepth;
static int			currentLineNumber;
static int			currentFileNumber;

static varDef_t		*scope;				// the function being parsed, or NULL
static const varDef_t *basetype;		// for accessing fields

static qboolean		compiling;
static jmp_buf		compileError;
static char			errorText[ 1024 ];

static varDef_t		*GetExpression( int priority );
static void			ParseStatement( void );
static void			ParseFunctionDef( typeDef_t *returnType, const char *name );
static void			ParseDefs( void );
static void			ParseNamespace( varDef_t *newScope );
static varDef_t		*ParseObjectCall( varDef_t *object, varDef_t *func );
static varDef_t		*ParseEventCall( varDef_t *object, varDef_t *func );

/*
============
Script_CompileError

Aborts the compilation of the current function (idCompileError).
============
*/
void QDECL Script_CompileError( const char *fmt, ... ) {
	va_list	argptr;

	va_start( argptr, fmt );
	Q_vsnprintf( errorText, sizeof( errorText ), fmt, argptr );
	va_end( argptr );

	if ( !compiling ) {
		G_Error( "%s", errorText );
	}
	longjmp( compileError, 1 );
}

#define Error		Script_CompileError

/*
============
TokenIs
============
*/
static ID_INLINE qboolean TokenIs( const char *string ) {
	return !strcmp( token.string, string );
}

/*
============
CurrentFileName

the file the parser reads from (an included one too)
============
*/
static const char *CurrentFileName( void ) {
	if ( source->scriptstack ) {
		return source->scriptstack->filename;
	}
	return source->filename;
}

/*
============
VirtualFunctionConstant

Creates a def for an index into a virtual function table
============
*/
static varDef_t *GetImmediate( typeDef_t *type, const eval_t *eval, const char *string );

static ID_INLINE varDef_t *VirtualFunctionConstant( varDef_t *func ) {
	eval_t eval;

	memset( &eval, 0, sizeof( eval ) );
	eval._int = Type_GetFunctionNumber( func->scope->typeDef, func->value.functionPtr );
	if ( eval._int < 0 ) {
		Error( "Function '%s' not found in scope '%s'", VarDef_Name( func ), VarDef_Name( func->scope ) );
	}

	return GetImmediate( &type_virtualfunction, &eval, "" );
}

/*
============
SizeConstant

Creates a def for a size constant
============
*/
static ID_INLINE varDef_t *SizeConstant( int size ) {
	eval_t eval;

	memset( &eval, 0, sizeof( eval ) );
	eval._int = size;
	return GetImmediate( &type_argsize, &eval, "" );
}

/*
============
JumpConstant

Creates a def for a jump constant
============
*/
static ID_INLINE varDef_t *JumpConstant( int value ) {
	eval_t eval;

	memset( &eval, 0, sizeof( eval ) );
	eval._int = value;
	return GetImmediate( &type_jumpoffset, &eval, "" );
}

/*
============
JumpDef

Creates a def for a relative jump from one code location to another
============
*/
static ID_INLINE varDef_t *JumpDef( int jumpfrom, int jumpto ) {
	return JumpConstant( jumpto - jumpfrom );
}

/*
============
JumpTo

Creates a def for a relative jump from current code location
============
*/
static ID_INLINE varDef_t *JumpTo( int jumpto ) {
	return JumpDef( Program_NumStatements(), jumpto );
}

/*
============
JumpFrom

Creates a def for a relative jump from code location to current code location
============
*/
static ID_INLINE varDef_t *JumpFrom( int jumpfrom ) {
	return JumpDef( jumpfrom, Program_NumStatements() );
}

/*
============
Divide
============
*/
static ID_INLINE float Divide( float numerator, float denominator ) {
	if ( denominator == 0 ) {
		Error( "Divide by zero" );
	}

	return numerator / denominator;
}

/*
============
FindImmediate

tries to find an existing immediate with the same value
============
*/
static varDef_t *FindImmediate( const typeDef_t *type, const eval_t *eval, const char *string ) {
	varDef_t	*def;
	etype_t		etype;

	etype = type->type;

	// check for a constant with the same value
	for( def = Program_GetDefList( "<IMMEDIATE>" ); def != NULL; def = def->next ) {
		if ( def->typeDef != type ) {
			continue;
		}

		switch( etype ) {
		case ev_field :
			if ( *def->value.intPtr == eval->_int ) {
				return def;
			}
			break;

		case ev_argsize :
			if ( def->value.argSize == eval->_int ) {
				return def;
			}
			break;

		case ev_jumpoffset :
			if ( def->value.jumpOffset == eval->_int ) {
				return def;
			}
			break;

		case ev_entity :
			if ( *def->value.intPtr == eval->entity ) {
				return def;
			}
			break;

		case ev_string :
			if ( strcmp( def->value.stringPtr, string ) == 0 ) {
				return def;
			}
			break;

		case ev_float :
			if ( *def->value.floatPtr == eval->_float ) {
				return def;
			}
			break;

		case ev_virtualfunction :
			if ( def->value.virtualFunction == eval->_int ) {
				return def;
			}
			break;


		case ev_vector :
			if ( ( def->value.vectorPtr[ 0 ] == eval->vector[ 0 ] ) &&
				( def->value.vectorPtr[ 1 ] == eval->vector[ 1 ] ) &&
				( def->value.vectorPtr[ 2 ] == eval->vector[ 2 ] ) ) {
				return def;
			}
			break;

		default :
			Error( "weird immediate type" );
			break;
		}
	}

	return NULL;
}

/*
============
GetImmediate

returns an existing immediate with the same value, or allocates a new one
============
*/
static varDef_t *GetImmediate( typeDef_t *type, const eval_t *eval, const char *string ) {
	varDef_t *def;

	def = FindImmediate( type, eval, string );
	if ( def ) {
		def->numUsers++;
	} else {
		// allocate a new def
		def = Program_AllocDef( type, "<IMMEDIATE>", &def_namespace, qtrue );
		if ( type->type == ev_string ) {
			VarDef_SetString( def, string, qtrue );
		} else {
			VarDef_SetValue( def, eval, qtrue );
		}
	}

	return def;
}

/*
============
OptimizeOpcode

try to optimize when the operator works on constants only
============
*/
static varDef_t *OptimizeOpcode( const opcode_t *op, varDef_t *var_a, varDef_t *var_b ) {
	eval_t		c;
	typeDef_t	*type;

	if ( var_a && var_a->initialized != DEF_INITIALIZEDCONSTANT ) {
		return NULL;
	}
	if ( var_b && var_b->initialized != DEF_INITIALIZEDCONSTANT ) {
		return NULL;
	}

	memset( &c, 0, sizeof( c ) );
	switch( op - opcodes ) {
		case OP_ADD_F:		c._float = *var_a->value.floatPtr + *var_b->value.floatPtr; type = &type_float; break;
		case OP_ADD_V:		VectorAdd( var_a->value.vectorPtr, var_b->value.vectorPtr, c.vector ); type = &type_vector; break;
		case OP_SUB_F:		c._float = *var_a->value.floatPtr - *var_b->value.floatPtr; type = &type_float; break;
		case OP_SUB_V:		VectorSubtract( var_a->value.vectorPtr, var_b->value.vectorPtr, c.vector ); type = &type_vector; break;
		case OP_MUL_F:		c._float = *var_a->value.floatPtr * *var_b->value.floatPtr; type = &type_float; break;
		case OP_MUL_V:		c._float = DotProduct( var_a->value.vectorPtr, var_b->value.vectorPtr ); type = &type_float; break;
		case OP_MUL_FV:		VectorScale( var_b->value.vectorPtr, *var_a->value.floatPtr, c.vector ); type = &type_vector; break;
		case OP_MUL_VF:		VectorScale( var_a->value.vectorPtr, *var_b->value.floatPtr, c.vector ); type = &type_vector; break;
		case OP_DIV_F:		c._float = Divide( *var_a->value.floatPtr, *var_b->value.floatPtr ); type = &type_float; break;
		case OP_MOD_F:		c._float = (int)*var_a->value.floatPtr % (int)*var_b->value.floatPtr; type = &type_float; break;
		case OP_BITAND:		c._float = ( int )*var_a->value.floatPtr & ( int )*var_b->value.floatPtr; type = &type_float; break;
		case OP_BITOR:		c._float = ( int )*var_a->value.floatPtr | ( int )*var_b->value.floatPtr; type = &type_float; break;
		case OP_GE:			c._float = *var_a->value.floatPtr >= *var_b->value.floatPtr; type = &type_float; break;
		case OP_LE:			c._float = *var_a->value.floatPtr <= *var_b->value.floatPtr; type = &type_float; break;
		case OP_GT:			c._float = *var_a->value.floatPtr > *var_b->value.floatPtr; type = &type_float; break;
		case OP_LT:			c._float = *var_a->value.floatPtr < *var_b->value.floatPtr; type = &type_float; break;
		case OP_AND:		c._float = *var_a->value.floatPtr && *var_b->value.floatPtr; type = &type_float; break;
		case OP_OR:			c._float = *var_a->value.floatPtr || *var_b->value.floatPtr; type = &type_float; break;
		case OP_NOT_BOOL:	c._int = !*var_a->value.intPtr; type = &type_boolean; break;
		case OP_NOT_F:		c._float = !*var_a->value.floatPtr; type = &type_float; break;
		case OP_NOT_V:		c._float = !var_a->value.vectorPtr[0] && !var_a->value.vectorPtr[1] && !var_a->value.vectorPtr[2]; type = &type_float; break;
		case OP_NEG_F:		c._float = -*var_a->value.floatPtr; type = &type_float; break;
		case OP_NEG_V:		VectorNegate( var_a->value.vectorPtr, c.vector ); type = &type_vector; break;
		case OP_INT_F:		c._float = ( int )*var_a->value.floatPtr; type = &type_float; break;
		case OP_EQ_F:		c._float = ( *var_a->value.floatPtr == *var_b->value.floatPtr ); type = &type_float; break;
		case OP_EQ_V:		c._float = VectorCompare( var_a->value.vectorPtr, var_b->value.vectorPtr ); type = &type_float; break;
		case OP_EQ_E:		c._float = ( *var_a->value.intPtr == *var_b->value.intPtr ); type = &type_float; break;
		case OP_NE_F:		c._float = ( *var_a->value.floatPtr != *var_b->value.floatPtr ); type = &type_float; break;
		case OP_NE_V:		c._float = !VectorCompare( var_a->value.vectorPtr, var_b->value.vectorPtr ); type = &type_float; break;
		case OP_NE_E:		c._float = ( *var_a->value.intPtr != *var_b->value.intPtr ); type = &type_float; break;
		case OP_UADD_F:		c._float = *var_b->value.floatPtr + *var_a->value.floatPtr; type = &type_float; break;
		case OP_USUB_F:		c._float = *var_b->value.floatPtr - *var_a->value.floatPtr; type = &type_float; break;
		case OP_UMUL_F:		c._float = *var_b->value.floatPtr * *var_a->value.floatPtr; type = &type_float; break;
		case OP_UDIV_F:		c._float = Divide( *var_b->value.floatPtr, *var_a->value.floatPtr ); type = &type_float; break;
		case OP_UMOD_F:		c._float = ( int ) *var_b->value.floatPtr % ( int )*var_a->value.floatPtr; type = &type_float; break;
		case OP_UOR_F:		c._float = ( int )*var_b->value.floatPtr | ( int )*var_a->value.floatPtr; type = &type_float; break;
		case OP_UAND_F:		c._float = ( int )*var_b->value.floatPtr & ( int )*var_a->value.floatPtr; type = &type_float; break;
		case OP_UINC_F:		c._float = *var_a->value.floatPtr + 1; type = &type_float; break;
		case OP_UDEC_F:		c._float = *var_a->value.floatPtr - 1; type = &type_float; break;
		case OP_COMP_F:		c._float = ( float )~( int )*var_a->value.floatPtr; type = &type_float; break;
		default:			type = NULL; break;
	}

	if ( !type ) {
		return NULL;
	}

	if ( var_a ) {
		var_a->numUsers--;
		if ( var_a->numUsers <= 0 ) {
			Program_FreeDef( var_a, NULL );
		}
	}
	if ( var_b ) {
		var_b->numUsers--;
		if ( var_b->numUsers <= 0 ) {
			Program_FreeDef( var_b, NULL );
		}
	}

	return GetImmediate( type, &c, "" );
}

/*
============
EmitOpcode

Emits a primitive statement, returning the var it places it's value in
============
*/
static varDef_t *EmitOpcodeOp( const opcode_t *op, varDef_t *var_a, varDef_t *var_b ) {
	statement_t	*statement;
	varDef_t	*var_c;

	var_c = OptimizeOpcode( op, var_a, var_b );
	if ( var_c ) {
		return var_c;
	}

	if ( var_a && !strcmp( VarDef_Name( var_a ), RESULT_STRING ) ) {
		var_a->numUsers++;
	}
	if ( var_b && !strcmp( VarDef_Name( var_b ), RESULT_STRING ) ) {
		var_b->numUsers++;
	}

	statement = Program_AllocStatement();
	statement->linenumber	= currentLineNumber;
	statement->file 		= currentFileNumber;

	if ( ( op->type_c == &def_void ) || op->rightAssociative ) {
		// ifs, gotos, and assignments don't need vars allocated
		var_c = NULL;
	} else {
		// allocate result space
		// try to reuse result defs as much as possible
		var_c = Program_FindFreeResultDef( op->type_c->typeDef, RESULT_STRING, scope, var_a, var_b );
		// set user count back to 1, a result def needs to be used twice before it can be reused
		var_c->numUsers = 1;
	}

	statement->op	= op - opcodes;
	statement->a	= var_a;
	statement->b	= var_b;
	statement->c	= var_c;

	if ( op->rightAssociative ) {
		return var_a;
	}

	return var_c;
}

static ID_INLINE varDef_t *EmitOpcode( int op, varDef_t *var_a, varDef_t *var_b ) {
	return EmitOpcodeOp( &opcodes[ op ], var_a, var_b );
}

/*
============
EmitPush

Emits an opcode to push the variable onto the stack.
============
*/
static qboolean EmitPush( varDef_t *expression, const typeDef_t *funcArg ) {
	const opcode_t *op;
	const opcode_t *out;

	out = NULL;
	for( op = &opcodes[ OP_PUSH_F ]; op->name && !strcmp( op->name, "<PUSH>" ); op++ ) {
		if ( ( funcArg->type == op->type_a->typeDef->type ) && ( VarDef_Type( expression ) == op->type_b->typeDef->type ) ) {
			out = op;
			break;
		}
	}

	if ( !out ) {
		if ( ( expression->typeDef != funcArg ) && !Type_Inherits( expression->typeDef, funcArg ) ) {
			return qfalse;
		}

		out = &opcodes[ OP_PUSH_ENT ];
	}

	EmitOpcodeOp( out, expression, 0 );

	return qtrue;
}

/*
==============
NextToken

Sets token, immediateType, and possibly immediate
==============
*/
static void NextToken( void ) {
	int		i;
	float	v[ 3 ];
	char	rest[ 2 ];

	// reset our type
	immediateType = NULL;
	memset( &immediate, 0, sizeof( immediate ) );

	// Save the token's line number and filename since when we emit opcodes the current
	// token is always the next one to be read
	currentLineNumber = token.line;
	currentFileNumber = Program_GetFilenum( CurrentFileName() );

	if ( !PC_ReadToken( source, &token ) ) {
		eof = qtrue;
		return;
	}

	if ( currentFileNumber != Program_GetFilenum( CurrentFileName() ) ) {
		if ( ( braceDepth > 0 ) && !TokenIs( "}" ) ) {
			// missing a closing brace.  try to give as much info as possible.
			if ( scope->typeDef->type == ev_function ) {
				Error( "Unexpected end of file inside function '%s'.  Missing closing braces.", VarDef_Name( scope ) );
			} else if ( scope->typeDef->type == ev_object ) {
				Error( "Unexpected end of file inside object '%s'.  Missing closing braces.", VarDef_Name( scope ) );
			} else if ( scope->typeDef->type == ev_namespace ) {
				Error( "Unexpected end of file inside namespace '%s'.  Missing closing braces.", VarDef_Name( scope ) );
			} else {
				Error( "Unexpected end of file inside braced section" );
			}
		}
	}

	switch( token.type ) {
	case TT_STRING:
		// handle quoted strings as a unit
		StripDoubleQuotes( token.string );
		immediateType = &type_string;
		return;

	case TT_LITERAL: {
		// handle quoted vectors as a unit
		StripSingleQuotes( token.string );
		immediateType = &type_vector;
		i = sscanf( token.string, "%f %f %f %1s", &v[ 0 ], &v[ 1 ], &v[ 2 ], rest );
		if ( i != 3 ) {
			Error( "vector '%s' is not in the form of 'x y z'", token.string );
		}
		VectorCopy( v, immediate.vector );
		return;
	}

	case TT_NUMBER:
		immediateType = &type_float;
		immediate._float = token.floatvalue;
		return;

	case TT_PUNCTUATION:
		// entity names
		if ( TokenIs( "$" ) ) {
			immediateType = &type_entity;
			PC_ReadToken( source, &token );
			return;
		}

		if ( TokenIs( "{" ) ) {
			braceDepth++;
			return;
		}

		if ( TokenIs( "}" ) ) {
			braceDepth--;
			return;
		}

		for ( i = 0; punctuation[ i ]; i++ ) {
			if ( TokenIs( punctuation[ i ] ) ) {
				return;
			}
		}

		Error( "Unknown punctuation '%s'", token.string );
		break;

	case TT_NAME:
		return;

	default:
		Error( "Unknown token '%s'", token.string );
	}
}

/*
=============
ExpectToken

Issues an Error if the current token isn't equal to string
Gets the next token
=============
*/
static void ExpectToken( const char *string ) {
	if ( !TokenIs( string ) ) {
		Error( "expected '%s', found '%s'", string, token.string );
	}

	NextToken();
}

/*
=============
CheckToken

Returns true and gets the next token if the current token equals string
Returns false and does nothing otherwise
=============
*/
static qboolean CheckToken( const char *string ) {
	if ( !TokenIs( string ) ) {
		return qfalse;
	}

	NextToken();

	return qtrue;
}

/*
============
ParseName

Checks to see if the current token is a valid name
============
*/
static void ParseName( char *name, int nameSize ) {
	if ( token.type != TT_NAME ) {
		Error( "'%s' is not a name", token.string );
	}

	Q_strncpyz( name, token.string, nameSize );
	NextToken();
}

/*
============
CheckType

Parses a variable type, including functions types
============
*/
static typeDef_t *CheckType( void ) {
	typeDef_t *type;

	if ( TokenIs( "float" ) ) {
		type = &type_float;
	} else if ( TokenIs( "vector" ) ) {
		type = &type_vector;
	} else if ( TokenIs( "entity" ) ) {
		type = &type_entity;
	} else if ( TokenIs( "string" ) ) {
		type = &type_string;
	} else if ( TokenIs( "void" ) ) {
		type = &type_void;
	} else if ( TokenIs( "object" ) ) {
		type = &type_object;
	} else if ( TokenIs( "boolean" ) ) {
		type = &type_boolean;
	} else if ( TokenIs( "namespace" ) ) {
		type = &type_namespace;
	} else if ( TokenIs( "scriptEvent" ) ) {
		type = &type_scriptevent;
	} else {
		type = Program_FindType( token.string );
		if ( type && !Type_Inherits( type, &type_object ) ) {
			type = NULL;
		}
	}

	return type;
}

/*
============
ParseType

Parses a variable type, including functions types
============
*/
static typeDef_t *ParseType( void ) {
	typeDef_t *type;

	type = CheckType();
	if ( !type ) {
		Error( "\"%s\" is not a type", token.string );
	}

	if ( ( type == &type_scriptevent ) && ( scope != &def_namespace ) ) {
		Error( "scriptEvents can only defined in the global namespace" );
	}

	if ( ( type == &type_namespace ) && ( scope->typeDef->type != ev_namespace ) ) {
		Error( "A namespace may only be defined globally, or within another namespace" );
	}

	NextToken();

	return type;
}

/*
============
ParseImmediate

Looks for a preexisting constant
============
*/
static varDef_t *ParseImmediate( void ) {
	varDef_t *def;

	def = GetImmediate( immediateType, &immediate, token.string );
	NextToken();

	return def;
}

/*
============
EmitFunctionParms
============
*/
static varDef_t *EmitFunctionParms( int op, varDef_t *func, int startarg, int startsize, varDef_t *object ) {
	varDef_t		*e;
	const typeDef_t	*type;
	const typeDef_t	*funcArg;
	varDef_t		*returnDef;
	typeDef_t		*returnType;
	varDef_t		*resultDef;
	statement_t		*statement;
	int 			arg;
	int 			size;
	int				resultOp;

	type = func->typeDef;
	if ( VarDef_Type( func ) != ev_function ) {
		Error( "'%s' is not a function", VarDef_Name( func ) );
	}

	// copy the parameters to the global parameter variables
	arg = startarg;
	size = startsize;
	if ( !CheckToken( ")" ) ) {
		do {
			if ( arg >= type->numParms ) {
				Error( "too many parameters" );
			}

			e = GetExpression( TOP_PRIORITY );

			funcArg = type->parmTypes[ arg ];
			if ( !EmitPush( e, funcArg ) ) {
				Error( "type mismatch on parm %i of call to '%s'", arg + 1, VarDef_Name( func ) );
			}

			if ( funcArg->type == ev_object ) {
				size += type_object.size;
			} else {
				size += funcArg->size;
			}

			arg++;
		} while( CheckToken( "," ) );

		ExpectToken( ")" );
	}

	if ( arg < type->numParms ) {
		Error( "too few parameters for function '%s'", VarDef_Name( func ) );
	}

	if ( op == OP_CALL ) {
		EmitOpcode( op, func, 0 );
	} else if ( ( op == OP_OBJECTCALL ) || ( op == OP_OBJTHREAD ) ) {
		EmitOpcode( op, object, VirtualFunctionConstant( func ) );

		// need arg size seperate since script object may be NULL
		statement = Program_GetStatement( Program_NumStatements() - 1 );
		statement->c = SizeConstant( func->value.functionPtr->parmTotal );
		// dhewm3's: the prototype's parmTotal is known before the method's body
		// (EmitFunctionParms and ParseFunctionDef there)
		if ( op == OP_OBJECTCALL && func->value.functionPtr->parmTotal > 0
			 && func->value.functionPtr->numParmSize == 0 ) {
			statement->flags = FLAG_OBJECTCALL_IMPL_NOT_PARSED_YET;
		}
	} else {
		EmitOpcode( op, func, SizeConstant( size ) );
	}

	// we need to copy off the result into a temporary result location, so figure out the opcode
	returnType = Type_ReturnType( type );
	if ( returnType->type == ev_string ) {
		resultOp = OP_STORE_S;
		returnDef = program_returnStringDef;
	} else {
		program_returnDef->typeDef = returnType;
		returnDef = program_returnDef;

		switch( returnType->type ) {
		case ev_void :
			resultOp = OP_STORE_F;
			break;

		case ev_boolean :
			resultOp = OP_STORE_BOOL;
			break;

		case ev_float :
			resultOp = OP_STORE_F;
			break;

		case ev_vector :
			resultOp = OP_STORE_V;
			break;

		case ev_entity :
			resultOp = OP_STORE_ENT;
			break;

		case ev_object :
			resultOp = OP_STORE_OBJ;
			break;

		default :
			Error( "Invalid return type for function '%s'", VarDef_Name( func ) );
			// shut up compiler
			resultOp = OP_STORE_OBJ;
			break;
		}
	}

	if ( returnType->type == ev_void ) {
		// don't need result space since there's no result, so just return the normal result def.
		return returnDef;
	}

	// allocate result space
	// try to reuse result defs as much as possible
	statement = Program_GetStatement( Program_NumStatements() - 1 );
	resultDef = Program_FindFreeResultDef( returnType, RESULT_STRING, scope, statement->a, statement->b );
	// set user count back to 0, a result def needs to be used twice before it can be reused
	resultDef->numUsers = 0;

	EmitOpcode( resultOp, returnDef, resultDef );

	return resultDef;
}

/*
============
ParseFunctionCall
============
*/
static varDef_t *ParseFunctionCall( varDef_t *funcDef ) {
	varDef_t *thisdef;

	assert( funcDef );

	if ( VarDef_Type( funcDef ) != ev_function ) {
		Error( "'%s' is not a function", VarDef_Name( funcDef ) );
	}

	if ( funcDef->initialized == DEF_UNINITIALIZED ) {
		Error( "Function '%s' has not been defined yet", VarDef_GlobalName( funcDef ) );
	}

	assert( funcDef->value.functionPtr );
	if ( callthread ) {
		if ( ( funcDef->initialized != DEF_UNINITIALIZED ) && funcDef->value.functionPtr->eventdef ) {
			Error( "Built-in functions cannot be called as threads" );
		}
		callthread = qfalse;
		return EmitFunctionParms( OP_THREAD, funcDef, 0, 0, NULL );
	} else {
		if ( ( funcDef->initialized != DEF_UNINITIALIZED ) && funcDef->value.functionPtr->eventdef ) {
			if ( ( scope->typeDef->type != ev_namespace ) && ( scope->scope->typeDef->type == ev_object ) ) {
				// get the local object pointer
				thisdef = Program_GetDef( scope->scope->typeDef, "self", scope );
				if ( !thisdef ) {
					Error( "No 'self' within scope" );
				}

				return ParseEventCall( thisdef, funcDef );
			} else {
				Error( "Built-in functions cannot be called without an object" );
			}
		}

		return EmitFunctionParms( OP_CALL, funcDef, 0, 0, NULL );
	}
}

/*
============
ParseObjectCall
============
*/
static varDef_t *ParseObjectCall( varDef_t *object, varDef_t *func ) {
	EmitPush( object, object->typeDef );
	if ( callthread ) {
		callthread = qfalse;
		return EmitFunctionParms( OP_OBJTHREAD, func, 1, type_object.size, object );
	} else {
		return EmitFunctionParms( OP_OBJECTCALL, func, 1, 0, object );
	}
}

/*
============
ParseEventCall
============
*/
static varDef_t *ParseEventCall( varDef_t *object, varDef_t *funcDef ) {
	if ( callthread ) {
		Error( "Cannot call built-in functions as a thread" );
	}

	if ( VarDef_Type( funcDef ) != ev_function ) {
		Error( "'%s' is not a function", VarDef_Name( funcDef ) );
	}

	if ( !funcDef->value.functionPtr->eventdef ) {
		Error( "\"%s\" cannot be called with object notation", VarDef_Name( funcDef ) );
	}

	if ( VarDef_Type( object ) == ev_object ) {
		EmitPush( object, &type_entity );
	} else {
		EmitPush( object, object->typeDef );
	}

	return EmitFunctionParms( OP_EVENTCALL, funcDef, 0, type_object.size, NULL );
}

/*
============
ParseSysObjectCall
============
*/
static varDef_t *ParseSysObjectCall( varDef_t *funcDef ) {
	if ( callthread ) {
		Error( "Cannot call built-in functions as a thread" );
	}

	if ( VarDef_Type( funcDef ) != ev_function ) {
		Error( "'%s' is not a function", VarDef_Name( funcDef ) );
	}

	if ( !funcDef->value.functionPtr->eventdef ) {
		Error( "\"%s\" cannot be called with object notation", VarDef_Name( funcDef ) );
	}

	if ( !funcDef->value.functionPtr->eventdef->sysFunc ) {
		Error( "\"%s\" is not callable as a 'sys' function", VarDef_Name( funcDef ) );
	}

	return EmitFunctionParms( OP_SYSCALL, funcDef, 0, 0, NULL );
}

/*
============
LookupDef
============
*/
static varDef_t *LookupDef( const char *name, const varDef_t *baseobj ) {
	varDef_t	*def;
	varDef_t	*field;
	varDef_t	*thisdef;
	etype_t		type_b;
	etype_t		type_c;
	const opcode_t	*op;

	// check if we're accessing a field
	if ( baseobj && ( VarDef_Type( baseobj ) == ev_object ) ) {
		const varDef_t *tdef;

		def = NULL;
		for( tdef = baseobj; tdef != &def_object; tdef = Type_SuperClass( tdef->typeDef )->def ) {
			def = Program_GetDef( NULL, name, tdef );
			if ( def ) {
				break;
			}
		}
	} else {
		// first look through the defs in our scope
		def = Program_GetDef( NULL, name, scope );
		if ( !def ) {
			// if we're in a member function, check types local to the object
			if ( ( scope->typeDef->type != ev_namespace ) && ( scope->scope->typeDef->type == ev_object ) ) {
				// get the local object pointer
				thisdef = Program_GetDef( scope->scope->typeDef, "self", scope );

				field = LookupDef( name, scope->scope->typeDef->def );
				if ( !field ) {
					Error( "Unknown value \"%s\"", name );
				}

				// type check
				type_b = VarDef_Type( field );
				if ( VarDef_Type( field ) == ev_function ) {
					type_c = Type_ReturnType( field->typeDef )->type;
				} else {
					type_c = Type_FieldType( field->typeDef )->type;	// field access gets type from field
					if ( CheckToken( "++" ) ) {
						if ( type_c != ev_float ) {
							Error( "Invalid type for ++" );
						}
						def = EmitOpcode( OP_UINCP_F, thisdef, field );
						return def;
					} else if ( CheckToken( "--" ) ) {
						if ( type_c != ev_float ) {
							Error( "Invalid type for --" );
						}
						def = EmitOpcode( OP_UDECP_F, thisdef, field );
						return def;
					}
				}

				op = &opcodes[ OP_INDIRECT_F ];
				while( ( op->type_a->typeDef->type != ev_object )
					|| ( type_b != op->type_b->typeDef->type ) || ( type_c != op->type_c->typeDef->type ) ) {
					if ( ( op->priority == FUNCTION_PRIORITY ) && ( op->type_a->typeDef->type == ev_object ) && ( op->type_c->typeDef->type == ev_void ) &&
						( type_c != op->type_c->typeDef->type ) ) {
						// catches object calls that return a value
						break;
					}
					op++;
					if ( !op->name || strcmp( op->name, "." ) ) {
						Error( "no valid opcode to access type '%s'", Type_SuperClass( field->typeDef )->name );
					}
				}

				if ( ( op - opcodes ) == OP_OBJECTCALL ) {
					ExpectToken( "(" );
					def = ParseObjectCall( thisdef, field );
				} else {
					// emit the conversion opcode
					def = EmitOpcodeOp( op, thisdef, field );

					// field access gets type from field
					def->typeDef = Type_FieldType( field->typeDef );
				}
			}
		}
	}

	return def;
}

/*
============
ParseValue

Returns the def for the current token
============
*/
static varDef_t *ParseValue( void ) {
	varDef_t	*def;
	varDef_t	*namespaceDef;
	char		name[ MAX_TOKEN ];

	if ( immediateType == &type_entity ) {
		// if an immediate entity ($-prefaced name) then create or lookup a def for it.
		// when entities are spawned, they'll lookup the def and point it to them.
		Com_sprintf( name, sizeof( name ), "$%s", token.string );
		def = Program_GetDef( &type_entity, name, &def_namespace );
		if ( !def ) {
			def = Program_AllocDef( &type_entity, name, &def_namespace, qtrue );
		}
		NextToken();
		return def;
	} else if ( immediateType ) {
		// if the token is an immediate, allocate a constant for it
		return ParseImmediate();
	}

	ParseName( name, sizeof( name ) );
	def = LookupDef( name, basetype );
	if ( !def ) {
		if ( basetype ) {
			Error( "%s is not a member of %s", name, basetype->typeDef->name );
		} else {
			Error( "Unknown value \"%s\"", name );
		}
	// if namespace, then look up the variable in that namespace
	} else if ( VarDef_Type( def ) == ev_namespace ) {
		while( VarDef_Type( def ) == ev_namespace ) {
			ExpectToken( "::" );
			ParseName( name, sizeof( name ) );
			namespaceDef = def;
			def = Program_GetDef( NULL, name, namespaceDef );
			if ( !def ) {
				Error( "Unknown value \"%s::%s\"", VarDef_GlobalName( namespaceDef ), name );
			}
		}
	}

	return def;
}

/*
============
GetTerm
============
*/
static varDef_t *GetTerm( void ) {
	varDef_t	*e;
	int 		op;

	if ( !immediateType && CheckToken( "~" ) ) {
		e = GetExpression( TILDE_PRIORITY );
		switch( VarDef_Type( e ) ) {
		case ev_float :
			op = OP_COMP_F;
			break;

		default :
			Error( "type mismatch for ~" );
			// shut up compiler
			op = OP_COMP_F;
			break;
		}

		return EmitOpcode( op, e, 0 );
	}

	if ( !immediateType && CheckToken( "!" ) ) {
		e = GetExpression( NOT_PRIORITY );
		switch( VarDef_Type( e ) ) {
		case ev_boolean :
			op = OP_NOT_BOOL;
			break;

		case ev_float :
			op = OP_NOT_F;
			break;

		case ev_string :
			op = OP_NOT_S;
			break;

		case ev_vector :
			op = OP_NOT_V;
			break;

		case ev_entity :
			op = OP_NOT_ENT;
			break;

		case ev_function :
			Error( "Invalid type for !" );
			// shut up compiler
			op = OP_NOT_F;
			break;

		case ev_object :
			op = OP_NOT_ENT;
			break;

		default :
			Error( "type mismatch for !" );
			// shut up compiler
			op = OP_NOT_F;
			break;
		}

		return EmitOpcode( op, e, 0 );
	}

	// check for negation operator
	if ( !immediateType && CheckToken( "-" ) ) {
		// constants are directly negated without an instruction
		if ( immediateType == &type_float ) {
			immediate._float = -immediate._float;
			return ParseImmediate();
		} else if ( immediateType == &type_vector ) {
			immediate.vector[0] = -immediate.vector[0];
			immediate.vector[1] = -immediate.vector[1];
			immediate.vector[2] = -immediate.vector[2];
			return ParseImmediate();
		} else {
			e = GetExpression( NOT_PRIORITY );
			switch( VarDef_Type( e ) ) {
			case ev_float :
				op = OP_NEG_F;
				break;

			case ev_vector :
				op = OP_NEG_V;
				break;
			default :
				Error( "type mismatch for -" );
				// shut up compiler
				op = OP_NEG_F;
				break;
			}
			return EmitOpcode( op, e, 0 );
		}
	}

	if ( CheckToken( "int" ) ) {
		ExpectToken( "(" );

		e = GetExpression( INT_PRIORITY );
		if ( VarDef_Type( e ) != ev_float ) {
			Error( "type mismatch for int()" );
		}

		ExpectToken( ")" );

		return EmitOpcode( OP_INT_F, e, 0 );
	}

	if ( CheckToken( "thread" ) ) {
		callthread = qtrue;
		e = GetExpression( FUNCTION_PRIORITY );

		if ( callthread ) {
			Error( "Invalid thread call" );
		}

		// threads return the thread number
		program_returnDef->typeDef = &type_float;
		return program_returnDef;
	}

	if ( !immediateType && CheckToken( "(" ) ) {
		e = GetExpression( TOP_PRIORITY );
		ExpectToken( ")" );

		return e;
	}

	return ParseValue();
}

/*
==============
TypeMatches
==============
*/
static qboolean TypeMatches( etype_t type1, etype_t type2 ) {
	if ( type1 == type2 ) {
		return qtrue;
	}

	return qfalse;
}

/*
==============
GetExpression
==============
*/
static varDef_t *GetExpression( int priority ) {
	const opcode_t	*op;
	const opcode_t	*oldop;
	varDef_t		*e;
	varDef_t		*e2;
	const varDef_t	*oldtype;
	statement_t		*statement;
	etype_t 		type_a;
	etype_t 		type_b;
	etype_t 		type_c;

	if ( priority == 0 ) {
		return GetTerm();
	}

	e = GetExpression( priority - 1 );
	if ( TokenIs( ";" ) ) {
		// save us from searching through the opcodes unneccesarily
		return e;
	}

	while( 1 ) {
		if ( ( priority == FUNCTION_PRIORITY ) && CheckToken( "(" ) ) {
			return ParseFunctionCall( e );
		}

		// has to be a punctuation
		if ( immediateType ) {
			break;
		}

		for( op = opcodes; op->name; op++ ) {
			if ( ( op->priority == priority ) && CheckToken( op->name ) ) {
				break;
			}
		}

		if ( !op->name ) {
			// next token isn't at this priority level
			break;
		}

		// unary operators act only on the left operand
		if ( op->type_b == &def_void ) {
			e = EmitOpcodeOp( op, e, 0 );
			return e;
		}

		// preserve our base type
		oldtype = basetype;

		// field access needs scope from object
		if ( ( op->name[ 0 ] == '.' ) && Type_Inherits( e->typeDef, &type_object ) ) {
			// save off what type this field is part of
			basetype = e->typeDef->def;
		}

		if ( op->rightAssociative ) {
			// if last statement is an indirect, change it to an address of
			if ( Program_NumStatements() > 0 ) {
				statement = Program_GetStatement( Program_NumStatements() - 1 );
				if ( ( statement->op >= OP_INDIRECT_F ) && ( statement->op < OP_ADDRESS ) ) {
					statement->op = OP_ADDRESS;
					Type_SetPointerType( &type_pointer, e->typeDef );
					e->typeDef = &type_pointer;
				}
			}

			e2 = GetExpression( priority );
		} else {
			e2 = GetExpression( priority - 1 );
		}

		// restore type
		basetype = oldtype;

		// type check
		type_a = VarDef_Type( e );
		type_b = VarDef_Type( e2 );

		// field access gets type from field
		if ( op->name[ 0 ] == '.' ) {
			if ( ( VarDef_Type( e2 ) == ev_function ) && Type_ReturnType( e2->typeDef ) ) {
				type_c = Type_ReturnType( e2->typeDef )->type;
			} else if ( VarDef_Type( e2 ) == ev_field && Type_FieldType( e2->typeDef ) ) {
				type_c = Type_FieldType( e2->typeDef )->type;
			} else {
				// not a field
				type_c = ev_error;
			}
		} else {
			type_c = ev_void;
		}

		oldop = op;
		while( !TypeMatches( type_a, op->type_a->typeDef->type ) || !TypeMatches( type_b, op->type_b->typeDef->type ) ||
			( ( type_c != ev_void ) && !TypeMatches( type_c, op->type_c->typeDef->type ) ) ) {
			if ( ( op->priority == FUNCTION_PRIORITY ) && TypeMatches( type_a, op->type_a->typeDef->type ) && TypeMatches( type_b, op->type_b->typeDef->type ) ) {
				break;
			}

			op++;
			if ( !op->name || strcmp( op->name, oldop->name ) ) {
				Error( "type mismatch for '%s'", oldop->name );
			}
		}

		switch( op - opcodes ) {
		case OP_SYSCALL :
			ExpectToken( "(" );
			e = ParseSysObjectCall( e2 );
			break;

		case OP_OBJECTCALL :
			ExpectToken( "(" );
			if ( ( e2->initialized != DEF_UNINITIALIZED ) && e2->value.functionPtr->eventdef ) {
				e = ParseEventCall( e, e2 );
			} else {
				e = ParseObjectCall( e, e2 );
			}
			break;

		case OP_EVENTCALL :
			ExpectToken( "(" );
			if ( ( e2->initialized != DEF_UNINITIALIZED ) && e2->value.functionPtr->eventdef ) {
				e = ParseEventCall( e, e2 );
			} else {
				e = ParseObjectCall( e, e2 );
			}
			break;

		default:
			if ( callthread ) {
				Error( "Expecting function call after 'thread'" );
			}

			if ( ( type_a == ev_pointer ) && ( type_b != Type_PointerType( e->typeDef )->type ) ) {
				// FIXME: need to make a general case for this
				if ( ( op - opcodes == OP_STOREP_F ) && ( Type_PointerType( e->typeDef )->type == ev_boolean ) ) {
					// copy from float to boolean pointer
					op = &opcodes[ OP_STOREP_FTOBOOL ];
				} else if ( ( op - opcodes == OP_STOREP_BOOL ) && ( Type_PointerType( e->typeDef )->type == ev_float ) ) {
					// copy from boolean to float pointer
					op = &opcodes[ OP_STOREP_BOOLTOF ];
				} else if ( ( op - opcodes == OP_STOREP_F ) && ( Type_PointerType( e->typeDef )->type == ev_string ) ) {
					// copy from float to string pointer
					op = &opcodes[ OP_STOREP_FTOS ];
				} else if ( ( op - opcodes == OP_STOREP_BOOL ) && ( Type_PointerType( e->typeDef )->type == ev_string ) ) {
					// copy from boolean to string pointer
					op = &opcodes[ OP_STOREP_BTOS ];
				} else if ( ( op - opcodes == OP_STOREP_V ) && ( Type_PointerType( e->typeDef )->type == ev_string ) ) {
					// copy from vector to string pointer
					op = &opcodes[ OP_STOREP_VTOS ];
				} else if ( ( op - opcodes == OP_STOREP_ENT ) && ( Type_PointerType( e->typeDef )->type == ev_object ) ) {
					// store an entity into an object pointer
					op = &opcodes[ OP_STOREP_OBJENT ];
				} else {
					Error( "type mismatch for '%s'", op->name );
				}
			}

			if ( op->rightAssociative ) {
				e = EmitOpcodeOp( op, e2, e );
			} else {
				e = EmitOpcodeOp( op, e, e2 );
			}

			if ( op - opcodes == OP_STOREP_OBJENT ) {
				// statement.b points to type_pointer, which is just a temporary that gets its type reassigned, so we store the real type in statement.c
				// so that we can do a type check during run time since we don't know what type the script object is at compile time because it
				// comes from an entity
				statement = Program_GetStatement( Program_NumStatements() - 1 );
				statement->c = Type_PointerType( &type_pointer )->def;
			}

			// field access gets type from field
			if ( type_c != ev_void ) {
				e->typeDef = Type_FieldType( e2->typeDef );
			}
			break;
		}
	}

	return e;
}

/*
================
PatchLoop
================
*/
static void PatchLoop( int start, int continuePos ) {
	int			i;
	statement_t	*pos;

	pos = Program_GetStatement( start );
	for( i = start; i < Program_NumStatements(); i++, pos++ ) {
		if ( pos->op == OP_BREAK ) {
			pos->op = OP_GOTO;
			pos->a = JumpFrom( i );
		} else if ( pos->op == OP_CONTINUE ) {
			pos->op = OP_GOTO;
			pos->a = JumpDef( i, continuePos );
		}
	}
}

/*
================
ParseReturnStatement
================
*/
static void ParseReturnStatement( void ) {
	varDef_t	*e;
	etype_t 	type_a;
	etype_t 	type_b;
	const opcode_t *op;
	typeDef_t	*returnType;

	if ( CheckToken( ";" ) ) {
		if ( Type_ReturnType( scope->typeDef )->type != ev_void ) {
			Error( "expecting return value" );
		}

		EmitOpcode( OP_RETURN, 0, 0 );
		return;
	}

	e = GetExpression( TOP_PRIORITY );
	ExpectToken( ";" );

	type_a = VarDef_Type( e );
	type_b = Type_ReturnType( scope->typeDef )->type;

	if ( TypeMatches( type_a, type_b ) ) {
		EmitOpcode( OP_RETURN, e, 0 );
		return;
	}

	for( op = opcodes; op->name; op++ ) {
		if ( !strcmp( op->name, "=" ) ) {
			break;
		}
	}

	assert( op->name );

	while( !TypeMatches( type_a, op->type_a->typeDef->type ) || !TypeMatches( type_b, op->type_b->typeDef->type ) ) {
		op++;
		if ( !op->name || strcmp( op->name, "=" ) ) {
			Error( "type mismatch for return value" );
		}
	}

	returnType = Type_ReturnType( scope->typeDef );
	if ( returnType->type == ev_string ) {
		EmitOpcodeOp( op, e, program_returnStringDef );
	} else {
		program_returnDef->typeDef = returnType;
		EmitOpcodeOp( op, e, program_returnDef );
	}
	EmitOpcode( OP_RETURN, 0, 0 );
}

/*
================
ParseWhileStatement
================
*/
static void ParseWhileStatement( void ) {
	varDef_t	*e;
	int			patch1;
	int			patch2;

	loopDepth++;

	ExpectToken( "(" );

	patch2 = Program_NumStatements();
	e = GetExpression( TOP_PRIORITY );
	ExpectToken( ")" );

	if ( ( e->initialized == DEF_INITIALIZEDCONSTANT ) && ( *e->value.intPtr != 0 ) ) {
		//FIXME: we can completely skip generation of this code in the opposite case
		ParseStatement();
		EmitOpcode( OP_GOTO, JumpTo( patch2 ), 0 );
	} else {
		patch1 = Program_NumStatements();
		EmitOpcode( OP_IFNOT, e, 0 );
		ParseStatement();
		EmitOpcode( OP_GOTO, JumpTo( patch2 ), 0 );
		Program_GetStatement( patch1 )->b = JumpFrom( patch1 );
	}

	// fixup breaks and continues
	PatchLoop( patch2, patch2 );

	loopDepth--;
}

/*
================
ParseForStatement

Form of for statement with a counter:

	a = 0;
start:					<< patch4
	if ( !( a < 10 ) ) {
		goto end;		<< patch1
	} else {
		goto process;	<< patch3
	}

increment:				<< patch2
	a = a + 1;
	goto start;			<< goto patch4

process:
	statements;
	goto increment;		<< goto patch2

end:

Form of for statement without a counter:

	a = 0;
start:					<< patch2
	if ( !( a < 10 ) ) {
		goto end;		<< patch1
	}

process:
	statements;
	goto start;			<< goto patch2

end:
================
*/
static void ParseForStatement( void ) {
	varDef_t	*e;
	int			start;
	int			patch1;
	int			patch2;
	int			patch3;
	int			patch4;

	loopDepth++;

	start = Program_NumStatements();

	ExpectToken( "(" );

	// init
	if ( !CheckToken( ";" ) ) {
		do {
			GetExpression( TOP_PRIORITY );
		} while( CheckToken( "," ) );

		ExpectToken( ";" );
	}

	// condition
	patch2 = Program_NumStatements();

	e = GetExpression( TOP_PRIORITY );
	ExpectToken( ";" );

	//FIXME: add check for constant expression
	patch1 = Program_NumStatements();
	EmitOpcode( OP_IFNOT, e, 0 );

	// counter
	if ( !CheckToken( ")" ) ) {
		patch3 = Program_NumStatements();
		EmitOpcode( OP_IF, e, 0 );

		patch4 = patch2;
		patch2 = Program_NumStatements();
		do {
			GetExpression( TOP_PRIORITY );
		} while( CheckToken( "," ) );

		ExpectToken( ")" );

		// goto patch4
		EmitOpcode( OP_GOTO, JumpTo( patch4 ), 0 );

		// fixup patch3
		Program_GetStatement( patch3 )->b = JumpFrom( patch3 );
	}

	ParseStatement();

	// goto patch2
	EmitOpcode( OP_GOTO, JumpTo( patch2 ), 0 );

	// fixup patch1
	Program_GetStatement( patch1 )->b = JumpFrom( patch1 );

	// fixup breaks and continues
	PatchLoop( start, patch2 );

	loopDepth--;
}

/*
================
ParseDoWhileStatement
================
*/
static void ParseDoWhileStatement( void ) {
	varDef_t	*e;
	int			patch1;

	loopDepth++;

	patch1 = Program_NumStatements();
	ParseStatement();
	ExpectToken( "while" );
	ExpectToken( "(" );
	e = GetExpression( TOP_PRIORITY );
	ExpectToken( ")" );
	ExpectToken( ";" );

	EmitOpcode( OP_IF, e, JumpTo( patch1 ) );

	// fixup breaks and continues
	PatchLoop( patch1, patch1 );

	loopDepth--;
}

/*
================
ParseIfStatement
================
*/
static void ParseIfStatement( void ) {
	varDef_t	*e;
	int			patch1;
	int			patch2;

	ExpectToken( "(" );
	e = GetExpression( TOP_PRIORITY );
	ExpectToken( ")" );

	//FIXME: add check for constant expression
	patch1 = Program_NumStatements();
	EmitOpcode( OP_IFNOT, e, 0 );

	ParseStatement();

	if ( CheckToken( "else" ) ) {
		patch2 = Program_NumStatements();
		EmitOpcode( OP_GOTO, 0, 0 );
		Program_GetStatement( patch1 )->b = JumpFrom( patch1 );
		ParseStatement();
		Program_GetStatement( patch2 )->a = JumpFrom( patch2 );
	} else {
		Program_GetStatement( patch1 )->b = JumpFrom( patch1 );
	}
}

/*
============
ParseStatement
============
*/
static void ParseStatement( void ) {
	if ( CheckToken( ";" ) ) {
		// skip semicolons, which are harmless and ok syntax
		return;
	}

	if ( CheckToken( "{" ) ) {
		do {
			ParseStatement();
		} while( !CheckToken( "}" ) );

		return;
	}

	if ( CheckToken( "return" ) ) {
		ParseReturnStatement();
		return;
	}

	if ( CheckToken( "while" ) ) {
		ParseWhileStatement();
		return;
	}

	if ( CheckToken( "for" ) ) {
		ParseForStatement();
		return;
	}

	if ( CheckToken( "do" ) ) {
		ParseDoWhileStatement();
		return;
	}

	if ( CheckToken( "break" ) ) {
		ExpectToken( ";" );
		if ( !loopDepth ) {
			Error( "cannot break outside of a loop" );
		}
		EmitOpcode( OP_BREAK, 0, 0 );
		return;
	}

	if ( CheckToken( "continue" ) ) {
		ExpectToken( ";" );
		if ( !loopDepth ) {
			Error( "cannot contine outside of a loop" );
		}
		EmitOpcode( OP_CONTINUE, 0, 0 );
		return;
	}

	if ( CheckType() != NULL ) {
		ParseDefs();
		return;
	}

	if ( CheckToken( "if" ) ) {
		ParseIfStatement();
		return;
	}

	GetExpression( TOP_PRIORITY );
	ExpectToken(";");
}

/*
================
ParseObjectDef
================
*/
static void ParseObjectDef( const char *objname ) {
	typeDef_t	*objtype;
	typeDef_t	*type;
	typeDef_t	*parentType;
	typeDef_t	*fieldtype;
	char		name[ MAX_TOKEN ];
	typeDef_t	newtype;
	varDef_t	*oldscope;
	int			i;

	oldscope = scope;
	if ( scope->typeDef->type != ev_namespace ) {
		Error( "Objects cannot be defined within functions or other objects" );
	}

	// make sure it doesn't exist before we create it
	if ( Program_FindType( objname ) != NULL ) {
		Error( "'%s' : redefinition; different basic types", objname );
	}

	// base type
	if ( !CheckToken( ":" ) ) {
		parentType = &type_object;
	} else {
		parentType = ParseType();
		if ( !Type_Inherits( parentType, &type_object ) ) {
			Error( "Objects may only inherit from objects." );
		}
	}

	objtype = Program_AllocNewType( ev_object, NULL, objname, parentType == &type_object ? 0 : parentType->size, parentType );
	objtype->def = Program_AllocDef( objtype, objname, scope, qtrue );
	scope = objtype->def;

	// inherit all the functions
	for( i = 0; i < parentType->numFunctions; i++ ) {
		Type_AddFunction( objtype, Type_GetFunction( parentType, i ) );
	}

	ExpectToken( "{" );

	Type_Init( &newtype, ev_field, NULL, "", 0, NULL );
	do {
		if ( CheckToken( ";" ) ) {
			// skip semicolons, which are harmless and ok syntax
			continue;
		}

		fieldtype = ParseType();
		Type_SetFieldType( &newtype, fieldtype );

		Type_SetName( &newtype, va( "%s field", fieldtype->name ) );

		ParseName( name, sizeof( name ) );

		// check for a function prototype or declaraction
		if ( CheckToken( "(" ) ) {
			ParseFunctionDef( Type_FieldType( &newtype ), name );
		} else {
			type = Program_GetType( &newtype, qtrue );
			assert( !type->def );
			Program_AllocDef( type, name, scope, qtrue );
			Type_AddField( objtype, type, name );
			ExpectToken( ";" );
		}
	} while( !CheckToken( "}" ) );
	Type_Free( &newtype );

	scope = oldscope;

	ExpectToken( ";" );
}

/*
============
ParseFunction

parse a function type
============
*/
static typeDef_t *ParseFunction( typeDef_t *returnType, const char *name ) {
	typeDef_t	newtype;
	typeDef_t	*type;
	char		parmName[ MAX_TOKEN ];

	Type_Init( &newtype, ev_function, NULL, name, type_function.size, returnType );

	if ( scope->typeDef->type != ev_namespace ) {
		// create self pointer
		Type_AddFunctionParm( &newtype, scope->typeDef, "self" );
	}

	if ( !CheckToken( ")" ) ) {
		do {
			type = ParseType();
			ParseName( parmName, sizeof( parmName ) );
			Type_AddFunctionParm( &newtype, type, parmName );
		} while( CheckToken( "," ) );

		ExpectToken( ")" );
	}

	type = Program_GetType( &newtype, qtrue );
	Type_Free( &newtype );
	return type;
}

/*
================
ParseFunctionDef
================
*/
static void ParseFunctionDef( typeDef_t *returnType, const char *name ) {
	typeDef_t		*type;
	varDef_t		*def;
	varDef_t		*oldscope;
	int 			i;
	int 			numParms;
	int				size;
	int				totalSize;
	const typeDef_t	*parmType;
	function_t		*func;
	statement_t		*pos;

	if ( ( scope->typeDef->type != ev_namespace ) && !Type_Inherits( scope->typeDef, &type_object ) ) {
		Error( "Functions may not be defined within other functions" );
	}

	type = ParseFunction( returnType, name );
	def = Program_GetDef( type, name, scope );
	if ( !def ) {
		def = Program_AllocDef( type, name, scope, qtrue );
		type->def = def;

		func = Program_AllocFunction( def );
		if ( Type_Inherits( scope->typeDef, &type_object ) ) {
			Type_AddFunction( scope->typeDef, func );
		}
	} else {
		func = def->value.functionPtr;
		assert( func );
		if ( func->firstStatement ) {
			Error( "%s redeclared", VarDef_GlobalName( def ) );
		}
	}

	// dhewm3's: the prototype's parmTotal is known before its body is parsed,
	// so the function can be called before that
	numParms = type->numParms;
	if ( !CheckToken( "{" ) ) {
		// it's just a prototype, so get the ; and move on
		ExpectToken( ";" );

		func->parmTotal = 0;
		for( i = 0; i < numParms; i++ ) {
			parmType = type->parmTypes[ i ];
			size = Type_Inherits( parmType, &type_object ) ? type_object.size : parmType->size;
			func->parmTotal += size;
		}
		return;
	}

	// calculate stack space used by parms
	totalSize = 0;
	Script_Free( func->parmSize );
	func->parmSize = Script_Alloc( MAX( numParms, 1 ) * sizeof( int ) );
	func->numParmSize = numParms;
	for( i = 0; i < numParms; i++ ) {
		parmType = type->parmTypes[ i ];
		if ( Type_Inherits( parmType, &type_object ) ) {
			func->parmSize[ i ] = type_object.size;
		} else {
			func->parmSize[ i ] = parmType->size;
		}
		totalSize += func->parmSize[ i ];
	}
	assert( func->parmTotal == 0 || totalSize == func->parmTotal );
	func->parmTotal = totalSize;

	// define the parms
	for( i = 0; i < numParms; i++ ) {
		if ( Program_GetDef( type->parmTypes[ i ], type->parmNames[ i ], def ) ) {
			Error( "'%s' defined more than once in function parameters", type->parmNames[ i ] );
		}
		Program_AllocDef( type->parmTypes[ i ], type->parmNames[ i ], def, qfalse );
	}

	oldscope = scope;
	scope = def;

	func->firstStatement = Program_NumStatements();

	// check if we should call the super class constructor
	if ( Type_Inherits( oldscope->typeDef, &type_object ) && !Q_stricmp( name, "init" ) ) {
		typeDef_t *superClass;
		function_t *constructorFunc = NULL;

		// find the superclass constructor
		for( superClass = Type_SuperClass( oldscope->typeDef ); superClass != &type_object; superClass = Type_SuperClass( superClass ) ) {
			constructorFunc = Program_FindFunction( va( "%s::init", superClass->name ) );
			if ( constructorFunc ) {
				break;
			}
		}

		// emit the call to the constructor
		if ( constructorFunc ) {
			varDef_t *selfDef = Program_GetDef( type->parmTypes[ 0 ], type->parmNames[ 0 ], def );
			assert( selfDef );
			EmitPush( selfDef, selfDef->typeDef );
			EmitOpcode( OP_CALL, constructorFunc->def, 0 );
		}
	}

	// parse regular statements
	while( !CheckToken( "}" ) ) {
		ParseStatement();
	}

	// check if we should call the super class destructor
	if ( Type_Inherits( oldscope->typeDef, &type_object ) && !Q_stricmp( name, "destroy" ) ) {
		typeDef_t *superClass;
		function_t *destructorFunc = NULL;

		// find the superclass destructor
		for( superClass = Type_SuperClass( oldscope->typeDef ); superClass != &type_object; superClass = Type_SuperClass( superClass ) ) {
			destructorFunc = Program_FindFunction( va( "%s::destroy", superClass->name ) );
			if ( destructorFunc ) {
				break;
			}
		}

		if ( destructorFunc ) {
			if ( func->firstStatement < Program_NumStatements() ) {
				// change all returns to point to the call to the destructor
				pos = Program_GetStatement( func->firstStatement );
				for( i = func->firstStatement; i < Program_NumStatements(); i++, pos++ ) {
					if ( pos->op == OP_RETURN ) {
						pos->op = OP_GOTO;
						pos->a = JumpDef( i, Program_NumStatements() );
					}
				}
			}

			// emit the call to the destructor
			{
				varDef_t *selfDef = Program_GetDef( type->parmTypes[ 0 ], type->parmNames[ 0 ], def );
				assert( selfDef );
				EmitPush( selfDef, selfDef->typeDef );
				EmitOpcode( OP_CALL, destructorFunc->def, 0 );
			}
		}
	}

	// always emit the return opcode
	EmitOpcode( OP_RETURN, 0, 0 );

	// record the number of statements in the function
	func->numStatements = Program_NumStatements() - func->firstStatement;

	scope = oldscope;
}

/*
================
ParseVariableDef
================
*/
static void ParseVariableDef( typeDef_t *type, const char *name ) {
	varDef_t	*def, *def2;
	qboolean	negate;

	def = Program_GetDef( type, name, scope );
	if ( def ) {
		Error( "%s redeclared", name );
	}

	def = Program_AllocDef( type, name, scope, qfalse );

	// check for an initialization
	if ( CheckToken( "=" ) ) {
		// if a local variable in a function then write out interpreter code to initialize variable
		if ( scope->typeDef->type == ev_function ) {
			def2 = GetExpression( TOP_PRIORITY );
			if ( ( type == &type_float ) && ( def2->typeDef == &type_float ) ) {
				EmitOpcode( OP_STORE_F, def2, def );
			} else if ( ( type == &type_vector ) && ( def2->typeDef == &type_vector ) ) {
				EmitOpcode( OP_STORE_V, def2, def );
			} else if ( ( type == &type_string ) && ( def2->typeDef == &type_string ) ) {
				EmitOpcode( OP_STORE_S, def2, def );
			} else if ( ( type == &type_entity ) && ( ( def2->typeDef == &type_entity ) || ( Type_Inherits( def2->typeDef, &type_object ) ) ) ) {
				EmitOpcode( OP_STORE_ENT, def2, def );
			} else if ( ( Type_Inherits( type, &type_object ) ) && ( def2->typeDef == &type_entity ) ) {
				EmitOpcode( OP_STORE_OBJENT, def2, def );
			} else if ( ( Type_Inherits( type, &type_object ) ) && ( Type_Inherits( def2->typeDef, type ) ) ) {
				EmitOpcode( OP_STORE_OBJ, def2, def );
			} else if ( ( type == &type_boolean ) && ( def2->typeDef == &type_boolean ) ) {
				EmitOpcode( OP_STORE_BOOL, def2, def );
			} else if ( ( type == &type_string ) && ( def2->typeDef == &type_float ) ) {
				EmitOpcode( OP_STORE_FTOS, def2, def );
			} else if ( ( type == &type_string ) && ( def2->typeDef == &type_boolean ) ) {
				EmitOpcode( OP_STORE_BTOS, def2, def );
			} else if ( ( type == &type_string ) && ( def2->typeDef == &type_vector ) ) {
				EmitOpcode( OP_STORE_VTOS, def2, def );
			} else if ( ( type == &type_boolean ) && ( def2->typeDef == &type_float ) ) {
				EmitOpcode( OP_STORE_FTOBOOL, def2, def );
			} else if ( ( type == &type_float ) && ( def2->typeDef == &type_boolean ) ) {
				EmitOpcode( OP_STORE_BOOLTOF, def2, def );
			} else {
				Error( "bad initialization for '%s'", name );
			}
		} else {
			// global variables can only be initialized with immediate values
			negate = qfalse;
			if ( token.type == TT_PUNCTUATION && TokenIs( "-" ) ) {
				negate = qtrue;
				NextToken();
				if ( immediateType != &type_float ) {
					Error( "wrong immediate type for '-' on variable '%s'", name );
				}
			}

			if ( immediateType != type ) {
				Error( "wrong immediate type for '%s'", name );
			}

			// global variables are initialized at start up
			if ( type == &type_string ) {
				VarDef_SetString( def, token.string, qfalse );
			} else {
				if ( negate ) {
					immediate._float = -immediate._float;
				}
				VarDef_SetValue( def, &immediate, qfalse );
			}
			NextToken();
		}
	} else if ( type == &type_string ) {
		// local strings on the stack are initialized in the interpreter
		if ( scope->typeDef->type != ev_function ) {
			VarDef_SetString( def, "", qfalse );
		}
	} else if ( Type_Inherits( type, &type_object ) ) {
		if ( scope->typeDef->type != ev_function ) {
			VarDef_SetObject( def, NULL );
		}
	}
}

/*
================
GetTypeForEventArg
================
*/
static typeDef_t *GetTypeForEventArg( char argType ) {
	typeDef_t *type;

	switch( argType ) {
	case D_EVENT_INTEGER :
		// this will get converted to int by the interpreter
		type = &type_float;
		break;

	case D_EVENT_FLOAT :
		type = &type_float;
		break;

	case D_EVENT_VECTOR :
		type = &type_vector;
		break;

	case D_EVENT_STRING :
		type = &type_string;
		break;

	case D_EVENT_ENTITY :
	case D_EVENT_ENTITY_NULL :
		type = &type_entity;
		break;

	case D_EVENT_VOID :
		type = &type_void;
		break;

	case D_EVENT_TRACE :
		// This data type isn't available from script
		type = NULL;
		break;

	default:
		// probably a typo
		type = NULL;
		break;
	}

	return type;
}

/*
================
ParseEventDef
================
*/
static void ParseEventDef( typeDef_t *returnType, const char *name ) {
	const typeDef_t	*expectedType;
	typeDef_t		*argType;
	typeDef_t		*type;
	typeDef_t		newtype;
	function_t		*func;
	int 			i;
	int				num;
	const char		*format;
	const eventDef_t *ev;
	char			parmName[ MAX_TOKEN ];

	ev = Event_FindEvent( name );
	if ( !ev ) {
		Error( "Unknown event '%s'", name );
	}

	// set the return type
	expectedType = GetTypeForEventArg( ev->returnType );
	if ( !expectedType ) {
		Error( "Invalid return type '%c' in definition of '%s' event.", ev->returnType, name );
	}
	if ( returnType != expectedType ) {
		Error( "Return type doesn't match internal return type '%s'", expectedType->name );
	}

	Type_Init( &newtype, ev_function, NULL, name, type_function.size, returnType );

	ExpectToken( "(" );

	format = ev->formatspec ? ev->formatspec : "";
	num = strlen( format );
	for( i = 0; i < num; i++ ) {
		expectedType = GetTypeForEventArg( format[ i ] );
		if ( !expectedType || ( expectedType == &type_void ) ) {
			Error( "Invalid parameter '%c' in definition of '%s' event.", format[ i ], name );
		}

		argType = ParseType();
		ParseName( parmName, sizeof( parmName ) );
		if ( argType != expectedType ) {
			Error( "The type of parm %d ('%s') does not match the internal type '%s' in definition of '%s' event.",
				i + 1, parmName, expectedType->name, name );
		}

		Type_AddFunctionParm( &newtype, argType, "" );

		if ( i < num - 1 ) {
			if ( CheckToken( ")" ) ) {
				Error( "Too few parameters for event definition.  Internal definition has %d parameters.", num );
			}
			ExpectToken( "," );
		}
	}
	if ( !CheckToken( ")" ) ) {
		Error( "Too many parameters for event definition.  Internal definition has %d parameters.", num );
	}
	ExpectToken( ";" );

	type = Program_FindType( name );
	if ( type ) {
		if ( !Type_MatchesType( &newtype, type ) || ( type->def->value.functionPtr->eventdef != ev ) ) {
			Error( "Type mismatch on redefinition of '%s'", name );
		}
	} else {
		type = Program_AllocType( &newtype );
		type->def = Program_AllocDef( type, name, &def_namespace, qtrue );

		func				= Program_AllocFunction( type->def );
		func->eventdef		= ev;
		func->parmSize		= Script_Alloc( MAX( num, 1 ) * sizeof( int ) );
		func->numParmSize	= num;
		for( i = 0; i < num; i++ ) {
			argType = newtype.parmTypes[ i ];
			func->parmTotal		+= argType->size;
			func->parmSize[ i ]	= argType->size;
		}

		// mark the parms as local
		func->locals	= func->parmTotal;
	}
	Type_Free( &newtype );
}

/*
================
ParseDefs

Called at the outer layer and when a local statement is hit
================
*/
static void ParseDefs( void ) {
	char		name[ MAX_TOKEN ];
	typeDef_t	*type;
	varDef_t	*def;
	varDef_t	*oldscope;

	if ( CheckToken( ";" ) ) {
		// skip semicolons, which are harmless and ok syntax
		return;
	}

	type = ParseType();
	if ( type == &type_scriptevent ) {
		type = ParseType();
		ParseName( name, sizeof( name ) );
		ParseEventDef( type, name );
		return;
	}

	ParseName( name, sizeof( name ) );

	if ( type == &type_namespace ) {
		def = Program_GetDef( type, name, scope );
		if ( !def ) {
			def = Program_AllocDef( type, name, scope, qtrue );
		}
		ParseNamespace( def );
	} else if ( CheckToken( "::" ) ) {
		def = Program_GetDef( NULL, name, scope );
		if ( !def ) {
			Error( "Unknown object name '%s'", name );
		}
		ParseName( name, sizeof( name ) );
		oldscope = scope;
		scope = def;

		ExpectToken( "(" );
		ParseFunctionDef( type, name );
		scope = oldscope;
	} else if ( type == &type_object ) {
		ParseObjectDef( name );
	} else if ( CheckToken( "(" ) ) {		// check for a function prototype or declaraction
		ParseFunctionDef( type, name );
	} else {
		ParseVariableDef( type, name );
		while( CheckToken( "," ) ) {
			ParseName( name, sizeof( name ) );
			ParseVariableDef( type, name );
		}
		ExpectToken( ";" );
	}
}

/*
================
ParseNamespace

Parses anything within a namespace definition
================
*/
static void ParseNamespace( varDef_t *newScope ) {
	varDef_t *oldscope;

	oldscope = scope;
	if ( newScope != &def_namespace ) {
		ExpectToken( "{" );
	}

	while( !eof ) {
		scope		= newScope;
		callthread	= qfalse;

		if ( ( newScope != &def_namespace ) && CheckToken( "}" ) ) {
			break;
		}

		ParseDefs();
	}

	scope = oldscope;
}

/*
============
UnreadToken

as Doom 3 starts each script: with tokens put back before its first
============
*/
static void UnreadToken( const char *string, int type, int subtype ) {
	token_t t;

	memset( &t, 0, sizeof( t ) );
	Q_strncpyz( t.string, string, sizeof( t.string ) );
	t.type = type;
	t.subtype = subtype;
	t.line = t.linescrossed = 0;
	PC_UnreadToken( source, &t );
}

/*
============
Compiler_CompileFile

compiles the 0 terminated text, adding definitions to the program structure
============
*/
qboolean Compiler_CompileFile( const char *text, const char *filename, qboolean toConsole ) {
	int			startTime;
	char		origFileName[ MAX_QPATH ];

	startTime = trap_Milliseconds();

	Q_strncpyz( origFileName, filename, sizeof( origFileName ) );

	scope				= &def_namespace;
	basetype			= NULL;
	callthread			= qfalse;
	loopDepth			= 0;
	eof					= qfalse;
	braceDepth			= 0;
	immediateType		= NULL;
	currentLineNumber	= 0;
	console				= toConsole;

	memset( &immediate, 0, sizeof( immediate ) );

	source = LoadSourceMemory( text, strlen( text ), filename, NULL );
	if ( !source ) {
		Com_sprintf( errorText, sizeof( errorText ), "Error: couldn't read %s\n", filename );
		return qfalse;
	}

	// unread tokens to include script defines
	UnreadToken( va( "\"%s\"", SCRIPT_DEFAULTDEFS ), TT_STRING, strlen( SCRIPT_DEFAULTDEFS ) );
	UnreadToken( "include", TT_NAME, strlen( "include" ) );
	UnreadToken( "#", TT_PUNCTUATION, P_PRECOMP );

	// init the current token line to be the first line so that currentLineNumber is set correctly in NextToken
	memset( &token, 0, sizeof( token ) );
	token.line = 1;

	compiling = qtrue;
	if ( setjmp( compileError ) ) {
		char error[ 1024 ];

		compiling = qfalse;
		if ( console ) {
			// don't print line number of an error if were calling script from the console using the "script" command
			Com_sprintf( error, sizeof( error ), "Error: %s\n", errorText );
		} else {
			Com_sprintf( error, sizeof( error ), "Error: file %s, line %d: %s\n", Program_GetFilename( currentFileNumber ), currentLineNumber, errorText );
		}
		Q_strncpyz( errorText, error, sizeof( errorText ) );
		FreeSource( source );
		source = NULL;
		return qfalse;
	}

	// read first token
	NextToken();
	while( !eof ) {
		// parse from global namespace
		ParseNamespace( &def_namespace );
	}

	compiling = qfalse;
	FreeSource( source );
	source = NULL;

	if ( !toConsole ) {
		G_DPrintf( "Compiled '%s': %d ms\n", origFileName, trap_Milliseconds() - startTime );
	}
	return qtrue;
}

/*
============
Compiler_LastError
============
*/
const char *Compiler_LastError( void ) {
	return errorText;
}

