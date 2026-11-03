/*
===========================================================================

PreyNX - Nintendo Switch platform layer: entry point, files, time, memory,
logging and crash reports.

===========================================================================
*/

#include "../../idlib/precompiled.h"

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

// idlib and libnx both define BIT(); they compute the same value.
#undef BIT
#include <switch.h>

#include "../sys_local.h"
#include "switch_local.h"

// what is still running when the engine thread ends
void GLimp_ReleaseForExit( void );
void FS_StopBackgroundDownloadThread( void );

// recursive game code expects the large stack it has on Windows
static const size_t ENGINE_STACK_SIZE = 16 * 1024 * 1024;

// the game module is a private object in this executable (cmake/PreyGameObject.cmake)
extern "C" void *GetGameAPI( void *import );

static int			s_argc;
static char **		s_argv;
static int			s_exitCode = EXIT_SUCCESS;
static bool			s_nxlinkActive = false;
static FILE *		s_logFile = NULL;
static u64			s_logLastFlush = 0;
static Mutex		s_logMutex;
static char			s_fatalError[1024];
static char			s_fatalMessage[1024];
static int			s_loadingBoostDepth = 0;
static bool			s_perfConfigChanged = false;

// retail file checks are for PC installs; patched layouts differ per region
static const char *s_defaultArgs[] = {
	"Prey",
	"+set", "fs_validateOfficialPaks", "0",
	"+set", "com_allowConsole", "1",
	"+set", "r_fullscreen", "1",
};

/*
============================================================================
OUTPUT

Everything the engine prints goes to SD_LOG/prey_log.txt, and to nxlink when
started with `nxlink -s`. The file is flushed at most four times a second, and
on warnings, errors and crashes, so the last lines survive a crash without
paying for a card write per line during loading.
============================================================================
*/

static void Switch_OpenLog( void ) {
	mkdir( SWITCH_LOG_PATH, 0777 );
	remove( SWITCH_LOG_PREVIOUS );
	rename( SWITCH_LOG_FILE, SWITCH_LOG_PREVIOUS );
	s_logFile = fopen( SWITCH_LOG_FILE, "wb" );
	if ( s_logFile ) {
		setvbuf( s_logFile, NULL, _IOFBF, 32 * 1024 );
	}
	mutexInit( &s_logMutex );
}

void Switch_FlushLog( void ) {
	if ( s_logFile ) {
		fflush( s_logFile );
	}
}

static void Switch_LogWrite( const char *text ) {
	if ( s_nxlinkActive ) {
		fputs( text, stdout );
	}
	if ( !s_logFile ) {
		return;
	}
	mutexLock( &s_logMutex );
	fputs( text, s_logFile );
	const u64 now = armGetSystemTick();
	const bool important = strstr( text, "WARNING" ) != NULL || strstr( text, "ERROR" ) != NULL;
	if ( important || now - s_logLastFlush > armGetSystemTickFreq() / 4 ) {
		fflush( s_logFile );
		s_logLastFlush = now;
	}
	mutexUnlock( &s_logMutex );
}

void Sys_Printf( const char *fmt, ... ) {
	char text[4096];
	va_list ap;
	va_start( ap, fmt );
	idStr::vsnPrintf( text, sizeof( text ), fmt, ap );
	va_end( ap );
	Switch_LogWrite( text );
}

void Sys_VPrintf( const char *fmt, va_list arg ) {
	char text[4096];
	idStr::vsnPrintf( text, sizeof( text ), fmt, arg );
	Switch_LogWrite( text );
}

void Sys_DebugPrintf( const char *fmt, ... ) {
	va_list ap;
	va_start( ap, fmt );
	Sys_VPrintf( fmt, ap );
	va_end( ap );
}

void Sys_DebugVPrintf( const char *fmt, va_list arg ) {
	Sys_VPrintf( fmt, arg );
}

void Sys_ShowConsole( int visLevel, bool quitOnClose ) {
}

/*
============================================================================
EXIT AND FATAL ERRORS

The message is written to its own file before anything shuts down, so a crash
during shutdown cannot lose it. The engine thread ends with pthread_exit and
the process leaves from main, on the main thread.
============================================================================
*/

static void Switch_WriteFatalFile( const char *msg ) {
	FILE *f = fopen( SWITCH_FATAL_FILE, "wb" );
	if ( f ) {
		fputs( msg, f );
		fputc( '\n', f );
		fclose( f );
	}
}

static void Switch_Exit( int code ) {
	s_exitCode = code;
	Switch_FlushLog();
	pthread_exit( NULL );
}

void Sys_Quit( void ) {
	Sys_ShutdownInput();
	Switch_Exit( EXIT_SUCCESS );
}

void Sys_Error( const char *error, ... ) {
	char text[1024];
	va_list ap;
	va_start( ap, error );
	idStr::vsnPrintf( text, sizeof( text ), error, ap );
	va_end( ap );

	Switch_WriteFatalFile( text );
	idStr::Copynz( s_fatalMessage, text, sizeof( s_fatalMessage ) );
	Sys_Printf( "ERROR: %s\n", text );
	Switch_Exit( EXIT_FAILURE );
}

void Sys_SetFatalError( const char *error ) {
	idStr::Copynz( s_fatalError, error, sizeof( s_fatalError ) );
	Switch_WriteFatalFile( s_fatalError );
}

/*
============================================================================
CRASH REPORTS

The libnx exception handler writes SD_LOG/prey_crash.txt. Addresses in the
executable are also given as ELF offsets, for:
	aarch64-none-elf-addr2line -f -C -e prey.elf <offset>...
============================================================================
*/

extern "C" {
	u32		__nx_exception_ignoredebug = 1;
	alignas( 16 ) u8 __nx_exception_stack[0x8000];
	u64		__nx_exception_stack_size = sizeof( __nx_exception_stack );
	void	__libnx_exception_handler( ThreadExceptionDump *ctx );
}

static bool Switch_IsReadable( u64 addr ) {
	MemoryInfo info;
	u32 pageInfo;
	if ( R_FAILED( svcQueryMemory( &info, &pageInfo, addr ) ) ) {
		return false;
	}
	return ( info.perm & Perm_R ) != 0 && info.type != MemType_Unmapped;
}

static void Switch_CrashAddress( FILE *f, const char *label, u64 addr, u64 codeStart, u64 codeEnd ) {
	if ( addr >= codeStart && addr < codeEnd ) {
		fprintf( f, "%-4s 0x%016llx  (elf offset 0x%llx)\n", label, (unsigned long long)addr, (unsigned long long)( addr - codeStart ) );
	} else {
		fprintf( f, "%-4s 0x%016llx\n", label, (unsigned long long)addr );
	}
}

void __libnx_exception_handler( ThreadExceptionDump *ctx ) {
	MemoryInfo code = {};
	u32 pageInfo;
	svcQueryMemory( &code, &pageInfo, (u64)&__libnx_exception_handler );
	const u64 codeStart = code.addr;
	const u64 codeEnd = code.addr + code.size;

	FILE *f = fopen( SWITCH_CRASH_FILE, "wb" );
	if ( f ) {
		fprintf( f, "Prey crash\nexception 0x%x  far 0x%016llx  esr 0x%x\n", ctx->error_desc, (unsigned long long)ctx->far.x, ctx->esr );
		fprintf( f, "alignment fault: %s\n", ( ctx->error_desc == ThreadExceptionDesc_MisalignedPC || ctx->error_desc == ThreadExceptionDesc_MisalignedSP ) ? "yes" : "no" );
		Switch_CrashAddress( f, "pc", ctx->pc.x, codeStart, codeEnd );
		Switch_CrashAddress( f, "lr", ctx->lr.x, codeStart, codeEnd );
		fprintf( f, "sp   0x%016llx\nfp   0x%016llx\nbacktrace:\n", (unsigned long long)ctx->sp.x, (unsigned long long)ctx->fp.x );

		u64 fp = ctx->fp.x;
		for ( int depth = 0; depth < 64 && fp && ( fp & 7 ) == 0; depth++ ) {
			if ( !Switch_IsReadable( fp ) || !Switch_IsReadable( fp + 8 ) ) {
				break;
			}
			const u64 nextFp = ( (const u64 *)fp )[0];
			char label[8];
			idStr::snPrintf( label, sizeof( label ), "#%d", depth );
			Switch_CrashAddress( f, label, ( (const u64 *)fp )[1], codeStart, codeEnd );
			if ( nextFp <= fp ) {
				break;
			}
			fp = nextFp;
		}
		for ( int i = 0; i < 29; i++ ) {
			fprintf( f, "x%-2d 0x%016llx%s", i, (unsigned long long)ctx->cpu_gprs[i].x, ( i % 3 == 2 ) ? "\n" : "  " );
		}
		fclose( f );
	}
	Switch_FlushLog();
	svcExitProcess();
}

/*
============================================================================
TIME AND CPU
============================================================================
*/

static u64 s_timeBase = 0;

int Sys_Milliseconds( void ) {
	if ( !s_timeBase ) {
		s_timeBase = armGetSystemTick();
	}
	return (int)( armTicksToNs( armGetSystemTick() - s_timeBase ) / 1000000ULL );
}

double Sys_GetClockTicks( void ) {
	return (double)armGetSystemTick();
}

double Sys_ClockTicksPerSecond( void ) {
	return (double)armGetSystemTickFreq();
}

void Sys_Sleep( int msec ) {
	if ( msec > 0 ) {
		svcSleepThread( (s64)msec * 1000000LL );
	}
}

cpuid_t Sys_GetProcessorId( void ) {
	return CPUID_GENERIC;
}

const char *Sys_GetProcessorString( void ) {
	return "Tegra X1 (Cortex-A57)";
}

bool Sys_FPU_StackIsEmpty( void ) {
	return true;
}

void Sys_FPU_ClearStack( void ) {
}

const char *Sys_FPU_GetState( void ) {
	return "";
}

void Sys_FPU_EnableExceptions( int exceptions ) {
}

void Sys_FPU_SetPrecision( int precision ) {
}

void Sys_FPU_SetRounding( int rounding ) {
}

void Sys_FPU_SetFTZ( bool enable ) {
}

void Sys_FPU_SetDAZ( bool enable ) {
}

/*
============================================================================
CLOCKS

Handheld runs at the official 1020 / 460.8 / 1331 MHz configuration; docked
keeps the system's default, which has more GPU. Map loads use the system's
FastLoad boost, as retail games do on loading screens.
============================================================================
*/

static const u32 SWITCH_HANDHELD_DEFAULT_CONFIG	= 0x00020003;	// 1020 / 307.2 / 1331
static const u32 SWITCH_HANDHELD_GAME_CONFIG	= 0x92220008;	// 1020 / 460.8 / 1331

static void Switch_ApplyClocks( void ) {
	const Result rc = apmSetPerformanceConfiguration( ApmPerformanceMode_Normal, SWITCH_HANDHELD_GAME_CONFIG );
	s_perfConfigChanged = R_SUCCEEDED( rc );
	Sys_Printf( "handheld clocks 1020/460/1331: %s (0x%x)\n", R_SUCCEEDED( rc ) ? "set" : "failed", rc );
}

static void Switch_RestoreClocks( void ) {
	if ( s_perfConfigChanged ) {
		apmSetPerformanceConfiguration( ApmPerformanceMode_Normal, SWITCH_HANDHELD_DEFAULT_CONFIG );
		s_perfConfigChanged = false;
	}
}

void Sys_SetLoadingBoost( bool enable ) {
	if ( enable ) {
		if ( s_loadingBoostDepth++ == 0 ) {
			appletSetCpuBoostMode( ApmCpuBoostMode_FastLoad );
		}
	} else if ( s_loadingBoostDepth > 0 && --s_loadingBoostDepth == 0 ) {
		appletSetCpuBoostMode( ApmCpuBoostMode_Normal );
	}
}

bool Switch_IsLoading( void ) {
	return s_loadingBoostDepth > 0;
}

/*
============================================================================
MEMORY
============================================================================
*/

int Sys_GetSystemRam( void ) {
	u64 total = 0;
	svcGetInfo( &total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0 );
	return (int)( total / ( 1024 * 1024 ) );
}

int Sys_GetVideoRam( void ) {
	return 1024;
}

void Sys_GetCurrentMemoryStatus( sysMemoryStats_t &stats ) {
	u64 total = 0, used = 0;
	svcGetInfo( &total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0 );
	svcGetInfo( &used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0 );
	memset( &stats, 0, sizeof( stats ) );
	stats.totalPhysical = (int)( total / 1024 );
	stats.availPhysical = (int)( ( total - used ) / 1024 );
}

void Sys_GetExeLaunchMemoryStatus( sysMemoryStats_t &stats ) {
	Sys_GetCurrentMemoryStatus( stats );
}

bool Sys_LockMemory( void *ptr, int bytes ) {
	return true;
}

bool Sys_UnlockMemory( void *ptr, int bytes ) {
	return true;
}

void Sys_SetPhysicalWorkMemory( int minBytes, int maxBytes ) {
}

void Sys_FlushCacheMemory( void *base, int bytes ) {
}

void Sys_GetCallStack( address_t *callStack, const int callStackSize ) {
	for ( int i = 0; i < callStackSize; i++ ) {
		callStack[i] = 0;
	}
}

const char *Sys_GetCallStackStr( const address_t *callStack, const int callStackSize ) {
	return "";
}

const char *Sys_GetCallStackCurStr( int depth ) {
	return "";
}

const char *Sys_GetCallStackCurAddressStr( int depth ) {
	return "";
}

void Sys_ShutdownSymbols( void ) {
}

/*
============================================================================
FILES AND PATHS
============================================================================
*/

const char *Sys_DefaultBasePath( void ) {
	return SWITCH_BASE_PATH;
}

const char *Sys_DefaultSavePath( void ) {
	return SWITCH_BASE_PATH;
}

const char *Sys_DefaultCDPath( void ) {
	return "";
}

const char *Sys_EXEPath( void ) {
	return SWITCH_BASE_PATH "/prey.nro";
}

void Sys_Mkdir( const char *path ) {
	mkdir( path, 0777 );
}

ID_TIME_T Sys_FileTimeStamp( FILE *fp ) {
	struct stat st;
	if ( fstat( fileno( fp ), &st ) != 0 ) {
		return 0;
	}
	return st.st_mtime;
}

int Sys_GetDriveFreeSpace( const char *path ) {
	struct statvfs st;
	if ( statvfs( SWITCH_BASE_PATH, &st ) != 0 ) {
		return 1000;
	}
	return (int)( ( (u64)st.f_bavail * st.f_frsize ) / ( 1024 * 1024 ) );
}

int Sys_ListFiles( const char *directory, const char *extension, idStrList &list ) {
	list.Clear();

	if ( !extension ) {
		extension = "";
	}

	// passing a slash as extension finds directories
	const bool dironly = extension[0] == '/' && extension[1] == 0;

	DIR *fdir = opendir( directory );
	if ( !fdir ) {
		return -1;
	}

	struct dirent *d;
	while ( ( d = readdir( fdir ) ) != NULL ) {
		const bool isDir = d->d_type == DT_DIR;
		if ( dironly != isDir ) {
			continue;
		}
		const idStr name = d->d_name;
		if ( extension[0] && !dironly ) {
			idStr ext;
			name.ExtractFileExtension( ext );
			if ( ext.Icmp( extension[0] == '.' ? extension + 1 : extension ) != 0 ) {
				continue;
			}
		}
		list.Append( name );
	}
	closedir( fdir );

	return list.Num();
}

/*
============================================================================
GAME MODULE

Homebrew cannot load a library, so the game is linked in as an object with all
its symbols private, and "loading" it hands out GetGameAPI.
============================================================================
*/

static const intptr_t GAME_MODULE_HANDLE = 1;

intptr_t Sys_DLL_Load( const char *dllName ) {
	return GAME_MODULE_HANDLE;
}

void *Sys_DLL_GetProcAddress( intptr_t dllHandle, const char *procName ) {
	if ( dllHandle == GAME_MODULE_HANDLE && !idStr::Cmp( procName, "GetGameAPI" ) ) {
		return (void *)GetGameAPI;
	}
	return NULL;
}

void Sys_DLL_Unload( intptr_t dllHandle ) {
}

/*
============================================================================
EVENTS
============================================================================
*/

static const int	MAX_QUEUED_EVENTS = 256;
static const int	MASK_QUEUED_EVENTS = MAX_QUEUED_EVENTS - 1;
static sysEvent_t	s_eventQueue[MAX_QUEUED_EVENTS];
static int			s_eventHead = 0;
static int			s_eventTail = 0;

void Switch_QueueEvent( sysEventType_t type, int value, int value2, int ptrLength, void *ptr ) {
	sysEvent_t *ev = &s_eventQueue[s_eventHead & MASK_QUEUED_EVENTS];
	if ( s_eventHead - s_eventTail >= MAX_QUEUED_EVENTS ) {
		if ( ev->evPtr ) {
			Mem_Free( ev->evPtr );
		}
		s_eventTail++;
	}
	s_eventHead++;

	ev->evType = type;
	ev->evValue = value;
	ev->evValue2 = value2;
	ev->evPtrLength = ptrLength;
	ev->evPtr = ptr;
}

sysEvent_t Sys_GetEvent( void ) {
	if ( s_eventHead > s_eventTail ) {
		s_eventTail++;
		return s_eventQueue[( s_eventTail - 1 ) & MASK_QUEUED_EVENTS];
	}
	sysEvent_t ev;
	memset( &ev, 0, sizeof( ev ) );
	return ev;
}

void Sys_ClearEvents( void ) {
	s_eventHead = s_eventTail = 0;
}

void Sys_GenerateEvents( void ) {
	if ( !appletMainLoop() ) {
		cmdSystem->BufferCommandText( CMD_EXEC_APPEND, "quit\n" );
	}
	Switch_PollInput();
}

/*
============================================================================
MISC
============================================================================
*/

void idSysLocal::OpenURL( const char *url, bool quit ) {
}

void idSysLocal::StartProcess( const char *exeName, bool quit ) {
}

void Sys_Init( void ) {
}

void Sys_Shutdown( void ) {
}

bool Sys_AlreadyRunning( void ) {
	return false;
}

char *Sys_GetClipboardData( void ) {
	return NULL;
}

void Sys_SetClipboardData( const char *string ) {
}

void Sys_DoPreferences( void ) {
}

void Sys_GrabMouseCursor( bool grabIt ) {
}

void Sys_ShowWindow( bool show ) {
}

bool Sys_IsWindowVisible( void ) {
	return true;
}

bool Sys_IsGameWindowFocused( void ) {
	return true;
}

/*
============================================================================
ENTRY POINT

The engine runs on its own thread with a large stack and core 0 to itself.
============================================================================
*/

static void *Switch_EngineThread( void * ) {
	idList<const char *> args;
	for ( size_t i = 0; i < sizeof( s_defaultArgs ) / sizeof( s_defaultArgs[0] ); i++ ) {
		args.Append( s_defaultArgs[i] );
	}
	for ( int i = 1; i < s_argc; i++ ) {
		args.Append( s_argv[i] );
	}

	Sys_SetLoadingBoost( true );
	common->Init( args.Num(), args.Ptr(), NULL );
	Sys_SetLoadingBoost( false );

	Switch_ApplyDefaultBinds();
	Switch_ApplyClocks();
	Switch_StartAsyncThread();

	while ( 1 ) {
		common->Frame();
	}
	return NULL;
}

/*
================
Switch_ShowMessage

The console's own text screen, for what the player must read. The renderer is not
running at this point: before it starts, or after it shut down.
================
*/
static void Switch_ShowMessage( const char *title, const char *message ) {
	consoleInit( NULL );
	printf( "\n  %s\n\n  %s\n\n  Press + to exit.\n", title, message );
	consoleUpdate( NULL );

	padConfigureInput( 1, HidNpadStyleSet_NpadStandard );
	PadState pad;
	padInitializeDefault( &pad );
	while ( appletMainLoop() ) {
		padUpdate( &pad );
		if ( padGetButtonsDown( &pad ) & HidNpadButton_Plus ) {
			break;
		}
		consoleUpdate( NULL );
	}
	consoleExit( NULL );
}

// the album's applet mode leaves a fraction of the memory the game needs
static const u64 MIN_MEMORY_BYTES = 2800ULL * 1024 * 1024;

int main( int argc, char **argv ) {
	s_argc = argc;
	s_argv = argv;

	u64 totalMemory = 0;
	svcGetInfo( &totalMemory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0 );
	if ( totalMemory < MIN_MEMORY_BYTES ) {
		Switch_ShowMessage( "Prey needs more memory.", "Start it from a forwarder, or hold R while starting a game, not from the album." );
		return EXIT_FAILURE;
	}

	// stdout over the network when started with `nxlink -s`
	if ( __nxlink_host.s_addr != 0 && R_SUCCEEDED( socketInitializeDefault() ) ) {
		s_nxlinkActive = nxlinkStdio() >= 0;
		if ( !s_nxlinkActive ) {
			socketExit();
		}
	}

	mkdir( SWITCH_BASE_PATH, 0777 );
	chdir( SWITCH_BASE_PATH );
	Switch_OpenLog();

	Sys_Milliseconds();
	Switch_InitThreads();

	pthread_attr_t attr;
	pthread_attr_init( &attr );
	pthread_attr_setstacksize( &attr, ENGINE_STACK_SIZE );
	Switch_SetNextThreadCore( SWITCH_CORE_GAME );
	pthread_t engineThread;
	if ( pthread_create( &engineThread, &attr, Switch_EngineThread, NULL ) != 0 ) {
		Switch_WriteFatalFile( "could not create the engine thread" );
		return EXIT_FAILURE;
	}
	pthread_attr_destroy( &attr );

	pthread_join( engineThread, NULL );

	Switch_StopAsyncThread();
	FS_StopBackgroundDownloadThread();
	GLimp_ReleaseForExit();
	Switch_RestoreClocks();
	Switch_FlushLog();
	if ( s_exitCode != EXIT_SUCCESS && s_fatalMessage[0] ) {
		Switch_ShowMessage( "Prey stopped with an error:", s_fatalMessage );
	}
	if ( s_logFile ) {
		fclose( s_logFile );
		s_logFile = NULL;
	}
	if ( s_nxlinkActive ) {
		socketExit();
	}
	return s_exitCode;
}
