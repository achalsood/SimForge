# Screenshot gallery

These twelve unchanged captures illustrate the native application. They use synthetic warehouse workloads. The current checkout keeps this small selection; complete milestone captures remain in the engineering history.

| Workflow | Capture |
|---|---|
| Live warehouse and operational dashboard | [Simulation](artifacts/visual-design-simulation-1280.png) |
| Scenario editing | [Editor](artifacts/visual-design-editor-1280.png) |
| Checkpoint intervention and exact timeline | [Setup](artifacts/checkpoint-setup.png) |
| Signed paired effects | [Measured results](artifacts/checkpoint-paired-effects.png) |
| Actual intervention branch playback | [Playback](artifacts/checkpoint-intervention-playback.png) |
| Demand diagnostics | [Observed and expected counts](artifacts/diagnostics-counts.png) |
| Predictive uncertainty | [Distinct predictive bounds](artifacts/diagnostics-prediction.png) |
| Observation coverage | [Coverage windows](artifacts/stochastic-coverage.png) |
| Demand calibration | [Holdout results](artifacts/demand-holdout.png) |
| Persistent evidence | [Study History](artifacts/study-history.png) |
| Paired capacity experiments | [Statistical results](artifacts/statistical-results.png) |
| One-factor resource sweep | [Sensitivity curve](artifacts/sensitivity-curve.png) |

## Historical captures

Publication preparation removed 132 additional PNGs from the current checkout. No history was rewritten: all 144 original captures and their original document context are available at Commit #33, `f8b308846bab4524ab881dd98710287c07febbf7`. Older engineering records cite archived filenames; they describe tests performed at that milestone, not new publication runs.

To recover an archived capture without adding it to the working tree or index, use PowerShell from the repository root. Replace the final filename with the capture cited by the document:

```powershell
git archive --format=zip --output=build/historical-capture.zip f8b308846bab4524ab881dd98710287c07febbf7 docs/artifacts/checkpoint-progress.png
Expand-Archive -LiteralPath build/historical-capture.zip -DestinationPath build/historical-capture
```

The build directory must exist, and the output paths must be unused. Exporting through an archive avoids binary corruption from Windows PowerShell text redirection. Removing current screenshots does not shrink the preserved clone history; it makes the current checkout smaller without discarding engineering evidence.
