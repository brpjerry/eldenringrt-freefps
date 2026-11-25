RT Batching 1.1.0 batches more refits and no longer reserves large scratch memory.

- Refits after character and other animation are now batched together with vegetation (`BatchAllAnimation=1`, default). On an RTX 5090 at 4K RT Maximum under Proton, this measured about 5% higher FPS than 1.0.3's vegetation-only batching (98–101 vs 92–97 FPS at one fixed view). Set `BatchAllAnimation=0` to restore 1.0.x behavior.
- Scratch memory is sized to the refits: about 4 MiB instead of about 136 MiB at the same view. Fixed 8 MiB per-command-list arenas could fill the 512 MiB pool in long sessions and silently switch batching off.
- The optional two-frame update schedule (`UpdateInterval=2`) still applies only to vegetation.
- CI now publishes the standalone GPU test so it can run on real hardware. It passed on an RTX 5090 under GE-Proton11-14.

To upgrade, extract the ZIP into a fresh folder and replace the previous RT Batching entry. Keep the companion add-on and settings together with `EldenRingRT.dll`. If you keep an old `RTVegetation.ini`, the new default still applies unless the file sets `BatchAllAnimation=0`.

Experimental release: compiled and packaged in CI; the GPU test and in-game measurements ran on Linux/Proton with an RTX 5090. Native Windows was not re-tested for this release.
