/* GUI subsystem: no console is attached, so this also tests whether wine's console handling was
 * what kept our earlier probe programs from running. Left 4 Dead is a GUI program too. */
#include <stdio.h>
#include <windows.h>
int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR cmd, int show) {
    (void) h; (void) p; (void) cmd; (void) show;
    FILE *f = fopen("C:\\linpad-probe.txt", "w");
    if (!f) return 2;
    fprintf(f, "gui probe ran in a %d-bit Windows process\n", (int)(sizeof(void *) * 8));
    fclose(f);
    return 0;
}
