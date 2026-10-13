// Implementation of the undo journal. See journal.h.
#include "journal.h"

#include "core/paths.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#include <io.h>
#include <sys/types.h>
#define ATUI_PID() ((long)_getpid())
#else
#include <unistd.h>
#define ATUI_PID() ((long)::getpid())
#endif

#if defined(_MSC_VER)
#include <direct.h>
#include <io.h>
#else
#include <dirent.h>
#endif

namespace atui {
namespace core {
namespace journal {

namespace {

// The copy buffer. Every file operation here is streamed through it, which is
// what keeps journaling a 5 MB file and a 50 byte file identical in memory on a
// machine where 64 KB is a large allocation.
const std::size_t kChunk = 4096;

// The entry name pattern. Eight-character-plus-three names because the journal
// has to survive FAT: `U000123.PRE`, `U000123.INF`.
const char *kPreExt = ".PRE";
const char *kInfExt = ".INF";

std::mutex &cs()
{
    static std::mutex m;
    return m;
}

// --- small facts ---------------------------------------------------------------

// FNV-1a, 64-bit (offset basis 14695981039346656037, prime 1099511628211). This
// is a "has the file changed underneath me" test and is not security-relevant,
// which is why it can be eight lines of arithmetic rather than a dependency.
inline unsigned long long fnvInit() { return 14695981039346656037ULL; }
inline void fnvByte( unsigned long long &h, char c )
{
    h ^= (unsigned char) c;
    h *= 1099511628211ULL;
}

std::string hex( unsigned long long v )
{
    char buf[ 24 ];
    std::snprintf( buf, sizeof buf, "%016llx", v );
    return std::string( buf );
}

std::string stamp()
{
    char buf[ 32 ];
    const std::time_t now = std::time( 0 );
    std::tm utc;
#if !defined(_WIN32)
    ::gmtime_r( &now, &utc );
#else
    utc = *::gmtime( &now );
#endif
    std::strftime( buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &utc );
    return std::string( buf );
}

bool fileExists( const std::string &path )
{
    return fileSize( path ) >= 0;
}

std::string longStr( long v )
{
    char buf[ 24 ];
    std::snprintf( buf, sizeof buf, "%ld", v );
    return std::string( buf );
}

// Values in a .INF are single-line and contain no quote games: paths with
// newlines are not supported, and saying so in one place is better than a parser
// that mis-reads them later.
std::string oneLine( const std::string &s )
{
    std::string out;
    for( std::size_t i = 0; i < s.size(); ++i )
    {
        const char c = s[i];
        if( c == '\n' || c == '\r' ) out += ' ';
        else out += c;
    }
    return out;
}

// --- copying and hashing -------------------------------------------------------

// Copies `src` to `dst`, hashing as it goes so the caller does not have to read
// the file twice. Returns false if either end failed; a partial `dst` is removed,
// because a half-written pre-image is worse than none - it looks like a copy.
bool copyFile( const std::string &src, const std::string &dst,
               std::string &hash, long &bytes, std::string &error )
{
    std::ifstream in( src.c_str(), std::ios::in | std::ios::binary );
    if( !in ) { error = "cannot read '" + src + "'"; return false; }

    std::FILE *out = std::fopen( dst.c_str(), "wb" );
    if( !out )
    {
        error = "cannot create '" + dst + "'";
        return false;
    }

    char buf[ kChunk ];
    unsigned long long h = fnvInit();
    long total = 0;
    bool ok = true;
    for( ;; )
    {
        in.read( buf, (std::streamsize) sizeof buf );
        const std::streamsize got = in.gcount();
        if( got <= 0 ) break;
        for( std::streamsize i = 0; i < got; ++i ) fnvByte( h, buf[i] );
        if( std::fwrite( buf, 1, (std::size_t) got, out ) != (std::size_t) got )
        {
            ok = false;
            break;
        }
        total += (long) got;
        if( got < (std::streamsize) sizeof buf ) break;
    }
    in.close();

    // Flushed and closed before anything else happens: the whole point of the
    // ordering is that the copy is on disk before the original is disturbed.
    if( std::fflush( out ) != 0 ) ok = false;
    if( std::fclose( out ) != 0 ) ok = false;

    if( !ok )
    {
        std::remove( dst.c_str() );
        error = "could not write the whole copy of '" + src + "'";
        return false;
    }
    hash = hex( h );
    bytes = total;
    return true;
}

// Hash of an existing file, or "" when it cannot be read.
std::string hashFile( const std::string &path )
{
    std::ifstream in( path.c_str(), std::ios::in | std::ios::binary );
    if( !in ) return std::string();
    char buf[ kChunk ];
    unsigned long long h = fnvInit();
    for( ;; )
    {
        in.read( buf, (std::streamsize) sizeof buf );
        const std::streamsize got = in.gcount();
        if( got <= 0 ) break;
        for( std::streamsize i = 0; i < got; ++i ) fnvByte( h, buf[i] );
        if( got < (std::streamsize) sizeof buf ) break;
    }
    return hex( h );
}

// --- the .INF sidecar ----------------------------------------------------------

// Writes (or rewrites) an entry's metadata. `state` is "pending" until commit
// and "saved" after it; anything still pending when atui starts is an
// interrupted write.
bool writeInf( const Entry &e, const std::string &state, std::string &error )
{
    std::ostringstream s;
    s << "atui-undo v1\n"
      << "stamp=" << oneLine( e.stamp ) << "\n"
      << "pid=" << longStr( e.pid ) << "\n"
      << "tool=" << oneLine( e.tool ) << "\n"
      << "op=" << oneLine( e.op ) << "\n"
      << "state=" << state << "\n"
      << "existed=" << ( e.existed ? 1 : 0 ) << "\n"
      << "target=" << oneLine( e.target ) << "\n"
      << "hash=" << e.hashBefore << "\n"
      << "bytes=" << longStr( e.bytes ) << "\n"
      << "after=" << e.hashAfter << "\n"
      << "afterbytes=" << longStr( e.bytesAfter ) << "\n";

    std::FILE *f = std::fopen( e.metaPath.c_str(), "wb" );
    if( !f ) { error = "cannot write '" + e.metaPath + "'"; return false; }
    const std::string text = s.str();
    bool ok = ( std::fwrite( text.data(), 1, text.size(), f ) == text.size() );
    // Flushed: an entry that only exists in the C stdio buffer is an entry that
    // disappears with the process, and the entry is the whole reason to have a
    // journal.
    if( std::fflush( f ) != 0 ) ok = false;
    if( std::fclose( f ) != 0 ) ok = false;
    if( !ok )
    {
        std::remove( e.metaPath.c_str() );
        error = "cannot write '" + e.metaPath + "'";
        return false;
    }
    return true;
}

std::string valueOf( const std::string &line )
{
    // Split on the FIRST '=': a path may contain any number of them.
    const std::string::size_type eq = line.find( '=' );
    if( eq == std::string::npos ) return std::string();
    return line.substr( eq + 1 );
}

bool readInf( const std::string &metaPath, Entry &out )
{
    std::ifstream f( metaPath.c_str(), std::ios::in );
    if( !f ) return false;

    std::string line;
    bool sawHeader = false;
    while( std::getline( f, line ) )
    {
        if( line.compare( 0, 4, "atui" ) == 0 ) { sawHeader = true; continue; }
        const std::string key   = line.substr( 0, line.find( '=' ) );
        const std::string value = valueOf( line );
        if( key == "stamp" )        out.stamp = value;
        else if( key == "tool" )    out.tool = value;
        else if( key == "op" )      out.op = value;
        else if( key == "target" )  out.target = value;
        else if( key == "hash" )    out.hashBefore = value;
        else if( key == "after" )   out.hashAfter = value;
        else if( key == "bytes" )   out.bytes = std::atol( value.c_str() );
        else if( key == "pid" )     out.pid = std::atol( value.c_str() );
        else if( key == "state" )   out.pending = ( value != "saved" );
        else if( key == "existed" ) out.existed = ( value == "1" );
    }
    // A .INF from a version that wrote no header is still readable; a file whose
    // first line is something else is not an entry at all, and listing it as one
    // would offer nonsense in the undo menu.
    out.metaPath = metaPath;
    return sawHeader;
}

// --- listing -------------------------------------------------------------------

void listDir( const std::string &dir, std::vector< std::string > &out )
{
#if defined(_MSC_VER)
    struct _finddata_t fd;
    long h = _findfirst( joinDir( dir, "*" ).c_str(), &fd );
    if( h == -1L ) return;
    do
    {
        if( !( fd.attrib & _A_SUBDIR ) ) out.push_back( std::string( fd.name ) );
    } while( _findnext( h, &fd ) == 0 );
    _findclose( h );
#else
    // POSIX, and the DOS/OS2 toolchains atui builds for ship dirent.h with it.
    DIR *d = ::opendir( dir.c_str() );
    if( !d ) return;
    struct dirent *e;
    while( ( e = ::readdir( d ) ) != 0 )
        out.push_back( std::string( e->d_name ) );
    ::closedir( d );
#endif
}

bool endsWith( const std::string &s, const char *suffix )
{
    const std::size_t n = std::strlen( suffix );
    if( s.size() < n ) return false;
    for( std::size_t i = 0; i < n; ++i )
        if( std::toupper( (unsigned char) s[s.size() - n + i] )
            != std::toupper( (unsigned char) suffix[i] ) ) return false;
    return true;
}

bool startsWith( const std::string &s, const char *prefix )
{
    const std::size_t n = std::strlen( prefix );
    return s.size() >= n && s.compare( 0, n, prefix ) == 0;
}

// "U000123" from a file name, or "" if this is not one of ours.
std::string idOf( const std::string &name, const char *ext )
{
    if( !endsWith( name, ext ) ) return std::string();
    if( !startsWith( name, "U" ) ) return std::string();
    return name.substr( 0, name.size() - std::strlen( ext ) );
}

// Newest first means the numeric part decides; ids are fixed width so text and
// numeric order agree.
bool newerFirst( const Entry &a, const Entry &b )
{
    return a.id > b.id;
}

std::string makeId( long seq )
{
    char buf[ 16 ];
    std::snprintf( buf, sizeof buf, "U%06ld", seq % 1000000L );
    return std::string( buf );
}

// The next free sequence number: the highest id present, plus one. The directory
// is bounded by maxBytes, so this scan is short, and it avoids a counter file
// that could itself be lost or corrupted.
long nextSequence()
{
    std::vector< std::string > names;
    listDir( dir(), names );
    long high = 0;
    for( std::size_t i = 0; i < names.size(); ++i )
    {
        std::string id = idOf( names[i], kInfExt );
        if( id.empty() ) id = idOf( names[i], kPreExt );
        if( id.empty() || id.size() < 7 ) continue;
        const long n = std::atol( id.c_str() + 1 );
        if( n > high ) high = n;
    }
    return high + 1;
}

std::string entryPath( const std::string &id, const char *ext )
{
    return joinDir( dir(), id + ext );
}

void removeEntryFiles( const Entry &e )
{
    if( !e.preImage.empty() ) std::remove( e.preImage.c_str() );
    if( !e.metaPath.empty() ) std::remove( e.metaPath.c_str() );
}

void readEntries( std::vector<Entry> &out )
{
    out.clear();
    const std::string d = dir();
    if( !isDirectory( d ) ) return;

    std::vector< std::string > names;
    listDir( d, names );
    for( std::size_t i = 0; i < names.size(); ++i )
    {
        const std::string id = idOf( names[i], kInfExt );
        if( id.empty() ) continue;

        Entry e;
        e.id = id;
        e.preImage = entryPath( id, kPreExt );
        if( !readInf( joinDir( d, names[i] ), e ) ) continue;
        if( !fileExists( e.preImage ) )
        {
            // Metadata without a copy: nothing to restore, so nothing to offer.
            continue;
        }
        out.push_back( e );
    }

    // A .PRE with no .INF is an interrupted write that died before its target
    // was recorded. It cannot be restored to anything, but it is somebody's
    // bytes and it counts against the cap, so it is listed - with no target,
    // which restore() and the UI both handle by refusing.
    for( std::size_t i = 0; i < names.size(); ++i )
    {
        const std::string id = idOf( names[i], kPreExt );
        if( id.empty() ) continue;
        bool hasMeta = false;
        for( std::size_t j = 0; j < out.size(); ++j )
            if( out[j].id == id ) { hasMeta = true; break; }
        if( hasMeta ) continue;

        Entry e;
        e.id = id;
        e.preImage = entryPath( id, kPreExt );
        e.metaPath = entryPath( id, kInfExt );
        e.target = "(unknown)";
        e.tool = "?";
        e.op = "unrecorded";
        e.bytes = fileSize( e.preImage );
        e.stamp = "?";
        e.pending = true;
        out.push_back( e );
    }

    std::sort( out.begin(), out.end(), newerFirst );
}

long totalBytes( const std::vector<Entry> &list )
{
    long total = 0;
    for( std::size_t i = 0; i < list.size(); ++i ) total += list[i].bytes;
    return total;
}

// --- environment seeds ---------------------------------------------------------
//
// The same rule as the permission settings: the environment provides the initial
// value and the config file overrides it, so a container or a dotfile can set the
// journal without editing anybody's INI.

std::string envStr( const char *name )
{
    const char *v = std::getenv( name );
    return ( v && *v ) ? std::string( v ) : std::string();
}

bool envFlag( const char *name, bool dflt )
{
    const char *v = std::getenv( name );
    if( !v || !*v ) return dflt;
    // Only an explicit no switches this off. A mistyped value must not quietly
    // remove the safety net.
    std::string s( v );
    for( std::size_t i = 0; i < s.size(); ++i )
        s[i] = (char) std::tolower( (unsigned char) s[i] );
    return !( s == "0" || s == "off" || s == "no" || s == "false" );
}

long envLong( const char *name, long dflt )
{
    const char *v = std::getenv( name );
    if( !v || !*v ) return dflt;
    char *end = 0;
    const long n = std::strtol( v, &end, 10 );
    return ( end != v ) ? n : dflt;
}

} // anonymous namespace

// --- configuration -------------------------------------------------------------

bool &enabled()
{
    static bool on = envFlag( "ATUI_UNDO", true );
    return on;
}

std::string &directory()
{
    static std::string d = envStr( "ATUI_UNDO_DIR" );
    return d;
}

long &maxBytes()
{
    static long b = clampMaxBytes(
        envLong( "ATUI_UNDO_MAX_BYTES", kDefaultMaxBytes ) );
    return b;
}

long &maxFileBytes()
{
    static long b = clampMaxFileBytes(
        envLong( "ATUI_UNDO_MAX_FILE", kDefaultMaxFileBytes ) );
    return b;
}

long clampMaxBytes( long bytes )
{
    if( bytes < kMinMaxBytes ) return kMinMaxBytes;
    if( bytes > kMaxMaxBytes ) return kMaxMaxBytes;
    return bytes;
}

long clampMaxFileBytes( long bytes )
{
    if( bytes < kMinMaxFileBytes ) return kMinMaxFileBytes;
    if( bytes > kMaxMaxFileBytes ) return kMaxMaxFileBytes;
    return bytes;
}

std::string humanBytes( long bytes )
{
    char buf[ 32 ];
    if( bytes >= 1024 * 1024 )
        std::snprintf( buf, sizeof buf, "%.1f MiB", bytes / (1024.0 * 1024.0) );
    else if( bytes >= 1024 )
        std::snprintf( buf, sizeof buf, "%ld KiB", bytes / 1024 );
    else
        std::snprintf( buf, sizeof buf, "%ld B", bytes );
    return std::string( buf );
}

std::string dir()
{
    if( !directory().empty() ) return directory();
    return joinDir( installDir(), "undo" );
}

bool ensureDir( std::string &error )
{
    const std::string d = dir();
    if( isDirectory( d ) ) return true;
    if( makeDirectory( d ) ) return true;
    // Creating it can fail because a parent is missing (a relative install
    // directory seen from another cwd) - one retry after making the parent is
    // worth it, and after that it is genuinely not writable.
    error = "cannot create the undo journal at '" + d + "'";
    return false;
}

std::string describe()
{
    if( !enabled() ) return "the undo journal is switched off";
    std::vector<Entry> list;
    {
        std::lock_guard< std::mutex > lock( cs() );
        readEntries( list );
    }
    long pend = 0;
    for( std::size_t i = 0; i < list.size(); ++i )
        if( list[i].pending ) ++pend;

    std::ostringstream s;
    s << dir() << " - " << list.size() << " saved, "
      << humanBytes( totalBytes( list ) ) << " of " << humanBytes( maxBytes() );
    // Worth saying out of the others: it means a previous run was caught writing
    // a file, and the copy is still here.
    if( pend ) s << ", " << pend << " interrupted";
    return s.str();
}

bool canBackUp( const std::string &path, std::string &why )
{
    if( !enabled() ) return true;   // no journal is a decision, not a failure

    // Resolved exactly as the write will be: "which file is this" has to be
    // answered before the size question, and a relative path means a different
    // file in a different directory.
    const std::string abs = absolutePath( path );
    const long size = fileSize( abs );
    if( size > maxFileBytes() )
    {
        why = longStr( size ) + " bytes, above the "
              + longStr( maxFileBytes() ) + " byte undo limit";
        return false;
    }
    std::string err;
    if( !ensureDir( err ) )
    {
        // The one place a user of a read-only install finds out that atui needs a
        // writable directory of its own, so it has to say what to do about it -
        // not just that something failed.
        why = err + "; point [undo] dir (or ATUI_UNDO_DIR) at a writable "
                     "directory, or set [undo] enabled = false";
        return false;
    }
    if( !isWritableDir( dir() ) )
    {
        // Only a reason if the create succeeded and the check says no; on FAT
        // this is coarse, and the actual failure will surface from begin().
        why = "'" + dir() + "' is not writable";
        return false;
    }
    return true;
}

// --- writing -------------------------------------------------------------------

bool begin( const std::string &target, const std::string &tool,
            const std::string &op, Pending &out, std::string &error )
{
    out = Pending();
    // The record names the file absolutely: an entry read after a restart, or
    // from another directory, has to say where the copy belongs.
    out.target = absolutePath( target );
    if( !enabled() ) return true;

    const long size = fileSize( out.target );
    out.existed = ( size >= 0 );
    if( out.existed && size > maxFileBytes() )
    {
        // Too big to copy. The caller has already been told by the policy and a
        // person has said yes, so proceed with no net and record that fact.
        out.overCap = true;
        return true;
    }

    std::lock_guard< std::mutex > lock( cs() );
    if( !ensureDir( error ) ) return false;

    out.id = makeId( nextSequence() );
    out.preImage = entryPath( out.id, kPreExt );
    out.metaPath = entryPath( out.id, kInfExt );

    if( out.existed )
    {
        if( !copyFile( out.target, out.preImage, out.hashBefore, out.bytesBefore,
                       error ) )
        {
            out.preImage.clear();
            return false;
        }
    }
    else
    {
        // "The file did not exist" is itself a state to restore to, and an empty
        // pre-image says exactly that.
        std::FILE *f = std::fopen( out.preImage.c_str(), "wb" );
        if( !f ) { error = "cannot create '" + out.preImage + "'"; return false; }
        std::fclose( f );
    }

    Entry e;
    e.id = out.id;
    e.stamp = stamp();
    e.pid = ATUI_PID();
    e.tool = tool;
    e.op = op;
    e.target = out.target;
    e.preImage = out.preImage;
    e.metaPath = out.metaPath;
    e.hashBefore = out.hashBefore;
    e.bytes = out.bytesBefore;
    e.existed = out.existed;
    // Pending until commit: if atui dies between here and the file being
    // rewritten, this entry is how the next run knows.
    if( !writeInf( e, "pending", error ) )
    {
        std::remove( out.preImage.c_str() );
        out.preImage.clear();
        return false;
    }
    return true;
}

bool commit( const Pending &p, std::string &error )
{
    if( !enabled() || p.id.empty() ) return true;

    std::lock_guard< std::mutex > lock( cs() );

    // Read the entry begin() wrote, so the tool, the operation and the timestamp
    // in the record are the ones captured when the change was made rather than
    // reconstructed here.
    Entry e;
    e.id = p.id;
    e.preImage = p.preImage;
    if( !readInf( p.metaPath, e ) )
    {
        error = "the undo entry for '" + p.target + "' has gone missing";
        return false;
    }
    e.hashAfter = hashFile( p.target );
    e.bytesAfter = fileSize( p.target );

    if( !writeInf( e, "saved", error ) ) return false;
    prune();
    return true;
}

void cancel( const Pending &p )
{
    if( !enabled() || p.id.empty() ) return;
    std::lock_guard< std::mutex > lock( cs() );
    Entry e;
    e.preImage = p.preImage;
    e.metaPath = p.metaPath;
    removeEntryFiles( e );
}

// --- reading -------------------------------------------------------------------

void entries( std::vector<Entry> &out )
{
    std::lock_guard< std::mutex > lock( cs() );
    readEntries( out );
}

void interrupted( std::vector<Entry> &out )
{
    std::vector<Entry> all;
    {
        std::lock_guard< std::mutex > lock( cs() );
        readEntries( all );
    }
    out.clear();
    for( std::size_t i = 0; i < all.size(); ++i )
        if( all[i].pending ) out.push_back( all[i] );
}

std::string compareTarget( const Entry &entry )
{
    // Every answer below is a predicate phrase: the caller puts "That file " in
    // front of it, and a dialog reads out the result.
    if( !restorable( entry ) )
        return "was never recorded as belonging to a file";

    const long now = fileSize( entry.target );
    if( now < 0 )
    {
        // Not there before atui wrote it, not there now: the saved copy is an
        // empty one and there is nothing to put back.
        if( !entry.existed ) return std::string();
        return "is not there at all any more";
    }

    const std::string h = hashFile( entry.target );
    if( !entry.hashBefore.empty() && h == entry.hashBefore )
        return std::string();   // the write never took effect: nothing to recover

    if( !entry.hashAfter.empty() && h == entry.hashAfter )
        return "holds what atui wrote (" + longStr( now ) + " bytes)";
    if( !entry.hashAfter.empty() )
        return "has been changed since atui wrote it (now " + longStr( now )
               + " bytes)";

    // An interrupted write has no "after" hash, so the bytes on disk cannot be
    // placed: they may be the ones atui was writing or the ones somebody typed
    // afterwards. Saying so is the whole point - a recovery tool that guesses
    // loses files.
    return "holds " + longStr( now ) + " bytes atui cannot account for (the "
           "saved copy holds " + longStr( entry.bytes ) + ")";
}

bool restorable( const Entry &entry )
{
    // "(unknown)" is what readEntries() writes for a copy whose .INF never
    // existed. One place sets it and one place reads it, which is the whole
    // reason this is a function rather than a test at each call site.
    return !entry.target.empty() && entry.target != "(unknown)";
}

bool restore( const Entry &entry, std::string &error )
{
    if( !restorable( entry ) )
    {
        error = "this copy has no recorded file to put it back in";
        return false;
    }
    if( !fileExists( entry.preImage ) )
    {
        error = "the copy is gone: '" + entry.preImage + "'";
        return false;
    }

    std::lock_guard< std::mutex > lock( cs() );

    // Restore over a file that someone else changed since atui wrote it would
    // destroy work that has nothing to do with this undo. Refuse, keep the
    // entry, and let the person decide with both files in front of them. The same
    // guard covers a file atui created: putting that entry back means deleting
    // the file, so it gets the same "has anybody touched this" test first.
    if( !entry.hashAfter.empty() && fileExists( entry.target ) )
    {
        const std::string now = hashFile( entry.target );
        if( !now.empty() && now != entry.hashAfter )
        {
            error = "'" + entry.target + "' has changed since atui wrote it; the "
                    "saved copy is at '" + entry.preImage + "'";
            return false;
        }
    }

    if( !entry.existed )
    {
        // Before atui there was no file, so putting it back means taking it away.
        if( fileExists( entry.target ) && std::remove( entry.target.c_str() ) != 0 )
        {
            error = "cannot remove '" + entry.target + "'";
            return false;
        }
        removeEntryFiles( entry );
        return true;
    }

    std::string hash;
    long bytes = 0;
    std::string copyError;
    if( !copyFile( entry.preImage, entry.target, hash, bytes, copyError ) )
    {
        error = "could not restore '" + entry.target + "': " + copyError;
        return false;   // entry kept: the copy is still there
    }
    removeEntryFiles( entry );
    return true;
}

bool discard( const Entry &entry, std::string &error )
{
    std::lock_guard< std::mutex > lock( cs() );
    removeEntryFiles( entry );
    if( fileExists( entry.preImage ) || fileExists( entry.metaPath ) )
    {
        error = "cannot delete the saved copy at '" + entry.preImage + "'";
        return false;
    }
    return true;
}

void prune()
{
    std::vector<Entry> list;
    readEntries( list );
    long total = totalBytes( list );
    if( total <= maxBytes() ) return;

    // How many entries could still be restored. At least one of those survives
    // whatever the cap says, or the undo box opens with nothing in it.
    long keep = 0;
    for( std::size_t i = 0; i < list.size(); ++i )
        if( restorable( list[i] ) ) ++keep;

    // Oldest first: readEntries sorts newest first, so this walks backwards.
    for( std::size_t i = list.size(); i > 0; --i )
    {
        if( total <= maxBytes() ) break;
        const Entry &e = list[i - 1];

        if( restorable( e ) )
        {
            // A pending copy may be the only version of a file that was being
            // written when atui died, so it is not for sale...
            if( e.pending ) continue;
            // ...and one saved copy is always kept: the change made a moment ago
            // is the one that is about to be undone, and a cap that ate it would
            // be a cap that ate the feature. describe() reports the overage
            // rather than hiding it.
            if( keep <= 1 ) break;
            --keep;
        }
        // A copy that cannot be restored to anything goes on cap alone: it is
        // bytes with no destination, kept only because the journal has room.
        total -= e.bytes;
        removeEntryFiles( e );
    }
}

} // namespace journal
} // namespace atui
} // namespace atui
