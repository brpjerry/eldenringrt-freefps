RT Batching 1.0.3 adds a native DLL package for ME3 Manager.

- Load `EldenRingRT.dll` through ME3. The bundled profile enables early loading and lists every companion file for ME3 Manager to import.
- The custom runtime locates settings and its add-on beside the DLL. The add-on resolves the batching bridge from the active ReShade module rather than the old ASI filename.
- CI loads the actual runtime and companion add-on in a separate test process using D3D12 WARP, checking settings, exports and bridge activation.
- Every-frame batching remains the default. The profile runs offline and has no custom save filename.

To upgrade, extract the ZIP into a fresh folder and replace the previous RT Batching entry with `EldenRingRT.dll`. Keep one runtime enabled. Old releases cannot acquire these loading changes by renaming their ASI file.

Import the ZIP or `eldenring-rt-batching.me3` in ME3 Manager, or launch the included profile directly. Keep the companion add-on and settings together with the DLL. See README.txt for details.

Experimental release: compilation, CPU tests, WARP loading and package checks run in CI. The DXR test is compiled but not run, and no new in-game test is claimed.
