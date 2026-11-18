#include "zynex_model.h"

#include <string.h>

static const struct {
    const char *stichwort;
    const char *aktion;
} STICHWORTE[] = {
    /* Reihenfolge ist wichtig: die strengsten Regeln zuerst. */
    {"regel", "regelschicht.aendern"},
    {"loesch", "datei.loeschen"},
    {"l\xC3\xB6sch", "datei.loeschen"},
    {"termin", "termine.zeigen"},
    {"gr\xC3\xB6" "ss", "ansicht.vergroessern"},
    {"groess", "ansicht.vergroessern"},
    {"gr\xC3\xB6\xC3\x9F", "ansicht.vergroessern"},
    {"notiz", "notiz.anlegen"},
    {"sende", "nachricht.senden"},
    {"schick", "nachricht.senden"},
    {"server", "server.anfragen"},
};

int zx_model_intent(const char *text, char *aktion, size_t cap)
{
    char klein[256];
    size_t i, n;

    if (text == NULL || aktion == NULL || cap == 0)
        return 0;

    n = strlen(text);
    if (n >= sizeof(klein))
        n = sizeof(klein) - 1;
    for (i = 0; i < n; i++) {
        char c = text[i];
        klein[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    klein[n] = '\0';

    for (i = 0; i < sizeof(STICHWORTE) / sizeof(STICHWORTE[0]); i++) {
        if (strstr(klein, STICHWORTE[i].stichwort) != NULL) {
            size_t len = strlen(STICHWORTE[i].aktion);
            if (len >= cap)
                return 0;
            memcpy(aktion, STICHWORTE[i].aktion, len + 1);
            return 1;
        }
    }
    return 0;
}

const char *zx_model_system(void)
{
    return "Du bist Zynex, das freundliche Gespraechs-Betriebssystem. Antworte immer auf Deutsch, "
           "hoechstens zwei kurze Saetze, ohne Aufzaehlungen. Wenn der Nutzer eine Aktion will, "
           "schreibe am Ende eine eigene Zeile 'AKTION: name' mit genau einem Namen aus dieser Liste: "
           "ansicht.vergroessern, termine.zeigen, notiz.anlegen, datei.loeschen, nachricht.senden, "
           "einstellung.aendern, server.anfragen, regelschicht.aendern. Ohne Aktionswunsch schreibst du keine AKTION-Zeile.";
}

static int name_zeichen(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_';
}

int zx_model_split(char *reply, char *aktion, size_t cap)
{
    char *p, *q;
    size_t n = 0, len;
    int gefunden = 0;

    if (!reply || !aktion || cap == 0)
        return 0;
    aktion[0] = '\0';
    for (p = reply; *p; p++) {
        if (strncmp(p, "AKTION:", 7) == 0 || strncmp(p, "Aktion:", 7) == 0)
            break;
    }
    if (*p) {
        q = p + 7;
        while (*q == ' ' || *q == '\t')
            q++;
        while (name_zeichen(*q) && n + 1 < cap) {
            char z = *q++;
            aktion[n++] = (z >= 'A' && z <= 'Z') ? (char)(z + 32) : z;
        }
        aktion[n] = '\0';
        gefunden = n > 0;
        *p = '\0';
    }
    for (q = reply; *q; q++)
        if (*q == '\n' || *q == '\r' || *q == '\t')
            *q = ' ';
    len = strlen(reply);
    while (len > 0 && reply[len - 1] == ' ')
        reply[--len] = '\0';
    return gefunden;
}
