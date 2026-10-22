#include "tui/ascii_fold.h"

#include <cstdlib>

namespace atui {
namespace tui {

namespace {

struct Fold {
    unsigned long cp;
    const char   *text;
};

// The characters a model actually writes that are not ASCII, in the order they were
// added: typography first (a conversation is mostly this), then the graphics a shell
// command produces (progress bars, spinners, tables). Anything not here and not covered
// by a rule below becomes "?".
Fold kFolds[] = {
    { 0x00A0, " "  },   // no-break space
    { 0x2002, " "  }, { 0x2003, " "  }, { 0x2004, " "  }, { 0x2005, " "  },
    { 0x2006, " "  }, { 0x2007, " "  }, { 0x2008, " "  }, { 0x2009, " "  },
    { 0x200A, " "  }, { 0x202F, " "  }, { 0x205F, " "  }, { 0x3000, " "  },
    { 0x2018, "'"  }, { 0x2019, "'"  }, { 0x201A, "'"  }, { 0x201B, "'"  },
    { 0x201C, "\"" }, { 0x201D, "\"" }, { 0x201E, "\"" }, { 0x201F, "\"" },
    { 0x00AB, "<"  }, { 0x00BB, ">"  }, { 0x2039, "<"  }, { 0x203A, ">"  },
    { 0x2013, "-"  }, { 0x2014, "-"  }, { 0x2015, "-"  }, { 0x2212, "-"  },
    { 0x2010, "-"  }, { 0x2011, "-"  }, { 0x00AD, ""   },   // soft hyphen
    { 0x2026, "..." },
    { 0x2022, "*"  }, { 0x2023, "*"  }, { 0x2043, "*"  }, { 0x00B7, "*"  },
    { 0x25CF, "*"  }, { 0x25AA, "*"  }, { 0x25E6, "o"  },
    { 0x2192, "->" }, { 0x21D2, "=>" }, { 0x2190, "<-" }, { 0x2194, "<->" },
    { 0x2191, "^"  }, { 0x2193, "v"  },
    { 0x2713, "ok" }, { 0x2714, "ok" }, { 0x2717, "X"  }, { 0x2718, "X"  },
    { 0x00A9, "(c)" }, { 0x00AE, "(r)" }, { 0x2122, "(tm)" },
    { 0x00BC, "1/2" }, { 0x00BD, "1/4" }, { 0x00BE, "3/4" }, { 0x00B0, "deg" },
    { 0x20AC, "EUR" }, { 0x00A3, "GBP" }, { 0x00A5, "JPY" }
};

// Counted from the table rather than written out, so a row added here cannot be left
// outside the loop that reads it.
const int kFoldsN = (int) ( sizeof( kFolds ) / sizeof( kFolds[ 0 ] ) );

// U+00C0..U+00FF, in order. Accented letters lose their accents rather than becoming
// "?", because they are almost always a person's name or a place, and "Jose" reads
// while "Jos?" does not.
const char *kLatin1[ 64 ] = {
    "A","A","A","A","A","A","AE","C",  "E","E","E","E","I","I","I","I",
    "D","N","O","O","O","O","O","x",   "O","U","U","U","U","Y","TH","ss",
    "a","a","a","a","a","a","ae","c",  "e","e","e","e","i","i","i","i",
    "d","n","o","o","o","o","o","/",   "o","u","u","u","u","y","th","y"
};

const char *lookupFold( unsigned long cp )
{
    for( int i = 0; i < kFoldsN; ++i )
        if( kFolds[ i ].cp == cp )
            return kFolds[ i ].text;
    if( cp >= 0x00C0 && cp <= 0x00FF )
        return kLatin1[ cp - 0x00C0 ];
    return 0;
}

// The ranges, for the things a terminal draws rather than writes.
const char *ruleFold( unsigned long cp )
{
    if( cp >= 0x0300 && cp <= 0x036F ) return "";       // combining marks
    if( cp >= 0x2500 && cp <= 0x257F )                  // box drawing
    {
        if( cp == 0x2502 || cp == 0x2503 || cp == 0x250A || cp == 0x250D ) return "|";
        if( cp == 0x2500 || cp == 0x2501 || cp == 0x2508 || cp == 0x250B ) return "-";
        return "+";
    }
    if( cp >= 0x2580 && cp <= 0x259F ) return "#";      // block shades (progress bars)
    if( cp >= 0x25A0 && cp <= 0x25FF ) return "#";      // geometric shapes
    if( cp >= 0x2800 && cp <= 0x28FF ) return ".";      // braille: spinners
    if( cp >= 0xE000 && cp <= 0xF8FF ) return "";       // private use: nothing to say
    return 0;
}

void emitCodePoint( unsigned long cp, std::string &out )
{
    const char *fold = lookupFold( cp );
    if( fold == 0 )
        fold = ruleFold( cp );
    if( fold == 0 )
    {
        out += '?';
        return;
    }
    for( const char *p = fold; *p != '\0'; ++p )
        out += *p;
}

inline bool isCont( unsigned char c )
{
    return ( c & 0xC0 ) == 0x80;
}

} // anonymous namespace

bool foldTerminalText()
{
    // ATUI_FOLD overrides the platform's answer: 1/y turn folding on, 0/n turn it off.
    // It is here because the fold is a property of the terminal in front of the program
    // rather than of the machine it was compiled for -- a CP850 window on any platform
    // wants folding, a UTF-8 OS/2 session does not -- and because being able to turn it
    // on in front of a developer is how this gets tested outside OS/2.
    //
    // Read every call rather than once: this is asked a few dozen times a second at the
    // very most, next to a std::string allocation, and a cached copy would make the
    // variable a thing you have to set before starting the program.
    const char *env = std::getenv( "ATUI_FOLD" );
    while( env != 0 && ( *env == ' ' || *env == '\t' ) )
        ++env;
    if( env != 0 && *env != '\0' )
        return *env == '1' || *env == 'y' || *env == 'Y';

#if defined( __OS2__ ) || defined( __DOS__ ) || defined( __DPMI__ )
    return true;
#else
    return false;
#endif
}

std::string foldToAscii( const std::string &in )
{
    std::string out;
    out.reserve( in.size() );

    const size_t n = in.size();
    size_t i = 0;
    while( i < n )
    {
        const unsigned char c = (unsigned char) in[ i ];

        if( c < 0x80 )
        {
            // Tab and newline are layout, not text. Everything else below 0x20 has no
            // printable meaning and some of it is terminal control, and 0x7F is DEL:
            // those are dropped rather than turned into "?", because a "?" is text and
            // they are not.
            if( c == '\t' || c == '\n' || ( c >= 0x20 && c != 0x7F ) )
                out += (char) c;
            ++i;
            continue;
        }

        // How many bytes this lead byte claims, and what it would have to be.
        size_t   want = 0;
        unsigned long cp = 0;
        if( ( c & 0xE0 ) == 0xC0 )      { want = 1; cp = c & 0x1Fu; }
        else if( ( c & 0xF0 ) == 0xE0 ) { want = 2; cp = c & 0x0Fu; }
        else if( ( c & 0xF8 ) == 0xF0 ) { want = 3; cp = c & 0x07u; }
        else
        {
            // Not a lead byte at all: a stray continuation byte, or 0xFF. Not UTF-8,
            // and this string may not be UTF-8 either -- pass it through rather than
            // destroy bytes the user typed on an 8-bit keyboard.
            out += (char) c;
            ++i;
            continue;
        }

        if( i + want >= n )
        {
            out += (char) c;            // truncated sequence: same reasoning
            ++i;
            continue;
        }
        bool bad = false;
        for( size_t k = 1; k <= want; ++k )
        {
            const unsigned char cc = (unsigned char) in[ i + k ];
            if( !isCont( cc ) ) { bad = true; break; }
            cp = ( cp << 6 ) | ( cc & 0x3Fu );
        }
        // Reject overlong forms: they decode to a value that a shorter sequence spells,
        // and accepting them would let a byte sequence hide behind this function.
        if( !bad && want == 1 && cp < 0x80u )   bad = true;
        if( !bad && want == 2 && cp < 0x800u )  bad = true;
        if( !bad && want == 3 && cp < 0x10000u ) bad = true;
        if( bad )
        {
            out += (char) c;
            ++i;
            continue;
        }

        emitCodePoint( cp, out );
        i += want + 1;
    }

    return out;
}

std::string foldForTerminal( const std::string &in )
{
    if( !foldTerminalText() )
        return in;
    return foldToAscii( in );
}

} // namespace tui
} // namespace atui
