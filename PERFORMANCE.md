# 0.5.5.1 performance maintenance

This release addresses costs introduced after 0.5.2 that could remain even with
new visual effects disabled. It does not include the separate ragdoll or culling
visualizer work, lower the player's saved quality settings, or promise a fixed
frame rate on every headset.

## Fixed unnecessary work

- The HUD shader no longer writes the reflection-coverage mask when no active
  screen-space reflection needs it. A separate no-coverage shader contains no
  storage atomics. The coverage path checks the Vulkan feature actually enabled
  on the logical device.
- World/skin/immediate rendering has a specialized effects-off path. Optional
  lighting/reflection calculations and their extra shader inputs can be removed
  when there is no consumer.
- Driving with reflections disabled no longer enables the occupied-car bounds
  and distance calculation for every rendered object.
- Reflection work uses effective strengths, not only the ON/OFF switches.
  Zero-strength SSR does not need the previous-scene copy or reprojection work;
  disabling it also invalidates its history before a later re-enable.
- Reflection coverage buffers are released with the renderer.
- Per-weapon laser overrides are cached instead of opening and parsing the
  complete settings file during weapon rendering. Editing a setting updates the
  cache; reopening the VR menu reloads externally changed overrides.

## Mip generation and streaming

- Turning generated mips OFF releases queued/completed jobs and discards the
  in-flight result. It does not wait for the current worker job on the menu thread.
- Evicted textures no longer leave their canceled jobs retaining memory in queues.
- Shutdown cancels the backlog instead of filtering it all before exiting.
- Failed filtering or staging-memory mapping cannot upload uninitialized data.
- Finished mip chains upload during an active frame, with a 2 MiB budget per
  frame. One larger chain is allowed to make progress; deferred chains are kept.
- The worker timing counter is atomic. The texture filter, compression and
  alpha-coverage calculations themselves have not been changed by this hotfix.

Both mip ON and OFF show **RESTART**. OFF stops additional generation and
automatic generated-mip sampling immediately, but existing GPU textures retain
their allocations and any BC3 promotion until a full restart. To compare the
memory and frame-time baseline, restart after changing this option.

With mips ON, a long streaming burst can still accumulate pending work: this
hotfix does not silently drop texture chains to enforce an arbitrary queue cap.

Water-normal generation and reduced-reflection target allocation still occur
at renderer initialization. These remaining startup/memory costs are separate
from the per-frame OFF-path fixes; the hotfix does not claim zero allocation
for every disabled feature.

## Comparing with 0.5.2

Use the same headset, game data/model packs, save, route, weather, traffic,
render scale, refresh rate, CPU/GPU mode and thermal conditions. Restart the
application for each run and warm up the same route before recording results.

Check these settings separately from the new effects:

- MSAA: the later renderer honors saved `MsaaSamples=2/4`; 0.5.2 forced 1x.
  Use 1x on both sides for a baseline comparison, and restart after changing it.
- Wrist radar, status, clock and ammo panels: later defaults can enable them.
  Match each panel setting, not just the main graphics toggles.
- `ShowProfiler`: its state is now saved across launches. Keep profiling either
  enabled on both compared builds or disabled on both.
- Driving mode and control highlights: later missing-key defaults enable
  immersive driving and highlights. Match these separately for driving tests.
- Generated mipmaps, PS2 alpha split, FXAA, distance fog, fountains, modern water,
  car reflections and dynamic lights each have independent controls.

Compare sustained frame times and stutters, including driving through new
sectors, not a single FPS reading. Host tests and successful shader/APK builds
check implementation correctness; they do not measure the FPS improvement on
a Quest. Keep that distinction when reporting results.

## Maintainer checks

The patch is based on source-kit commit `2b94d67` (0.5.5); the comparison baseline
is `89936c9` (0.5.2). Isolated host regressions live under `tools/tests`.
Build the staged source kit independently of unfinished work before release.
Android `versionCode` is **506** for 0.5.5.1; the next release must use a greater
code even if its displayed version is 0.5.6.
