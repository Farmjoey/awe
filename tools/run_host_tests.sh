#!/bin/sh
# Compiles and runs the pure-logic unit tests on the host (C++11, same as the ESP32 core).
# Widget layouts are compiled by the app's JavaScript first, so firmware and app are tested against each other.
set -e
cd "$(dirname "$0")/.."
if command -v node >/dev/null 2>&1; then
  rm -rf /tmp/wbin
  W=$(ls app/widgets/starter/*.json | grep -v index.json)
  node tools/widget_cli.js check $W
  node tools/widget_cli.js bin /tmp/wbin $W
  node tools/widget_cli.js header /tmp/DefaultLayout.h app/widgets/starter/standard.json
  cmp /tmp/DefaultLayout.h src/DefaultLayout.h || { echo "src/DefaultLayout.h is out of date: run node tools/widget_cli.js header src/DefaultLayout.h app/widgets/starter/standard.json"; exit 1; }
else
  echo "node not found: widget cross-test skipped"; exit 1
fi
g++ -std=c++11 -Wall -Wextra -Werror -O1 test/host/test_logic.cpp -o /tmp/nova_host_tests
/tmp/nova_host_tests
