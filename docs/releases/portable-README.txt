SimForge v0.1.0 - Windows x64 portable release

1. Extract the complete ZIP to a folder.
2. Install Microsoft's x64 Visual C++ v14 Redistributable, 14.51.36231 or later:
   https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist
3. Ensure your graphics driver supplies Vulkan 1.1, swapchain and
   swapchain-maintenance1 presentation fences. Vulkan 1.1 alone is insufficient.
4. Launch simforge_sandbox.exe. Keep SDL3.dll and examples beside it.
5. Click Open warehouse workspace, then Simulation. Play/Pause, Step and Reset
   control playback. Left-drag orbits, middle-drag pans, scroll zooms.
6. Use File > Save Project As to save to a writable folder you choose.

Read docs/windows-release.md for all requirements, examples and troubleshooting.
Start with examples/warehouse-baseline.json; warehouse-constrained.json shows
a bottleneck. stochastic-coverage/homogeneous.json uses seeded Poisson arrivals.
checkpoint-interventions/forklift.json is a study request, not a scenario file.
simforge_headless.exe provides command-line analysis without a graphics runtime.

No compiler or Vulkan SDK is needed. The runtime and driver are prerequisites.
Automatic recovery normally uses %APPDATA%\SimForge\SimForge; saves and exports
use locations you choose. No automatic writes belong in the application folder.

The window title shows the version. Run either executable with --version for
the exact source commit; build-info.json and package-inventory.json record it.
LICENSE covers SimForge; NOTICE, THIRD_PARTY_NOTICES.txt and licenses/ preserve
dependency terms. Roboto font license/provenance are in assets/fonts/.

Tested graphics: NVIDIA GeForce RTX 5080 Laptop GPU on Windows 11 Home 26H2.
Other machines/OS versions are unverified. Full clean-machine Vulkan testing
remains unavailable. This executable is unsigned. See release validation before
publication; the ZIP itself does not imply publication approval.
