# Manual GitHub publication plan and audit

This plan incorporates the mandatory publication amendment. Preparation is local; creating a remote and the first push require the repository destination and visibility to be confirmed. No executable, release, workflow or cloud build pipeline is part of this task.

This document records the completed preparation snapshot on 2026-10-15, immediately before the first branch push. The delivery report records the resulting published commit and outcome; this snapshot does not claim that tests ran on GitHub.

## Publication status

- Repository: [achalsood/SimForge](https://github.com/achalsood/SimForge), public, explicitly confirmed by the owner. Created empty without an initial README, license commit or workflow; `origin` points to its HTTPS Git URL. Existing authentication was verified as `achalsood`.
- Latest published commit at this pre-push snapshot: none; the complete `master` branch is ready for its initial normal push. Engineering HEAD at audit start is Commit #33, `f8b308846bab4524ab881dd98710287c07febbf7`; it remains an ancestor of the normal preparation commit.
- Pushes and releases at this snapshot: none. No executable binaries or release assets are included.
- Project license: Apache-2.0, explicitly selected by the owner; copyright 2026 Achal Sood, matching the existing Git author name. Root [LICENSE](../LICENSE) and [NOTICE](../NOTICE) are included. The existing font license is preserved; see [third-party review](../THIRD_PARTY.md).
- GitHub Actions: disabled in the new repository's settings; an authenticated API readback verified `enabled: false`. No workflow paths exist in the tracked tree or any reachable historical commit. No CI badges, hosted test claims, schedules or automated release publishing are added.

## Required sequence before the first push

1. Complete the tracked-file and reachable-history audit, local verification and documentation below. Keep dependency caches, compiler products, experiments, recordings and generated logs outside version control.
2. Confirm GitHub owner/repository and visibility. Confirm the project license and copyright attribution, add the chosen license, and review the resulting changes. Do not infer these decisions from a local username.
3. Finish a normal local preparation commit and verify that Commit #33 remains its ancestor. Preserve all meaningful history; do not squash, amend old commits, force-push or replace a remote branch. No security or oversized-blob issue found in the audit requires a history rewrite.
4. Check authentication for the chosen account. If repository creation is needed, obtain the requested confirmation first. Create an empty repository with no generated README, license, workflows or initial commit. Disable Actions in its repository settings and verify the disabled setting before the first push; do not introduce workflow files to do this. If permissions prevent that setting change, have the owner disable it before publication proceeds.
5. Inspect the chosen remote with `git ls-remote` before adding or pushing it. If it has history, compare and preserve it; resolve any divergence through an ordinary reviewed integration. Never use `--force` or overwrite it. A provider-managed initial commit requires reconciliation, not replacement.
6. Push the complete local branch only after the preceding items are resolved. Keep the existing branch name unless the owner requests a change. Verify the remote SHA and ancestry, then record the actual URL, visibility and latest published commit here. Do not create a release or upload binaries.

These are pending manual publication steps, not commands already executed. Repository creation, authentication changes and visibility choices have not been performed.

## Local verification

The [README's Building and Testing Locally section](../README.md#building-and-testing-locally) contains the actual CMake/CTest commands. Existing presets and test infrastructure are unchanged. Tests run locally, never on GitHub.

Publication rerun on 2026-10-15: all three presets configured and built successfully; Ninja reported no compilation work needed. Debug and Release explicitly enable `SIMFORGE_GRAPHICS_TESTS=ON`; the headless preset disables the renderer. No application source, CMake presets or test configuration changed during preparation. Local raw logs are under `build/publication-*.log` and are excluded from version control.

| Fresh local check | Result | Elapsed |
|---|---|---:|
| Windows Debug full CTest | 795/795 passed, including all 21 graphics cases | 855.40 s |
| Windows Release full CTest | 795/795 passed, including all 21 graphics cases | 273.65 s |
| Renderer-disabled full CTest | 754/754 passed | 127.15 s |
| Checkpoint Vulkan Review and fresh-process reopen | Passed; both processes report validation errors/warnings 0/0, with clean shutdown/reinitialization | 17.01 s |

The additional Review command initially omitted `-Scenario` and `-Live`: the cube smoke itself passed with zero validation messages, then reopening failed because no checkpoint project had been produced. The README command was corrected to match the existing CTest scenario/live invocation. This was a documentation command error; no application or test harness code changed. Failed diagnostics remain in ignored `build/test-artifacts/graphics/failures/1724ff165cd944cc90ac86ce57e637b2/`.

The corrected successful Review is `facede5f7ea3432aacb484837a457058`, retained locally under `build/test-artifacts/graphics/latest/`; its console verifies the actual checkpoint workflow, control/intervention playback, headless parity, exports, Save/Open and fresh-process reconstruction. Raw suite logs are `build/publication-{debug,release,headless}-tests.log`; the final review driver log is `build/publication-vulkan-review-corrected.log`. All are local, ignored artifacts. The README headless scenario command was also executed successfully.

Prior exact engineering baseline evidence is retained in [Commit #33 validation](commit-33-validation.md): Release 795/795, renderer-disabled 754/754, Debug all 795 covered across the full run and documented rechecks, and zero Vulkan errors/warnings in the native checkpoint walkthrough and fresh-process reopen. That record discloses two fixed UI regressions and an unexplained isolated custom-live timeout that passed on recheck. No first-pass all-green Debug result is claimed for that earlier run.

## Tracked artifacts and history

The initial audit inspected all 35 reachable commits, 1,108 distinct reachable blobs and the 562-file tracked tree. The tracked tree contained 28,684,510 bytes (27.36 MiB). The `.git` directory occupied 21,944,429 bytes (20.93 MiB); `git count-objects -vH` reported 5.58 MiB loose objects plus 15.20 MiB packed objects. Distinct reachable blobs total 36,612,827 uncompressed bytes (34.92 MiB). These measure different things; ignored local build/dependency directories are not repository payload.

The largest historical blob is a curated demand-diagnostics example, 570,635 bytes (557.26 KiB). No blob approaches an oversized-artifact threshold. No compiler outputs, CMake build directories, dependency caches, virtual environments, IDE caches, recordings, generated logs or temporary experiment projects were tracked in the inspected tree or history. The filename `docs/build-artifact-management.md` is documentation, not a generated build directory.

All 144 tracked PNGs were audited: 20,832,377 bytes (19.87 MiB). The current checkout now retains twelve meaningful native screenshots; 132 redundant milestone captures are removed from the current tree, with old captures and their engineering context preserved in history. See [gallery and exact recovery command](screenshot-gallery.md). The preserved history deliberately retains those bytes.

The selected screenshots total 2,188,488 bytes (2.09 MiB). The documented archive/recovery command was executed and the recovered PNG's Git blob hash matched the original exactly. The complete local Markdown link check found no missing file targets, and the two README images were visually reviewed.

The prepared source tree contains 436 tracked files and approximately 9.63 MiB, compared with the initial 27.36 MiB. Exact final `.git` size is reported with delivery after the new commit; preserving history means the earlier screenshot blobs remain part of the clone.

Keep source, tests, required configuration, the pinned dependency manifest, synthetic example requests/reports, project compatibility fixtures and concise measurement CSVs that substantiate engineering claims. These curated fixtures and small measurement summaries are not disposable benchmark-generated datasets. New bulk benchmark output belongs in ignored `build/`, `benchmark-data/` or `benchmark-results/`; no raw generated dataset is added by this task.

The expanded `.gitignore` excludes alternate CMake build trees, compiler products, local dependency caches, scratch projects, datasets, virtual environments, recordings and logs. It does not remove tracked content retroactively: the tracked index/tree and all reachable history were audited separately. No Git history rewrite or garbage collection is necessary or performed.

## Security review

The reachable-blob scan checked text for private-key headers, GitHub/AWS/Slack/Google credential signatures, authenticated HTTP URLs and suspicious assigned secret fields. Historical paths were also checked for credential files and key containers. No matches were found in history or the final prepared tree. The scan reported only paths/categories/object IDs, never secret values. All 1,108 initial reachable blobs were inventoried; binary blobs were size/type audited rather than treated as text secrets.

The repository does not include dependency source caches or binary releases. Dependency versions are pinned through the existing vcpkg manifest baseline. This review is a repository exposure audit, not a comprehensive code security assessment or a claim that dependencies have no vulnerabilities. Git commit identity metadata remains part of the preserved history; no author identities were rewritten. `git fsck --full --no-reflogs` found no integrity errors; it reports ordinary dangling blobs/trees from earlier local work, which are not reachable history and are not included in a normal branch push. No assertion is made about unrelated account-wide policies or hosted vulnerability scanning.

## Licensing and documentation

No root license was present initially. The owner selected Apache-2.0 during preparation, and its complete text plus a project NOTICE were added with copyright 2026 Achal Sood. The bundled unmodified Roboto font retains its separate Apache-2.0 text and pinned provenance; its SHA-256 matches. Installed dependency notices were reviewed and summarized in [THIRD_PARTY.md](../THIRD_PARTY.md), including SDL's additional component notices and the Vulkan loader's incomplete SPDX label. Binary redistribution notice assembly remains a future manual release task.

The README now gives a concise product overview, two selected native captures, reproducible Debug/Release/renderer-disabled commands, Vulkan review commands, platform limits, local-only testing policy, license status and manual release policy. Detailed tutorials are preserved in [the usage guide](usage-guide.md). Historical engineering records link to screenshot recovery rather than missing images. No CI badge or claim of GitHub validation is included.

## Future manual v0.1.0 release

Only after separate approval: build Windows Release locally, rerun tests, inspect the executable's actual dependency closure, collect required notices, verify a clean-machine launch, prepare checksums and release notes, and review the package. Creating a tag, GitHub release or uploading executable binaries requires that separate release approval. No automatic publishing is configured.

## Outstanding decisions

No preparation blockers remain. The owner confirmed public `achalsood/SimForge` and Apache-2.0 with the existing Git author attribution. Existing Git authentication works outside the sandbox; its Windows Credential Manager store is inaccessible inside the sandbox, and no new login was started. The approved empty repository has been created, Actions disabled and the empty remote inspected. The normal branch push and verification follow this preparation snapshot. No release has been approved.
