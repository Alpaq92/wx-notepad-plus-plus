# Default editor font — attribution

**JetBrains Mono** — © the JetBrains Mono Project Authors — <https://github.com/JetBrains/JetBrainsMono>
— [SIL Open Font License, Version 1.1](https://openfontlicense.org) (full text in
`JetBrainsMono-OFL.txt`, alongside).

Used as the default editor font (`STYLE_DEFAULT`, `src/main.cpp`) in place of Consolas: Consolas is
a proprietary Microsoft font that happens to already be installed on Windows but isn't ours to
redistribute, and isn't present on Linux/macOS at all. JetBrains Mono is bundled instead so every
platform gets the same, permissively-licensed, code-oriented monospace font out of the box.

Only the Regular and Bold weights are bundled (`JetBrainsMono-Regular.ttf`,
`JetBrainsMono-Bold.ttf`) - unmodified from upstream. On Windows they're loaded as a process-private
font resource (no installation or admin rights needed, and nothing is added to the user's system-wide
font list); see `NppApp::OnInit`/`OnExit` in `src/main.cpp`.

See the root `LICENSING.md` and `NOTICE` for the project-wide licensing summary this entry is
consistent with.
