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
| `def_projectile` | what it fires |
| `numProjectiles` N, `spread` D | that many a shot, at most D degrees off aim |
| `obituary`, `obituary2` | kill message: "victim obituary attacker obituary2" |
| `obituary_splash`, `obituary_self` | for splash kills and suicides |

What it looks like: `model_view` (first person), with `anim_raise`,
`anim_lower`, `anim_idle`, `anim_fire`, `anim_reload`, `anim_empty` naming
the model's frames (`"shoota"` for `shoota01`..., optionally followed by
frames a second) and `anim_fps` (10); `model_world` (on the player's
tag_weapon), `model_flash`, `icon`, `snd_fire` (several, picked at random),
`snd_raise`, `snd_hum`, `snd_firing`, `flashColor` and `flashRadius` (a
light), `fx_muzzle` (an effect, see below).

The weapons players start with are the `weapon` key of the `player` def
(Doom 3's), a list of weapon defs.

## Projectiles

The keys are Doom 3's: `velocity` (forward, left, up), `gravity` (non-zero
falls), `bounce` (the speed kept), `max_bounces`, `fuse` (seconds),
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
