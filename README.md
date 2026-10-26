# Howl Button

A [Dusklight](https://github.com/TwilitRealm/dusklight) mod that lets Wolf Link howl, growl and bark whenever
you want.

Everything uses the game's own animations and voices: the howl is the howl action the game plays in
cutscenes, and the growl is the stance and growl the wolf uses when targeting an enemy. No assets are
added or replaced.

## Controls

As Wolf Link, **hold R and press B** to howl.

- **Tap** for a single howl.
- **Hold B** to keep howling. In the sitting style the note sustains until you let go.

**Hold R and hold A** to growl, for as long as you keep A held.

- Standing still, or holding L: Wolf Link takes the crouched growl stance.
- Moving without L: the growl sound plays and Wolf Link keeps moving normally.

**Hold R and press D-pad Right** to bark. The game has no bark of its own; by default the bark is the
unused "Threat" sound left in the game, which plays while you hold the button. Other sounds can be
picked in the options. Barking doesn't interrupt what you're doing.

You can howl, growl or bark while standing still, walking or running, idling, panting at low health, or L-targeting
something. You can't howl in the air, while swimming, while carrying something in your mouth, during
cutscenes or text, or in any other special action (digging, sensing, dashing, attacking, and so on).

## Options

Open the Mods window, select **Howl Button**:

| Option | Default | What it does |
| --- | --- | --- |
| Hold | R | Button held for the chord: R, L, Z, or None. |
| Then press | B | Button pressed to howl: B, A, X, Y, or a D-pad direction. |
| Howl style | Sitting | **Sitting**: the howling-stone pose; long note that sustains while held. **Standing**: a shorter standing howl that repeats while held. |
| Allow howling while targeting enemies | Off | When off, you can't howl while L-targeting an enemy. Targeting NPCs and objects is never blocked. |
| Growl: Hold / Then press | R / A | The growl chord, with the same choices as the howl. |
| Bark: Hold / Then press | R / D-pad Right | The bark chord, with the same choices as the howl. |
| Bark: Sound | Threat (unused, hold) | Any of the game's ~50 wolf voice sounds. "Threat" is an unused sound left in the game, possibly a cut bark. Sounds marked "(hold)" loop while the button is held and stop when it's released. |

While a chord is pressed, its button is hidden from the game, so the chord doesn't also attack, dash or
toggle the minimap. In states where the chord can't be used, the button works normally.

Avoid chords Dusklight already uses: R+X (Sun's Song), R+Y (Quick Transform), R+D-pad Up/Down
(teleport). R+A also triggers Dusklight's Moon Jump if that cheat is enabled. If two chords are the same,
howl wins over bark, and bark over growl. Choosing "None" as the held button makes the pressed button howl on its own, which
replaces that button's normal wolf action in the states listed above.

## Known limitations

- **"Allow howling while targeting enemies" only covers L-targeting.** The game has no simple way to
  tell that a fight is happening when you aren't locked on, so with the option off you can still howl
  in the middle of a fight if you aren't targeting an enemy.
- The bark is sound only; the game has no bark animation.
- The growl stance only appears while standing still or holding L. When moving without L, the growl
  is sound only, because the game's normal running animations have no growl version.
- The howl is the plain vanilla howl. It doesn't play howling-stone songs or change the time of day
  (that's Dusklight's own "Sun's Song" setting).
- Only controller port 1 is read.

## Building

Requires CMake 3.26+, Ninja (or another generator), a C++20 compiler and git.

```sh
cmake -B build -G Ninja
cmake --build build
```

The first configure fetches the pinned Dusklight sources (`DUSKLIGHT_VERSION` in `CMakeLists.txt`,
currently `v2.0.3`) into `dusklight/`. That version must match the Dusklight release you run;
the loader rejects mods built against a different game ABI.

The result is `build/mods/howl_button.dusk`. A local build only contains your own platform's
library. The GitHub Actions workflow builds every platform and merges them into one `.dusk`
(artifact `mod-combined`; pushing a tag creates a release).

## Installing

Copy `howl_button.dusk` into the Dusklight mods folder:

- Windows: `%APPDATA%\TwilitRealm\Dusklight\mods`
- Linux: `~/.local/share/TwilitRealm/Dusklight/mods`
- macOS: `~/Library/Application Support/TwilitRealm/Dusklight/mods`

While developing, rebuild, copy, and click **Reload** in the in-game Mods window.

## How it works

`src/mod.cpp` installs these hooks:

- `fapGm_Execute`: runs each game tick after the controller is read and before any actor or menu.
  It detects the chords and removes their buttons from the pad state, so nothing else reacts to them.
- `daAlink_c::checkNextActionFromButton`: where the game decides which button action to take each
  frame. When the howl chord was pressed and Wolf Link is in an allowed state, it starts the vanilla
  howl with `procWolfHowlInit` and skips the normal button handling for that frame. It also plays the
  bark and growl sounds.
- `procWolfHowl`: the howl only sustains while the A button is held, so while the chord button is
  held the mod sets A in Link's per-frame button state.
- `daAlink_c::checkWolfAtnWait` (post-hook): the game's "targeting an enemy" test, which switches on
  the growl stance and voice. While growling it reports true.
- `daAlink_c::setBlendWolfMoveAnime`: while growling and standing still, it uses the lock-on
  animation blend instead, which shows the growl stance.
