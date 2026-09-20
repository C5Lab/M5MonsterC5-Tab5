# Distributed Worker Reliability Specification

## Goal

Make every Grove, USB, and M-BUS crack worker expose its current preparation/run phase and survive transient control-plane failures without duplicating a job or losing dictionary coverage.

## Coordinator behavior (Tab5)

- Treat capabilities, capture synchronization, wordlist synchronization, and job start as explicit per-worker stages.
- Allow at most three stage attempts total, with 250 ms and 1000 ms backoff before attempts two and three.
- Show and log transport, phase, attempt number, and the most recent failure reason.
- A file-sync attempt may resume an existing `.part`; it must never blindly restart USB while the binary/text boundary is uncertain.
- USB retry is allowed only after the existing recovery path has observed terminal `END` plus the CLI prompt. Failure to recover that boundary marks the worker lost.
- Grove/M-BUS may retry at 115200 after a failed fast-baud attempt. All further attempts remain at 115200 so that three stage attempts do not expand into nested baud retries.
- Job start uses one stable job ID and assignment across all attempts. After every missing confirmation, query `status <job>` first. Resend `start` only after an ordered `unknown_job` response.
- Runtime status still requires three consecutive misses before loss. Any valid correlated response clears the miss counter and refreshes the worker UI.
- After retry exhaustion, cancel a possibly active remote job where safe and process only the unconfirmed suffix from the last monotonic `safe_offset` locally.

## Worker behavior (JanOS 1.7.5)

- Keep CRACK protocol version 4 and JanOS version `1.7.5`.
- Make `start` idempotent for the same job ID and identical capture, wordlist, and byte range.
- A replayed identical start returns `ACCEPTED ... replay=1 state=<state>` and never creates a second task.
- Reusing a job ID for a different assignment returns `REJECTED code=job_conflict job=<id>`.
- A different job while one is running returns the existing `busy` rejection. A different job after a terminal state may replace it.
- `STATUS` appends backward-compatible `phase=<phase>` and `progress_age_ms=<milliseconds>` fields.
- `progress_age_ms` is measured from acceptance until the first committed safe progress, then from the latest safe progress commit. It is diagnostic only and does not alter lease/loss decisions.

## Compatibility and safety

- Existing fields and ordering remain accepted by older peers; new tokens are optional to the Tab5 parser.
- No firmware compilation or flashing is part of this implementation. Host tests are required.
- Preserve unrelated dirty-tree changes and all generated firmware binaries.

