/* Faktentabelle: Hauptstaedte. Erkennt "Hauptstadt von Italien", "capital of Italy", "capitale de la France", "Rom ist Hauptstadt von welchem Land" usw. */
#include "fakten.h"

/* Land (Alternativen mit '|', klein, ohne Umlaute: deutsch|englisch|...), Hauptstadt deutsch, Hauptstadt englisch */
static const struct { const char* land; const char* hs_de; const char* hs_en; } T[] = {
    { "deutschland|germany", "Berlin", "Berlin" }, { "oesterreich|austria", "Wien", "Vienna" }, { "schweiz|switzerland|suisse|svizzera|suiza", "Bern", "Bern" },
    { "liechtenstein", "Vaduz", "Vaduz" }, { "italien|italy|italie|italia", "Rom", "Rome" }, { "frankreich|france|francia", "Paris", "Paris" },
    { "spanien|spain|espagne|spagna|espana", "Madrid", "Madrid" }, { "portugal", "Lissabon", "Lisbon" },
    { "grossbritannien|vereinigtes koenigreich|united kingdom|england|great britain|britain|uk", "London", "London" },
    { "irland|ireland|irlande|irlanda", "Dublin", "Dublin" }, { "niederlande|netherlands|holland|pays-bas|paesi bassi|paises bajos", "Amsterdam", "Amsterdam" },
    { "belgien|belgium|belgique|belgio|belgica", "Bruessel", "Brussels" }, { "luxemburg|luxembourg|lussemburgo", "Luxemburg", "Luxembourg" },
    { "daenemark|denmark|danemark|danimarca|dinamarca", "Kopenhagen", "Copenhagen" }, { "schweden|sweden|suede|svezia|suecia", "Stockholm", "Stockholm" },
    { "norwegen|norway|norvege|norvegia|noruega", "Oslo", "Oslo" }, { "finnland|finland|finlande|finlandia", "Helsinki", "Helsinki" },
    { "island|iceland|islande|islanda|islandia", "Reykjavik", "Reykjavik" }, { "polen|poland|pologne|polonia", "Warschau", "Warsaw" },
    { "tschechien|czechia|czech republic|tchequie|cechia|chequia", "Prag", "Prague" }, { "slowakei|slovakia|slovaquie|slovacchia|eslovaquia", "Bratislava", "Bratislava" },
    { "ungarn|hungary|hongrie|ungheria|hungria", "Budapest", "Budapest" }, { "rumaenien|romania|roumanie|rumania", "Bukarest", "Bucharest" },
    { "bulgarien|bulgaria|bulgarie|bulgaria", "Sofia", "Sofia" }, { "griechenland|greece|grece|grecia", "Athen", "Athens" },
    { "tuerkei|turkey|turquie|turchia|turquia", "Ankara", "Ankara" }, { "kroatien|croatia|croatie|croazia|croacia", "Zagreb", "Zagreb" },
    { "slowenien|slovenia|slovenie|slovenia|eslovenia", "Ljubljana", "Ljubljana" }, { "serbien|serbia|serbie|serbia", "Belgrad", "Belgrade" },
    { "bosnien|bosnia|bosnie|bosnia", "Sarajevo", "Sarajevo" }, { "albanien|albania|albanie|albania", "Tirana", "Tirana" },
    { "nordmazedonien|north macedonia|macedoine du nord|macedonia del nord", "Skopje", "Skopje" }, { "montenegro", "Podgorica", "Podgorica" },
    { "kosovo", "Pristina", "Pristina" }, { "ukraine|ucraina|ucrania", "Kiew", "Kyiv" }, { "russland|russia|russie|rusia", "Moskau", "Moscow" },
    { "weissrussland|belarus|bielorussia|bielorrusia", "Minsk", "Minsk" }, { "litauen|lithuania|lituanie|lituania", "Vilnius", "Vilnius" },
    { "lettland|latvia|lettonie|lettonia|letonia", "Riga", "Riga" }, { "estland|estonia|estonie|estonia", "Tallinn", "Tallinn" },
    { "moldawien|moldova|moldavie|moldavia", "Chisinau", "Chisinau" }, { "malta", "Valletta", "Valletta" }, { "zypern|cyprus|chypre|cipro|chipre", "Nikosia", "Nicosia" },
    { "usa|vereinigte staaten|united states|etats-unis|stati uniti|estados unidos|amerika|america", "Washington D.C.", "Washington, D.C." },
    { "kanada|canada", "Ottawa", "Ottawa" }, { "mexiko|mexico|mexique|messico", "Mexiko-Stadt", "Mexico City" },
    { "brasilien|brazil|bresil|brasile|brasil", "Brasilia", "Brasilia" }, { "argentinien|argentina|argentine", "Buenos Aires", "Buenos Aires" },
    { "chile|cile", "Santiago", "Santiago" }, { "peru|perou", "Lima", "Lima" }, { "kolumbien|colombia|colombie", "Bogota", "Bogota" },
    { "venezuela", "Caracas", "Caracas" }, { "kuba|cuba", "Havanna", "Havana" }, { "ecuador|equateur", "Quito", "Quito" },
    { "uruguay", "Montevideo", "Montevideo" }, { "paraguay", "Asuncion", "Asuncion" },
    { "china|chine|cina", "Peking", "Beijing" }, { "japan|japon|giappone", "Tokio", "Tokyo" }, { "suedkorea|south korea|coree du sud|corea del sud", "Seoul", "Seoul" },
    { "nordkorea|north korea|coree du nord|corea del nord", "Pjoengjang", "Pyongyang" }, { "indien|india|inde|inda", "Neu-Delhi", "New Delhi" },
    { "pakistan", "Islamabad", "Islamabad" }, { "indonesien|indonesia|indonesie", "Jakarta", "Jakarta" }, { "thailand|thailande|tailandia", "Bangkok", "Bangkok" },
    { "vietnam|viet nam", "Hanoi", "Hanoi" }, { "philippinen|philippines|filippine|filipinas", "Manila", "Manila" },
    { "australien|australia|australie", "Canberra", "Canberra" }, { "neuseeland|new zealand|nouvelle-zelande|nuova zelanda|nueva zelanda", "Wellington", "Wellington" },
    { "aegypten|egypt|egypte|egitto|egipto", "Kairo", "Cairo" }, { "suedafrika|south africa|afrique du sud|sudafrica", "Pretoria", "Pretoria" },
    { "marokko|morocco|maroc|marocco|marruecos", "Rabat", "Rabat" }, { "nigeria", "Abuja", "Abuja" }, { "kenia|kenya", "Nairobi", "Nairobi" },
    { "israel", "Jerusalem", "Jerusalem" }, { "saudi-arabien|saudi arabia|arabie saoudite|arabia saudita", "Riad", "Riyadh" },
    { "iran", "Teheran", "Tehran" }, { "irak|iraq", "Bagdad", "Baghdad" }, { "syrien|syria|syrie|siria", "Damaskus", "Damascus" },
    { "aethiopien|ethiopia|ethiopie|etiopia", "Addis Abeba", "Addis Ababa" }, { "algerien|algeria|algerie|argelia", "Algier", "Algiers" },
    { "tunesien|tunisia|tunisie|tunez", "Tunis", "Tunis" }, { "libanon|lebanon|liban|libano", "Beirut", "Beirut" },
    { "jordanien|jordan|jordanie|giordania|jordania", "Amman", "Amman" }, { "afghanistan", "Kabul", "Kabul" },
    { "kasachstan|kazakhstan|kazakistan|kazajistan", "Astana", "Astana" }, { "georgien|georgia|georgie", "Tiflis", "Tbilisi" },
    { "armenien|armenia|armenie", "Eriwan", "Yerevan" }, { "aserbaidschan|azerbaijan|azerbaidjan|azerbaigian|azerbaiyan", "Baku", "Baku" },
    { "nepal", "Kathmandu", "Kathmandu" }, { "singapur|singapore|singapour|singapore", "Singapur", "Singapore" },
    { 0, 0, 0 }
};

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int wort_c(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; }

/* Kleinbuchstaben, ASCII: Umlaute und Akzente (UTF-8 und Latin-1) werden vereinfacht */
static void falte(const char* s, char* o, int max) {
    int n = 0;
    for (int i = 0; s[i] && n < max - 3; i++) {
        unsigned char c = (unsigned char)s[i], d = (unsigned char)s[i + 1]; const char* r = 0; char one[2] = { 0, 0 };
        if (c == 0xC3 && d) { i++; c = (unsigned char)(d + 0x40); }
        if (c >= 'A' && c <= 'Z') { one[0] = (char)(c + 32); r = one; }
        else if (c < 128) { one[0] = (char)c; r = one; }
        else if (c == 0xE4 || c == 0xC4) r = "ae"; else if (c == 0xF6 || c == 0xD6) r = "oe"; else if (c == 0xFC || c == 0xDC) r = "ue"; else if (c == 0xDF) r = "ss";
        else if ((c >= 0xE0 && c <= 0xE5) || (c >= 0xC0 && c <= 0xC5)) r = "a"; else if ((c >= 0xE8 && c <= 0xEB) || (c >= 0xC8 && c <= 0xCB)) r = "e";
        else if ((c >= 0xEC && c <= 0xEF) || (c >= 0xCC && c <= 0xCF)) r = "i"; else if ((c >= 0xF2 && c <= 0xF5) || (c >= 0xD2 && c <= 0xD5)) r = "o";
        else if ((c >= 0xF9 && c <= 0xFA) || (c >= 0xD9 && c <= 0xDA)) r = "u"; else if (c == 0xE7 || c == 0xC7) r = "c"; else if (c == 0xF1 || c == 0xD1) r = "n";
        else r = " ";
        while (*r) o[n++] = *r++;
    }
    o[n] = 0;
}
static int hat_wort(const char* s, const char* w, int wl) {        /* ganzes Wort/Wortgruppe */
    for (int i = 0; s[i]; i++) {
        if (i > 0 && wort_c(s[i - 1])) continue;
        int k = 0; while (k < wl && s[i + k] == w[k]) k++;
        if (k == wl && !wort_c(s[i + k])) return 1;
    }
    return 0;
}
static int hat_alt(const char* s, const char* alt) {                /* eine der mit '|' getrennten Alternativen */
    int i = 0;
    while (alt[i]) { int e = i; while (alt[e] && alt[e] != '|') e++; if (hat_wort(s, alt + i, e - i)) return e - i; i = alt[e] ? e + 1 : e; }
    return 0;
}
static int hat_wort_str(const char* s, const char* w) { return hat_wort(s, w, slen(w)); }
static int hat_praefix(const char* s, const char* p) {                /* Wort, das mit p beginnt ("hauptst" findet auch den Tippfehler "Hauptstatt") */
    int pl = slen(p);
    for (int i = 0; s[i]; i++) { if (i > 0 && wort_c(s[i - 1])) continue; int k = 0; while (k < pl && s[i + k] == p[k]) k++; if (k == pl) return 1; }
    return 0;
}
static const char* anzeige(const char* n) {                           /* Umlaute fuer die Anzeige (die Tabelle selbst ist ASCII) */
    static const char* const U[][2] = { { "Oesterreich", "\xC3\x96sterreich" }, { "Daenemark", "D\xC3\xA4nemark" }, { "Rumaenien", "Rum\xC3\xA4nien" }, { "Tuerkei", "T\xC3\xBCrkei" },
        { "Aegypten", "\xC3\x84gypten" }, { "Suedkorea", "S\xC3\xBC" "dkorea" }, { "Suedafrika", "S\xC3\xBC" "dafrika" }, { "Aethiopien", "\xC3\x84thiopien" }, { "Bruessel", "Br\xC3\xBCssel" },
        { "Pjoengjang", "Pj\xC3\xB6ngjang" }, { 0, 0 } };
    for (int i = 0; U[i][0]; i++) { int k = 0; while (U[i][0][k] && U[i][0][k] == n[k]) k++; if (!U[i][0][k] && !n[k]) return U[i][1]; }
    return n;
}
static void cat(char* d, const char* s, int max) { int n = slen(d); int i = 0; while (s[i] && n + i < max - 1) { d[n + i] = s[i]; i++; } d[n + i] = 0; }
static void erstes(const char* alt, char* o) { int i = 0; while (alt[i] && alt[i] != '|') { o[i] = alt[i]; i++; } o[i] = 0; if (o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32); }
static void zweites(const char* alt, char* o) { int i = 0; while (alt[i] && alt[i] != '|') i++; if (alt[i]) i++; int k = 0; while (alt[i] && alt[i] != '|') o[k++] = alt[i++]; o[k] = 0; if (!k) erstes(alt, o); else if (o[0] >= 'a' && o[0] <= 'z') o[0] = (char)(o[0] - 32); }

int fakt_antwort(const char* frage, int lang, char* out, int max) {
    static char s[200];
    falte(frage, s, sizeof(s));
    if (!(hat_praefix(s, "hauptst") || hat_praefix(s, "capital"))) return 0;
    int best = -1, bl = 0;
    for (int i = 0; T[i].land; i++) { int l = hat_alt(s, T[i].land); if (l > bl) { bl = l; best = i; } }
    char nm[48]; out[0] = 0;
    if (best >= 0) {
        const char* hs = lang == 0 ? anzeige(T[best].hs_de) : T[best].hs_en;
        erstes(T[best].land, nm);
        if (lang != 0) zweites(T[best].land, nm);
        const char* nmv = lang == 0 ? anzeige(nm) : nm;
        if (lang == 0) { cat(out, "Die Hauptstadt von ", max); cat(out, nmv, max); cat(out, " ist ", max); cat(out, hs, max); cat(out, ".", max); }
        else if (lang == 1) { cat(out, "The capital of ", max); cat(out, nmv, max); cat(out, " is ", max); cat(out, hs, max); cat(out, ".", max); }
        else if (lang == 2) { cat(out, "La capitale de ", max); cat(out, nmv, max); cat(out, " est ", max); cat(out, hs, max); cat(out, ".", max); }
        else if (lang == 3) { cat(out, "La capitale di ", max); cat(out, nmv, max); cat(out, " e' ", max); cat(out, hs, max); cat(out, ".", max); }
        else { cat(out, "La capital de ", max); cat(out, nmv, max); cat(out, " es ", max); cat(out, hs, max); cat(out, ".", max); }
        return 1;
    }
    for (int i = 0; T[i].land; i++) {                               /* umgekehrt: "Rom ist die Hauptstadt von welchem Land?" */
        char a[40], b[40]; int k = 0; for (; T[i].hs_de[k] && k < 38; k++) a[k] = (char)((T[i].hs_de[k] >= 'A' && T[i].hs_de[k] <= 'Z') ? T[i].hs_de[k] + 32 : T[i].hs_de[k]); a[k] = 0;
        k = 0; for (; T[i].hs_en[k] && k < 38; k++) b[k] = (char)((T[i].hs_en[k] >= 'A' && T[i].hs_en[k] <= 'Z') ? T[i].hs_en[k] + 32 : T[i].hs_en[k]); b[k] = 0;
        if (hat_wort_str(s, a) || hat_wort_str(s, b)) {
            erstes(T[i].land, nm); if (lang != 0) zweites(T[i].land, nm);
            cat(out, lang == 0 ? anzeige(T[i].hs_de) : T[i].hs_en, max); cat(out, lang == 0 ? " ist die Hauptstadt von " : lang == 1 ? " is the capital of " : lang == 2 ? " est la capitale de " : lang == 3 ? " e' la capitale di " : " es la capital de ", max); cat(out, lang == 0 ? anzeige(nm) : nm, max); cat(out, ".", max);
            return 1;
        }
    }
    return 0;
}
