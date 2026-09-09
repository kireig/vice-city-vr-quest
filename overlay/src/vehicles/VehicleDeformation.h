#pragma once

#include "VehicleDeformationSettings.h"

class CEntity;
class CVehicle;
class CVector;

namespace VehicleDeformation {
// Native collision manifolds can outlive a participant's temporary collision
// matrix. Plain storage lets the bridge capture it only while the option is ON.
struct CollisionPose {
	float right[3], forward[3], up[3], position[3];
};
extern bool enabled;
inline bool IsVehicleDeformationEnabled() { return enabled; }
void SetVehicleDeformationEnabled(bool value);
void RecordCollision(CVehicle *vehicle, const CVector &worldPoint,
                     const CVector &receiverInwardNormal, float impulse,
                     const CEntity *other, const CollisionPose *collisionPose = nullptr);
void Process(CVehicle *vehicle);
// Advance queued impacts once per game frame, independently of visibility.
void Update();
// Called before the base entity destroys/detaches its RenderWare clump.
void Release(CEntity *entity);
// A flying component keeps its native geometry, without private subdivisions.
void PrepareDetachedAtomic(RpAtomic *atomic);
}
