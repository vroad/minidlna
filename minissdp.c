/* $Id$ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include "config.h"
#include "upnpdescstrings.h"
#include "miniupnpdpath.h"
#include "upnphttp.h"
#include "upnpglobalvars.h"
#include "minissdp.h"

/* SSDP ip/port */
#define SSDP_PORT (1900)
#define SSDP_MCAST_ADDR ("239.255.255.250")

static int
AddMulticastMembership(int s, in_addr_t ifaddr/*const char * ifaddr*/)
{
	struct ip_mreq imr;	/* Ip multicast membership */

    /* setting up imr structure */
    imr.imr_multiaddr.s_addr = inet_addr(SSDP_MCAST_ADDR);
    /*imr.imr_interface.s_addr = htonl(INADDR_ANY);*/
    imr.imr_interface.s_addr = ifaddr;	/*inet_addr(ifaddr);*/
	
	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&imr, sizeof(struct ip_mreq)) < 0)
	{
        syslog(LOG_ERR, "setsockopt(udp, IP_ADD_MEMBERSHIP): %m");
		return -1;
    }

	return 0;
}

int
OpenAndConfSSDPReceiveSocket()
/*OpenAndConfSSDPReceiveSocket(int n_lan_addr,
							 struct lan_addr_s * lan_addr)*/
{
	int s;
	int i = 1;
	struct sockaddr_in sockname;
	
	if( (s = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		syslog(LOG_ERR, "socket(udp): %m");
		return -1;
	}	

	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i)) < 0)
	{
		syslog(LOG_WARNING, "setsockopt(udp, SO_REUSEADDR): %m");
	}
	
	memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(SSDP_PORT);
	/* NOTE : it seems it doesnt work when binding on the specific address */
    /*sockname.sin_addr.s_addr = inet_addr(UPNP_MCAST_ADDR);*/
    sockname.sin_addr.s_addr = htonl(INADDR_ANY);
    /*sockname.sin_addr.s_addr = inet_addr(ifaddr);*/

    if(bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
	{
		syslog(LOG_ERR, "bind(udp): %m");
		close(s);
		return -1;
    }

	i = n_lan_addr;
	while(i>0)
	{
		i--;
		if(AddMulticastMembership(s, lan_addr[i].addr.s_addr) < 0)
		{
			syslog(LOG_WARNING,
			       "Failed to add multicast membership for address %s", 
			       lan_addr[i].str );
		}
	}

	return s;
}

/* open the UDP socket used to send SSDP notifications to
 * the multicast group reserved for them */
static int
OpenAndConfSSDPNotifySocket(in_addr_t addr)
{
	int s;
	unsigned char loopchar = 0;
	int bcast = 1;
	struct in_addr mc_if;
	struct sockaddr_in sockname;
	
	if( (s = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
	{
		syslog(LOG_ERR, "socket(udp_notify): %m");
		return -1;
	}

	mc_if.s_addr = addr;	/*inet_addr(addr);*/

	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopchar, sizeof(loopchar)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, IP_MULTICAST_LOOP): %m");
		close(s);
		return -1;
	}

	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&mc_if, sizeof(mc_if)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, IP_MULTICAST_IF): %m");
		close(s);
		return -1;
	}
	
	if(setsockopt(s, SOL_SOCKET, SO_BROADCAST, &bcast, sizeof(bcast)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, SO_BROADCAST): %m");
		close(s);
		return -1;
	}

	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_addr.s_addr = addr;	/*inet_addr(addr);*/

	if (bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
	{
		syslog(LOG_ERR, "bind(udp_notify): %m");
		close(s);
		return -1;
	}

	return s;
}

int
OpenAndConfSSDPNotifySockets(int * sockets)
/*OpenAndConfSSDPNotifySockets(int * sockets,
                             struct lan_addr_s * lan_addr, int n_lan_addr)*/
{
	int i, j;
	for(i=0; i<n_lan_addr; i++)
	{
		sockets[i] = OpenAndConfSSDPNotifySocket(lan_addr[i].addr.s_addr);
		if(sockets[i] < 0)
		{
			for(j=0; j<i; j++)
			{
				close(sockets[j]);
				sockets[j] = -1;
			}
			return -1;
		}
	}
	return 0;
}

/*
 * response from a LiveBox (Wanadoo)
HTTP/1.1 200 OK
CACHE-CONTROL: max-age=1800
DATE: Thu, 01 Jan 1970 04:03:23 GMT
EXT:
LOCATION: http://192.168.0.1:49152/gatedesc.xml
SERVER: Linux/2.4.17, UPnP/1.0, Intel SDK for UPnP devices /1.2
ST: upnp:rootdevice
USN: uuid:75802409-bccb-40e7-8e6c-fa095ecce13e::upnp:rootdevice

 * response from a Linksys 802.11b :
HTTP/1.1 200 OK
Cache-Control:max-age=120
Location:http://192.168.5.1:5678/rootDesc.xml
Server:NT/5.0 UPnP/1.0
ST:upnp:rootdevice
USN:uuid:upnp-InternetGatewayDevice-1_0-0090a2777777::upnp:rootdevice
EXT:
 */

/* not really an SSDP "announce" as it is the response
 * to a SSDP "M-SEARCH" */
static void
SendSSDPAnnounce2(int s, struct sockaddr_in sockname,
                  const char * st, int st_len, const char * suffix,
                  const char * host, unsigned short port)
{
	int l, n;
	char buf[512];
	/* TODO :
	 * follow guideline from document "UPnP Device Architecture 1.0"
	 * put in uppercase.
	 * DATE: is recommended
	 * SERVER: OS/ver UPnP/1.0 miniupnpd/1.0
	 * - check what to put in the 'Cache-Control' header 
	 * */
	char   szTime[30];
	time_t tTime = time(NULL);
	strftime(szTime, 30,"%a, %d %b %Y %H:%M:%S GMT" , gmtime(&tTime));

	l = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n"
		"CACHE-CONTROL: max-age=1810\r\n"
		"DATE: %s\r\n"
		"Ext:\r\n"
		"ST: %.*s%s\r\n"
		"USN: %s::%.*s%s\r\n"
		"EXT:\r\n"
		"SERVER: " MINIUPNPD_SERVER_STRING "\r\n"
		"LOCATION: http://%s:%u" ROOTDESC_PATH "\r\n"
		"Content-Length: 0\r\n"
		"\r\n",
		szTime,
		st_len, st, suffix,
		uuidvalue, st_len, st, suffix,
		host, (unsigned int)port);
	n = sendto(s, buf, l, 0,
	           (struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
	if(n < 0)
	{
		syslog(LOG_ERR, "sendto(udp): %m");
	}
}

static const char * const known_service_types[] =
{
	"upnp:rootdevice",
	"urn:schemas-upnp-org:device:MediaServer:",
	"urn:schemas-upnp-org:service:ContentDirectory:",
	"urn:schemas-upnp-org:service:ConnectionManager:",
	"urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:",
	uuidvalue,
	0
};

static void
SendSSDPNotifies(int s, const char * host, unsigned short port,
                 unsigned int lifetime)
{
	struct sockaddr_in sockname;
	int l, n, dup, i=0;
	char bufr[512];

	memset(&sockname, 0, sizeof(struct sockaddr_in));
	sockname.sin_family = AF_INET;
	sockname.sin_port = htons(SSDP_PORT);
	sockname.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);

	for( dup=0; dup<2; dup++ )
	{
		if( dup )
			usleep(200000);
		i=0;
		while(known_service_types[i])
		{
			l = snprintf(bufr, sizeof(bufr), 
					"NOTIFY * HTTP/1.1\r\n"
					"HOST:%s:%d\r\n"
					"CACHE-CONTROL:max-age=%u\r\n"
					"LOCATION:http://%s:%d" ROOTDESC_PATH"\r\n"
					"SERVER: " MINIUPNPD_SERVER_STRING "\r\n"
					"NT:%s%s\r\n"
					"USN:%s::%s%s\r\n"
					"NTS:ssdp:alive\r\n"
					"\r\n",
					SSDP_MCAST_ADDR, SSDP_PORT,
					lifetime,
					host, port,
					known_service_types[i], (i==0?"":"1"),
					uuidvalue, known_service_types[i], (i==0?"":"1") );
			if(l>=sizeof(bufr))
			{
				syslog(LOG_WARNING, "SendSSDPNotifies(): truncated output");
				l = sizeof(bufr);
			}
			//DEBUG printf("Sending NOTIFY:\n%s", bufr);
			n = sendto(s, bufr, l, 0,
				(struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
			if(n < 0)
			{
				syslog(LOG_ERR, "sendto(udp_notify=%d, %s): %m", s, host);
			}
			i++;
		}
	}
}

void
SendSSDPNotifies2(int * sockets,
                  unsigned short port,
                  unsigned int lifetime)
/*SendSSDPNotifies2(int * sockets, struct lan_addr_s * lan_addr, int n_lan_addr,
                  unsigned short port,
                  unsigned int lifetime)*/
{
	int i;
	for(i=0; i<n_lan_addr; i++)
	{
		SendSSDPNotifies(sockets[i], lan_addr[i].str, port, lifetime);
	}
}

/* ProcessSSDPRequest()
 * process SSDP M-SEARCH requests and responds to them */
void
ProcessSSDPRequest(int s, unsigned short port)
/*ProcessSSDPRequest(int s, struct lan_addr_s * lan_addr, int n_lan_addr,
                   unsigned short port)*/
{
	int n;
	char bufr[1500];
	socklen_t len_r;
	struct sockaddr_in sendername;
	int i, l;
	int lan_addr_index = 0;
	char * st = 0;
	int st_len = 0;
	len_r = sizeof(struct sockaddr_in);

	n = recvfrom(s, bufr, sizeof(bufr), 0,
	             (struct sockaddr *)&sendername, &len_r);
	if(n < 0)
	{
		syslog(LOG_ERR, "recvfrom(udp): %m");
		return;
	}

	if(memcmp(bufr, "NOTIFY", 6) == 0)
	{
		/* ignore NOTIFY packets. We could log the sender and device type */
		return;
	}
	else if(memcmp(bufr, "M-SEARCH", 8) == 0)
	{
		i = 0;
		while(i < n)
		{
			while(bufr[i] != '\r' || bufr[i+1] != '\n')
				i++;
			i += 2;
			if(strncasecmp(bufr+i, "st:", 3) == 0)
			{
				st = bufr+i+3;
				st_len = 0;
				while(*st == ' ' || *st == '\t') st++;
				while(st[st_len]!='\r' && st[st_len]!='\n') st_len++;
				/*syslog(LOG_INFO, "ST: %.*s", st_len, st);*/
				/*j = 0;*/
				/*while(bufr[i+j]!='\r') j++;*/
				/*syslog(LOG_INFO, "%.*s", j, bufr+i);*/
			}
		}
		/*syslog(LOG_INFO, "SSDP M-SEARCH packet received from %s:%d",
	           inet_ntoa(sendername.sin_addr),
	           ntohs(sendername.sin_port) );*/
		if( ntohs(sendername.sin_port) <= 1024 || ntohs(sendername.sin_port) == 1900 )
		{
			syslog(LOG_INFO, "WARNING: Ignoring invalid SSDP M-SEARCH from %s [bad source port %d]",
			   inet_ntoa(sendername.sin_addr), ntohs(sendername.sin_port));
		}
		else if(st)
		{
			/* TODO : doesnt answer at once but wait for a random time */
			syslog(LOG_INFO, "SSDP M-SEARCH from %s:%d ST: %.*s",
	        	   inet_ntoa(sendername.sin_addr),
	           	   ntohs(sendername.sin_port),
				   st_len, st);
			/* find in which sub network the client is */
			for(i = 0; i<n_lan_addr; i++)
			{
				if( (sendername.sin_addr.s_addr & lan_addr[i].mask.s_addr)
				   == (lan_addr[i].addr.s_addr & lan_addr[i].mask.s_addr))
				{
					lan_addr_index = i;
					break;
				}
			}
			/* Responds to request with a device as ST header */
			for(i = 0; known_service_types[i]; i++)
			{
				l = (int)strlen(known_service_types[i]);
				if(l<=st_len && (0 == memcmp(st, known_service_types[i], l)))
				{
					SendSSDPAnnounce2(s, sendername,
					                  st, st_len, "",
					                  lan_addr[lan_addr_index].str, port);
					break;
				}
			}
			/* Responds to request with ST: ssdp:all */
			/* strlen("ssdp:all") == 8 */
			if(st_len==8 && (0 == memcmp(st, "ssdp:all", 8)))
			{
				for(i=0; known_service_types[i]; i++)
				{
					l = (int)strlen(known_service_types[i]);
					SendSSDPAnnounce2(s, sendername,
					                  known_service_types[i], l, i==0?"":"1",
					                  lan_addr[lan_addr_index].str, port);
				}
			}
			/* responds to request by UUID value */
			l = (int)strlen(uuidvalue);
			if(l==st_len && (0 == memcmp(st, uuidvalue, l)))
			{
				SendSSDPAnnounce2(s, sendername, st, st_len, "",
				                  lan_addr[lan_addr_index].str, port);
			}
		}
		else
		{
			syslog(LOG_INFO, "Invalid SSDP M-SEARCH from %s:%d",
	        	   inet_ntoa(sendername.sin_addr), ntohs(sendername.sin_port));
		}
	}
	else
	{
		syslog(LOG_NOTICE, "Unknown udp packet received from %s:%d",
		       inet_ntoa(sendername.sin_addr), ntohs(sendername.sin_port));
	}
}

/* This will broadcast ssdp:byebye notifications to inform 
 * the network that UPnP is going down. */
int
SendSSDPGoodbye(int * sockets, int n_sockets)
{
	struct sockaddr_in sockname;
	int n, l;
	int i, j;
	char bufr[512];

    memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(SSDP_PORT);
    sockname.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);

	for(j=0; j<n_sockets; j++)
	{
	    for(i=0; known_service_types[i]; i++)
	    {
	        l = snprintf(bufr, sizeof(bufr),
	                 "NOTIFY * HTTP/1.1\r\n"
	                 "HOST:%s:%d\r\n"
	                 "NT:%s%s\r\n"
	                 "USN:%s::%s%s\r\n"
	                 "NTS:ssdp:byebye\r\n"
	                 "\r\n",
	                 SSDP_MCAST_ADDR, SSDP_PORT,
					 known_service_types[i], (i==0?"":"1"),
	                 uuidvalue, known_service_types[i], (i==0?"":"1"));
		n = sendto(sockets[j], bufr, l, 0,
	                   (struct sockaddr *)&sockname, sizeof(struct sockaddr_in) );
			if(n < 0)
			{
				syslog(LOG_ERR, "sendto(udp_shutdown=%d): %m", sockets[j]);
				return -1;
			}
    	    }
	}
	return 0;
}
