# SimForge v0.1.0 for Windows

SimForge lets you run a warehouse simulation, watch its activity in 3D, and compare operational changes. This guide describes the portable Windows x64 package. Publication is pending explicit owner approval; see the validation record for current readiness.

## Requirements

- Windows x64 with a desktop display. Local validation uses Windows 11 Home 26H2, build 26300.9711. Other Windows versions, Windows on ARM and other operating systems have not been validated for this release.
- A Vulkan 1.1-capable graphics driver with `VK_KHR_swapchain` and feature-enabled `VK_KHR_swapchain_maintenance1` or `VK_EXT_swapchain_maintenance1` presentation fences. The matching instance surface-maintenance extension and `VK_KHR_get_surface_capabilities2` are required. Graphics/presentation queues, color/depth attachments, surface formats and presentation modes must be available for the actual window surface.
- Local graphics evidence is on an NVIDIA GeForce RTX 5080 Laptop GPU. The installed Intel driver was rejected for missing presentation-fence support; Vulkan 1.1 alone is insufficient. This is not a promise of compatibility with other drivers or GPUs.
- Install the **x64 Microsoft Visual C++ v14 Redistributable**, version **14.51.36231 or later**, from [Microsoft's official download page](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170). Microsoft requires a runtime at least as recent as the build tools. The [official x64 installer](https://aka.ms/vc14/vc_redist.x64.exe) installs the runtime independently of Visual Studio.

You do not need Visual Studio, CMake, Ninja, vcpkg, a compiler, shader tools or the Vulkan SDK to run the ZIP. SDL3 is included. Vulkan comes from your graphics driver/runtime installation. Update the driver from your GPU manufacturer if needed. No driver, SDK or copied Windows/MSVC system DLLs are bundled.

## Download, extract and launch

After publication, open **Releases** on the [SimForge repository](https://github.com/achalsood/SimForge), choose **SimForge v0.1.0 — First Windows Release**, and download `SimForge-v0.1.0-windows-x64.zip` and `SimForge-v0.1.0-SHA256.txt`.

Optionally verify the download in PowerShell with `Get-FileHash .\SimForge-v0.1.0-windows-x64.zip -Algorithm SHA256` and compare its hash with the checksum file. Right-click the ZIP, choose **Extract All**, and open the extracted `SimForge-v0.1.0-windows-x64` folder. Double-click **simforge_sandbox.exe**. Keep `SDL3.dll`, `examples` and the other supplied files together. Launch from the extracted folder, not from inside the ZIP. The console window shows diagnostics; closing the application window exits normally.

A shortcut may point directly to `simforge_sandbox.exe`; its Start in folder does not need to match the application folder. The initial example is resolved beside the executable. Explicit command-line input/output paths remain relative to your chosen working directory.

## First warehouse

The opening panel already contains the full path to `examples/warehouse-baseline.json`. Click **Open warehouse workspace**. Select **Simulation** in the header to see the warehouse and playback controls. Use **Play/Pause** to run or pause, **Step** for a single tick while paused, and **Reset** to return to the initial state. Adjust the speed to see a full simulated shift more quickly. Completed shipments, waiting work and resource utilization appear in the dashboard. These are model outputs, not predictions guaranteed for a real warehouse.

Over the viewport: drag with the left mouse button to orbit, drag with the middle button to pan, and scroll to zoom. Click an object to inspect it. **F** frames the warehouse, **R** restores the overview, **Space** pauses/resumes, **Right arrow** steps while paused, and **Escape** clears selection. Keyboard shortcuts apply when the viewport has focus; editing a text field consumes typing.

To open another scenario from the opening panel, replace the JSON path with the path to an included scenario. Restarting the application returns to the opening panel; the Scenario Editor also supports loading a scenario.

| Included file under examples/ | Purpose |
|---|---|
| `warehouse-baseline.json` | Regular arrivals, queues, processing and completed shipments |
| `warehouse-constrained.json` | One forklift makes the resource bottleneck visible |
| `stochastic-coverage/homogeneous.json` | Poisson arrivals with explicit seed 42 and variable processing |
| `checkpoint-interventions/forklift.json` | An experiment request adding a third forklift after two hours; use the checkpoint study tools or the headless command below, rather than opening it as a scenario |

The checkpoint request embeds its source scenario and needs no external data. See `docs/checkpoint-intervention-studies.md` for the study workflow. From the package directory in PowerShell:

```powershell
.\simforge_headless.exe --headless --experiment .\examples\checkpoint-interventions\forklift.json
```

`simforge_headless.exe` runs simulation and experiment commands without loading SDL or Vulkan. It provides command-line analysis rather than the interactive window.

## Projects and your files

Click **New Project** in the opening panel to start from the baseline, or open a warehouse and use **File > New Project**. Use **File > Save Project As...**, choose a writable folder such as Documents, and enter a new `.simforge` filename. Wait for **Project saved.** Use **Save Project** to update that project. **Open Project...** restores it later, including persisted settings and retained studies. Unsaved changes prompt before replacement/close. A running experiment itself is not saved.

Save As and report exports require a new filename; they do not silently replace existing files. Project saves use transactional replacement, with temporary/backup files beside the selected project. Errors appear in the UI. Keep that folder writable and leave sufficient space. Automatic session recovery uses SDL's per-user preferences folder: normally `%APPDATA%\SimForge\SimForge\session.recovery`. Recovery can be restored or discarded through the UI. If recovery storage fails, the UI reports the error; choose an explicit project save location.

The application does not write settings or logs into its installation folder: ImGui INI/log files are disabled, diagnostics go to the console, and screenshots are only written when an explicit diagnostic capture path is requested. Reports and studies are exported to locations you choose; retained studies are stored in the project. A protected installation folder should remain read-only. There is no implicit portable-data directory beside the executable.

## Troubleshooting

- **Missing SDL3.dll:** extract the complete ZIP and keep the executable with its included DLL. Avoid running it from inside the archive.
- **Missing MSVCP140, MSVCP140_ATOMIC_WAIT, VCRUNTIME140 or VCRUNTIME140_1:** install/repair Microsoft's x64 v14 Redistributable linked above. Do not download individual DLLs from third-party sites. These imports load before application code, so Windows may show this error itself.
- **Cannot load the Windows Vulkan runtime:** update/install the GPU manufacturer's Vulkan-capable driver. You do not need the SDK. The application checks the system Vulkan loader before graphics initialization.
- **No suitable Vulkan GPU / surface maintenance1 unavailable:** the driver or selected GPU lacks required presentation features. Updating a driver may help; unsupported hardware cannot be made compatible by installing shader tools. On a hybrid laptop, Windows Graphics settings can select the supported high-performance GPU for this executable.
- **Window closes or initialization fails:** launch from PowerShell to retain console diagnostics. Normal no-argument startup also shows a dialog for application initialization errors. Save the error text and your GPU/driver details when reporting an issue.
- **Cannot save:** select a writable folder, choose a new filename for Save As/exports, and check free disk space. Application resources are not a save location.
- **Windows blocks an unfamiliar download:** this initial portable executable is unsigned. Review the release source and checksum and follow your organization's download policy.

## Limits

Only the environment recorded in the validation report is tested. A separate clean Windows machine with a compatible Vulkan GPU is not available in this workspace; extracted-package tests with a restricted PATH are useful isolation checks but do not establish clean-machine graphics compatibility. Vulkan validation layers are development test tools and are not a user prerequisite.

Device/surface-loss recovery is not implemented; save work and restart after such failures. Presentation uses FIFO/vsync, so reported frame times include synchronization and UI work and are not GPU throughput benchmarks. Cross-platform bitwise simulation identity is not promised. Seeded replay depends on the documented generator/platform assumptions. Checkpoint studies allow at most 66 positive-intensity Poisson seeds under the conservative execution budget; other replication plans are bounded to 100 distinct seeds. Project/evidence storage limits remain enforced. See the included stochastic and checkpoint guides for statistical assumptions and bounds.
