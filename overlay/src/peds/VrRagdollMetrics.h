#pragma once

#ifdef __ANDROID__
#include <android/log.h>
#include "QuestProfiler.h"
#endif

// Opt-in, aggregate CPU measurements. No timers or per-contact log output
// while the existing Quest profiler is disabled.
namespace VrRagdollMetrics
{
enum Phase { UPDATE, SOLVE, GROUND, WORLD, VEHICLE, CONTACT, POSE, BULLET, ANIMATION, PHASE_COUNT };
enum Counter { STEPS, GROUND_QUERIES, POOL_CHECKS, CANDIDATES, CONTACTS,
	WAKEUPS, CANDIDATE_OVERFLOW, SHAPE_OVERFLOW, UNSUPPORTED_MODELS, DROPPED_STEPS, HITS,
	TRIANGLE_SCANS, TRIANGLE_OVERFLOW,
	CAR_BRAKES, BRAKE_POINTS,
	WORLD_QUERIES, WORLD_TRIANGLES, WORLD_OVERFLOW, WORLD_FALLBACKS,
	SOLVER_PASSES,
	COUNTER_COUNT };
struct Timing {
	unsigned long long total, peak;
	unsigned int calls;
};
static bool sampling;
static unsigned long long windowStart;
static Timing timing[PHASE_COUNT];
static unsigned int counters[COUNTER_COUNT];
static float peakLengthError, peakSpeedSq;
static float brakeImpulse;

inline unsigned long long
Now(void)
{
#ifdef __ANDROID__
	return androidgame::QuestProfilerNowNanoseconds();
#else
	return 0;
#endif
}

inline void
Reset(void)
{
	for(int i = 0; i < PHASE_COUNT; i++){
		timing[i].total = timing[i].peak = 0;
		timing[i].calls = 0;
	}
	for(int i = 0; i < COUNTER_COUNT; i++)
		counters[i] = 0;
	peakLengthError = peakSpeedSq = 0.0f;
	brakeImpulse = 0.0f;
}

inline void
BeginFrame(int active, int awake, int saved)
{
#ifdef __ANDROID__
	const bool on = androidgame::QuestProfilerIsEnabled();
	if(!on){
		sampling = false;
		return;
	}
	const unsigned long long now = Now();
	if(!sampling){
		Reset();
		windowStart = now;
		sampling = true;
	}
	if(now-windowStart < 1000000000ULL)
		return;
	const double frames = timing[UPDATE].calls ? timing[UPDATE].calls : 1;
	__android_log_print(ANDROID_LOG_INFO, "QuestRagdoll",
		"active=%d awake=%d saved=%d frames=%u window_ms=%.0f "
		"update_ms=%.3f peak_ms=%.3f solve_ms=%.3f ground_ms=%.3f "
		"vehicle_ms=%.3f pose_ms=%.3f bullet_ms=%.3f anim_ms=%.3f "
		"steps=%u ground=%u pool=%u candidates=%u contacts=%u wake=%u "
		"overflow=%u shapes_overflow=%u triangles_scan=%u triangles_overflow=%u unsupported=%u dropped=%u hits=%u error_pct=%.1f vmax=%.2f car_brakes=%u brake_points=%u brake_Ns=%.1f world_ms=%.3f world_queries=%u world_triangles=%u world_overflow=%u world_fallbacks=%u solver_passes=%u",
		active, awake, saved, timing[UPDATE].calls, (now-windowStart)*0.000001,
		timing[UPDATE].total*0.000001/frames,
		timing[UPDATE].peak*0.000001,
		(timing[SOLVE].total-timing[CONTACT].total)*0.000001/frames,
		timing[GROUND].total*0.000001/frames,
		(timing[VEHICLE].total+timing[CONTACT].total)*0.000001/frames,
		timing[POSE].total*0.000001/frames,
		timing[BULLET].total*0.000001/frames,
		timing[ANIMATION].total*0.000001/frames,
		counters[STEPS], counters[GROUND_QUERIES], counters[POOL_CHECKS],
		counters[CANDIDATES], counters[CONTACTS], counters[WAKEUPS],
		counters[CANDIDATE_OVERFLOW], counters[SHAPE_OVERFLOW], counters[TRIANGLE_SCANS], counters[TRIANGLE_OVERFLOW], counters[UNSUPPORTED_MODELS],
		counters[DROPPED_STEPS], counters[HITS], peakLengthError*100.0,
		sqrtf(peakSpeedSq),counters[CAR_BRAKES],counters[BRAKE_POINTS],brakeImpulse,
		timing[WORLD].total*0.000001/frames,counters[WORLD_QUERIES],counters[WORLD_TRIANGLES],
		counters[WORLD_OVERFLOW],counters[WORLD_FALLBACKS],counters[SOLVER_PASSES]);
	Reset();
	windowStart = now;
#else
	(void)active;
	(void)awake;
	(void)saved;
#endif
}

inline void
Count(Counter counter, unsigned int amount = 1)
{
	if(sampling)
		counters[counter] += amount;
}

inline void AddBrakeImpulse(float impulse)
{
	if(sampling) brakeImpulse += impulse;
}

inline void
ObserveStep(float lengthError, float speedSq)
{
	if(!sampling)
		return;
	if(lengthError > peakLengthError)
		peakLengthError = lengthError;
	if(speedSq > peakSpeedSq)
		peakSpeedSq = speedSq;
}

struct Scope {
	Phase phase;
	unsigned long long start;
	Scope(Phase p) : phase(p), start(sampling ? Now() : 0) {}
	~Scope() {
		if(!start)
			return;
		const unsigned long long elapsed = Now()-start;
		timing[phase].total += elapsed;
		timing[phase].calls++;
		if(elapsed > timing[phase].peak)
			timing[phase].peak = elapsed;
	}
};
}
