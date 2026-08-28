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
(`vkraster.cpp`). The player's TXDs are never modified. Two details matter:

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
