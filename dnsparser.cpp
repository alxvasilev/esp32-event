#include "dnsparser.h"
#include "stdio.h"
#include <string.h>
#include <string>
#include <arpa/inet.h>

using namespace std;
#define DNS_LOG_DEBUG(fmtStr,...) printf("%s: " fmtStr, __FUNCTION__, ##__VA_ARGS__)

#pragma pack(push, 1)
struct DnsHeader
{
	unsigned short id; // identification number

	unsigned char rd :1; // recursion desired
	unsigned char tc :1; // truncated message
	unsigned char aa :1; // authoritive answer
	unsigned char opcode :4; // purpose of message
	unsigned char qr :1; // query/response flag

	unsigned char rcode :4; // response code
	unsigned char cd :1; // checking disabled
	unsigned char ad :1; // authenticated data
	unsigned char z :1; // its z! reserved
	unsigned char ra :1; // recursion available

	unsigned short q_count; // number of question entries
	unsigned short ans_count; // number of answer entries
	unsigned short auth_count; // number of authority entries
	unsigned short add_count; // number of resource entries
};

//Constant sized fields of query structure
struct Question
{
	unsigned short qtype;
	unsigned short qclass;
};

//Constant sized fields of the resource record structure
struct ResourceHeader
{
	unsigned short type;
	unsigned short rclass;
	unsigned int ttl;
	unsigned short dataLen;
};
#pragma pack(pop)

static const char* dnsReadName(const char* aSrc, string& name, const char* buf, const char* bufEnd);
static int dnsWriteName(char* dns, const char* host);
LDNS_CAPI const char* rrTypeToString(int rrType);

LDNS_CAPI uint16_t dnsCreateRequest(const char* host, int qtype, unsigned short id, char* buf, uint16_t bufSize)
{
    DnsHeader* dns = (DnsHeader*)buf;
    memset(dns, 0, sizeof(DnsHeader));
    dns->id = id; //rand() % 65535;
	dns->rd = 1; //Recursion Desired
	dns->q_count = htons(1); //we have only 1 question

	//point to the query portion
    char* qname = buf+sizeof(DnsHeader);
    int qnameLen = dnsWriteName(qname, host);
    Question* qinfo = (Question*)(buf+sizeof(DnsHeader) + qnameLen);
	qinfo->qtype = htons(qtype);
    qinfo->qclass = htons(1);
    return sizeof(DnsHeader) + qnameLen + sizeof(Question);
}

LDNS_CAPI int dnsParseResponse(const char* buf, uint16_t buflen, uint8_t isName, char* retBuf, uint16_t* retBufSize)
{
//DnsHeader.fixed QNAME.var QTYPE.fixed QCLASS.fixed Answer.var
    if (buflen < sizeof(DnsHeader)+sizeof(Question))
    {
        DNS_LOG_DEBUG("dnsParseResponse: Packet is shorter than minimal");
        return EINVAL;
    }
    const char* packetEnd = buf+buflen;
    DnsHeader* hdr = (DnsHeader*)buf;
    auto ansCount = ntohs(hdr->ans_count);
    auto qtype = ((Question*)(buf+sizeof(DnsHeader)))->qtype;
    string name;
    const char* cnameData = nullptr;
    const char* rData = nullptr;
    uint16_t rDataLen = 0;
    bool found = false;
    //skip QUESTION and QNAME
    const char* src = dnsReadName(buf+sizeof(DnsHeader)+sizeof(Question), name, buf, packetEnd);
	for (size_t i = 0; i < ansCount; i++)
	{
        src = dnsReadName(src, name, buf, packetEnd);
        auto reshdr = (ResourceHeader*)src;
        src += sizeof(ResourceHeader);
		if (src > packetEnd)
        {
            DNS_LOG_DEBUG("dnsParseResponse: Domain name in resource record spans beyond packet end");
            return EINVAL;
        }
        rData = src;
        rDataLen = ntohs(reshdr->dataLen);
        src += rDataLen;
		if ((src-1) > packetEnd)
        {
            DNS_LOG_DEBUG("dnsParseResponse: Resource record data spans beyond packet buffer");
            return EINVAL;
        }
        auto rType = ntohs(reshdr->type);
        if (rType == qtype)
		{
            break;
        }
        else if (rType == DNSQ_CNAME && !cnameData)
        {
            cnameData = rData;
        }
	}
    if (rData)
    {
        if (name.size() > *retBufSize)
        {
            DNS_LOG_DEBUG("dnParseResponse: Record data is larger than provided output buffer");
            return ENOBUFS;
        }
            dnsReadName(rData, name, buf, packetEnd);
            *retBufSize = name.size();
            memcpy(retBuf, name.c_str(), name.size());
        }
        else
        {
            if (rDataLen > *retBufSize)
            {
                DNS_LOG_DEBUG("dnsParseResponse: Record data is larger than provided output buffer");
                return ENOBUFS;
            }
            *retBufSize = rDataLen;
            memcpy(retBuf, rData, rDataLen);
        }
        found = true;
    }

    //we havent found an answer, check if it's a CNAME redirect
    if (cnameData)
    {
        if (found)
        {
            printf("Server answered, but provided CNAME as well\n");
            return 0;
        }
        dnsReadName(cnameData, name, buf, packetEnd);
        if (name.size() > *retBufSize)
        {
            DNS_LOG_DEBUG("dnsParseResponse: Output buffer is too small for a CNAME redirect domain");
            return ENOBUFS;
        }
        *retBufSize = name.size();
        memcpy(retBuf, name.c_str(), name.size());
        return EAGAIN;
    }
    return ENOENT;
}

static const char* dnsReadName(const char* src, string& name, const char* buf, const char* bufEnd)
{
    if ((src < buf) || (src > bufEnd))
    {
        DNS_LOG_DEBUG("dnsReadName: Pointer not inside buffer");
        return nullptr;
    }
	bool jumped = false; //detect if src pointer has jumped, so that we stop incrementing the 'next' pointer
	name.clear();
	//read and convert the names in 3www6google3com form
	while (*src)
	{
        unsigned char ch = *((unsigned char*)src);
        if(ch >= 192) //192 = 0b11000000
		{
            jumped = true; //jumped, we can't track the length of the name after a jump
            src = buf + ((ch & 63)<<8) + *(unsigned char*)(src+1); //63 = 00111111; x&63 resets the first two 1 bits
		}
		else
		{
            name.append(src+1, ch)+='.'; //could be an @ if this is an email
            src+=(ch+1);
		}
        if ((src < buf) || (src > bufEnd))
        {
            DNS_LOG_DEBUG("Pointer not inside buffer");
            return nullptr;
        }
	}
	if (!name.empty())
    {
		name.resize(name.size()-1); //remove last dot
    }
    return jumped ? nullptr : src+1;
}

//this will convert www.google.com to 3www6google3com
static int dnsWriteName(char* dns, const char* host)
{
	int lastLenPos = 0;
	int currentLabelLen = 0;
    for(; *host; host++)
	{
        if(*host == '.')
		{
			dns[lastLenPos] = currentLabelLen;
			lastLenPos += (currentLabelLen+1);
			currentLabelLen = 0;
		}
		else
		{
			currentLabelLen++;
            dns[lastLenPos+currentLabelLen] = *host;
		}
	}
	dns[lastLenPos] = currentLabelLen;
	lastLenPos += (currentLabelLen+1);
	dns[lastLenPos] = 0;
	return lastLenPos+1; //size of dns name
}

LDNS_CAPI const char* rrTypeToString(int rrType)
{
	switch (rrType)
	{
	case 1:
		return "A";
	case 2:
		return "NS";
	case 5:
		return "CNAME";
	case 6:
		return "SOA";
	case 15:
		return "MX";
	case 28:
		return "AAAA";
	case 255:
		return "<Query All>";
	case 41:
		return "OPT";
	case 35:
		return "NAPTR";
	default:
		return "<unknown>";
	}
}
