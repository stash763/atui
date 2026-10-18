# atui Testing Notes

Working notes on how to build, run, and test atui — especially the TUI layer,
which needs a real terminal (pty) to exercise.

## Build & unit tests

    cmake --build build          # from repo root
    ./build/atui --self-test     # unit + integration tests (106/106 network build, 79/79 without)
    # ...plus 4 live-server tests when ATUI_LIVE_BASE_URL is set (see 'Live endpoint testing')

Env vars read by the app (see `src/net/oai_chat_model.cpp`, `src/tui/theme.cpp`,
`src/settings/settings.cpp`, `src/core/journal.cpp`):

- `ATUI_MODEL`, `ATUI_BASE_URL`, `ATUI_API_KEY`, `ATUI_TEMPERATURE`
- `ATUI_THINKING_RESEND` — `1`/`y`/`t` to replay an assistant turn's thinking
  to the model on later turns (default off; also a Providers-dialog checkbox).
- `ATUI_THEME` — `dark` (default) or `classic`; also switchable at runtime via
  `File ▸ Options ▸ Theme...` (Alt-T), which opens a radio-button dialog.
- `ATUI_REQUEST_MINUTES` — how long one request may take before atui gives up on
  it (default 120; also `File ▸ Options ▸ Settings...`, Alt-I). Unset, empty or
  unparseable keeps the default.
- `ATUI_CONFIG` — override the config file path (see Settings persistence).
- `ATUI_HOME` — where atui is installed, and so where its undo journal goes;
  unset, the directory of `argv[0]` is used. `ATUI_UNDO`, `ATUI_UNDO_DIR`,
  `ATUI_UNDO_MAX_BYTES` and `ATUI_UNDO_MAX_FILE` configure the journal itself (see
  `PERMISSIONS.md` and "The undo journal" below).

## Settings persistence (INI)

Settings are saved to / loaded from an INI file via a small helper
(`src/settings/ini.{h,cpp}`) plus thin glue that owns the schema
(`src/settings/settings.{h,cpp}`: `loadAll()` / `saveAll()` / `configPath()`).
The parts that *use* settings (`net/provider`, `tui/theme`) keep their normal
accessors and are untouched; `app.cpp` just calls `loadAll()` once at startup
and `saveAll()` when a dialog is committed.

Path precedence for `configPath()`: `$ATUI_CONFIG`, else
`$XDG_CONFIG_HOME/atui/config.ini`, else `$HOME/.atui.ini`, else `atui.ini`.
The file is written `0600` (it holds `api_key`); parent dirs are created `0700`.

Schema (one `[provider:<id>]` section per provider; `[app] provider` selects
the active one):

    [ui]
    theme = dark            # dark | classic
    thinking_expand = false # show reasoning blocks expanded at startup
    wheel_lines = 3         # lines one mouse wheel notch scrolls (1-30)

    [app]
    provider = ollama       # active provider name
    request_minutes = 120   # how long one request may take (1-1440)

    [provider:ollama]       # section name is lower-cased; `name` keeps the case
    name     = ollama
    base_url = http://127.0.0.1:11434/v1
    api_key  =
    model    = qwen3:30b    # model this provider uses
    models   = llama3.2,qwen3:30b  # cached model ids for this provider (from GET /models)
    temperature = 0.7
    thinking_resend = false # replay thinking to the model on later turns

An old single `[provider]` section is still read and becomes the provider named
`default`; on the next save it is rewritten in the new form.

Precedence: env vars seed the singletons first; a config file, when present,
overrides them (a key missing from the file keeps its env/default value).

Manual round-trip test:

    CFG=$(mktemp -u /tmp/atui_cfg_XXXX.ini)
    printf '[ui]\ntheme = classic\n' > "$CFG"
    ATUI_CONFIG="$CFG" tui-test run --cols 80 --rows 20 -- ./build/atui
    # Alt-T -> pick Dark -> OK; then quit. The file now reads "theme = dark".
    cat "$CFG"
    rm -f "$CFG"

## Switching models (Switch model menu)

One generated dropdown sits next to `File`:

    File   Switch model   About        (providers and theme are under File > Options)

* **Switch model** (`Alt-S`) — one entry per configured provider, labelled with
  the model *that provider is set to use*: `qwen3:30b  (ollama)`, the provider
  in use marked `*`. Picking an entry makes that provider active (its configured
  model comes with it) and writes the config. The list is deliberately **not**
  everything a server happens to offer: which model an endpoint runs is a
  property of the provider, so it is edited with the provider.
  Providers are listed in store order, which is alphabetical by section id (the
  INI helper keeps its sections in a sorted map) rather than file order.

The status line's left-most field always shows what a message is actually sent
to: ` ollama | qwen3:30b `.

Changing the model of a provider — including picking one of the ids a server
reports — happens in the Providers dialog: the `Model` field takes an id
directly, `Available models` + `Refresh` list the ones the server offers and
cache them on the provider (`models = ...` in the config, so they survive
restarts and the dialog does not have to re-fetch).

Implementation (`src/net/provider.{h,cpp}`): a process-wide store
(`providers()`, `activeProviderIndex()`) with `addProvider` /
`removeProvider` / `setActiveProvider[ByName]` / `findProvider` /
`uniqueProviderName`, plus `setModels` / `selectModel` for the model list.
`providerSettings()` still returns the **active** provider, which is what
`OaiChatModel` snapshots per request — so the request path did not change.
`addProvider` copies `defaultProviderSettings()` (env-seeded) under a unique
name; the last remaining provider cannot be deleted.

TVision notes for the generated dropdown (all verified live):

* A dropdown must be a `TSubMenu`; `subMenu + menuItem` appends to the **last
  top-level entry's own** menu, so a plain `TMenuItem` in that chain silently
  lands inside `File`.
* `TMenuItem::~TMenuItem` deletes `subMenu` only for `command == 0` items, and
  `TMenu::~TMenu` deletes its item chain — so refreshing is "assign a new
  `TMenu`, delete the old one" (`setSubMenu_`).
* `command == 0` also means "has a submenu" to `TMenuBox` (it draws a `►` and
  opens the submenu), so an entry that should only be informational needs a real
  no-op command rather than `0`.
* Generated entries use `CmdSwitchModelBase + index` and are handled in
  `TApp::handleEvent()` before the command switch; `kMaxSwitchModels` caps the
  list so the command range stays inside bounds.
* The app's own start-up pass is too late for the status line: `TProgram::init()`
  creates the desktop **before** the status line, so the provider label is filled
  in at the end of `initStatusLine_`.

Self-tests: `provider: store add/switch/delete`, `provider: model lists`,
`settings: provider round-trip` (two providers + active + delete-drops-section)
and `settings: legacy [provider] section`.

## Providers dialog (remote OpenAI-compatible endpoint)

`File ▸ Options ▸ Providers...` (shortcut Alt-F, then Alt-P; or Alt-P directly)
manages the provider list — every entry is an OpenAI-compatible endpoint such
as llama.cpp's `server` or Ollama:

* **Provider list** (top-left) — click to edit one; the active provider is
  marked `name *`. `+ New` appends a provider (based on the env-seeded
  defaults), `- Delete` removes the one being edited (the last provider cannot
  be deleted), `Make active` switches to the edited provider.
* **Name / Url / Key / Model / Temp / Resend thinking** — the fields of the
  provider being edited.
* **Available models** + `Refresh` — `GET <url>/models` for the URL/Key as
  typed; clicking a model selects it (and caches the list on the provider).

Clicking a provider row, a model row, or `New` / `Delete` / `Make active` acts
on the store **and the config file immediately** (the status line in the dialog
says what happened); the text fields are written on `OK`, and `Cancel` discards
them. Renaming via `Name` changes the config section id (a duplicate name is
made unique).

Everything lives in the process-wide store in `src/net/provider.{h,cpp}`,
seeded from `ATUI_BASE_URL` / `ATUI_API_KEY` / `ATUI_MODEL` / `ATUI_TEMPERATURE`.
`OaiChatModel::ask()` snapshots `providerSettings()` per request, so a switch
takes effect on the next chat message without a restart.

`provider.cpp` has no libcurl dependency and is always compiled, so the dialog
builds and runs with `ATUI_HAVE_NETWORK=OFF` too (verified in both configs;
without it the model list stays empty and says "Model list needs a network
build.").

> tui-test note: `TListBox` items come back **sorted** (`TStringCollection`), so
> a row number is not a store index — the row *text* identifies the entry
> (`providerRowToIndex_`). Harness clicks can also fire twice, which looks like a
> button acting twice.

## Live endpoint testing

The standard target is the lab **llama.cpp** server:

    base url  http://10.196.88.170:10001/v1
    model     Ternary-Bonsai-2-27B-PQ2_0.gguf     (reasoning + tool calls)

What its `/v1/chat/completions` stream looks like (matters to the parser):

- thinking arrives as `delta.reasoning_content` chunks *before* the answer
  (`{"choices":[{"index":0,"delta":{"reasoning_content":"…"}}]}`), which
  `deltaReasoning()` picks up (llama.cpp is the DeepSeek-style key; Ollama uses
  `reasoning`);
- tool calls are proper OpenAI-shaped `delta.tool_calls` fragments keyed by
  `index`, with `arguments` split across chunks, closed by
  `finish_reason: "tool_calls"`;
- `GET /v1/models` and `GET /health` answer as expected, so the model cache and
  the Providers dialog work against it; `/props` reports the model/build.

Ollama `0.14.3` at `http://10.196.88.167:11434` is the alternative target (used
for the multi-provider and model-cache checks):

- Endpoint discovery: `GET /v1/models` (OpenAI-compatible) lists the models;
  `GET /api/version` confirms Ollama.
- Streaming SSE chunks are
  `data: {"choices":[{"index":0,"delta":{"role":"assistant","content":"…"},
  "finish_reason":null}]}`, ending with `data: [DONE]`. Our parser
  (`json_extract.cpp:deltaContent`) handles this exactly (finds `"delta"` then
  `"content"`; treats `[DONE]` as terminator).
- Two-turn context (`conv_` carry-over) — see the bug fix note below.

### Automated live tests (`ATUI_LIVE_BASE_URL`)

`tests/oai_live_test.cpp` adds four tests that a mock cannot serve: that a real
model reasons, that a real `tool_calls` exchange round-trips, and that the
result reaches the model. They are registered **only** when the endpoint is
configured, so the default self-test stays offline and fast:

    ATUI_LIVE_BASE_URL=http://10.196.88.170:10001/v1 ./build/atui --self-test
    # -> live: model list / live: chat stream / live: reasoning / live: tool round trip
    #    110 passed, 0 failed (~30 s); offline runs stay at 106 passed

| test | what it asserts |
|---|---|
| `live: model list` | `GET <base>/models` on the real server yields ids, and one is usable |
| `live: chat stream` | a plain prompt streams non-empty answer content and ends cleanly |
| `live: reasoning` | thinking arrives tagged as thinking (`tkThinking`) *and* an answer follows |
| `live: tool round trip` | registers a `live_probe` tool returning a fixed string, and the model's final answer contains that string — so the call was parsed, executed, and its result used |

`ATUI_LIVE_MODEL` pins a model (default: the first one the server reports) and
`ATUI_LIVE_API_KEY` sends a bearer token. `live: reasoning` needs a reasoning
model; a wrong endpoint fails in seconds with a `self-test NOTE live: …` line
naming the server and model rather than hanging.

Run the TUI against it (env vars seed the provider defaults; `--cwd` matters
for `run_command` / relative `edit_file` paths):

    printf '[app]\nprovider = cpp\n\n[provider:cpp]\nname = cpp\nbase_url = http://10.196.88.170:10001/v1\nmodel = Ternary-Bonsai-2-27B-PQ2_0.gguf\ntemperature = 0.0\n' > /tmp/atui_cpp.ini
    tui-test run --session cpp --cols 88 --rows 26 --cwd /tmp/atui_live \
        --env ATUI_CONFIG=/tmp/atui_cpp.ini -- "$PWD/build/atui"

Verified with it: a riddle prompt renders `[+] thinking (228 chars)
click/Ctrl-T to expand` and expands to the model's step-by-step reasoning;
`Call the run_command tool with the command: uname -sr` produced a Confirm box
showing `$ uname -sr`, and after Alt-Y the answer quoted the real
`Darwin 27.0.0` **plus** a thinking block about the result.

Against Ollama (fast tiny model, or swap in a capable one):

    tui-test run --env ATUI_BASE_URL=http://10.196.88.167:11434/v1 \
                 --env ATUI_MODEL=smollm:135m \
                 --cols 90 --rows 24 -- ./build/atui
    tui-test write "…" ; tui-test key press Enter

The app always uses `OaiChatModel` when built with `ATUI_HAVE_NETWORK=ON`;
the env vars only seed the provider dialog defaults. You can also point at the
server purely through the dialog (`File ▸ Options ▸ Providers...`) with no env
vars — verified to work and to be picked up on the next message.

### Context carry-over bug (fixed 2026-10-05)

`OaiChatModel::conv_` was only appended on the worker's assistant reply; the
user turn was never committed, so every request after the first lost the
user's earlier messages (observed: model answered "Your name is User" instead
of recalling the name). `oai_chat_model.cpp` now commits user + assistant
together, only on a successful request, so `conv_` mirrors exactly what the
model has seen. Confirmed against Ollama: two-turn recall works.

## Tool calling

The model is offered a `tools` array (OpenAI function-calling schema) rendered
by `ToolRegistry::renderToolsJson()` (`src/tools/tool_registry.cpp`). Three
tools are registered by default:

- `get_time` (`src/tools/time_tool.cpp`) — no arguments; returns the machine's
  local date/time via `strftime`. Safe, no approval needed.
- `edit_file` (`src/tools/edit_file_tool.cpp`) — replaces an exact substring of
  an existing file: `{path, old_text, new_text, all?}` (first match unless
  `all=true`). Reports how many occurrences were replaced (and how many remain).
  **Requires approval.**
- `run_command` (`src/tools/run_command_tool.cpp`) — runs a shell command
  through `/bin/sh` (POSIX) / `cmd` (Windows), merging stderr into stdout and
  returning combined output (capped at 200 KB) plus the exit code:
  `{command}`. **Requires approval.**

### Confirmation prompt for sensitive tools

> Superseded in part: whether to ask at all is now a policy decision - see
> `PERMISSIONS.md` and "Permissions: modes, the judging agent, and the log"
> below. The handshake described here is what the question uses once the policy
> decides to ask it.

`edit_file` and `run_command` touch the file system / shell, so they set
`Tool::requiresApproval()` and the loop asks the user before running them. The
worker thread and the TUI handshake lives in `src/tools/approval.{h,cpp}`:

- The tool loop (`oai_chat_model.cpp`) checks `requiresApproval()`. If a
  process-wide `Approver` is installed, it builds an `Approval` (tool name + a
  one-line `approvalSummary()` preview) and blocks in `approve()` until
  answered. No approver, or a timeout (10 min), means **refuse** — the tool
  never runs and the model gets a "declined"/"no approval UI" result.
- The TUI installs a `TuiApprover` (`src/tui/app.cpp`). `approve()` runs on the
  worker thread: it publishes the request and blocks. `pollSink_()` (the 50 ms
  timer on the UI thread) picks it up and shows a modal Yes/No box; Yes runs the
  tool, anything else declines. A `g_approvalShowing` guard prevents a re-entrant
  dialog while the modal box pumps its own event loop.
- The box is a `TDialog` built by `showApprovalDialog_()`, **not**
  `messageBox()`: TVision's `makeRect()` sizes a message box for about three
  rows of 33 columns and `TStaticText` silently clips the rest, which cut off
  the one line that says what is about to run. The box wraps its message with
  the transcript's `wrapText_()` helper and sizes itself around it.
- Default button: `TButton::setState()` promotes the *focused* button to default
  (`makeDefault()` on `sfFocused`), and `TGroup::insert()` prepends, so a dialog's
  initial focus is the view inserted **last**. `Yes` is therefore inserted first
  and `No` second: `No` ends up focused and default, so **Enter and Esc both
  decline** and allowing takes a deliberate Alt-Y. The keystrokes are spelled out
  in the box text.
### What the transcript keeps of a tool call

The confirmation box disappears once answered, so the transcript carries the
record instead — a status line saying "calling run_command..." left no trace of
*what* had been run.

Each call is two tokens (`src/net/token_queue.h`): `tkTool` when the call is
made and `tkToolResult` when it finishes. The worker pushes the first **before**
approval, so every attempt is recorded, its outcome is filled in afterwards, and
a command that takes a while is visible while it runs:

```
$ echo hello from atui             tkTool: Tool::callPreview(args)
  exit code 0                      tkToolResult: the result string
  hello from atui
```

- `Tool::callPreview()` (`src/tools/tool.h`) decides how a call reads; it is the
  transcript's counterpart to `approvalSummary()` and may be multi-line:
  `run_command` shows the command itself (so a record looks like shell history),
  `edit_file` shows the file, `(first match)`/`(all matches)` and an
  `- old:` / `+ new:` pair. A tool that does not override it is recorded by
  name, and an unknown tool with its (clipped) raw arguments.
- Argument text is shaped by `src/tools/preview.h` (`previewClip()`): line
  breaks kept, tabs expanded, control bytes replaced, capped at
  `kMaxPreview` bytes, trailing blanks dropped — the model can put anything in
  an argument and the transcript still renders one sane row per line.
- The renderer (`relayout()`, `kTool` branch in `src/tui/app.cpp`) prefixes the
  first line with `$ ` and indents continuation lines under it; the result is
  indented two columns in a dimmer role (`rToolOut`).
- A collapsed record never takes more than `kToolRows` (5) rows: the command
  line first (it *is* the record, so it is never cut), then as much of the
  result as fits, then `.. N more lines (click to expand)`. Clicking anywhere on
  the record shows the rest, and `(click to collapse)`, like a thinking block
  (`lines_[].toggle` + the `kTool` case in the mouse handler). A record of five
  rows or fewer is simply shown whole, with no hint. While the record is open —
  no result yet — nothing else is drawn, which is what makes a running command
  readable without inventing a status.
- A decline is recorded too, and it is the only trace that anything was asked:

```
$ whoami
  The user declined to run 'run_command'.
```

`tool: call previews` in the self-tests covers the shaping rules; `oai: chat
tool loop` and `oai: tool approval allow/deny` check the token pair through the
mock server (their `drainKinds()` marks each token's kind so a record cannot be
confused with answer text).

Arguments are parsed with a small flat-JSON reader (`src/tools/json_args.{h,cpp}`)
that unescapes strings and skips nested objects/arrays, so values containing
braces, quotes or newlines are handled correctly.

The exchange is a loop in `OaiChatModel::ask()` (`src/net/oai_chat_model.cpp`),
all on the worker thread, capped at `kMaxToolRounds = 6`:

1. Send the request (with `tools`). The SSE stream is parsed for
   `delta.tool_calls` fragments and `finish_reason` (`json_extract.cpp`).
2. If `finish_reason == "tool_calls"`: for each call, look it up, push its
   transcript record (`tkTool` with `callPreview()`), then — if it requires
   approval — gate on the `Approver` (see above) and run
   `ToolRegistry::find(name)->execute(args)`. Push the outcome (`tkToolResult`),
   append the assistant turn (with its `tool_calls`) and a `role:"tool"` result
   message (or the decline message), then request again.
3. Otherwise stop — that content was the final answer. The whole exchange is
   committed to `conv_` only on success.

Streaming fragments arrive keyed by `index`: the first carries `id` +
`function.name`, later chunks append pieces of `function.arguments` (which may
be split mid-string). `parseToolCallFrags()` (string-aware scanner) and
`mergeToolFrags()` in `oai_client.cpp` reassemble them.

### Verification with a real model (through the TUI)

Run these against the llama.cpp endpoint from "Live endpoint testing" above
(`Ternary-Bonsai-2-27B-PQ2_0.gguf`) — it reasons *and* emits proper OpenAI
`tool_calls`, and the automated `live: tool round trip` test covers its core.
The table below was filled in against that model and, previously, against Ollama
`0.14.3` at `http://10.196.88.167:11434` with `ministral-3:8b-instruct-2512-q4_K_M`
(temperature 0) — both are good targets; the checks are the same. Recipe:

    mkdir -p /tmp/atui_tools && printf 'ALPHA stays here\n' > /tmp/atui_tools/notes.txt
    printf '[app]\nprovider = cpp\n\n[provider:cpp]\nname = cpp\nbase_url = http://10.196.88.170:10001/v1\nmodel = Ternary-Bonsai-2-27B-PQ2_0.gguf\ntemperature = 0.0\n' > /tmp/atui_cpp.ini
    tui-test run --session cpp --cols 88 --rows 24 --cwd /tmp/atui_tools \
        --env ATUI_CONFIG=/tmp/atui_cpp.ini -- "$PWD/build/atui"
    tui-test write --session cpp "Call the get_time tool."   # or run_command / edit_file
    tui-test key press --session cpp Enter                  # and Alt-Y / Enter to answer

Naming the tool and spelling out its arguments verbatim is what makes a small
model comply, e.g. `Call the run_command tool with the command: uname -sr` and
`Call the edit_file tool with path /tmp/atui_tools/notes.txt, old_text "...",
new_text "..."`.

| asked for | what happened |
|---|---|
| `get_time` | no prompt (safe tool) → record `$ get_time` with the time as its result → answer with the real local date/time |
| `run_command` `echo HELLO_FROM_TOOL`, allowed with Alt-Y | box showed `$ echo HELLO_FROM_TOOL` → record `$ echo HELLO_FROM_TOOL`, `exit code 0`, `HELLO_FROM_TOOL` → answer quoted the real output |
| `run_command` `seq 1 40`, allowed | record collapsed to 5 rows — `$ seq 1 40`, `exit code 0`, a blank, `1`, `.. 39 more lines (click to expand)`; clicking showed all 40, clicking again collapsed back to the same 5 rows |
| `run_command` `touch denied_marker`, declined with Enter | record `$ touch denied_marker` + `The user declined to run 'run_command'.`, **no file created**, answer said the action was declined |
| `run_command` `touch esc_test_file`, dismissed with Esc | same: treated as a refusal, recorded as declined, no file |
| `edit_file` on `notes.txt`, allowed | box showed path + `- old:` / `+ new:` → record `$ edit_file "notes.txt" (first match)` with both sides plus the tool's "edited … (replaced 1 occurrence)" line → file on disk changed |
| `get_time` **and** `run_command` in one turn | three rounds, two records (the shell one visible while it runs), then one answer containing both real results |

Clicking a row only does what you expect once the reply has finished streaming:
while the answer is still arriving the transcript grows underneath the pointer,
so a row read off an older screenshot is no longer the row the app sees.

Model notes (worth knowing before blaming the client):

- `tools` support is per model. `GET /api/show {"model":…}` lists `tools` in
  `capabilities`, but that is not the whole story: **`phi4-mini:3.8b` advertises
  `tools` yet, through the `/v1` OpenAI-compatible endpoint, writes the ChatML
  tokens into the answer instead of returning `tool_calls`** — the transcript
  then shows `[{"type": "function", ...}]u003c|/tool_call|u003e...` plus an
  invented tool response, and nothing is executed. `ministral-3:8b-instruct-2512-q4_K_M`
  behaves correctly; `mistral:latest`, `granite4:latest` and `nemotron-mini:4b`
  also advertise tools.
- A tool-capable model still decides: rephrasings of the same request sometimes
  get a direct answer with no tool call at all (nothing to see in the transcript
  then). Asking for the tool by name works best.
- Small models may ask a clarifying question first (e.g. "what is the full path
  of `notes.txt`?") instead of calling `edit_file`; the next turn then produces
  the call, which is also a check that tool rounds keep the conversation.
- Test in a scratch directory (`tui-test run --cwd /tmp/...`): `run_command`
  really runs and relative `edit_file` paths resolve against the client's cwd.
  Run one client at a time — a second client hitting the same server made a
  follow-up round stall for minutes in testing.

The same thing recorded from the request side (a debug build logging each
round) — "What is the current time?" against that model:

    [round 0] finish='tool_calls'  exec get_time({}) -> 2026-10-05 22:08:34 EDT
    [round 1] finish='stop'        "The current time is **10:08 PM EDT** …"

The scripted-server equivalent (a mock replying with `run_command`
`{command:"echo APPROVED_42"}`) is the way to test the box without a model: the
transcript shows `run_command: awaiting your approval…` while the worker blocks,
the box shows `$ echo APPROVED_42`, Alt-Y leads to `calling run_command...` and
the final answer, and refusing never prints `calling run_command...` — the loop
just continues with the "declined" result.

Unit coverage (`--self-test`): `finish_reason: parse`, `tool: call frags split`,
`tool: args with braces` (JSON with escaped quotes/braces in `arguments`),
`tool: get_time`, `tool: registry render` (now also checks `edit_file` /
`run_command` are registered and their `requiresApproval()` flags),
`tool: json args strings` / `bool/nested` / `looks object` (argument parser:
escapes, nested objects, braces-in-strings, key-not-a-value),
`tool: edit_file first/all` (first vs `all=true`, multi-line match),
`tool: edit_file errors` (not-found / empty old_text / missing file / bad args),
`tool: run_command basic` (stdout, non-zero exit, merged stderr, empty cmd).
The tool loop's approval round-trip is covered by the integration tests
`oai: tool approval allow` and `oai: tool approval deny`.

## OpenAI client integration tests (localhost mock server)

`tests/oai_mock_test.cpp` + `tests/mock_http.h` exercise the real request path
(`OaiClient::buildBody()` → libcurl → SSE parse → tool reassembly → and the
`OaiChatModel` tool loop) against a scripted HTTP/SSE server bound to an
ephemeral port on `127.0.0.1` — no external network required.

`MockOpenAiServer` records each full raw request (request line + headers +
body), so tests assert on exactly what went on the wire, and replies with the
next enqueued SSE body (HTTP 200, EOF-terminated). The accept loop exits when
the scripted responses run out (polled with a 100 ms tick), so it never blocks
teardown. Coverage (`--self-test`, network build):

- `oai: request shape` — method/path (`POST /v1/chat/completions`),
  `Authorization: Bearer …`, `Accept: text/event-stream`, and the model/stream/
  temperature/message fields in the body; `tools` absent when none offered.
- `oai: tools serialized` — body carries `"tools":[…]` and `"tool_choice":"auto"`.
- `oai: content stream` — multi-chunk `delta.content` assembled into the full
  answer; token callback and `OaiResponse::content` agree; `finish_reason:stop`.
- `oai: tool calls` — `delta.tool_calls` fragments with `arguments` split across
  chunks reassembled (`[1` + `,2]` → `[1,2]`); id/name/finish_reason captured.
- `oai: message serialization` — an assistant tool-call turn serializes with
  `"content":null` + `"tool_calls":[…]`; a `role:"tool"` turn carries
  `"tool_call_id"`/`"name"`.
- `oai: connection refused` — request to a closed port returns `false`.
- `oai: chat tool loop` — `OaiChatModel::ask()` runs the full round-trip:
  round 0 returns `get_time` (status `~ calling get_time` is pushed to the
  queue), the tool executes, and round 1's request replays the assistant
  `tool_calls` + `role:"tool"` result; final answer reaches the queue.
- `oai: chat context carry` — a second `ask()` replays the prior user +
  assistant turns plus the new user turn (guards the `conv_` carry-over fix).

These register via `atui_test::init_network_tests()` (called from
`selftest.cpp` under `#ifdef ATUI_HAVE_NETWORK`); the file compiles to nothing
in non-network builds (verified: 40/40 there, 60/60 with network).


## Model picker (Providers dialog)

(The quick switcher is the **Switch model** menu on the menu bar — it offers one
entry per provider, see "Switching models" above. This dialog's list is the
discovery view: it shows what a server reports and caches it on the provider.)

`File ▸ Options ▸ Providers…` shows an **Available models** list box
populated from the server via a synchronous `GET <baseUrl>/models`
(OpenAI-compatible; works on Ollama and llama.cpp). Implementation:

- `oai_client.cpp:fetchModels()` does the blocking GET (short timeouts:
  10s connect / 20s total) and reports a short error string on failure.
- `json_extract.cpp:modelIds()` pulls `data[].id` in server order; a
  payload without a `data` array yields no ids.
- The widget is a tvision `TListBox` + `TScrollBar` (the tvdemo
  fileview/listdlg pattern). `ModelListBox` (a `TListBox` subclass) fires
  `selectItem()` on a **single** click — clicking a model copies its id into
  the **Model** field immediately. **Refresh** reloads the list from the
  URL/Key fields *as currently typed*, so you can browse before saving. The
  collection is a `TStringCollection` (sorted, owns/frees its strings).
- The list auto-loads when the dialog opens; the status line shows
  "`N model(s) loaded.`" or "`Refresh failed: <reason>`". Without the network
  build it shows "`Model list needs a network build.`" and the list stays
  empty.

**Verified live** (real Ollama server): opening the dialog lists all installed
models; clicking one updates the Model field (e.g. `llama` →
`granite3.2-vision:2b-q8_0`).

> tui-test quirk: it delivers mouse `where.y` as the absolute screen row, so a
> harness click can select the neighbouring row; native `TListViewer` mapping
> is correct in real terminals. Selection is confirmed by watching the Model
> field change after `tui-test mouse click <x> <y>`.

Self-tests (network build): `oai: model ids parse` (list/empty/no-data-array,
id-outside-array ignored) and `oai: fetch models` (round-trips a mock
`GET /v1/models`, asserts the request line + parsed ids, empty URL errors).


## Markdown & code highlighting

AI answers are rendered as Markdown with syntax-highlighted fenced code blocks.
Rendering is **span-based**: every row is a vector of spans, each carrying a
`Role` that `TranscriptView::mapColor()` turns into a theme-aware colour (both
dark and classic schemes are covered).

Modules (all network-independent, always built):

- `src/tui/md_style.h` — `Role` enum + `Span` / `Line`.
- `src/tui/syntax.{h,cpp}` — `detectLang()` + `highlightLine()`; tokenises
  keywords / types / strings / chars / comments / numbers / functions /
  preprocessor / operators. Carries `SynState` across lines so block comments,
  triple-quoted strings and JS template literals highlight correctly.
  Languages: **C/C++, Python, JS/TS, JSON, Bash, Go, Rust**; unknown languages
  fall back to plain code (monospace colour, no per-token colouring). JSON keys
  are coloured distinctly from string values.
- `src/tui/markdown.{h,cpp}` — `render(md, width)` — block parser (ATX headings,
  fenced code, bullet / ordered / task lists with nesting, blockquotes, thematic
  breaks, GFM pipe tables, footnote + link reference definitions) and inline
  parser (strong / emphasis / strike / inline code / links / autolinks / images
  / footnote markers) plus a span-aware word wrapper that keeps styling across
  wraps. Link URLs are hidden (only the label shows, underlined). A still-open
  fence (mid-stream) renders its body.

Notes: width math is byte-based (1 byte = 1 column), so borders use ASCII
(`|`, `-`) rather than box-drawing glyphs. Indented (4-space) code is treated as
prose, not code (only fenced blocks are highlighted). Each laid-out `Line`
carries a `bg` role; code rows use `rCode` so `TranscriptView` fills the code
background across the **full row width** (edge-to-edge), which keeps short and
blank lines inside a fence reading as one solid block instead of patchy.

**Verified live** (mock SSE server serving a markdown answer, coloured
screenshot via `tui-test screenshot`): heading colour + bold, `**bold**` /
`*italic*` / `` `code` `` styling, underlined link with the URL hidden, bullet +
nested + ordered list markers, blockquote bar, an aligned GFM table, and a
python code block with keyword / function / comment / number / operator colours
on the darker code background. The code background was confirmed to fill every
row of the block to the full widget width (including a blank line inside the
fence and a short `x` line), while inline code stays a small chip on a normal
background row and prose keeps the normal background.

Self-tests (16, in `tests/markdown_test.cpp`, run in **both** configs): heading,
strong/em/code, link, bullet / nested / ordered lists, blockquote, wrapping,
table, cpp + python code-fence highlighting, unclosed fence, code-background
role + blank-line preservation, `detectLang`, block-comment state carry-over,
and JSON key colouring.


## Reasoning / "thinking" display

Some models emit a reasoning ("thinking") stream alongside the answer. The
client reads it from the streaming delta under either key — Ollama sends
`reasoning`, DeepSeek-style servers send `reasoning_content`
(`json_extract.cpp:deltaReasoning`). Tokens are routed through the UI queue as
a typed `Token` (`tkThinking` vs `tkContent` vs `tkStatus`), so the transcript
can render each distinctly.

The transcript is a custom `TranscriptView` (a `TScroller` subclass in
`app.cpp`), not a `TTerminal`, so it can color items and hold collapsible
blocks. Behavior:

- Reasoning arrives as a **collapsed block** by default:
  `[+] thinking (57 chars)  click/Ctrl-T to expand`. The full text is buffered
  even while collapsed.
- Click the block (mouse) or press **Ctrl-T** (global) to expand/collapse.
  Ctrl-T toggles *all* blocks; a click toggles just that one. Expanded it shows
  `[-] thinking (N chars)  click/Ctrl-T to collapse` plus the indented text.
- Colors (via `TranscriptView::mapColor`): answer = yellow, thinking text = dim
  slate, thinking header = cyan, user = green, tool status = amber (dark theme;
  distinct BIOS attrs in classic).
- **Fenced code blocks inside reasoning are recognized.** Models routinely think
  *in* code — a snippet they are about to write, a command, a diff — and as
  prose that is unreadable. `tui/thinking.cpp:splitThinking` cuts the reasoning
  into prose and `` ``` ``/`~~~` segments (the fence lines themselves are not
  shown); `renderThinkingBody` lays the prose out as plain `rThinking` text and
  hands each code segment to the markdown/syntax pipeline, so it gets exactly
  what an answer's code block gets: highlighting plus the code background
  filling the row. Everything stays indented three columns, so the block still
  reads as one piece. Nothing else in reasoning is interpreted — `**bold**` and
  `` `inline` `` stay literal, only fences are. An unterminated fence (normal
  mid-stream) renders as code to the end, and a fence whose body contains
  backticks is re-fenced one character longer, so the snippet cannot close
  itself.
- Scrolling: mouse wheel, or click/drag the vertical scrollbar; auto-follows the
  bottom while streaming until you scroll up.
- Whether thinking is stored and replayed to the model on later turns is the
  `thinking_resend` option (default off). When on, the assistant turn carries
  `reasoning_content` on subsequent requests (`oai_client.cpp:buildBody`).

**Verified live** against the llama.cpp endpoint (`reasoning_content` deltas
from a real thinking model) and against the localhost mock SSE server: a question
renders `[+] thinking (228 chars)  click/Ctrl-T to expand`, and expanding shows
the model's step-by-step reasoning above the answer.

To see a code block inside reasoning deterministically (whether a real model
puts one there is up to the model), serve a canned SSE response whose
`reasoning_content` contains a fenced block and point a provider at it:

```python
# fake_sse.py - reasoning with a ```c block, then an answer
import json
from http.server import BaseHTTPRequestHandler, HTTPServer

REASONING = ("Let me write the helper first:\n```c\n"
             "int sum(int n) {\n    int s = 0;   // accumulate\n"
             "    return s;\n}\n```\nand that is it.\n")

class H(BaseHTTPRequestHandler):
    def do_POST(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.end_headers()
        emit = lambda o: self.wfile.write(b"data: " + json.dumps(o).encode() + b"\n\n")
        emit({"choices": [{"index": 0, "delta": {"role": "assistant",
                                                 "reasoning_content": REASONING}}]})
        emit({"choices": [{"index": 0, "delta": {"content": "Answer: 30.\n"}}]})
        emit({"choices": [{"index": 0, "delta": {}, "finish_reason": "stop"}]})
        self.wfile.write(b"data: [DONE]\n\n"); self.wfile.flush()
    def log_message(self, *a): pass

HTTPServer(("127.0.0.1", 8799), H).serve_forever()
```

with `base_url = http://127.0.0.1:8799/v1` and `[ui] thinking_expand = true`.
Checked with `tui-test cells` (it reports the truecolour the app emits, unlike a
screenshot): the prose rows carry the normal background `#152238`, the code rows
the code background `#0c1626` edge-to-edge — including the three-column indent —
with `int` in the keyword colour, `sum` as a function, `0` as a number and
`// accumulate` as a comment, and the prose after the block back on normal.

> Note: `tui-test key press Ctrl+T` does reach the app (verified 2026-10-10; an
> older note here said Ctrl-T could not be injected — that was with an older
> harness/key syntax). Mouse clicks work too and toggle a single block. Enter
> itself is delivered as `evCommand` (kbEnter) in this harness, so
> `handleEvent` accepts both the key-down and command forms.

Test coverage (`--self-test`, network build):
`oai: delta reasoning parse` (both keys + null), `oai: reasoning stream`
(content/reasoning separated, callback routing), `oai: chat thinking` (queued
as `tkThinking`/`tkContent`), `oai: chat resend off/on` (the `reasoning_content`
is replayed only when the option is on), and `live: reasoning` against a real
reasoning server. The segmentation and layout have their own tests, offline and
in both builds: `thinking: prose only`, `thinking: code between prose` (info
string, indent, fence lines dropped), `thinking: unclosed fence`,
`thinking: fence length rules` (a shorter run cannot close a block; a fence line
with a trailing word is content), `thinking: fenced markdown renders` (the
re-fence grows past backticks inside), `thinking: body prose` (markdown is *not*
interpreted in reasoning), `thinking: body code block` (rows, roles, backgrounds
and the three-column indent) and `thinking: body unclosed fence`.


## Mouse wheel scrolling (Options > Settings)

One wheel notch moves the transcript `[ui] wheel_lines` lines - **3 by default**,
clamped to 1-30 (`settings::clampWheelLines`, which every value passes through:
config file, dialog, or an edit by hand). The wheel is where most scrolling
actually happens, so it is the one scroll setting worth a dialog; PgUp/PgDn and
dragging the bar keep TVision's own steps.

`File ▸ Options ▸ Settings...` (Alt-I) edits it - see "The Settings dialog"
below. No restart needed either way: the transcript reads the value on every
wheel event.

Verified in the TUI against a transcript of numbered rows - a streamed answer of
`ROW 001..ROW 080` inside a fenced block, so each row is exactly one line - with
`wheel_lines` 1 / 3 / 5 / 30: one notch moved the top row back by 1 / 3 / 5 / 30
rows.

Gotchas found doing it:

- `TScrollBar` claims `evMouseWheel` too and scrolls by its *own* arrow step,
  which nothing in the app could tune. Wheel events are not routed by pointer
  position (`TView::positionalEvents = evMouse & ~evMouseWheel`), so `TGroup`
  offers one to every view that claims it, and `TranscriptView` - iterated
  before the bar, because `TGroup::insert` prepends - handles it and
  `clearEvent()`s it. Drop that clear and the bar's fixed step wins.
- `tui-test mouse scroll up|down` sends **3** wheel events by default; use
  `--amount 1` to move a single notch.
- `tui-test key press NAME` types the name literally when it does not know it:
  `key press ArrowDown` typed `ArrowDown` into whatever had focus (`Down` is the
  key). Read "arrow keys insert junk" as a harness artifact until checked twice.
- `TStaticText` clips rather than wraps, so dialog copy has to be counted out
  against the dialog's width - and the settings range belongs in that copy
  anyway.


## How long a request may take (Options > Settings)

The whole request has to finish inside one curl timeout - connect, thinking, and
every streamed token - and a model that is still reasoning holds the connection
open the entire time. That limit used to be a hard-coded 300 s, which is how a
long prompt "appeared to time out": nothing had gone wrong, the wait was just
shorter than the model.

Now it is `[app] request_minutes` - **120 by default**, clamped 1-1440 by
`net::clampRequestMinutes`, which every value passes through (config file,
dialog, `ATUI_REQUEST_MINUTES`, hand edit). It bounds **one request**, so a tool
round is a request of its own and gets the whole wait again; the app has only one
in flight at a time. Read per request, so a change applies to the next prompt.
Two other waits stay short on purpose - they mean a dead socket rather than a
thinking model: `CURLOPT_CONNECTTIMEOUT` (30 s) and the model-list GET (20 s).

A failed request is also finally *reported*: the transcript gets a status line
with the reason, naming the limit and where to change it,

    request failed: gave up after 1 min waiting for the server (Options > Settings)

where before the reply simply stopped and it looked like the app had hung up.
`OaiClient::request()` takes an optional `err` out-param for this
(`curl_easy_strerror()` for everything that is not a timeout).

`File ▸ Options ▸ Settings...` (Alt-I) edits the wait - see "The Settings dialog"
below.

Verified with a canned SSE server that sleeps 90 s before sending its first
byte: `request_minutes = 1` cut the request off at exactly 60 s with the timeout
status line above; `request_minutes = 2` let the same answer arrive normally.

Test coverage (`--self-test`, both builds): `settings: request minutes` - the
default (120) and its being inside the range, the clamp at both ends, reading
`[app] request_minutes`, saving it back, an out-of-range value read as the
nearest wait that works, and a missing or unparseable key leaving the wait
already in force.


## The Settings dialog (File > Options > Settings)

The numeric preferences live in one dialog (`SettingsDialog` in `src/tui/app.cpp`,
opened by `TApp::showSettings()` on `CmdSettings`, Alt-I):

```
┌─────────────────────── Options - Settings ───────────────────────┐
│                                                                  │
│  Mouse wheel, lines/notch:    3      1 to 30                     │
│                                                                  │
│  Request timeout, minutes:    120    1 to 1440                   │
│                                                                  │
│  Before running something:                                       │
│     ( ) Always ask before a tool runs (ask_all)                  │
│     ( ) In-project work is fine, ask outside (exact)             │
│     (•) A model judges what the facts cannot settle (agent)      │
│                                                                  │
│  Judging model:                                                  │
│  Judging seconds:             5    1 to 60                       │
│  Command timeout, sec:        300    0 to 86400, 0 = none        │
│                                                                  │
│  One wheel notch scrolls the chat by that many lines; the        │
│  request timeout is per request, so a tool round gets it         │
│  again. A command is stopped at its timeout along with           │
│  everything it started, and 0 there means no limit. A            │
│  field with no number in it keeps the value it has.              │
│                                                                  │
│  Judging is by a second model; blank uses the chat model.        │
│                      OK    ▄      Cancel   ▄                     │
│                   ▀▀▀▀▀▀▀▀▀▀    ▀▀▀▀▀▀▀▀▀▀▀▀                     │
└──────────────────────────────────────────────────────────────────┘
```

Plain entries, nothing to pick from. Scrolling and the request wait each had
their own menu entry and their own dialog with radio-button presets, which meant
two dialogs to remember and a short list of guesses about how fast a wheel should
be or how long a model may think. What is left is the number: **each field prints
the range it accepts**, since there is no longer a list to imply it.

The box is 25 rows tall because DOS text mode is 25 rows, and 25 rows is the whole
screen: a field added here has to come out of the explanation text, not grow the
dialog. `pass_env` is not in it either, and not for want of room - see the note
under `[tools]` in `PERMISSIONS.md`.

The rules, all of them per field:

 - **OK** applies every field, each clamped into the range it prints (5000 minutes
   becomes 1440, 99 lines becomes 30, 99999 seconds becomes 86400), then saves.
 - A field with no digits in it - cleared by mistake - keeps the setting it has,
   so a half-edited dialog loses nothing rather than jumping to an extreme.
 - **The exception is `0`**, which is a value rather than an absence: an empty
   command timeout keeps the limit it has, and a `0` in it means no limit. It is
   the only field where the two differ, which is why the note says so.
 - Cancel/Esc applies nothing at all.
 - The wheel step takes effect on the next notch (the transcript reads the value
   per event); the request wait on the next request; the command timeout on the
   next command. None needs a restart.

Adding a setting means one label, one field, one range, and one line in
`showSettings()` - which is the point of keeping them in one place. A value a
16-bit `int` cannot hold (the command timeout goes up to a day's seconds) uses
`SettingsDialog::longIn` rather than `numberIn`.

Gotchas found doing it:

 - A `TInputLine`'s constructor limit **counts the terminating NUL**, and the
   widget spends its **first and last column on scroll arrows**. So `n` digits
   need a limit of `n + 1` and a field `n + 2` columns wide; asking for 4 digits
   in a 5 column field let only 3 through, silently - `1440` could not be typed
   and `1441` came back as `144`. The fields here hold 5 digits in 7 columns, and
   `SettingsDialog`'s constructor says so where they are built.
 - Backspace in a `TInputLine` deletes *before the caret*, and a click puts the
   caret where it was clicked rather than at the end (no select-all on click). A
   test that clicks mid-value and backspaces twice is not clearing the field -
   press End first.
 - `tui-test` clicks on a field: aim at its text, not its leftmost column, and
   confirm with `state` (the cursor is `visible: true` only once the field has
   focus) before concluding the dialog is broken.

Verified with `tui-test` at 80×25, which is the smallest screen this dialog has to
fit: 4-digit `1440` typed and stored; `5000` → `1440` and wheel `99` → `30` through
the clamp; one OK applying both fields (45 and 6); an empty wheel field leaving
`wheel_lines = 30` while the timeout in the same dialog was applied; Escape leaving
both alone; and `File ▸ Options` showing Providers / Settings / ─ / Theme with the
two old entries gone. The command timeout field the same way: `7` stored, `99999`
stored as `86400`, `0` stored as `0`, and an emptied field leaving the saved `42`
untouched - the three answers a field like this can be asked and the one people get
wrong.


## Permissions: modes, the judging agent, and the log

The policy itself - the ladder, the deny-list, the agent contract, the config
keys - is in `PERMISSIONS.md`. This section is how it is checked.

### Offline, both builds (no model, no network)

`decide()` is pure function of (mode, root, subject, plan), so the entire truth
table is a unit test:

```console
./build/atui --self-test 2>&1 | grep permission
```

Covering: containment (relative, absolute, `..` escape, symlink out of the tree,
symlink inside pointing inside, macOS `/tmp` realpath, the root itself, a path
whose parent does not exist), the same in the notation of the other platforms
(`C:\Windows\win.ini`, `\\server\c$\notes.txt`, a drive-relative `C:notes.txt`,
`..` collapsing against the drive, `C:` and `\\srv\share` as roots too broad to
contain anything, and containment under a Windows-shaped root with case folding
switched on and off - `tests/selftest.cpp`'s `core: path shapes` and
`permission: paths`), the deny-list in either separator and either case, the
presence checks *and their false positives*, `rootTooBroad`, `publishesOutside`
both directions (`git push` yes, `git pushed` and `npm run build` no), every plan
flag against a fake judge, exfil outranking `network = allow`, the cache hit/miss
and prompt-version invalidation, the `[permissions]` round-trip and clamps, and
the log line's shape.

Platform path shapes are checked on whatever host runs the tests, which is the
point of answering them independently of the host: `pathAnchor` and
`pathIsUnder` are tested as data, and `core::setPathsAreCaseInsensitive()` is the
one switch a test flips (and restores) rather than a `#ifdef` that leaves the
Windows half of a change untested until someone cross-compiles it.

The loop around it - policy, then approver, then the transcript note - runs
against the localhost mock server (`tests/oai_mock_test.cpp`): exact edits in
tree run, commands ask, an agent that clears a command clears it, one answer per
call, a flagged effect asks, an unreadable reply asks, and `Remember` is exact to
the arguments.

**Every failure shape must produce a question, never an allow.** That is the
property these tests exist for; a change that makes an unreadable answer mean
"run" should be treated as a vulnerability, not a flake.

### The dry run: `--judge-check`

Nothing is executed, so it is safe on a corpus of `rm -rf`:

```console
atui --judge-check 'make -C . test' 'cat ~/.ssh/id_ed25519'
atui --judge-check tests/permission_cases.txt      # exit 1 on any mismatch
atui --judge-check --json -                        # JSONL
atui --judge-check --root /some/other/tree 'cat ../x'
```

With no server configured, the facts-decidable lines still report correctly and
every judged line reports `rung 8/9` - which is the honest answer, not a broken
tool. The header prints the root, the mode and the agent it would ask, so a run
that quietly used the wrong config is visible in the first line.

Malformed corpus lines are an error with a file:line, and the self-test parses the
corpus too: the runner *skips* what it cannot parse, so without that guard a
typo turns 43 checks into 42 and the run still says it passed.

### Live, with a real model

```console
ATUI_CONFIG=/private/tmp/none.ini \
ATUI_BASE_URL=http://10.196.88.170:10001/v1 \
ATUI_MODEL=Ternary-Bonsai-2-27B-PQ2_0.gguf \
ATUI_JUDGE_SECONDS=60 \
./build/atui --judge-check tests/permission_cases.txt
```

Last run: 43/43, 31 of them without a model call, judged calls 1.4-3.6s. Kept out
of the default suite like the other live tests.

The corpus deliberately mixes notations - `.git/config` and `.git\config`, `~/.ssh`
and `%USERPROFILE%\notes.txt`, `/etc/hosts` and `C:\Windows\win.ini` - because it
is run on one host and has to be right for all of them. A line whose expectation
depends on the machine running it does not belong in it; those go in the self-test,
where the switch can be set (see `core: path shapes`).

Add `ATUI_JUDGE_DEBUG=1` to see the raw reply for every judgement on stderr - the
only way to tell "the model said EXEC yes" apart from "the model returned a code
fence" or "the model thought for 300 tokens and said nothing". Prompt wording is
tuned by watching that against the corpus, and `judgePromptVersion()` must be
bumped whenever the question changes, or stale cached plans answer under a prompt
they were not produced by.

Two model behaviours worth remembering when a corpus run goes wrong: a reasoning
model needs thinking switched off or it spends the whole budget before answering
(`OaiRequest::disableThinking`), and the answer only arrives over SSE - a
non-streaming request parses as empty content.

### Through the TUI (tui-test)

Verified with `tui-test`: in-tree `echo in-project` runs unattended with the
record `# agent: prints a greeting for the user` and a matching
`permissions.log` line; a plan flagged `ELEVATED` raises the dialog quoting the
reason; Alt-Y runs it, Enter and Esc both decline; clicking `~R~emember` allows
it and the *same* call again shows `# allowed for this session` with no dialog,
while a different command still asks; Settings ▸ mode radio persists
`mode = exact` and the status line changes to `perm: in-project ok, asks
outside` without a restart. With no `[permissions]` section in the file at all,
the status line reads `perm: in-project ok, a model checks the rest` - the
shipped default, checked by starting with a config that says nothing.

## The undo journal

Design and the storage format are in `PERMISSIONS.md`; this is how it is checked.

### Offline, both builds

```console
./build/atui --self-test 2>&1 | grep -E "journal|undo"
```

The crash is not simulated by killing anything: `begin()` without `commit()` *is*
the state a crash leaves behind, and the journal's whole contract is what happens
when it is reopened in that state. Killing the process would add nothing and make
the test unportable, so the tests abandon entries on purpose.

Covering: the pre-image is on disk before the target is touched; committed entries
are not pending and record both hashes; an abandoned entry is found by
`interrupted()` and restores the exact bytes, consuming the entry; a committed
entry refuses to restore over a file that changed since; `cancel()` leaves nothing;
`compareTarget()` distinguishes "nothing to recover" from "bytes I cannot
account for" (the difference between a quiet cleanup and asking a person); a file
over `max_file` gets no copy and `canBackUp()` says why in bytes; a journal
switched off is a decision and not a failure; retention drops the oldest entries
but never an interrupted one and never the newest; `.INF` files with no `.PRE`,
and `.PRE` files with no `.INF`, are handled instead of crashing; the `[undo]`
section round-trips (through `getLong`, since 1 MiB does not fit a 16-bit `int`);
and an `edit_file` through the real tool leaves an entry that reproduces the file
byte for byte.

Policy: an in-tree path is `prPathInside` while the journal covers it and
`prNoUndoNet` when it does not; a journal switched off asks nothing; and a path
outside the tree is still decided by containment first, so the journal never gets
to make an out-of-tree write acceptable.

The tool-side tests own their journal directory (`JournalGuard`,
`ScratchProject`'s `undo/`), because the default is the install directory and a
test that litters it fails differently depending on where it was built.

```console
./build/atui --self-test 2>&1 | grep "flush to disk"
```

Durability is the promise the ordering is for, and it cannot be tested honestly:
the test that proves a byte reached a platter involves a power lead and a VM. What
`core: flush to disk` checks instead is the two things that break in practice -
that a file pushed through `core::flushToDisk()` reads back whole by another
route, and that `canFlushToDisk()` still reports true on the platforms whose call
the durability section of `PERMISSIONS.md` is written around. A lost `#elif` is how
a promise silently becomes a different promise.

### Through the TUI (tui-test)

The journal is plain files, so a fixture can be written by hand - this is the
recipe, and it is the only way to get an "interrupted" entry without dying:

```console
mkdir -p /tmp/und/proj /tmp/und/journal
printf 'current\n'  > /tmp/und/proj/notes.txt
printf 'the old one\n' > /tmp/und/journal/U000007.PRE
cat > /tmp/und/journal/U000007.INF <<'EOF'
atui-undo v1
stamp=2026-10-13T09:14:02Z
pid=4242
tool=edit_file
op=replace:first
state=pending
existed=1
target=/tmp/und/proj/notes.txt
hash=0000000000000000
bytes=12
after=
afterbytes=0
EOF
tui-test run --session undo --cols 88 --rows 25 \
  --env ATUI_CONFIG=/private/tmp/none.ini \
  --env ATUI_UNDO_DIR=/tmp/und/journal --cwd /tmp/und/proj -- ./build/atui
```

Verified with it: the **Interrupted write** box appears as soon as the desktop is
up, naming the file, what it holds now versus what the copy holds, and where the
copy is; Esc leaves the file alone, notes it in the transcript and keeps the entry
for the menu. `Alt-U` lists entries newest-first with interrupted ones first and
`INTERRUPTED` on their row, the header reading
`<dir> - 4 saved, 75 B of 1.0 MiB, 1 interrupted`. `Restore` on an ordinary
in-tree file writes it back with no further question. `Restore` on an entry whose
target is `.env` raises the policy's own dialog quoting the rule
(`never done unattended (a dotenv file): /tmp/und/proj/.env`), Enter declines and
leaves both files alone, Yes restores. `Discard` asks first - it destroys the only
copy - and its No leaves the entry.

Gotchas found doing it:

- `tui-test mouse click` and `tui-test find text` use **0-based rows**, so the
  row in `find`'s output is the row to click. Resolve them immediately before
  clicking: these dialogs are centred, so one more transcript line moves every
  button up a row, and a click a row out lands on a button's shadow. Clicking below the last
  item of a `TListViewer` is not ignored either: it clamps to the last item, which
  in a list where every row is "a file somebody is about to overwrite" looks
  exactly like acting on the wrong entry. Count rows with `find text`, do not
  eyeball `tui-test text`, and never reuse coordinates from an earlier screen.
- A `TDialog` subclass has to intercept its own command ids **before** calling
  `TDialog::handleEvent`, because those four ids (`cmOK/cmCancel/cmYes/cmNo`)
  close the dialog. Custom ids (`CmdUndoRestore`) plus `clearEvent` keep it open
  so the list can be refreshed and the outcome shown in the same box.
- `TStaticText` reserves the rows it was given at construction: a header that
  wraps onto a second line needs the second line in its `TRect` and the dialog one
  row taller, or the tail of the path is silently gone (see the same note under
  the wheel-scrolling section).
- The startup offer is triggered from the periodic `cmTimerExpired` branch, not
  from `initDeskTop_()`: during init `TProgram::deskTop` is still null, and a
  dialog executed on `TProgram::application` takes keyboard but no mouse.

## What a command atui runs gets

`run_command` is the one tool that runs something the model wrote, so the child is
set up deliberately rather than inherited from `popen()`. The policy is in
`PERMISSIONS.md` under "Running a command"; this is how it is checked.

```console
./build/atui --self-test 2>&1 | grep -E "child environment|deadline|signals|tools section"
```

- `core: child environment` - the name patterns (`ATUI_API_KEY`, `GH_TOKEN`,
  `AWS_SECRET_ACCESS_KEY`, `DB_PASSWORD`, `svc_credentials` in; `PATH`, `HOME`,
  `CC`, `MAKEFLAGS` and `SSH_AUTH_SOCK` out), the `pass_env` list matching case and
  stray spaces, the block `ChildEnvironment` builds for `execve()`, and
  `HiddenCredentials` taking a variable out and putting it back.
- `run_command: environment and deadline` - a real command run through the tool
  with a fake `ATUI_SELFTEST_SECRET_KEY` in the environment: the output must show
  `[]`, never the value; with the name in `pass_env` it must show the value. Then,
  where the deadline can be enforced, that `cat` returns `exit code 0 (no output)`
  instead of waiting for a terminal, that `sleep 30` with `command_timeout = 1`
  comes back saying `timed out` in under ten seconds, and that
  `sleep 30 & sleep 30` is stopped as well - which is the assertion that the
  *process group* was killed and not just the shell.
- `run_command: signals belong to the command` - blocks `SIGTERM` in its own thread
  and then runs `kill -TERM $$; echo reached` through the tool. The child must die
  of the signal and never print the word; had it inherited the blocked mask, the
  signal would only go pending and the shell would carry on. Comment the
  `pthread_sigmask` call out of the child to watch it fail with `rc=-2`, which is
  how one knows the assertion is load-bearing.
- `settings: tools section` - `[tools] command_timeout` / `pass_env` round-trip,
  zero meaning no limit and a negative clamping to it rather than to a timeout that
  would fire immediately.

By hand, the two that are worth seeing once:

```console
ATUI_COMMAND_TIMEOUT=2 ./build/atui
```
and ask for `sleep 30`: the transcript says the command was killed with everything
it started, in about two seconds, and `pgrep -fl "sleep 30"` afterwards should come
back empty. Then `ATUI_COMMAND_TIMEOUT=0` and the same command runs to completion.

### Through the TUI (tui-test)

The unit tests call the tool; this is the same code behind a real approval dialog, a
streaming transcript and a terminal that has to still work afterwards. The
credential half needs an environment variable to be missing, so it is only visible
as an absence - which is the point:

```console
mkdir -p /tmp/atui_tui/proj && cd /tmp/atui_tui/proj && echo notes > notes.txt
tui-test run --cols 110 --rows 34 \
  --env ATUI_CONFIG=/private/tmp/none.ini \
  --env ATUI_BASE_URL=http://...:10001/v1 --env ATUI_MODEL=... \
  --env ATUI_TEST_TOKEN=hunter2secret --env ATUI_COMMAND_TIMEOUT=2 \
  --env ATUI_UNDO_DIR=/tmp/atui_tui/undo -- ./build/atui
tui-test type 'run this exact command and tell me what it prints: echo [$ATUI_TEST_TOKEN]'
tui-test key press Enter
```

What came back, in order: the tool line `$ echo [$ATUI_TEST_TOKEN]`, then a dialog
whose reason was *"it reads personal data"* (the judge refusing to be sure what a
variable named `*_TOKEN` is for, which is the right answer), `Alt-Y`, and the result
record `# it reads personal data (you allowed it)` / `exit code 0` / `[]`. The secret
was nowhere on the screen, and the model's own conclusion was that the variable is
not set - which is what the scrub looks like from inside the command.

Then `sleep 5` with the 2-second limit: `# agent: waits five seconds` (auto-approved,
the judge's reasoning as the record line) and `timed out after 2s and was killed,
along with everything it started (no output)`, with the transcript carrying on
streaming underneath. `pgrep -fl "sleep 5"` afterwards is empty, and the
`permissions.log` beside the config file has both decisions - `rung=10` for the ask,
`rung=13` for the run.

The same session covers the journal, since `edit_file` is where the flushed writes
land: asking it to change a word in `notes.txt` auto-approves with
`# auto: inside the project` (rung 15, the journal covering the write), leaves
`U000001.PRE`/`.INF` in `ATUI_UNDO_DIR` with `state=saved` and both byte counts, and
then `Alt-U` → Restore puts the original bytes back and consumes the entry, leaving
the journal directory empty.

Found doing it with tui-test:

- `tui-test submit` types and then presses the *shell's* return key, which atui's
  prompt does not treat as send; a separate `tui-test key press Enter` is what the
  app's handler intercepts. Text still sitting in the prompt box after a `submit` is
  not a stuck UI.
- `tui-test click text Restore` finds two matches in the undo dialog - the button,
  and the sentence under it that starts with the same word. Resolve with
  `find text Restore --json` then `mouse click <x> <y>`, immediately before the
  click, for the same reason as everywhere else in this file.
- `Alt-X` does nothing while the undo dialog is open, which is the dialog doing its
  job: the outcome ("restored notes.txt") is shown *in* the box and the box closes on
  its own buttons. Cancel, then Alt-X.

### Spawn implementation notes

- `popen()` has no pid, so a timeout on top of it is a message rather than a
  behaviour. The Unix path is `pipe` + `fork` + `execve` with `setpgid()` in the
  child **and** in the parent - either can win the race, and only the parent's call
  closes the window in which a command that hangs immediately cannot be signalled.
- The child's stdin has to be `/dev/null`. Leaving the terminal on stdin is how
  `cat`, `git commit` without `-m`, or anything that decides to prompt, used to stop
  the session: the command blocked, the tool loop waited, and atui's UI had no way
  to interrupt a turn.
- `HiddenCredentials` is scoped to the spawn, not to the command. `popen()` copies
  the environment at the moment it forks, so the variables can go straight back;
  holding them out for the whole run would keep atui's own keys away from the next
  request for no extra protection.
- `setenv()` copies its arguments; `putenv(strdup(...))` does not, and looks the
  same until a long session has leaked one string per command.
- `exec()` clears signal *handlers* and leaves the signal *mask* alone, so a blocked
  signal travels from atui's worker thread into every command it starts and stays
  there for the command's whole life. The mirror image of the same fact is
  `CURLOPT_NOSIGNAL`: left at its default, curl may hold `SIGPIPE` ignored while a
  transfer is up, and an *ignored* signal survives `exec()` too. Both ends are now
  closed, because "the build works at the shell" stops being a useful sentence the
  moment the shell is replaced by a tool call.
- The deadline path is `#if defined(__unix__) || defined(__APPLE__)`, and
  `commandTimeoutIsEnforced()` is how the tests know to skip what that platform
  cannot do - rather than a test that passes by asserting nothing.

## Prompt input (wrapping + auto-grow)

The message box is a custom `PromptInput` view (a `TView` subclass in
`src/tui/app.cpp`), not `TInputLine`. `TInputLine` keeps one screen column per
character and its `draw()` repaints that single row, so it cannot wrap or grow.

Behavior:

- Holds a single logical line (Enter submits; there are no hard newlines) and
  soft-wraps it greedily at the panel width, breaking on the last space in each
  row (hard-breaking only over-long words).
- The panel **grows and shrinks to fit** the wrapped rows. `relayoutPanels_()`
  re-shares the desktop so the transcript fills the rows above and the prompt
  sits below, with no gap and no overlap. A short cap (`MIN_TR`) keeps a couple
  of transcript rows visible; past that the prompt scrolls internally to keep the
  caret on screen.
- **Row 0 of the view is a horizontal separator** (`-`) drawn between the
  transcript above and the typed text below — this is the visual divider between
  the response and prompt areas.
- Editing: left/right/home/end, up/down (across wrapped rows), Backspace/Delete
  are UTF-8 aware (continuation bytes are treated as part of the code point).
- **Text is read from the event's UTF-8 `text` buffer, not `keyCode`** — many
  terminals (and bracketed paste) deliver the character only in `text` with
  `keyCode == 0`. Control bytes are skipped so Enter/Esc/accelerators still
  reach `TApp`.
- The caret is the **real terminal cursor**: `draw()` maps the logical caret to a
  cell and calls `setCursor( col, row )` (row + 1 because row 0 is the
  separator). A caret sitting one past the last column of a full row is clamped
  onto that column — the view has no cell beyond it, and a caret outside the view
  is dropped rather than drawn.

Focus & plumbing notes (learned via `tui-test`):

- The transcript and the scrollbar both set `options &= ~ofSelectable`, so the
  prompt is the desktop's only selectable view and gets the initial keyboard
  focus. Mouse input is unaffected: `TGroup` routes positional events by
  position, not by focus, so the bar still takes clicks.
- **A vertical `TScrollBar` has to be exactly ONE column wide.** The widget
  picks its orientation from `size.x` (`size.x == 1` means vertical), so the
  three-column box this app had at first was drawn - and clicked - as a
  *horizontal* bar: `drawPos()` fills `size.x * size.y` cells from a
  `TDrawBuffer` it only initialises for one row, which left a column of
  uninitialised cells with a stray marker where the bar should have been
  ("the scrollbar is invisible / does nothing"). It draws its own `▲`/`▼` end
  arrows inside that single column, so nothing else is needed.
- Clicking the track jumps the thumb to the cursor, the arrows step one line,
  and the wheel scrolls by line. `TranscriptView::scrollDraw()` (the
  `cmScrollBarChanged` hook) keeps `follow_` in step, so scrolling up while the
  model is streaming stays up instead of being yanked back to the bottom; a new
  message - or dragging the thumb back to the end - resumes following.
- When the prompt grows and the split moves, `relayoutPanels_()` resizes *and*
  redraws the bar, so no stale pixels survive on rows that flip between
  transcript and prompt.
- The 50 ms timer (network build) calls `relayoutPanels_()`, which re-reads the
  live `deskTop` size and therefore **self-heals the split after a terminal
  resize**.
- **A custom text view has to ask for a caret with `showCursor()`.** `TView`'s
  state is just `sfVisible`, and `TView::resetCursor()` (`computeCaretSize`) only
  moves the terminal caret when `sfVisible | sfCursorVis | sfFocused` are all
  set — `sfCursorVis` is set by `TInputLine` itself, never by `TView`. Without it
  `setCursor()` is accepted and silently ignored: the prompt worked but showed no
  caret. For the same reason the "no caret here" branch parks the cursor off-view
  (`setCursor( -1, -1 )`) instead of calling `hideCursor()`, which clears
  `sfCursorVis` for good — nothing would put it back when the view is focused
  again (e.g. after closing a dialog).
- `rewrap()` records the buffer offset of every wrapped row (`rowOfs_`) because
  those offsets cannot be recovered by summing the row lengths: wrapping on a
  space drops that space from the row, so each wrap shifted the caret one column
  left on every row below it.

Manual verification:

    tui-test run --session c --cols 80 --rows 24 -- ./build/atui
    tui-test write "a long line that must wrap onto several visual rows ..."
    # the box grows; press Enter (key press Enter) -> submits and collapses back.
    tui-test resize 50 12        # network build re-tiles within ~50 ms.

The caret is easiest checked through `tui-test state`, which reports the terminal
cursor (note that `key press Alt-H` types the literal text — the key is `Alt+H`):

    tui-test state --session c | grep '^cursor'   # visible:true, x/y = caret cell
    tui-test write "hello"     # x advances to 5; Home/Left -> 0, End -> 5
    # wrap a long line: x must land just past the last character of the last row
    tui-test key press Alt+H   # About opens -> visible:false
    tui-test key press Esc     # closed again -> visible:true (flag not lost)


## Color scheme design (follows the tvision examples)

The pattern is taken from the tvision examples (`tvision/examples/tvdemo/`):

- The **classic** scheme is the stock Turbo Vision palette `cpAppColor`
  (`tvision/include/tvision/app.h:142`) — same as tvdemo, proven readable.
- Palette overrides live in `TApp::getPalette()` (`src/tui/app.cpp`), which
  returns the theme palette for color screens and falls back to the stock
  BW/mono palettes otherwise (tvdemo3.cpp:102-114 pattern).
- Runtime switches mutate the palette selection and force a full repaint with
  `setScreenMode(TScreen::screenMode)` (tvdemo2.cpp:349-350 pattern).

The dark theme (`src/tui/theme.cpp`) is the classic 135-entry palette with
every BIOS fg/bg pair re-mapped: foregrounds become readable light shades,
backgrounds dark slate/navy/teal equivalents. Foreground colors are kept
bright (secondary text fgDark[8] = #A9B6C3) so contrast stays high.

The chat transcript (`ChatTerminal` in `src/tui/app.cpp`) overrides the
stock TScroller palette: index 1 maps to app palette entry 13 (BIOS 0x1E,
yellow on blue — the classic TV text-display style used by tvdemo's file
viewer) instead of entry 6 (BIOS 0x28, gray on green), which had poor
contrast. In dark mode this yields soft yellow #F2E4AC on dark navy
#152238. Scrollbars stay stock (entries 4/5, same as tvdemo).

## Testing the TUI (theme verification)

### tui-test (Microsoft, installed via brew — preferred)

https://github.com/microsoft/tui-test — controls and inspects real terminal
sessions (Alacritty/Ghostty/Rio/xterm.js backends). Install:

    brew tap microsoft/tui-test https://github.com/microsoft/tui-test
    brew trust microsoft/tui-test        # required for unsigned taps
    brew install tui-test

Session workflow (sessions persist between CLI calls):

    tui-test run --env ATUI_THEME=classic --cols 100 --rows 30 -- ./build/atui
    tui-test expect text "atui (M2)"
    tui-test cells 0 1 3 1 --json        # read fg/bg styles of cells
    tui-test key press Alt+T             # open Theme dialog (radio picker)
    tui-test click text Classic          # pick classic (or Dark)
    tui-test click text OK               # apply; setScreenMode repaints
    tui-test screenshot -o out.svg
    tui-test close --all

Verified with this workflow:
- classic startup (`ATUI_THEME=classic`): transcript is yellow-on-blue (fg
  index 11 / bg index 4 in the backend's ANSI palette = BIOS 0x1E); stock menu.
- dark startup (default, no env): transcript #F2E4AC on #152238; menu bar
  #E8EAED on #252B33 with #E89A9A hotkeys.
- Theme dialog (Alt-T) pre-selects the active theme; picking the other radio +
  OK repaints both directions; reopening reflects the new selection.
- 2026-10-05: dark is the default theme; theme commands moved from a top-level
  Theme menu into `File ▸ Options` (menu bar is now File, About only). Verified
  via `tui-test cells` that default startup uses the dark RGB palette and
  `ATUI_THEME=classic` reverts to the BIOS palette (menu bg index 7).

### Manual pty harness (fallback)

Recreate from /tmp/opencode/atui_pty_test.py pattern:
- `pty.fork()` + `fcntl.ioctl(fd, termios.TIOCSWINSZ, ...)` (without a real
  window size the app sees a 0x0 terminal), then `os.execve(path, args, env)`
  — NOT `os.execv`, which silently drops the env dict.
- Kill with SIGKILL after capture; TVision restores the terminal on normal
  exit only.

### Gotchas learned the hard way

- TVision **redirects stderr to a pipe** when it is a tty
  (`tvision/source/platform/errredir.cpp`), so debug fprintf(stderr, ...)
  vanishes. Log to a file instead — and remember to remove the logging.
- `tui-test close --all` closes the session but the child can still be alive, and
  a session that is gone from `sessions` still holds its last screen. Rebuild,
  relaunch, read the screen, and you are looking at the **previous binary** — which
  is how a correct change gets blamed on the build. `pgrep -f build/atui` before
  relaunching; `pkill -f` if it answers.
- A `TDialog` only closes on `cmOK` / `cmCancel` / `cmYes` / `cmNo`. A button
  wired to a custom command id is silently swallowed - the click lands, the
  handler runs, and the dialog stays open looking broken. Reuse one of the four
  and read which one it was.
- `TButton` hotkeys (`~Y~es`) do work through Alt+letter, but **Alt-S is the
  "Switch model" menu**, so it is not available to dialogs. Free letters so far:
  B C D E G J K L M N Q R U V W Y Z.
- A dialog executed on `TProgram::application` gets the keyboard but **no working
  mouse hit-testing**. Every atui dialog goes through `deskTop->execView()`;
  anything else works fine until the first click.
- `TStaticText` clips silently — a `TDialog` sized by `makeRect()` fits about
  three rows of 33 columns and the rest of the message simply is not there. Count
  the wrapped lines and size the dialog around them.
- A `TInputLine` `limit` counts the NUL, and only `width - 2` columns are
  visible: n digits need `limit = n + 1` and `width = n + 2`, or a 4-field value
  scrolls off its own field and looks like a failure to save.
- `TERM=xterm-256color` → truecolor SGR (`38;2`/`48;2`) for RGB palette
  entries; BIOS palette entries become plain 16-color SGR.
- Alt keys arrive as `ESC` + letter in raw pty input; tui-test `key press
  Alt+D` handles this correctly.
- Palette entries are walked at every owner level (`TView::mapColor`), and
  `TColorAttr::operator uchar()` packs BIOS pairs back into a byte
  (non-BIOS attrs become 0x5F) — widget `cp*` strings are index chains that
  terminate at the app palette.

## Remote targets (OS/2 over telnet)

A target machine that cannot show us its screen is still testable:
`tools/atui-remote` is a telnet bridge that runs **as the program tui-test
starts**, so every tui-test verb acts on the remote screen and there is still
exactly one screen model in the system -- the emulator's. Nothing about the
existing workflow changes; `run` names the bridge instead of `./build/atui`.

    # one time: credentials in the environment, out of the shell history
    export ATUI_REMOTE_HOST=10.196.88.129 ATUI_REMOTE_USER=atui ATUI_REMOTE_PASSWORD=...

    tui-test run --cols 80 --rows 25 -- tools/atui-remote
    tui-test expect text "ATUI-SCREEN-MARKER"   # asserts against the remote screen
    tui-test cells 0 3 44 3 --json              # colours/attributes, remotely
    tui-test submit "echo hello"                # runs on the remote machine
    tui-test close

    tools/atui-remote --exec "dir d:\\atui"       # login, run one command, print, exit
    tools/atui-remote put src/main.cpp 'd:\atui\src\main.cpp'
    tools/atui-remote sync                        # mirror the sources, by hash
    tools/atui-remote build                       # sync, build on the target, report
    tools/atui-remote bridge-test                 # local suite, no target needed
    tools/atui-remote probe                       # asks the real target questions

`bridge-test` runs against a fake target the tool also provides (`serve`), so it
works with the OS/2 machine switched off; `probe` asks the real machine the same
questions. Both are worth running -- the fake target caught two bridge bugs, the
probe caught two of its own.

### What was measured on the OS/2 box (eComStation, P4, 2026-10-17)

* **The server echoes.** The bridge therefore puts the local terminal in raw
  mode with echo off. Getting this wrong is not cosmetic: a doubled echo makes
  every screen assertion lie, so `bridge-test` includes a control session with
  echo deliberately forced on and requires the check to see the text twice.
* **Terminal type selects the character set**, which is not obvious and silently
  ruins the screen. Answering `vt100` or `ansi` makes the server convert the
  session's CP437 output to Latin-1: `0x80` arrives as `0xC7` ('Ç') and every
  box-drawing byte becomes a punctuation mark. `pcansi`, `xterm` and `ibmpc`
  pass CP437 through unchanged (`0x80`→`0x80`, `0xda`→`0xda`), so the bridge
  answers **`pcansi`** by default. If box drawing arrives as accented letters,
  look at `--ttype` before looking at anything else.
* **Escape sequences pass through untouched**, `ESC ( 0`/`ESC ( B` (VT alternate
  character set) included, *even when the high-bit bytes beside them are being
  converted*. That is the durable lesson for the OS/2 display backend: express
  graphics as escapes, not as high-bit bytes, and no charset negotiation on
  either side can corrupt them.
* One wrinkle: a received `ESC[2J` comes back with an `ESC[0;0H` inserted after
  it, so the server interprets some output rather than being purely a pipe.
* **NAWS works in both directions**: the bridge reports the tui-test session
  size and re-sends it on `tui-test resize 60 20`, which the target sees.
* **Output streams while a program runs** -- verified by running `ping` and
  reading lines as they appeared, not after it exited. Without this no TUI can
  work; with a shell alone you would never find out.
* **FTP is byte-transparent** (0..255 round-tripped identical, `STOR` and
  `RETR`), so it is safe for sources as well as payloads, and `D:` is not FAT --
  long filenames survive. `C:\usr\bin\curl.exe` exists and P4 reaches the model
  endpoint (10.196.88.170, ICMP 0 ms), so HTTP can be checked before atui has a
  socket layer of its own.
* The server sets `TERM=DUMB`, which is a red herring: nothing on the OS/2 side
  reads it, and the bridge decides everything from negotiation and `--charset`.

### Gotchas with the bridge itself

- `tui-test cells`/`text` are **0-based**; row 3 is the fourth row.
- A character typed at the OS/2 prompt without Enter stays in the input line.
  The probe learned this the hard way: its echo test left a bare `q` behind and
  the next command ran as `qtype ...`, which reported itself as a transparency
  failure. Abandon such lines with Ctrl-C (`0x03`).
- macOS has no `telnet` client (that is why this tool exists) and no `timeout`.
  `nc` exists but does not negotiate, which is not the same thing.
- `tui-test close --all` can leave children alive: `pgrep -f atui-remote` before
  believing a session is gone.
- `--log-raw PREFIX` writes `PREFIX.out`/`PREFIX.in` byte-exact. Use it before
  reasoning about what the wire carried: a charset conversion is invisible in
  decoded text, and it was caught only by comparing hex.

### Building on the target (Open Watcom, OS/2)

The port is written here and compiled there, so the bridge is also a build loop:

    tools/atui-remote build                    # sync, compile, bring build.log back
    tools/atui-remote build --batch-args tvision
    tools/atui-remote sync --dry-run           # what would be sent
    tools/atui-remote build --no-sync --tail 40

`build` mirrors the sources by hash (367 files, ~74 s the first time and nothing
on a second run), runs `tools/os2/build.cmd` on the target, fetches `build.log`
over FTP and exits with the status the batch printed. Everything about those
three steps is load-bearing, and the reasons are the facts below.

**The toolchain** (`D:\WATCOM`, Open Watcom 1.9, 2010 build, in `PATH`):

* `wpp` is the **16-bit** C++ compiler and what it links cannot run in an OS/2
  session (`SYS0191`) -- use `wpp386`. Verify the compiler by running its output;
  "compiled with no errors" and "runs" are different claims.
* The linker spells the target differently from the compiler: `wlink system os2`
  quietly means **16-bit** and then fails on 32-bit libraries
  (`E2053: cannot reach a DLL with a relative relocation`). **`system os2v2`** is
  the 32-bit image. Omitting `system` happens to give 32-bit; do not rely on it.
* `-fo=name` names the *object*, not the executable, and there is no `-fe=`.
  `-fo=dir\` writes `<source-base>.obj` into `dir`, which is what makes a
  wildcard-driven build possible. `hello.exe` was an OMF object for a while
  because of exactly this.
* **C++ exceptions are off by default**, and the symptom is not a message about
  exceptions: Watcom's own `<string>` and `<vector>` fail with `E625 exceptions
  must be enabled to use feature (use 'xs' option)` from inside the header. `-xs`
  in `defs.rsp` took the first full build from 4142 errors to about 210 on atui's
  own sources. Read the E-numbers before writing any porting code.
* `long long` exists and is 8 bytes; `stdint.h`, `cstdint`, `inttypes.h` are
  there. Flat 32-bit: pointers and `long` are 4.
* There is **no libcurl and no pthread**, and no `<mutex>`, `<thread>`,
  `<atomic>`, `<chrono>`, `<condition_variable>` -- those are the port's own work.
  `dirent.h` is missing too (`direct.h`, `io.h`, `unistd.h`, `fcntl.h`,
  `sys/stat.h` exist).
* **Watcom has BSD socket headers** -- `h\os2\sys\socket.h`, `select.h`,
  `ioctl.h`, `sockio.h`, `netinet\in.h`, `arpa\`, `netdb.h` -- and an import
  library `lib386\os2\tcpip32.lib`, so the network transport can be sockets and
  a small HTTP client rather than libcurl, which does not exist for this
  compiler. **This is now proven at run time, not on paper**: `probe_sock`
  compiles, links against Watcom's own `tcpip32.lib`, opens a socket, and gets
  `HTTP/1.1 200 OK` / `Server: llama.cpp` from the inference endpoint. The IBM
  toolkit's library was therefore never needed, and the question of which of
  `tcpip32.lib` / `tcpipdll.lib` / `tcp32dll.lib` in `D:\TOOLKIT\lib` belongs to
  the running stack can be left alone. `socket() -> 5020`, `connect() -> 0`,
  `send() -> 86`, `recv() -> 511`, `fd_set` is 260 bytes, `struct sockaddr_in`
  is 16, `inet_addr` and `htons` work.
* **Socket includes have an order that must be kept.** `h\os2\sys\socket.h` was
  written against the IBM toolkit's type set: it names `u_char`, `u_short`,
  `u_int`, `u_long` and `caddr_t` without defining them and includes
  `<sys/cdefs.h>` at line 298, after the structures that need it. It pulls in
  `h\os2\types.h` itself, which supplies `long_int` and `MAXSOCKETS` but none of
  the `u_*` names, so five typedefs go in front of it. Ordering is what matters:
  `unistd.h` pulls in a header that sets the guard `os2\types.h` tests, so with
  `unistd.h` first that file skips itself silently and every socket structure
  downstream is broken. `<sys/types.h>` must not be included at all, because the
  one that resolves from `h\sys` is a different file with none of the BSD names.
  Measured across five permutations (`sktest.cmd`); the symptom of getting it
  wrong is `E1022: Missing or misspelled data type near 'long_int'` from inside
  the compiler's own header, several files from the cause.
* **`__cplusplus` is `1`**, not `199711L`, and `__WATCOMC__` is 1290. Anything
  that feature-detects with `#if __cplusplus >= 201103L` correctly sees C++98,
  but a library testing `#if __cplusplus` as a proxy for "standard C++ at all"
  sees something no one expected. tvision's configuration machinery is exactly
  that kind of code.
* **RTTI is off by default too**, in the same spirit as exceptions: `typeid` and
  `dynamic_cast` are `E875 ... RTTI must be enabled (use 'xr' option)`. The port
  does not enable it, so `#if defined( __RTTI__ )` guards are how a probe can ask
  the question without failing the build.
* `printf` **does** understand `%zu`, `%lld` and `%llu` -- measured, and worth
  measuring, because it decides whether the port needs helper functions for
  printing sizes and 64-bit values. It does not.
* **`huge` is a reserved word** (a memory-class keyword, alongside `far` and
  `near`). `std::string huge;` reads as a declaration with a storage class and no
  name, and the compiler says only `E121: syntax error` several columns away. Any
  identifier from that era's memory model is suspect.
* There is **no libcurl and no pthread**, and no `<mutex>`, `<thread>`,
  `<atomic>`, `<chrono>`, `<condition_variable>` -- those are the port's own work.
  `dirent.h` is missing as a header (`direct.h`, `io.h`, `unistd.h`, `fcntl.h`,
  `sys/stat.h` exist), but see the symbol map below: the functions it would
  declare are in the C library, so only the header has to be written.

**Includes, and why there is no forwarder directory.** Watcom's extensionless
C++ headers are stored truncated to eight characters -- `algorith`, `exceptio`,
`stdexcep`, `streambu`, `type_tra`, `unordere` -- which looks exactly like a
problem that `#include <algorithm>`-to-`<algorith>` forwarders would solve, and
an early revision of this port carried seven of them. They did nothing. Two
facts, measured on the target:

* The compiler resolves the long names by itself: OS/2 falls back to the
  truncated form, so `<algorithm>` opens `algorith` unaided. A ten-character
  extensionless include in a directory of our own opens too, so the compiler is
  not truncating blindly -- it is only the installed file that is short.
* `D:\WATCOM\h` is searched **before** the `INCLUDE` variable and before `-i=`
  directories, in whatever order they are given, so nothing here can shadow a
  header Watcom ships. The proof was `<streambuf>`: forwarder correct, uploaded,
  first on the include path, and the compiler still opened
`D:\WATCOM\h\streambu`.

`compat/os2/include/` therefore holds only headers this compiler does not have
at all -- `dirent.h`, `sys/wait.h`, `mutex`, `thread`, `atomic`, `chrono`,
`condition_variable` -- which is exactly the point at which this directory is
useful.

**Empty headers, which is worse than missing ones.** Some of Watcom's C++
headers open, compile, and declare nothing. The important one is `<sstream>`:
after its guards, its entire content is `namespace std { }`. It gives no
`std::ostringstream`, and no compat header can supply one because Watcom's stub
always wins the lookup. `<locale>`, `<queue>` and `<valarray>` are the same
shape; `<unordered_map>` is only `#include <_hash.h>`. The table below came from
compiling one name per file rather than from reading the directory:

| present | absent |
|---|---|
| `string`, `vector`, `map`, `list`, `deque`, `set`, `stack` | `std::function` -- only the pre-standard adaptors |
| `fstream`, `iostream`, `iomanip`, `strstream` (real, 216 lines) | `std::unique_ptr`; `shared_ptr` and `auto_ptr` exist |
| `algorithm` (`sort`, `count_if`), `iterator`, `numeric`, `utility` | `std::to_string`, `string::front`, `string::back`, the 3-argument `string::compare` |
| `memory`, `limits`, `bitset`, partial `type_traits` | `std::unordered_map`, and all of `<sstream>` |
| `std::memcpy`, `std::getenv` via `<cstring>`/`<cstdlib>` | those same names in the **global** namespace -- include `<string.h>` or qualify with `std::` |

`std::map` declares `find()` as a non-const member only, so calling `find()`
through a `const std::map &` is a compile error; `count()` and `lower_bound()`
do have const forms.

**What the C library actually provides.** `probe_sym` declares the POSIX and
OS/2 surface the port needs with C linkage, references every name without
calling it, and lets the linker answer: 64 names, 25 resolved. The undefined
list is the port's to-do list and the resolved half is bigger than expected:

| present against the default libraries | absent |
|---|---|
| `opendir`, `readdir`, `closedir` -- the **functions** are there, only `<dirent.h>` is missing | `scandir`, `alphasort` |
| `fsync` -- so the durability promise can be kept | `fdatasync`, `msync`, `sync` |
| `strdup`, `strcasecmp`, `strtoull`, `setenv`, `sleep`, `utime`, `chmod`, `_snprintf`, `_timezone`, `environ` | `asprintf`, `strtok_r`, `unsetenv`, `gettimeofday`, `usleep`, `nanosleep` |
| `_findfirst`/`_findnext`/`_findclose`, `_stat`, `_mkdir`, `_getcwd` | `realpath`, `mkdtemp`, `statvfs`, `symlink`, `link`, `readlink` |
| `execle`, `execlp`, `wait`, `signal`, `_beginthread` | `waitpid`, `wait4`, `kill`, `posix_spawn`, `popen`, `pclose`, `sigaction`, `sigprocmask`, `_beginthreadex`, all of pthread, `poll`, `select` |

Three of those absences are design decisions rather than inconveniences. No
`waitpid`, `kill` or `posix_spawn`, so a child process is `DosExecPgm` plus
`DosWaitChild` and a deadline cannot be "kill the process group". No `poll` and
no `gettimeofday`, so the socket loop waits with `select()` -- which lives in
`tcpip32.lib`, not in the C library, hence its appearance above -- and takes its
clock from elsewhere. And no `sigaction`/`sigprocmask`, which means the child
signal mask written for POSIX hosts is code the OS/2 build must not compile
rather than code to port.

**The kernel API is there and it works.** `probe_api` takes the addresses of
`DosCreateThread`, `DosWaitThread`, `DosSleep`, `DosExecPgm`, `DosWaitChild`,
`DosFindFirst`, `DosFindNext` and `DosQueryPathInfo`; all resolve against
`os2386.lib`, which Watcom does not link by default -- which is also why
`probe_sym` reported the Dos* names as undefined, a finding about the link line
rather than about the platform. Then it calls the synchronisation primitives,
because a link test proves that an entry point exists and nothing more: mutex
create, acquire and release all return 0; an event semaphore returns 640
(`ERROR_TIMEOUT`) when nothing has been posted and 0 after `DosPostEventSem`.
Private, unnamed semaphores, which is all an in-process mutex needs. So
`std::mutex` and `std::condition_variable` written over `DosCreateMutexSem` and
`DosCreateEventSem` are bookkeeping -- the answer the threading design needed.
Actually starting a thread is a different experiment and has not been done.
`select()` on an unconnected socket with a one second timeout returns 0 with
errno 0, so a read loop with a deadline is possible, which is the other half of
why a thread might not be needed at all.

Two names in `os2.h` were unreachable with the include guards in use --
`DosQuerySysInfo` with `QSV_MAX_PATH_LENGTH`, and the exit-thread entry point
under either spelling -- and neither is needed, recorded so that nobody spends
four round trips on them over a telnet link.

**Renaming does not replace.** `rename()` onto an existing file fails with
`errno` 6; `remove()` followed by `rename()` works. `probe_os` measures this
because the undo journal and the log rotation both depend on the ordering, and
both already do it in that order -- which is the reassuring kind of result from a
probe.

**The FTP account is jailed to `d:\atui`** (550 on any other directory) while
telnet is not, so reading one of Watcom's headers from the host means copying it
into the work directory first. Concatenating the headers into one text file and
fetching that once is how the table above was derived: there is no tar on the
target, and one file is one round trip.

**What this target's command processor will and will not do.** Every one of these
was found by a build script failing quietly rather than by reading a manual,
because there is no manual installed:

* An interactive command line longer than about **315 characters is silently
  discarded** -- 310 runs, 320 does not, and nothing is printed either way. A
  compile command with these switches is longer than that, so the build lives in
  a batch file and is started with a short command.
* **`set X=value` refuses a value containing `=`**: no error, the variable is
  simply not stored, and every later `%X%` expands to nothing. This presented as
  `SYS1041: The name -fo is not recognized`, because the compile line lost its
  command word. The fix is a response file (`wpp386 @defs.rsp`), which works and
  also keeps quoting out of cmd.exe entirely -- but note that a response file is
  bare options: `//` comment lines would be parsed as switches.
* No `%%~nf` modifier (no way to strip an extension off a `for` variable), no
  `call :label`, and `wlib` takes no wildcards. `start /bg` does return
  immediately, and `for %%f in (path\*.cpp)` keeps the path in the variable, so
  `-fo=obj\` plus a directory wildcard is the workable shape.
* `if errorlevel 1 (echo a>>log) else (echo b>>log)` works **inside a batch file**
  but is rejected (`SYS1003`) on an interactive command line -- which is why the
  batch writes its own `ATUI-BUILD-RC=` verdict and the tool reads that instead
  of wrapping the call.
* Related, and a bug that produced a confident wrong answer: `errorlevel` belongs
  to the command that ran **last**, so anything between the command of interest
  and the test is read instead. A probe driver that did `wlink ... `, then
  `if errorlevel 1 type link.log`, then `if errorlevel 1 goto skip` fell through
  both tests happily and printed `SYMBOLS-ALL-RESOLVED` for a link that had just
  failed with 43 undefined references. The shape that cannot go wrong is
  `if not errorlevel 1 goto ok` immediately after the command, with the reporting
  on the other side of the label.
* Which brings us to: **no step may have a silent failure path.** A batch that
  `goto`s past a compile error without printing anything is indistinguishable
  from a program that printed nothing because it had nothing to say, and one
  evening was spent on that difference.
* **A batch file cannot be named after an internal command.** `verify.cmd` was
  parsed as the internal `VERIFY` with an argument of `.cmd` and answered
  `SYS1001: An incorrect parameter was entered for the command`, with no hint
  that a file of that name was sitting in the current directory. `copy`, `dir`,
  `path`, `prompt`, `verify` and their fellows win over files, so the driver is
  `allprobes.cmd`.
* Redirection works inside a `for` body, so each file's compiler output can go to
  the log while one line per file goes to the screen. That split matters: the
  tool decides a build is over by watching for output, and a build with every
  byte redirected is indistinguishable from a hung one.
* **LF-only sources compile cleanly**, so no translation is needed on the way
  over; batch files do need CRLF, which `sync` applies to `.cmd`/`.bat` only.
* `INCLUDE` in the login environment also names the IBM toolkit, which has its
  own `sys\socket.h`. Build scripts therefore *set* INCLUDE to Watcom's two
  directories rather than extending it; mixing the two sets of declarations is
  silent, and fatal in the interesting direction.

**Where the port stands** (measured 2026-10-18, `-od -w3 -xs -5`): atui's own 23
translation units produce 210 errors in 18 files, and not one of them is about
include paths -- the headers atui asks for either resolve or are in the absent
column above. The work, in rough order of size: replace what uses
`std::function` and `<sstream>`, neither of which exists here at all; supply the
OS-shaped headers (`mutex`, `thread`, `atomic`, `chrono`, `condition_variable`,
`sys/wait.h`, `dirent.h`); C++11 syntax the 1998 grammar rejects (`nullptr`,
`override`, in-class initialisers, `>>` in nested templates); `std::string`
without `front`/`back`/the three-argument `compare`, and no `std::to_string`;
`find()` through a `const std::map &`; `memcpy`/`getenv`/`mkdir`/`getcwd` absent
from the global namespace when only `<cstring>`/`<cstdlib>` are included; and
tvision's own header problems -- `tspan.h` uses `nullptr`,
`compat/borland/iosfwd.h` expects `std::streampos`, `util.h` declares `stricmp`
and `strupr` into a calling-convention clash (`E619`/`E670`, `watcall`) -- plus
`tv.h`, which decides that anything neither Borland nor Win32 is Unix, defining
`_TV_UNIX` and `#define __cdecl` over a Watcom keyword. The fork's first change
is a compiler branch there.

## Open question (minor, does not affect final rendering)

With `ATUI_THEME=dark`, startup produces two full-screen flushes: the first
with classic BIOS attrs before any `getPalette()` call, the second (post
resize/redraw) with dark RGB attrs. The final screen is correct. Cause not
fully traced; tui-test cell assertions pin the final state either way.

## tvision is our fork

`tvision` is a fork of [magiblot/tvision](https://github.com/magiblot/tvision)
under `stash763/tvision`, because atui needs changes inside it -- platform
backends for OS/2 and DOS, and era-compiler support upstream has no reason to
carry. The gitlink in atui still pins an exact commit, so builds stay
reproducible; the fork decides only where that commit is fetched from.

    git -C tvision remote -v         # origin = our fork, upstream = magiblot
    git -C tvision fetch upstream    # then merge/rebase their fixes as needed

Changing tvision is always **two commits, in this order**: commit and push
inside `tvision` (to the fork), then commit the new gitlink in atui. An atui
commit that points at a submodule commit nobody has pushed makes the repository
unclonable, so push first, and `git submodule status` should then show no `+`.

The `upstream` remote lives in `.git/modules/tvision/config`, so unlike
`.gitmodules` it is per-clone and has to be added once after cloning:

    git -C tvision remote add upstream https://github.com/magiblot/tvision
    git -C tvision remote set-url --push upstream DISABLED://upstream-is-read-only

The second line is not tidiness: it is what stops a mistargeted push from
trying to write to magiblot's repository.

## Layout reference

- `src/tui/app.cpp` — TApp (menu, status line, desktop, chat widgets)
- `src/tui/theme.cpp/.h` — theme selection + classic/dark app palettes
- `tvision/` — TVision submodule; app palette in
  `tvision/include/tvision/app.h` (`cpAppColor`, entries 1-135)
- `tvision/examples/tvdemo/` — reference patterns for palette handling