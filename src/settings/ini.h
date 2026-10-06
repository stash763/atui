// Small INI-file helper: reads and writes a simple
//   [section]
//   key = value
// configuration file. One class, reused by every part of the app that needs
// persistence, so callers do not hand-roll parsing.
//
// Supported syntax:
//   - "[name]" starts a new section; keys before any section go to "general".
//   - "key = value" or "key=value"; the split is at the FIRST '=' only, so
//     values may themselves contain ':' or spaces (e.g. http URLs).
//   - Lines that are blank or start with '#' or ';' are comments.
//   - Section and key names are matched case-insensitively; values are kept
//     verbatim (leading/trailing whitespace trimmed).
//
// C++14. Values are stored as strings; typed accessors convert on demand and
// fall back to a caller-supplied default when a key is absent or unparseable.
#ifndef ATUI_SETTINGS_INI_H
#define ATUI_SETTINGS_INI_H

#include <map>
#include <string>

namespace atui {
namespace settings {

class IniFile {
public:
    explicit IniFile( const std::string &path = std::string() );

    void setPath( const std::string &path );
    const std::string &path() const;

    // Read the file. Missing/unreadable file returns false (not an error on
    // first run); existing keys are replaced by what the file contains.
    bool load();
    // Write all current values to path(), creating parent directories when
    // needed. Uses restrictive (0600) permissions on POSIX since config may
    // hold secrets such as an API key.
    bool save() const;

    bool loaded() const;   // whether the last load() found a file

    bool has( const char *section, const char *key ) const;

    std::string getString( const char *section, const char *key,
                           const std::string &def = std::string() ) const;
    int    getInt( const char *section, const char *key, int def = 0 ) const;
    double getDouble( const char *section, const char *key, double def = 0.0 ) const;
    bool   getBool( const char *section, const char *key, bool def = false ) const;

    void setString( const char *section, const char *key, const std::string &value );
    void setInt( const char *section, const char *key, int value );
    void setDouble( const char *section, const char *key, double value );
    void setBool( const char *section, const char *key, bool value );

private:
    // section -> (key -> value)
    typedef std::map<std::string, std::string> Section;
    std::map<std::string, Section> data_;
    std::string path_;
    bool loaded_;
};

} // namespace atui::settings
} // namespace atui

#endif // ATUI_SETTINGS_INI_H
