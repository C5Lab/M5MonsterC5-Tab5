# Headless LVGL rendering plan

## Scope

Render actual production UI builders with the repository's LVGL version, fonts,
and styles. Hardware I/O is excluded from the host process. Documentation is English.
Generate logical screenshots for all four display rotations and a separate navigation map.

## Implementation and validation

1. Build the checked-in LVGL sources in a Docker host environment.
2. Extract production C declarations and UI function dependencies into a generated
   host translation unit. Keep source provenance; explicitly list substituted functions.
3. Initialize deterministic tab/application state and render each reachable constructor
   in a fresh process. Use synthetic data where required and record unsupported cases.
4. Save PNG output for 0/90/180/270. Distinguish logical UI orientation from native-panel
   pixel placement; host rendering does not test ESP32 PPA or physical touch.
5. Verify dimensions, nonempty output, rendering failures, and representative images.
6. Produce navigation flow diagrams with source evidence independently of rendering.

## Files

- `tools/ui_render/`: extraction, host adapter, container build/run tooling.
- `docs/ui-render/`: PNGs, coverage manifest, English documentation.
- `tools/ui_flow/` and `docs/ui-render/flow/`: independently generated navigation maps.

## Constraints

Do not edit firmware to make screenshots work. Do not execute radio actions or flash
the attached device. Never label reconstructed HTML, AI images, or text diagrams as
actual LVGL screenshots. Report coverage and substitutions rather than claiming full
fidelity for unsupported dynamic states.

## Result

The host executable renders 98 named cases in all four orientations: 392 logical
frames, 392 expected native-panel placement images, and 98 comparison sheets.
All cases completed without timeout or AddressSanitizer errors in the final run.
Five navigation diagrams contain 108 source-validated routes. See
`docs/ui-render/README.md` for fixture and coverage limits; this is not exhaustive
coverage of loaded PCAP analyses or every asynchronous application state.
