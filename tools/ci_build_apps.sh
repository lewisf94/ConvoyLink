#!/usr/bin/env bash
# Build every ESP-IDF app under firmware/apps/. Used by CI and locally:
#   . ~/esp-idf/export.sh && ./tools/ci_build_apps.sh
# Passes trivially while no apps exist yet, so CI is green from day one.
set -euo pipefail
cd "$(dirname "$0")/.."

shopt -s nullglob
projects=(firmware/apps/*/CMakeLists.txt)

if [ ${#projects[@]} -eq 0 ]; then
    echo "No firmware apps yet - nothing to build."
    exit 0
fi

fail=0
for cmk in "${projects[@]}"; do
    app_dir=$(dirname "$cmk")
    echo "::group::Building ${app_dir}"
    if idf.py -C "$app_dir" set-target esp32 && idf.py -C "$app_dir" build; then
        echo "::endgroup::"
    else
        echo "::endgroup::"
        echo "BUILD FAILED: ${app_dir}"
        fail=1
    fi
done

if [ $fail -ne 0 ]; then
    exit 1
fi
echo "All ${#projects[@]} firmware app(s) built OK."
