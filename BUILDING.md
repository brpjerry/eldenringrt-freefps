# Building on Windows

## Prerequisites

- Git and Python 3.11 or newer, available on PATH.
- Visual Studio 2022 or Build Tools 2022 with Desktop development with C++, the v143 x64 toolset, and Windows SDK 10.0.26100.0.
- Internet access for the pinned ReShade sources, submodules and Python packages.
- A DXR-capable GPU for the optional GPU test. Install Windows Graphics Tools to enable the DirectX debug layer.

The original game-tested runtime used MSVC 14.38; the original add-on/test used 14.44. Both used SDK 10.0.26100.0. A rebuild can have different hashes because of compiler versions and build metadata; the scripts do not claim bit-for-bit reproducibility.

## Prepare and build

From the repository root:

```powershell
python scripts/prepare.py
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-build.txt
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -RunGpuTests
```

The preparation script checks out ReShade commit `18deaa52de0c425a78b329e9cb3c497281cd00ec`, initializes its submodules, applies the integration patch, and copies the canonical native files into the dependency checkout. It also supplies the matching Khronos XML registries to GLAD's reproducible-generation directory.

Edit native code in `src/native/`, then rerun preparation. Do not maintain separate edits in `external/reshade`. Unexpected patch conflicts fail instead of resetting local changes.

The build script discovers VS 2022 through vswhere, enters its x64 developer environment, and builds the runtime, add-on and test. It creates:

- `build/bin/ReShade64.asi`
- `build/bin/RT-Pass-Probe.addon64`
- `build/tests/native_batch_test.exe`
- Build/test logs under `build/`

Use `-Target Native`, `-Target Addon` or `-Target Test` to build one component. `-RunGpuTests` runs the standalone test when Test or All is selected. The Test target needs the Windows toolchain and GPU, but does not need the ReShade checkout or Python environment.

Native compilation defaults to one compiler worker; use `-Jobs` to change it.

The native build accepts `-SdkVersion` for another installed SDK; that combination needs its own validation. Close the game before replacing any installed DLLs. Building does not install anything or launch the game.

## Package

```powershell
python scripts/package.py --binaries build/bin --output dist
```

The output is `dist/EldenRing-RT-Batching-1.0.0-portable.zip` plus a SHA-256 file. The archive contains only the two runtime files, four package templates, license notices, a manifest and checksums. It includes no developer scripts, source checkout, captures, game data or other mods.

The packager records actual binary hashes. It reports whether they match the original game-tested binaries; a freshly compiled artifact does not inherit that validation merely because compilation passed. Run the GPU test and an offline in-game check before claiming hardware/game validation for a new binary build. CI-built experimental releases explicitly distinguish compilation/package checks from those hardware tests.

## Check repository contents

```powershell
python scripts/check_repo.py
git diff --check
```

The check scans Git-visible files for unwanted binaries, captures, saves, personal Windows paths and common credential patterns. It also checks source/package layout, the pinned integration patch, and the public profile. It is a release hygiene check, not a guarantee that every possible secret has been detected.

The source repo deliberately excludes generated dependencies, build outputs, diagnostic saves and local customized profiles. Upload the portable ZIP as a release asset rather than committing compiled binaries.

## CI and releases

The `Build and release` workflow runs for pushes to main, pull requests, version tags, and manual dispatches. It uses Windows Server 2022, VS 2022/v143, SDK 10.0.26100.0 and Python 3.12. Action dependencies are pinned to immutable commits. The build job has read-only repository permissions; only the successful tag-release job can write releases.

Each run prepares the pinned dependencies, builds both DLLs and the standalone GPU test, runs the CPU vegetation-policy test, and verifies the archive allowlist, x64 PE headers, defaults, source commit and all checksums. ZIPs and build logs are uploaded as Actions artifacts. Hosted runners do not execute the DXR test or Elden Ring.

For a release, update `version.json` and `docs/RELEASE_NOTES.md`, push the commit, and wait for the main build. Then create and push the matching tag, for example `v1.0.0`. The tag workflow rebuilds that exact commit, verifies its package, creates a draft release, checks uploaded asset digests, and publishes it. A mismatched tag fails, and a published release is never overwritten. The `prerelease` value in `version.json` controls its experimental label.

The packager reads the version from `version.json`; it records the source commit, binary hashes and available compiler/build metadata. No private account token is needed in repository secrets: the release job uses the built-in scoped GitHub token.
