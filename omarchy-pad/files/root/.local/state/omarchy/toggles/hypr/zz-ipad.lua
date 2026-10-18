-- Omarchy Pad: overrides that must win over every other toggle.
--
-- Omarchy loads every file in this directory in name order, so "zz-" is last: these are the
-- settings the iPad cannot compromise on, whatever the theme or the user's toggles say.

hl.config({
  -- No cursor plane and no hardware cursor: the compositor draws the cursor into the
  -- frame omarchy-pad-bridge captures.
  cursor = {
    no_hardware_cursors = true,
  },

  misc = {
    disable_hyprland_logo = true,
    disable_splash_rendering = true,
    force_default_wallpaper = 0,
    -- The frame path tears down and rebuilds surfaces on session restarts; do not ask
    -- about it.
    disable_autoreload = true,
  },

  -- The guest has no usable X11: XWayland is off until a later phase turns it on.
  xwayland = {
    enabled = false,
  },
})

-- The display/input bridge drives the compositor through zwp_virtual_keyboard_v1. Hyprland
-- gates a virtual keyboard behind a permission rule (ecosystem:enforce_permissions, real by
-- default) and the default answer is to *ask*: in a headless session there is nobody to ask,
-- so the keyboard device is created, its keymap is parsed, and every key is then dropped in
-- silence -- which is exactly what happened (the pointer needs no permission and worked all
-- along). Allow this one client, by the name Hyprland derives for it
-- (misc:name_vk_after_proc makes it "hl-virtual-keyboard-<client>").
hl.permission({ binary = "hl-virtual-keyboard-.*", type = "keyboard", mode = "allow" })
