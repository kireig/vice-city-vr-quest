# Vehicle deformation

Vehicle deformation is optional visual damage applied after a native collision.
Its submenu controls strength (25–400%), affected radius (50–200%), maximum dent
depth (10–60 cm) and impact threshold (25–200%). Defaults are 100% strength,
radius and threshold with a 32 cm limit. Changes apply without rebuilding.
Lowering the maximum depth limits subsequent deformation; it does not repair
existing dents or move an already damaged panel back toward its original shape.

The native collision point, inward normal and impulse are recorded in the
receiver's collision pose, so both cars receive the correct local hit even if a
temporary collision transform is later restored. Recording adds no world query.

The material filter includes opaque exterior surfaces in eligible body frames,
including unpainted bumpers and grilles. Transparent surfaces and identifiable
engine, cabin and wheel textures remain protected. Shared vertices touching a
protected triangle veto movement. Each car owns its modified geometry; the
original shared model remains unchanged.

The selector considers nearby components and preserves the main chassis and
impact-side bonnet or boot. A fourth queued component is permitted when those
cannot fit alongside the nearest three. This prevents a rigid neighbouring wing
or body surface from covering a dent in another panel.

Processing is bounded across frames: fixed car records, vertex/triangle work
budgets, subdivision limits, one geometry commit per frame and private geometry
caps. It does not depend on the affected car being rendered. Empty or failed jobs
release their state, while cars with committed dents retain their geometry.
Disabling the option skips the processing path and restores owned geometry.

Deformation preserves UVs and uses an orientation check with backtracking to
avoid inverted triangles. Accumulated movement obeys the configured depth limit.
The normal
field follows compression and bonnet bending. This remains a visual approximation:
independent panels can open seams under repeated strong impacts, and native
damage-part replacement or detachment can change which geometry is visible.

Tests read a user's local DFF/COL files at runtime. They use rendered BinMesh
materials and native component transforms, then run the production module over
front, corner and side contacts. Exterior-contour checks include unselected body
parts; movement of the bonnet alone cannot satisfy them. Protected engine/cabin
geometry is measured separately from the exterior skin. See
[the test guide](../../tools/tests/README.md).
