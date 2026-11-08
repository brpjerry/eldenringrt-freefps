# Architecture

## Recognition and dispatch

The add-on recognizes vegetation animation from available pipeline debug names and checks shader-model bytecode. The observed shaders were compute Shader Model 6.0; the eligibility gate allows pre-RayQuery compute models through 6.4. It tags qualifying dispatches for the native runtime. Unknown shaders are conservative boundaries.

The add-on resolves the native bridge from the same ReShade module handle used for add-on registration. It no longer depends on the old `.asi` filename. The packaged runtime is `EldenRingRT.dll`; its initialization treats that filename as a portable native module, keeping settings and add-on discovery beside the DLL. Stock ReShade and renamed older releases lack this initialization change.

The ME3 profile loads the DLL early. Its `config` companion list tells ME3 Manager to copy the add-on, settings, documentation and notices alongside the native DLL when importing the profile. These entries are manager metadata; the runtime still loads `RT-Pass-Probe.addon64` through ReShade's normal add-on mechanism.

## Eligible refits

Only supported in-place BLAS refits on direct command lists qualify. Geometry descriptors are copied; inputs, transforms, destinations and scratch ranges are checked. Initial builds, unsupported/postbuild work, TLAS construction and nonqualifying geometry use the original path.

Committed resources are marked. Placed resources are checked against their heap allocation and live aliases. Unknown, reserved, shared and overlapping allocations are excluded.

## Scheduling and synchronization

The runtime defers eligible independent refits across safe vegetation animation dispatches, using separate 256-byte-aligned scratch regions. Groups are bounded by 64 builds and an 8 MiB arena. Original resource barriers remain and additional UAV barriers bound groups.

Dependency transitions, aliasing, repeated destinations, unknown compute, ray dispatch, draws, copies, queries, indirect work, TLAS builds and command-list completion flush queued work. Enhanced barriers and predication disable batching for that list until reset.

Scratch arenas are retained by command lists and queue submission receipts. Reuse requires released list ownership and completed fences from every recorded submission. The pool is capped at 512 MiB; failure falls back to original work. Command-list boundaries and submission ordering remain intact.

## Update cadence

`UpdateInterval=1` preserves every update. The optional interval 2 policy uses a stable address hash to distribute vegetation across frames, while preserving first use, reappearance and repeated same-frame builds. It can leave the RT representation one frame behind moving vegetation.

## Development features

The current add-on retains status/command IPC, short timestamp captures, audits and startup dumps of up to eight observed animation shaders. These are development features; the public package does not include live-control scripts. Timestamp allocations occur only on a capture request.

Do not interpret a profiler label as an intrinsic pass cost when batching moves work across labels. Do not publish locally dumped game shaders, saves, traces or logs containing machine paths.
