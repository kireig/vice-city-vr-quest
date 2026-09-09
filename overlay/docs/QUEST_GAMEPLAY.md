# Quest gameplay

The in-headset menus group cheats by purpose. Back returns to the parent
category. Never Wanted is under WANTED / POLICE; it clears current pursuit and
blocks new wanted levels while enabled. The override is separate from native
mission/garage flags and is not saved: it defaults to OFF after app restart.
Scripted enemies are not removed.

Tracked weapon aim uses the held barrel pose, a limited range and view cone,
and an obstruction check before dispatching the original pedestrian or police
reaction. Holstered guns, invalid tracking and restricted game states do not
produce that reaction. Police checks are staggered over frames.

The tracked instant-hit path also invokes the original special police
helicopter damage test. World geometry and intervening vehicles still block
the shot; weapon damage rules remain native. Rockets use their own path.

Physical melee follows the full calibrated blade, including the first deliberate
swing after drawing. Contact latching, player-motion compensation and tracking
gap rejection prevent a continuous contact or a tracking jump from repeatedly
counting as a new swing. Run Without Limits bypasses the usual weapon-specific
sprint restrictions while enabled.

Vehicle entry distinguishes movement held during the approach from a new cancel
input after the control returns to neutral. Door locks, speed and vehicle state
checks still apply. Seat centering waits for the actual seat and valid current
head tracking, then centers once. Leaning later does not retrigger it.

The main VR menu has a direct Vehicle shortcut. Ragdoll and vehicle deformation
settings have their own pages, with per-setting explanations and saved tuning.
The culling inspector is available only when building with developer tools;
ordinary graphics culling choices remain available in player builds.

See [QUEST_RAGDOLL.md](QUEST_RAGDOLL.md) and
[QUEST_VEHICLE_DEFORMATION.md](QUEST_VEHICLE_DEFORMATION.md) for contact physics
and optional visual vehicle damage.
