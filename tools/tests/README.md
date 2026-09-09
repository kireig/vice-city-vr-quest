# Host regression tests

These tests compile production helpers or extract production functions into
small native adapters. They check code behavior; they do not launch the game,
replace game data or establish headset appearance/FPS.

Windows tests require Visual Studio 2022 or Build Tools with the Desktop
development with C++ workload. Run from a disposable source-kit copy because
some extraction runners write temporary `.inc` files beside the test source.
Those files are ignored and must not be included in a release archive.

The header-only ragdoll suite works directly against the kit:

```powershell
.\tools\tests\test-ragdoll.ps1 -KitRoot .
```

Tests that extract patched engine functions require a freshly assembled reVC
tree via `-StageRoot`, `-KitRoot` or `-EngineRoot`, as declared by that runner.
Some historical comparison fixtures additionally require a local
`baseline-qbuild` subdirectory containing the comparison revision; that tree is
not distributed. `test-heli-bullets.ps1 -OriginalRoot` needs a clean git checkout
of the documented upstream commit. Read each runner's parameter block before
invoking it.

Actual-mesh diagnostics require the player's local, legally owned game data:

```powershell
.\tools\tests\test-vehicle-deformation-body.ps1 `
  -StageRoot C:\src\vice-city-vr-build `
  -GameDataRoot 'C:\Games\Grand Theft Auto Vice City'
```

The vehicle readers access `models/gta3.img`, `models/gta3.dir` and
`models/coll/vehicles.col`; optional Modern tests also need the locally generated
overlay. No asset is embedded in these tests or exported for redistribution.
`-UseBaseline`, where available, intentionally runs the old implementation and
can fail the fixed-behavior assertion.

Useful groups include ragdoll anatomy/contact/support, tracked aim and melee,
vehicle entry/seat transitions, wanted behavior, menu layout and vehicle
deformation geometry/lifetime. Benchmarks report host time only. Exact pass
counts are local release evidence rather than a substitute for player testing.
