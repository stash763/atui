// A scripted raw HTTP responder, for testing the transport as a client.
//
// `mock_http.h` is the OpenAI-shaped server: it answers with a 200 and an SSE body
// and is the right thing to test a client's request-building and SSE parsing
// against.  Testing the HTTP layer needs the opposite -- a server that is
// deliberately awkward, because that is where a state machine fails: headers that
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

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace atui_test {

class ScriptedHttp
{
public:
    struct Script
    {
        std::vector<std::string> pieces;   // written in order
        int pauseMs;                       // between pieces
        bool closeAfter;                   // else hold the connection open

        Script() : pauseMs( 0 ), closeAfter( true ) {}
    };

    ScriptedHttp()
        : listenFd_( -1 ), port_( 0 ), stop_( false )
    {
    }

    ~ScriptedHttp() { stop(); }

    ScriptedHttp( const ScriptedHttp & ) = delete;
    ScriptedHttp &operator=( const ScriptedHttp & ) = delete;

    bool start()
    {
        listenFd_ = ::socket( AF_INET, SOCK_STREAM, 0 );
        if( listenFd_ < 0 )
            return false;
        int one = 1;
        ::setsockopt( listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof( one ) );

        sockaddr_in addr;
        std::memset( &addr, 0, sizeof( addr ) );
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
        addr.sin_port = 0;
        if( ::bind( listenFd_, (sockaddr *) &addr, sizeof( addr ) ) != 0 )
            return false;
        socklen_t len = sizeof( addr );
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
        if( listenFd_ >= 0 )
        {
            ::shutdown( listenFd_, SHUT_RDWR );
            ::close( listenFd_ );
            listenFd_ = -1;
        }
        if( thread_.joinable() )
            thread_.join();
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

    std::string lastRequest() const
    {
        std::lock_guard<std::mutex> g( mutex_ );
        return lastRequest_;
    }

private:
    void run()
    {
        while( !stop_ )
        {
            pollfd pfd;
            pfd.fd = listenFd_;
            pfd.events = POLLIN;
            const int ready = ::poll( &pfd, 1, 50 );
            if( ready <= 0 )
                continue;

            const int client = ::accept( listenFd_, 0, 0 );
            if( client < 0 )
                return;
            handle( client );
            ::close( client );
        }
    }

    void handle( int client )
    {
        // Read the request: until the headers are complete and the body they
        // promise has arrived, with a bounded wait so a client that never sends
        // cannot wedge the test.
        std::string request;
        for( int idle = 0; idle < 20; ++idle )
        {
            pollfd pfd;
            pfd.fd = client;
            pfd.events = POLLIN;
            if( ::poll( &pfd, 1, 50 ) <= 0 )
                break;
            char buf[ 4096 ];
            const ssize_t n = ::recv( client, buf, sizeof( buf ), 0 );
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

    static void writeAll( int fd, const std::string &data )
    {
        size_t sent = 0;
        while( sent < data.size() )
        {
            const ssize_t n = ::send( fd, data.data() + sent, data.size() - sent, 0 );
            if( n <= 0 )
                return;
            sent += (size_t) n;
        }
    }

    int listenFd_;
    int port_;
    std::atomic<bool> stop_;
    std::thread thread_;
    std::vector<Script> scripts_;
    std::string lastRequest_;
    mutable std::mutex mutex_;
};

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
#endif // ATUI_TESTS_HTTP_PIPE_H
