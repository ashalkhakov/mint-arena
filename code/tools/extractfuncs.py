#!/usr/bin/env python3
"""The game's callbacks by name, for saved games (RTCW's extractfuncs).

A saved entity's think, touch, use... are written as the names of the
functions they point to and found again by name when the game is loaded,
so a save outlives a rebuild. This writes g_funcs.h: a prototype and a
table entry for every function of the game's sources with a callback's
signature (static ones can't be named from g_save.c: a callback must not be
static). Team Arena's code (#ifdef MISSIONPACK) is left out.

	extractfuncs.py <g_funcs.h> <source.c>...

CMake runs it as the game's sources change.
"""
import os
import re
import sys

# the callback kinds: their parameters' types, and their prototypes
KINDS = {
	( "gentity_t*", ): "void %s( gentity_t *self );",
	( "gentity_t*", "gentity_t*" ): "void %s( gentity_t *self, gentity_t *other );",
	( "gentity_t*", "gentity_t*", "trace_t*" ): "void %s( gentity_t *self, gentity_t *other, trace_t *trace );",
	( "gentity_t*", "gentity_t*", "gentity_t*" ): "void %s( gentity_t *self, gentity_t *other, gentity_t *activator );",
	( "gentity_t*", "gentity_t*", "int" ): "void %s( gentity_t *self, gentity_t *attacker, int damage );",
	( "gentity_t*", "gentity_t*", "gentity_t*", "int", "int" ):
		"void %s( gentity_t *self, gentity_t *inflictor, gentity_t *attacker, int damage, int mod );",
}
SNAPSHOT = ( "gentity_t*", "gentity_t*" )		# qboolean (*snapshotCallback)

DEF = re.compile( r"^(static\s+)?((?:ID_INLINE\s+)?(?:void|qboolean))\s+(\w+)\s*\(([^;{)]*)\)\s*\{", re.M )


def without_missionpack( text ):
	"""the source with what only Team Arena builds blanked out"""
	out, stack = [], []
	for line in text.split( "\n" ):
		s = line.strip()
		if s.startswith( "#if" ):
			if re.match( r"#ifdef\s+MISSIONPACK\b", s ):
				stack.append( False )
			elif re.match( r"#ifndef\s+MISSIONPACK\b", s ):
				stack.append( True )
			else:
				stack.append( None )		# not ours: both ways count
		elif s.startswith( "#else" ) and stack:
			if stack[-1] is not None:
				stack[-1] = not stack[-1]
		elif s.startswith( "#endif" ) and stack:
			stack.pop()
		keep = all( v is not False for v in stack )
		out.append( line if keep else "" )
	return "\n".join( out )


def param_types( params ):
	types = []
	for p in params.split( "," ):
		p = p.strip()
		if not p or p == "void":
			continue
		p = re.sub( r"\bconst\b", "", p )
		m = re.match( r"(\w+)\s*(\**)\s*\w*$", p.replace( "* ", "*" ).replace( " *", "*" ).strip() ) \
			or re.match( r"(\w+)\s*(\**)", p )
		if not m:
			return None
		types.append( m.group( 1 ) + m.group( 2 ) )
	return tuple( types )


def main():
	out, sources = sys.argv[1], sys.argv[2:]
	found = {}
	for path in sources:
		if not re.search( r"[/\\]game[/\\][^/\\]+\.c$", path ):
			continue
		text = without_missionpack( open( path, encoding = "latin-1" ).read() )
		for m in DEF.finditer( text ):
			static, ret, name, params = m.groups()
			if static or "ID_INLINE" in ret or "const" in params:
				continue		# (a const parameter isn't a callback's)
			types = param_types( params )
			if ret.endswith( "qboolean" ):
				if types == SNAPSHOT:
					found[name] = "qboolean %s( gentity_t *self, gentity_t *player );" % name
			elif types in KINDS:
				found[name] = KINDS[types] % name

	lines = [ "// made by code/tools/extractfuncs.py: the game's callbacks by name, for saved games",
			  "// (g_save.c); don't edit", "" ]
	lines += [ found[n] for n in sorted( found ) ]
	lines += [ "", "static const saveFunc_t g_saveFuncs[] = {" ]
	lines += [ "\t{ \"%s\", (void *)%s }," % ( n, n ) for n in sorted( found ) ]
	lines += [ "\t{ NULL, NULL }", "};", "" ]
	text = "\n".join( lines )
	if not os.path.exists( out ) or open( out ).read() != text:
		with open( out, "w" ) as fp:
			fp.write( text )


if __name__ == "__main__":
	main()
