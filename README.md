# Howl Button

A [Dusklight](https://github.com/TwilitRealm/dusklight) mod that lets Wolf Link howl whenever you want.

The howl uses the game's own animation and voice: the same howl action the game plays in cutscenes.
No assets are added or replaced.

## Controls

As Wolf Link, **hold R and press B** to howl.

- **Tap** for a single howl.
- **Hold B** to keep howling. In the sitting style the note sustains until you let go.

You can howl while standing still, walking or running, idling, panting at low health, or L-targeting
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

Avoid chords Dusklight already uses: R+X (Sun's Song), R+Y (Quick Transform), R+D-pad Up/Down
(teleport). Choosing "None" as the held button makes the pressed button howl on its own, which
replaces that button's normal wolf action in the states listed above.

## Known limitations

- **"Allow howling while targeting enemies" only covers L-targeting.** The game has no simple way to
  tell that a fight is happening when you aren't locked on, so with the option off you can still howl
  in the middle of a fight if you aren't targeting an enemy.
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

`src/mod.cpp` installs two pre-hooks on Link's actor (`daAlink_c`):

- `checkNextActionFromButton`: where the game decides which button action to take each frame.
  When the chord is pressed and Wolf Link is in an allowed state, it starts the vanilla howl with
  `procWolfHowlInit` and skips the normal button handling for that frame.
- `procWolfHowl`: the howl only sustains while the A button is held, so while the chord button is
  held the mod sets A in Link's per-frame button state.
