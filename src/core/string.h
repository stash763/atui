// A small dynamic string used by the portable core.
//
// Deliberately C++98, no <string>, no exceptions: the core must compile on
// older compilers (Borland C++ 4.52 / Open Watcom) for the DOS build.

#ifndef ATUI_CORE_STRING_H
#define ATUI_CORE_STRING_H

namespace atui {
namespace core {

class String {
public:
    String();
    explicit String( const char *s );
    String( const String &o );
    String &operator=( const String &o );
    ~String();

    int size() const;
    const char *c_str() const;
    void clear();
    void reserve( int n );

    void append( const char *s );
    void append( const String &s );
    String &operator+=( const char *s );
    String &operator+=( const String &s );

    char at( int i ) const;

    int indexOf( const char *needle ) const;
    int indexOf( const char *needle, int from ) const;

    String substr( int begin, int end = -1 ) const;

    int compareTo( const String &o ) const;
    bool equals( const String &o ) const;
    bool equals( const char *o ) const;

private:
    char *data_;
    int size_;
    int cap_;

    void ensure( int n );
};

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_STRING_H
