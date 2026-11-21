#!/bin/bash
# Build inside WSL. Toolchain + libdragon live in ~/n64dev.
set -e
export N64_INST="${N64_INST:-$HOME/n64dev/tc/opt/libdragon}"
export PATH="$N64_INST/bin:$PATH"
cd "$(dirname "$0")"
make -j8 "$@"
