#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd -- "$script_dir/../.." && pwd)"
jobs="${EMULATOR_JOBS:-4}"
[[ "$jobs" =~ ^[1-9][0-9]*$ ]] || { echo 'EMULATOR_JOBS must be a positive integer.' >&2; exit 1; }
if [[ "${1:-}" != '--local' ]]; then
    image='tab5-ui-emulator:emsdk-4.0.14'
    docker build --tag "$image" --file "$script_dir/Dockerfile" "$script_dir"
    exec docker run --rm --mount "type=bind,source=$repo_root,target=/repo" --env "EMULATOR_JOBS=$jobs" "$image"
fi
cd -- "$repo_root"
python3 tools/ui_emulator/prepare_browser.py
emcmake cmake -S tools/ui_emulator -B tools/ui_emulator/build -DCMAKE_BUILD_TYPE=Release
cmake --build tools/ui_emulator/build --parallel "$jobs"
test -s tools/ui_emulator/dist/emulator.js
test -s tools/ui_emulator/dist/emulator.wasm
test -s tools/ui_emulator/dist/index.html
echo 'Static emulator built in tools/ui_emulator/dist/'
