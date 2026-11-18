#include "layouts.h"
int kbd_find(const char* code) {
    for (int i = 0; i < KBD_LAYOUTS; i++) {
        const char* a = kbd_layouts[i].code; const char* b = code; int j = 0;
        while (a[j] && a[j] == b[j]) j++;
        if (!a[j] && (b[j] == 0 || b[j] == ' ' || b[j] == '\n')) return i;
    }
    return -1;
}
char kbd_char(int id, uint8_t k, int shift, int altgr) {
    if (id < 0 || id >= KBD_LAYOUTS) id = 0;
    int i = k == 0x64 ? 53 : (k >= 4 && k <= 0x38) ? k - 4 : -1;
    if (i < 0) return 0;
    const uint8_t (*t)[54] = kbd_layouts[id].t;
    uint8_t c = 0;
    if (altgr) { c = shift ? t[3][i] : t[2][i]; if (!c && shift) c = t[2][i]; return (char)c; }
    return (char)(shift ? t[1][i] : t[0][i]);
}
