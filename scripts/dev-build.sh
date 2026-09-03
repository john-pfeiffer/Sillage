#!/usr/bin/env bash
# Fast local dev build: configure + build Release, print artifact paths.
#
#   scripts/dev-build.sh [--debug] [--test] [--install]
#
#   --debug    build Debug instead of Release
#   --test     run the test suite after building
#   --install  copy the VST3 into the per-user plugin folder
#
# Dev builds are identified as dev-<shortsha> (used in the tracker's Build field).
set -euo pipefail

cd "$(dirname "$0")/.."

CONFIG=Release
RUN_TESTS=0
DO_INSTALL=0
for arg in "$@"; do
    case "$arg" in
        --debug)   CONFIG=Debug ;;
        --test)    RUN_TESTS=1 ;;
        --install) DO_INSTALL=1 ;;
        *) echo "unknown option: $arg" >&2; exit 2 ;;
    esac
done

BUILD_DIR="build/$CONFIG"
GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$CONFIG" "${GENERATOR_ARGS[@]}"
cmake --build "$BUILD_DIR" --config "$CONFIG" --parallel

SHORTSHA=$(git rev-parse --short HEAD 2>/dev/null || echo "nogit")
echo
echo "== Sillage dev build: dev-$SHORTSHA ($CONFIG) =="
find "$BUILD_DIR" \( -name "*.vst3" -o -name "*.component" -o -name Sillage -type f \) \
    -not -path "*/CMakeFiles/*" 2>/dev/null | sed 's/^/  /'

if [[ "$RUN_TESTS" == 1 ]]; then
    echo
    ctest --test-dir "$BUILD_DIR" --output-on-failure -C "$CONFIG"
fi

if [[ "$DO_INSTALL" == 1 ]]; then
    VST3_SRC=$(find "$BUILD_DIR" -name "Sillage.vst3" -type d | head -1)
    if [[ -z "$VST3_SRC" ]]; then
        echo "no VST3 bundle found to install" >&2; exit 1
    fi
    case "$(uname -s)" in
        Darwin) DEST="$HOME/Library/Audio/Plug-Ins/VST3" ;;
        *)      DEST="$HOME/.vst3" ;;
    esac
    mkdir -p "$DEST"
    rm -rf "$DEST/Sillage.vst3"
    cp -R "$VST3_SRC" "$DEST/"
    echo "installed to $DEST/Sillage.vst3"
    if [[ "$(uname -s)" == Darwin ]]; then
        AU_SRC=$(find "$BUILD_DIR" -name "Sillage.component" -type d | head -1)
        if [[ -n "$AU_SRC" ]]; then
            AU_DEST="$HOME/Library/Audio/Plug-Ins/Components"
            mkdir -p "$AU_DEST"
            rm -rf "$AU_DEST/Sillage.component"
            cp -R "$AU_SRC" "$AU_DEST/"
            echo "installed to $AU_DEST/Sillage.component"
        fi
    fi
fi
