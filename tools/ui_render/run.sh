#!/bin/bash
set -euo pipefail
PYTHON=/opt/esp/python_env/idf6.0_py3.12_env/bin/python
$PYTHON /work/prepare.py
bash /work/build.sh
$PYTHON /work/render_all.py
$PYTHON /work/publish.py
