// A tiny single-threaded, scripted HTTP/SSE server for integration-testing the
// OpenAI-compatible client against a real socket (localhost), with no external
// network. Only used by tests/oai_mock_test.cpp (network builds).
//
// It binds an ephemeral port on 127.0.0.1, and for each incoming request:
//   * records the full raw request (headers + body) so tests can assert on what
//     OaiClient::buildBody() actually put on the wire,
//   * replies with the next enqueued SSE body (HTTP 200, EOF-terminated),
//   * closes the connection.
// When the scripted responses run out (or stop() is called) the accept loop
// exits, so the worker thread never blocks the process at teardown.
#ifndef ATUI_TESTS_MOCK_HTTP_H
#define ATUI_TESTS_MOCK_HTTP_H

#ifdef ATUI_HAVE_NETWORK

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace atui_test {

class MockOpenAiServer
{
public:
    MockOpenAiServer() : listenFd_( -1 ), port_( 0 ), stop_( false ) {}
    ~MockOpenAiServer() { stop(); }

    MockOpenAiServer( const MockOpenAiServer & ) = delete;
    MockOpenAiServer &operator=( const MockOpenAiServer & ) = delete;

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
        addr.sin_port = 0; // ephemeral
        if( ::bind( listenFd_, (sockaddr *) &addr, sizeof( addr ) ) != 0 )
        {
            ::close( listenFd_ );
            listenFd_ = -1;
            return false;
        }
        socklen_t len = sizeof( addr );
        if( ::getsockname( listenFd_, (sockaddr *) &addr, &len ) != 0 )
        {
            ::close( listenFd_ );
            listenFd_ = -1;
            return false;
        }
        port_ = ntohs( addr.sin_port );

        if( ::listen( listenFd_, 8 ) != 0 )
        {
            ::close( listenFd_ );
            listenFd_ = -1;
            port_ = 0;
            return false;
        }

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

    std::string baseUrl() const
    {
        char buf[64];
        std::snprintf( buf, sizeof( buf ), "http://127.0.0.1:%d/v1", port_ );
        return std::string( buf );
    }

    // Queue a canned SSE body (the lines already formatted as "data: ...\n\n").
    void enqueue( const std::string &sseBody )
    {
        std::lock_guard< std::mutex > lk( mu_ );
        responses_.push_back( sseBody );
    }

    // Raw request texts recorded so far (request line + headers + body).
    std::vector<std::string> requests() const
    {
        std::lock_guard< std::mutex > lk( mu_ );
        return requests_;
    }

    std::string lastRequest() const
    {
        std::lock_guard< std::mutex > lk( mu_ );
        if( requests_.empty() )
            return std::string();
        return requests_.back();
    }

    // Build an SSE body from a list of JSON payload strings, terminated with
    // the [DONE] sentinel (mirrors what OpenAI-compatible servers send).
    static std::string sse( const std::vector<std::string> &payloads )
    {
        std::string out;
        for( size_t i = 0; i < payloads.size(); ++i )
        {
            out += "data: ";
            out += payloads[i];
            out += "\n\n";
        }
        out += "data: [DONE]\n\n";
        return out;
    }

private:
    void run()
    {
        while( !stop_ )
        {
            pollfd pfd;
            pfd.fd = listenFd_;
            pfd.events = POLLIN;
            pfd.revents = 0;
            int r = ::poll( &pfd, 1, 100 ); // 100ms tick to observe stop_
            if( stop_.load() )
                break;
            if( r == 0 )
                continue; // timeout: re-check stop_
            if( r < 0 )
            {
                if( errno == EINTR )
                    continue;
                break; // socket closed
            }

            int client = ::accept( listenFd_, nullptr, nullptr );
            if( client < 0 )
            {
                if( errno == EINTR || errno == ECONNABORTED )
                    continue;
                break;
            }

            handle( client );
            ::close( client );

            // Once every scripted response has been consumed, stop listening:
            // the client is done, and this lets the thread exit cleanly.
            bool more;
            {
                std::lock_guard< std::mutex > lk( mu_ );
                more = served_ < responses_.size();
            }
            if( !more )
                break;
        }
    }

    static bool readAll( int fd, std::string &out )
    {
        char buf[4096];
        while( true )
        {
            ssize_t n = ::recv( fd, buf, sizeof( buf ), 0 );
            if( n > 0 )
            {
                out.append( buf, (size_t) n );
                continue;
            }
            if( n == 0 )
                return true;  // peer closed
            if( n < 0 && errno == EINTR )
                continue;
            return false;
        }
    }

    static void writeAll( int fd, const std::string &data )
    {
        size_t off = 0;
        while( off < data.size() )
        {
            ssize_t n = ::send( fd, data.data() + off, data.size() - off,
                                MSG_NOSIGNAL );
            if( n <= 0 )
            {
                if( n < 0 && errno == EINTR )
                    continue;
                return;
            }
            off += (size_t) n;
        }
    }

    static std::string lower( const std::string &s )
    {
        std::string o = s;
        for( size_t i = 0; i < o.size(); ++i )
            if( o[i] >= 'A' && o[i] <= 'Z' )
                o[i] = char( o[i] - 'A' + 'a' );
        return o;
    }

    // Read request headers (up to the blank line), honour Content-Length and
    // Expect: 100-continue, then record the whole request.
    void handle( int client )
    {
#ifdef SO_NOSIGPIPE
        int one = 1;
        ::setsockopt( client, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof( one ) );
#endif
        std::string req;
        size_t hdrEnd = std::string::npos;
        // Read until the header terminator is present.
        while( ( hdrEnd = req.find( "\r\n\r\n" ) ) == std::string::npos )
        {
            char buf[4096];
            ssize_t n = ::recv( client, buf, sizeof( buf ), 0 );
            if( n > 0 )
                req.append( buf, (size_t) n );
            else if( n == 0 )
                break;
            else if( errno == EINTR )
                continue;
            else
                return;
        }
        if( hdrEnd == std::string::npos )
            return;

        std::string headers = req.substr( 0, hdrEnd );
        size_t bodyHave = req.size() - ( hdrEnd + 4 );

        // Content-Length (case-insensitive).
        std::string hl = lower( headers );
        size_t clPos = hl.find( "content-length:" );
        size_t contentLength = 0;
        if( clPos != std::string::npos )
            contentLength = (size_t) std::strtoul(
                headers.c_str() + clPos + 15, nullptr, 10 );

        if( hl.find( "expect: 100-continue" ) != std::string::npos )
            writeAll( client, "HTTP/1.1 100 Continue\r\n\r\n" );

        // Drain the rest of the body if the client still has bytes to send.
        while( bodyHave < contentLength )
        {
            char buf[4096];
            ssize_t n = ::recv( client, buf, sizeof( buf ), 0 );
            if( n > 0 )
            {
                req.append( buf, (size_t) n );
                bodyHave += (size_t) n;
            }
            else if( n == 0 )
                break;
            else if( errno == EINTR )
                continue;
            else
                break;
        }

        {
            std::lock_guard< std::mutex > lk( mu_ );
            requests_.push_back( req );
        }

        // Pick the next scripted SSE body.
        std::string body;
        {
            std::lock_guard< std::mutex > lk( mu_ );
            if( served_ < responses_.size() )
                body = responses_[ served_++ ];
            else
                body = "data: {\"error\":\"no scripted response\"}\n\n";
        }

        std::string resp;
        resp += "HTTP/1.1 200 OK\r\n";
        resp += "Content-Type: text/event-stream\r\n";
        resp += "Cache-Control: no-cache\r\n";
        resp += "Connection: close\r\n";
        resp += "\r\n";
        resp += body;
        writeAll( client, resp );
    }

    int listenFd_;
    int port_;
    std::atomic<bool> stop_;
    std::thread thread_;
    mutable std::mutex mu_;
    std::vector<std::string> requests_;
    std::vector<std::string> responses_;
    size_t served_ = 0;
};

} // namespace atui_test

#endif // ATUI_HAVE_NETWORK
#endif // ATUI_TESTS_MOCK_HTTP_H
