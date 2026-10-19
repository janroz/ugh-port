#!/bin/bash
# Usage: analysis/dump.sh <seg> <start-off> <end-off>   (hex, e.g. 113b 4ca5 4e20)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
proj="${GHIDRA_PROJ_DIR:-/private/tmp/ugh-ghidra}"
lin() { printf '%x' $(( 0x$1 * 16 + 0x$2 )); }
export JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home
"$HOME/Applications/ghidra_12.1.4_PUBLIC/support/analyzeHeadless" "$proj" ugh -process UGH.EXE -noanalysis -readOnly \
  -scriptPath "$root/analysis/ghidra_scripts" -postScript DumpRange.java "$(lin "$1" "$2")" "$(lin "$1" "$3")" 2>&1 \
  | sed -n 's/^INFO  DumpRange.java> //p' | sed 's/ (GhidraScript)[[:space:]]*$//'
