#!/usr/bin/env bash
# verify.sh — fast feedback loop for sappkit.
# Builds core+CLI+tests (plugin skipped for speed unless build/ already has it)
# and runs both this repo's tests and a CLI smoke check.

set -e
cd "$(dirname "$0")"

echo "▶ configure"
if [ ! -d build ]; then
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DSAPPKIT_BUILD_PLUGIN=OFF > /dev/null
fi

echo "▶ build"
cmake --build build -j8 --target SappKitTests sappkit-cli 2>&1 | grep -E "error|FAILED" && exit 1 || true

echo "▶ tests"
./build/SappKitTests --reporter compact | tail -2

echo "▶ cli smoke"
./build/sappkit params > /dev/null
./build/sappkit pads --diagnostic | head -c 120; echo " ..."

echo "✓ verify passed"
