# atui Testing Notes

Working notes on how to build, run, and test atui — especially the TUI layer,
which needs a real terminal (pty) to exercise.

## Build & unit tests

    cmake --build build          # from repo root
    ./build/atui --self-test     # unit + integration tests (64/64 network build, 44/44 without)
    # ...plus 4 live-server tests when ATUI_LIVE_BASE_URL is set (see 'Live endpoint testing')

Env vars read by the app (see `src/net/oai_chat_model.cpp`, `src/tui/theme.cpp`,
`src/settings/settings.cpp`):

- `ATUI_MODEL`, `ATUI_BASE_URL`, `ATUI_API_KEY`, `ATUI_TEMPERATURE`
- `ATUI_THINKING_RESEND` — `1`/`y`/`t` to replay an assistant turn's thinking
  to the model on later turns (default off; also a Providers-dialog checkbox).
- `ATUI_THEME` — `dark` (default) or `classic`; also switchable at runtime via
  `File ▸ Options ▸ Theme...` (Alt-T), which opens a radio-button dialog.
- `ATUI_CONFIG` — override the config file path (see Settings persistence).

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

    [app]
    provider = ollama       # active provider name

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
    #    68 passed, 0 failed (~30 s); offline runs stay at 64 passed

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
- `TERM=xterm-256color` → truecolor SGR (`38;2`/`48;2`) for RGB palette
  entries; BIOS palette entries become plain 16-color SGR.
- Alt keys arrive as `ESC` + letter in raw pty input; tui-test `key press
  Alt+D` handles this correctly.
- Palette entries are walked at every owner level (`TView::mapColor`), and
  `TColorAttr::operator uchar()` packs BIOS pairs back into a byte
  (non-BIOS attrs become 0x5F) — widget `cp*` strings are index chains that
  terminate at the app palette.

## Open question (minor, does not affect final rendering)

With `ATUI_THEME=dark`, startup produces two full-screen flushes: the first
with classic BIOS attrs before any `getPalette()` call, the second (post
resize/redraw) with dark RGB attrs. The final screen is correct. Cause not
fully traced; tui-test cell assertions pin the final state either way.

## Layout reference

- `src/tui/app.cpp` — TApp (menu, status line, desktop, chat widgets)
- `src/tui/theme.cpp/.h` — theme selection + classic/dark app palettes
- `tvision/` — TVision submodule; app palette in
  `tvision/include/tvision/app.h` (`cpAppColor`, entries 1-135)
- `tvision/examples/tvdemo/` — reference patterns for palette handling