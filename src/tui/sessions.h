// Sessions: conversations a person can leave and come back to.
//
// A session is the pair that makes "the other conversation" mean
// something: the transcript's blocks (what the screen shows) and the
// model-side conversation (what the next request will carry, held by
// a ChatTurn). They switch together and never separately, or the
// screen and the model start disagreeing about what was said.
//
// The store keeps titles, blocks, and the active seat. It deliberately
// knows nothing about turns as objects: Session::turn is a pointer to
// an incomplete type, held but never followed or freed here. The app
// creates a turn on the session's first message and frees it when a
// session dies or the app does -- the delete lives where ChatTurn's
// complete type does, which for the no-transport build is nowhere,
// and both compile.
//
// The store also never empties: delete the last session and its seat
// resets to a fresh one. An app with no conversation to type into is
// a dead end the user did not ask for.
//
// C++98, standard library only.
#ifndef ATUI_TUI_SESSIONS_H
#define ATUI_TUI_SESSIONS_H

#include <string>
#include <vector>

namespace atui {
namespace net { class ChatTurn; }   // incomplete here on purpose; the app owns it

namespace tui {

// One transcript record. The vocabulary of the TranscriptView -- what
// a conversation is made of once it has been said -- lives here
// because the session store carries these between switches, and the
// view renders them; two owners, one truth.
enum Kind { kUser, kAnswer, kThinking, kStatus, kTool };

struct Block
{
    int kind;
    std::string text;    // user text / answer / thinking / tool preview
    std::string out;     // tool blocks: what the tool returned
    std::string note;    // tool blocks: who allowed it and why, if anything
    bool expanded;
    bool open;

    Block() : kind( 0 ), expanded( false ), open( false ) {}
};

struct Session
{
    std::string title;           // what the list shows
    std::string autoTitle;       // the generated title; title == autoTitle
                                 // means the conversation has not earned a
                                 // better one yet
    std::vector<Block> blocks;   // the transcript, as the view last held it

    // The model-side conversation. Owned by the app: created when the
    // session gets its first message, freed when the session is deleted
    // and when the app shuts down. Null until then.
    atui::net::ChatTurn *turn;

    Session() : turn( 0 ) {}

    // Hands the turn to the caller (the only way it leaves a session,
    // so nothing here can free a type it cannot see).
    atui::net::ChatTurn *releaseTurn()
    {
        atui::net::ChatTurn *t = turn;
        turn = 0;
        return t;
    }
};

class SessionStore
{
public:
    // A store is never empty: it starts with one auto-titled session.
    SessionStore();

    // Returned by add() when the cap is reached, and used as "no seat".
    static const std::size_t kNoSeat;
    enum { kMaxSessions = 16 };

    std::size_t count() const { return seats_.size(); }

    Session &session( std::size_t i ) { return seats_[ i ]; }
    const Session &session( std::size_t i ) const { return seats_[ i ]; }

    std::size_t activeIndex() const { return active_; }
    Session &active() { return seats_[ active_ ]; }
    const Session &active() const { return seats_[ active_ ]; }

    // Appends a session and returns its index, or kNoSeat at the cap.
    // An empty (or blank) title gets the next generated one,
    // "Session N" -- N never repeats, so a deleted session's number is
    // not reused next to the sessions that remember it. Active does
    // not move: adding is not visiting, and the caller switches on
    // purpose when it means to.
    std::size_t add( const std::string &title );

    // Blank titles are refused: an unnamed row in a list of names is
    // worse than no edit. Surrounding blanks are trimmed on the way in.
    bool rename( std::size_t i, const std::string &title );

    // Erases one session and keeps the active seat pointing at
    // something sensible (the next session, or the last one). Removing
    // the only session resets the seat to a fresh one rather than
    // emptying the store. The erased session's turn must have been
    // released by the caller; the store will not free what it cannot
    // see.
    bool remove( std::size_t i );

    bool setActive( std::size_t i );

private:
    std::vector<Session> seats_;
    std::size_t active_;
    unsigned long next_;   // the number the next auto title carries
};

} // namespace atui::tui
} // namespace atui

#endif // ATUI_TUI_SESSIONS_H
