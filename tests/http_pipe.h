// A scripted raw HTTP responder, for testing the transport as a client.
//
// Testing the HTTP layer needs a server that is deliberately awkward, because that
// is where a state machine fails: headers that
// arrive in three pieces, a body with chunk framing, a connection that stops in the
// middle.  So this one plays exactly the script it is given: for each queued script
// it accepts one connection, records the request bytes, writes the pieces in order
// with a pause between them, and closes.
//
// The pause is the point.  A server that writes the whole response in one call
// proves nothing about a client that has to cope with a byte at a time; with a
// couple of milliseconds between pieces, a client that assumes "one read, one
// response" fails the test in the way it would fail on a network.
//
// Localhost only, tests only, and C++14 like the rest of the test tree (the program
// it tests is C++98, because the compiler atui is written for first is older).
#ifndef ATUI_TESTS_HTTP_PIPE_H
#define ATUI_TESTS_HTTP_PIPE_H

#ifdef ATUI_HAVE_NETWORK

#include <atomic>
#include <chrono>
#include <cstdlib>   // atoi, for the Content-Length in a request
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "pipe_sock.h"

namespace atui_test {

class ScriptedHttp
{
public:
    struct Script
    {
        std::vector<std::string> pieces;   // written in order
        int pauseMs;                       // between pieces
        bool closeAfter;                   // else hold the connection open
        // Close the moment the connection is accepted: no reading, no answering.
        // This is the peer that hangs up while the client is still sending, which is
        // the case a POSIX process has to survive -- writing to a socket whose peer
        // has gone raises SIGPIPE, whose default disposition is to terminate.
        bool hangUpFirst;
        // Accept, read nothing, answer nothing, and close after this long -- while
        // the client is still writing.  Different from `hangUpFirst`, which closes
        // before the client's first write and so teaches the stack about the hangup
        // before any byte goes out.
        int hangUpAfterMs;

        Script()
            : pauseMs( 0 ), closeAfter( true ), hangUpFirst( false )
            , hangUpAfterMs( 0 ) {}
    };

    ScriptedHttp()
        : listenFd_( kPipeInvalid ), port_( 0 ), stop_( false ), connections_( 0 )
    {
    }

    ~ScriptedHttp() { stop(); }

    ScriptedHttp( const ScriptedHttp & ) = delete;
    ScriptedHttp &operator=( const ScriptedHttp & ) = delete;

    bool start()
    {
        if( !pipeStackInit() )
            return false;

        listenFd_ = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( listenFd_ == kPipeInvalid )
            return false;
        int one = 1;
        ::setsockopt( listenFd_, SOL_SOCKET, SO_REUSEADDR, (const char *) &one,
                      sizeof( one ) );

        sockaddr_in addr;
        std::memset( &addr, 0, sizeof( addr ) );
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        addr.sin_port = 0;
        if( ::bind( listenFd_, (sockaddr *) &addr, sizeof( addr ) ) != 0 )
            return false;
        PipeLen len = sizeof( addr );
        if( ::getsockname( listenFd_, (sockaddr *) &addr, &len ) != 0 )
            return false;
        port_ = ntohs( addr.sin_port );
        if( ::listen( listenFd_, 4 ) != 0 )
            return false;
        thread_ = std::thread( [this] { run(); } );
        return true;
    }

    void stop()
    {
        stop_ = true;
        // Woken, joined, and only then closed.  The order is a Windows order: a socket
        // closed by one thread while another is waiting on it is not something Winsock
        // promises anything about, whereas shutting it down is, and the shutdown is what
        // makes the waiting `accept` come back.  POSIX cares about neither, and gets the
        // same sequence.
        if( listenFd_ != kPipeInvalid )
            pipeWake( listenFd_ );
        if( thread_.joinable() )
            thread_.join();
        if( listenFd_ != kPipeInvalid )
        {
            pipeClose( listenFd_ );
            listenFd_ = kPipeInvalid;
        }
    }

    int port() const { return port_; }

    void enqueue( const Script &script )
    {
        std::lock_guard<std::mutex> g( mutex_ );
        scripts_.push_back( script );
    }

    // A script that writes `response` and closes.
    void enqueueResponse( const std::string &response, int pauseMs = 0,
                          std::size_t pieceSize = 0 )
    {
        Script s;
        s.pauseMs = pauseMs;
        if( pieceSize == 0 )
            s.pieces.push_back( response );
        else
            for( std::size_t at = 0; at < response.size(); at += pieceSize )
                s.pieces.push_back( response.substr( at, pieceSize ) );
        enqueue( s );
    }

    // A script that writes the head, waits, writes the rest, then closes early.
    void enqueueTruncated( const std::string &head, const std::string &part )
    {
        Script s;
        s.pieces.push_back( head );
        s.pieces.push_back( part );
        s.pauseMs = 20;
        enqueue( s );
    }

    // How many times a client has connected.
    //
    // A request that is *decided* and a connection that is *alive* are different things,
    // and the difference only shows on a machine that gets to the first step late -- the
    // OS/2 interface measured six seconds, long enough for a server to hang up on a
    // connection nobody was reading.  The request bytes look the same either way, so this
    // counter is how a test can tell whether opening was deferred.
    int connections() const
    {
        return connections_.load();
    }

    std::string lastRequest() const
    {
        std::lock_guard<std::mutex> g( mutex_ );
        return lastRequest_;
    }

    // The body of the last request, which is the part that is JSON.  `lastRequest` is
    // what arrived -- request line, headers, the blank line, the body -- and every test
    // that has used it has looked for a substring, so the difference never mattered
    // until something needed to parse what came after the headers.  Splitting it here
    // keeps the one place that knows how this server frames a request the one place
    // that does it.  Empty when there was no blank line: no headers, no body to read.
    std::string lastBody() const
    {
        std::lock_guard<std::mutex> g( mutex_ );
        const std::size_t at = lastRequest_.find( "\r\n\r\n" );
        if( at == std::string::npos )
            return std::string();
        return lastRequest_.substr( at + 4 );
    }

private:
    void run()
    {
        while( !stop_ )
        {
            if( !pipeReadable( listenFd_, 50 ) )
                continue;

            const PipeFd client = ::accept( listenFd_, 0, 0 );
            if( client == kPipeInvalid )
                return;
            ++connections_;
            handle( client );
            pipeClose( client );
        }
    }

    void handle( PipeFd client )
    {
        // A hanging-up peer is scripted, not raced: take the instruction before
        // reading anything, so the timing is the script's and not the test's.
        int hangUpAfterMs = 0;
        {
            std::lock_guard<std::mutex> g( mutex_ );
            if( !scripts_.empty() )
            {
                if( scripts_.front().hangUpFirst )
                {
                    scripts_.erase( scripts_.begin() );
                    return;
                }
                hangUpAfterMs = scripts_.front().hangUpAfterMs;
            }
        }
        if( hangUpAfterMs > 0 )
        {
            // Close while the client still has bytes to write, and do not answer:
            // the client is mid-`send` when the FIN arrives.
            std::this_thread::sleep_for( std::chrono::milliseconds( hangUpAfterMs ) );
            return;
        }

        // Read the request: until the headers are complete and the body they
        // promise has arrived, with a bounded wait so a client that never sends
        // cannot wedge the test.
        std::string request;
        for( int idle = 0; idle < 20; ++idle )
        {
            if( !pipeReadable( client, 50 ) )
                break;
            char buf[ 4096 ];
            const PipeCount n = ::recv( client, buf, (int) sizeof( buf ), 0 );
            if( n <= 0 )
                break;
            request.append( buf, (size_t) n );
            if( requestComplete( request ) )
                break;
        }
        {
            std::lock_guard<std::mutex> g( mutex_ );
            lastRequest_ = request;
        }

        Script script;
        {
            std::lock_guard<std::mutex> g( mutex_ );
            if( scripts_.empty() )
                return;                       // nothing scripted: close
            script = scripts_.front();
            scripts_.erase( scripts_.begin() );
        }

        for( size_t i = 0; i < script.pieces.size(); ++i )
        {
            if( i > 0 && script.pauseMs > 0 )
                std::this_thread::sleep_for( std::chrono::milliseconds( script.pauseMs ) );
            writeAll( client, script.pieces[i] );
        }
        if( !script.closeAfter )
            std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
    }

    static bool requestComplete( const std::string &request )
    {
        const size_t head = request.find( "\r\n\r\n" );
        if( head == std::string::npos )
            return false;
        size_t want = 0;
        const std::string headers = request.substr( 0, head );
        size_t at = 0;
        while( ( at = headers.find( "Content-Length:", at ) ) != std::string::npos )
        {
            want = (size_t) atoi( headers.c_str() + at + 15 );
            at += 1;
        }
        return request.size() >= head + 4 + want;
    }

    static void writeAll( PipeFd fd, const std::string &data )
    {
        size_t sent = 0;
        while( sent < data.size() )
        {
            const PipeCount n = ::send( fd, data.data() + sent,
                                        (int) ( data.size() - sent ), 0 );
            if( n <= 0 )
                return;
            sent += (size_t) n;
        }
    }

    PipeFd listenFd_;
    int port_;
    std::atomic<bool> stop_;
    std::thread thread_;
    std::vector<Script> scripts_;
    std::string lastRequest_;
    std::atomic<int> connections_;
    mutable std::mutex mutex_;
};

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
#endif // ATUI_TESTS_HTTP_PIPE_H
