#!/bin/bash
set -e
/opt/esp/tools/cmake/4.0.3/bin/cmake -S /work -B /tmp/tab5-build >/tmp/config.log 2>&1
/opt/esp/tools/cmake/4.0.3/bin/cmake --build /tmp/tab5-build --target render -j 12 >/tmp/build.log 2>&1 || { tail -80 /tmp/build.log; exit 1; }
tail -5 /tmp/build.log
