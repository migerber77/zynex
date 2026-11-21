/*
 * Zynex OS - Autovervollstaendigung fuer alle Programme (wird in desktop.c eingebunden).
 * Erkennt, wie der Satz weitergehen soll: Satzbausteine ("das Wet" -> "das Wetter wird ..."), Woerter und
 * Saetze, die der Nutzer schon geschrieben hat. Ergebnis: bis zu AC_MAX Vorschlaege; der erste erscheint als grauer
 * Text hinter der Eingabemarke (Tab uebernimmt), alle drehen sich im Rad (wie im Chat).
 */

#define AC_MAX 6
enum { AZ_NICHTS, AZ_TEXT, AZ_KP, AZ_TAB, AZ_PF, AZ_NZ, AZ_BD };
static int ac_cx, ac_cy, ac_ch, ac_hat;                 /* Eingabemarke des Texteditors (Bildschirm) */
static int ac_n, ac_ziel, ac_pos, ac_len;                /* Anzahl Vorschlaege, Ziel, Gueltigkeitsmarke (Cursor, Laenge) */
static char ac_c[AC_MAX][96]; static int ac_er[AC_MAX];  /* Vorschlag und Anzahl bereits getippter Zeichen, die er ersetzt */

static const char* const ac_phr_de[] = {
    "das Wetter wird morgen sonnig und warm.", "das Wetter wird am Wochenende wechselhaft.", "das Wetter wird besser.", "das Wetter ist heute sch\366n.",
    "das ist eine gute Idee.", "das ist kein Problem.", "das habe ich nicht verstanden.", "das passt mir gut.", "das machen wir gleich morgen.",
    "ich habe die Unterlagen erhalten und werde sie pr\374fen.", "ich melde mich bis Ende der Woche bei Ihnen.", "ich freue mich auf Ihre R\374ckmeldung.",
    "ich w\374nsche Ihnen einen sch\366nen Tag.", "ich bin ab Montag wieder erreichbar.", "ich bin gerne bereit, Ihnen weiterzuhelfen.",
    "ich danke Ihnen f\374r Ihre Anfrage.", "ich m\366chte mich f\374r die Verz\366gerung entschuldigen.", "ich werde mich darum k\374mmern.",
    "ich habe eine Frage zu Ihrem Angebot.", "ich schlage vor, dass wir einen Termin vereinbaren.",
    "wir freuen uns auf die Zusammenarbeit mit Ihnen.", "wir haben Ihre Bestellung erhalten.", "wir bitten um Verst\344ndnis.",
    "wir werden Sie so schnell wie m\366glich informieren.", "wir melden uns nach der Pr\374fung bei Ihnen.", "wir danken Ihnen f\374r Ihr Vertrauen.",
    "vielen Dank f\374r Ihre Nachricht.", "vielen Dank f\374r Ihre Unterst\374tzung.", "vielen Dank im Voraus.", "vielen Dank f\374r das nette Gespr\344ch.",
    "sehr geehrte Damen und Herren,", "sehr geehrter Herr", "sehr geehrte Frau", "sehr geehrtes Team,",
    "mit freundlichen Gr\374ssen", "mit besten Gr\374ssen", "mit herzlichen Gr\374ssen", "mit freundlichem Gruss",
    "liebe Gr\374sse", "liebe Kolleginnen und Kollegen,", "lieber Herr", "liebe Frau", "guten Tag,", "guten Morgen,", "guten Abend,",
    "anbei sende ich Ihnen die gew\374nschten Unterlagen.", "anbei finden Sie die Rechnung.", "anbei erhalten Sie unser Angebot.",
    "gerne stehe ich Ihnen f\374r R\374ckfragen zur Verf\374gung.", "gerne kommen wir auf Ihr Angebot zur\374ck.", "gerne sende ich Ihnen weitere Informationen.",
    "bitte teilen Sie mir mit, ob der Termin passt.", "bitte best\344tigen Sie den Erhalt dieser Nachricht.", "bitte entschuldigen Sie die Umst\344nde.",
    "bitte melden Sie sich bei Fragen.", "bitte senden Sie mir die Unterlagen zu.",
    "leider kann ich den Termin nicht wahrnehmen.", "leider ist das Produkt zurzeit nicht verf\374gbar.", "leider habe ich keine andere M\366glichkeit.",
    "in der Anlage finden Sie das Protokoll der Sitzung.", "in der Zwischenzeit w\374nsche ich Ihnen alles Gute.",
    "zun\344chst m\366chte ich mich bei Ihnen bedanken.", "zu Ihrer Information: die Frist endet am", "zusammenfassend l\344sst sich sagen, dass",
    "wie besprochen sende ich Ihnen die Zusammenfassung.", "wie telefonisch vereinbart, melde ich mich heute bei Ihnen.", "wie Sie wissen, ist das Projekt im Zeitplan.",
    "im Moment bin ich nicht im B\374ro.", "im Anhang finden Sie die aktuelle Version.", "im Namen des gesamten Teams danke ich Ihnen.",
    "die Sitzung findet am Montag um 10 Uhr statt.", "die Rechnung ist innert 30 Tagen zahlbar.", "die Lieferung erfolgt innerhalb von drei Werktagen.",
    "die Preise verstehen sich inklusive Mehrwertsteuer.", "die Zusammenarbeit hat uns sehr gefreut.", "die n\344chste Besprechung ist am",
    "der Termin ist best\344tigt.", "der Vorschlag gef\344llt mir sehr gut.", "der Bericht ist bis Freitag fertig.",
    "es freut mich, Ihnen mitteilen zu k\366nnen, dass", "es tut mir leid, dass", "es ist wichtig, dass wir rechtzeitig starten.",
    "heute ist ein sch\366ner Tag.", "heute gibt es viel zu tun.", "morgen fr\374h treffen wir uns im B\374ro.", "morgen wird es regnen.",
    "nach Absprache mit dem Team", "nach meiner Einsch\344tzung ist das m\366glich.", "unter Ber\374cksichtigung der aktuellen Lage",
    "aufgrund der hohen Nachfrage", "aufgrund eines technischen Problems", "auf Wunsch senden wir Ihnen gerne ein Muster.",
    "vielen Dank f\374r Ihr Interesse an unseren Produkten.", "falls Sie Fragen haben, melden Sie sich jederzeit.", "falls n\366tig, rufen Sie mich an.",
    "ausserdem m\366chte ich darauf hinweisen, dass", "ausserdem haben wir festgestellt, dass", "trotzdem sind wir zuversichtlich.",
    "hiermit best\344tige ich den Auftrag.", "hiermit k\374ndige ich den Vertrag zum n\344chstm\366glichen Zeitpunkt.", "hiermit bewerbe ich mich um die Stelle als",
    "meine Telefonnummer lautet", "meine Adresse lautet", "meine E-Mail-Adresse lautet", "Notizen zur Besprechung:", "Aufgaben f\374r diese Woche:",
    "Zusammenfassung:", "Ergebnis:", "Ziel des Projekts ist es,", "Ausgangslage:", "N\344chste Schritte:", "Fazit:", "Einleitung", "Inhaltsverzeichnis",
};
static const char* const ac_phr_en[] = {
    "the weather will be sunny and warm tomorrow.", "the weather is nice today.", "that is a good idea.", "that is not a problem.",
    "I have received the documents and will review them.", "I will get back to you by the end of the week.", "I look forward to your reply.",
    "I wish you a pleasant day.", "I would like to apologise for the delay.", "I will take care of it.", "I suggest that we schedule a meeting.",
    "we look forward to working with you.", "we have received your order.", "we will inform you as soon as possible.", "we thank you for your trust.",
    "thank you for your message.", "thank you for your support.", "thank you in advance.", "thank you for your inquiry.",
    "dear Sir or Madam,", "dear Mr", "dear Ms", "dear team,", "kind regards", "best regards", "yours sincerely", "good morning,", "good afternoon,",
    "please find attached the requested documents.", "please let me know if the date works for you.", "please confirm receipt of this message.",
    "please do not hesitate to contact me.", "unfortunately I cannot attend the meeting.", "as discussed, I am sending you the summary.",
    "the meeting will take place on Monday at 10 am.", "the invoice is payable within 30 days.", "the delivery takes three working days.",
    "in summary, we can say that", "next steps:", "summary:", "conclusion:", "introduction", "table of contents", "notes on the meeting:",
};
static const char* const ac_wort_de[] = {
    "Wetter", "Wochenende", "Woche", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag", "Sonntag", "Januar", "Februar", "M\344rz", "April", "Juni", "Juli", "August", "September", "Oktober", "November", "Dezember",
    "Rechnung", "Angebot", "Bestellung", "Lieferung", "Zahlung", "Kunde", "Kundin", "Auftrag", "Vertrag", "Termin", "Besprechung", "Sitzung", "Protokoll", "Bericht", "Projekt", "Ergebnis", "Zusammenfassung",
    "Unterlagen", "Information", "Informationen", "Anfrage", "Antwort", "Frage", "L\366sung", "Problem", "M\366glichkeit", "Vorschlag", "Entscheidung", "Verf\374gung", "Verf\374gbarkeit", "Verz\366gerung",
    "Gr\374sse", "Dank", "Entschuldigung", "Unterst\374tzung", "Zusammenarbeit", "Verst\344ndnis", "R\374ckmeldung", "R\374ckfrage", "Erfahrung", "Verbesserung",
    "Mitarbeiter", "Mitarbeiterin", "Kollege", "Kollegin", "Gesch\344ftsf\374hrer", "Abteilung", "Unternehmen", "Firma", "B\374ro", "Arbeit", "Stelle", "Bewerbung", "Lebenslauf",
    "Preis", "Preise", "Kosten", "Budget", "Rabatt", "Mehrwertsteuer", "Zahlungsfrist", "Konto", "Betrag", "Summe", "Tabelle", "Pr\344sentation", "Dokument", "Datei", "Ordner", "Programm", "Computer", "Bildschirm",
    "Nachricht", "Telefon", "Telefonnummer", "Adresse", "Strasse", "Postleitzahl", "Schweiz", "Deutschland", "\326sterreich", "Z\374rich", "Bern", "Basel", "Luzern",
    "heute", "morgen", "gestern", "\374bermorgen", "sofort", "sp\344ter", "gerne", "leider", "bitte", "danke", "vielleicht", "nat\374rlich", "selbstverst\344ndlich", "ausserdem", "deshalb", "trotzdem", "ebenfalls", "insbesondere", "beziehungsweise",
    "wichtig", "m\366glich", "notwendig", "erforderlich", "verf\374gbar", "zufrieden", "freundlich", "schnell", "rechtzeitig", "regelm\344ssig", "gemeinsam", "individuell", "zuverl\344ssig", "sonnig", "regnerisch", "bew\366lkt", "windig",
    "erstellen", "senden", "schicken", "pr\374fen", "best\344tigen", "vereinbaren", "besprechen", "informieren", "entscheiden", "unterst\374tzen", "verbessern", "bearbeiten", "abschliessen", "beantworten", "weiterleiten", "organisieren",
    "Geburtstag", "Weihnachten", "Urlaub", "Ferien", "Familie", "Freunde", "Einladung", "Gesundheit", "Gl\374ckwunsch", "Willkommen",
};

static char ac_lern[16][96]; static int ac_nlern, ac_lernt;
static int ac_klein(int c) { return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7) ? c + 32 : c; }
static int ac_pre(const char* p, const char* s, int n) {      /* beginnt p mit den ersten n Zeichen von s (Gross-/Kleinschreibung egal)? */
    for (int i = 0; i < n; i++) { if (!p[i]) return 0; if (ac_klein((unsigned char)p[i]) != ac_klein((unsigned char)s[i])) return 0; }
    return 1;
}
static void ac_laden(void) {
    if (ac_lernt) return; ac_lernt = 1;
    static char b[1700]; int r = zdb_get_text("desktop.satzhistorie", b, sizeof(b) - 1); if (r <= 0) return; b[r] = 0;
    int i = 0; while (b[i] && ac_nlern < 16) { int e = i; while (b[e] && b[e] != '\n') e++; if (e > i && e - i < 95) { for (int k = i; k < e; k++) ac_lern[ac_nlern][k - i] = b[k]; ac_lern[ac_nlern][e - i] = 0; ac_nlern++; } i = b[e] ? e + 1 : e; }
}
static void ac_lerne(const char* s) {                          /* fertigen Satz merken (mindestens drei Woerter) */
    ac_laden();
    int l = slen(s), w = 1; if (l < 12 || l > 90) return;
    for (int i = 0; i < l; i++) if (s[i] == ' ') w++;
    if (w < 3) return;
    for (int i = 0; i < ac_nlern; i++) if (gleich(ac_lern[i], s)) return;
    if (ac_nlern >= 16) { for (int i = 0; i + 1 < 16; i++) scopy(ac_lern[i], ac_lern[i + 1], 96); ac_nlern = 15; }
    scopy(ac_lern[ac_nlern++], s, 96);
    static char b[1700]; b[0] = 0; for (int i = 0; i < ac_nlern; i++) { cat(b, ac_lern[i], 1700); cat(b, "\n", 1700); }
    zdb_put_text("desktop.satzhistorie", b, slen(b));
}
static void ac_add(const char* t, int er, const char* chunk) {
    if (ac_n >= AC_MAX) return;
    char tmp[96]; scopy(tmp, t, 96);
    if (chunk[0] >= 'A' && chunk[0] <= 'Z' && tmp[0] >= 'a' && tmp[0] <= 'z') tmp[0] = (char)(tmp[0] - 32);     /* Satzanfang gross wie getippt */
    if (slen(tmp) <= er) return;
    for (int i = 0; i < ac_n; i++) if (gleich(ac_c[i], tmp)) return;
    scopy(ac_c[ac_n], tmp, 96); ac_er[ac_n] = er; ac_n++;
}
/* Vorschlaege fuer den Text vor der Eingabemarke (ctx) bauen; dok_text/dok_len: eigener Text fuer Woerter (darf 0 sein) */
static void ac_baue(const char* ctx, const char* dok_text, int dok_len) {
    ac_n = 0; ac_laden();
    int L = slen(ctx); if (L < 2) return;
    int starts[8], ns = 0, p = L;                                                  /* Wortanfaenge rueckwaerts (bis 7 Woerter, nicht ueber einen Satzanfang hinaus) */
    while (ns < 7) {
        int q = p; while (q > 0 && ctx[q - 1] == ' ') q--;
        while (q > 0 && ctx[q - 1] != ' ') q--;
        starts[ns++] = q; p = q;
        if (q == 0) break;
        if (q >= 2 && ctx[q - 1] == ' ' && (ctx[q - 2] == '.' || ctx[q - 2] == '!' || ctx[q - 2] == '?' || ctx[q - 2] == ':')) break;
    }
    const char* const* tab = lang == 0 ? ac_phr_de : ac_phr_en; int nt = lang == 0 ? (int)(sizeof(ac_phr_de) / sizeof(ac_phr_de[0])) : (int)(sizeof(ac_phr_en) / sizeof(ac_phr_en[0]));
    for (int si = ns - 1; si >= 0 && ac_n < AC_MAX; si--) {                         /* laengster zusammenhaengender Teil zuerst */
        const char* ch = ctx + starts[si]; int cl = L - starts[si]; if (cl < 2) continue;
        for (int i = 0; i < ac_nlern; i++) if (ac_pre(ac_lern[i], ch, cl)) ac_add(ac_lern[i], cl, ch);
        for (int i = 0; i < nt; i++) if (ac_pre(tab[i], ch, cl)) ac_add(tab[i], cl, ch);
    }
    if (ac_n >= 3) return;
    /* Wortvervollstaendigung: angefangenes Wort (mindestens 2 Buchstaben) */
    int ws = L; while (ws > 0 && ctx[ws - 1] != ' ') ws--;
    const char* w = ctx + ws; int wl = L - ws; if (wl < 2 || ctx[L - 1] == ' ') return;
    for (int i = 0; i < (int)(sizeof(ac_wort_de) / sizeof(ac_wort_de[0])) && ac_n < AC_MAX; i++) if (ac_pre(ac_wort_de[i], w, wl)) ac_add(ac_wort_de[i], wl, w);
    if (dok_text) {                                                                 /* Woerter aus dem eigenen Text */
        int i = 0; while (i < dok_len && ac_n < AC_MAX) {
            while (i < dok_len && !((dok_text[i] >= 'A' && dok_text[i] <= 'Z') || (dok_text[i] >= 'a' && dok_text[i] <= 'z') || (unsigned char)dok_text[i] >= 0xC0)) i++;
            int e = i; while (e < dok_len && ((dok_text[e] >= 'A' && dok_text[e] <= 'Z') || (dok_text[e] >= 'a' && dok_text[e] <= 'z') || (unsigned char)dok_text[e] >= 0xC0)) e++;
            if (e - i > wl && e - i < 40 && ac_pre(dok_text + i, w, wl) && !(dok_text + i >= ctx && dok_text + i < ctx + L)) { char t[48]; for (int k = i; k < e; k++) t[k - i] = dok_text[k]; t[e - i] = 0; ac_add(t, wl, w); }
            i = e + 1;
        }
    }
}
/* grauer Vorschlag hinter der Eingabemarke (nur wenn der Stand noch zum Vorschlag passt) */
static void ac_ghost_zeichnen(int x, int grund, int xmax) {
    if (ac_n <= 0 || ac_ziel != AZ_TEXT || ac_pos != dokcur || ac_len != doklen) return;
    const char* g = ac_c[0] + ac_er[0]; uint8_t st = dok_cst;
    for (; *g; g++) { int adv = st_adv((unsigned char)*g, st); if (x + adv > xmax) break; gfx_zeichen_stil(x + 2, grund - st_asc(st), (unsigned char)*g, 0x00A0A0A0u, st_font(st), st & 3); x += adv; }
}
