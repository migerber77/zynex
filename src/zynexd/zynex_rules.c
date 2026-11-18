#include "zynex_rules.h"

#include <stddef.h>

static const struct {
    const char *aktion;
    zx_level stufe;
} TABELLE[] = {
    /* frei */
    {"ansicht.vergroessern", ZX_FREI},
    {"termine.zeigen", ZX_FREI},
    {"notiz.anlegen", ZX_FREI},
    /* nachfragen */
    {"datei.loeschen", ZX_NACHFRAGEN},
    {"nachricht.senden", ZX_NACHFRAGEN},
    {"einstellung.aendern", ZX_NACHFRAGEN},
    {"server.anfragen", ZX_NACHFRAGEN},
    /* nie */
    {"regelschicht.aendern", ZX_NIE},
    {"daten.senden.ohne_freigabe", ZX_NIE},
    {"schutz.abschalten", ZX_NIE},
};

static char klein(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

static int gleich(const char *a, const char *b)
{
    while (*a && *b) {
        if (klein(*a) != klein(*b))
            return 0;
        a++;
        b++;
    }
    return *a == *b;
}

zx_level zx_rules_check(const char *aktion)
{
    size_t i;

    if (aktion == NULL || aktion[0] == '\0')
        return ZX_NIE;
    for (i = 0; i < sizeof(TABELLE) / sizeof(TABELLE[0]); i++) {
        if (gleich(aktion, TABELLE[i].aktion))
            return TABELLE[i].stufe;
    }
    return ZX_NACHFRAGEN;
}

const char *zx_level_name(zx_level stufe)
{
    switch (stufe) {
    case ZX_FREI:
        return "frei";
    case ZX_NACHFRAGEN:
        return "nachfragen";
    default:
        return "nie";
    }
}
