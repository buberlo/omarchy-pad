LinPad 1.4.0 build 4446 fixes Unity SDL startup rollback by adding an idle netlink uevent monitor endpoint. On the physical iPad, native JIT uses a 512 MiB arena, boot succeeds, all 35 native regression checks pass, and the exact game SDL initialization retains one display.

GPU presentation on this build, Hollow Knight rendering/playability and steady-state performance are still pending. Libudev BPF filtering and actual hotplug delivery remain unsupported; this checkpoint does not claim them.

Source: `72e1eef8b3125b39e7c852abf2d16bf7a1f82029`. Signed IPA and matching dSYM ZIP passed CRC and content verification. Both approved memory entitlements remain enabled. Installed FEX/Mesa/EGL/game data are separate from the IPA.
