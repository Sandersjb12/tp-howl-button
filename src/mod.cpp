// Howl Button: lets Wolf Link howl, growl and bark on demand.
//
// Howl reuses the vanilla howl action (procWolfHowlInit / procWolfHowl), which the game otherwise
// only enters from cutscenes. It provides the howl animation, the Z2SE_WL_V_HOWL voice, and a
// sustain loop (frames 38-56) while the action button is held.
//
// Growl reuses the stance and voice the game plays while L-targeting an enemy, both gated by
// daAlink_c::checkWolfAtnWait().
//
// Bark plays one of the wolf voice sounds. There is no vanilla bark, so the sound is selectable;
// the list doubles as a way to audition every wolf voice in game.

#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "mods/svc/ui.h"

// Game includes
#include "Z2AudioLib/Z2SeMgr.h"
#include "d/actor/d_a_alink.h"
#include "d/d_com_inf_game.h"
#include "f_ap/f_ap_game.h"
#include "m_Do/m_Do_controller_pad.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

DEFINE_MOD();
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(ConfigService, svc_config);
IMPORT_SERVICE(UiService, svc_ui);

namespace {

// Dropdown labels and their pad masks; index is the stored config value.
constexpr const char* kModifierLabels[] = {"R", "L", "Z", "None"};
constexpr u32 kModifierMasks[] = {PAD_TRIGGER_R, PAD_TRIGGER_L, PAD_TRIGGER_Z, 0};
static_assert(std::size(kModifierLabels) == std::size(kModifierMasks));

constexpr const char* kButtonLabels[] = {
    "B", "A", "X", "Y", "D-pad Up", "D-pad Down", "D-pad Left", "D-pad Right"};
constexpr u32 kButtonMasks[] = {PAD_BUTTON_B, PAD_BUTTON_A, PAD_BUTTON_X, PAD_BUTTON_Y,
    PAD_BUTTON_UP, PAD_BUTTON_DOWN, PAD_BUTTON_LEFT, PAD_BUTTON_RIGHT};
static_assert(std::size(kButtonLabels) == std::size(kButtonMasks));
constexpr int64_t kButtonA = 1;
constexpr int64_t kButtonDpadRight = 7;

// Interpolation frames when switching between the idle and growl stances (-1 would snap).
constexpr f32 kGrowlBlendFrames = 5.0f;

// Sitting is the howling-stone pose (WANM_HOWL_SIT): vanilla sustains it while held.
// Standing (WANM_HOWL) has no vanilla sustain, so the mod holds its pose while held.
constexpr const char* kStyleLabels[] = {"Sitting", "Standing"};
constexpr int64_t kStyleSitting = 0;

// Standing howl (WANM_HOWL, 49 frames): the nose is up (72-75 degrees, measured from head joint 4)
// on frames 16-35, and vanilla voices frames 38-40 as the head comes down, then ends or restarts
// the howl past the cancel frame (40). Holding sways the animation along a sine wave across the
// nose-up plateau and voices it; on release the rate eases back to normal, the mod keeps voicing
// until vanilla's own voice window, and the howl plays out.
// The rate must never be 0: daPy_frameCtrl_c::checkAnmEnd() treats a stopped animation as ended.
constexpr f32 kStandingSwayCenter = 26.0f;
constexpr f32 kStandingSwayAmplitude = 8.0f;  // frames 18-34
// Peak sway speed is amplitude * omega = 1 frame/tick, matching normal playback where the sway
// starts (the center, moving forward).
constexpr f32 kStandingSwayOmega = 1.0f / kStandingSwayAmplitude;
constexpr f32 kStandingReleaseEase = 0.2f;     // fraction of the remaining rate change per tick
constexpr f32 kStandingVanillaVoiceFrame = 38.0f;

// Every wolf voice in Z2SeMgr.h. Looping ("level") sounds must be refreshed every tick, so they
// play while the bark chord is held instead of as a one-shot; started as a one-shot they never
// stop. The unused sounds have no vanilla call site to tell, so they're treated as looping
// (THREAT is confirmed to loop).
struct BarkSound {
    const char* label;
    u32 id;
    bool loop;
};

constexpr BarkSound kBarkSounds[] = {
    {"Threat (unused, hold)", Z2SE_WL_V_THREAT, true},
    {"Title screen howl (unused, hold)", Z2SE_WL_V_TITLE_HOWL, true},
    {"Breath: idle", Z2SE_WL_V_BREATH_WAIT, false},
    {"Breath: tired", Z2SE_WL_V_BREATH_TIRED, false},
    {"Breath: sitting", Z2SE_WL_V_BREATH_SIT, false},
    {"Breath: walk", Z2SE_WL_V_BREATH_WALK, false},
    {"Breath: run", Z2SE_WL_V_BREATH_RUN, false},
    {"Breath: jump", Z2SE_WL_V_BREATH_JUMP, false},
    {"Damage", Z2SE_WL_V_DAMAGE, false},
    {"Howl (hold)", Z2SE_WL_V_HOWL, true},
    {"Yawn", Z2SE_WL_V_YAWN, false},
    {"Growl (hold)", Z2SE_WL_V_ROAR, true},
    {"Attack: short", Z2SE_WL_V_ATTACK_S, false},
    {"Attack: long", Z2SE_WL_V_ATTACK_L, false},
    {"Attack: spin", Z2SE_WL_V_ATTACK_SPIN, false},
    {"Attack: thrust", Z2SE_WL_V_ATTACK_THRUST, false},
    {"Dash", Z2SE_WL_V_DASH, false},
    {"Sniff (hold)", Z2SE_WL_V_SNIFF, true},
    {"Snort", Z2SE_WL_V_NOSE, false},
    {"Land damage", Z2SE_WL_V_LAND_DAMAGE, false},
    {"Fall to restart", Z2SE_WL_V_FALL_TO_RESTART, false},
    {"Restart", Z2SE_WL_V_RESTART, false},
    {"Die", Z2SE_WL_V_DIE, false},
    {"Bite attack", Z2SE_WL_V_BITE_ATTACK, false},
    {"Bite throat", Z2SE_WL_V_BITE_THROAT, false},
    {"Hold in mouth", Z2SE_WL_V_HOLD_MOUTH, false},
    {"Lock-on jump", Z2SE_WL_V_LOCK_JUMP, false},
    {"Terrified", Z2SE_WL_V_TERRORED, false},
    {"Caught", Z2SE_WL_V_CATCHED, false},
    {"Caught (hold)", Z2SE_WL_V_CATCHED_LOOP, true},
    {"Slip roll", Z2SE_WL_V_SLIP_ROLL, false},
    {"Slip climb", Z2SE_WL_V_SLIP_CLIMB, false},
    {"Die in water", Z2SE_WL_V_DIE_WATER, false},
    {"Fall in lava", Z2SE_WL_V_FALL_MAGMA, false},
    {"Fall in quicksand", Z2SE_WL_V_FALL_QUICKSAND, false},
    {"Push rock", Z2SE_WL_V_PUSH_ROCK, false},
    {"Turn back", Z2SE_WL_V_TURN_BACK, false},
    {"Throw from mouth", Z2SE_WL_V_HOLD_THROW, false},
    {"Bite and hang", Z2SE_WL_V_BITE_HANG, false},
    {"Failed", Z2SE_WL_V_FAILED, false},
    {"Foot miss", Z2SE_WL_V_FOOTMISS, false},
    {"Entrance", Z2SE_WL_V_ENTRANCE, false},
    {"Midna shock", Z2SE_WL_V_MD_SHOCK, false},
    {"Twilight pull", Z2SE_WL_V_TW_PULL, false},
    {"Poe attack: start", Z2SE_WL_V_ATTACK_POU_ST, false},
    {"Poe attack: bite", Z2SE_WL_V_ATTACK_POU_BITE, false},
    {"Electric damage", Z2SE_WL_V_DAMAGE_ELEC, false},
    {"Freeze damage", Z2SE_WL_V_DAMAGE_FREEZE, false},
};

// Dropdown wants a plain label array.
constexpr auto kBarkSoundLabels = [] {
    std::array<const char*, std::size(kBarkSounds)> labels{};
    for (size_t i = 0; i < labels.size(); i++) {
        labels[i] = kBarkSounds[i].label;
    }
    return labels;
}();

// A configurable chord. The input layer claims its button for the tick it's pressed and for as
// long as it stays held, so the button's vanilla action never sees it.
struct ChordAction {
    ConfigVarHandle modifier = 0;
    ConfigVarHandle button = 0;
    bool triggered = false;  // pressed and claimed this tick; cleared once acted on
    bool held = false;       // claimed and still physically held
};

ChordAction g_howl;
ChordAction g_bark;
ChordAction g_growl;

ConfigVarHandle g_cvarStyle = 0;
ConfigVarHandle g_cvarAllowCombatHowl = 0;
ConfigVarHandle g_cvarBarkSound = 0;

// Set while a howl this mod started is playing, so the sustain hook leaves cutscene howls alone.
bool g_modHowl = false;
// Standing howl hold state, sway tick counter, and the animation rate to resume with.
enum class StandingHold { None, Sway, Release };
StandingHold g_standingHold = StandingHold::None;
int g_standingSwayTick = 0;
f32 g_standingHowlRate = 1.0f;
// Whether the idle growl stance was shown on the previous setBlendWolfMoveAnime call.
bool g_growlStance = false;
// Set while the mod asks for the real checkWolfAtnWait() result (the howl's combat gate).
bool g_queryRealAtnWait = false;

DEFINE_HOOK(fapGm_Execute, GameExecute);
DEFINE_HOOK(&daAlink_c::checkNextActionFromButton, NextActionFromButton);
DEFINE_HOOK(&daAlink_c::procWolfHowl, WolfHowl);
DEFINE_HOOK(&daAlink_c::checkWolfAtnWait, WolfAtnWait);
DEFINE_HOOK(&daAlink_c::setBlendWolfMoveAnime, WolfMoveAnime);
DEFINE_HOOK(&Z2CreatureLink::startLinkVoice, LinkVoice);
DEFINE_HOOK(&Z2CreatureLink::startLinkVoiceLevel, LinkVoiceLevel);

int64_t get_int_option(ConfigVarHandle handle, int64_t fallback) {
    int64_t value = fallback;
    if (handle == 0 || svc_config->get_int(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

bool get_bool_option(ConfigVarHandle handle, bool fallback) {
    bool value = fallback;
    if (handle == 0 || svc_config->get_bool(mod_ctx, handle, &value) != MOD_OK) {
        return fallback;
    }
    return value;
}

template <class T, size_t N>
const T& get_table_option(ConfigVarHandle handle, const T (&table)[N]) {
    const int64_t index = std::clamp<int64_t>(get_int_option(handle, 0), 0, N - 1);
    return table[index];
}

// Only ordinary standing/moving states; anything else (dash, swim, dig, carry, cutscene
// states...) has exit cleanup that a forced state change would skip.
bool in_free_wolf_state(daAlink_c* link) {
    switch (link->mProcID) {
    case daAlink_c::PROC_WOLF_WAIT:
    case daAlink_c::PROC_WOLF_MOVE:
    case daAlink_c::PROC_WOLF_SERVICE_WAIT:
    case daAlink_c::PROC_WOLF_TIRED_WAIT:
    case daAlink_c::PROC_WOLF_ATN_AC_MOVE:
        break;
    default:
        return false;
    }
    return link->checkWolf() && !link->checkEventRun() && link->mLinkAcch.ChkGroundHit() &&
           !link->checkModeFlg(daAlink_c::MODE_PLAYER_FLY) && !link->checkWolfGrabAnime();
}

bool can_howl(daAlink_c* link) {
    if (!in_free_wolf_state(link)) {
        return false;
    }
    if (link->mProcID != daAlink_c::PROC_WOLF_ATN_AC_MOVE ||
        get_bool_option(g_cvarAllowCombatHowl, false))
    {
        return true;
    }
    // checkWolfAtnWait() is the game's "L-targeting an enemy" test. Bypass the growl override.
    g_queryRealAtnWait = true;
    const bool targetingEnemy = link->checkWolfAtnWait();
    g_queryRealAtnWait = false;
    return !targetingEnemy;
}

void update_chord(ChordAction& action, bool allowed, u32 hold, u32 trig, u32& claimed) {
    const u32 modifier = get_table_option(action.modifier, kModifierMasks);
    const u32 button = get_table_option(action.button, kButtonMasks);

    action.triggered = false;
    if (action.held) {
        action.held = (hold & button) != 0;
    }
    if (!action.held && allowed && !(claimed & button) && (hold & modifier) == modifier &&
        (trig & button))
    {
        action.triggered = true;
        action.held = true;
    }
    if (action.held) {
        claimed |= button;
    }
}

// Runs once per sim tick after the pad is read and Dusklight's own combos ran, before any actor
// or menu, so consumed buttons are invisible to the rest of the game for this tick.
HookAction on_game_execute_pre(ModContext*, void*, void*, void*) {
    interface_of_controller_pad& pad = mDoCPd_c::getCpadInfo(PAD_1);
    const u32 hold = pad.mButtonFlags;
    const u32 trig = pad.mPressedButtonFlags;

    daAlink_c* link = static_cast<daAlink_c*>(dComIfGp_getPlayer(0));
    const bool inFreeState = link != nullptr && in_free_wolf_state(link);

    // Priority order: an earlier action keeps a shared button.
    u32 claimed = 0;
    update_chord(g_howl, inFreeState && can_howl(link), hold, trig, claimed);
    update_chord(g_bark, inFreeState, hold, trig, claimed);
    update_chord(g_growl, inFreeState, hold, trig, claimed);

    pad.mPressedButtonFlags &= ~claimed;
    pad.mButtonFlags &= ~claimed;
    return HOOK_CONTINUE;
}

bool try_start_howl(daAlink_c* link) {
    if (!g_howl.triggered || !can_howl(link)) {
        return false;
    }
    g_howl.triggered = false;

    const bool sitting = get_int_option(g_cvarStyle, kStyleSitting) == kStyleSitting;
    if (!link->procWolfHowlInit(sitting ? 1 : 0)) {
        return false;
    }
    g_modHowl = true;
    g_standingHold = StandingHold::None;
    return true;
}

void update_bark(daAlink_c* link) {
    if (!g_bark.held || !in_free_wolf_state(link)) {
        return;
    }
    const BarkSound& sound = get_table_option(g_cvarBarkSound, kBarkSounds);
    if (sound.loop) {
        link->voiceStartLevel(sound.id);
    } else if (g_bark.triggered) {
        link->voiceStart(sound.id);
    }
    g_bark.triggered = false;
}

bool growling(daAlink_c* link) {
    return g_growl.held && in_free_wolf_state(link);
}

// The lock-on proc plays the growl voice itself (via checkWolfAtnWait); everywhere else the mod
// does. Level sounds must be refreshed every tick.
void update_growl_voice(daAlink_c* link) {
    if (growling(link) && link->mProcID != daAlink_c::PROC_WOLF_ATN_AC_MOVE) {
        link->voiceStartLevel(Z2SE_WL_V_ROAR);
    }
}

// Runs where vanilla picks A/B/X/Y actions, after death, transform and slide checks.
HookAction on_next_action_from_button_pre(ModContext*, void* args, void* retval, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    if (link->mProcID != daAlink_c::PROC_WOLF_HOWL) {
        g_modHowl = false;
        g_standingHold = StandingHold::None;
    }

    update_bark(link);
    update_growl_voice(link);

    if (!try_start_howl(link)) {
        return HOOK_CONTINUE;
    }
    if (retval != nullptr) {
        *static_cast<BOOL*>(retval) = TRUE;
    }
    return HOOK_SKIP_ORIGINAL;
}

// Holds the howl while the chord button (consumed from the pad by the input layer) stays held.
HookAction on_wolf_howl_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    const bool holding = g_modHowl && g_howl.held && !link->checkEventRun();

    // Sitting: vanilla loops frames 38-56 while doButton() is held. mItemButton is rebuilt every
    // frame by setStickData, so this doesn't leak into other actions.
    if (link->mProcVar3.field_0x300e != 0) {
        if (holding) {
            link->mItemButton |= daAlink_c::BTN_A;
        }
        return HOOK_CONTINUE;
    }

    // Standing. Animations advance before the proc runs, so a rate set here moves next tick's frame.
    daPy_frameCtrl_c& frameCtrl = link->mUnderFrameCtrl[0];
    const f32 frame = frameCtrl.getFrame();
    if (!g_modHowl || link->checkEventRun()) {
        g_standingHold = StandingHold::None;
        return HOOK_CONTINUE;
    }

    f32 rate = frameCtrl.getRate();
    switch (g_standingHold) {
    case StandingHold::None:
        if (!holding || frame < kStandingSwayCenter) {
            return HOOK_CONTINUE;
        }
        g_standingHowlRate = rate;
        g_standingSwayTick = 0;
        g_standingHold = StandingHold::Sway;
        [[fallthrough]];
    case StandingHold::Sway:
        if (!holding) {
            g_standingHold = StandingHold::Release;
            break;
        }
        // Step to the next point on the sine exactly, so the frame never drifts out of range.
        rate = kStandingSwayAmplitude *
               (std::sin(kStandingSwayOmega * (g_standingSwayTick + 1)) -
                   std::sin(kStandingSwayOmega * g_standingSwayTick));
        g_standingSwayTick++;
        break;
    case StandingHold::Release:
        break;
    }

    if (g_standingHold == StandingHold::Release) {
        rate += (g_standingHowlRate - rate) * kStandingReleaseEase;
        if (std::fabs(g_standingHowlRate - rate) < 0.02f) {
            rate = g_standingHowlRate;
        }
        if (frame >= kStandingVanillaVoiceFrame) {
            // Vanilla voices from here and the howl plays out.
            frameCtrl.setRate(g_standingHowlRate);
            g_standingHold = StandingHold::None;
            return HOOK_CONTINUE;
        }
    }

    frameCtrl.setRate(rate != 0.0f ? rate : 0.001f);
    // The howl voice is a level sound: it must be refreshed every tick.
    link->voiceStartLevel(Z2SE_WL_V_HOWL);
    return HOOK_CONTINUE;
}

// Lock-on and idle: report "targeting an enemy" so the game shows its growl stance (WANM_ATN) and,
// in lock-on, plays its growl voice.
void on_wolf_atn_wait_post(ModContext*, void* args, void* retval, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    if (g_queryRealAtnWait || retval == nullptr || !growling(link)) {
        return;
    }
    if (link->mProcID == daAlink_c::PROC_WOLF_ATN_AC_MOVE ||
        link->mProcID == daAlink_c::PROC_WOLF_WAIT)
    {
        *static_cast<BOOL*>(retval) = TRUE;
    }
}

// Link's voice object has a single sound handle, and while moving the breathing one-shots keep it
// busy, so the growl (a level sound) never gets to play. Drop the breaths while growling.
HookAction on_link_voice_pre(ModContext*, void* args, void* retval, void*) {
    if (!g_growl.held) {
        return HOOK_CONTINUE;
    }
    switch (static_cast<u32>(mods::arg<JAISoundID>(args, 1))) {
    case Z2SE_WL_V_BREATH_WAIT:
    case Z2SE_WL_V_BREATH_TIRED:
    case Z2SE_WL_V_BREATH_SIT:
    case Z2SE_WL_V_BREATH_WALK:
    case Z2SE_WL_V_BREATH_RUN:
    case Z2SE_WL_V_BREATH_JUMP:
        if (retval != nullptr) {
            *static_cast<Z2SoundHandlePool**>(retval) = nullptr;
        }
        return HOOK_SKIP_ORIGINAL;
    default:
        return HOOK_CONTINUE;
    }
}

// The growl's volume follows the lock-on mark state: 1.0 with none, 0.3 holding L without a
// target, 0.5 targeting a non-enemy, 0.8 targeting an enemy. Play the mod's growl at the no-target
// volume unless an enemy is targeted. setAtnList recomputes the state every tick.
HookAction on_link_voice_level_pre(ModContext*, void* args, void*, void*) {
    Z2CreatureLink* z2Link = mods::arg<Z2CreatureLink*>(args, 0);
    if (g_growl.held && static_cast<u32>(mods::arg<JAISoundID>(args, 1)) == Z2SE_WL_V_ROAR &&
        (z2Link->mMarkState == 1 || z2Link->mMarkState == 2))
    {
        z2Link->setMarkState(0);
    }
    return HOOK_CONTINUE;
}

// Idle has no growl stance of its own; use the lock-on blend, which picks WANM_ATN when standing
// still. procWolfWait calls this every frame.
HookAction on_wolf_move_anime_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    f32& interpolation = mods::arg_ref<f32>(args, 1);

    const bool stance = link->mProcID == daAlink_c::PROC_WOLF_WAIT && growling(link);
    if (stance != g_growlStance && link->mProcID == daAlink_c::PROC_WOLF_WAIT) {
        interpolation = kGrowlBlendFrames;
    }
    g_growlStance = stance;
    if (!stance) {
        return HOOK_CONTINUE;
    }

    // Keep the bored-idle timer from starting a yawn mid-growl.
    link->initServiceWaitTime();
    link->setBlendWolfAtnMoveAnime(interpolation);
    return HOOK_SKIP_ORIGINAL;
}

void add_control(UiElementHandle pane, const UiControlDesc& control) {
    svc_ui->pane_add_control(mod_ctx, pane, &control, nullptr);
}

void add_dropdown(UiElementHandle pane, const char* label, ConfigVarHandle cvar,
    const char* const* options, size_t optionCount) {
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_DROPDOWN;
    control.label = label;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = cvar;
    control.options = options;
    control.option_count = optionCount;
    add_control(pane, control);
}

void add_chord_dropdowns(UiElementHandle pane, const ChordAction& action) {
    add_dropdown(pane, "Hold", action.modifier, kModifierLabels, std::size(kModifierLabels));
    add_dropdown(pane, "Then press", action.button, kButtonLabels, std::size(kButtonLabels));
}

constexpr const char* kCombatLimitation =
    "Only applies while L-targeting an enemy. Howling during a fight while not targeting is not "
    "preventable.";

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, panel, "Howl");
    add_chord_dropdowns(panel, g_howl);
    add_dropdown(panel, "Howl style", g_cvarStyle, kStyleLabels, std::size(kStyleLabels));
    svc_ui->pane_add_text(mod_ctx, panel, "Hold the button to keep howling.", nullptr);

    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Allow howling while targeting enemies";
    control.help_rml = kCombatLimitation;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarAllowCombatHowl;
    add_control(panel, control);
    // help_rml only renders in window tabs, so also show the limitation in the panel itself.
    svc_ui->pane_add_text(mod_ctx, panel, kCombatLimitation, nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Growl");
    add_chord_dropdowns(panel, g_growl);
    svc_ui->pane_add_text(mod_ctx, panel,
        "Growls while held. The crouched growl stance shows while standing still or holding L; "
        "while moving without L it's the sound only.",
        nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Bark");
    add_chord_dropdowns(panel, g_bark);
    add_dropdown(panel, "Sound", g_cvarBarkSound, kBarkSoundLabels.data(), kBarkSoundLabels.size());
    svc_ui->pane_add_text(mod_ctx, panel,
        "Every wolf voice in the game is listed so you can try them. \"(hold)\" sounds loop while "
        "the button is held.",
        nullptr);

    svc_ui->pane_add_text(mod_ctx, panel,
        "Avoid R+X, R+Y, R+D-pad Up/Down: Dusklight uses them. R+A also triggers Dusklight's "
        "Moon Jump if that's enabled.",
        nullptr);
    return MOD_OK;
}

ModResult register_int_var(
    const char* name, int64_t defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_INT;
    cvarDesc.default_int = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register option");
    }
    return MOD_OK;
}

ModResult register_bool_var(
    const char* name, bool defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_BOOL;
    cvarDesc.default_bool = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register option");
    }
    return MOD_OK;
}

ModResult register_options(ModError* error) {
    const struct {
        const char* name;
        int64_t defaultValue;
        ConfigVarHandle& handle;
    } intVars[] = {
        {"howlModifier", 0, g_howl.modifier},
        {"howlButton", 0, g_howl.button},
        {"howlStyle", kStyleSitting, g_cvarStyle},
        {"barkModifier", 0, g_bark.modifier},
        {"barkButton", kButtonDpadRight, g_bark.button},
        {"barkSound", 0, g_cvarBarkSound},
        {"growlModifier", 0, g_growl.modifier},
        {"growlButton", kButtonA, g_growl.button},
    };
    for (const auto& var : intVars) {
        const ModResult result = register_int_var(var.name, var.defaultValue, var.handle, error);
        if (result != MOD_OK) {
            return result;
        }
    }
    return register_bool_var("allowCombatHowl", false, g_cvarAllowCombatHowl, error);
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result = register_options(error);
    if (result != MOD_OK) {
        return result;
    }

    result = mods::hook::add_pre<GameExecute>(on_game_execute_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook fapGm_Execute");
    }
    result = mods::hook::add_pre<NextActionFromButton>(on_next_action_from_button_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook daAlink_c::checkNextActionFromButton");
    }
    result = mods::hook::add_pre<WolfHowl>(on_wolf_howl_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook daAlink_c::procWolfHowl");
    }
    result = mods::hook::add_post<WolfAtnWait>(on_wolf_atn_wait_post);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook daAlink_c::checkWolfAtnWait");
    }
    result = mods::hook::add_pre<WolfMoveAnime>(on_wolf_move_anime_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook daAlink_c::setBlendWolfMoveAnime");
    }
    result = mods::hook::add_pre<LinkVoice>(on_link_voice_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook Z2CreatureLink::startLinkVoice");
    }
    result = mods::hook::add_pre<LinkVoiceLevel>(on_link_voice_level_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook Z2CreatureLink::startLinkVoiceLevel");
    }

    UiModsPanelDesc panelDesc = UI_MODS_PANEL_DESC_INIT;
    panelDesc.build = build_panel;
    svc_ui->register_mods_panel(mod_ctx, &panelDesc);

    mods::log::info("howl_button initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    g_modHowl = false;
    g_growlStance = false;
    return MOD_OK;
}
}
