// The one image format every target can write from raw pixels: Windows BMP,
// uncompressed, no library.
//
// A screenshot backend's job ends at a rectangle of RGB bytes; how they reach
// a disk is the same arithmetic everywhere, and it is arithmetic worth testing
// once instead of per platform. 24-bit BI_RGB is what gets written because it
// is the format every reader in the world (and every OS/2 viewer) opens; the
// reader here also understands the older 12-byte OS/2 COREHEADER, because
// macOS's screencapture -t bmp writes that variant and a tool that cannot
// confirm the file it just produced has no business claiming success.
//
// C++98, standard library only: this file is compiled by Watcom 1.9 as well
// as by the host compilers.
#ifndef ATUI_PLATFORM_BMPOUT_H
#define ATUI_PLATFORM_BMPOUT_H

#include <string>

namespace atui {
namespace platform {

// Writes `rgb` -- w*h pixels, top-down, three bytes per pixel (R,G,B) -- to
// `path` as an uncompressed 24-bit BMP. Returns false and fills `err` when
// the file cannot be written.
bool writeBmp24( const std::string &path, const unsigned char *rgb,
                 int w, int h, std::string &err );

// Reads a BMP header from `path` -- either the 40-byte Windows
// BITMAPINFOHEADER or the 12-byte OS/2 BITMAPCOREHEADER style -- and reports
// the pixel dimensions and bit depth. Negative heights (top-down rows) are
// reported as their magnitude: callers of this function ask how big the
// picture is, not which way the rows run.
bool bmpHeaderInfo( const std::string &path, int &w, int &h, int &bpp,
                    std::string &err );

} // namespace atui::platform
} // namespace atui

#endif // ATUI_PLATFORM_BMPOUT_H
