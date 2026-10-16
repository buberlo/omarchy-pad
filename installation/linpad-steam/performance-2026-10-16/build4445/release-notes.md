LinPad 1.5.0 build 4445 fixes unsafe reads from PROT_NONE guest memory and gives the vDSO/vvar their explicit permissions. The physical iPad boots to the desktop with native JIT and a 512 MiB arena; protected-memory probes and 24 exact-address SIGSEGV cases pass.

With the separately installed patched FEX runtime and Mesa 26 overlay, a 640×360 Vulkan cube completes 1200 frames with exit 0 in 59.09 seconds including startup. Two physical-device captures show different cube orientations. This establishes presentation and motion, with no steady-state FPS claim.

Hollow Knight is not yet playable. Its captured Unity/Vulkan startup reports missing `libGL.so.1` during main-window creation; dependency repair remains pending. The patched FEX and Mesa guest additions are not embedded in the IPA.

Signed IPA and matching dSYM ZIP pass full CRC checks; both approved memory entitlements remain enabled. Source commit: 08528edebc03aa4f07f6b49fdae5d8c08cd2bcd0. Build 4444 remains rejected and unpublished; build 4443 is preserved.
