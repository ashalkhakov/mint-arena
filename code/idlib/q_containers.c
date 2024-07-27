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

#include "q_containers.h"
#include "idlib_local.h"
#include "l_script.h"
#include "l_precomp.h"

//===============================================================================

#define	MAX_PRINT_MSG		4096

void StripTrailing( char *str, const char c ) {
	int i, len;
	
    len = i = strlen( str );
	for( ; i > 0 && str[ i - 1 ] == c; i-- ) {
		str[ i - 1 ] = '\0';
		len--;
	}
}

/*
=================
FS_WriteFloatStringImpl
=================
*/
int FS_WriteFloatStringImpl( char *buf, const char *fmt, va_list argPtr ) {
	long i;
	unsigned long u;
	double f;
	char *str;
	int index;
	char tmp[MAX_QPATH], format[MAX_QPATH];
    char *formatPtr = format;

	index = 0;

	while( *fmt ) {
		switch( *fmt ) {
			case '%':
				format[0] = 0;
                formatPtr = &format[0];
				*formatPtr++ = *fmt++;
				while ( (*fmt >= '0' && *fmt <= '9') ||
						*fmt == '.' || *fmt == '-' || *fmt == '+' || *fmt == '#') {
					*formatPtr++ = *fmt++;
				}
				*formatPtr++ = *fmt;
				switch( *fmt ) {
					case 'f':
					case 'e':
					case 'E':
					case 'g':
					case 'G':
						f = va_arg( argPtr, double );
						if ( ( formatPtr - format ) <= 2 ) {
							// high precision floating point number without trailing zeros
							sprintf( tmp, "%1.10f", f );
							StripTrailing( tmp, '0' );
							StripTrailing( tmp, '.' );
							index += sprintf( buf+index, "%s", tmp );
						}
						else {
                            *formatPtr = 0;
							index += sprintf( buf+index, format, f );
						}
						break;
					case 'd':
					case 'i':
                        *formatPtr = 0;
						i = va_arg( argPtr, long );
						index += sprintf( buf+index, format, i );
						break;
					case 'u':
                        *formatPtr = 0;
						u = va_arg( argPtr, unsigned long );
						index += sprintf( buf+index, format, u );
						break;
					case 'o':
                        *formatPtr = 0;
						u = va_arg( argPtr, unsigned long );
						index += sprintf( buf+index, format, u );
						break;
					case 'x':
                        *formatPtr = 0;
						u = va_arg( argPtr, unsigned long );
						index += sprintf( buf+index, format, u );
						break;
					case 'X':
                        *formatPtr = 0;
						u = va_arg( argPtr, unsigned long );
						index += sprintf( buf+index, format, u );
						break;
					case 'c':
                        *formatPtr = 0;
						i = va_arg( argPtr, long );
						index += sprintf( buf+index, format, (char) i );
						break;
					case 's':
                        *formatPtr = 0;
						str = va_arg( argPtr, char * );
						index += sprintf( buf+index, format, str );
						break;
					case '%':
                        *formatPtr = 0;
						index += sprintf( buf+index, format );
						break;
					default:
						Com_Error( ERR_DROP, "FS_WriteFloatString: invalid format %s", format );
						break;
				}
				fmt++;
				break;
			case '\\':
				fmt++;
				switch( *fmt ) {
					case 't':
						index += sprintf( buf+index, "\t" );
						break;
					case 'v':
						index += sprintf( buf+index, "\v" );
						break;
					case 'n':
						index += sprintf( buf+index, "\n" );
						break;
					case '\\':
						index += sprintf( buf+index, "\\" );
						break;
					default:
						Com_Error( ERR_DROP, "FS_WriteFloatString: unknown escape character \'%c\'", *fmt );
						break;
				}
				fmt++;
				break;
			default:
				index += sprintf( buf+index, "%c", *fmt );
				fmt++;
				break;
		}
	}

	return index;
}

/*
=================
FS_WriteFloatString
=================
*/
int FS_WriteFloatString( fileHandle_t fs, const char *fmt, ... ) {
	char buf[MAX_PRINT_MSG];
	int len;
	va_list argPtr;

	va_start( argPtr, fmt );
	len = FS_WriteFloatStringImpl( buf, fmt, argPtr );
	va_end( argPtr );

	return ii.FS_Write( buf, len, fs );
}
