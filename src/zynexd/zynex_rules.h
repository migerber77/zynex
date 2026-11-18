/* Zynex Regelschicht: entscheidet, was Zynex selbst tun darf.
 * Die Tabelle ist fest eingebaut und wird zur Laufzeit nicht veraendert. */
#ifndef ZYNEX_RULES_H
#define ZYNEX_RULES_H

typedef enum {
    ZX_FREI = 0,        /* wird sofort ausgefuehrt */
    ZX_NACHFRAGEN = 1,  /* braucht ein Ja des Nutzers */
    ZX_NIE = 2          /* wird nie ausgefuehrt */
} zx_level;

/* Stufe einer Aktion, z.B. "datei.loeschen". Gross-/Kleinschreibung egal.
 * Unbekannte Aktionen: ZX_NACHFRAGEN. NULL oder leer: ZX_NIE. */
zx_level zx_rules_check(const char *aktion);

const char *zx_level_name(zx_level stufe);

#endif
