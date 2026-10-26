# Accepted Emulator Decisions

The user confirmed the following scope and experience preferences after the initial Phase 1 work.

## Current scope

- Full Sub-GHz feature parity is included in the current emulator work (user decision on 2026-10-26). Simulate the module and radio operations locally.
- Default to the available virtual modules needed by supported workflows. The current seed uses Grove, MBus and Internal; it does not require physical hardware.
- Missing modules, disconnections and missing SD cards are alternate scenarios, not the default opening experience.
- Use cyberpunk names for synthetic networks and devices.
- Keep generated Markdown documentation in English.

The source inventory and acceptance checks now include Sub-GHz screens and scenarios. Synthetic signals and virtual SD files remain browser-session data.

On 2026-09-10 the user supplied a SubGHz UART command reference. Its
[protocol intake notes](SubGHz_Protocol_Notes.md) preserve implementation
implications and inconsistencies to resolve. The 2026-10-26 decision replaces the earlier deferral.

## Volatile demo data (2026-09-12)

The user explicitly excluded persistent virtual SD. Generated files and guided
story progress live only for the current page session and are discarded on
reload/reset. Do not add IndexedDB or durable file storage for demo data.

## Timing behavior

- Default to shortened demo operations.
- Offer a realistic timing mode using the production timing contracts where verified.
- The initial proposed demo setting is a 10x virtual device-job clock; realistic mode is 1x. The exact demo multiplier is an implementation choice, not an additional user requirement.
- Advance operation progress, scheduled responses and consumer deadlines on the same device clock so acceleration does not create artificial timeouts.
- Keep display animation and user-input timing separate from accelerated device jobs.
- Change timing modes while idle in the initial implementation; do not rescale a running operation halfway through.

## Remaining ownership

No further product preferences are needed from the user to continue Phase 1. Control contracts, semantic identifiers and the retained-code/adapter boundary review are engineering tasks. Their completion must be supported by code evidence; accepting these preferences does not by itself close those tasks.
