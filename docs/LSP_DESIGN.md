# Language Server Protocol client — design

Status: **design agreed, not yet built.** Written before any code, deliberately, because the expensive
mistakes here are architectural and two of them are security-shaped.

This is option **C** of the four costed for the call-tip gap in
[MISSING_FUNCTIONALITY.md](MISSING_FUNCTIONALITY.md). Option **B** — a workspace signature index — has
already shipped and closes the "function defined elsewhere in *this project*" case. LSP is what closes
the case B cannot: `std::vector::push_back` and everything else the workspace does not define.

## Why this is smaller than it looks

Three of the four hard parts already have precedent in this tree:

| Need | Already here |
| --- | --- |
| Spawn a child process and consume its output without freezing the UI | `src/term_backend.cpp` (the PTY backend) |
| Read JSON | `src/json_value.h` — **parse only** |
| Do background work in slices off a timer | `src/file_index.h` (the Quick Open crawl) |
| Keep protocol logic pure so it is testable with no server | `diff_myers.h`, `regex_engine.h`, `file_index.h` |

The genuinely new pieces are a small **JSON writer** (the reader is parse-only; the writer that exists
lives in `keymap_store.h` and is schema-specific) and the **position conversion** below.

## The actual hard part: positions, not features

Every LSP feature needs the server's view of the buffer to match the editor's, which means
`didOpen`/`didChange`/`didClose`. That part is mechanical. The part that is not:

**LSP positions are `{line, character}` where `character` counts UTF-16 code units. Scintilla is
byte-oriented UTF-8.** Every request and every response crosses that boundary.

This is the single most bug-prone area of any LSP client, and it fails *silently*: on any line
containing a non-ASCII character the positions drift by a few units, and the user gets hover text for
the wrong symbol rather than an error. Nothing crashes, nothing logs, it is just subtly wrong.

Cases that must be pinned by tests:

- ASCII only (the trivial case, and the one that hides the bug during development)
- 2-byte UTF-8 (`é`): 2 bytes, 1 UTF-16 unit
- 3-byte UTF-8 (`€`, CJK): 3 bytes, 1 UTF-16 unit
- 4-byte UTF-8 (emoji, astral plane): 4 bytes, **2** UTF-16 units — the surrogate pair, and the case
  that breaks naive implementations
- CRLF vs LF line ends (the line index must not shift)
- A position at end-of-line, and at end-of-document
- A byte offset landing mid-codepoint (must clamp, never split)

Conversion is bidirectional and both directions get tests.

## Decisions

| # | Decision | Choice | Why |
| --- | --- | --- | --- |
| **L1** | Transport | **Timer-polled non-blocking pipe. No threads.** | `term_backend` threads because a PTY streams continuously and TUIs need low latency. LSP is request/response, modest volume, ~50–100 ms tolerance for hover. Polling keeps the editor single-threaded — no locks, and none of the "queued `CallAfter` fires after the object is destroyed" hazard `term_backend.h` documents at length. `file_index.h` argues for exactly this. If latency ever disappoints, the reader becomes a thread *behind the same interface* without touching callers. |
| **L2** | Where server commands come from | **`userDataDir` config only. Never the workspace tree.** | An LSP config is "run this executable". The VS Code convention of a per-project config file means opening someone's repository runs their command — arbitrary code execution from opening a file. This is a shape that has produced real CVEs in other editors. Per-project overrides are not worth that, and a trust prompt is not a mitigation because people click through prompts. |
| **L3** | Discovery | **Probe PATH for known servers; use one if present, stay silent if not.** | Zero config for anyone who already has `clangd` / `rust-analyzer` / `pylsp` / `gopls`, and completely invisible for anyone who does not. Avoids the "ships a feature that does nothing out of the box" cliff that D10 raises for the plugin catalog. An explicit config entry overrides the probe. |
| **L4** | First slice | **`signatureHelp` + `hover`, one server, full-text sync.** | Read-only, no merging with existing UI sources, and it directly closes the gap that started this. Deliberately *not* completion (latency-sensitive on every keystroke, and has to merge with the existing keyword + document-word + workspace-index sources — a UX problem in its own right) and *not* diagnostics (server-pushed rather than request/response, and it collides with the spell-checker's squiggle indicators, which needs an indicator-allocation decision first). |

## Architecture

    src/lsp_proto.h     pure. No wx, no I/O, no process.
                        - Content-Length framing: encode, and a decoder that accepts bytes in
                          arbitrary chunks (a header split across two reads, several messages in
                          one read, a message larger than one read)
                        - JSON-RPC request / response / notification encode + decode
                        - the UTF-8 byte <-> UTF-16 {line, character} conversion above
                        -> tests/lsp_proto_test.cpp, runs with no server and no display

    src/lsp_client.h    process + session. Spawns via wxProcess, polls its stdout from a wxTimer,
                        feeds bytes to the decoder, correlates responses to requests by id,
                        performs the initialize/initialized handshake, tracks server capabilities,
                        and owns document sync.

    main.cpp            wiring only: on '(' ask for signatureHelp, on the hover command ask for
                        hover, and fall back to what exists today when there is no answer.

Keeping `lsp_proto.h` free of wx is what makes the risky part testable. The framing decoder and the
position conversion are where the bugs live, and neither needs a running server to exercise.

## Degradation is the default, not an error path

No server on PATH, spawn fails, the server crashes, the server is slow, the server returns nothing —
**all of these must be indistinguishable from "LSP is not installed"**: the existing call-tip sources
answer instead and nothing is shown. No dialog, ever. A status-bar hint at most, and only on an
explicit user action. A language server dying must never interrupt typing.

Every request carries a timeout. A late response for a superseded request is dropped, not applied —
the caret has moved on.

## Lifecycle

- One server per (language, workspace root). Started lazily on first use, not at launch.
- Shutdown: `shutdown` request, then `exit` notification, then a bounded wait, then kill.
- **Teardown ordering matters, and this project has been bitten by it before** — see the Nib
  plugin-unload crash (`CallAfter` deferring `unloadNibPlugins` past the WM_CLOSE call stack). Here:
  stop the poll timer *before* killing the process, and make sure no queued work can deliver into a
  frame that is going away.

## Phases

1. **`lsp_proto.h` + tests.** Framing, JSON-RPC, position conversion. No I/O at all. This is the
   phase that carries the risk, and it is fully testable.
2. **`lsp_client.h`.** Spawn, poll, handshake, capability negotiation, full-text document sync.
3. **Wire `signatureHelp` + `hover`** into the existing paths, behind the fallback contract above.
4. Later, each its own decision: completion (merge strategy), diagnostics (indicator allocation),
   go-to-definition, workspace symbols.

## Open questions, deliberately deferred

- Which servers go in the PATH probe list, and under what names per platform.
- Request timeout values, and whether a slow server should be remembered and skipped.
- Whether to surface "a language server is running" anywhere in the UI at all.
- Full-text sync is phase 2's choice; incremental sync is a later optimisation and needs the position
  conversion to already be trustworthy.
