/*
===========================================================================

PreyNX - native deko3D renderer: shader modules.

The renderer's own programs are built into the NRO's romfs. The game's material
programs (glprogs/*.vfp in the retail data) are translated and compiled on the
PC from the player's own copy by tools/build_shaders.py, into
sdmc:/switch/prey/shaders/<program>.<vert|frag>.dksh.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../tr_local.h"

#undef BIT
#include <switch.h>

#include "dk_local.h"

static const uint32_t SHADER_CODE_SIZE	= 16 * 1024 * 1024;

static const char *ROMFS_SHADER_PATH	= "romfs:/shaders/";
static const char *CARD_SHADER_PATH		= "sdmc:/switch/prey/shaders/";

typedef struct {
	const char *	vertex;
	const char *	fragment;
} dkFixedProgramDef_t;

static const dkFixedProgramDef_t s_fixedDefs[DKP_COUNT] = {
	{ "depth", "depth" },
	{ "depth_clip", "depth" },
	{ "depth_alpha", "depth_alpha" },
	{ "stage", "stage" },
	{ "stage_normal_cube", "stage_cube" },
	{ "stage_tex3_cube", "stage_cube" },
	{ "stage_screen", "stage_screen" },
	{ "color", "color" },
	{ "depth_clip", "color" },
	{ "shadow", "depth" },
	{ "interaction", "interaction" },
	{ "environment", "environment" },
	{ "bumpyenvironment", "bumpyenvironment" },
	{ "fog", "fog" },
	{ "blendlight", "blendlight" },
};

typedef struct {
	char			name[64];
	GLenum			target;
	DkShader		shader;
	bool			loaded;
	bool			missing;
} dkMaterialProgram_t;

static const int MAX_MATERIAL_PROGRAMS = 200;

static DkMemBlock			s_codeBlock;
static uint32_t				s_codeHead;
static DkShader				s_fixedVertex[DKP_COUNT];
static DkShader				s_fixedFragment[DKP_COUNT];
static dkMaterialProgram_t	s_materialPrograms[MAX_MATERIAL_PROGRAMS];
static int					s_numMaterialPrograms;

static bool Dk_LoadShaderFile( const char *path, DkShader &shader ) {
	FILE *f = fopen( path, "rb" );
	if ( !f ) {
		return false;
	}
	fseek( f, 0, SEEK_END );
	const uint32_t size = (uint32_t)ftell( f );
	fseek( f, 0, SEEK_SET );

	const uint32_t offset = ( s_codeHead + DK_SHADER_CODE_ALIGNMENT - 1 ) & ~( DK_SHADER_CODE_ALIGNMENT - 1 );
	if ( offset + size + DK_SHADER_CODE_UNUSABLE_SIZE > SHADER_CODE_SIZE ) {
		fclose( f );
		common->FatalError( "deko3D: out of shader code memory" );
	}

	void *code = (byte *)dkMemBlockGetCpuAddr( s_codeBlock ) + offset;
	const bool ok = fread( code, 1, size, f ) == size;
	fclose( f );
	if ( !ok ) {
		return false;
	}
	s_codeHead = offset + size;
	Dk_DescriptorsChanged();	// the next submission drops the code cache too

	DkShaderMaker maker;
	dkShaderMakerDefaults( &maker, s_codeBlock, offset );
	dkShaderInitialize( &shader, &maker );
	return dkShaderIsValid( &shader );
}

static bool Dk_LoadNamedShader( const char *name, const char *stage, DkShader &shader ) {
	const idStr romfsPath = va( "%s%s.%s.dksh", ROMFS_SHADER_PATH, name, stage );
	if ( Dk_LoadShaderFile( romfsPath.c_str(), shader ) ) {
		return true;
	}
	const idStr cardPath = va( "%s%s.%s.dksh", CARD_SHADER_PATH, name, stage );
	return Dk_LoadShaderFile( cardPath.c_str(), shader );
}

void Dk_InitPrograms( void ) {
	DkMemBlockMaker maker;
	dkMemBlockMakerDefaults( &maker, dkr.device, SHADER_CODE_SIZE );
	maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
	s_codeBlock = dkMemBlockCreate( &maker );
	s_codeHead = 0;

	// materials keep their program indexes across a renderer restart
	for ( int i = 0; i < s_numMaterialPrograms; i++ ) {
		s_materialPrograms[i].loaded = false;
		s_materialPrograms[i].missing = false;
	}

	// a forwarder may not say where the NRO is
	if ( R_FAILED( romfsInit() ) && R_FAILED( romfsMountFromFsdev( "sdmc:/switch/prey/prey.nro", 0, "romfs" ) ) ) {
		common->FatalError( "deko3D: the NRO's romfs could not be mounted" );
	}
	for ( int i = 0; i < DKP_COUNT; i++ ) {
		if ( !Dk_LoadNamedShader( s_fixedDefs[i].vertex, "vert", s_fixedVertex[i] ) ) {
			common->FatalError( "deko3D: missing shader %s.vert", s_fixedDefs[i].vertex );
		}
		if ( !Dk_LoadNamedShader( s_fixedDefs[i].fragment, "frag", s_fixedFragment[i] ) ) {
			common->FatalError( "deko3D: missing shader %s.frag", s_fixedDefs[i].fragment );
		}
	}
}

void Dk_ShutdownPrograms( void ) {
	romfsExit();
	if ( s_codeBlock ) {
		dkMemBlockDestroy( s_codeBlock );
		s_codeBlock = NULL;
	}
}

void Dk_UseFixedProgram( dkFixedProgram_t program ) {
	Dk_SetProgram( &s_fixedVertex[program], &s_fixedFragment[program] );
}

const DkShader *Dk_FixedVertexShader( dkFixedProgram_t program ) {
	return &s_fixedVertex[program];
}

/*
============================================================================
Material programs

Materials ask for programs by file name, in any case, and a file can hold both
a vertex and a fragment program. The index goes straight into the material; the
shader is loaded the first time a stage draws with it.
============================================================================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	idStr stripped = program;
	stripped.StripFileExtension();
	stripped.ToLower();

	for ( int i = 0; i < s_numMaterialPrograms; i++ ) {
		if ( s_materialPrograms[i].target == target && !idStr::Cmp( s_materialPrograms[i].name, stripped.c_str() ) ) {
			return i + 1;
		}
	}
	if ( s_numMaterialPrograms == MAX_MATERIAL_PROGRAMS ) {
		common->Error( "R_FindARBProgram: too many programs" );
	}

	dkMaterialProgram_t &entry = s_materialPrograms[s_numMaterialPrograms++];
	idStr::Copynz( entry.name, stripped.c_str(), sizeof( entry.name ) );
	entry.target = target;
	entry.loaded = false;
	entry.missing = false;
	return s_numMaterialPrograms;
}

/*
================
R_ARBProgramUsesInteractionInputs

Interaction programs are the ones that sample the light falloff and projection
textures. Varyings do not tell: tangent space warps like distort.vfp use the same ones.
================
*/
bool R_ARBProgramUsesInteractionInputs( GLenum target, const char *program ) {
	if ( target != GL_FRAGMENT_PROGRAM_ARB ) {
		return false;
	}

	idStr fileName = "glprogs/";
	fileName += program;
	void *buffer = NULL;
	if ( fileSystem->ReadFile( fileName.c_str(), &buffer ) <= 0 || !buffer ) {
		return false;
	}

	idStr source = (const char *)buffer;
	fileSystem->FreeFile( buffer );

	const int start = source.Find( "!!ARBfp" );
	if ( start < 0 ) {
		return false;
	}

	// comments and spacing out
	idStr code;
	bool comment = false;
	for ( int i = start; i < source.Length(); i++ ) {
		const char c = source[i];
		if ( c == '#' ) {
			comment = true;
		} else if ( c == '\n' || c == '\r' ) {
			comment = false;
		} else if ( !comment && c != ' ' && c != '\t' ) {
			code.Append( c );
		}
	}
	return code.Find( "texture[2]" ) >= 0 && code.Find( "texture[3]" ) >= 0;
}

const DkShader *Dk_MaterialShader( int programIndex ) {
	if ( programIndex <= 0 || programIndex > s_numMaterialPrograms ) {
		return NULL;
	}
	dkMaterialProgram_t &entry = s_materialPrograms[programIndex - 1];
	if ( entry.missing ) {
		return NULL;
	}
	if ( !entry.loaded ) {
		const char *stage = ( entry.target == GL_VERTEX_PROGRAM_ARB ) ? "vert" : "frag";
		if ( !Dk_LoadNamedShader( entry.name, stage, entry.shader ) ) {
			common->Warning( "deko3D: no shader for program %s (%s): its stages will not draw", entry.name, stage );
			entry.missing = true;
			return NULL;
		}
		entry.loaded = true;
	}
	return &entry.shader;
}

void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
}

void R_ARB2_Init( void ) {
	glConfig.allowARB2Path = true;
}
