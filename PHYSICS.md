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
`mass`, `bouncyness`, `friction`, `angles`.

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
