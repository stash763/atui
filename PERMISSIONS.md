# Permissions

atui asks before a tool call that reaches outside the project it was started in,
and gets on with the work inside it. "Inside" is decided by code. "What does this
command actually do" is asked of a small dedicated model, which is never the
thing that grants permission.

The invariant the whole design rests on:

> **The model describes; the policy decides. A model can only ever widen what
> reaches the user, never narrow it.**

It cannot approve, cannot lift a veto, and its claims about paths are treated as
unverified unless our own checks agree. Every failure - no model configured,
timeout, an answer that cannot be parsed, a non-network build - lands on *ask*.

## Two layers

**Facts** (`src/tools/permissions.cpp`), all deterministic and testable offline:

- **Containment.** The project root is captured at startup (`realpath()` of the
  cwd, or `--root DIR`). A path is inside if its expanded, `..`-normalised,
  `realpath`-resolved form *is* the root or lies below it, compared component by
  component so `/home/u/proj-extra` is not inside `/home/u/proj`. This is what
  makes symlinks out of the tree visible instead of surprising.
- **Path shapes.** Answered for every platform atui builds for, on whichever one
  it happens to be running (`src/core/paths.{h,cpp}`): `/etc/hosts`, `C:\Windows`,
  `C:/Windows`, `\\server\c$\share` and the DOS drive-relative `C:notes.txt` are
  each absolute in their own way, either separator splits a component on either
  side of a comparison, and the anchor - drive, share or `/` - has to match before
  any directory name does. Case folds where the filesystem folds it (Windows,
  DOS, OS/2), nowhere else, and the tests flip that switch rather than trusting
  the host. A policy that only understood `/` could be walked past by typing `\`:
  `C:\Windows\win.ini` used to have the project root glued in front of it, which
  made it *start* with the root and read as inside.
- **The deny-list.** Files that are never read or written unattended, in the tree
  or out: `.git`/`.hg`/`.svn`, `.ssh`, `.aws`, `.gnupg`, `.kube`, `.docker`,
  `.vscode`, `.idea`, `.config/gh`, `.config/gcloud`, `Keychain(s)`, `.mozilla`;
  dotenv files; shell rc files; `.pem` `.key` `.p12` `.pfx` `.kdbx` `.crt` `.der`
  `.secret`; `id_*` private keys; `tasks.json`; and the files the DOS-derived
  platforms run or keep their settings in - `autoexec.bat`, `config.sys`,
  `win.ini`, `system.ini`, `os2.ini`, `protocol.ini`, `ntuser.dat`. Matching folds
  case and accepts either separator, because `~/.SSH/id_rsa` and `AUTOEXEC.BAT`
  name the same files as anything else: the cost is a question about a project
  file someone happened to name `.ENV`.
- **Unverified surface.** Presence checks, not a shell parser: `$(`, backticks,
  `${`, `-exec`, `xargs`, `eval`, `base64`/`base32`/`xxd`, `sudo`/`doas`/`su`,
  a pipe into an interpreter; and their equivalents where the shell is `cmd` -
  `powershell`/`pwsh`, `iex`/`invoke-expression`, an `-enc`/`-encodedcommand`
  payload, `certutil`, `bitsadmin`, `mshta`, `rundll32`, `regsvr32`, `schtasks`,
  `cmdkey`, and `reg add|delete|import|load`. The text is matched case-folded,
  since neither `cmd` nor PowerShell cares how a program was typed. Anything using
  one is never auto-approved *and never sent to the model* - it goes to the user,
  because it is exactly what a prompt injection writes and the answer cannot be
  read off the text. Ordinary work stays ordinary: `python3 -m pytest` is not
  "pipes into an interpreter", and `go build ./cmd/server` is not a script host.
- **Publishing.** `git push`, `npm publish`, `twine upload`, `docker push`,
  `kubectl apply/delete/create`, `terraform apply/destroy`, `aws s3 cp/sync`,
  `scp`, `sftp`, `rsync`. Writing to another machine is outside the project in
  the only way that matters, and containment cannot see it happen: no directory
  involved. The list is deliberately one-sided - a missed entry costs the model a
  chance to describe the command, an over-eager one costs one question. Package
  installs are *not* on it; `cargo build`, `npm ci` and `pip install -r` into a
  project's own venv are the work this feature exists to stop interrupting, and a
  system-wide install announces itself as an out-of-tree path or as `sudo`.
- **Paths mentioned.** Tokens that look like paths (`/x`, `./x`, `~/x`, `$HOME/x`,
  `%USERPROFILE%\x`, `dir\file`, `--out=/tmp/x`, `C:\Windows\win.ini`,
  `\\server\c$\notes.txt`, and `@file` as curl spells a file argument) are
  extracted, kept exactly as typed so the question quotes what the person wrote,
  and resolved. A path with a variable in it whose value atui does not have -
  `%TEMP%\cache`, `$BUILD_DIR/out` - is a location the *shell* picks rather than
  the caller, so it stays unplaced: that is a question, never an approval. Only
  the home-directory variables are expanded, and a whole drive or share is never
  guessed at.

**Judgement** (`src/net/permission_judge.{h,cpp}`) - one model call, for the
commands facts cannot settle. It answers what the call does; it is never told
whether it may do it.

## Running a command

Approval decides *whether* a command runs. What it runs with is a separate policy,
and it is the one part of the design that limits damage rather than asking about
it (`src/core/childenv.{h,cpp}`, `run_command_tool.cpp`):

- **No credentials in the environment.** A child inherits its parent's environment,
  and atui's contains the provider API keys plus whatever the developer had
  exported. A command's output goes into the transcript and from there to a model
  on another machine, so `env` would be a way to read the machine's secrets into
  somebody's context window. Variables whose *names* look like credentials
  (`KEY`, `TOKEN`, `SECRET`, `PASSWORD`, `PASSWD`, `CREDENTIAL`, `PASSPHRASE`,
  `PRIVATE`) are removed. `SSH_AUTH_SOCK` stays, because it is a socket name rather
  than a secret and removing it would break `git push` over ssh for nothing;
  `PATH`, `HOME`, `CC` and `MAKEFLAGS` stay, because a command that cannot find the
  compiler is not a safer command. `pass_env` (below) names the ones a build
  genuinely reads out of the environment - the escape hatch is one line of config,
  and the default is the one that is right when nobody has thought about it.
- **No standard input.** A command that reads from a terminal nobody is looking at
  is the second classic way a tool call never returns. Its stdin is `/dev/null`, so
  it gets end-of-file, which is the honest answer.
- **A deadline that stops it.** `[tools] command_timeout` (default 300s, 0 for no
  limit). The child runs in its own process group and the whole group is killed when
  the time is up, because the thing that hangs is rarely the shell - it is the
  grandchild still holding the pipe, and killing the shell alone would leave that
  behind. Where the platform cannot stop a process - `cmd` hands back no pid to hold
  on to - the timeout is not quietly pretended: `commandTimeoutIsEnforced()` says
  so, and that platform keeps the documented limit instead of the real one.
- **Signals belong to the command.** `exec()` resets signal *handlers* but not the
  signal *mask*, and atui makes its requests from a worker thread whose mask belongs
  to whatever the network library was doing on it. The child's mask is therefore
  emptied before it execs: a command that cannot be interrupted, or that never hears
  that the pipe it was writing to closed, is a bug nobody can reproduce at the
  shell. For the same reason `curl` is not allowed to ignore `SIGPIPE` or install a
  `SIGALRM` handler to time a name resolution - `CURLOPT_NOSIGNAL` is set on both
  handles. An ignored signal is one the command inherits, and a `longjmp` out of a
  handler curl installed is a crash in the middle of a streamed answer with no
  reproducible cause.

Together these are why "the user approved it" is not the only thing standing between
a confused model and the machine: the command it runs cannot read the keys out of
its own environment, cannot wait for input that never comes, cannot outlive the time
it was given, and dies of the signals it expects to die of.

## The ladder

`decide()` walks these in order. The first match wins, and every rung except
`prPathInside` and `prPlanCleared` means a question. The numbers are stable
identifiers, not positions, so an old log still means what it meant:

| Rung | Meaning |
|---|---|
| 1  `prModeAskAll`    | the mode says ask before everything |
| 2  `prRootTooBroad`  | the root is `$HOME`, `/`, a whole drive or share - containment proves nothing |
| 3  `prUnknownCall`   | a tool that reports no subject to check |
| 4  `prDenyList`      | credentials, hooks, editor task files |
| 5  `prPathInside`    | the file it touches resolves inside the project |
| 6  `prUnverified`    | shell surface refused to reason about |
| 7  `prPathOutside`   | a path we resolved lands outside the project, or on another machine's share |
| 8  `prNoAgent`       | nothing was asked, or there is nothing to ask |
| 9  `prPlanUnread`    | the agent's answer could not be used |
| 10 `prPlanFlagged`   | `elevated`, `sensitive`, or `exec` |
| 11 `prExfil`         | network together with a read outside the project |
| 12 `prWriteOutside`  | a write we cannot place inside the project |
| 13 `prPlanCleared`   | described, contained, nothing flagged |
| 14 `prSessionAllowed` | the user allowed this exact call earlier in the run |
| 15 `prPublish`       | publishes to another machine |
| 16 `prNoUndoNet`     | a write the undo journal cannot cover |

Three of them are worth dwelling on:

**`prRootTooBroad`** is a refusal to pretend. With `$HOME` as the project,
"inside the project" describes everything worth stealing, so nothing is allowed
unattended no matter what the mode says. Starting at `/`, at `C:\`, or at the root
of a share is the same argument one level further down.

**`prExfil`** is the one rule no setting can override. A plan that uses the
network *and* reads outside the root, or reads a deny-listed path, is refused by
code whatever the model said it was doing - the model's intent may be genuine and
the effect is still a secret leaving the machine.

**`prNoUndoNet`** admits that "inside the project" is only half of what makes a
write safe to do without asking. The other half is that it can be taken back:
atui spends its life blocked on a network stream and the realistic failure is the
process dying partway through a turn that has already rewritten two files. So an
in-tree write is auto-approved only while there is a copy behind it, and when the
journal is enabled but cannot cover this file - too big for `max_file`, no
writable journal directory - it is a question instead. Turning the journal *off*
is not this: that is a decision already taken, and the policy stops asking.

## The agent contract

Asked of a *different* model than the one writing the code when
`judge_model` is set: no shared persona, no self-approval, and it does not queue
behind the chat request on a single-slot server. It sees the project root, the
tool name and its arguments - never the conversation, or an injection only has to
write a convincing line for the other model.

Seven lines, nothing else:

```
READ <path>        a file or directory the call reads
WRITE <path>       a file or directory it creates or changes
NET <yes|no>       whether it uses the network
EXEC <yes|no>      whether it runs code from outside the project
ELEVATED <yes|no>  whether it needs root or changes privileges
SENSITIVE <yes|no> whether it reads personal data
SUMMARY <short line>
```

No verdict key exists, so there is nothing for it to be talked into. `EXEC` is
spelled out in the prompt as "not the project's own build and tests", because a
model asked whether `make test` runs code will say yes.

Details that turned out to matter:

- **The request must be streamed.** This client parses SSE `data:` lines; a
  `stream = false` reply is a single JSON body and yields empty content.
- **Thinking must be turned off.** A reasoning model spends the entire token
  budget on `reasoning_content` and answers with nothing, so a 300-token judge
  request came back empty after 22 seconds. `OaiRequest::disableThinking` sends
  `reasoning_effort: "none"`, `think: false` and
  `chat_template_kwargs: {enable_thinking: false}` together, since servers
  disagree about which one they read, and if the answer is still empty the parser
  falls back to the reasoning text. Turning thinking off took a judgement from
  22s-and-nothing to 1.4-3.6s.
- **An unreadable flag counts as set.** The call mentioned the effect; guessing
  "no" is the direction that runs something nobody approved.
- **Zero recognised keys is not an answer.** A reply of prose, or a code fence,
  or ChatML leakage is `prPlanUnread` - ask.
- **Verdicts are cached** on `(prompt version, model, root, tool, arguments)`,
  unhashed, so a fix-and-rerun loop pays once per distinct command. The prompt
  version (`judgePromptVersion()`) is part of the key: change the question and
  every cached answer is void.
- **`ATUI_JUDGE_DEBUG=1`** echoes what the model actually said to stderr. Without
  it the subsystem cannot be tuned, because "could not describe the command" is
  otherwise indistinguishable from six different causes.

## Configuration

`[permissions]` in the config file, each key seeded from the environment first:

| Key | Env | Values | Default |
|---|---|---|---|
| `mode`          | `ATUI_PERMISSIONS`        | `ask_all`, `exact`, `agent` | `agent` |
| `network`       | `ATUI_PERMISSION_NETWORK` | `agent`, `ask`, `allow` | `agent` |
| `judge_model`   | `ATUI_JUDGE_MODEL`        | a model id, empty = the active model | |
| `judge_seconds` | `ATUI_JUDGE_SECONDS`      | 1-1440 | 5 |
| `log`           | `ATUI_PERMISSION_LOG`     | bool | true |

- `ask_all` - every tool call is confirmed. What atui did before, and one
  environment variable away.
- `exact` - containment only: in-tree `edit_file` runs, out-of-tree paths ask,
  and a command that needs understanding asks because nothing is there to
  understand it.
- `agent` - facts plus judgement. The shipped default, and safe by construction
  rather than by judgement: `decide()` only reaches "run" by falling through every
  veto, and a session with no model to ask degrades to `exact` for commands.

`network` covers the case the facts cannot see at all, since a command's network
use is only ever a model's claim: `ask` asks about any network use the agent
reports, `agent` (the default) judges it per command so a build that fetches
dependencies is not an interrupt, and `allow` stops treating it as a question.
Either way `prExfil` outranks it.

`[tools]` is what a command runs with rather than what it is allowed to do, so it
is not in the ladder:

| Key | Env | Values | Default |
|---|---|---|---|
| `command_timeout` | `ATUI_COMMAND_TIMEOUT` | seconds, 0 = no limit | 300 |
| `pass_env`        | `ATUI_PASS_ENV`        | variable names, comma separated | empty |

`command_timeout` is enforced where a process can be stopped and documented where
it cannot (see "Running a command"), and it is the one setting of the two with a
field in Options > Settings. `pass_env` deliberately has no field: its whole job is
to let one credential-shaped variable through, and a dialog that makes it as quick
to type as the wheel speed is a dialog that gets typed in. Matching ignores case,
so a config file written by hand does not have to remember how the shell spelled
the name.

## The undo journal

`[undo]`, seeded from the environment:

| Key | Env | Values | Default |
|---|---|---|---|
| `enabled`     | `ATUI_UNDO`            | bool | true |
| `dir`         | `ATUI_UNDO_DIR`        | a directory | empty = `<install dir>/undo` |
| `max_bytes`   | `ATUI_UNDO_MAX_BYTES`  | 64 KiB - 64 MiB | 1048576 |
| `max_file`    | `ATUI_UNDO_MAX_FILE`   | 1 KiB - 32 MiB | 1048576 |

The install directory is found from `ATUI_HOME`, else the directory of
`argv[0]`, else the prefix compiled in by CMake - in that order because argv[0]
is the one mechanism every platform atui builds for agrees on, and a
read-only or packaged install is what `ATUI_HOME` is for.

**The order of operations is the feature.** Before a file is touched: copy its
current bytes into the journal, flush, close. Then write the new contents. Then
mark the entry committed. An entry that was never committed is not garbage, it is
an interrupted write, and atui finds it on the next start and offers to put the
file back:

```
atui stopped while it was writing

  /home/u/proj/notes.txt

That file holds 31 bytes atui cannot account for (the saved copy holds 27).
...
Put the saved copy back over the file?
```

That middle sentence is why the offer can be trusted. An interrupted write has no
"after" hash, so the bytes on disk cannot be placed: they may be the ones atui
was writing or the ones somebody typed afterwards. So the dialog says what it
knows and asks, and when the file still holds exactly what the copy holds - the
write never happened - the entry is cleared quietly instead of asking about
nothing.

Storage is `U000123.PRE` (the previous bytes) plus `U000123.INF` (a dozen
`key=value` lines: `stamp`, `pid`, `tool`, `op`, `state`, `target`, `hash`,
`bytes`, `after`) in names that are legal on FAT and readable with `type` or
`more` on a machine with no tooling. Copies are streamed through a 4 KiB buffer,
so journaling a 5 MB file costs the same memory as journaling a 50 byte one. The
whole journal is capped at `max_bytes` and the oldest entries are dropped first -
except an interrupted one, which may be the only copy of a file, and except the
newest entry, because a cap that ate the change made a moment ago would be a cap
that ate the feature.

Restores go through `decide()` like any other write: an entry pointing at `.env`
is refused by the same rule that refused to edit it, and the confirm dialog quotes
it. Clicking a menu item is not a fact about the file. The one answer that is
dropped is `prNoUndoNet` itself, since the copy in hand *is* the undo copy.

Durability, stated honestly: every journal write is flushed, closed and - where
the operating system has a call for it - pushed out to the device, so on Linux,
FreeBSD, macOS and Windows the promise is **survives the machine dying** rather
than merely survives atui. MS-DOS and OS/2 are not asked: there the call is either
missing or means "flush every buffer on the machine", which costs whoever is
writing to a floppy more than the bytes it protects, and the promise there stays
the weaker one.

Two windows stay open wherever a flush is not enough to close them. A new file can
be written without its directory entry surviving, because no portable call flushes
a directory. And the moment an entry is rewritten from `pending` to `saved` is a
truncate-and-write of the record itself: losing power there loses that one undo
option, not the file it describes, since the copy of the original had already been
taken to the device first. Both are written here rather than fixed because closing
them means either a log-structured journal or a rename atui cannot assume is
atomic.

`run_command` has no journal, because a command has no inverse - which is also why
commands are the thing the policy is strictest about.

`--root DIR` also changes the working directory to DIR. Not for tidiness: the
policy resolves a relative path against the root, `fopen()` resolves it against
the working directory, and the journal records what it copied. When those two
directories differ, the policy is describing a file that no write ever went to.

## In the UI

- The status line shows what the current settings will actually do:
  `perm: asks first` (ask_all), `perm: in-project ok, asks outside` (exact),
  `perm: in-project ok, a model checks the rest` (agent with somewhere to ask),
  or `perm: in-project ok, asks about commands` (agent in a build or session
  with no model - files are still contained, commands still ask). Live, no
  restart.
- Settings (File > Options > Settings) has the three modes as a radio group plus
  the judging model and its timeout.
- The approval dialog says *why it is asking* in the reason the policy produced
  ("reaches outside the project", "it publishes to somewhere else (git push)"),
  not just what it is. Yes / **cmYes**, Remember / **cmOK**, No / **cmNo**, with
  Enter and Esc both declining.
- **Remember** (`~R~emember`) allows that tool with those exact arguments for the
  rest of the run, in memory only. It is deliberately exact: a following call
  with different arguments is a different question.
- Every call leaves one transcript line saying who answered - `# auto: inside the
  project`, `# agent: runs the project's own tests`, `# allowed for this session`.
- File > **Undo file...** (`Alt-U`) lists the saved copies, interrupted writes
  first, newest next, with `Restore` and `Discard` and the journal's location and
  fullness in the header. Nine rows fit the box; what is not shown is in the
  journal directory the header names, not hidden. A restore is noted in the
  transcript, a declination is not - the transcript records what happened to a
  file.

## Measuring it: `--judge-check`

The judgement is invisible in the TUI, which makes it impossible to tune from the
TUI. This is the dry run:

```console
atui --judge-check 'make -C . test' 'cat ~/.ssh/id_ed25519'
git ls-files | atui --judge-check -            # one command per line
atui --judge-check tests/permission_cases.txt  # corpus, exit 1 on any mismatch
atui --judge-check --json -                    # JSONL, one object per decision
```

Nothing is executed - an allowing verdict is reported, not run. `--root DIR`
moves the project, because the same command is a different question in a
different tree.

`tests/permission_cases.txt` is 32 cases with the reasoning in comments. Measured
with the corpus, against `Ternary-Bonsai-2-27B` on llama.cpp: **32/32 as
expected**, 24 of them decided without any model call at all, the 8 judged ones
in 1.4-3.6s each. Against no server at all: the same 24, with the 8 judged ones
asking, which is the truth rather than a bug.

The last line of that sentence is the property the corpus exists to protect, so
the self-test parses the corpus file too: a check that stops parsing its input
quietly stops checking it, and still reports success.

## The decision log

`<config dir>/permissions.log`, one ASCII `key=value` line per decision, `cmd=`
last so it can be quoted without escaping the world:

```
2026-10-11T02:14:07Z tool=run_command decision=run rung=13 mode=agent agent=llama latency=2413ms reason="Runs the project's own test target via make" cmd="make -C . test"
```

Written on the deciding thread, open-append-write-close per entry (a handful a
turn, survives a crash), fields truncated - a summary can quote a file's
contents, and file contents never belong in a log. Rotated to `.1` past 1 MiB.
`log = false` turns it off, which is worth knowing: this file records what the
model wanted to do, including the sensitive paths it was refused.

## Known limits, stated plainly

- **Rung 6 is where the safety rests.** Commands with `$(...)`, backticks,
  `sudo`, `eval` or a pipe into an interpreter are never auto-approved and never
  judged. The guarantee is "auto-approval only happens for commands simple enough
  to read", not "no bad command ever runs".
- **Agent-claimed paths are unverified.** A write it reports that we cannot
  independently place inside the root is `prWriteOutside` - ask. The presence
  checks keep it away from commands whose paths it could invent.
- **`NET` is a model claim.** `cargo build` does fetch from a registry, and a
  model that says `NET no` will not be caught. That is why `network = ask`
  exists, and why exfiltration is decided from the *path* half, which is a fact.
- **In-tree writes are trusted**, including files the project executes later
  beyond `.git/hooks`: a `Makefile` or `setup.py` edit is auto-approved, and the
  `make` that follows is judged on its own.
- **Hardlinks are invisible** to realpath containment; symlinks are not.
- **Only `realpath` can say "the same file".** Containment compares paths, and on
  the platforms where atui cannot ask the filesystem what a name really means -
  Windows has no `realpath` here - two spellings of one file are two paths. That
  fails safe in the direction that matters: an 8.3 alias (`PROJEC~1\x.c`) is not
  recognised as the project's own `Project\x.c`, so it is a question rather than an
  auto-approval. A junction or reparse point *inside* the tree that leads out of it
  is invisible, exactly as a hardlink is.
- **A variable is a question.** `cp x $OUT/file` and `copy x %OUT%\file` name a
  location this process cannot see, and guessing that it was somewhere inside the
  project is the one error containment cannot survive. It costs a question whenever
  a person reaches for an environment variable in a command - which is rarer than
  it sounds once the agent is writing the commands, and the agent is asked to name
  paths rather than variables.
- **A fold that errs towards asking.** The deny-list and the command-shape checks
  match case-folded on every platform, and either separator bounds a component
  everywhere. A project file named `.ENV` or a directory called `powershell-notes`
  gets asked about on Linux too. That is the direction chosen deliberately: the
  alternative is `~/.SSH/id_rsa` being readable without a word to anybody.
- **The environment scrub matches names, not contents.** `API_KEY` goes, `TOKEN`
  goes, and a secret kept in `BUILD_FLAGS` is handed over as part of the compiler
  options it pretends to be. Names are the convention people and platforms agree
  on; values are not, and grepping values would hide half of `PATH` for no gain.
  Neither does it confine anything: the command still runs with the user's own
  permissions, which is the point of asking before it runs at all.
- **The deadline needs a process that can be stopped.** On Unix atui forks its own
  child, so it has a pid, a process group and a `poll()` to time out on. `cmd`
  hands back nothing to hold on to, so there the timeout is a line in the result
  rather than a stop, and `commandTimeoutIsEnforced()` is what the code says so
  with. A hung command on those platforms means killing atui, which the undo
  journal was written for.
- **One journal, no locking.** Two atui runs in the same install share a journal
  directory. Entry ids cannot collide, so nothing corrupts, but neither run knows
  about the other and each prunes by itself.
- **A restore can be blocked by other work.** A committed entry knows what the
  file held when atui finished with it, and refuses to restore over a file that
  has since changed: the copy stays where it is and the dialog says where that is.
- A busy single-slot server serializes the judge behind the chat request. The
  symptom is judge timeouts turning into prompts, never into allows.
- Every judged call costs a round-trip; the cache is what keeps a test-fix loop
  from paying it again.

## Deferred

- OS-level confinement (`sandbox-exec`, Landlock/bwrap) so in-tree auto-approval
  is a kernel guarantee instead of a policy. Ruled out as a *shipped* requirement
  rather than postponed: it cannot be honoured on MS-DOS, OS/2 or FAT, so it would
  be a promise atui breaks on half its targets.
- A real shell grammar (tree-sitter-bash) replacing the presence checks - the
  single largest upgrade available to the facts layer. Also why there is no
  per-platform equivalent yet: there is no single shell to parse (`/bin/sh` is
  dash, `command.com`, `cmd.exe`, PowerShell), and a parser only widens the
  auto-approve surface if the facts it produces are trusted.
- Resolving a name the way the operating system does on the platforms that offer
  an API for it (`GetLongPathNameW`, reparse-point queries), so `PROJEC~1\x.c` is
  recognised as the project's own file instead of being a question. Strictly a
  precision improvement: the current answer is the safe one.
- Flushing a *directory*, so that a journal entry created moments before the power
  goes is still there when the machine comes back. POSIX can do it and nowhere else
  meaningfully can, which is why nothing does it; the remaining two durability
  windows are in "The undo journal".
- Per-project permission files (`.atui/permissions`) and workspace trust.
- Persisting session allowances to config from the dialog.
