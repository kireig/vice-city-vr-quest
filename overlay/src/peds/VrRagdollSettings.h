#pragma once

// Shared live settings; inline-local storage is shared by all engine users.
// Integer percentages match the persistent Quest menu representation.
namespace VrRagdollSettings
{
enum { DEFAULT_BRAKE = 200, DEFAULT_GRIP = 150, DEFAULT_SHOT = 100, DEFAULT_WEIGHT = 200, MAX_BRAKE = 2000 };
struct Values {
	int brakePercent, gripPercent, shotPercent, weightPercent;
	Values() : brakePercent(DEFAULT_BRAKE), gripPercent(DEFAULT_GRIP), shotPercent(DEFAULT_SHOT), weightPercent(DEFAULT_WEIGHT) {}
};
inline Values &Get() { static Values settings; return settings; }
inline int Clamp(int value, int low, int high) { return value < low ? low : (value > high ? high : value); }
inline void SetBrake(int value) { Get().brakePercent = Clamp(value,0,MAX_BRAKE); }
inline void SetGrip(int value) { Get().gripPercent = Clamp(value,0,300); }
inline void SetShot(int value) { Get().shotPercent = Clamp(value,25,200); }
inline void SetWeight(int value) { Get().weightPercent = Clamp(value,50,300); }
inline float WeightScale() { return Get().weightPercent*0.01f; }
}
