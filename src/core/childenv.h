// What a command atui runs gets to see of the environment.
//
// A tool call runs a shell command as a child of atui, and a child inherits its
// parent's environment - which for atui means the provider API keys, whatever
// `CI`-style tokens the developer had exported, and anything else that was in the
// shell atui was started from. The output of that command goes into the transcript
// and from there to a model on another machine, so `env` in a build step is a way
// to read the machine's credentials out of it and into somebody else's context
// window. Commands atui runs do not need them; `make` and `git` and the test suite
// are not the things that were given a secret to log in with.
//
// So the credential-looking names are dropped, and the ones a build genuinely
// needs can be named in `[tools] pass_env`. Everything else - PATH, HOME, TERM,
// the compiler's own variables - passes through untouched, because a command that
// cannot find `cc` is not a safer command, only a broken one.
//
// C++14, no TUI and no network dependency.
#ifndef ATUI_CORE_CHILDENV_H
#define ATUI_CORE_CHILDENV_H

#include <string>
#include <vector>

namespace atui {
namespace core {

// Whether `NAME` looks like it holds a credential rather than a configuration
// value. Deliberately a name match and not a value match: the name is what both
// platforms and people write consistently, and a variable named `API_KEY` holding
// something harmless costs a build step the option of reading it.
bool looksSecretEnvName( const std::string &name );

// Whether `name` appears in `passList` - the `[tools] pass_env` value, names
// separated by commas or spaces. Comparison ignores case and an accidental
// surrounding space, since this is typed by a person into a config file.
bool envNamePasses( const std::string &name, const std::string &passList );

// The environment to hand a child process: everything this process has except the
// credential-looking names, except those `passList` names. Built once per child.
//
// `argv()` is the NULL-terminated `char *[]` that `execve()` wants. The pointers
// belong to this object and stay valid until it is destroyed or rebuilt, which is
// the lifetime a fork/exec pair needs.
class ChildEnvironment
{
public:
    explicit ChildEnvironment( const std::string &passList = std::string() );

    char *const * argv() const;
    std::size_t size() const;    // entries, not counting the terminating null

private:
    void rebuild( const std::string &passList );

    std::vector< std::string > entries_;
    std::vector< char * > pointers_;
};

// Hides the credential-looking variables from any child started while it exists,
// and puts them back afterwards. For the platforms whose spawn mechanism copies
// the parent's environment at the moment of the call (`_popen`), where there is no
// way to hand over a curated one. The values are copied before anything is
// removed: unsetting a variable is not guaranteed to leave the string `getenv()`
// returned pointing at something.
class HiddenCredentials
{
public:
    explicit HiddenCredentials( const std::string &passList = std::string() );
    ~HiddenCredentials();

private:
    HiddenCredentials( const HiddenCredentials & );
    HiddenCredentials &operator=( const HiddenCredentials & );

    std::vector< std::string > names_;
    std::vector< std::string > values_;
};

} // namespace atui::core
} // namespace atui

#endif // ATUI_CORE_CHILDENV_H
