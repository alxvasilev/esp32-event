#ifndef DNSCLIENT_H
#define DNSCLIENT_H
#include <stdint.h>

#ifdef __cplusplus
    #define LDNS_CAPI extern "C"
#else
    #define LDNS_CAPI
#endif
enum
{
    DNSQ_A=1, /* host address */
    DNSQ_NS=2, /* authoritative server */
    DNSQ_CNAME=5, /* canonical name */
    DNSQ_SOA=6, /* start of authority zone */
    DNSQ_PTR=12, /* domain name pointer */
    DNSQ_MX=15, /* mail routing information */
};

LDNS_CAPI uint16_t dnsCreateRequest(const char* host, int qtype, unsigned short id, char* buf, uint16_t bufSize);
LDNS_CAPI int dnsParseResponse(const char* buf, uint16_t buflen, uint8_t isName, char* retbuf, uint16_t* retBufSize);

#endif // DNSCLIENT_H
