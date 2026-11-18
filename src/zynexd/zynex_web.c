#define _GNU_SOURCE
#include "zynex_web.h"

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "zynex_model.h"
#include "zynex_rules.h"

extern const char zx_web_html[];
extern const unsigned zx_web_html_len;

#define KOPF_MAX 8192
#define KOERPER_MAX 65536
#define NOTIZ_MAX 200
#define NOTIZ_TEXT 8000

static zx_web_host g_h;
static pthread_mutex_t g_notiz_mx = PTHREAD_MUTEX_INITIALIZER;

/* ---------- kleine Helfer ---------- */

static int sende(int fd, const char *d, size_t n)
{
    while (n > 0) {
        ssize_t w = send(fd, d, n, MSG_NOSIGNAL);
        if (w <= 0)
            return -1;
        d += w;
        n -= (size_t)w;
    }
    return 0;
}

static int sende_s(int fd, const char *s) { return sende(fd, s, strlen(s)); }

static void antwort(int fd, int code, const char *typ, const char *koerper, size_t n)
{
    char kopf[256];
    int l = snprintf(kopf, sizeof(kopf),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                     code, code == 200 ? "OK" : (code == 404 ? "Not Found" : "Bad Request"), typ, n);
    sende(fd, kopf, (size_t)l);
    sende(fd, koerper, n);
}

static void antwort_s(int fd, int code, const char *typ, const char *s) { antwort(fd, code, typ, s, strlen(s)); }

static int hexw(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Wert zu Schluessel aus "a=b&c=d" holen (prozent-dekodiert) nach out. Rueckgabe 1, wenn gefunden. */
static int feld(const char *body, const char *schluessel, char *out, size_t cap)
{
    size_t kl = strlen(schluessel);
    const char *p = body;
    while (*p) {
        if (strncmp(p, schluessel, kl) == 0 && p[kl] == '=' && (p == body || p[-1] == '&')) {
            size_t o = 0;
            p += kl + 1;
            while (*p && *p != '&' && o + 1 < cap) {
                if (*p == '%' && hexw(p[1]) >= 0 && hexw(p[2]) >= 0) {
                    out[o++] = (char)(hexw(p[1]) * 16 + hexw(p[2]));
                    p += 3;
                } else if (*p == '+') {
                    out[o++] = ' ';
                    p++;
                } else {
                    out[o++] = *p++;
                }
            }
            out[o] = '\0';
            return 1;
        }
        p = strchr(p, '&');
        if (!p)
            break;
        p++;
    }
    if (cap)
        out[0] = '\0';
    return 0;
}

/* JSON-String schreiben (mit Anfuehrungszeichen) */
static size_t json_s(char *out, size_t cap, const char *s)
{
    size_t o = 0;
    if (cap < 3)
        return 0;
    out[o++] = '"';
    for (; *s && o + 8 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            out[o++] = '\\';
            out[o++] = (char)c;
        } else if (c == '\n') {
            out[o++] = '\\'; out[o++] = 'n';
        } else if (c == '\r') {
            out[o++] = '\\'; out[o++] = 'r';
        } else if (c == '\t') {
            out[o++] = '\\'; out[o++] = 't';
        } else if (c < 0x20) {
            o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
        } else {
            out[o++] = (char)c;
        }
    }
    out[o++] = '"';
    out[o] = '\0';
    return o;
}

/* ---------- Notizen (eine Zeile je Notiz: titel<TAB>text, \n und \\ maskiert) ---------- */

typedef struct {
    char titel[120];
    char *text;
} notiz;

static void maskiere(FILE *f, const char *s)
{
    for (; *s; s++) {
        if (*s == '\n') fputs("\\n", f);
        else if (*s == '\\') fputs("\\\\", f);
        else if (*s == '\r' || *s == '\t') fputc(' ', f);
        else fputc(*s, f);
    }
}

static void demaskiere(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (*r == '\\' && r[1] == 'n') { *w++ = '\n'; r += 2; }
        else if (*r == '\\' && r[1] == '\\') { *w++ = '\\'; r += 2; }
        else *w++ = *r++;
    }
    *w = '\0';
}

static int notizen_lesen(notiz *liste, int max)
{
    FILE *f;
    char *zeile = malloc(NOTIZ_TEXT * 2 + 256);
    int n = 0;
    if (!zeile || !g_h.notizdatei)
        goto ende;
    f = fopen(g_h.notizdatei, "r");
    if (!f)
        goto ende;
    while (n < max && fgets(zeile, NOTIZ_TEXT * 2 + 256, f)) {
        char *tab = strchr(zeile, '\t');
        size_t l = strlen(zeile);
        while (l > 0 && (zeile[l - 1] == '\n' || zeile[l - 1] == '\r'))
            zeile[--l] = '\0';
        if (!tab)
            continue;
        *tab = '\0';
        snprintf(liste[n].titel, sizeof(liste[n].titel), "%s", zeile);
        demaskiere(tab + 1);
        liste[n].text = strdup(tab + 1);
        if (liste[n].text)
            n++;
    }
    fclose(f);
ende:
    free(zeile);
    return n;
}

static void notizen_schreiben(notiz *liste, int n)
{
    FILE *f;
    int i;
    if (!g_h.notizdatei)
        return;
    f = fopen(g_h.notizdatei, "w");
    if (!f)
        return;
    for (i = 0; i < n; i++) {
        maskiere(f, liste[i].titel);
        fputc('\t', f);
        maskiere(f, liste[i].text);
        fputc('\n', f);
    }
    fclose(f);
}

static void notizen_frei(notiz *liste, int n)
{
    while (n-- > 0)
        free(liste[n].text);
}

static void api_notizen(int fd, const char *pfad, const char *body)
{
    static notiz liste[NOTIZ_MAX];
    int n, i;
    pthread_mutex_lock(&g_notiz_mx);
    n = notizen_lesen(liste, NOTIZ_MAX);
    if (strcmp(pfad, "/api/notizen/neu") == 0) {
        char t[120], *x = malloc(NOTIZ_TEXT + 1);
        if (x && n < NOTIZ_MAX && feld(body, "x", x, NOTIZ_TEXT + 1)) {
            feld(body, "t", t, sizeof(t));
            if (!t[0])
                snprintf(t, sizeof(t), "Notiz");
            snprintf(liste[n].titel, sizeof(liste[n].titel), "%s", t);
            liste[n].text = strdup(x);
            if (liste[n].text)
                n++;
            notizen_schreiben(liste, n);
        }
        free(x);
    } else if (strcmp(pfad, "/api/notizen/loeschen") == 0) {
        char z[16];
        feld(body, "i", z, sizeof(z));
        i = atoi(z);
        if (z[0] && i >= 0 && i < n) {
            free(liste[i].text);
            memmove(&liste[i], &liste[i + 1], (size_t)(n - i - 1) * sizeof(notiz));
            n--;
            notizen_schreiben(liste, n);
        }
    }
    {
        size_t cap = 256, o = 0;
        char *js;
        for (i = 0; i < n; i++)
            cap += strlen(liste[i].text) * 2 + 400;
        js = malloc(cap);
        if (js) {
            js[o++] = '[';
            for (i = 0; i < n; i++) {
                if (i) js[o++] = ',';
                o += (size_t)snprintf(js + o, cap - o, "{\"t\":");
                o += json_s(js + o, cap - o, liste[i].titel);
                o += (size_t)snprintf(js + o, cap - o, ",\"x\":");
                o += json_s(js + o, cap - o, liste[i].text);
                js[o++] = '}';
            }
            js[o++] = ']';
            js[o] = '\0';
            antwort(fd, 200, "application/json; charset=utf-8", js, o);
            free(js);
        }
    }
    notizen_frei(liste, n);
    pthread_mutex_unlock(&g_notiz_mx);
}

/* ---------- Gedaechtnis ---------- */

static void api_gedaechtnis(int fd, const char *pfad, const char *body)
{
    char js[ZX_MEM_MAX * (ZX_MEM_LEN * 2 + 8) + 64], t[ZX_MEM_LEN + 4], z[16];
    size_t o = 0;
    int i;
    pthread_mutex_lock(g_h.mem_mx);
    if (strcmp(pfad, "/api/gedaechtnis/neu") == 0) {
        if (feld(body, "t", t, sizeof(t)))
            zx_mem_neu(g_h.mem, t);
    } else if (strcmp(pfad, "/api/gedaechtnis/loeschen") == 0) {
        if (feld(body, "alle", z, sizeof(z)))
            zx_mem_alles_loeschen(g_h.mem);
        else if (feld(body, "i", z, sizeof(z)))
            zx_mem_loesche(g_h.mem, atoi(z));
    }
    js[o++] = '[';
    for (i = 0; i < g_h.mem->n; i++) {
        if (i) js[o++] = ',';
        o += json_s(js + o, sizeof(js) - o, g_h.mem->zettel[i]);
    }
    js[o++] = ']';
    js[o] = '\0';
    pthread_mutex_unlock(g_h.mem_mx);
    antwort(fd, 200, "application/json; charset=utf-8", js, o);
}

/* ---------- Chat (Antwort laeuft als offener Textstrom, Ende: \x1e + Endtext) ---------- */

typedef struct {
    int fd;
    int fehler;
    char text[4096];
} strom;

static int strom_cb(const char *stueck, void *n)
{
    strom *s = n;
    size_t l = strlen(s->text), k = strlen(stueck);
    if (l + k + 1 < sizeof(s->text))
        memcpy(s->text + l, stueck, k + 1);
    if (sende(s->fd, stueck, k) != 0) {
        s->fehler = 1;
        return 1; /* Browser weg: Erzeugung abbrechen */
    }
    return 0;
}

static void api_chat(int fd, const char *body)
{
    char *q = malloc(12000), *verlauf = malloc(ZX_VERLAUF_N * 2 * 1200 + 64), f[1200], a[1200], key[8], extra[400] = "";
    char gtext[600];
    int i;
    size_t vo = 0;

    if (!q || !verlauf) {
        free(q); free(verlauf);
        antwort_s(fd, 500, "text/plain", "Speicher");
        return;
    }
    feld(body, "q", q, 12000);
    verlauf[0] = '\0';
    for (i = 0; i < 6; i++) {
        snprintf(key, sizeof(key), "f%d", i);
        if (!feld(body, key, f, sizeof(f)))
            break;
        snprintf(key, sizeof(key), "a%d", i);
        feld(body, key, a, sizeof(a));
        vo += (size_t)snprintf(verlauf + vo, ZX_VERLAUF_N * 2 * 1200 + 64 - vo,
                               "<|im_start|>user\n%s<|im_end|>\n<|im_start|>assistant\n%s<|im_end|>\n", f, a);
    }
    sende_s(fd, "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\n"
                "Cache-Control: no-store\r\nX-Accel-Buffering: no\r\nConnection: close\r\n\r\n");

    pthread_mutex_lock(g_h.mem_mx);
    i = zx_mem_befehl(g_h.mem, q, gtext, sizeof(gtext));
    pthread_mutex_unlock(g_h.mem_mx);
    if (i) { /* Gedaechtnis-Befehl, kein Modell noetig */
        sende_s(fd, gtext);
        sende_s(fd, "\x1e");
        sende_s(fd, gtext);
        sende_s(fd, "\x1fg"); /* Hinweis: Gedaechtnis hat sich evtl. geaendert */
    } else if (!g_h.bereit || !g_h.bereit()) {
        const char *t = "Mein Sprachmodell ist noch nicht bereit. Gib mir einen Moment.";
        sende_s(fd, t);
        sende_s(fd, "\x1e");
        sende_s(fd, t);
    } else {
        strom s;
        double tps = 0.0;
        char aktion[64];
        int n;
        s.fd = fd; s.fehler = 0; s.text[0] = '\0';
        n = g_h.frage(verlauf, q, strom_cb, &s, &tps);
        if (n < 0)
            snprintf(s.text, sizeof(s.text), "Hm, dabei ist mir ein Fehler passiert.");
        if (!s.fehler) {
            if (zx_model_split(s.text, aktion, sizeof(aktion))) {
                switch (zx_rules_check(aktion)) {
                case ZX_FREI: snprintf(extra, sizeof(extra), " Gerne. %s (simuliert, Regel: frei).", aktion); break;
                case ZX_NACHFRAGEN: snprintf(extra, sizeof(extra), " Ich wuerde %s tun (simuliert, Regel: nachfragen).", aktion); break;
                default: snprintf(extra, sizeof(extra), " Das tue ich nicht. Die Regelschicht erlaubt es nie."); break;
                }
            }
            sende_s(fd, "\x1e");
            sende_s(fd, s.text);
            sende_s(fd, extra);
            snprintf(gtext, sizeof(gtext), "\x1f" "t%.1f", tps);
            sende_s(fd, gtext);
        }
    }
    free(q);
    free(verlauf);
}

/* ---------- Verbindung ---------- */

static void *verbindung(void *arg)
{
    int fd = (int)(intptr_t)arg;
    char *puf = malloc(KOPF_MAX + KOERPER_MAX + 8);
    size_t n = 0, kopf_ende = 0, laenge = 0;
    char methode[8], pfad[200];
    struct timeval tv = {30, 0};

    if (!puf) { close(fd); return NULL; }
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    for (;;) {
        ssize_t r;
        char *e;
        if (n >= KOPF_MAX + KOERPER_MAX) break;
        r = recv(fd, puf + n, KOPF_MAX + KOERPER_MAX - n, 0);
        if (r <= 0) break;
        n += (size_t)r;
        puf[n] = '\0';
        if (!kopf_ende && (e = strstr(puf, "\r\n\r\n")) != NULL) {
            const char *cl;
            kopf_ende = (size_t)(e - puf) + 4;
            cl = strcasestr(puf, "content-length:");
            laenge = (cl && (size_t)(cl - puf) < kopf_ende) ? (size_t)strtoul(cl + 15, NULL, 10) : 0;
            if (laenge > KOERPER_MAX) { antwort_s(fd, 400, "text/plain", "zu gross"); goto ende; }
        }
        if (kopf_ende && n >= kopf_ende + laenge) break;
    }
    if (!kopf_ende || sscanf(puf, "%7s %199s", methode, pfad) != 2) { goto ende; }
    {
        char *body = puf + kopf_ende, *qm = strchr(pfad, '?');
        if (qm) *qm = '\0';
        body[laenge < n - kopf_ende ? laenge : n - kopf_ende] = '\0';
        if (strcmp(methode, "GET") == 0 && (strcmp(pfad, "/") == 0 || strcmp(pfad, "/index.html") == 0)) {
            antwort(fd, 200, "text/html; charset=utf-8", zx_web_html, zx_web_html_len);
        } else if (strcmp(pfad, "/api/status") == 0) {
            antwort_s(fd, 200, "application/json", g_h.bereit && g_h.bereit() ? "{\"bereit\":true}" : "{\"bereit\":false}");
        } else if (strcmp(pfad, "/api/chat") == 0 && strcmp(methode, "POST") == 0) {
            api_chat(fd, body);
        } else if (strncmp(pfad, "/api/notizen", 12) == 0) {
            api_notizen(fd, pfad, body);
        } else if (strncmp(pfad, "/api/gedaechtnis", 16) == 0) {
            api_gedaechtnis(fd, pfad, body);
        } else {
            antwort_s(fd, 404, "text/plain", "nicht gefunden");
        }
    }
ende:
    free(puf);
    close(fd);
    return NULL;
}

static void *annehmen(void *arg)
{
    int s = (int)(intptr_t)arg;
    for (;;) {
        int fd = accept(s, NULL, NULL);
        pthread_t th;
        pthread_attr_t at;
        if (fd < 0) {
            if (errno == EINTR) continue;
            sleep(1);
            continue;
        }
        pthread_attr_init(&at);
        pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&th, &at, verbindung, (void *)(intptr_t)fd) != 0)
            close(fd);
        pthread_attr_destroy(&at);
    }
    return NULL;
}

int zx_web_start(const zx_web_host *host, int port)
{
    struct sockaddr_in a;
    int s, ja = 1;
    pthread_t th;
    pthread_attr_t at;

    g_h = *host;
    s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0)
        return -1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &ja, sizeof(ja));
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 16) != 0) {
        close(s);
        return -1;
    }
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, annehmen, (void *)(intptr_t)s) != 0) {
        close(s);
        return -1;
    }
    pthread_attr_destroy(&at);
    return 0;
}

int zx_web_adresse(char *out, size_t cap, int port)
{
    struct ifaddrs *ifa, *p;
    int ok = 0;
    if (getifaddrs(&ifa) != 0)
        return 0;
    for (p = ifa; p && !ok; p = p->ifa_next) {
        char ip[INET_ADDRSTRLEN];
        if (!p->ifa_addr || p->ifa_addr->sa_family != AF_INET || (p->ifa_flags & IFF_LOOPBACK))
            continue;
        inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr, ip, sizeof(ip));
        snprintf(out, cap, "http://%s:%d", ip, port);
        ok = 1;
    }
    freeifaddrs(ifa);
    return ok;
}
