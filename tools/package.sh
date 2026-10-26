#!/usr/bin/env bash
# Package a built histsim executable with its docs into a zip.
#
#   tools/package.sh <build-dir> <version-label> <platform-name>
#
# Writes histsim-<version>-<platform>.zip in the current directory and prints
# its name. With PACKAGE_NO_ZIP=1 it leaves the folder instead (CI uploads it as
# an artifact, which GitHub zips itself). Used by the CI and Release workflows.
set -euo pipefail

build_dir=$1
version=$2
platform=$3
root=$(cd "$(dirname "$0")/.." && pwd)

pkg="histsim-${version}-${platform}"
rm -rf "$pkg" "$pkg.zip"
mkdir "$pkg"

if [ -f "$build_dir/cli/Release/histsim.exe" ]; then
    cp "$build_dir/cli/Release/histsim.exe" "$pkg/"
elif [ -f "$build_dir/cli/histsim.exe" ]; then
    cp "$build_dir/cli/histsim.exe" "$pkg/"
else
    cp "$build_dir/cli/histsim" "$pkg/"
fi
cp "$root/LICENSE" "$root/README.md" "$pkg/"

cat > "$pkg/QUICKSTART.txt" <<'EOF'
histsim - human history simulator (milestone M0, foundations)

Double-click histsim (histsim.exe on Windows) to run a 1,000-year demo.

From a terminal you can run your own worlds:

  histsim run --seed 7 --level 6 --years 2000 --report-every 200
  histsim run --years 500 --save run.hsim
  histsim resume run.hsim --years 500
  histsim world --seed 7
  histsim --help

--seed   picks the world and its history (same seed = same result)
--level  grid detail: 5 = 10,242 cells (fast), 7 = 163,842 cells
--years  how long to simulate, in years since the run began

This build is a headless runner for the simulation core. The map interface
arrives in later milestones.

Windows may show a SmartScreen warning because the executable is not
code-signed: choose "More info", then "Run anyway".
EOF

if [ "${PACKAGE_NO_ZIP:-0}" = "1" ]; then
    echo "$pkg"
    exit 0
fi

if command -v zip > /dev/null; then
    zip -qr "$pkg.zip" "$pkg"
else
    7z a -tzip "$pkg.zip" "$pkg" > /dev/null
fi
rm -rf "$pkg"
echo "$pkg.zip"
