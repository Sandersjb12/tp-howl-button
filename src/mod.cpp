// Howl Button: lets Wolf Link howl on demand.
//
// Reuses the vanilla howl action (procWolfHowlInit / procWolfHowl), which the game otherwise only
// enters from cutscenes. It provides the howl animation, the Z2SE_WL_V_HOWL voice, and a sustain
// loop (frames 38-56) while the action button is held.

#include "d/actor/d_a_alink.h"
#include "m_Do/m_Do_controller_pad.h"
#include "mods/service.hpp"
#include "mods/svc/config.h"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.hpp"
#include "mods/svc/ui.h"

#include <algorithm>
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

// Sitting is the howling-stone pose (WANM_HOWL_SIT): long voice, sustains while held.
// Standing (WANM_HOWL) is shorter and repeats while held.
constexpr const char* kStyleLabels[] = {"Sitting", "Standing"};
constexpr int64_t kStyleSitting = 0;

ConfigVarHandle g_cvarModifier = 0;
ConfigVarHandle g_cvarButton = 0;
ConfigVarHandle g_cvarStyle = 0;
ConfigVarHandle g_cvarAllowCombatHowl = 0;

// Set while a howl this mod started is playing, so the sustain hook leaves cutscene howls alone.
bool g_modHowl = false;

DEFINE_HOOK(&daAlink_c::checkNextActionFromButton, NextActionFromButton);
DEFINE_HOOK(&daAlink_c::procWolfHowl, WolfHowl);

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

template <size_t N>
u32 get_mask_option(ConfigVarHandle handle, const u32 (&masks)[N]) {
    const int64_t index = std::clamp<int64_t>(get_int_option(handle, 0), 0, N - 1);
    return masks[index];
}

bool chord_triggered() {
    const u32 modifier = get_mask_option(g_cvarModifier, kModifierMasks);
    const u32 button = get_mask_option(g_cvarButton, kButtonMasks);
    if ((mDoCPd_c::getHold(PAD_1) & modifier) != modifier) {
        return false;
    }
    return (mDoCPd_c::getTrig(PAD_1) & button) != 0;
}

// Only ordinary standing/moving states; anything else (dash, swim, dig, carry, cutscene
// states...) has exit cleanup that a forced state change would skip.
bool howl_allowed_in_proc(daAlink_c* link) {
    switch (link->mProcID) {
    case daAlink_c::PROC_WOLF_WAIT:
    case daAlink_c::PROC_WOLF_MOVE:
    case daAlink_c::PROC_WOLF_SERVICE_WAIT:
    case daAlink_c::PROC_WOLF_TIRED_WAIT:
        return true;
    case daAlink_c::PROC_WOLF_ATN_AC_MOVE:
        // checkWolfAtnWait() is the game's "L-targeting an enemy" test.
        return get_bool_option(g_cvarAllowCombatHowl, false) || !link->checkWolfAtnWait();
    default:
        return false;
    }
}

bool can_howl(daAlink_c* link) {
    return link->checkWolf() && !link->checkEventRun() && link->mLinkAcch.ChkGroundHit() &&
           !link->checkModeFlg(daAlink_c::MODE_PLAYER_FLY) && !link->checkWolfGrabAnime() &&
           howl_allowed_in_proc(link);
}

// Runs where vanilla picks A/B/X/Y actions, after death, transform and slide checks.
HookAction on_next_action_from_button_pre(ModContext*, void* args, void* retval, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    if (link->mProcID != daAlink_c::PROC_WOLF_HOWL) {
        g_modHowl = false;
    }

    if (!chord_triggered() || !can_howl(link)) {
        return HOOK_CONTINUE;
    }

    const bool sitting = get_int_option(g_cvarStyle, kStyleSitting) == kStyleSitting;
    if (!link->procWolfHowlInit(sitting ? 1 : 0)) {
        return HOOK_CONTINUE;
    }

    g_modHowl = true;
    if (retval != nullptr) {
        *static_cast<BOOL*>(retval) = TRUE;
    }
    return HOOK_SKIP_ORIGINAL;
}

// procWolfHowl sustains (or repeats) the howl while doButton() is held. Feed it the chord button.
// mItemButton is rebuilt every frame by setStickData, so this doesn't leak into other actions.
HookAction on_wolf_howl_pre(ModContext*, void* args, void*, void*) {
    daAlink_c* link = mods::arg<daAlink_c*>(args, 0);
    if (g_modHowl && !link->checkEventRun() &&
        (mDoCPd_c::getHold(PAD_1) & get_mask_option(g_cvarButton, kButtonMasks)) != 0)
    {
        link->mItemButton |= daAlink_c::BTN_A;
    }
    return HOOK_CONTINUE;
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

constexpr const char* kCombatLimitation =
    "Only applies while L-targeting an enemy. Howling during a fight while not targeting is not "
    "preventable.";

ModResult build_panel(ModContext*, UiElementHandle panel, void*, ModError*) {
    svc_ui->pane_add_section(mod_ctx, panel, "Controls");
    add_dropdown(panel, "Hold", g_cvarModifier, kModifierLabels, std::size(kModifierLabels));
    add_dropdown(panel, "Then press", g_cvarButton, kButtonLabels, std::size(kButtonLabels));
    add_dropdown(panel, "Howl style", g_cvarStyle, kStyleLabels, std::size(kStyleLabels));
    svc_ui->pane_add_text(mod_ctx, panel,
        "Hold the button to keep howling. Avoid R+X, R+Y, R+D-pad Up/Down: Dusklight uses them.",
        nullptr);

    svc_ui->pane_add_section(mod_ctx, panel, "Combat");
    UiControlDesc control = UI_CONTROL_DESC_INIT;
    control.kind = UI_CONTROL_TOGGLE;
    control.label = "Allow howling while targeting enemies";
    control.help_rml = kCombatLimitation;
    control.binding = UI_BINDING_CONFIG_VAR;
    control.config_var = g_cvarAllowCombatHowl;
    add_control(panel, control);
    // help_rml only renders in window tabs, so also show the limitation in the panel itself.
    svc_ui->pane_add_text(mod_ctx, panel, kCombatLimitation, nullptr);
    return MOD_OK;
}

ModResult register_int_var(
    const char* name, int64_t defaultValue, ConfigVarHandle& outHandle, ModError* error) {
    ConfigVarDesc cvarDesc = CONFIG_VAR_DESC_INIT;
    cvarDesc.name = name;
    cvarDesc.type = CONFIG_VAR_INT;
    cvarDesc.default_int = defaultValue;
    if (svc_config->register_var(mod_ctx, &cvarDesc, &outHandle) != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register howl option");
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
        return mods::set_error(error, MOD_ERROR, "failed to register howl option");
    }
    return MOD_OK;
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    ModResult result = register_int_var("howlModifier", 0, g_cvarModifier, error);
    if (result == MOD_OK) {
        result = register_int_var("howlButton", 0, g_cvarButton, error);
    }
    if (result == MOD_OK) {
        result = register_int_var("howlStyle", kStyleSitting, g_cvarStyle, error);
    }
    if (result == MOD_OK) {
        result = register_bool_var("allowCombatHowl", false, g_cvarAllowCombatHowl, error);
    }
    if (result != MOD_OK) {
        return result;
    }

    result = mods::hook::add_pre<NextActionFromButton>(on_next_action_from_button_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook daAlink_c::checkNextActionFromButton");
    }
    result = mods::hook::add_pre<WolfHowl>(on_wolf_howl_pre);
    if (result != MOD_OK) {
        return mods::set_error(error, result, "failed to hook daAlink_c::procWolfHowl");
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
    return MOD_OK;
}
}
