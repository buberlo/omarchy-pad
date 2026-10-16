        if (sys == 0xd51b4400) {                      // MSR FPCR, Xt
            int h = read_greg(t, HOT, rt, HR_T0, false);
            // Match gadget_msr_fpcr: retain the guest's low-32-bit shadow,
            // but never enable host FP exceptions or unadvertised FP modes.
            put(t, HOT, e_str_w(h, HR_CTX, CTX_OFF(cpu.fpcr)));
            // Keep this mask in sync with jit.c and FPCR_GUEST_MASK_HI.
            mov_imm64(t, HOT, HR_T1, 0x07c80000u);
            put(t, HOT, 0x8a000000u | (HR_T1 << 16) | (h << 5) | HR_T0); // AND X
            put(t, HOT, e_str_w(HR_T0, HR_CTX, CTX_OFF(guest_fpcr_masked)));
            put(t, HOT, e_msr_fpcr(HR_T0));
            return 1;
        }
