#!/usr/bin/env sh
set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
kit_root=$(CDPATH= cd -- "$script_dir/../.." && pwd)
if [ "$#" -gt 0 ]; then
    mkdir -- "$1"
    output_dir=$(CDPATH= cd -- "$1" && pwd)
else
    output_dir=$(mktemp -d "${TMPDIR:-/tmp}/vcvr-mip-tests.XXXXXXXX")
fi

# Test the production queue and upload functions, with host-side Vulkan mocks.
awk '
    /^struct GeneratedMipJob/ { found = 1 }
    /^\/\/ Creates a texture directly from DXT blocks/ {
        if (found) { done = 1; exit }
    }
    found { print }
    END { if (!found || !done) exit 1 }
' "$kit_root/librw/src/vulkan/vkraster.cpp" > "$output_dir/mip-worker-extracted.inc"

"${CXX:-c++}" -std=c++17 -pthread -Wall -Wextra -Werror -O2 \
    -I "$output_dir" "$script_dir/mip-worker-test.cpp" \
    -o "$output_dir/mip-worker-test"
"$output_dir/mip-worker-test"
printf 'Test artifacts: %s\n' "$output_dir"
