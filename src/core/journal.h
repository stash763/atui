// The undo journal: what was in a file before atui changed it.
//
// The journal exists for the crash, not for regret. atui spends its life blocked
// on a network stream, so the realistic failure is the process dying partway
// through an agent turn that has already rewritten two files - and an edit that
// cannot be undone at that point is a data-loss bug in atui rather than a mistake
// by the model. Hence a fixed order of operations, and no shortcuts in it:
//
//   1. copy the current contents into the journal, flush, close  (durable first)
//   2. write the new contents beside the target and replace it
//   3. mark the entry committed
//
// An entry that never got committed is not garbage, it is an interrupted write:
// atui finds it on the next start and offers to put the file back. Undo - the
// ordinary case of changing your mind - reads the same entries.
//
// Storage lives in the install directory, in names that are legal on FAT
// (`U000123.PRE` holds the previous bytes; `U000123.INF` holds a handful of
// `key=value` lines about them, readable with `type` or `more` on a machine with
// no tooling). Total size is capped and the oldest entries are dropped first,
// because "old system" is part of the target platform: the copy is streamed in a
// fixed buffer, so journaling a 5 MB file costs the same memory as journaling a
// 50 byte one.
//
// Durability, stated honestly: every write is flushed and closed, so an atui
// crash is survived. A power cut is not promiseable on platforms where the block
// cache decides when bytes reach the disk and no portable call flushes it.
//
// C++14. No TUI and no network dependency, so it is testable in both build
// configurations.
#ifndef ATUI_CORE_JOURNAL_H
#define ATUI_CORE_JOURNAL_H

#include <string>
#include <vector>

namespace atui {
namespace core {
namespace journal {

// --- configuration -------------------------------------------------------------
//
// Seeded from the environment, then overridden by [undo] in the config file.

bool         &enabled();          // default on: the net is the safe assumption
std::string  &directory();        // empty = <install dir>/undo
long         &maxBytes();         // whole-journal cap; oldest entries go first
long         &maxFileBytes();     // above this there is no copy, and edits ask

const long kMinMaxBytes      = 64 * 1024;
const long kMaxMaxBytes      = 64L * 1024 * 1024;
const long kDefaultMaxBytes  = 1024 * 1024;
const long kMinMaxFileBytes  = 1024;
const long kMaxMaxFileBytes  = 32L * 1024 * 1024;
const long kDefaultMaxFileBytes = 1024 * 1024;

long clampMaxBytes( long bytes );
long clampMaxFileBytes( long bytes );

// Where the entries are kept, resolved. Does not create anything.
std::string dir();

// Creates the journal directory if it is missing. Idempotent, and the first
// write is when that happens - starting atui should not leave a directory behind
// in an install tree nobody asked to modify. `error` says why it failed.
bool ensureDir( std::string &error );

// One line for the UI: where the journal is, what is in it, and when it cannot
// be trusted ("1 write interrupted").
std::string describe();

// A byte count the way a person reading a status line wants it ("512 B",
// "18 KiB", "1.4 MiB"). Exposed because the journal's sizes are the numbers the
// UI has to show next to the files.
std::string humanBytes( long bytes );

// The cheap question the permission policy asks before approving an in-tree
// write: would there be a copy to go back to? True when the journal is switched
// off - that is a decision the user already took, not a failure - and false with
// a reason when it is on and this file cannot be covered (too big, no writable
// journal).
bool canBackUp( const std::string &path, std::string &why );

// --- writing -------------------------------------------------------------------

// One watched write. Lives on the caller's stack between begin() and
// commit()/cancel().
struct Pending {
    std::string target;       // the file about to be written
    std::string preImage;     // the saved copy ("" when none was needed)
    std::string metaPath;     // its .INF companion
    std::string id;           // "U000123", the entry name without extension
    std::string hashBefore;
    long        bytesBefore;
    bool        existed;      // false = the file did not exist yet
    bool        overCap;      // too big to copy: see canBackUp()

    Pending() : bytesBefore( 0 ), existed( true ), overCap( false ) {}
};

// Step 1. Copies the current contents away and records the entry as pending.
// False means the write must not happen: `error` says why, and nothing has been
// touched. Returns true with `overCap` set when the file is too large to copy,
// which is the caller's cue to ask a person first (the policy does that in
// decide(); see tools::prNoUndoNet).
bool begin( const std::string &target, const std::string &tool,
            const std::string &op, Pending &out, std::string &error );

// Step 3. Marks the entry as a completed edit, recording what the file holds now
// so a later undo can tell "restore this" from "something else changed it".
// False leaves the entry pending - the safe state, since pending entries are
// offered for recovery - and `error` says what went wrong.
bool commit( const Pending &p, std::string &error );

// The write never happened, and the target was not touched: forget the entry.
void cancel( const Pending &p );

// --- reading -------------------------------------------------------------------

// One saved entry, newest first in every list the callers get.
struct Entry {
    std::string id;
    std::string target;
    std::string preImage;
    std::string metaPath;
    std::string stamp;        // when, as UTC text
    long        pid;          // which run made the change
    std::string tool;         // what made the change
    std::string op;           // "replace:1", "restore", ...
    std::string hashBefore;
    std::string hashAfter;    // what the file held when the edit finished
    long        bytes;        // size of the saved copy
    long        bytesAfter;
    bool        existed;      // false = the file did not exist before the edit
    bool        pending;      // committed never ran: an interrupted write

    Entry() : pid( 0 ), bytes( 0 ), bytesAfter( 0 ), existed( true ),
              pending( false ) {}
};

// Every entry in the journal, newest first.
void entries( std::vector<Entry> &out );

// Whether an entry could ever be restored. An interrupted write whose metadata
// never got written has bytes and no destination: it is listed, because it is
// somebody's file and it counts against the cap, but it is also the one kind of
// pending entry retention will delete (see prune()) - there is nothing it can be
// restored to.
bool restorable( const Entry &entry );

// The entries that matter most: writes that were interrupted.
void interrupted( std::vector<Entry> &out );

// Puts the saved contents back. Refuses when the file no longer holds what atui
// wrote - restoring over somebody else's edit is a worse bug than a stale undo
// entry - and in that case leaves the entry where it is and explains in `error`.
bool restore( const Entry &entry, std::string &error );

// Whether the entry is worth showing to anybody: "" when the file on disk still
// holds the bytes this entry would put back (so recovering it would change
// nothing), otherwise a sentence describing how the two differ, for the dialog
// that has to ask a person which one they want. Interrupted entries need this:
// with no "after" hash there is no way to tell a finished write from a file that
// somebody has since edited, and guessing is not a thing a recovery tool should
// do silently.
std::string compareTarget( const Entry &entry );

// Throws the entry away without restoring. The last resort, and only ever asked
// for explicitly.
bool discard( const Entry &entry, std::string &error );

// Drops the oldest committed entries until the journal fits its cap. Pending
// entries are never dropped: one of them may be the only copy of a file.
void prune();

} // namespace journal
} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_JOURNAL_H
