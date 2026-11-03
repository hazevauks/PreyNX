/*
===========================================================================

PreyNX - native deko3D renderer: images.

===========================================================================
*/

#include "../../idlib/precompiled.h"
#include "../tr_local.h"

#undef BIT
#include <switch.h>

#include "dk_local.h"

struct dkImageData_t {
	DkImage			image;
	DkImageView		view;
	DkImageFormat	format;
	dkAlloc_t		alloc;
	bool			copyTarget;
};

static DkImageFormat Dk_ImageFormat( textureFormat_t format ) {
	switch ( format ) {
		case FMT_RGBA8:			return DkImageFormat_RGBA8_Unorm;
		case FMT_XRGB8:			return DkImageFormat_RGBA8_Unorm;
		case FMT_RGB565:		return DkImageFormat_RGB565_Unorm;
		case FMT_ALPHA:			return DkImageFormat_R8_Unorm;
		case FMT_L8A8:			return DkImageFormat_RG8_Unorm;
		case FMT_LUM8:			return DkImageFormat_R8_Unorm;
		case FMT_INT8:			return DkImageFormat_R8_Unorm;
		case FMT_DXT1:			return DkImageFormat_RGBA_BC1;
		case FMT_DXT3:			return DkImageFormat_RGBA_BC2;
		case FMT_DXT5:			return DkImageFormat_RGBA_BC3;
		case FMT_DEPTH:			return DkImageFormat_Z24X8;
		case FMT_DEPTH_STENCIL:	return DkImageFormat_Z24S8;
		case FMT_X16:			return DkImageFormat_R16_Unorm;
		case FMT_Y16_X16:		return DkImageFormat_RG16_Unorm;
		default:
			common->Error( "unhandled image format %d", format );
			return DkImageFormat_RGBA8_Unorm;
	}
}

// OpenGL's luminance and alpha formats
static void Dk_ImageSwizzle( const idImageOpts &opts, textureUsage_t usage, DkImageSwizzle swizzle[4] ) {
	swizzle[0] = DkImageSwizzle_Red;
	swizzle[1] = DkImageSwizzle_Green;
	swizzle[2] = DkImageSwizzle_Blue;
	swizzle[3] = DkImageSwizzle_Alpha;

	if ( opts.colorFormat == CFM_GREEN_ALPHA ) {
		swizzle[0] = swizzle[1] = swizzle[2] = DkImageSwizzle_One;
		swizzle[3] = DkImageSwizzle_Green;
		return;
	}
	switch ( opts.format ) {
		case FMT_XRGB8:
			swizzle[3] = DkImageSwizzle_One;
			break;
		case FMT_ALPHA:
			swizzle[0] = swizzle[1] = swizzle[2] = DkImageSwizzle_One;
			swizzle[3] = DkImageSwizzle_Red;
			break;
		case FMT_LUM8:
		case FMT_X16:
			swizzle[0] = swizzle[1] = swizzle[2] = DkImageSwizzle_Red;
			swizzle[3] = DkImageSwizzle_One;
			break;
		case FMT_INT8:
			swizzle[0] = swizzle[1] = swizzle[2] = swizzle[3] = DkImageSwizzle_Red;
			break;
		case FMT_L8A8:
		case FMT_Y16_X16:
			swizzle[0] = swizzle[1] = swizzle[2] = DkImageSwizzle_Red;
			swizzle[3] = DkImageSwizzle_Green;
			break;
		default:
			// the game's programs read a normal map's X from alpha (RXGB, retail .dds files)
			if ( usage == TD_BUMP && opts.colorFormat != CFM_NORMAL_DXT5 ) {
				swizzle[3] = DkImageSwizzle_Red;
			}
			break;
	}
}

/*
========================
idImage::AllocImage
========================
*/
void idImage::AllocImage() {
	PurgeImage();

	if ( !tr.IsOpenGLRunning() ) {
		return;
	}

	dkData = new dkImageData_t;
	memset( dkData, 0, sizeof( *dkData ) );
	dkData->format = Dk_ImageFormat( opts.format );

	DkImageLayoutMaker maker;
	dkImageLayoutMakerDefaults( &maker, dkr.device );
	maker.type = ( opts.textureType == TT_CUBIC ) ? DkImageType_Cubemap : DkImageType_2D;
	maker.format = dkData->format;
	maker.dimensions[0] = opts.width;
	maker.dimensions[1] = ( opts.textureType == TT_CUBIC ) ? opts.width : opts.height;
	maker.dimensions[2] = ( opts.textureType == TT_CUBIC ) ? 6 : 1;
	maker.mipLevels = opts.numLevels;
	// images the engine copies the screen into go through the 2D engine
	if ( !IsCompressed() && opts.numLevels == 1 && opts.textureType == TT_2D ) {
		maker.flags |= DkImageFlags_Usage2DEngine;
		dkData->copyTarget = true;
	}

	DkImageLayout layout;
	dkImageLayoutInitialize( &layout, &maker );

	if ( !dkr.imageHeap.Alloc( (uint32_t)dkImageLayoutGetSize( &layout ), dkImageLayoutGetAlignment( &layout ), dkData->alloc ) ) {
		common->FatalError( "deko3D: out of image memory for %s (%ux%u), %u MB in use",
			GetName(), opts.width, opts.height, dkr.imageHeap.BytesUsed() >> 20 );
	}
	dkImageInitialize( &dkData->image, &layout, dkData->alloc.block, dkData->alloc.offset );

	dkImageViewDefaults( &dkData->view, &dkData->image );
	Dk_ImageSwizzle( opts, usage, dkData->view.swizzle );

	texnum = Dk_AllocImageDescriptor();
	Dk_WriteImageDescriptor( texnum, dkData->view );
	SetTexParameters();
}

/*
========================
idImage::PurgeImage
========================
*/
void idImage::PurgeImage() {
	if ( dkData ) {
		Dk_DeferFreeImage( dkData->alloc, texnum );
		delete dkData;
		dkData = NULL;
	}
	texnum = TEXTURE_NOT_LOADED;
}

/*
========================
idImage::SubImageUpload

Width and height are in pixels, multiples of four for compressed data except
at the small mips.
========================
*/
void idImage::SubImageUpload( int mipLevel, int x, int y, int z, int width, int height, const void *pic, int pixelPitch ) const {
	if ( !dkData ) {
		return;
	}

	uint32_t rowBytes;
	uint32_t rows;
	if ( IsCompressed() ) {
		rowBytes = ( ( width + 3 ) / 4 ) * ( ( opts.format == FMT_DXT1 ) ? 8 : 16 );
		rows = ( height + 3 ) / 4;
	} else {
		rowBytes = width * BitsForFormat( opts.format ) / 8;
		rows = height;
	}
	const uint32_t size = rowBytes * rows;

	DkGpuAddr source;
	void *space = Dk_UploadSpace( size, 16, source );
	if ( opts.format == FMT_RGB565 ) {
		// the loader writes these big endian, as OpenGL's unpack swap expected
		const uint16_t *src = (const uint16_t *)pic;
		uint16_t *dst = (uint16_t *)space;
		for ( uint32_t i = 0; i < size / 2; i++ ) {
			dst[i] = (uint16_t)( ( src[i] << 8 ) | ( src[i] >> 8 ) );
		}
	} else if ( pixelPitch != 0 ) {
		const uint32_t sourceStride = pixelPitch * BitsForFormat( opts.format ) / 8;
		for ( uint32_t row = 0; row < rows; row++ ) {
			memcpy( (byte *)space + row * rowBytes, (const byte *)pic + row * sourceStride, rowBytes );
		}
	} else {
		memcpy( space, pic, size );
	}

	DkImageView view;
	dkImageViewDefaults( &view, &dkData->image );
	view.mipLevelOffset = mipLevel;
	view.mipLevelCount = 1;

	DkCopyBuf buffer = { source, 0, 0 };
	DkImageRect rect = { (uint32_t)x, (uint32_t)y, (uint32_t)z, (uint32_t)width, (uint32_t)height, 1 };
	dkCmdBufCopyBufferToImage( dkr.uploadCmd, &buffer, &view, &rect, 0 );
}

void idImage::SetPixel( int mipLevel, int x, int y, const void *data, int dataSize ) {
	SubImageUpload( mipLevel, x, y, 0, 1, 1, data );
}

/*
========================
idImage::SetTexParameters

The sampler is part of the handle a bind sets.
========================
*/
void idImage::SetTexParameters() {
	samplerIndex = Dk_SamplerIndex( filter, repeat );
}

void idImage::SetSamplerState( textureFilter_t tf, textureRepeat_t tr ) {
	if ( tf == filter && tr == repeat ) {
		return;
	}
	filter = tf;
	repeat = tr;
	SetTexParameters();
}

void idImage::Resize( int width, int height ) {
	if ( opts.width == width && opts.height == height ) {
		return;
	}
	opts.width = width;
	opts.height = height;
	AllocImage();
}

/*
==============
idImage::Bind
==============
*/
void idImage::Bind() {
	if ( !IsLoaded() ) {
		ActuallyLoadImage( true );
	}
	backEnd.glState.tmu[backEnd.glState.currenttmu].textureType = opts.textureType;
	Dk_SetTexture( backEnd.glState.currenttmu, dkMakeTextureHandle( texnum, samplerIndex ) );
}

void idImageManager::BindNull() {
}

void idImageManager::UnbindAll() {
}

/*
====================
idImage::CopyFramebuffer

The screen's rows are stored top first and a texture's bottom first, so the
copy flips it. Programs then read _currentRender as they do on OpenGL.
====================
*/
void idImage::CopyFramebuffer( int x, int y, int imageWidth, int imageHeight ) {
	if ( imageWidth <= 0 || imageHeight <= 0 ) {
		return;
	}
	if ( !dkData || !dkData->copyTarget || opts.width != imageWidth || opts.height != imageHeight || opts.format != FMT_RGBA8 ) {
		opts.width = imageWidth;
		opts.height = imageHeight;
		opts.format = FMT_RGBA8;
		opts.colorFormat = CFM_DEFAULT;
		opts.numLevels = 1;
		opts.textureType = TT_2D;
		AllocImage();
		Dk_SubmitUploads();
	}

	Dk_FlushForDraw();
	Dk_ColorTargetBarrier();

	DkImageRect srcRect = { (uint32_t)x, (uint32_t)( DK_SCREEN_HEIGHT - y - imageHeight ), 0, (uint32_t)imageWidth, (uint32_t)imageHeight, 1 };
	DkImageRect dstRect = { 0, 0, 0, (uint32_t)imageWidth, (uint32_t)imageHeight, 1 };
	dkCmdBufBlitImage( dkr.cmd, &dkr.colorViews[dkr.imageSlot], &srcRect, &dkData->view, &dstRect, DkBlitFlag_FlipY, 0 );
	dkCmdBufBarrier( dkr.cmd, DkBarrier_Primitives, DkInvalidateFlags_Image );

	backEnd.c_copyFrameBuffer++;
}

void idImage::CopyDepthbuffer( int x, int y, int imageWidth, int imageHeight ) {
	// nothing in Prey's shipped materials samples _currentDepth
}

/*
========================
Precompressed images

Prey ships its textures compressed in dds/, named after the image program that
would generate them. Loading those directly avoids decoding and compressing
on the console, and takes a fraction of the memory.
========================
*/
#define DDSF_FOURCC		0x00000004
#define DDSF_MIPMAPCOUNT 0x00020000
#define MAKE_FOURCC( a, b, c, d ) ( (unsigned int)(a) | ( (unsigned int)(b) << 8 ) | ( (unsigned int)(c) << 16 ) | ( (unsigned int)(d) << 24 ) )

typedef struct {
	unsigned int	magic;
	unsigned int	size;
	unsigned int	flags;
	unsigned int	height;
	unsigned int	width;
	unsigned int	pitchOrLinearSize;
	unsigned int	depth;
	unsigned int	mipMapCount;
	unsigned int	reserved1[11];
	unsigned int	pfSize;
	unsigned int	pfFlags;
	unsigned int	fourCC;
	unsigned int	rgbBitCount;
	unsigned int	rMask;
	unsigned int	gMask;
	unsigned int	bMask;
	unsigned int	aMask;
	unsigned int	caps[4];
	unsigned int	reserved2;
} ddsHeader_t;

static void Dk_ImageProgramToDdsName( const char *imageProgram, idStr &fileName ) {
	char name[MAX_IMAGE_NAME * 2];
	char *f = name;
	int depth = 0;
	for ( const char *s = imageProgram; *s && f < name + sizeof( name ) - 8; s++ ) {
		if ( *s == '/' || *s == '\\' || *s == '(' ) {
			if ( depth < 4 ) {
				*f = '/';
				depth++;
			} else {
				*f = ' ';
			}
			f++;
		} else if ( *s == '<' || *s == '>' || *s == ':' || *s == '|' || *s == '"' || *s == '.' ) {
			*f++ = '_';
		} else if ( *s == ' ' && f > name && f[-1] == '/' ) {
			// a space right after a slash goes away
		} else if ( *s != ')' && *s != ',' ) {
			*f++ = *s;
		}
	}
	*f = 0;
	fileName = "dds/";
	fileName += name;
	fileName += ".dds";
}

bool idImage::LoadPrecompressed() {
	if ( cubeFiles != CF_2D ) {
		return false;
	}

	idStr fileName;
	Dk_ImageProgramToDdsName( GetName(), fileName );

	byte *data = NULL;
	ID_TIME_T timestamp;
	const int length = fileSystem->ReadFile( fileName.c_str(), (void **)&data, &timestamp );
	if ( length < (int)sizeof( ddsHeader_t ) || !data ) {
		if ( data ) {
			fileSystem->FreeFile( data );
		}
		return false;
	}

	const ddsHeader_t *header = (const ddsHeader_t *)data;
	if ( header->magic != MAKE_FOURCC( 'D', 'D', 'S', ' ' ) ) {
		fileSystem->FreeFile( data );
		return false;
	}

	textureFormat_t format = FMT_NONE;
	textureColor_t colorFormat = CFM_DEFAULT;
	if ( header->pfFlags & DDSF_FOURCC ) {
		switch ( header->fourCC ) {
			case MAKE_FOURCC( 'D', 'X', 'T', '1' ):
				format = FMT_DXT1;
				break;
			case MAKE_FOURCC( 'D', 'X', 'T', '3' ):
				format = FMT_DXT3;
				break;
			case MAKE_FOURCC( 'D', 'X', 'T', '5' ):
				format = FMT_DXT5;
				break;
			case MAKE_FOURCC( 'R', 'X', 'G', 'B' ):
				format = FMT_DXT5;
				colorFormat = CFM_NORMAL_DXT5;
				break;
			default:
				break;
		}
	}
	if ( format == FMT_NONE ) {
		fileSystem->FreeFile( data );
		return false;
	}

	opts.textureType = TT_2D;
	opts.format = format;
	opts.colorFormat = colorFormat;
	opts.width = header->width;
	opts.height = header->height;
	opts.numLevels = ( header->flags & DDSF_MIPMAPCOUNT ) ? Max( 1u, header->mipMapCount ) : 1;
	sourceFileTime = timestamp;

	AllocImage();

	const byte *mip = data + sizeof( ddsHeader_t );
	const byte *end = data + length;
	const int blockBytes = ( format == FMT_DXT1 ) ? 8 : 16;
	int w = opts.width;
	int h = opts.height;
	for ( int level = 0; level < opts.numLevels; level++ ) {
		const int size = ( ( w + 3 ) / 4 ) * ( ( h + 3 ) / 4 ) * blockBytes;
		if ( mip + size > end ) {
			break;
		}
		SubImageUpload( level, 0, 0, 0, w, h, mip );
		mip += size;
		w = Max( 1, w >> 1 );
		h = Max( 1, h >> 1 );
	}

	fileSystem->FreeFile( data );
	return true;
}
