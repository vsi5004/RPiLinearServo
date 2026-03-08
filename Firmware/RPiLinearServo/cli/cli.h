#pragma once

// ── CLI ────────────────────────────────────────────────────────────────
// Line-buffered command interface over USB CDC (stdio).
// Call cli_init() once, then cli_poll() from the main loop.

/// Initialise the CLI (prints prompt).
void cli_init();

/// Poll for incoming characters and process complete lines.
/// Non-blocking — returns immediately if no data available.
void cli_poll();
