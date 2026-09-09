#pragma once

namespace VehicleDeformationSettings {
enum { DEFAULT_STRENGTH = 100, DEFAULT_RADIUS = 100, DEFAULT_MAX_DENT_CM = 32, DEFAULT_THRESHOLD = 100 };
struct Values {
	int strength = DEFAULT_STRENGTH;
	int radius = DEFAULT_RADIUS;
	int maxDentCm = DEFAULT_MAX_DENT_CM;
	int threshold = DEFAULT_THRESHOLD;
};
inline Values &Get() { static Values values; return values; }
inline int Clamp(int value, int low, int high) { return value < low ? low : (value > high ? high : value); }
}

namespace VehicleDeformation {
inline int GetStrengthPercent() { return VehicleDeformationSettings::Get().strength; }
inline void SetStrengthPercent(int value) { VehicleDeformationSettings::Get().strength = VehicleDeformationSettings::Clamp(value, 25, 400); }
inline int GetRadiusPercent() { return VehicleDeformationSettings::Get().radius; }
inline void SetRadiusPercent(int value) { VehicleDeformationSettings::Get().radius = VehicleDeformationSettings::Clamp(value, 50, 200); }
inline int GetMaxDentCentimeters() { return VehicleDeformationSettings::Get().maxDentCm; }
inline void SetMaxDentCentimeters(int value) { VehicleDeformationSettings::Get().maxDentCm = VehicleDeformationSettings::Clamp(value, 10, 60); }
inline int GetThresholdPercent() { return VehicleDeformationSettings::Get().threshold; }
inline void SetThresholdPercent(int value) { VehicleDeformationSettings::Get().threshold = VehicleDeformationSettings::Clamp(value, 25, 200); }
inline void ResetTuning() { VehicleDeformationSettings::Get() = VehicleDeformationSettings::Values(); }
}
