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
