# WPA PSK Auditor Batch Manager — design

## Goal

Extend the existing WPA PSK Auditor with a durable, sequential queue of
handshake audits. A user can select multiple captures, choose one candidate
source, leave the device, and later continue after a pause or reboot. Each
queue item still uses the existing distributed cracker and all available
Grove, USB and M-BUS workers.

JanOS remains at protocol/version 1.7.5. The batch manager is a Tab5 concern
and reuses the current CRACK/1 worker protocol without new commands.

## User flow

1. Scan all sources and filter the merged capture catalog by audit state.
2. Select one or more captures. Remote-only captures are marked for automatic
   synchronization to Tab5.
3. Choose one shared candidate source: Internal, one wordlist, or ALL.
4. Add the selection to the queue. Every local capture is validated before it
   becomes runnable; invalid captures remain visible but are never queued.
5. Start or resume the batch. Exactly one capture is audited at a time, while
   that audit distributes its dictionary ranges across Tab5 and all workers.
6. Pause preserves the current crack checkpoint and prevents the next item
   from starting. Cancel current interrupts only the current item and leaves
   the rest of the queue intact. Remove affects only non-running queue items.

## Ownership and data model

The existing session catalog remains the source of truth for fine-grained
crack progress, wordlist fingerprints, worker ranges and resume state. The
audit history remains the source of truth for completed episodes. A new
`hs_audit_queue` module owns only batch-level state:

- stable batch and item identifiers;
- ordered capture identities and local paths;
- the shared candidate-source configuration;
- queue state and the linked crack session identifier;
- coarse progress, timing, ETA and last failure reason.

The queue is bounded to 32 items and uses alternating `batch.a` / `batch.b`
snapshots with sequence numbers and CRCs. Writes are atomic (`.tmp`, flush,
rename). On load, the newest valid slot wins. A slot with an interrupted
`RUNNING`, `SYNCING` or `CANCELLING` item is reconciled to `PAUSED`; it is never
silently marked complete or restarted before the user resumes the batch.

## States and transitions

Batch states are `READY`, `RUNNING`, `PAUSED`, `COMPLETE` and `CANCELLED`.
Item states are `QUEUED`, `SYNCING`, `RUNNING`, `PAUSED`, `FOUND`, `NOT_FOUND`,
`ERROR`, `INVALID` and `CANCELLED`.

Terminal states are immutable unless the user explicitly removes and queues
the capture again. Persist every transition before beginning the corresponding
side effect. This makes a power loss conservative: work may be offered for
resume, but a range is never treated as acknowledged merely because it was
scheduled.

## Engine integration

There remains one crack coordinator. The batch manager launches an item through
the same cracker entry point used by Compromised Data and Resume audits. A small
completion hook publishes a terminal result back to the batch controller after
the crack task has released transports and before the next item is scheduled.

Local captures are first-class inputs. The launcher can either download a
remote capture into `_crack_tmp.pcap`, or copy/open an existing Tab5 capture.
The worker preparation and distributed range logic is identical after that
boundary. A local capture does not attempt `save_pass` to a source Monster;
recovered credentials are still stored in local `cracked.txt`.

Pause is cooperative: request the normal cracker cancellation, wait for the
existing checkpoint/history path, then mark the item and batch paused. Resume
loads the linked session when it still matches the capture and wordlist;
otherwise the item remains in error with an actionable reason.

## Catalog and validation

Catalog rows gain selection controls and a derived audit-state badge. Filters
are: All, New, Resumable, Found, Not found, Error and Invalid. State is derived
from local validation, active sessions and recent history, in that priority.
Filtering never deletes selection; hidden selected items remain counted in the
selection summary.

Adding remote-only assets runs the existing Sync-to-Tab5 path first. After each
copy, Tab5 validates the local file and records its stable content identity.
Transport errors mean `ERROR`, not `INVALID`. Invalid captures stay in the
catalog as material for later Handshaker diagnostics.

## UI

The Auditor page adds a compact Batch queue section above the catalog:

- current item and `x / n` aggregate progress;
- elapsed time, ETA and active worker count;
- shared candidate-source dropdown;
- Start/Resume, Pause, Cancel current and Remove controls;
- a collapsible queue list with per-item state and progress.

Catalog actions show `n selected`, Select visible, Clear and Add to queue.
Disabled controls explain their prerequisite in the nearby status line. The
existing single-capture Audit action and Compromised Data flow remain available.

## Failure and recovery policy

- Corrupt queue slot: fall back to the other valid slot and report recovery.
- Both slots corrupt: keep captures/sessions/history untouched and start with
  an empty queue plus an error banner.
- Reboot during an audit: reconcile to Paused and offer Resume batch.
- Worker loss: handled by the existing worker recovery and local fallback.
- Capture missing or fingerprint changed: item becomes Error; later items may
  continue after explicit Resume/Skip.
- Wordlist changed: do not substitute silently; show Error and require the user
  to choose/requeue with the new source.
- Password found: finish the current item as Found, persist it, then continue.

## Acceptance

- A 3-item batch survives reboot and resumes the interrupted item from its
  existing safe checkpoints before advancing.
- Found, not-found, invalid and transport-error results remain distinct.
- Pausing during preparation and cracking releases every transport and leaves
  a resumable item.
- Removing an item never removes its capture, session or history.
- Single-file Compromised Data and Auditor Resume flows remain unchanged.
- Host tests cover persistence, CRC fallback, transition guards, reboot
  reconciliation, selection/filter derivation and coordinator decisions.
- Target `main.c.obj` compiles with the repository warning policy.
