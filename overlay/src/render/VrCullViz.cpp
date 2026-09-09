#include "VrCullViz.h"

#if defined(MIAMIVR_DEV_TOOLS) && MIAMIVR_DEV_TOOLS
#include "common.h"
#include "Camera.h"
#include "Entity.h"
#include "Lists.h"
#include "Renderer.h"
#include "Timer.h"
#include "World.h"
#include "vulkan/rwvk.h"

// The tool snapshots the frame once, on freeze: every drawn-list entity is
// re-tested against the exact eye frusta (blue kept / red frustum / orange
// occluder), and every other entity in the surrounding sectors is a
// distance/LOD casualty (yellow). Simulation is code-paused, so the entities
// hold still and the snapshot stays valid however the head turns. Each frame
// after that just redraws the stored set in its colours; the librw backend
// carries the colour index per draw.

namespace VrCullViz
{

enum {
	COL_VISIBLE = 1,
	COL_FRUSTUM = 2,
	COL_OCCLUDED = 3,
	COL_DISTANCE = 4,
};

static bool active;
static CEntity *ents[NUMVISIBLEENTITIES];
static uint8 colour[NUMVISIBLEENTITIES];
static int32 count;
static int32 counts[5];
static char status[96];

bool
IsActive(void)
{
	return active;
}

static void
Store(CEntity *ent, uint8 col)
{
	if(count >= NUMVISIBLEENTITIES)
		return;
	ents[count] = ent;
	colour[count] = col;
	count++;
	counts[col]++;
}

static void
Snapshot(void)
{
	count = 0;
	for(int i = 0; i < 5; i++)
		counts[i] = 0;

	// A private scan code marks everything the snapshot has already placed,
	// so the sector sweep below does not re-list the drawn entities and
	// nothing is counted twice.
	CWorld::AdvanceCurrentScanCode();
	const uint16 sc = CWorld::GetCurrentScanCode();

	const int32 drawn = CRenderer::GetNoOfVisibleEntities();
	for(int32 i = 0; i < drawn; i++){
		CEntity *e = CRenderer::GetVisibleEntity(i);
		if(e == nil || e->m_rwObject == nil)
			continue;
		e->m_scanCode = sc;
		const int32 reason = CRenderer::VrClassifyPerView(e);
		Store(e, reason == 0 ? (uint8)COL_VISIBLE :
			reason == 1 ? (uint8)COL_FRUSTUM : (uint8)COL_OCCLUDED);
	}

	// Everything else in range never reached the drawn list -- distance,
	// draw-distance, LOD swap, zone. The same square the scan walks.
	const CVector cam = CRenderer::GetVrViewCameraPosition();
	const float radius = LOD_DISTANCE;
	int x1 = CWorld::GetSectorIndexX(cam.x - radius);
	int x2 = CWorld::GetSectorIndexX(cam.x + radius);
	int y1 = CWorld::GetSectorIndexY(cam.y - radius);
	int y2 = CWorld::GetSectorIndexY(cam.y + radius);
	if(x1 < 0) x1 = 0;
	if(x2 >= NUMSECTORS_X) x2 = NUMSECTORS_X - 1;
	if(y1 < 0) y1 = 0;
	if(y2 >= NUMSECTORS_Y) y2 = NUMSECTORS_Y - 1;
	for(int x = x1; x <= x2; x++)
		for(int y = y1; y <= y2; y++){
			CSector *sector = CWorld::GetSector(x, y);
			for(int l = 0; l < NUMSECTORENTITYLISTS; l++)
				for(CPtrNode *node = sector->m_lists[l].first;
				    node != nil; node = node->next){
					CEntity *e = (CEntity *)node->item;
					if(e == nil || e->m_rwObject == nil ||
					   e->m_scanCode == sc)
						continue;
					e->m_scanCode = sc;
					Store(e, COL_DISTANCE);
				}
		}

	snprintf(status, sizeof(status),
		"CULL VIZ  VIS %d  FRUSTUM %d  OCCLUDED %d  DIST %d",
		counts[COL_VISIBLE], counts[COL_FRUSTUM],
		counts[COL_OCCLUDED], counts[COL_DISTANCE]);
}

void
Toggle(void)
{
	if(active){
		active = false;
		rw::vulkan::setDebugVizActive(false);
		CTimer::SetCodePause(false);
	}else{
		Snapshot();
		active = true;
		rw::vulkan::setDebugVizActive(true);
		// Freeze the world so the classification stays true while the
		// head looks around it.
		CTimer::SetCodePause(true);
	}
}

void
Render(void)
{
	if(!active)
		return;
	for(int32 i = 0; i < count; i++){
		CEntity *e = ents[i];
		if(e == nil || e->m_rwObject == nil)
			continue;
		rw::vulkan::setDebugTint(colour[i]);
		e->Render();
	}
	rw::vulkan::setDebugTint(0);
}

const char *
StatusLine(void)
{
	return status;
}

}
#endif
