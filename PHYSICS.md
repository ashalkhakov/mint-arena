# Physics props

Rigid bodies built on the Doom 3 collision model: a C port of Doom 3's
`idPhysics_RigidBody` in `code/game/g_physics.c`. Props sweep rotating,
non-box trace models through the clip world (`ClipMotion`), use contact
points for friction and resting, stack, tumble, get pushed by players and
knocked around by weapons. The cgame draws each prop from the same trace
model the game collides with.

Needs the OpenGL2 renderer for the debug views (`com_renderer opengl2`).

## Commands

Player commands (forwarded to the server, work in any game):

| Command | What it does |
| --- | --- |
| `prop [shape] [size]` | Drop a prop in front of you |
| `propthrow [shape] [speed]` | Throw a small prop from your eyes |
| `propstack [count] [size]` | A tower of crates on the floor in front of you |
| `proppyramid [rows] [size]` | A pyramid of crates |
| `propclear` | Remove all props |

Shapes: `box`, `cylinder`, `cone`, `octahedron`, `dodecahedron`.

Server console commands, for testing without a player:

| Command | What it does |
| --- | --- |
| `propspawn <shape> <x> <y> <z> [size] [pitch yaw roll]` | Spawn a prop at a position |
| `proplist` | Position, speed, contacts and rest state of every prop |
| `propblast <x> <y> <z>` | Rocket splash damage at a position |

## Map entity

`func_physics` (point entity): `shape`, `size` or `mins`/`maxs`, `density`,
`mass`, `bouncyness`, `friction`, `angles`, and:

- `model`, `modelscale`: an MD3 to draw, colliding as a convex hull of its
  mesh (below), or as the shape when the model has no hull.
- `static 1`: never moves, solid scenery.
- `hull_parts N`: how many convex pieces a static model prop may collide as
  (8; 1 for one hull around all of it).
- `health`, `breaksound`: breaks after that much damage, firing its targets.

A model prop placed into the floor is lifted out of it; one stuck any other
way stays where it is, static.

## Model hulls

`bg_prophull.c` wraps the first frame of an MD3 in a k-DOP: the planes
touching the mesh in 26 directions (the axes, the edge and the corner
diagonals). Trace models hold at most 16 faces and 32 edges, so while the
polytope is bigger the diagonal plane whose removal adds the least volume
goes. A hull that comes out with slivers falls back to 18 directions, then
to a box. Game and cgame build it from the same file, cached per model and
scale, so prediction collides with what the game simulates.

A static model prop collides as up to 8 convex pieces instead, so concave
scenery (a tree: its crown, its trunk) isn't wrapped as a whole, as
Half-Life gives models sets of hit boxes. The mesh is cut in two with a
plane again and again: each time the piece whose best cut (7 tried along
each axis) shrinks its bounding box the most is cut, while that saves at
least 30% of it. The triangles are clipped at the planes, so the pieces meet
without gaps, and each is wrapped as above. The game links one clip model a
piece; the cgame (told how many in `origin2[1]`) makes the same pieces to
predict against. Moving props still simulate as one hull. `proplist` says
how many pieces a prop has, and debug view 1 draws them.

Daikatana's decos are model props: the converter (bsp-sekai) writes a solid
`deco_e*` as a `func_physics` with its model, scale and mass, `static` for
those that don't move, and `health` for the ones Daikatana lets break.

## Cvars

| Cvar | Default | |
| --- | --- | --- |
| `g_physicsStep` | 16 | Simulation step in msec, a server frame runs several |
| `g_propDensity` | 0.005 | Mass per cubic unit, a 32 unit crate weighs 164 |
| `g_propBounce` | 0.4 | Bouncyness |
| `g_propFriction` | 0.015 | Contact friction, applied at every contact point |
| `g_propPushMass` | 60 | How hard players push props |
| `g_propKnockback` | 1 | Scales weapon knockback on props |

## Debug views

Drawn by the server game through the renderer's debug lines, so only on a
listen server. Many lines need `r_maxpolys 20000` and `r_maxpolyverts 80000`.

`g_debugPhysics` (bits):

| Bit | Shows |
| --- | --- |
| 1 | Prop hulls: green simulated, grey resting, red spawned in solid |
| 2 | Contact points and normals (yellow) |
| 4 | Linear (red) and angular (cyan) velocity |
| 8 | Impacts of the last second (magenta) |
| 16 | Center of mass and orientation |
| 32 | Statistics on the console every second |
| 64 | Every simulation step of the active props on the console |

`g_debugCollision` (bits):

| Bit | Shows |
| --- | --- |
| 1 | World collision polygons around the player |
| 2 | Every clip model around the player |
| 4 | Player and prop bounding boxes |

## Interactions

- Players push props by walking into them, using the velocity their movement
  keys ask for, so leaning on a crate keeps pushing it.
- Weapons push props with the knockback a player would get.
- Props hitting players hard (over 150 ups) knock them back.
- Movers carry props standing on them and push props in their way. A blocked
  push is undone for props like for everything else. Doors stop and reverse on
  props like on players, crushers pop them.
- Impacts clank, hard impacts thud.

The game sends props' positions as trajectories (linear while sliding, gravity
while airborne) and the cgame predicts against the exact rotated trace model
through `trap_CM_TempTraceModel` (cgame API 1.1), so pushing props mispredicts
very little.
