# atui Testing Notes

Working notes on how to build, run, and test atui — especially the TUI layer,
which needs a real terminal (pty) to exercise.

## Build & unit tests

    cmake --build build          # from repo root
    ./build/atui --self-test     # unit + integration tests (60/60 PASS, network build)

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
    models   = llama3.2,qwen3:30b  # ids offered in the Model menu (from GET /models)
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

## Multiple providers, model switching (Model / Switch provider menus)

Two generated menu-bar dropdowns sit next to `File`:

    File   Model   Switch provider   About        (Options lives under File)

* **Model** (`Alt-M`) — the active provider's models; the one in use is marked
  `*`. Picking one sets it immediately and writes the config. Below the list:
  `Refresh models` (re-reads `GET <base_url>/models`) and
  `Enter model name...` (a prompt for endpoints that do not list their models;
  a hand-typed id is appended to the list).
* **Switch provider** (`Alt-S`) — every configured provider, active one marked,
  plus `New provider...` (asks for a name, activates it and opens the Providers
  dialog) and `Edit providers...`.

The status line's left-most field always shows what a message is actually sent
to: ` ollama | qwen3:30b `.

Model ids are cached in the config (`models = ...`), so the Model menu works
offline; `Refresh models` updates the cache from the server.

Implementation (`src/net/provider.{h,cpp}`): a process-wide store
(`providers()`, `activeProviderIndex()`) with `addProvider` /
`removeProvider` / `setActiveProvider[ByName]` / `findProvider` /
`uniqueProviderName`, plus `setModels` / `selectModel` for the model list.
`providerSettings()` still returns the **active** provider, which is what
`OaiChatModel` snapshots per request — so the request path did not change.
`addProvider` copies `defaultProviderSettings()` (env-seeded) under a unique
name; the last remaining provider cannot be deleted.

TVision notes for the generated dropdowns (all verified live):

* A dropdown must be a `TSubMenu`; `subMenu + menuItem` appends to the **last
  top-level entry's own** menu, so a plain `TMenuItem` in that chain silently
  lands inside `File`.
* `TMenuItem::~TMenuItem` deletes `subMenu` only for `command == 0` items, and
  `TMenu::~TMenu` deletes its item chain — so refreshing is "assign a new
  `TMenu`, delete the old one" (`setSubMenu_`).
* `command == 0` also means "has a submenu" to `TMenuBox` (it draws a `►` and
  opens the submenu), so greyed-out informational lines get a real no-op command
  (`CmdNoop`) instead.
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

Verified end-to-end against Ollama `0.14.3` at `http://10.196.88.167:11434`:

- Endpoint discovery: `GET /v1/models` (OpenAI-compatible) lists the models;
  `GET /api/version` confirms Ollama.
- Streaming SSE chunks are
  `data: {"choices":[{"index":0,"delta":{"role":"assistant","content":"…"},
  "finish_reason":null}]}`, ending with `data: [DONE]`. Our parser
  (`json_extract.cpp:deltaContent`) handles this exactly (finds `"delta"` then
  `"content"`; treats `[DONE]` as terminator).
- Two-turn context (`conv_` carry-over) — see the bug fix note below.

Run against Ollama (fast tiny model, or swap in a capable one):

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
  one-line `approvalSummary()` preview), pushes a status line
  (`~ <tool>: awaiting your approval…`), and blocks in `approve()` until
  answered. No approver, or a timeout (10 min), means **refuse** — the tool
  never runs and the model gets a "declined"/"no approval UI" result.
- The TUI installs a `TuiApprover` (`src/tui/app.cpp`). `approve()` runs on the
  worker thread: it publishes the request and blocks. `pollSink_()` (the 50 ms
  timer on the UI thread) picks it up and shows a modal
  `messageBox(..., mfConfirmation | mfYesButton | mfNoButton)`; Yes runs the
  tool, No / Cancel declines. A `g_approvalShowing` guard prevents a re-entrant
  dialog while the modal box pumps its own event loop.

Arguments are parsed with a small flat-JSON reader (`src/tools/json_args.{h,cpp}`)
that unescapes strings and skips nested objects/arrays, so values containing
braces, quotes or newlines are handled correctly.

The exchange is a loop in `OaiChatModel::ask()` (`src/net/oai_chat_model.cpp`),
all on the worker thread, capped at `kMaxToolRounds = 6`:

1. Send the request (with `tools`). The SSE stream is parsed for
   `delta.tool_calls` fragments and `finish_reason` (`json_extract.cpp`).
2. If `finish_reason == "tool_calls"`: for each call, look it up. If it
   requires approval, gate on the `Approver` (see above); otherwise (or once
   approved) push a `~ calling <name>` status line and run
   `ToolRegistry::find(name)->execute(args)`. Append the assistant turn (with
   its `tool_calls`) and a `role:"tool"` result message (or the decline
   message), then request again.
3. Otherwise stop — that content was the final answer. The whole exchange is
   committed to `conv_` only on success.

Streaming fragments arrive keyed by `index`: the first carries `id` +
`function.name`, later chunks append pieces of `function.arguments` (which may
be split mid-string). `parseToolCallFrags()` (string-aware scanner) and
`mergeToolFrags()` in `oai_client.cpp` reassemble them.

Not every model supports tools. Verified against Ollama `0.14.3` at
`http://10.196.88.167:11434` — check `GET /api/show {"model":…}` and look for
`tools` in `capabilities`. Known tool-capable tags there:
`ministral-3:8b-instruct-2512-q4_K_M`, `mistral:latest`, `phi4-mini:3.8b`,
`granite4:latest`, `nemotron-mini:4b`. A tool-less model just ignores `tools`
and answers directly (harmless).

End-to-end (real client + real tool + second round) — "What is the current
time?" against `ministral-3:8b-instruct-2512-q4_K_M`:

    [round 0] finish='tool_calls'  exec get_time({}) -> 2026-10-05 22:08:34 EDT
    [round 1] finish='stop'        "The current time is **10:08 PM EDT** …"

**Live confirmation check** (mock server asking for `run_command`
`{command:"echo APPROVED_42"}`): submitting shows
`~ run_command: awaiting your approval…` then a modal Confirm dialog
(`$ echo APPROVED_42`, Yes/No) while the worker is blocked. Answering **Yes**
proceeds to `~ calling run_command...` and the round-1 final answer; answering
**No** shows the prompt but never prints `calling run_command...` — the command
does not run and the loop continues with the "declined" result.

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

(The quick switcher is the **Model** menu on the menu bar — see "Multiple
providers, model switching" above. The dialog's list is the discovery view: it
shows what the server reports and caches it for the menu.)

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
- Scrolling: mouse wheel or the vertical scrollbar; auto-follows the bottom
  while streaming until you scroll up.
- Whether thinking is stored and replayed to the model on later turns is the
  `thinking_resend` option (default off). When on, the assistant turn carries
  `reasoning_content` on subsequent requests (`oai_client.cpp:buildBody`).

**Verified live** (localhost mock SSE server, since the served models here fold
reasoning into `content`): sending a question renders the collapsed thinking
block; clicking it expands to show the buffered reasoning then the answer.

> Note: `tui-test` cannot inject Ctrl-T — its raw `0x14` byte (and named
> combos) are dropped by tvision/ncurses before reaching the app, and `key
> press` needs `press` (e.g. `tui-test key press <s> Enter`). Ctrl-T works in
> real terminals; the click path exercises the identical toggle and was used
> to verify rendering. Enter itself is delivered as `evCommand` (kbEnter) in
> this harness, so `handleEvent` accepts both the key-down and command forms.

Test coverage (`--self-test`, network build):
`oai: delta reasoning parse` (both keys + null), `oai: reasoning stream`
(content/reasoning separated, callback routing), `oai: chat thinking` (queued
as `tkThinking`/`tkContent`), `oai: chat resend off/on` (the `reasoning_content`
is replayed only when the option is on).


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

Focus & plumbing notes (learned via `tui-test`):

- The transcript and the scrollbar both set `options &= ~ofSelectable`, so the
  prompt is the desktop's only selectable view and gets the initial keyboard
  focus.
- The scrollbar renders 3 cells (`◄▓►`) and is sized to cols `x2-3..x2-1`; when
  the split moves, `relayoutPanels_()` redraws it so no stale separator pixels
  survive on rows that flip between transcript and prompt.
- The 50 ms timer (network build) calls `relayoutPanels_()`, which re-reads the
  live `deskTop` size and therefore **self-heals the split after a terminal
  resize**.

Manual verification:

    tui-test run --cols 80 --rows 24 -- ./build/atui
    tui-test write "a long line that must wrap onto several visual rows ..."
    # the box grows; press Enter (key press Enter) -> submits and collapses back.
    tui-test resize 50 12        # network build re-tiles within ~50 ms.


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