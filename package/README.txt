ELDEN RING RT BATCHING - PORTABLE PACKAGE
Experimental release; see manifest.json for version and source commit.

QUICK INSTALL

1. Install ME3 for Windows from its official release page:
   https://github.com/garyttierney/me3/releases/latest
   Choose me3_installer.exe. ME3 is a separate requirement, not bundled here.

2. Extract the entire ZIP into a writable folder of your choice, outside the
   game's Game folder. Keep every file inside RT-Batching together. Do not run
   files directly from inside the ZIP. No files need to be copied into Game.

3. Start Steam and close any existing Elden Ring session. Double-click
   "eldenring-rt-batching.me3". The ME3 installer registers this file type.

4. Enable ray tracing in the game's graphics menu. After changing RT, quit
   to the title menu and reload your character to apply it. This mod does
   not change resolution or RT quality automatically.

5. Batching starts enabled with every-frame vegetation updates. To confirm
   loading, open the generated probe.log in this folder and look for:
     RT batch experiment loaded; interval=1 native bridge=1

REQUIREMENTS AND SCOPE

Windows x64, DirectX 12, an RT-capable GPU, Elden Ring, Steam, and ME3.
Locally validated with ME3 0.13.0, game 1.17.2 / calibrations 1.17.3, RTX 5090.
Other game versions, GPUs and driver combinations have not been validated.
Use the supplied offline ME3 profile with Easy Anti-Cheat disabled by the
modded launch route. Launch through this profile, not the protected launcher.
No compiler, profiler, FPS unlocker, or separate ReShade installation is needed.
The game retains its normal 60 FPS cap unless you install a separate unlocker.
This package includes no other gameplay mods, shaders, presets, or save files.

SAVES

This standalone profile uses rt-batching.sl2 in the game's normal save folder.
ME3 copies an existing base save to that name when it does not already exist.
Subsequent progress stays in that separate save; it does not sync back to the
base save or to another mod profile. Back up saves before changing profiles.
To deliberately use an existing ME3 save, edit only the savefile value in the
profile to its filename while the game is closed. No account ID or absolute
save path belongs in this profile. No saves are modified by installation.

CONFIGURATION

Batching and full-rate vegetation updates are enabled by default. To change
these settings, close the game and edit RTVegetation.ini before launching:
  [Settings]
  UpdateInterval=1
  BatchMode=2
UpdateInterval=1 preserves every-frame updates. Optional UpdateInterval=2
reduces vegetation refits to a two-frame schedule, which can make moving
vegetation's RT representation lag by one frame. BatchMode=2 enables batching;
0 disables it. For original scheduling use BatchMode=0 and UpdateInterval=1.

OPTIONAL RESHADE EFFECTS

The included ReShade64.asi is a custom ReShade 6.8.0 runtime with the batching
hooks. Keep that filename and binary. A stock ReShade runtime lacks these hooks.
Do not overwrite it or install a second ReShade dxgi.dll alongside this runtime.

Normal ReShade effects are expected to work through the included runtime.
Put effect .fx/.fxh files under reshade-shaders/Shaders and textures under
reshade-shaders/Textures, preserving each shader pack's layout. Press Home
in game to open ReShade, reload effects, and select a compatible preset.
No effects or presets are included. Specific presets have not been tested;
depth-based effects may need depth-buffer configuration and add GPU cost.

HOW IT WORKS AND TRADE-OFFS

The custom D3D12 runtime groups eligible independent vegetation acceleration-
structure refits, assigns separate scratch memory, and submits groups of up
to 64. Original resource barriers remain, and groups are flushed before
dependent work. Scratch memory is reused only after GPU completion.

Default full-rate mode skips no vegetation updates and does not lower ray
resolution or RT quality. Unrecognized or unsupported work keeps its original
path. Extra scratch VRAM was 128-224 MiB in tested scenes, with a 512 MiB pool
ceiling. CPU bookkeeping and GPU power use can increase. Gains depend on the
scene; rain alone was not established as the cause of the original slowdown.

Controlled fixed-view testing at Four Belfries, 4K RT Maximum, measured about
28.9 FPS original versus 53.5 FPS batched. Other views reached higher FPS;
these results do not predict every scene. Short movement checks looked normal.
A standalone GPU test of the batching code passed serialization, dependency,
allocation and fence-reuse checks with zero DirectX debug-layer errors. That
debug-layer result applies to the test, not the running game. Long sessions,
all areas, other hooks and every GPU have not been validated.

The package manifest records whether these binaries match the original
game-tested build. Rebuilt binaries require their own validation.
No automatic GPU profiling is active. The diagnostic-capable add-on may create
probe.log, command files and up to eight local animation shader dumps in a
shaders folder. Those generated files are not installation dependencies and
are not included in the ZIP. ReShade also creates local logs and settings. Do not redistribute your used folder wholesale; share the ZIP.

TROUBLESHOOTING AND REMOVAL

- If double-clicking the .me3 file does not open ME3, reinstall ME3 using its
  Windows installer to restore the file association. With me3.exe on PATH,
  you can also open a terminal in this folder and run:
    me3 launch -g eldenring -p ".\eldenring-rt-batching.me3"
- No effect: confirm RT is active after reloading, native bridge=1 in probe.log,
  and BatchMode=2 in RTVegetation.ini. A 60 FPS cap can hide gains above 60.
- Read probe.log and ReShade.log for loading errors. If adding effects, first
  try with those effects disabled. Quit normally before replacing files.
- To uninstall, close the game and delete this extracted folder. The separate
  save remains in the normal save directory. Launch your usual profile or
  Steam afterward. No game executables or global settings are replaced here.

PROVENANCE AND NOTICES

ReShade base: v6.8.0, commit 18deaa52de0c425a78b329e9cb3c497281cd00ec.
This is an unofficial modified build, not an official ReShade release.
See LICENSE, THIRD_PARTY.md and licenses/. SHA256SUMS.txt records the
original package files; user-edited settings will naturally change hashes.

ME3 installation and configuration documentation:
https://github.com/garyttierney/me3/blob/main/docs/user-guide/installation.md
https://github.com/garyttierney/me3/blob/main/docs/configuration-reference.md
ReShade shader documentation:
https://github.com/crosire/reshade-shaders
