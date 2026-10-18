-- Omarchy Pad: the iPad is the only output.
--
-- This file is loaded after Omarchy's own ~/.config/hypr/monitors.lua template, so the
-- wildcard rule there is replaced by one explicit headless output. omarchy-pad-bridge
-- reconfigures it at runtime (`hyprctl keyword monitor HEADLESS-1, WxH@60, 0x0, 1`)
-- whenever the native window changes size, so the boot-time mode here is only a default.

hl.monitor({ output = "HEADLESS-1", mode = "1280x800@60", position = "0x0", scale = 1 })

-- The captured frame is scaled by the iPad window, not by GTK.
hl.env("GDK_SCALE", "1")
