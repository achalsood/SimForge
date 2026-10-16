# Third-party components

SimForge's owner selected Apache-2.0, with copyright 2026 Achal Sood; see [LICENSE](LICENSE) and [NOTICE](NOTICE). This review records the separate notices present in the bundled font and locally installed dependencies at the manifest baseline in `vcpkg.json`.

| Component | Locally installed version | License evidence |
|---|---|---|
| Roboto Regular, bundled unmodified | Pinned source commit in [font provenance](assets/fonts/README.md) | Apache-2.0; complete text in [Roboto-LICENSE.txt](assets/fonts/Roboto-LICENSE.txt) |
| SDL3 | 3.4.18 | Installed SPDX expression: `Apache-2.0 AND BSD-3-Clause AND MIT AND Zlib`; the main SDL license is zlib, with additional component notices in its `copyright` file |
| Dear ImGui | 1.92.9 | MIT |
| GLM | 1.0.3 | MIT |
| nlohmann/json | 3.12.0#2 | MIT |
| Vulkan headers | 1.4.363.0 | Installed SPDX expression: `(Apache-2.0 OR MIT)` |
| Vulkan loader | 1.4.357.0 | Installed `copyright` states Apache-2.0 for most files with more permissive exceptions; SPDX metadata says `LicenseRef-vcpkg-null`, so it is not sufficient by itself |

The review read each package's installed `copyright` and `vcpkg.spdx.json` under `build/windows-release/vcpkg_installed/x64-windows/share/<package>/`. These files are local build products; dependency source and binaries are not vendored into this repository. SDK tools and the GPU driver are external prerequisites. This is a notice inventory, not a vulnerability assessment or a replacement for the upstream license texts.

The bundled font's SHA-256 was verified against its documented source hash: `56a45233d29f11b4dfb86d248e921939d115778f87325e7ae8cc108383d6664d`. Warehouse meshes and GLSL shaders are repository source, rather than an imported model or texture pack. Example demand fixtures and screenshots describe synthetic simulation workloads.

The local Windows packager copies the complete installed copyright texts for SDL3 (including component notices), Dear ImGui, GLM, nlohmann/json and Vulkan headers into `licenses/` and combines them into `THIRD_PARTY_NOTICES.txt`. ImGui's embedded stb rectangle-packing, text-editing and TrueType components retain Sean Barrett's complete MIT license alternative, extracted from the installed headers into `licenses/imgui-stb.txt` and the combined notices. nlohmann/json's header attributions also identify Björn Hoehrmann, Florian Loitsch, Evan Nemerson and the Abseil Authors alongside Niels Lohmann; all installed header SPDX declarations are MIT. Those exact attributions and complete MIT terms are preserved in `licenses/nlohmann-components.txt` and the combined notices. Roboto's full license and provenance remain in `assets/fonts/`. The renderer embeds shaders and the font; geometry and examples are project-authored. Benchmark/test dependencies are not distributed. The actual package inventory records the shipped binaries and imports. The Vulkan loader/driver, SDK and Microsoft runtime files are not bundled; the latter runtime and driver are documented prerequisites. See [manual packaging](docs/releases/windows-release-process.md) and [release validation](docs/releases/v0.1.0-validation.md). Binary publication requires separate approval.
