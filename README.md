# Howl Button

A [Dusklight](https://github.com/TwilitRealm/dusklight) mod that lets Wolf Link howl, growl, bark or play any wolf voice
whenever you want.

Everything uses the game's own animations and voices: the howl is the howl action the game plays in
cutscenes, and the growl is the stance and growl the wolf uses when targeting an enemy. No assets are
added or replaced.

## Controls

The mod has four chords. Each one can be set to any sound in the options. The defaults are:

| Chord | Sound |
| --- | --- |
| **R + B** | Bark |
| **R + A** | Growl |
| **R + D-pad Right** | Howl |
| **R + D-pad Left** | None (unassigned) |

- **Howl**: the howl animation and voice. Tap for a single howl; hold to keep howling. In the sitting
  style the note sustains; in the standing style Wolf Link holds the nose-up pose.
- **Growl**: lasts while the button is held. Standing still, or holding L, Wolf Link takes the crouched
  growl stance. Moving without L, the growl sound plays and Wolf Link keeps moving normally.
- **Bark**: the game has no bark of its own. This is an unused "Threat" sound left in the game; it plays
  while you hold the button. Barking doesn't interrupt what you're doing.
- **Any other wolf voice**: all ~50 of the game's wolf voice sounds are available, as sound only.
  Sounds marked "(hold)" loop while the button is held and stop when it's released.

You can use the chords while standing still, walking or running, idling, panting at low health, or
L-targeting something. They don't work in the air, while swimming, while carrying something in your
mouth, during cutscenes or text, or in any other special action (digging, sensing, dashing,
attacking, and so on).

## Options

Open the Mods window and select **Howl Button**:

| Option | Default | What it does |
| --- | --- | --- |
| Chord 1–4: Hold | R | Button held for the chord: R, L, Z, or None. |
| Chord 1–4: Then press | B / A / D-pad Right / D-pad Left | Button pressed: B, A, X, Y, or a D-pad direction. |
| Chord 1–4: Sound | Bark / Growl / Howl / None | **None**, **Howl**, **Growl**, or any wolf voice. "Howl voice only" and "Growl voice only" play just the sound, with no animation. |
| Howl style | Sitting | **Sitting**: the howling-stone pose; long note that sustains while held. **Standing**: a standing howl; holding the button holds the nose-up pose and the note. |
| Allow howling while targeting enemies | Off | When off, you can't howl while L-targeting an enemy. Targeting NPCs and objects is never blocked. |

While a chord is pressed, its button is hidden from the game, so the chord doesn't also attack, dash or
open the map. A chord set to **None** leaves its button alone, and in states where a chord can't be
used, the button works normally.

Avoid chords Dusklight already uses: R+X (Sun's Song), R+Y (Quick Transform), R+D-pad Up/Down
(teleport). R+A also triggers Dusklight's Moon Jump if that cheat is enabled. If two chords use the
same button, the lower-numbered chord wins. Choosing "None" as the held button makes the pressed
button work on its own, which replaces that button's normal wolf action in the states listed above.

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
  frame. When a Howl chord was pressed and Wolf Link is in an allowed state, it starts the vanilla
  howl with `procWolfHowlInit` and skips the normal button handling for that frame. It also plays the
  growl sound and any voices assigned to chords.
- `procWolfHowl`: holds the howl while the chord button is held. The sitting howl has a vanilla
  sustain that runs while A is held, so the mod sets A in Link's per-frame button state. The
  standing howl has none, so the mod sways its animation along a sine wave across the nose-up part
  (frames 18–34, where the nose is 72–75° up) and plays the howl voice. On release, the animation
  eases back to normal speed and plays out.
- `daAlink_c::checkWolfAtnWait` (post-hook): the game's "targeting an enemy" test, which switches on
  the growl stance and voice. While growling it reports true.
- `daAlink_c::setBlendWolfMoveAnime`: while growling and standing still, it uses the lock-on
  animation blend instead, which shows the growl stance.
- `Z2CreatureLink::startLinkVoice`: Link's voice plays one sound at a time, and while moving the
  wolf's breathing sounds keep it busy. While a growl or voice chord is held, the breathing sounds
  are skipped so the chord's sound can play.
- `Z2CreatureLink::startLinkVoiceLevel`: the game lowers the growl's volume when L is held
  without a target (0.3) or a non-enemy is targeted (0.5). While growling, the mod plays it at the
  normal volume (1.0) unless an enemy is targeted (the vanilla 0.8).
