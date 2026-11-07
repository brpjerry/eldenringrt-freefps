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
