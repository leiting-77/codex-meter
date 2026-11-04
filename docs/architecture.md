# Architecture

C++17 / Win32 / GDI. No embedded browser or interpreter.

- src/model.hpp: typed JSON normalization; percentages and optional quota/credit metadata.
- src/platform.hpp: UTF-8, Windows handle ownership, atomic metadata persistence, Codex discovery.
- src/rpc.hpp: bounded JSONL transport; explicit inherited handles; process tree Job Object; handshake; managed login; query; deadlines and cancellation.
- src/worker.hpp: separate FIFO lanes for login and normal queries (at most two helpers); typed cancellation and fresh post-login sessions; results delivered through a mutex-protected event queue.
- src/ui_geometry.hpp and ring_renderer.hpp: shared hit tests, edge/scroll geometry and antialiased rounded ring rendering.
- src/main.cpp: compact/detail renderer, native buttons, tray, profile actions, DPI/position persistence and refresh scheduling.
- src/usage.hpp and usage_cache.hpp: validated daily token buckets, bounded per-profile atomic cache with account identity binding; no chat log ingestion.
- src/surface_renderer.hpp: antialiased shape masks composited into the shared top-down premultiplied DIB surfaces, preserving fractional alpha at glass edges and respecting the content clip.
- src/text_renderer.hpp and dwrite_renderer.hpp: Segoe UI system font selection, DirectWrite grayscale glyph masks and antialiased traffic-control paths composited through the same alpha pipeline. Small text uses at least 11 logical pixels; no ClearType subpixel colors are copied onto translucent surfaces.
- third_party/nlohmann: pinned MIT JSON library, no network build dependency.
- tests: model tests, fake app-server, Windows IPC/storage tests.

The UI thread owns profiles and snapshots. Worker tasks copy a profile and executable path; workers never mutate UI state. Per-account busy state prevents concurrent operations on the same account. There are no detached threads. Shutdown cancels and joins both lanes; Job Objects dispose of owned descendants. User cancellation pauses automatic polling for that account until a manual action or restart.

Supported Windows 11 builds use DWMWA_SYSTEMBACKDROP_TYPE (Desktop Acrylic) and a premultiplied 32-bit GDI buffer over the extended DWM frame. Only the background tint is translucent; text and cards remain opaque. Failed backdrop setup and high-contrast mode use an opaque fallback. No undocumented accent API, background screenshot capture or global layered-window opacity is used. See https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwm_systembackdrop_type .

Managed profiles get independent CODEX_HOME directories and keyring storage. The optional local profile inherits the current CLI environment and auth settings and never logs out that account. This release delegates the browser OAuth flow to account/login/start, not a separate SIWC registration implementation.

Quota responses use rateLimitsByLimitId when populated, otherwise the legacy rateLimits view. Primary/secondary windows remain distinct even if reset timestamps match. Weekly means exactly 10080 minutes. Missing credits differ from zero credits. availableCount is authoritative.

A multi-account refresh is sequential polling. No claim is made that notifications from an unrelated Codex process automatically reach this app. Each query performs initialize/initialized, account/read, and account/rateLimits/read, then shuts down the helper. When details are visible, account/usage/read may run in the same helper, at most every five minutes except explicit refreshes. Unsupported daily usage is optional and does not discard quota data. Daily date labels are kept as returned by the service, with a seven-local-calendar-date display; no undocumented timezone or cross-product coverage is inferred. We do not start threads, turns, inference, reset redemption or workspace emails.

Official protocol reference: https://learn.chatgpt.com/docs/app-server . The monitor does not assume that the full app-server has a documented quota-only low-memory startup mode. The 2026-11-01 local resource sample showed an approximately 1 GiB official helper peak; this remains a known dependency cost despite the main UI's approximately 18 MiB working set. Resource sampling includes descendants and checks process creation dates to avoid recycled-parent-PID attribution.

