#pragma once
#include <stdint.h>
/* Netzwerk: Intel-e1000/e1000e-Treiber + ARP, IPv4, ICMP, UDP, DHCP, DNS (Polling, kein TCP). */
int  net_init(void);                       /* Karte starten, DHCP versuchen */
void net_poll(void);                       /* empfangene Pakete abarbeiten */
int  net_up(void);
void net_info(void);
int  net_dhcp(void);
int  net_ping(const char* ziel);
int  net_dns(const char* name, uint32_t* ip);
int  net_parse_ip(const char* s, uint32_t* ip);
int  net_http_fp(const char* url, const char* fp);   /* fp: SHA-256-Fingerabdruck des Server-Zertifikats (https) oder 0 */
int  net_http(const char* url);            /* http://host[:port]/pfad (nur HTTP, kein TLS) */
int  net_status(char* out, int max);   /* 0 keine Karte, 1 ohne IP, 2 bereit (out = IP) */
int  net_wget(const char* url, const char* name, const char* fp);   /* Seite ins SynFS speichern */
