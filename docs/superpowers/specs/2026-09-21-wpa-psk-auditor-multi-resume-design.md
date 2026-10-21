# WPA PSK Auditor — multi-session resume design

## Goal

Preserve multiple interrupted WPA audits so starting a different capture does
not destroy the previous resumable checkpoint. The dashboard exposes up to the
eight most recently updated active sessions and lets the operator resume or
restart a specific one.

## Storage

Each session owns an independent A/B pair under:

`/sdcard/lab/handshakes/.crack_audit/active/<session-id>/active.a|active.b`

The session ID is encoded as 32 lowercase hexadecimal characters. A/B remains
the power-loss safety mechanism for one session; it is not a second session.
The catalog scans session directories, validates both slots with the existing
codec, ignores tombstones, de-duplicates session IDs and orders entries by the
newest slot modification time. It retains all valid checkpoint directories;
the UI reads only the newest eight, avoiding silent deletion of user progress.

The legacy `.crack_audit/active.a|active.b` pair remains readable. Once that
session is checkpointed again it is written to its session directory. A legacy
copy with the same session ID is suppressed from the catalog.

## Runtime selection

The crack dialog can carry an optional target session ID. A dashboard Resume
must restore exactly that session and fail closed if its capture or wordlist no
longer matches. The legacy Compromised Data flow has no explicit target and may
select the newest exact capture/wordlist match.

Starting an unrelated capture creates a new session without tombstoning other
active sessions. Successful completion and `Start over` tombstone only the
current or explicitly selected session.

## Dashboard

The resumable-audits card renders one compact row per session with capture,
wordlist, safe progress, tried count, worker count, elapsed time and ETA.
Each row has its own Resume and Start over actions. Actions are disabled while
another crack or transfer is active or when the source Monster is offline.
The immutable episode history remains separate and informational.

## Verification

Host tests cover independent A/B stores, newest-first listing, legacy
compatibility, duplicate suppression and tombstoning one session without
affecting another. Source integration tests cover explicit session selection,
catalog-based checkpointing and per-row dashboard actions. Firmware compilation
is left to the operator; host tests must not invoke ESP-IDF builds.
