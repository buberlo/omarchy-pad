-- Omarchy Pad: there are no libinput devices in the guest.
--
-- omarchy-pad-bridge injects absolute pointer motion and keyboard events over
-- zwlr_virtual_pointer_v1 / zwp_virtual_keyboard_v1, so the settings here only describe the
-- iPad's own keyboard and pointer to Hyprland: a German layout by default, flat
-- acceleration, and pointer movement that follows the injected absolute positions.

hl.config({
  input = {
    kb_layout = "de",
    follow_mouse = 1,
    sensitivity = 0,
    accel_profile = "flat",
    numlock_by_default = false,
    touchpad = {
      natural_scroll = true,
      clickfinger_behavior = true,
    },
  },
})
