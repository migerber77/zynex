/* Externe Sprachmodelle, siehe extki.h */
#include "extki.h"
#include "zyndb.h"
#include "net.h"

extern void kprint(const char* str);

static const char* const KENN[4] = { "intern", "openai", "claude", "gemini" };
static const char* const ANZ[4] = { "Zynie (intern)", "ChatGPT", "Claude", "Gemini" };
static const char* const STD[4] = { "", "gpt-4o-mini", "claude-haiku-5-5", "gemini-2.0-flash" };

static int slen(const char* s) { int n = 0; while (s[n]) n++; return n; }
static int gl(const char* a, const char* b) { while (*a && *a == *b) { a++; b++; } return *a == *b; }
static void kat(char* d, const char* s, int max) { int n = slen(d); int i = 0; while (s[i] && n + i < max - 1) { d[n + i] = s[i]; i++; } d[n + i] = 0; }
static void schl(char* k, int q, const char* f) { k[0] = 0; kat(k, "extern.", 48); kat(k, KENN[q], 48); kat(k, ".", 48); kat(k, f, 48); }

const char* ext_name(int q) { return ANZ[q >= 0 && q < 4 ? q : 0]; }
const char* ext_kennung(int q) { return KENN[q >= 0 && q < 4 ? q : 0]; }
int ext_quelle(void) { const char* v = zdb_get("extern.quelle"); if (!v) return 0; for (int i = 1; i < 4; i++) if (gl(v, KENN[i])) return i; return 0; }
void ext_setze_quelle(int q) { zdb_put("extern.quelle", KENN[q >= 0 && q < 4 ? q : 0]); }
/* Schluessel koennen laenger sein als ein ZynDB-Wert (160 Zeichen, z. B. OpenAI-Projektschluessel): sie liegen in Stuecken (zdb_put_text) */
int ext_schluessel_da(int q) { char k[48]; static char b[4]; schl(k, q, "key"); if (zdb_get_text(k, b, 2) > 0) return 1; const char* v = zdb_get(k); return v && v[0]; }
void ext_schluessel_setze(int q, const char* key) { char k[48]; schl(k, q, "key"); zdb_put_text(k, key, slen(key)); zdb_put(k, ""); }
void ext_modell_setze(int q, const char* name) { char k[48]; schl(k, q, "modell"); zdb_put(k, name); }
const char* ext_modell(int q) { char k[48]; schl(k, q, "modell"); const char* v = zdb_get(k); return v && v[0] ? v : STD[q]; }

/* ---- JSON ---- */
static int hex(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; }
int ext_json_text(const char* b, uint32_t len, const char* f, char* out, int max) {
    int fl = slen(f);
    for (uint32_t i = 0; i + (uint32_t)fl + 2 < len; i++) {
        if (b[i] != '"') continue; int ok = 1; for (int j = 0; j < fl; j++) if (b[i + 1 + (uint32_t)j] != f[j]) { ok = 0; break; }
        if (!ok || b[i + 1 + (uint32_t)fl] != '"') continue;
        uint32_t p = i + 2 + (uint32_t)fl; while (p < len && (b[p] == ' ' || b[p] == ':' || b[p] == '\n' || b[p] == '\r' || b[p] == '\t')) p++;
        if (p >= len || b[p] != '"') continue;
        p++; int o = 0;
        while (p < len && b[p] != '"' && o < max - 4) {
            char c = b[p++];
            if (c != '\\') { out[o++] = c; continue; }
            if (p >= len) break; c = b[p++];
            if (c == 'n' || c == 'r' || c == 't') out[o++] = ' ';
            else if (c == 'u' && p + 4 <= len) {
                int v = 0; for (int k = 0; k < 4; k++) { int h = hex(b[p + (uint32_t)k]); v = v * 16 + (h < 0 ? 0 : h); } p += 4;
                if (v < 0x80) out[o++] = (char)v; else if (v < 0x800) { out[o++] = (char)(0xC0 | (v >> 6)); out[o++] = (char)(0x80 | (v & 63)); } else out[o++] = '?';
            } else out[o++] = c;
        }
        out[o] = 0; return o;
    }
    return -1;
}
static int escape(const char* s, char* o, int max) {
    int n = 0;
    for (; *s && n < max - 8; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') { o[n++] = '\\'; o[n++] = (char)c; }
        else if (c == '\n' || c == '\r' || c == '\t') o[n++] = ' ';
        else if (c < 32) continue;
        else o[n++] = (char)c;
    }
    o[n] = 0; return n;
}

int ext_frage(const char* frage, char* out, int max) {
    int q = ext_quelle(); out[0] = 0;
    if (!q) return -1;
    char k[48]; schl(k, q, "key"); static char kbuf[360]; const char* key = zdb_get_text(k, kbuf, sizeof(kbuf)) > 0 ? kbuf : zdb_get(k);
    if (!key || !key[0]) { kat(out, "Kein Schluessel fuer ", max); kat(out, ANZ[q], max); kat(out, " (Shell: kiextern einrichten ", max); kat(out, KENN[q], max); kat(out, ")", max); return -2; }
    static char esc[3400], body[3800], path[200], hdr[400];
    escape(frage, esc, sizeof(esc));
    const char* host; const char* modell = ext_modell(q);
    body[0] = path[0] = hdr[0] = 0;
    if (q == 1) {
        host = "api.openai.com"; kat(path, "/v1/chat/completions", 200);
        kat(hdr, "Authorization: Bearer ", 400); kat(hdr, key, 400); kat(hdr, "\r\n", 400);
        kat(body, "{\"model\":\"", 3800); kat(body, modell, 3800); kat(body, "\",\"max_tokens\":1200,\"messages\":[{\"role\":\"user\",\"content\":\"", 3800); kat(body, esc, 3800); kat(body, "\"}]}", 3800);
    } else if (q == 2) {
        host = "api.anthropic.com"; kat(path, "/v1/messages", 200);
        kat(hdr, "x-api-key: ", 400); kat(hdr, key, 400); kat(hdr, "\r\nanthropic-version: 2023-06-01\r\n", 400);
        kat(body, "{\"model\":\"", 3800); kat(body, modell, 3800); kat(body, "\",\"max_tokens\":1200,\"messages\":[{\"role\":\"user\",\"content\":\"", 3800); kat(body, esc, 3800); kat(body, "\"}]}", 3800);
    } else {
        host = "generativelanguage.googleapis.com"; kat(path, "/v1beta/models/", 200); kat(path, modell, 200); kat(path, ":generateContent?key=", 200); kat(path, key, 200);
        kat(body, "{\"contents\":[{\"parts\":[{\"text\":\"", 3800); kat(body, esc, 3800); kat(body, "\"}]}]}", 3800);
    }
    const char* ant; uint32_t al, st;
    int r = net_post_tls(host, path, hdr, body, &ant, &al, &st);
    if (r != 0) { kat(out, "Keine Verbindung zu ", max); kat(out, ANZ[q], max); return -3; }
    if (st != 200) {
        char m[160]; if (ext_json_text(ant, al, "message", m, sizeof(m)) > 0) kat(out, m, max); else { kat(out, ANZ[q], max); kat(out, " lehnt die Anfrage ab (Status ", max); char z[8]; int n = 0; uint32_t v = st; char t[8]; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); for (int i = 0; i < n; i++) z[i] = t[n - 1 - i]; z[n] = 0; kat(out, z, max); kat(out, ")", max); }
        return -4;
    }
    int n = ext_json_text(ant, al, q == 1 ? "content" : "text", out, max);
    if (n <= 0) { out[0] = 0; kat(out, "Antwort von ", max); kat(out, ANZ[q], max); kat(out, " nicht lesbar", max); return -5; }
    return n;
}
