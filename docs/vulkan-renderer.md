# Vulkan renderer foundation

This document records the renderer foundation and its historical validation.
The current live viewer includes scenario-driven warehouses, camera interaction,
selection and the refined operational dashboard. See the
[interactive viewer guide](interactive-warehouse-viewer.md) for current screenshots,
185-test Debug/Release results, responsive/DPI behavior and measured UI costs.

The renderer now draws the first static 3D scene: a perspective camera, floor,
grid and four colored cubes with depth testing. See [3D pipeline and camera details](first-3d-scene.md)
for shader compilation, geometry/buffer ownership, math conventions, image checks,
Intel compatibility findings and a verified screenshot. There are no warehouse
visuals, camera controls, lighting, transparency, shadows, scene graph or asset database.
The background remains linear RGBA `(0.025, 0.045, 0.075, 1)` (display RGB approximately `44,60,77`).

## Module boundaries

`simforge_engine` owns deterministic scheduling, World/ECS, warehouse state,
snapshots, and replay. It has no dependency on SDL or Vulkan. `simforge_analysis`
continues to depend only on the simulation core and JSON support.

`simforge_platform` owns SDL video initialization, native windows, events, logical
dimensions, actual pixel dimensions, and the SDL Vulkan surface adapter.
`simforge_renderer` depends on platform and Vulkan, with no engine, ECS, analysis,
or warehouse dependency. `simforge_render_policy` contains pure selection logic;
its only Vulkan dependency is headers, not the loader or a live GPU.
`simforge_render_scene` owns camera math, frame validation and built-in CPU geometry;
it links header-only GLM, with no SDL, Vulkan loader or simulation dependency.

```text
engine / warehouse --> future application adapter --> const RenderFrameData
                                                          |
                                                          v
platform (SDL window/surface adapter) <-------------- renderer --> Vulkan
```

`Renderer::render_frame(const RenderFrameData&)` consumes owned camera/object values.
Each object supplies a stable built-in mesh ID, transform and linear RGB tint.
An optional `neutral_material` flag preserves baked vertex luminance while replacing
its hue with the operational tint. It reuses the existing 80-byte push constants and
scene pipeline; the default keeps static materials unchanged. No new draw pass or
warehouse dependency is introduced. Graphical builds embed licensed Roboto through
`cmake/EmbedFont.cmake`; renderer-disabled CPU viewer tests do not read/load the font
or link ImGui. Renderer diagnostics count UI texture creation/update requests; prior
submission completion protects dynamic font uploads and DPI changes.
The renderer retains no caller references after recording. Future application
adapters can extract renderable values from simulation, without exposing mutable World pointers, scheduler
queues, resource managers, or GPU handles. Render assets/caches belong to the
renderer and never enter simulation snapshots. The graphics sandbox polls events
and presents independently of simulation ticks; it does not run a warehouse or
movement workload. Headless results retain their existing timing and semantics.

The public renderer header hides Vulkan behind an implementation pointer.
Context, device, swapchain and Vulkan error/enumeration helpers are private source
headers. There is no generic resource-wrapper framework or global handle registry.
Window and renderer calls must stay on the SDL main thread; neither API provides
concurrent access or support for several renderers on the same window.

## Windows dependencies and build

Use MSVC x64, CMake, Ninja, and the existing vcpkg toolchain from an x64 Visual
Studio developer terminal. A normal PowerShell session does not necessarily have
the compiler's standard-library INCLUDE/LIB environment.

Install the [official LunarG Vulkan SDK](https://vulkan.lunarg.com/sdk/home).
Reopen the terminal so `VULKAN_SDK` identifies that installation. CMake uses
`find_package(Vulkan 1.4.335 REQUIRED)` and imported `Vulkan::Headers` and
`Vulkan::Vulkan` targets through [CMake's supported discovery module](https://cmake.org/cmake/help/latest/module/FindVulkan.html);
no machine-specific SDK path is embedded. That header
minimum supplies the promoted KHR maintenance1 declarations. It does **not** mean
the renderer requests Vulkan 1.4 at runtime: the instance requests Vulkan 1.1.

The GPU driver supplies the loader and WSI implementation. At runtime we require
Vulkan 1.1, `VK_KHR_swapchain`, color attachments, graphics/presentation queues,
and feature-checked swapchain-maintenance1 presentation fences. Both the KHR
and original EXT maintenance1 variants are accepted when their corresponding
instance surface-maintenance extensions are available. The KHR variant is preferred
per device. `VK_KHR_get_surface_capabilities2` is enabled as a dependency. Updating
only SDK headers does not give a driver missing device features.

Presentation fences are a compatibility requirement for provable retirement of
WSI resources at resize and shutdown. Submission fences and device-idle waits do
not by themselves establish presentation-resource retirement. See the
[Khronos semaphore-reuse guide](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
and [swapchain-maintenance1 extension](https://docs.vulkan.org/refpages/latest/refpages/source/VK_KHR_swapchain_maintenance1.html).
No shader, compute, ray-tracing, or discrete-GPU capability is required.

The vcpkg manifest explicitly requests SDL3's optional `vulkan` feature; its core
package alone does not enable Vulkan window creation at the pinned baseline.
The renderer requires the discovered `Vulkan::glslc` compiler. CMake builds GLSL
into SPIR-V and embeds typed words, so executables need no shader-file lookup.

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug
ctest --preset windows-debug --output-on-failure
build/windows-debug/apps/sandbox/simforge_sandbox.exe

cmake --preset windows-release
cmake --build --preset windows-release
ctest --test-dir build/windows-release --output-on-failure
```

For a build without Vulkan package discovery, use a separate directory:

```powershell
cmake --preset windows-release -B build/windows-headless -DSIMFORGE_BUILD_RENDERER=OFF
cmake --build build/windows-headless
build/windows-headless/apps/sandbox/simforge_headless.exe --headless --scenario examples/warehouse-baseline.json
```

SDL remains the existing platform build dependency, but neither headless executable
links it when the renderer is disabled. In a graphics-enabled build,
`simforge_headless` also links neither SDL nor Vulkan. The original
`simforge_sandbox --headless` route links the graphics runtime but never creates a
window, initializes video, constructs a renderer, creates an instance, queries a
GPU, or allocates a swapchain. Use the separately linked executable when even
loading the graphics DLLs must be avoided.

## Window and initialization

The sandbox creates an SDL3 Vulkan window with resizable and high-pixel-density
flags. The Vulkan adapter obtains SDL's required instance extensions and uses
`SDL_Vulkan_CreateSurface`. Its native-window lease keeps the window and SDL video
subsystem alive until surface/instance cleanup completes, even if the Window
wrapper is destroyed first. A close event disables rendering immediately, and
the final lease release destroys the native window. SDL video shutdown tracks
successful initialization independently of the window's open flag.

Instance extensions/layers are enumerated before enabling them. Debug requests
Khronos validation when available and reports its absence. Release defaults to
validation off. When available, synchronization validation is explicitly enabled
through `VK_EXT_validation_features`. The debug messenger reports real warnings
and errors, with separate counters; its callback cannot throw across the C ABI.
`require_validation` makes missing layer/debug-utils support an initialization error.

Each physical device is checked against the actual SDL surface: nonempty graphics
and presentation queues, API/extensions/features, formats with color-attachment
support, surface image usage, presentation modes and depth attachment formats. Rejected devices print their
name and reason. Suitable discrete devices are preferred over integrated devices;
first enumeration order breaks ties. A combined queue family is preferred, but
separate families create two queues and use concurrent swapchain sharing.

## Swapchain and frame lifecycle

Selection prefers BGRA sRGB with nonlinear sRGB color space, then RGBA sRGB,
then BGRA UNORM, then an available color-attachment format. A legacy single
undefined format is resolved to a supported common color format. FIFO is preferred;
the pure policy has a first-supported fallback. Image count is minimum plus one,
clamped to a nonzero maximum, with overflow checking. Opaque composite alpha is
preferred with supported alternatives. The surface's current transform is used.

`SDL_GetWindowSizeInPixels` supplies drawable dimensions, independent of logical
window size. A fixed `currentExtent` takes precedence for an active window;
variable extents clamp pixel size to capability limits. Closed/minimized windows
and zero pixel extents skip work before recreation or acquisition.

One frame is in flight. The renderer owns one resettable command pool, one primary
command buffer, one acquire semaphore, one acquisition fence, and one submission
fence. Each swapchain image owns a view, framebuffer, render-finished semaphore,
and maintenance1 presentation fence. A swapchain generation also owns one shared
depth image/view/memory allocation, safe because submissions are serialized by the
single frame fence. The scene owns two static mesh buffers, two shader modules, a
push-constant layout and one compatible graphics pipeline.

1. Poll events; skip a closed, minimized or zero-size drawable.
2. Recreate if pixel size changed or acquisition/presentation marked it dirty.
3. Wait for the previous submission before reusing commands/acquire semaphore.
4. Acquire an image with a bounded one-second timeout; wait its acquisition fence.
5. Record a color/depth render pass and indexed object draws. The camera derives
   aspect from actual swapchain extent; viewport and scissor are dynamic.
   The color attachment discards previous
   content from `UNDEFINED`, transitions to `COLOR_ATTACHMENT_OPTIMAL`, clears and
   stores, then transitions to `PRESENT_SRC_KHR`. An external color-output dependency
   and color-output acquisition wait synchronize the attachment transition/write.
   Depth clears to 1 and uses depth-stencil attachment layout. Early/late fragment
   dependencies include previous depth writes and new depth reads/writes.
6. Reset the submission fence **after** successful acquisition/recording; submit,
   waiting for acquisition and signaling this image's render-finished semaphore.
   Diagnostic capture optionally copies color into a mapped buffer before present;
   transfer/host barriers, completion fences and noncoherent invalidation protect reads.
7. Present through the presentation queue, waiting that semaphore and attaching
   this image's present fence. Wait before reusing a pending present fence.

Per-image render-finished semaphores avoid confusing graphics submission
completion with completion of presentation's semaphore waits. See
[Khronos synchronization guidance](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html).
Steady rendering does not allocate application containers or wait device-idle;
diagnostic `info()` copies strings and is outside the normal steady loop.

## Resize, failures, and ownership

Resize or suboptimal/out-of-date results mark the swapchain dirty. Recreation
waits device work and all pending presentation fences, creates a replacement using
`oldSwapchain`, and retires the old generation. If creation returns out-of-date,
the old swapchain is discarded because passing it may retire it even on failure;
the next active frame retries from no swapchain. Minimized windows do not recreate
every frame. Depth attachments/framebuffers are replaced at the new extent;
shaders, mesh buffers and pipeline layout stay alive. The pipeline is reused for
compatible color/depth formats and rebuilt only when those formats change.
Acquisition timeout/not-ready skips without resetting the submission
fence, preventing a subsequent wait on an unsubmitted frame.

Other Vulkan errors throw with operation/result and useful device-loss, surface-loss,
or allocation-failure text. The renderer then rejects more frames until shutdown.
Surface-loss/device-loss recovery is not implemented. Cleanup attempts to drain
work; a lost device avoids waits that cannot complete and still destroys owned
resources. Missing-loader failures can occur before entry to main because the
graphics executable links the Vulkan import library; install the runtime or use
the separately linked headless executable.

Resource owners cannot be copied or moved. Constructors catch failures and release
every acquired child, including partly populated swapchain resource arrays. Failed
`initialize` never publishes an initialized renderer. Repeated `shutdown` is safe;
a successful shutdown permits reinitialization. Diagnostics remain available after
shutdown, including destruction messages.

Destruction drains device work/acquisition and presentation, frees the command
pool/buffers, framebuffers/render pass, views, presentation semaphores/fences and
swapchain, then renderer fences/semaphore and device, then surface/debug messenger/
instance, and finally the native-window lease. Depth resources and optional readback
are freed with framebuffers; scene pipelines/layout/shaders and mapped mesh buffers
are freed before the logical device. The debug callback's diagnostic
storage outlives the entire context. There are no simulation-state ownership changes.

## Tests and observed validation

Ordinary CTest includes synthetic policy tests for counts, formats/color space,
presentation/alpha fallback, extent boundaries, queue selection, missing support,
and device preference. Lifecycle tests cover uninitialized state, invalid frame
calls, repeated precondition failure/shutdown, invalid window input, and no SDL
video initialization. They never create a Vulkan instance or query a GPU; policy
tests link headers only, lifecycle tests link the loader but perform no live calls.
Three additional CLI cases reject invalid graphics requests. Three separately linked
headless cases cover movement, warehouse, and batch analysis. All 84 prior tests
remain registered unchanged. Commit #12 adds camera, transform, scene ownership,
geometry, depth/memory/pipeline policy, shader workflow and capture CLI coverage,
giving 112 ordinary tests. No ordinary test creates a window or Vulkan instance.

The graphics integration test is opt-in, never a default CI/display requirement:

```powershell
build/windows-debug/apps/sandbox/simforge_sandbox.exe --graphics-smoke 180
# Or explicitly register a graphics-labelled CTest in this build:
cmake --preset windows-debug -DSIMFORGE_GRAPHICS_TESTS=ON
ctest --preset windows-debug -L graphics --output-on-failure
```

Smoke mode requires validation and synchronization validation, presents the requested
frames, resizes three times, minimizes for 250 ms, checks that skipped frames increase
without recreation, restores, sends a close event, shuts down, reinitializes, and
tests releasing the Window wrapper before renderer cleanup. Validation errors fail
the smoke. It also verifies four cube colors and meaningful geometry in GPU readback,
a near-first depth probe, identical pixels with reversed draw order, an empty scene,
pipeline reuse and expected geometry/allocation counts. Capture requires RGBA/BGRA8
nonlinear sRGB and surface transfer-source support; normal rendering does not require
capture support. Its watchdog fails after 30 seconds without presentation progress.
The opt-in CTest has a 40-second timeout for its 180-frame run.

Commit #11 historical baseline, observed on 2026-10-08: SDK 1.4.363.0 and queried
runtime loader 1.4.363; NVIDIA GeForce
RTX 5080 Laptop GPU, device API 1.4.351; graphics/present family 0; 1280x720,
three images, B8G8R8A8_SRGB, FIFO. The Intel Graphics driver was rejected because it
lacks maintenance1 present fences. Debug and Release each passed 96/96 ordinary
tests and a live 180-frame graphics smoke with four swapchain generations and zero
validation warnings/errors through shutdown and reinitialization. An extended Debug
1800-frame run also passed. Both normal Debug and Release sandboxes' visible
blue-gray output was observed in DPI-aware 1280x720 client-area desktop captures
(RGB 44,60,77), and native close requests exited with code 0. Normal Debug reported
zero validation messages; normal Release correctly had validation disabled.
The renderer-disabled Release build passed 90/90 tests without Vulkan package
discovery. `dumpbin /dependents` confirmed that the isolated headless executable
imports neither SDL3 nor Vulkan. This was automated desktop interaction,
not a human drag/minimize session. Allocation exhaustion/device loss were audited,
not fault-injected; other hardware, split queues, and OSes were not live-tested.

## Limits and next scene design

Current 3D results, screenshot and baseline are recorded in [first-3d-scene.md](first-3d-scene.md).
Debug/Release pass 112/112 tests; the renderer-disabled build passes 93/93.
Both 3D smoke paths run with validation enabled.

Drivers without maintenance1 fences are unsupported, including the tested Intel
driver. This is not an integrated-GPU exclusion. Runtime installation and a real
display are necessary for graphical tests. Resize waits idle deliberately; there
is one frame in flight, optional diagnostic readback and no GPU profiler. FIFO/vsync
CPU frame times do not measure raw GPU throughput. The scene is static and does
not run a simulation; fixed-step scheduling does not guarantee bitwise simulation
determinism across hardware.

Commit #13 implements the static warehouse adapter and reusable industrial geometry.
See [warehouse visualization](warehouse-visualization.md) for current validation,
the desktop screenshot and measured drawing costs. The adapter depends on scenario
definitions and CPU scene values; the Vulkan renderer has no warehouse-domain
dependency. Asset buffers and pipelines remain renderer-owned. Batching, selection
and live interpolation are deferred; simulation ticks and snapshots are unchanged.
