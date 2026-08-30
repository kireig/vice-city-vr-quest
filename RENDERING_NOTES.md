# Rendering notes

Things about this port's renderer that cost real time to work out, kept so the
next person does not spend that time again.

## The PS2 alpha rule is not optional

Vice City draws masked geometry -- railings, fences, chain link, foliage,
shop-front glass -- with an alpha test reference of **3 out of 255**, set in
`DefinedState` in `src/rw/RwHelper.cpp`. That is deliberate: almost nothing is
discarded, and the shape is meant to come out of blending rather than
cutting.

Every masked element carries an invisible border in its texture -- a margin of
texels with zero alpha, and RGB left at black. Those texels pass a reference of
3, so they are drawn: they contribute no colour, and **they write depth**.
Anything drawn afterwards that stands behind them fails the depth test. A
railing therefore carves its own outline out of the wall behind it, and through
that outline the player sees whatever is further away -- the sky, the beach,
the sea. It reads as a faint blue fringe along the fence in daylight and
disappears at night, because at night what shows through is dark too.

Rockstar solved this in 2003 and librw carries the solution: `GSALPHATEST`, the
PlayStation 2 two-pass alpha rule. A blended mesh that writes depth is drawn
twice:

| pass | alpha test | depth write | draws |
|---|---|---|---|
| 1 | `>= GSALPHATESTREF` | on | the solid half |
| 2 | `< GSALPHATESTREF` | off | the faint half |

Nothing is discarded -- both halves reach the screen -- but the faint half no
longer claims the depth buffer. reVC asks for this every frame from
`gPS2alphaTest`, and the shipped `reVC.ini` has `PS2AlphaTest = 1`.

`librw`'s `d3d`, `d3d9` and `gl3` backends all implement it
(`drawInst_GSemu` in `src/d3d/d3d9render.cpp` is the clearest reference). **The
Vulkan backend did not.** `GSALPHATEST` and `GSALPHATESTREF` reached
`setRenderState` and were dropped on the floor, so the Quest build had an
artefact the desktop builds never showed. It is implemented now in
`vkpipe.cpp`, with the second half selected by pushing a negative alpha
reference to `rw_world.frag`.

Things that look like the cause and are not, each of them checked:

- **Not the distance fog.** Turning it off changes nothing; the artefact
  predates that feature.
- **Not FXAA or the colour filter.** Both were switched off and it remained.
- **Not the textures.** Every fence and railing texture in `gta3.img` was
  decoded and measured; their transparent regions are black or the fence's own
  colour, never sky blue. There is no sky matte baked into the art.
- **Not the mip chain.** Levels come from the TXD exactly as on desktop.

Raising the global alpha reference does remove it, but it also discards real
detail everywhere else in the game -- thin leaves and wire mesh lose their
softest edge. That is the wrong lever. So is sorting the world back to front:
the alpha entity list is `NUMALPHALIST = 20` entries, sized for vehicles and
translucent peds, and routing world geometry through it means a much larger
list, a per-frame sort of hundreds of entities and a linear duplicate scan on
every insert.

## Vice City ships single-level textures

No world texture in `gta3.img` carries a mip chain: `numLevels` is 1
throughout. Distant fences, signs and window grids are therefore minified
straight off the full-size image, which is what makes them crawl when the head
moves.

The port builds the missing levels itself, at load, on a worker thread
(`vkraster.cpp`). The player's TXDs are never modified. Three details matter:

- **The filter is weighted by alpha.** A plain box filter averages in the
  colour of texels that are not there -- the cut-out half of a fence is black,
  and averaging it into the bars outlines every distant railing in the colour
  the artist left behind the mask.
- **Adreno accepts BC**, so the game's DXT blocks reach the GPU compressed and
  never pass through the uncompressed upload path. Generating their chain means
  decoding each level, filtering it and encoding it again, which is why the
  work is queued to a worker rather than done where the texture is created.
  Doing it inline cost about 13 seconds of stall across a session, delivered in
  bursts whenever the player drove into unseen ground.
- **The worker has to be cancellable, including mid-job.** A raster can be
  destroyed while its chain is being filtered, and the job under the filter
  sits in neither queue for that window -- it walked straight past the scan
  in `cancelGeneratedMips` and came back holding a handle that no longer
  existed. That is the crash players hit right after a save loaded, where
  the streamer drops a district's worth of textures at once. The job in
  flight is tracked in `gMipInFlight` and re-checked after it is built.

## Foliage shimmered because its mips were never sampled

Leaf edges crawled and sparkled with the head perfectly still, and a canopy two
streets away filled with drifting dots. It reads as textbook aliasing on an
alpha mask, so every fix aimed at aliasing was tried, and every one of them
missed.

Things that look like the cause and are not, each of them checked:

- **Not the PS2 alpha rule.** It shimmered before that landed and after, and
  turning `PS2AlphaTest` off changes nothing on leaves. Foliage comes through
  the alpha list, drawn back to front with depth writes off, so it never was
  in the class the two-pass rule carves out -- the fences and the neon were,
  which is why those answered to it and the leaves did not.
- **Not the render scale.** San Andreas on the same headset runs 110 against
  Vice City's 125 with multisampling off, and its palms sit still.
- **Not the vegetation pack.** The HD set carries many more separate leaves,
  which looks like a geometry density problem -- but stock 128x128 foliage
  with `ModelSetVegetation = 0` shimmers exactly the same.
- **Not the LOD bias.** `FOLIAGE SOFTNESS` from 0 to 1.5 levels changed
  nothing on leaves at any setting; it only fattened the neon. That was the
  real clue and it was misread twice: a bias cannot do anything while the
  sampler is not allowed off level zero to begin with.

Three faults sat in the path between the TXD and the sampler, each one hiding
the next:

1. **The loader threw the authored chain away.** `readAsImageAnyFormat` in
   `src/d3d/d3d8.cpp` read level 0 and seeked past the rest, so a model pack
   that did ship a chain still arrived as a single-level raster. It gathers
   every level into one buffer now and hands them to `rasterFromDXT`.
2. **The sampler cache overflowed its own table.** The key packs bias, filter
   and both address modes and reaches 127; the table was `samplers[64]`. Both
   trilinear modes wrapped back onto slot 0, so every world texture was drawn
   with whichever sampler happened to be built first. It is `samplers[512]`.
3. **The textures ask for `LINEAR`, and the sampler obeys.** This is the one
   that mattered.

Vice City was authored when nothing carried a chain, so its materials declare
`Texture::LINEAR` -- bilinear, no mip stage. `vkstate.cpp` honours that with
`maxLod = 0`, and it has to: the port now builds a chain for everything, and
without the clamp the fonts, the radar and the HUD sheets would start
minifying along with the world. But the same clamp pins a leaf mask to its
base level, sampled at full resolution from any distance, and every level
built for it goes unread.

The fix is one substitution in the world pipe, which is the only path the
interface never travels:

```c
uint32 textureFilter = material->texture->getFilter();
if(textureFilter == Texture::LINEAR && raster != nil &&
   rasterNumLevels(raster) > 1 && rasterHasGeneratedMips(raster))
	textureFilter = Texture::LINEARMIPLINEAR;
```

`rasterHasGeneratedMips` is the load-bearing half of that condition: only a
chain this backend built is trusted. The levels baked into the model packs
were filtered without weighting the colour by alpha, so they carry a dark rim
around every shape in the mask, and reading them turns foliage and neon into a
lattice. `tools/modelsets/txdcompress.cpp` has the weighting now -- lift the
restriction once the packs are rebuilt with it.

### What a mask needs from a chain that a wall does not

- **Coverage is preserved per level.** Averaging alpha thins a crown one level
  at a time, and a tree three streets away ends up a handful of leaves on a
  branch, thickening and thinning as the head moves. `preserveAlphaCoverage`
  picks the cut that keeps as much of the level as the artist's own mask kept
  of the base.
- **BC1 cut-outs are re-packed as BC3 first.** One bit of alpha cannot hold
  what a filter produces. Blocks with no cut-out are copied bit for bit and
  simply told they are opaque, so the artist's colour survives; only the
  blocks along the mask are unpacked and fitted again, and a mip was going to
  rewrite those anyway.
- **A block where nothing survived the cut still has to carry a colour.**
  Left black it shows through as a dark square once the level is magnified, so
  the encoder fits over all sixteen texels instead of over the empty set.

### Multisampling is not the lever, and coverage is worse

MSAA earns its place on the edges the rasteriser itself creates -- railings,
poles, wires, the geometry the setting was made for -- and it is offered as
`STEREO MSAA < OFF / 2X / 4X >`. It does nothing for a leaf edge, because that
edge lives inside the texture and every sample of it lands on the same texel.

Alpha to coverage was added on top of it and taken out again. It turns the
mask into per-sample coverage, and at two or four samples that is a dither
pattern, not a soft edge: on a neon tube or a leaf crown it reads as a visible
lattice. That lattice was blamed on the new mip chains until multisampling was
switched off and it went with it.

Nothing spatial resolves detail finer than a pixel, which is why the desktop
answer to this is temporal -- DLAA simply blurs the leaves until they hold
still. The remaining spatial lever is the honest one: bias the mask down the
chain and let it soften, which is what `FOLIAGE SOFTNESS` does now that the
sampler is able to walk the levels at all.

## Car reflections are the previous frame, not a cubemap

RenderWare drew vehicle env-map materials with a second textured pass; this
backend never implemented it, so for most of the port's life the cars had no
reflections at all. What ships now is screen-space: the frame the GPU finished
a moment ago is still resident -- the post pass reads it -- so the reflection
block in `rw_world.frag` reads it a second time. Per env-mapped pixel: reflect
the view ray off the normal, walk a guessed distance along it, project that
point through the PREVIOUS frame's matrices, fetch the colour. The street that
actually stands around the car lands on its body and slides correctly as the
head or the car moves, which is the whole reason it reads as metal -- a
gradient or a static streak texture alone reads as lighter paint. Where the
previous frame has no answer (anything behind the camera, the frame edge) it
cross-fades to the DFF's own env streak art wrapped as a panorama and tinted
by the timecycle. A real cubemap was rejected on cost: six world renders a
frame, and every one of them pays draw calls and streaming for geometry the
reflection may never show. The screen-space path costs two matrix multiplies
and a texture fetch, only on vehicle pixels.

Every piece of that sentence hides a trap that cost real time:

- **`scene.previousViewProj` is not the previous frame's matrices.** It falls
  back to the CURRENT matrices whenever `temporalHistoryValid` is unset -- and
  the only writer of that flag, `prepareMotionUniform`, returns immediately
  when SGSR is off, which is the shipping configuration. Everything consuming
  the field silently reprojected through current-pose matrices into a
  one-frame-old image, and every reflection stuck to the head and snapped back
  once per frame. The reflection path captures its own copy in `endFrame`
  (`gvk.reflectionPrevViewProj`) and trusts nothing else.
- **Play space itself moves between frames.** The probe is expressed in this
  frame's play space; the sampled image was rendered in last frame's, and the
  camera fold between them changes with every camera step. The reprojection is
  the full round trip -- current play space, world, submitted frame's play
  space, its clip -- with the submitted fold also captured in `endFrame`.
- **The lookup is mono, and that is a decision rather than a shortcut.** Each
  eye used to project through its own matrix into its own layer, which
  sounds right -- true binocular depth -- and was wrong in practice.
  Whenever the march distance missed the true depth the two eyes fetched
  genuinely different pictures, and the reflection read as rivalry rather
  than as metal. Both eyes now go through the left eye's matrix and fetch
  the left layer, so the reflected content is identical by construction and
  reads as laid on the body, exactly like the panorama layer under it.
- **The interface is baked into the sampled image.** Help boxes and fonts are
  head-locked, so their ghosts slide across a car body with every head turn.
  `rw_im2d.frag` marks the coarse screen cells it touches (16x16 per eye, a
  16-byte storage buffer written with atomics from every fourth pixel) and the
  reflection refuses those cells, falling back to the panorama. Two details:
  world sprites -- coronas, headlight glows -- travel the same Im2D path but
  carry real camera depth in the vertex w (`fragWorldSprite`), and they must
  NOT mask: their light belongs in a reflection, and masking them carved a
  travelling square around every street lamp. And a brightness threshold that
  spared the translucent grey help-box background was reverted -- the grey
  slab reads clearly on a car body; everything visible masks.
- **The march distance is a guess** -- downward rays are cut short (the road
  is right under the car), upward rays reach further (buildings, sky) --
  because the depth buffer is deliberately not there to refine it: depth
  storeOp is DONT_CARE with SGSR off, and keeping it costs a full two-layer
  depth writeback every frame. Exact depth-refined lookups and compositing
  the interface in the post pass (which would delete the coverage mask
  entirely) are the two known upgrades, both priced but not taken.
- **The cockpit turns "only on vehicle pixels" inside out.** From the
  driver's seat the player car's env-mapped body and glass wrap the entire
  view, so the block silently became a full-screen pass the moment the
  player sat down -- frame rate fell off a cliff and the lookups drew mostly
  smears of the car's own previous-frame pixels. The block fades in between
  1.1m and 1.9m from the eye: dashboard and windscreen skip it outright, the
  bonnet keeps it, every other car is untouched.
- **The cost is the arithmetic, not the lookup.** Measured on the headset at
  2524x2645 per eye, standing in front of one car: switching the reflection
  on moves the frame from 8.7 ms to 11.4, against a 13.9 ms budget at
  72 Hz. Two attempts to cheapen the fetch moved nothing -- sampling a
  quarter-resolution copy of the previous frame instead of the full one,
  and giving the panorama fetch an explicit mip level instead of a
  derivative-chosen one. Nor is `SSR STRENGTH 0`, which skips the
  screen-space fetch outright, meaningfully cheaper than full strength.
  What is left is the per-pixel arithmetic -- `atan`, `normalize`,
  `reflect`, `pow`, the reprojection multiply -- paid on every pixel of
  bodywork on screen. That is also why the cost swings while the player
  stands still: traffic drives through the view and the bodywork area
  changes with it, so any A/B here needs `TRAFFIC > VEHICLES` at OFF to
  mean anything. Shaving instructions is the only lever left on this
  technique; the one that changes the order of the cost is a cubemap,
  which replaces the whole block with a single coherent fetch.
