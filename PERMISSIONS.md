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
  `realpath`-resolved form has the root as a prefix or is the root itself. This
  is what makes symlinks out of the tree visible instead of surprising.
- **The deny-list.** Files that are never read or written unattended, in the tree
  or out: `.git`/`.hg`/`.svn`, `.ssh`, `.aws`, `.gnupg`, `.kube`, `.docker`,
  `.vscode`, `.idea`, `.config/gh`, `.config/gcloud`, `Keychain(s)`, `.mozilla`;
  dotenv files; shell rc files; `.pem` `.key` `.p12` `.pfx` `.kdbx` `.crt` `.der`
  `.secret`; `id_*` private keys; `tasks.json`.
- **Unverified surface.** Presence checks, not a shell parser: `$(`, backticks,
  `${`, `-exec`, `xargs`, `eval`, `base64`/`base32`/`xxd`, `sudo`/`doas`/`su`,
  and a pipe into an interpreter. Anything using one is never auto-approved *and
  never sent to the model* - it goes to the user, because it is exactly what a
  prompt injection writes and the answer cannot be read off the text. Ordinary
  work stays ordinary: `python3 -m pytest` is not "pipes into an interpreter".
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
  `dir/file`, `--out=/tmp/x`, and `@file` as curl spells a file argument) are
  extracted and resolved. `$HOME` is expanded; `$BUILD_DIR` is not guessed at, so
  an unexpanded variable is not treated as an escape.

**Judgement** (`src/net/permission_judge.{h,cpp}`) - one model call, for the
commands facts cannot settle. It answers what the call does; it is never told
whether it may do it.

## The ladder

`decide()` walks these in order. The first match wins, and every rung except
`prPathInside` and `prPlanCleared` means a question. The numbers are stable
identifiers, not positions, so an old log still means what it meant:

| Rung | Meaning |
|---|---|
| 1  `prModeAskAll`    | the mode says ask before everything |
| 2  `prRootTooBroad`  | the root is `$HOME` or `/` - containment proves nothing |
| 3  `prUnknownCall`   | a tool that reports no subject to check |
| 4  `prDenyList`      | credentials, hooks, editor task files |
| 5  `prPathInside`    | the file it touches resolves inside the project |
| 6  `prUnverified`    | shell surface refused to reason about |
| 7  `prPathOutside`   | a path we resolved lands outside the project |
| 8  `prNoAgent`       | nothing was asked, or there is nothing to ask |
| 9  `prPlanUnread`    | the agent's answer could not be used |
| 10 `prPlanFlagged`   | `elevated`, `sensitive`, or `exec` |
| 11 `prExfil`         | network together with a read outside the project |
| 12 `prWriteOutside`  | a write we cannot place inside the project |
| 13 `prPlanCleared`   | described, contained, nothing flagged |
| 14 `prSessionAllowed` | the user allowed this exact call earlier in the run |
| 15 `prPublish`       | publishes to another machine |

Two of them are worth dwelling on:

**`prRootTooBroad`** is a refusal to pretend. With `$HOME` as the project,
"inside the project" describes everything worth stealing, so nothing is allowed
unattended no matter what the mode says. Starting at `/` is the same.

**`prExfil`** is the one rule no setting can override. A plan that uses the
network *and* reads outside the root, or reads a deny-listed path, is refused by
code whatever the model said it was doing - the model's intent may be genuine and
the effect is still a secret leaving the machine.

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
Either
way `prExfil` outranks it.

## In the UI

- The status line shows `perm: ask_all | exact | agent`, live.
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
- A busy single-slot server serializes the judge behind the chat request. The
  symptom is judge timeouts turning into prompts, never into allows.
- Every judged call costs a round-trip; the cache is what keeps a test-fix loop
  from paying it again.

## Deferred

- OS-level confinement (`sandbox-exec`, Landlock/bwrap) so in-tree auto-approval
  is a kernel guarantee instead of a policy.
- A real shell grammar (tree-sitter-bash) replacing the presence checks - the
  single largest upgrade available to the facts layer.
- Undo for auto-approved edits: keep the prior contents, restore from the
  transcript.
- Per-project permission files (`.atui/permissions`) and workspace trust.
- Persisting session allowances to config from the dialog.
