#pragma once

// Culling visualizer. Freezes the frame and repaints it by cull reason so the
// player can turn their head and see, in place, what the per-eye culling kept
// (blue) and what it rejected and why (red frustum, orange occluder, yellow
// distance/LOD). A debug aid for tuning the occlusion modes.
namespace VrCullViz
{
#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
bool IsActive(void);
// Freeze-and-classify, or release. Called from the menu.
void Toggle(void);
// Draws the frozen, colour-tinted snapshot; replaces the world draw while
// active. Called from RenderScene.
void Render(void);
// One line of counters for the menu.
const char *StatusLine(void);
#else
// Shipping builds cannot activate the snapshot, including through stale
// settings or a direct caller. No out-of-line calls or tool storage remain.
inline bool IsActive(void) { return false; }
inline void Toggle(void) {}
inline void Render(void) {}
inline const char *StatusLine(void) { return ""; }
#endif
}
