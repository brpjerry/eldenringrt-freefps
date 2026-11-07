# Third-party notices

## ReShade

The runtime integration patch applies to [crosire/reshade v6.8.0](https://github.com/crosire/reshade/tree/v6.8.0), commit `18deaa52de0c425a78b329e9cb3c497281cd00ec`. ReShade is copyright Patrick Mours and is distributed under BSD 3-Clause. Its notice is preserved in [licenses/ReShade-LICENSE.txt](licenses/ReShade-LICENSE.txt).

The new batching implementation is in `src/native/`; integration changes and surrounding upstream context are in `patches/reshade-v6.8.0-integration.patch`. New contributions use the root BSD 3-Clause license; the upstream notice continues to apply to ReShade-derived material.

## Development profiler

`src/addon/gpu_profiler.cpp`, `gpu_profiler.hpp` and `d3d12_util.hpp` derive from Mason Petrosky's [EldenRingDLSS](https://github.com/masonpetrosky/EldenRingDLSS/tree/1fddbb6ce895dd497c7d9d1297f1d682c5ad7a26), commit `1fddbb6ce895dd497c7d9d1297f1d682c5ad7a26`. These files retain MIT SPDX labels and the [Mason Petrosky MIT notice](licenses/Mason-Petrosky-MIT.txt). The root BSD license does not replace this license.

No DLSS runtime, other mod binary or extracted game shader is included.

## ReShade dependencies

ReShade's pinned submodules remain under their upstream licenses. The available dependency notices are preserved under `licenses/deps/` and carried into binary packages. Additional notices embedded by upstream ReShade remain in its runtime. Fetching or building the dependency checkout does not relicense those components.
