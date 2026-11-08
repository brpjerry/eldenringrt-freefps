# Elden Ring RT Batching

[![Build and release](https://github.com/brpjerry/eldenringrt-freefps/actions/workflows/build.yml/badge.svg)](https://github.com/brpjerry/eldenringrt-freefps/actions/workflows/build.yml)

An experimental DirectX 12 mod that batches independent vegetation ray-tracing acceleration-structure refits in Elden Ring. The default mode preserves every-frame vegetation updates and the game's RT quality settings.

The project consists of a ReShade add-on and a small native integration patch against a pinned ReShade revision. A stock ReShade runtime does not include the required batching hooks.

## Install the packaged mod

Download the portable ZIP from [Releases](https://github.com/brpjerry/eldenringrt-freefps/releases), extract it into a writable folder outside the game's `Game` directory, and double-click `eldenring-rt-batching.me3` after installing [ME3](https://github.com/garyttierney/me3/releases/latest). The profile runs offline and uses the game's normal save file (`ER0000.sl2`), with no savefile override. Enable ray tracing in the game and reload your character after changing RT.

For ME3 Manager, import the ZIP or its `.me3` profile. The native entry is `EldenRingRT.dll` with early loading enabled; the profile also lists its companion files so the manager copies the complete mod.

See [the installation instructions](package/README.txt) for save handling, optional ReShade effects, configuration and removal. The published package contains no CMD/PowerShell launchers or live-control scripts. It does not unlock the game's FPS cap.

## Build from source

See [BUILDING.md](BUILDING.md). The scripts discover the Visual Studio installation and use paths relative to the checkout. ReShade and its pinned submodules are fetched into ignored `external/`; no game files are needed to build.

The developer build scripts remain in this source repository. They are excluded from the player-facing ZIP.

## How it works

Recognized vegetation animation is followed by eligible in-place BLAS refits. The runtime defers independent refits, assigns separate scratch regions, and emits groups of up to 64. Original barriers remain, dependency boundaries flush queued work, and GPU fences control scratch reuse. Unknown or unsupported work retains its original path.

Full-rate mode skips no updates. Extra scratch memory and CPU bookkeeping are the main costs; observed scratch use was 128–224 MiB with a 512 MiB pool ceiling. Optional two-frame updates trade vegetation RT freshness for additional performance.

[Architecture](docs/ARCHITECTURE.md) describes the dependency checks and fallback behavior.

## Validation and limits

The original binaries were tested offline on an RTX 5090 with game 1.17.2 / calibrations 1.17.3. One controlled Four Belfries view at 4K RT Maximum measured about 28.9 FPS original versus 53.5 FPS batched. Performance varies by view, lighting and workload; this does not establish a universal gain or rain-related cause.

A standalone real-GPU test checks serialized BLAS results, allocation aliases, dependency boundaries and fence-delayed scratch reuse. See [validation notes](docs/VALIDATION.md). Short in-game movement checks looked normal; long sessions, all areas, other GPUs and combinations of hooks have not been validated.

This is an unofficial modified ReShade build. Use the offline profile with EAC disabled through the modded launch route. It is not affiliated with FromSoftware, Bandai Namco, NVIDIA or the ReShade project.

## Source layout

- `src/addon/`: vegetation recognition, cadence policy and optional development profiling.
- `src/native/`: canonical native batching implementation.
- `patches/`: integration changes applied to pinned ReShade.
- `tests/`: standalone DirectX 12 correctness test.
- `scripts/`: dependency preparation, build, package and source checks.
- `package/`: public profile, configuration and installation instructions.
- `licenses/`: preserved upstream notices.

## CI and releases

Pull requests and pushes to main build the x64 runtime and tests on Windows, run CPU policy checks and a D3D12 WARP loading test, and upload a verified portable ZIP. Tags matching `version.json` publish an experimental release after the build succeeds. The hosted runner compiles the DXR test but cannot run it or the game. See [BUILDING.md](BUILDING.md#ci-and-releases).

New project code is licensed under [BSD 3-Clause](LICENSE). ReShade and the borrowed profiler retain their own notices; see [THIRD_PARTY.md](THIRD_PARTY.md).
