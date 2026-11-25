# Validation

## Original binary validation

The original game-tested hashes are recorded in `upstream.json`. The source was reorganized for publication and received license headers; the batching algorithm was not changed during repository preparation.

Platform: RTX 5090, Ryzen 7 9800X3D, 3840×2160, game 1.17.2 / calibrations 1.17.3, RT Maximum, offline ME3 session.

A fixed Four Belfries view was sampled in original/batched/original/batched order, with every-frame vegetation updates. The original runs were 28.65–29.09 FPS; batched runs were 53.17–53.93 FPS. Combined averages were 28.88 and 53.54 FPS. Normal FPS captures did not have the add-on's GPU timestamp capture active.

Other views and weather transitions gave different results, including substantially higher FPS. The slower state was not reproduced in every later visit, and rain was not established as its cause. Do not compare separate scenes as a controlled performance experiment.

Short movement/camera checks reported no visible changes. This does not establish correctness in all areas or long sessions.

## Standalone GPU test

`tests/native_batch_test.cpp` compiles the canonical batching implementation with a small device adapter. It compares 32 batched and serial refits at matching GPU addresses using 614,400 serialized bytes, and checks:

- Nonoverlapping placed resources, alias rejection and re-enabling after alias destruction.
- Repeated destinations and input transitions as flush boundaries.
- Scratch reuse and an intentionally blocked GPU submission preventing in-flight reuse.
- Device health and DirectX debug-layer errors when the layer is installed.

The original output is retained in [original-gpu-test.txt](original-gpu-test.txt). Its zero-error debug-layer result applies to the standalone test, not to the running game.

## Rebuilds

Publication preparation verified the pinned integration patch on a fresh checkout, source/package hygiene, and packaging with the original tested binaries. The first local full build was interrupted during native compilation. The GitHub Actions workflow now records fresh compilation, CPU-policy and package checks separately; a hosted CI pass does not imply a new GPU or in-game test pass.

A successful build or standalone test does not prove that a newly built runtime is interchangeable in every game scene. Record compiler/SDK versions, run the test, and perform an offline in-game smoke test before publishing changed binaries. The public package manifest distinguishes original tested hashes from rebuilt artifacts.

## DLL loading integration test

Starting with 1.0.3, CI also executes `runtime_load_test.exe` against the built DLL and add-on using the D3D12 WARP software adapter. The host executable is outside the package directory and has no ReShade configuration. The test checks DLL-local settings, all batching exports, automatic add-on discovery, native bridge activation, and the D3D12 device callback. This is a loading test; it does not validate DXR correctness, in-game visuals, or the ME3 Manager GUI.

## Linux / Proton measurements (1.1.0 development)

Platform: RTX 5090 (driver 620.73), Ryzen 7 9800X3D, Arch Linux, GE-Proton11-14 (vkd3d-proton cae8353e), `VKD3D_CONFIG=no_descriptor_heap`, 3840×2160, RT Maximum, offline ME3 session. Each launch restored the same save; the character stood still at a fixed outdoor view. FPS is the add-on's frame counter over 20-second segments. Settings that can change at runtime were alternated within one session; launch-time settings were alternated across launches (ABAB). Launch-to-launch noise was about ±1 FPS.

| Configuration | FPS (segments) |
| --- | --- |
| Batching off (`BatchMode=0`) | 64.2–67.0 |
| Vegetation batching (1.0.3 behavior) | 91.6–97.2 |
| All-animation batching (`BatchAllAnimation=1`) | 98.4–100.6, higher than vegetation-only in every paired segment |

GPU timestamps at this view: about 250 BLAS refits per frame (235 vegetation, 12 skinned, 2 other animated), 517 vegetation animation dispatches, 2 ray dispatches. Batching reduced vegetation refit time from about 5.7 ms to under 0.5 ms per frame. All-animation batching removed about 0.3 ms more, mainly because skin animation dispatches no longer split vegetation groups.

Rejected experiments, measured the same way:

- Holding post-animation transitions, dropping UAV barriers that only order queued builds, and issuing pre-animation transitions as enhanced barriers without compute sync: 96.5 vs 95.2 FPS (within noise). Vegetation animation dispatches went from only 1.10 to 1.02 ms, so their cost is shader work rather than barrier drains.
- Rewriting refitted BLAS from `PREFER_FAST_BUILD` to `PREFER_FAST_TRACE`: 86.4 vs 95.3 FPS. Rays got 7% faster, but refits cost two to three times more.
- The same rewrite for static BLAS had nothing to change: the game already builds all static BLAS with `PREFER_FAST_TRACE`.

The standalone GPU test (`native_batch_test.exe`, published by CI) passed on the RTX 5090 under GE-Proton11-14 with vkd3d-proton. Arena right-sizing reduced scratch use at this view from 136 MiB to about 4 MiB, without changing FPS. Visual comparison of the character and vegetation with all-animation batching on and off showed no differences beyond normal motion. Long sessions and other areas were not tested.
