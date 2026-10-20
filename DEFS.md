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

Items are the defs with `"spawnclass" "item"`. Quake 3's items are built-in
defs of their classnames, so a def can inherit from `item_quad` or change
`weapon_shotgun`. Built-in items keep their numbers; items new in defs follow
in name order (256 at most). What an item gives:

| Key | Gives |
| --- | --- |
| `inv_health` N | health |
| `inv_armor` N | armor |
| `inv_weapon` `<weapon item>` | that weapon, with `inv_ammo` N |
| `inv_ammo` N, `ammo_type` `<weapon item>` | ammo for that weapon |
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
