// Implementation of the BMP writer and header reader. See bmpout.h.
#include "platform/bmpout.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace atui {
namespace platform {

namespace {

// Small-endian writers, spelled out because the file format says
// little-endian and the platforms disagree about what that means for a
// long: OS/2 on a Pentium and Linux on an AMD64 agree, but a hand-written
// on-disk format should not rest on that agreement.
void put16( unsigned char *p, unsigned v )
{
    p[ 0 ] = (unsigned char)( v & 0xFF );
    p[ 1 ] = (unsigned char)( ( v >> 8 ) & 0xFF );
}

void put32( unsigned char *p, unsigned long v )
{
    p[ 0 ] = (unsigned char)( v & 0xFF );
    p[ 1 ] = (unsigned char)( ( v >> 8 ) & 0xFF );
    p[ 2 ] = (unsigned char)( ( v >> 16 ) & 0xFF );
    p[ 3 ] = (unsigned char)( ( v >> 24 ) & 0xFF );
}

unsigned long get32( const unsigned char *p )
{
    return (unsigned long)p[ 0 ]
         | ( (unsigned long)p[ 1 ] << 8 )
         | ( (unsigned long)p[ 2 ] << 16 )
         | ( (unsigned long)p[ 3 ] << 24 );
}

int get32s( const unsigned char *p )
{
    return (int)get32( p );
}

} // anonymous namespace

bool writeBmp24( const std::string &path, const unsigned char *rgb,
                 int w, int h, std::string &err )
{
    if( rgb == 0 || w <= 0 || h <= 0 )
    {
        err = "no pixels to write";
        return false;
    }

    // Rows pad to 4 bytes; the picture is stored bottom-up.
    const int rowBytes  = w * 3;
    const int rowStride = ( rowBytes + 3 ) & ~3;
    const unsigned long dataSize =
        (unsigned long)rowStride * (unsigned long)h;
    const unsigned long fileSize = 14UL + 40UL + dataSize;

    unsigned char header[ 54 ];
    std::memset( header, 0, sizeof header );

    // BITMAPFILEHEADER.
    header[ 0 ] = 'B';
    header[ 1 ] = 'M';
    put32( header + 2, fileSize );
    put32( header + 10, 54UL );

    // BITMAPINFOHEADER.
    put32( header + 14, 40UL );
    put32( header + 18, (unsigned long)w );
    put32( header + 22, (unsigned long)h );      // positive: bottom-up
    put16( header + 26, 1 );                      // planes
    put16( header + 28, 24 );                     // bits per pixel
    put32( header + 34, dataSize );

    std::FILE *f = std::fopen( path.c_str(), "wb" );
    if( f == 0 )
    {
        err = "cannot open " + path + " for writing";
        return false;
    }

    bool ok = std::fwrite( header, 1, sizeof header, f ) == sizeof header;

    // One padded row buffer, reused bottom row first.
    std::vector<unsigned char> row( (size_t)rowStride, 0 );
    for( int y = h - 1; ok && y >= 0; --y )
    {
        const unsigned char *src = rgb + (size_t)y * (size_t)rowBytes;
        for( int x = 0; x < w; ++x )
        {
            row[ (size_t)x * 3 + 0 ] = src[ x * 3 + 2 ];  // B
            row[ (size_t)x * 3 + 1 ] = src[ x * 3 + 1 ];  // G
            row[ (size_t)x * 3 + 2 ] = src[ x * 3 + 0 ];  // R
        }
        ok = std::fwrite( &row[ 0 ], 1, (size_t)rowStride, f )
             == (size_t)rowStride;
    }

    if( std::fclose( f ) != 0 )
        ok = false;
    if( !ok )
    {
        err = "writing " + path + " failed";
        std::remove( path.c_str() );
    }
    return ok;
}

bool bmpHeaderInfo( const std::string &path, int &w, int &h, int &bpp,
                    std::string &err )
{
    w = h = bpp = 0;

    std::FILE *f = std::fopen( path.c_str(), "rb" );
    if( f == 0 )
    {
        err = "cannot open " + path;
        return false;
    }

    unsigned char head[ 32 ];   // past either header kind: the Windows
                                // bit count lives at byte 28
    const size_t got = std::fread( head, 1, sizeof head, f );
    std::fclose( f );

    if( got < 26 || head[ 0 ] != 'B' || head[ 1 ] != 'M' )
    {
        err = path + " is not a BMP file";
        return false;
    }

    const unsigned long dhSize = get32( head + 14 );
    if( dhSize == 40UL )
    {
        if( got < sizeof head )
        {
            err = path + ": truncated Windows BMP header";
            return false;
        }
        w   = get32s( head + 18 );
        h   = get32s( head + 22 );
        bpp = (int)(unsigned short)( head[ 28 ] | ( head[ 29 ] << 8 ) );
    }
    else if( dhSize == 12UL )
    {
        // OS/2 BITMAPCOREHEADER: 12 bytes, and macOS writes this variant
        // for -t bmp -- BITMAPINFOHEADER fields are 16 bits wide, height
        // signed (negative for top-down rows).
        w   = (int)(unsigned short)( head[ 18 ] | ( head[ 19 ] << 8 ) );
        h   = (int)(short)( head[ 20 ] | ( head[ 21 ] << 8 ) );
        bpp = (int)(unsigned short)( head[ 24 ] | ( head[ 25 ] << 8 ) );
    }
    else
    {
        err = path + ": unsupported BMP header size";
        return false;
    }

    if( h < 0 )
        h = -h;                 // top-down rows: same picture, other order
    if( w <= 0 || h <= 0 )
    {
        err = path + ": empty bitmap";
        return false;
    }
    return true;
}

} // namespace atui::platform
} // namespace atui
