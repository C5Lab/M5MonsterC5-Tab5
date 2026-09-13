# Accepted Emulator Decisions

The user confirmed the following scope and experience preferences after the initial Phase 1 work.

## Current scope

- SubGHz is excluded from the current emulator milestone. Do not request its firmware repository to close Phase 1.
- Default to the available virtual modules needed by supported workflows. The current seed uses Grove, MBus and Internal; it does not require physical hardware.
- Missing modules, disconnections and missing SD cards are alternate scenarios, not the default opening experience.
- Use cyberpunk names for synthetic networks and devices.
- Keep generated Markdown documentation in English.

The complete source inventory may still contain SubGHz references because it records the firmware. Those references are deferred scope, not a requirement for the current simulator. The seed contains no SubGHz module, signals, files or variants. Its empty signal schema slot is reserved for later compatibility.

On 2026-09-10 the user supplied a SubGHz UART command reference. Its
[protocol intake notes](SubGHz_Protocol_Notes.md) preserve implementation
implications and inconsistencies to resolve. This is new reference material;
the user has not yet explicitly replaced the deferred implementation decision.

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
