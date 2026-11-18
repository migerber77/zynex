#include <stdio.h>
#include <string.h>

#include "zynex_model.h"
#include "zynex_rules.h"
#include "zynex_mem.h"
#include "zynex_face.h"
#include "zynex_ui.h"
#include <stdlib.h>
#include "zynex_visemes.h"

static int fehler;

#define PRUEFE(bed)                                                      \
    do {                                                                 \
        if (!(bed)) {                                                    \
            printf("FEHLER %s:%d: %s\n", __FILE__, __LINE__, #bed);       \
            fehler++;                                                    \
        }                                                                \
    } while (0)

static void test_regeln(void)
{
    PRUEFE(zx_rules_check("termine.zeigen") == ZX_FREI);
    PRUEFE(zx_rules_check("ANSICHT.Vergroessern") == ZX_FREI);
    PRUEFE(zx_rules_check("datei.loeschen") == ZX_NACHFRAGEN);
    PRUEFE(zx_rules_check("nachricht.senden") == ZX_NACHFRAGEN);
    PRUEFE(zx_rules_check("regelschicht.aendern") == ZX_NIE);
    PRUEFE(zx_rules_check("daten.senden.ohne_freigabe") == ZX_NIE);
    PRUEFE(zx_rules_check("schutz.abschalten") == ZX_NIE);
    /* Unbekanntes wird nie still erlaubt */
    PRUEFE(zx_rules_check("etwas.unbekanntes") == ZX_NACHFRAGEN);
    PRUEFE(zx_rules_check("termine.zeigen.extra") == ZX_NACHFRAGEN);
    PRUEFE(zx_rules_check("") == ZX_NIE);
    PRUEFE(zx_rules_check(NULL) == ZX_NIE);
    PRUEFE(strcmp(zx_level_name(ZX_FREI), "frei") == 0);
    PRUEFE(strcmp(zx_level_name(ZX_NACHFRAGEN), "nachfragen") == 0);
    PRUEFE(strcmp(zx_level_name(ZX_NIE), "nie") == 0);
}

static void test_mundformen(void)
{
    zx_viseme v[64];
    uint32_t total = 0;
    size_t n, i;

    n = zx_visemes_build("Hallo", v, 64, &total);
    PRUEFE(n == 5);
    PRUEFE(total == 70 + 95 + 70 + 70 + 95);
    PRUEFE(v[1].open_pm == 1000); /* a */
    PRUEFE(v[4].open_pm == 850);  /* o */
    for (i = 1; i < n; i++)
        PRUEFE(v[i].t_ms > v[i - 1].t_ms);

    /* Satzzeichen schliessen den Mund und dauern laenger */
    n = zx_visemes_build("a.", v, 64, &total);
    PRUEFE(n == 2);
    PRUEFE(v[1].open_pm == 0);
    PRUEFE(total == 95 + 380);

    /* Umlaute (UTF-8) und Grossbuchstaben */
    n = zx_visemes_build("\xC3\xB6\xC3\x96", v, 64, &total);
    PRUEFE(n == 2);
    PRUEFE(v[0].open_pm == 600 && v[1].open_pm == 600);
    PRUEFE(v[1].byte_index == 2);

    /* Grenzfaelle */
    PRUEFE(zx_visemes_build(NULL, v, 64, &total) == 0 && total == 0);
    PRUEFE(zx_visemes_build("", v, 64, &total) == 0 && total == 0);

    /* Platz ist knapp: es wird nichts ueberschrieben, die Dauer stimmt trotzdem */
    n = zx_visemes_build("aaaa", v, 2, &total);
    PRUEFE(n == 2);
    PRUEFE(total == 4 * 95);

    /* Abgebrochenes UTF-8 darf nicht abstuerzen */
    n = zx_visemes_build("a\xC3", v, 64, &total);
    PRUEFE(n == 1);
    n = zx_visemes_build("a\xE2\x82", v, 64, &total);
    PRUEFE(n == 1);
}

static void test_absicht(void)
{
    char a[64];

    PRUEFE(zx_model_intent("Zeig mir meine Termine", a, sizeof(a)) && strcmp(a, "termine.zeigen") == 0);
    PRUEFE(zx_model_intent("l\xC3\xB6sche die Datei", a, sizeof(a)) && strcmp(a, "datei.loeschen") == 0);
    PRUEFE(zx_model_intent("alles gr\xC3\xB6sser bitte", a, sizeof(a)) && strcmp(a, "ansicht.vergroessern") == 0);
    PRUEFE(zx_model_intent("aendere die Regeln", a, sizeof(a)) && strcmp(a, "regelschicht.aendern") == 0);
    PRUEFE(!zx_model_intent("Wie geht es dir?", a, sizeof(a)));
    PRUEFE(!zx_model_intent(NULL, a, sizeof(a)));
    PRUEFE(!zx_model_intent("termin", a, 3)); /* Puffer zu klein */
}

static void test_antwort_zerlegen(void)
{
    char r[200], a[64];

    strcpy(r, "Ich oeffne deine Termine.\nAKTION: termine.zeigen");
    PRUEFE(zx_model_split(r, a, sizeof(a)) == 1);
    PRUEFE(strcmp(a, "termine.zeigen") == 0);
    PRUEFE(strcmp(r, "Ich oeffne deine Termine.") == 0);

    strcpy(r, "Alles gut.\nAktion: Datei.Loeschen\n");
    PRUEFE(zx_model_split(r, a, sizeof(a)) == 1);
    PRUEFE(strcmp(a, "datei.loeschen") == 0);

    strcpy(r, "Mir geht es gut,\ndanke der Nachfrage.  ");
    PRUEFE(zx_model_split(r, a, sizeof(a)) == 0);
    PRUEFE(a[0] == '\0');
    PRUEFE(strcmp(r, "Mir geht es gut, danke der Nachfrage.") == 0);

    strcpy(r, "Text AKTION: ");
    PRUEFE(zx_model_split(r, a, sizeof(a)) == 0);
    PRUEFE(strcmp(r, "Text") == 0);
    PRUEFE(zx_model_system()[0] != '\0');
}

static void test_gesicht(void)
{
    static zx_face f;
    uint32_t t;
    float maxo = 0, minblink = 1;
    int sprach_ende = 0;
    zx_canvas c = {NULL, 640, 480};
    static zx_ui u;

    zx_face_init(&f, 0, 7);
    zx_face_update(&f, 0);
    PRUEFE(!zx_face_is_speaking(&f));
    zx_face_speak(&f, "Hallo Welt, aaa");
    PRUEFE(zx_face_is_speaking(&f));
    for (t = 0; t < 12000; t += 16) {
        zx_face_update(&f, t);
        if (f.mouth_o > maxo) maxo = f.mouth_o;
        if (zx_face_blink_open(&f) < minblink) minblink = zx_face_blink_open(&f);
        if (!zx_face_is_speaking(&f)) sprach_ende = 1;
    }
    PRUEFE(maxo > 0.6f);          /* Mund oeffnet sich beim Sprechen */
    PRUEFE(sprach_ende);          /* Sprechen endet */
    PRUEFE(f.mouth_o < 0.05f);    /* Mund geschlossen danach */
    PRUEFE(minblink < 0.2f);      /* es wird geblinzelt */

    /* Zeichnen: Augenfarbe muss irgendwo vorkommen, nichts ausserhalb schreiben */
    c.px = calloc((size_t)c.w * (size_t)c.h, 4);
    zx_ui_init(&u, 0);
    zx_ui_say(&u, 1, "Test \xC3\xA4\xC3\xB6\xC3\xBC");
    zx_ui_draw(&u, &c);
    {
        int i, cyan = 0;
        for (i = 0; i < c.w * c.h; i++)
            if (c.px[i] == 0x4fd8ffu) cyan++;
        PRUEFE(cyan > 500);
    }
    free(c.px);
}

static void test_gedaechtnis(void)
{
    zx_mem m;
    char out[400], sys[800], vl[2000];
    zx_mem_init(&m, NULL);
    PRUEFE(zx_mem_befehl(&m, "Merke dir: ich heisse Michel.", out, sizeof(out)) == 1);
    PRUEFE(m.n == 1 && strcmp(m.zettel[0], "ich heisse Michel") == 0);
    PRUEFE(zx_mem_befehl(&m, "wie spaet ist es", out, sizeof(out)) == 0);
    PRUEFE(zx_mem_befehl(&m, "Was weisst du ueber mich?", out, sizeof(out)) == 1 && strstr(out, "Michel"));
    zx_mem_system(&m, "Basis.", sys, sizeof(sys));
    PRUEFE(strstr(sys, "Basis.") && strstr(sys, "ich heisse Michel"));
    zx_mem_verlauf_add(&m, "a", "b");
    zx_mem_verlauf(&m, vl, sizeof(vl));
    PRUEFE(strstr(vl, "<|im_start|>user\na<|im_end|>") != NULL);
    PRUEFE(zx_mem_befehl(&m, "vergiss Michel", out, sizeof(out)) == 1 && m.n == 0);
    zx_mem_befehl(&m, "merke dir x", out, sizeof(out));
    PRUEFE(zx_mem_befehl(&m, "vergiss alles", out, sizeof(out)) == 1 && m.n == 0 && m.nv == 0);
}

int main(void)
{
    test_gedaechtnis();
    test_regeln();
    test_mundformen();
    test_absicht();
    test_antwort_zerlegen();
    test_gesicht();
    if (fehler) {
        printf("%d Fehler\n", fehler);
        return 1;
    }
    puts("alle Tests bestanden");
    return 0;
}
