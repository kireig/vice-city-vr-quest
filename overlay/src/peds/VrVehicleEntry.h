#pragma once

namespace VrVehicleEntry {
// Player-only input state, kept outside CPed's serialized/save-game layout.
// Continuing the approach gesture is not a new request to cancel entry.
struct CancelInput {
	const void *player = nullptr;
	const void *vehicle = nullptr;
	bool neutralSeen = false;

	bool Update(const void *nextPlayer, const void *nextVehicle,
	            bool entering, bool controlsEnabled, int walkX, int walkY)
	{
		if (!entering || !controlsEnabled || !nextPlayer || !nextVehicle) {
			player = vehicle = nullptr;
			neutralSeen = false;
			return false;
		}
		if (player != nextPlayer || vehicle != nextVehicle) {
			player = nextPlayer;
			vehicle = nextVehicle;
			neutralSeen = false;
		}
		// Quest's input mapper already has a 0.30 radial deadzone. These
		// thresholds are inside that deadzone; use magnitude so head-relative
		// yaw/axis crossings cannot turn held movement into a new gesture.
		const int magnitudeSqr = walkX * walkX + walkY * walkY;
		if (magnitudeSqr <= 16 * 16)
			neutralSeen = true;
		return neutralSeen && magnitudeSqr >= 32 * 32;
	}
};
}
