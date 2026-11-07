First experimental release of Elden Ring RT Batching.

- Batches eligible independent vegetation BLAS refits using a modified ReShade 6.8.0 runtime.
- Preserves every-frame vegetation updates and the selected ray-tracing quality by default.
- Includes a standalone offline ME3 profile, settings, installation instructions and license notices.
- The portable ZIP contains no CMD/PowerShell scripts, other mods, saves or captured game shaders. Launch the included .me3 profile after installing ME3.
- Includes ZIP/member SHA-256 checksums and a manifest identifying the source commit and binary hashes.

Validation: GitHub Actions builds the x64 runtime, add-on and standalone GPU test, runs CPU-only vegetation-policy checks, and verifies the complete portable archive. Hosted CI does not execute the DXR GPU test or launch Elden Ring. These newly compiled binaries are not the exact original in-game-tested hashes; the original RTX 5090 measurements and GPU correctness results are historical evidence for the implementation, not a new in-game test of this release.

This release is experimental. It does not unlock the FPS cap. See README.txt in the ZIP for installation, save-file behavior, optional ReShade effects and limitations.
