# Quest ragdolls

Open TRAFFIC > RAGDOLL to enable ragdolls and tune body weight, vehicle braking,
car grip and bullet reaction. Changes apply immediately and retain their saved
preferences. Braking supports 0–2000%; zero disables the added braking response.
Grip acts through contact friction and does not attach a body to the vehicle.

New settings start with ragdolls ON, vehicle braking 200%, grip 150%, bullet
reaction 100% and body weight 200%. Existing saved preferences take precedence.

The solver uses 19 points, fixed bone lengths, anatomical knee/hip/spine limits
and selected limb/trunk self-contact pairs. A shared knee plane keeps the thigh,
calf and foot oriented consistently, including near a straight knee. Captured
model axes and unsimulated child bones remain compatible with the skin.

Car contacts use bounded collision shapes and triangles, swept movement and
surface velocity. Nearby static triangles, boxes and spheres are gathered from
world sectors into bounded caches. Joint and interior limb probes prevent fast
motion from crossing thin walls. Contact position correction and velocity
response are separate to avoid converting overlap repair into an extra launch.

Pose storage covers the 220-ped pool. Expensive simulation is scheduled separately:
at most 12 bodies, 18 steps and 72 anatomy passes per update. Priority contacts
retain fine steps; background bodies may combine intervals up to 50 ms. Waiting
and sleeping bodies retain their poses. This bounds work under crowd load; it
does not guarantee identical update frequency for an arbitrarily large crowd.

Carried bodies use a short presentation transform between their solved support
pose and the current car pose. The same coordinates are used for skin matrices,
bounds, sectors and bullet queries. Invalid, expired, recycled or abruptly moved
support rejects transport. The simulation remains free to slide or lose contact.
Current-step gravity is not counted again as a new impact against the car.

The entity root and shared collision model are not enlarged to fit a fallen
pose. Body bounds follow the actual joints; ordinary population and corpse-age
cleanup remain in force. This is an approximate particle solver, not a full
rigid-body engine or inter-corpse contact simulation.

Host tests cover illegal takeover poses, falls, settling, high-speed car/wall
contacts, moving support, reaction impulses, scheduling, leg orientation and
visibility. See [the test guide](../../tools/tests/README.md). Desktop timings
and compiled assertions do not establish Quest frame times or visual feel.
