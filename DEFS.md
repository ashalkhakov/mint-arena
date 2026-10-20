# Entity defs

Doom 3 style entity definitions, read by the game and the cgame from every
`def/*.def` (in name order), in `code/game/bg_defs.c`:

```
entityDef item_health_25 {
	"inherit"	"item_health"			// its keys, unless this def sets them
	"inv_name"	"Health"
	"model"		"models/e1/a1_hlth.md3"
}
```

`//` and `/* */` comments work; other kinds of Doom 3 declarations in the
same files are skipped. A def's `classname` is its name. Defs a file gives
replace earlier ones of the name, except the ones built into the game, which
a file's def only changes key by key.

## Items

Items are the defs with `"spawnclass" "item"`. Quake 3's items (but its
weapons and ammo) are built-in defs of their classnames, so a def can inherit
from `item_quad`. Built-in items keep their numbers; items new in defs follow
in name order (256 at most). What an item gives:

| Key | Gives |
| --- | --- |
| `inv_health` N | health |
| `inv_armor` N | armor |
| `inv_weapon` `<weapon def>` | that weapon, with `inv_ammo` N |
| `inv_ammo` N, `ammo_type` `<weapon def>` | ammo for that weapon |
| `inv_powerup` `<powerup item>` | that powerup, for `time` seconds |
| `inv_persistant` `<item>` | a persistant powerup |
| `inv_holdable` `<holdable item>` | that holdable |
| `inv_team` `<item>` | a team item (flags) |

Look: `model` (and `model2` to `model4`), `model_scale` (1), `icon`,
`snd_acquire`, `inv_name`, `sounds` (more to precache). An empty value
(`"model2" ""`) clears an inherited one.

## Item physics

With `g_itemPhysics 1` (the default, latched) items are rigid bodies, as
Doom 3's moveable items: they fall to the floor from where maps place them,
tumble when dropped and are pushed by explosions; they collide as the hull
of their model (see PHYSICS.md), or their old box for a model without one,
and stay triggers for picking up. Picked up, they come back where they
first came to rest. Keys: `mass` or `density`, `bouncyness` (0.1),
`friction` (0.5), `nophysics`. Suspended map items (spawnflag 1) and team
items (flags) keep Quake 3's behaviour. The cgame draws them where the game
simulates them, without bobbing or spinning.

## Weapons

There are no built-in weapons: a weapon is a def with a `weapon_slot`, the
number key that selects it (`weapon 2` picks the player's weapon in slot 2,
again for the next one there). Weapons are numbered from 1 by slot, then by
name (63 at most). A weapon is usually an item too, Doom 3 style:

```
entityDef weapon_ionblaster {
	"spawnclass"		"item"
	"inv_weapon"		"weapon_ionblaster"		// itself
	"inv_ammo"			"40"
	"weapon_slot"		"2"
	"fireRate"			"0.4"
	"def_projectile"	"projectile_ion"
	...
}
```

| Key | |
| --- | --- |
| `weapon_slot` N | the number key |
| `fireRate` S | seconds between shots (0.5) |
| `ammoRequired` N | ammo a shot takes (1), 0 for none |
| `maxAmmo` N | the most ammo it holds (200) |
| `startAmmo` N | ammo when players start with it |
| `raiseTime`, `lowerTime` S | switching (0.25, 0.2) |
| `melee` 1 | a melee weapon (the torso animations, awards) |
| `burstCount` N, `burstRate` S | a pull of the trigger fires N shots S seconds apart; `fireRate` is then the whole burst's |
| `altfire` `detonate`, `altFireRate` S | the alt attack button (`+altattack`) sets off the player's projectiles of the weapon |
| `bot_rating` N | how much bots like it (slot × 100); 0, never |
| `def_projectile` | what it fires |
| `numProjectiles` N, `spread` D | that many a shot, at most D degrees off aim |
| `obituary`, `obituary2` | kill message: "victim obituary attacker obituary2" |
| `obituary_splash`, `obituary_self` | for splash kills and suicides |

What it looks like: `model_view` (first person), with `anim_raise`,
`anim_lower`, `anim_idle`, `anim_fire`, `anim_reload`, `anim_empty` naming
the model's frames (`"shoota"` for `shoota01`..., optionally followed by
frames a second) and `anim_fps` (10); `model_world` (on the player's
tag_weapon; on a skeletal character's weapon joint its frame
`model_world_frame`, -1 for none, turned and moved by `model_world_angles`
and `model_world_offset`, scaled by `model_world_scale`), `model_flash`, `icon`, `snd_fire` (several, picked at random),
`snd_raise`, `snd_hum`, `snd_firing`, `snd_altfire`, `flashColor` and
`flashRadius` (a light), `fx_muzzle` (an effect, see below), `anim_altfire`.
A first person model is drawn at the eye, facing the view, with Daikatana's
small bob, as its models are made for that.

**Reloading, Doom 3's.** A weapon with `clipSize` holds that many shots
loaded: a shot takes from the clip as well as from the ammo carried (`ammo`
stays all of it, `ammoClip` what's loaded). An empty clip reloads by itself,
and `+reload` (bound to R; Doom 3's reload impulse) reloads early; the reload
takes `reloadRate` seconds, plays `anim_reload` and `snd_reload`, and the
clip is filled when it's over. Changing weapons cancels it. Weapons come
loaded when given or picked up. An animation may be part of the model's
frames: `"<frames> [fps [first [count]]]"`, so `"shoot 22 34"` plays `shoot`
from its 35th frame (the shotcycler fires its volley with the start of
`shoot` and reloads with the rest, as Daikatana's cycles).

`player_anim_suffix` picks a skeletal player's animations by the weapon, as
Daikatana's do by weapon type and Doom 3's by `animPrefix`: with `"b"` the
torso and legs play `atakb`, `runb`… where the model has them, else the plain
ones.

The weapons players start with are the `weapon` key of the `player` def
(Doom 3's), a list of weapon defs.

## Projectiles

The keys are Doom 3's: `velocity` (forward, left, up), `gravity` (non-zero
falls), `bounce` (the speed kept), `max_bounces`, `stick` (stops where it hits, facing out of
the surface), `fuse` (seconds),
`detonate_on_fuse`, `detonate_on_world` (else it bounces), `detonate_on_actor`,
`mins`/`maxs`, `def_damage` and `def_splash_damage` (defs with `damage` and
`radius`). `"hitscan" "1"` traces the shots at once, to `range` (8192); the
cgame makes their impacts from the same seed. Melee weapons are hitscan ones
with a short range.

Looks: `model`, `light_color` and `light_radius`, `snd_fly`, `fx_fly` (an
effect every `fx_fly_interval` seconds, 0.05, along the path), `fx_detonate`
and `snd_explode`, `fx_impact`, `fx_impact_flesh` and `snd_impact` (hitscan),
`mtr_detonate` and `decal_size` (a mark).

## Effects

Effects are `effects/<name>.efx` files in the format of Jedi Academy's
effects (cg_fx.c): particles, tails, lines, lights, sounds, decals,
emitters, camera shakes, each with ranges and curves, so everything a weapon
shows is data. A def names them without the extension (`"dk/ion_fly"`).

## Models

Skeletal characters are IQM models (Spearmint draws them, and the game asks
it where their joints are on any frame). A model is a def with a `mesh`,
Doom 3's `model` decl as entityDef keys (`bg_model.c`):

```
entityDef model_testchar {
	"mesh"				"characters/test/testchar.iqm"
	"mins"				"-16 -16 -24"			// its collision box (a player's standing)
	"maxs"				"16 16 48"
	"anim idle"			"idle"					// the IQM's animation of the name...
	"anim wave"			"40 20 10 loop"			// ...or frames: first, count, fps, loop
	"hitbox Head"		"-4.5 -4 0 4.5 4 10 head"	// a box on a joint: mins, maxs in its space, zone
	"damage_scale head"	"2"						// Doom 3's
	"frame walk 0"		"sound sounds/global/p_stp1.wav"
	"channel torso"		"*Waist"				// the joints the torso channel plays on (the default)
	"channel head"		"*Head"					// and the head's (the default)
	"blend_time"		"0.2"					// seconds an animation fades in over (the default)
}
```

- **Channels** are Doom 3's: animations play at once on the legs (every
  joint), the torso and the head, each over the ones before it, as
  `idAnimator` does. A channel's joints are a list: `*Waist` is the joint and
  everything under it, `Waist` the joint alone, `-Joint` and `-*Joint` take
  them out (`"*Waist -*Head"`). A channel with nothing playing shows what's
  under it. A new animation cross-fades from the last over `blend_time`;
  one stopped on the torso or head fades out the same way. The game and the
  cgame blend the same way from the same numbers, so the hitboxes follow the
  pose drawn, and the renderer skins the mesh from the joints the cgame gives
  it.

- **Hitboxes** ride their joints. Shots (hitscan and projectiles) that reach
  a character's bounding box are tested again against its boxes, exactly,
  with the collision model; a shot that misses them all goes on through (between
  the legs). The box's zone scales the damage (`damage_scale <zone>`, 1 by
  default; zone `body` for a box without one). The game keeps the last 32
  frames of a character's animation and place, so shots rewound for lag hit
  the boxes where they were.
- **Frame commands** run when the animation reaches the frame: `sound
  <file>`, `fx <effect>` (the cgame's); `event <name>` is for the game (the
  move system, to come).
- Numbers: models are numbered by name in the game and the cgame alike; an
  `ET_CHARACTER` entity carries its model in `modelindex2`, the animation
  in `legsAnim` and its start in `time`, the torso's in `torsoAnim` (+1, 0
  for none) and `time2`; the head's is to come.

**Players.** A player whose model (`model` in the userinfo, `hiro` or
`hiro/skin`) names a model def, `model_hiro` or `hiro` itself, is a skeletal
character: drawn from the def's mesh, with hitboxes that shots hit as they hit
a `misc_character`'s, rewound for lag the same. pmove's animations (Quake 3's
`animNumber_t`, `animation.cfg`'s order) play through the def's keys:

```
	"player_anim LEGS_RUN"		"run"			// the model's animation for it
	"player_anim BOTH_DEAD1"	"die hold"		// hold: its last frame, for a corpse
	"player_anim TORSO_ATTACK"	"atak"			// on the torso channel, over the legs'
	"aim_torso"					"Waist"			// the joints that turn to aim (the defaults)
	"aim_head"					"Head"
	"joint_weapon"				"WEAPON_ATTACHER"	// where the weapon's "model_world" goes
	"joint_flag"				"FLAG_ATTACHER"		// and the flag
	"crouch_height"				"14"				// maxs z crouched (16)
	"eye_height"				"25"				// the view standing (26)
	"crouch_eye_height"			"10"				// and crouched (12)
```

**Ladders.** On a ladder pmove plays `LEGS_CLIMB` (or `LEGS_CLIMBDOWN` going
down); a skeletal model maps them like the rest (Daikatana's `ladderup`,
`ladderdn`). Their frames go by height, not time: a cycle for every
`ladder_climb` units climbed, so the hands and feet keep to the rungs at any
speed and stop with the player. The model faces the ladder and moves up to it
so the hands, `ladder_reach` ahead of it, are on the rungs; the game and every
client find the ladder by the same trace (`BG_PlayerLadder`), nothing more is
sent. The weapon is put away and doesn't fire, and the view turns at most 70°
from the ladder. Climbing is Daikatana's: 200 up or down (looking up or down
while going forward, or jump and crouch), 75 across, even from water.

The player's box is its model's (`BG_PlayerBox`): `mins`/`maxs` standing,
`crouch_height` its top crouched, Quake 3's without a def. The game sizes it
by the userinfo model, and the cgame predicts by the same (the configstring's
`model`, not what `cg_forceModel` draws).

An animation without a key leaves the legs on the model's first and the
torso to the legs. The game stamps when each started (`time`, `time2`), so
the cgame and the hitboxes blend them alike; the torso and head turn by
Quake 3's angles (the legs to the movement, the torso a quarter of that and
three quarters of the pitch, the head the view), the cgame's swinging and
the game's not. A model without a def is drawn Quake 3's way.

**Sounds and footsteps, Doom 3's way.** A model def's `snd_*` keys name
sound shaders: `sound/*.sndshd` blocks listing files (one played at random)
with Doom 3's parameters (`volume`, `minDistance`, `maxDistance`, `no_dups`;
`volume` is applied, the distances are kept for later). A key may also list
files directly. A player's voice comes from `snd_pain`, `snd_pain_heavy` (low
health), `snd_death`, `snd_jump`, `snd_land`, `snd_land_hurt`, `snd_gasp`,
`snd_drown`, `snd_exitwater` and `snd_taunt` instead of Quake 3's
`sound/player/<model>/`. The frame command `footstep <joint> [volume]` traces
under that joint, and the hit material's surface type (`SURF_TYPE` of its
surface flags: Doom 3's `surfTypes_t`, metal, stone, flesh, wood, cardboard,
liquid, glass, plastic, ricochet, then ice, snow and sand) picks
`snd_footstep_<type>`, or `snd_footstep`. Players whose model has them don't
make Quake 3's footstep events.

**Walk IK, Doom 3's** (`idIK_Walk`, `bg_ik.c`). After the animations blend,
each foot's floor is traced, the ankles move to it (smoothed), the waist drops
(or rises) so the feet reach, and each leg is solved as two bones keeping its
bend; the ankles keep their orientation. Unlike Doom 3, a foot moves at most a
step (18), a foot over no floor (or traced from inside a wall) stays, a target
out of reach is pulled in instead of folding the leg, the knee bends only
forward, and the bones turn the least way, never twisting about their length. The keys are Doom 3's: `ik_numLegs`,
`ik_waist`, `ik_hip<n>`, `ik_knee<n>`, `ik_ankle<n>`, `ik_foot<n>` (the ankle
when missing), `ik_footSize`, `ik_smoothing`, `ik_waistSmoothing`,
`ik_footShift`, `ik_waistShift`, `ik_minWaistAnkleDist`, `ik_footUpTrace`,
`ik_footDownTrace`. Players' feet are planted when on the ground, characters'
always; `cg_ik 0` turns the drawing's off. The game's hitboxes follow it too,
unsmoothed (the pose at the moment shot, rewound for lag as the rest).

`misc_character` (map entity): `model`, `anim`, `torso_anim`, `anim_cycle`
(seconds to the next animation, 0 never), `health`, `angle`.
`spawncharacter <model> [anim] [height] [side]` puts one in front of you, and
`characteranim <entity | all> <legs | torso> <anim | none>` plays an
animation on one (cheats). `cg_drawHitboxes 1` draws the
boxes, `cg_drawSkeleton 1` the joints (a line to each parent, and the axes:
x red, y green, z blue; 2 without the mesh), `g_debugHitboxes 1` says what
shots hit.
