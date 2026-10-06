# atui Testing Notes

Working notes on how to build, run, and test atui — especially the TUI layer,
which needs a real terminal (pty) to exercise.

## Build & unit tests

    cmake --build build          # from repo root
    ./build/atui --self-test     # unit + integration tests (29/29 PASS, network build)

Env vars read by the app (see `src/net/oai_chat_model.cpp`, `src/tui/theme.cpp`,
`src/settings/settings.cpp`):

- `ATUI_MODEL`, `ATUI_BASE_URL`, `ATUI_API_KEY`, `ATUI_TEMPERATURE`
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

Schema:

    [ui]
    theme = dark            # dark | classic

    [provider]
    base_url = http://127.0.0.1:8080/v1
    api_key  =
    model    = llama
    temperature = 0.7

Precedence: env vars seed the singletons first; a config file, when present,
overrides them (a key missing from the file keeps its env/default value).

Manual round-trip test:

    CFG=$(mktemp -u /tmp/atui_cfg_XXXX.ini)
    printf '[ui]\ntheme = classic\n' > "$CFG"
    ATUI_CONFIG="$CFG" tui-test run --cols 80 --rows 20 -- ./build/atui
    # Alt-T -> pick Dark -> OK; then quit. The file now reads "theme = dark".
    cat "$CFG"
    rm -f "$CFG"

## Providers dialog (remote OpenAI-compatible endpoint)

`File ▸ Options ▸ Providers...` (shortcut Alt-F, then Alt-P; or Alt-P directly)
opens a dialog to configure the remote server (any OpenAI-compatible endpoint,
e.g. llama.cpp's `server` or Ollama):

- Url — base URL (e.g. `http://127.0.0.1:8080/v1`, `http://host:11434/v1`)
- Key — optional bearer token
- Model — served model name
- Temp — sampling temperature

Edits are stored in the process-wide `atui::net::providerSettings()` singleton
(`src/net/provider.{h,cpp}`), seeded from `ATUI_BASE_URL` / `ATUI_API_KEY` /
`ATUI_MODEL` / `ATUI_TEMPERATURE`. `OaiChatModel::ask()` snapshots these
settings per request, so a dialog edit takes effect on the next chat message
without a restart. Pressing OK also writes the settings to the config file
(see Configuration above), so they persist across runs.

`provider.cpp` has no libcurl dependency and is always compiled, so the dialog
builds and runs with `ATUI_HAVE_NETWORK=OFF` too (verified in both configs).

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
by `ToolRegistry::renderToolsJson()` (`src/tools/tool_registry.cpp`). One tool
is registered by default:

- `get_time` (`src/tools/time_tool.cpp`) — no arguments; returns the machine's
  local date/time via `strftime`.

The exchange is a loop in `OaiChatModel::ask()` (`src/net/oai_chat_model.cpp`),
all on the worker thread, capped at `kMaxToolRounds = 6`:

1. Send the request (with `tools`). The SSE stream is parsed for
   `delta.tool_calls` fragments and `finish_reason` (`json_extract.cpp`).
2. If `finish_reason == "tool_calls"`: for each call, push a status line
   (`~ calling <name>`) to the queue, execute the tool via
   `ToolRegistry::find(name)->execute(args)`, append the assistant turn (with
   its `tool_calls`) and a `role:"tool"` result message, and request again.
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

Unit coverage (`--self-test`): `finish_reason: parse`, `tool: call frags split`,
`tool: args with braces` (JSON with escaped quotes/braces in `arguments`),
`tool: get_time`, `tool: registry render`.

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
in non-network builds (verified: 18/18 there, 29/29 with network).


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