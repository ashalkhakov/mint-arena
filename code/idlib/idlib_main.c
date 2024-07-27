#include "idlib_local.h"
#include "dict.h"
#include "q_containers.h"

idlib_import_t ii;
static idlib_export_t idlib_export;

static void IdLibInit( void ) {
	Dict_Init();
}

static void IdLibShutdown( void ) {
	Dict_Shutdown();
}

idlib_export_t *GetIdLibAPI( int apiVersion, idlib_import_t *import ) {
	assert(import);
	ii = *import;
	assert(ii.Com_Printf);

	Com_Memset( &idlib_export, 0, sizeof( idlib_export ) );

	if ( apiVersion != IDLIB_API_VERSION ) {
		ii.Com_Error( ERR_DROP, "Mismatched IDLIB_API_VERSION: expected %i, got %i\n", IDLIB_API_VERSION, apiVersion );
		return NULL;
	}

	idlib_export.Init = IdLibInit;
	idlib_export.Shutdown = IdLibShutdown;

	return &idlib_export;
}