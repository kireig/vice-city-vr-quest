#pragma once

// Ownership follows the ped pool; expensive updates have a separate fixed
// budget. Six recent/nearby contacts get priority, six oldest pending bodies
// get fair service. Sleeping poses need no solver work.
namespace VrRagdollSchedule {
enum { MAX_UPDATES = 12, PRIORITY_UPDATES = 6, MAX_STEPS = 18, MAX_PASSES = 72 };
struct Candidate {
    bool ready, urgent;
    float pending, distanceSq;
};
inline int Select(const Candidate *bodies, int count, int *selected)
{
    int used = 0;
    for(int turn = 0; turn < MAX_UPDATES; turn++){
        int best = -1;
        float bestScore = -1.0f;
        for(int i = 0; i < count; i++){
            if(!bodies[i].ready) continue;
            bool already = false;
            for(int j = 0; j < used; j++) already |= selected[j] == i;
            if(already) continue;
            const Candidate &c = bodies[i];
            // Older debt dominates the fair half even during sustained fire.
            const float score = turn < PRIORITY_UPDATES ?
                (c.urgent ? 2.0f : 0.0f) + 1.0f/(1.0f+c.distanceSq*0.01f) + c.pending :
                c.pending + 0.00001f/(1.0f+c.distanceSq);
            if(score > bestScore){ best = i; bestScore = score; }
        }
        if(best < 0) break;
        selected[used++] = best;
    }
    return used;
}
}
