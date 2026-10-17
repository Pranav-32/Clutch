#!/usr/bin/env bash
# Run the shared Linux/macOS verification workflow when a development container
# is created. Also safe to rerun manually from any working directory.
set -euo pipefail

# Locate the mounted checkout independently of the client's starting directory.
source_dir="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)"

# Windows bind mounts can report a different owner from the development user.
# Trust this exact checkout inside Docker only; never configure a wildcard or
# change the host's Git settings when the script is run natively.
if [[ -f /.dockerenv && -e "$source_dir/.git" ]]; then
    if ! git config --global --get-all safe.directory | grep -Fxq -- "$source_dir"; then
        git config --global --add safe.directory "$source_dir"
    fi
fi

# Keep container artifacts separate and export the database used by clangd.
export CC="${CC:-clang}"
export CXX="${CXX:-clang++}"
export CLUTCH_TEST_BUILD_DIR="$source_dir/build-container"
export CLUTCH_TEST_EXPORT_DATABASE=ON

# Replace this wrapper so initialization reports the verification runner's status.
exec bash "$source_dir/tests/verify.sh"
