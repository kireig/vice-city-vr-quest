#!/usr/bin/env bash
# Update the source kit, build and replace only the existing Quest APK.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec bash "$SCRIPT_DIR/tools/update-and-install.sh" "$@"
