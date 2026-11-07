#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
manifest="${1:-tests/library_hashes.sha256}"
diff -u <(git ls-files -- rootfs/system/lib64 rootfs/system/bin/linker64 | LC_ALL=C sort) \
        <(sed -n 's/^[0-9a-f]\{64\}  //p' "$manifest" | LC_ALL=C sort)
sha256sum --check --strict --quiet "$manifest"
echo "Bundled Android runtime hashes verified"
