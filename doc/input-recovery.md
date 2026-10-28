# Input recovery hardening

This patch improves recovery without claiming to reproduce or fix the reported
intermittent Skyrim E/R incident. It adds no diagnostic logging or permanent polling.

## Required behavior

- Closing an already-closed logical menu still releases QuickLoot's custom input group.
  Other mods' control flags must not be globally enabled.
- A real loot-menu close clears its session. An older queued Hide must not cancel a
  newer Show that has not reached the UI yet.
- Pending actions belong to their original container session. A close, replacement
  or load reset invalidates them, including tasks already drained to a local batch
  and keys still held from the previous session. Non-global actions require a fresh
  Down in their current session; explicitly global enable/disable shortcuts remain available.
- Pre-load, post-load and new-game messages clear transient references, activation
  release state and repeat timers. Explicit disable requests owned by other mods remain.
- A failed movie or missing required SWF object closes capture instead of continuing
  with an unusable view. Loading another skin does not itself prove an input bug.
- Explicit hold-Activate bindings use the hold action. A normal short Activate still
  triggers its release action; normal object grabbing remains when no satisfied explicit
  hold binding owns the key. A fired hold must not also trigger the tap on release.

## Verification

The standalone tests compile production `LootMenuManager.cpp` and its real header
against a small engine adapter. The activation tests compile the exact production
method bodies selected by the runner; they do not reproduce Skyrim's native event pump.
Failed-movie tests compile the production VR post-create and frame methods against a
movie adapter that detects invalid access without causing a native crash.
The test fixtures cover the hardening contract, not the user's unobserved game failure.

```powershell
python tests/run_recovery_tests.py --build-dir <temporary-build-directory>
python tests/run_recovery_tests.py --build-dir <separate-directory> --baseline-ref 0afe2ba
```

The baseline invocation intentionally fails the closed-session, hold-Activate and
failed-movie regression checks.
Only run the DLL build with automatic deployment disabled. No installed Mod files or
logging configuration are changed by these tests.
