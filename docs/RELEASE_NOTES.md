RT Batching 1.0.2 removes the custom save filename from the standalone ME3 profile.

- The public profile now uses Elden Ring's normal `ER0000.sl2` save, with no `savefile` override.
- Installation and removal instructions describe the normal save behavior. Existing custom saves are not moved, deleted or merged automatically.
- Packaging and CI reject a save override in the public profile and record the absence of an override in the manifest.
- Batching remains enabled with every-frame vegetation updates. The profile runs offline.

Download and extract the portable ZIP, then launch `eldenring-rt-batching.me3`. See README.txt for installation. The ZIP contains no launch/toggle scripts, other mods, saves or captured shaders.

This is an experimental CI-built release. The Windows build, CPU tests and package verification run in CI; the DXR GPU test is compiled but not run. These rebuilt binaries have not received a new in-game test.
