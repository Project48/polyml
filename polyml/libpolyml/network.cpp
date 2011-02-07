/*
    Title:      Network functions.

    Copyright (c) 2000-7 David C. J. Matthews

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.
    
    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.
    
    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

*/
#ifdef HAVE_CONFIG_H
#include "config.h"
#elif defined(WIN32)
#include "winconfig.h"
#else
#error "No configuration file"
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_ASSERT_H
#include <assert.h>
#define ASSERT(x) assert(x)
#else
#define ASSERT(x) 0
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_PARAM_H
#include <sys/param.h>
#endif

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif

#ifdef HAVE_NETDB_H
#include <netdb.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#ifdef HAVE_NETINET_IN_H
#include <netinet/in.h>
#endif

#ifdef HAVE_NETINET_TCP_H
#include <netinet/tcp.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
#include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif

#ifdef HAVE_SYS_SELECT_H
#include <sys/select.h>
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t;
#endif

#ifdef HAVE_WINDOWS_H
#include <windows.h>
#endif


#ifdef WINDOWS_PC
#ifdef USEWINSOCK2
#include <winsock2.h>
#else
#include <winsock.h>
#endif
#endif

#include "globals.h"
#include "gc.h"
#include "arb.h"
#include "run_time.h"
#include "mpoly.h"
#include "processes.h"
#include "network.h"
#include "io_internal.h"
#include "sys.h"
#include "polystring.h"
#include "save_vec.h"
#include "rts_module.h"
#include "machine_dep.h"

#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 256
#endif

#define STREAMID(x) (DEREFSTREAMHANDLE(x)->streamNo)
#define SAVE(x) taskData->saveVec.push(x)
#define ALLOC(n) alloc_and_save(taskData, n)
#define SIZEOF(x) (sizeof(x)/sizeof(PolyWord))

#ifdef WINDOWS_PC
static int winsock_init = 0; /* Check that it has been initialised. */

#else
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR    (-1)
#endif

#ifndef HAVE_SOCKLEN_T
typedef int socklen_t; // This must be int for Windows at least
#endif

#ifndef SHUT_RD
#define SHUT_RD     0
#endif

#ifndef SHUT_WR
#define SHUT_WR     1
#endif

#ifndef SHUT_RDWR
#define SHUT_RDWR   2
#endif

/* Address families.  Although this table is in ascending
   numerical order of address family nothing depends on that.
   The only requirement is that "INET" => AF_INET must always
   be present and "UNIX" => AF_UNIX must be present on Unix.
   Other entries are entirely optional and are for amusement
   only. */
struct af_tab_struct {
    const char *af_name;
    int af_num;
} af_table[] =
{
#ifdef AF_UNIX
    { "UNIX",       AF_UNIX }, /* This is nearly always there. */
#endif
#ifdef AF_LOCAL
    { "LOCAL",      AF_LOCAL },
#endif
    { "INET",       AF_INET }, /* This one should always be there. */
#ifdef AF_IMPLINK
    { "IMPLINK",        AF_IMPLINK },
#endif
#ifdef AF_PUP
    { "PUP",        AF_PUP },
#endif
#ifdef AF_CHAOS
    { "CHAOS",      AF_CHAOS },
#endif
#ifdef AF_IPX
    { "IPX",        AF_IPX },
#endif
#ifdef AF_NS
    { "NS",         AF_NS },
#endif
#ifdef AF_ISO
    { "ISO",        AF_ISO },
#endif
#ifdef AF_OSI
    { "OSI",        AF_OSI },
#endif
#ifdef AF_ECMA
    { "ECMA",       AF_ECMA },
#endif
#ifdef AF_DATAKIT
    { "DATAKIT",        AF_DATAKIT },
#endif
#ifdef AF_CCITT
    { "CCITT",      AF_CCITT },
#endif
#ifdef AF_SNA
    { "SNA",        AF_SNA },
#endif
#ifdef AF_DECnet
    { "DECnet",     AF_DECnet },
#endif
#ifdef AF_DLI
    { "DLI",        AF_DLI },
#endif
#ifdef AF_LAT
    { "LAT",        AF_LAT },
#endif
#ifdef AF_HYLINK
    { "HYLINK",     AF_HYLINK },
#endif
#ifdef AF_APPLETALK
    { "APPLETALK",      AF_APPLETALK },
#endif
#ifdef AF_NETBIOS
    { "NETBIOS",        AF_NETBIOS },
#endif
#ifdef AF_ROUTE
    { "ROUTE",      AF_ROUTE },
#endif
#ifdef AF_VOICEVIEW
    { "VOICEVIEW",      AF_VOICEVIEW },
#endif
#ifdef AF_FIREFOX
    { "FIREFOX",        AF_FIREFOX },
#endif
#ifdef AF_BAN
    { "BAN",        AF_BAN },
#endif
#ifdef AF_LINK
    { "LINK",       AF_LINK },
#endif
#ifdef AF_COIP
    { "COIP",       AF_COIP },
#endif
#ifdef AF_CNT
    { "CNT",        AF_CNT },
#endif
#ifdef AF_SIP
    { "SIP",        AF_SIP },
#endif
#ifdef AF_ISDN
    { "ISDN",       AF_ISDN },
#endif
#ifdef AF_E164
    { "E164",       AF_E164 },
#endif
#ifdef AF_INET6
    { "INET6",      AF_INET6 },
#endif
#ifdef AF_NATM
    { "NATM",       AF_NATM },
#endif
#ifdef AF_ATM
    { "ATM",        AF_ATM },
#endif
#ifdef AF_NETGRAPH
    { "NETGRAPH",       AF_NETGRAPH },
#endif
};

/* Socket types.  Only STREAM and DGRAM are required.  */
struct sk_tab_struct {
    const char *sk_name;
    int sk_num;
} sk_table[] =
{
    { "STREAM",     SOCK_STREAM },
    { "DGRAM",      SOCK_DGRAM },
    { "RAW",        SOCK_RAW },
    { "RDM",        SOCK_RDM },
    { "SEQPACKET",      SOCK_SEQPACKET }
};

static Handle makeHostEntry(TaskData *taskData, struct hostent *host);
static Handle makeProtoEntry(TaskData *taskData, struct protoent *proto);
static Handle makeServEntry(TaskData *taskData, struct servent *proto);
static Handle makeNetEntry(TaskData *taskData, struct netent *net);
static Handle makeList(TaskData *taskData, int count, char *p, int size, void *arg,
                       Handle (mkEntry)(TaskData *, void*, char*));
static Handle mkAftab(TaskData *taskData, void*, char *p);
static Handle mkSktab(TaskData *taskData, void*, char *p);
static Handle setSocketOption(TaskData *taskData, Handle args, int level, int opt);
static Handle getSocketOption(TaskData *taskData, Handle args, int level, int opt);
static Handle getSocketInt(TaskData *taskData, Handle args, int level, int opt);
static Handle selectCall(TaskData *taskData, Handle args, int blockType);

#ifdef WINDOWS_PC
/* To allow for portable code we map Windows socket errors to
   errno-style errors if there is a suitable entry in errno.h.
   If there isn't we simply return the negative value and handle
   it later. N.B. WSAEWOULDBLOCK is one of those which does not
   have an equivalent.  (It does seem to be defined in Windows CE). */
static int MapError(int err)
{
    switch (err)
    {
#ifdef EINTR
    case WSAEINTR: return EINTR;
#endif
#ifdef EBADF
    case WSAEBADF: return EBADF;
#endif
#ifdef EACCES
    case WSAEACCES: return EACCES;
#endif
#ifdef EFAULT
    case WSAEFAULT: return EFAULT;
#endif
#ifdef EINVAL
    case WSAEINVAL: return EINVAL;
#endif
#ifdef EMFILE
    case WSAEMFILE: return EMFILE;
#endif
#ifdef ENAMETOOLONG
    case WSAENAMETOOLONG: return ENAMETOOLONG;
#endif
#ifdef ENOTEMPTY
    case WSAENOTEMPTY: return ENOTEMPTY;
#endif
#ifdef EWOULDBLOCK
	case WSAEWOULDBLOCK: return EWOULDBLOCK;
#endif
    default: return -err;
    }
}

/* If these are not defined define them as negative because GetError returns
   negative values for socket library errors which do not have
   equivalents as errno-style errors. */
#ifndef EWOULDBLOCK
#define EWOULDBLOCK (-WSAEWOULDBLOCK)
#endif

#ifndef EINPROGRESS
#define EINPROGRESS EWOULDBLOCK
#endif

#ifndef EMFILE 
#define EMFILE      (-WSAEMFILE)
#endif

#ifndef EINTR
#define EINTR       (-WSAEINTR)
#endif

#ifndef EBADF
#define EBADF       (-WSAEBADF)
#endif

static int GetError()
{
    return MapError(WSAGetLastError());
}
#define GETERROR    (GetError())
#define MAPERROR(x) (MapError(x))
#else
#define GETERROR    (errno)
#define MAPERROR(x) (x)
#endif

class WaitNet: public Waiter {
public:
    WaitNet(SOCKET sock, bool isOOB = false) : m_sock(sock), m_isOOB(isOOB) {}
    void Wait(unsigned maxMillisecs);
private:
    SOCKET m_sock;
    bool m_isOOB;
};

// Use "select" in both Windows and Unix.  In Windows that means we
// don't watch hWakeupEvent but that's only a hint.
void WaitNet::Wait(unsigned maxMillisecs)
{
    fd_set readFds, writeFds, exceptFds;
    struct timeval toWait = { 0, 0 };
    // Mac OS X requires the usec field to be less than a million
    toWait.tv_sec = maxMillisecs / 1000;
    toWait.tv_usec = (maxMillisecs % 1000) * 1000;
    FD_ZERO(&readFds);
    FD_ZERO(&writeFds);
    FD_ZERO(&exceptFds);
    FD_SET(m_sock, m_isOOB ? &exceptFds : &readFds);
    int result = select(FD_SETSIZE, &readFds, &writeFds, &exceptFds, &toWait);
    ASSERT(result >= 0 || errno == EINTR); // The only "error" should be an interrupt.
}

Handle Net_dispatch_c(TaskData *taskData, Handle args, Handle code)
{
    int c = get_C_long(taskData, DEREFWORDHANDLE(code));
TryAgain:
    switch (c)
    {
    case 0:
        { /* Get the current host name. */
            char hostName[MAXHOSTNAMELEN];
            if (gethostname(hostName, MAXHOSTNAMELEN) != 0)
                raise_syscall(taskData, "gethostname failed", GETERROR);
            return (SAVE(C_string_to_Poly(taskData, hostName)));
        }

    case 1:
        {
            /* Look up a host name. */
            char hostName[MAXHOSTNAMELEN];
            int length;
            struct hostent *host;
            length = Poly_string_to_C(DEREFWORD(args),
                        hostName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Host name too long", ENAMETOOLONG);
            host = gethostbyname(hostName);
            if (host == NULL)
                raise_syscall(taskData, "gethostbyname failed", GETERROR);
            return makeHostEntry(taskData, host);
        }

    case 2:
        {
            /* Look up entry by address. */
            unsigned long addr =
                htonl(get_C_ulong(taskData, DEREFWORDHANDLE(args)));
            struct hostent *host;
            /* Look up a host name given an address. */
            host = gethostbyaddr((char*)&addr, sizeof(addr), AF_INET);
            if (host == NULL)
                raise_syscall(taskData, "gethostbyaddr failed", GETERROR);
            return makeHostEntry(taskData, host);
        }

    case 3:
        {
            /* Look up protocol entry. */
            char protoName[MAXHOSTNAMELEN];
            int length;
            struct protoent *proto;
            length = Poly_string_to_C(DEREFWORD(args),
                        protoName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Protocol name too long", ENAMETOOLONG);
            proto = getprotobyname(protoName);
            if (proto == NULL)
                raise_syscall(taskData, "getprotobyname failed", GETERROR);
            return makeProtoEntry(taskData, proto);
        }

    case 4:
        {
            /* Look up protocol entry. */
            struct protoent *proto;
            int pNum = get_C_ulong(taskData, DEREFWORDHANDLE(args));
            proto = getprotobynumber(pNum);
            if (proto == NULL)
                raise_syscall(taskData, "getprotobynumber failed", GETERROR);
            return makeProtoEntry(taskData, proto);
        }

    case 5:
        {
            /* Get service given service name only. */
            char servName[MAXHOSTNAMELEN];
            int length;
            struct servent *serv;
            length = Poly_string_to_C(DEREFWORD(args),
                        servName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Service name too long", ENAMETOOLONG);
            serv = getservbyname (servName, NULL);
            if (serv == NULL)
                raise_syscall(taskData, "getservbyname failed", GETERROR);
            return makeServEntry(taskData, serv);
        }

    case 6:
        {
            /* Get service given service name and protocol name. */
            char servName[MAXHOSTNAMELEN], protoName[MAXHOSTNAMELEN];
            int length;
            struct servent *serv; 
            length = Poly_string_to_C(args->WordP()->Get(0), servName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Service name too long", ENAMETOOLONG);
            length = Poly_string_to_C(args->WordP()->Get(1), protoName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Protocol name too long", ENAMETOOLONG);
            serv = getservbyname (servName, protoName);
            if (serv == NULL)
                raise_syscall(taskData, "getservbyname failed", GETERROR);
            return makeServEntry(taskData, serv);
        }

    case 7:
        {
            /* Get service given port number only. */
            struct servent *serv;
            long port = htons(get_C_ushort(taskData, DEREFWORDHANDLE(args)));
            serv = getservbyport(port, NULL);
            if (serv == NULL)
                raise_syscall(taskData, "getservbyport failed", GETERROR);
            return makeServEntry(taskData, serv);
        }

    case 8:
        {
            /* Get service given port number and protocol name. */
            char protoName[MAXHOSTNAMELEN];
            int length;
            struct servent *serv;
            long port = htons(get_C_ushort(taskData, DEREFHANDLE(args)->Get(0)));
            length = Poly_string_to_C(args->WordP()->Get(1), protoName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Protocol name too long", ENAMETOOLONG);
            serv = getservbyport (port, protoName);
            if (serv == NULL)
                raise_syscall(taskData, "getservbyport failed", GETERROR);
            return makeServEntry(taskData, serv);
        }

    case 9:
        {
            /* Get a network entry. */
            struct netent *net = NULL;
#ifdef HAVE_GETNETBYNAME
            char netName[MAXHOSTNAMELEN];
            POLYUNSIGNED length = Poly_string_to_C(DEREFWORD(args),
                        netName, MAXHOSTNAMELEN);
            if (length > MAXHOSTNAMELEN)
                raise_syscall(taskData, "Network name too long", ENAMETOOLONG);
            /* Winsock2 does not have getnetbyname.
               Just return failure. */
            net = getnetbyname(netName);
#endif
            if (net == NULL)
                raise_syscall(taskData, "getnetbyname failed", GETERROR);
            return makeNetEntry(taskData, net);
        }

    case 10:
        {
            /* Look up network entry from a number. */
            struct netent *net = NULL;
#ifdef HAVE_GETNETBYADDR
            unsigned long netNum = htonl(get_C_ulong(taskData, DEREFHANDLE(args)->Get(0)));
            long af = get_C_ulong(taskData, DEREFHANDLE(args)->Get(1));
            /* Winsock2 does not have getnetbyaddr.
               Just return failure. */
            net = getnetbyaddr(netNum, af);
#endif
            if (net == NULL)
                raise_syscall(taskData, "getnetbyaddr failed", GETERROR);
            return makeNetEntry(taskData, net);
        }

    case 11:
        {
            /* Return a list of known address families. */
            return makeList(taskData, sizeof(af_table)/sizeof(af_table[0]),
                            (char*)af_table, sizeof(af_table[0]),
                            0, mkAftab);
        }

    case 12:
        {
            /* Return a list of known socket types. */
            return makeList(taskData, sizeof(sk_table)/sizeof(sk_table[0]),
                            (char*)sk_table, sizeof(sk_table[0]),
                            0, mkSktab);
        }

    case 13: /* Return the "any" internet address. */
        return Make_unsigned(taskData, INADDR_ANY);

    case 14: /* Create a socket */
        {
            Handle str_token = make_stream_entry(taskData);
            PIOSTRUCT strm;
            int stream_no = STREAMID(str_token);
            int af = get_C_long(taskData, DEREFHANDLE(args)->Get(0));
            int type = get_C_long(taskData, DEREFHANDLE(args)->Get(1));
            int proto = get_C_long(taskData, DEREFHANDLE(args)->Get(2));
            unsigned long onOff = 1;
            SOCKET skt = socket(af, type, proto);
            if (skt == INVALID_SOCKET)
            {
                free_stream_entry(stream_no);
                switch (GETERROR)
                {
                case EMFILE: /* too many open files */
                    {
                        if (emfileFlag) /* Previously had an EMFILE error. */
                            raise_syscall(taskData, "socket failed", EMFILE);
                        emfileFlag = true;
                        FullGC(taskData); /* May clear emfileFlag if we close a file. */
                        goto TryAgain;
                    }
                case EINTR: goto TryAgain;
                default: raise_syscall(taskData, "socket failed", GETERROR);
                }
            }
            /* Set the socket to non-blocking mode. */
#ifdef WINDOWS_PC
            if (ioctlsocket(skt, FIONBIO, &onOff) != 0)
#else
            if (ioctl(skt, FIONBIO, &onOff) < 0)
#endif
            {
                free_stream_entry(stream_no);
#ifdef WINDOWS_PC
                closesocket(skt);
#else
                close(skt);
#endif
                raise_syscall(taskData, "ioctl failed", GETERROR);
            }
            strm = &basic_io_vector[stream_no];
            strm->device.sock = skt;
            strm->ioBits =
                IO_BIT_OPEN | IO_BIT_READ | IO_BIT_WRITE | IO_BIT_SOCKET ;
            return(str_token);
        }

    case 15: /* Set TCP No-delay option. */
        return setSocketOption(taskData, args, IPPROTO_TCP, TCP_NODELAY);

    case 16: /* Get TCP No-delay option. */
        return getSocketOption(taskData, args, IPPROTO_TCP, TCP_NODELAY);

    case 17: /* Set Debug option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_DEBUG);

    case 18: /* Get Debug option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_DEBUG);

    case 19: /* Set REUSEADDR option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_REUSEADDR);

    case 20: /* Get REUSEADDR option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_REUSEADDR);

    case 21: /* Set KEEPALIVE option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_KEEPALIVE);

    case 22: /* Get KEEPALIVE option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_KEEPALIVE);

    case 23: /* Set DONTROUTE option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_DONTROUTE);

    case 24: /* Get DONTROUTE option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_DONTROUTE);

    case 25: /* Set BROADCAST option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_BROADCAST);

    case 26: /* Get BROADCAST option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_BROADCAST);

    case 27: /* Set OOBINLINE option. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_OOBINLINE);

    case 28: /* Get OOBINLINE option. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_OOBINLINE);

    case 29: /* Set SNDBUF size. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_SNDBUF);

    case 30: /* Get SNDBUF size. */
        return getSocketInt(taskData, args, SOL_SOCKET, SO_SNDBUF);

    case 31: /* Set RCVBUF size. */
        return setSocketOption(taskData, args, SOL_SOCKET, SO_RCVBUF);

    case 32: /* Get RCVBUF size. */
        return getSocketInt(taskData, args, SOL_SOCKET, SO_RCVBUF);

    case 33: /* Get socket type e.g. SOCK_STREAM. */
        return getSocketInt(taskData, args, SOL_SOCKET, SO_TYPE);

    case 34: /* Get error status and clear it. */
        return getSocketOption(taskData, args, SOL_SOCKET, SO_ERROR);

    case 35: /* Set Linger time. */
        {
            struct linger linger;
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int lTime = get_C_long(taskData, DEREFHANDLE(args)->Get(1));
            /* We pass in a negative value to turn the option off,
               zero or positive to turn it on. */
            if (lTime < 0)
            {
                linger.l_onoff = 0;
                linger.l_linger = 0;
            }
            else
            {
                linger.l_onoff = 1;
                linger.l_linger = lTime;
            }
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (setsockopt(strm->device.sock, SOL_SOCKET, SO_LINGER,
                (char*)&linger, sizeof(linger)) != 0)
                raise_syscall(taskData, "setsockopt failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 36: /* Get Linger time. */
        {
            struct linger linger;
            PIOSTRUCT strm = get_stream(args->WordP());
            socklen_t size = sizeof(linger);
            int lTime = 0;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (getsockopt(strm->device.sock, SOL_SOCKET, SO_LINGER,
                (char*)&linger, &size) != 0)
                raise_syscall(taskData, "getsockopt failed", GETERROR);
            /* If the option is off return a negative. */
            if (linger.l_onoff == 0) lTime = -1;
            else lTime = linger.l_linger;
            return Make_arbitrary_precision(taskData, lTime);
        }

    case 37: /* Get peer name. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            struct sockaddr sockA;
            socklen_t   size = sizeof(sockA);
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (getpeername(strm->device.sock, &sockA, &size) != 0)
                raise_syscall(taskData, "getpeername failed", GETERROR);
            /* Addresses are treated as strings. */
            return(SAVE(Buffer_to_Poly(taskData, (char*)&sockA, size)));
        }

    case 38: /* Get socket name. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            struct sockaddr sockA;
            socklen_t   size = sizeof(sockA);
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (getsockname(strm->device.sock, &sockA, &size) != 0)
                raise_syscall(taskData, "getsockname failed", GETERROR);
            return(SAVE(Buffer_to_Poly(taskData, (char*)&sockA, size)));
        }

    case 39: /* Return the address family from an address. */
        {
            PolyStringObject *psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr *psock = (struct sockaddr *)&psAddr->chars;
            return Make_unsigned(taskData, psock->sa_family);
        }

    case 40: /* Create a socket address from a port number and
                internet address. */
        {
            struct sockaddr_in sockaddr;
            memset(&sockaddr, 0, sizeof(sockaddr));
            sockaddr.sin_family = AF_INET;
            sockaddr.sin_port = htons(get_C_ushort(taskData, DEREFHANDLE(args)->Get(0)));
            sockaddr.sin_addr.s_addr =
                htonl(get_C_ulong(taskData, DEREFHANDLE(args)->Get(1)));
            return(SAVE(Buffer_to_Poly(taskData, (char*)&sockaddr, sizeof(sockaddr))));
        }

    case 41: /* Return port number from an internet socket address.
                Assumes that we've already checked the address family. */
        {
            PolyStringObject *psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr_in *psock =
                (struct sockaddr_in *)&psAddr->chars;
            return Make_unsigned(taskData, ntohs(psock->sin_port));
        }

    case 42: /* Return internet address from an internet socket address.
                Assumes that we've already checked the address family. */
        {
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr_in *psock =
                (struct sockaddr_in *)&psAddr->chars;
            return Make_unsigned(taskData, ntohl(psock->sin_addr.s_addr));
        }

        /* 43 - Set non-blocking mode.  Now removed. */

    case 44: /* Find number of bytes available. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            unsigned long readable;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
#ifdef WINDOWS_PC
            if (ioctlsocket(strm->device.sock, FIONREAD, &readable) != 0)
                raise_syscall(taskData, "ioctlsocket failed", GETERROR);
#else
            if (ioctl(strm->device.sock, FIONREAD, &readable) < 0)
                raise_syscall(taskData, "ioctl failed", GETERROR);
#endif
            return Make_arbitrary_precision(taskData, readable);
        }

    case 45: /* Find out if we are at the mark. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            unsigned long atMark;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
#ifdef WINDOWS_PC
            if (ioctlsocket(strm->device.sock, SIOCATMARK, &atMark) != 0)
                raise_syscall(taskData, "ioctlsocket failed", GETERROR);
#else
            if (ioctl(strm->device.sock, SIOCATMARK, &atMark) < 0)
                raise_syscall(taskData, "ioctl failed", GETERROR);
#endif
            return Make_arbitrary_precision(taskData, atMark == 0 ? 0 : 1);
        }

    case 46: /* Accept a connection. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);

    case 58: /* Non-blocking accept. */
        {
            PIOSTRUCT strm = get_stream(args->WordP());
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            else {
                SOCKET sock = strm->device.sock, result;
                struct sockaddr resultAddr;
                int stream_no;
                socklen_t addrLen;
                Handle addrHandle, pair;
                Handle str_token;
                PIOSTRUCT newStrm;
                /* Get a token for the new socket - may raise an
                   exception if it fails. */
                str_token = make_stream_entry(taskData);
                stream_no = STREAMID(str_token);
                addrLen = sizeof(resultAddr);
                result = accept(sock, &resultAddr, &addrLen);

                if (result == INVALID_SOCKET)
                {
                    /* Free the stream entry */
                    free_stream_entry(stream_no);
                    switch (GETERROR)
                    {
                    case EINTR:
                        goto TryAgain; /* Have to retry if we got EINTR. */
                    case EMFILE: /* Too many files. */
                        {
                            if (emfileFlag) /* Previously had an EMFILE error. */
                                raise_syscall(taskData, "accept failed", EMFILE);
                            emfileFlag = true;
                            FullGC(taskData); /* May clear emfileFlag if we close a file. */
                            goto TryAgain;
                        }
                    case EWOULDBLOCK:
                        /* If the socket is in non-blocking mode we pass
                           this back to the caller.  If it is blocking we
                           suspend this process and try again later. */
                        if (c == 46 /* blocking version. */) {
                            WaitNet waiter(strm->device.sock);
                            processes->BlockAndRestart(taskData, &waiter, false, POLY_SYS_network);
                        }
                        /* else drop through. */
                    default:
                        raise_syscall(taskData, "accept failed", GETERROR);
                    }
                }

                addrHandle = SAVE(Buffer_to_Poly(taskData, (char*)&resultAddr, addrLen));
                newStrm = &basic_io_vector[stream_no];
                newStrm->device.sock = result;
                newStrm->ioBits =
                    IO_BIT_OPEN | IO_BIT_READ | IO_BIT_WRITE | IO_BIT_SOCKET;
                /* Return a pair of the new socket and the address. */
                pair = ALLOC(2);
                DEREFHANDLE(pair)->Set(0, DEREFWORDHANDLE(str_token));
                DEREFHANDLE(pair)->Set(1, DEREFWORDHANDLE(addrHandle));
                return pair;
            }
        }

    case 47: /* Bind an address to a socket. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP()->Get(1).AsObjPtr();
            struct sockaddr *psock = (struct sockaddr *)&psAddr->chars;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (bind(strm->device.sock, psock, psAddr->length) != 0)
                raise_syscall(taskData, "bind failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 48: /* Connect to an address. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 59: /* Non-blocking connect. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP()->Get(1).AsObjPtr();
            struct sockaddr *psock = (struct sockaddr *)&psAddr->chars;
            int res;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            /* In Windows, and possibly also in Unix, if we have
               received a previous EWOULDBLOCK we have to use "select"
               to tell us whether the connection actually succeeded. */
            while (1)
            {
                if (strm->ioBits & IO_BIT_INPROGRESS)
                {
                    fd_set read_fds, write_fds, except_fds;
                    struct timeval delay;
                    int sel;
                    SOCKET sock = strm->device.sock;
                    FD_ZERO(&read_fds);
                    FD_ZERO(&write_fds);
                    FD_ZERO(&except_fds);
                    FD_SET(sock, &write_fds);
                    FD_SET(sock, &except_fds);
                    delay.tv_sec  = 0; /* Poll. */
                    delay.tv_usec = 0;
                    /* In Windows failure is indicated by the bit being set in
                       the exception set rather than the write set. */
                    sel = select(FD_SETSIZE,&read_fds,&write_fds,&except_fds,&delay);
                    if (sel < 0)
                    {
                        int err = GETERROR;
                        if (err != EINTR)
                            raise_syscall(taskData, "select failed", err);
                        /* else continue */
                    }
                    else if (sel == 0)
                    {
                        /* Nothing yet */
                        processes->BlockAndRestart(taskData, NULL, false, POLY_SYS_network);
                            /* -1 => not for reading. */
                    }
                    else /* Definite result. */
                    {
                        int result = 0;
                        socklen_t len = sizeof(result);
                        strm->ioBits &= ~IO_BIT_INPROGRESS; /* No longer in progress. */
                        if (getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&result, &len) != 0
                            || result != 0)
                            raise_syscall(taskData, "connect failed", MAPERROR(result));
                        return Make_arbitrary_precision(taskData, 0); /* Success. */
                    }
                }
                else
                {
                    int err;
                    res = connect(strm->device.sock, psock, psAddr->length);
                    if (res == 0) return Make_arbitrary_precision(taskData, 0); /* OK */
                    /* It isn't clear that EINTR can ever occur with
                       connect, but just to be safe, we retry. */
                    err = GETERROR;
                    if ((err == EWOULDBLOCK || err == EINPROGRESS) && c == 48 /*blocking version*/)
                    {
                        strm->ioBits |= IO_BIT_INPROGRESS;
                        processes->BlockAndRestart(taskData, NULL, false, POLY_SYS_network);
                            /* -1 => not for reading. */
                    }
                    else if (err != EINTR)
                        raise_syscall(taskData, "connect failed", err);
                    /* else try again. */
                }
            }
        }

    case 49: /* Put socket into listening mode. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int backlog = get_C_ulong(taskData, DEREFHANDLE(args)->Get(1));
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (listen(strm->device.sock, backlog) != 0)
                raise_syscall(taskData, "listen failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 50: /* Shutdown the socket. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            int mode = 0;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            switch (get_C_ulong(taskData, DEREFHANDLE(args)->Get(1)))
            {
            case 1: mode = SHUT_RD; break;
            case 2: mode = SHUT_WR; break;
            case 3: mode = SHUT_RDWR;
            }
            if (shutdown(strm->device.sock, mode) != 0)
                raise_syscall(taskData, "shutdown failed", GETERROR);
            return Make_arbitrary_precision(taskData, 0);
        }

    case 51: /* Send data on a socket. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 60: /* Non-blocking send. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            PolyWord pBase = DEREFHANDLE(args)->Get(1);
            char    ch, *base;
            unsigned int offset = get_C_ulong(taskData, DEREFHANDLE(args)->Get(2));
            unsigned int length = get_C_ulong(taskData, DEREFHANDLE(args)->Get(3));
            unsigned int dontRoute = get_C_ulong(taskData, DEREFHANDLE(args)->Get(4));
            unsigned int outOfBand = get_C_ulong(taskData, DEREFHANDLE(args)->Get(5));
            int flags = 0, sent;
            if (dontRoute != 0) flags |= MSG_DONTROUTE;
            if (outOfBand != 0) flags |= MSG_OOB;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (IS_INT(pBase)) {
                /* Handle the special case where we are sending a single
                   byte vector and the "address" is the tagged byte itself. */
                ch = (char)UNTAGGED(pBase);
                base = &ch;
                offset = 0;
                length = 1;
            }
            else base = (char*)pBase.AsObjPtr()->AsBytePtr();

            while (1)
            {
                int err;
                sent = send(strm->device.sock, base+offset, length, flags);
                /* It isn't clear that EINTR can ever occur with
                   send but just to be safe we deal with that case and
                   retry the send. */
                if (sent != SOCKET_ERROR) /* OK. */
                    return Make_arbitrary_precision(taskData, sent);
                err = GETERROR;
                if (err == EWOULDBLOCK && c == 51 /* blocking */)
                {
                    processes->BlockAndRestart(taskData, NULL, false, POLY_SYS_network);
                        /* -1 => not for reading */
                    ASSERT(0); /* Must not have returned. */
                }
                else if (err != EINTR)
                    raise_syscall(taskData, "send failed", err);
                /* else try again */
            }
        }

    case 52: /* Send data on a socket to a given address. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 61: /* Non-blocking send. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP()->Get(1).AsObjPtr();
            PolyWord pBase = DEREFHANDLE(args)->Get(2);
            char    ch, *base;
            unsigned int offset = get_C_ulong(taskData, DEREFHANDLE(args)->Get(3));
            unsigned int length = get_C_ulong(taskData, DEREFHANDLE(args)->Get(4));
            unsigned int dontRoute = get_C_ulong(taskData, DEREFHANDLE(args)->Get(5));
            unsigned int outOfBand = get_C_ulong(taskData, DEREFHANDLE(args)->Get(6));
            int flags = 0, sent;
            if (dontRoute != 0) flags |= MSG_DONTROUTE;
            if (outOfBand != 0) flags |= MSG_OOB;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
            if (IS_INT(pBase)) {
                /* Handle the special case where we are sending a single
                   byte vector and the "address" is the tagged byte itself. */
                ch = (char)UNTAGGED(pBase);
                base = &ch;
                offset = 0;
                length = 1;
            }
            else base = (char*)pBase.AsObjPtr()->AsBytePtr();

            while (1)
            {
                int err;
                sent = sendto(strm->device.sock, base+offset, length, flags,
                            (struct sockaddr *)psAddr->chars, psAddr->length);
                /* It isn't clear that EINTR can ever occur with
                   send but just to be safe we deal with that case and
                   retry the send. */
                if (sent != SOCKET_ERROR) /* OK. */
                    return Make_arbitrary_precision(taskData, sent);
                err = GETERROR;
                if (err == EWOULDBLOCK && c == 52 /* blocking */)
                {
                    processes->BlockAndRestart(taskData, NULL, false, POLY_SYS_network);
                    ASSERT(0); /* Must not have returned. */
                }
                else if (err != EINTR)
                    raise_syscall(taskData, "sendto failed", err);
                /* else try again */
            }
        }

    case 53: /* Receive data into an array. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 62: /* Non-blocking receive. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            char *base = (char*)DEREFHANDLE(args)->Get(1).AsObjPtr()->AsBytePtr();
            unsigned int offset = get_C_ulong(taskData, DEREFHANDLE(args)->Get(2));
            unsigned int length = get_C_ulong(taskData, DEREFHANDLE(args)->Get(3));
            unsigned int peek = get_C_ulong(taskData, DEREFHANDLE(args)->Get(4));
            unsigned int outOfBand = get_C_ulong(taskData, DEREFHANDLE(args)->Get(5));
            int flags = 0, recvd;
            if (peek != 0) flags |= MSG_PEEK;
            if (outOfBand != 0) flags |= MSG_OOB;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);

            while (1) {
                int err;
                recvd = recv(strm->device.sock, base+offset, length, flags);
                err = GETERROR;
                if (recvd != SOCKET_ERROR) { /* OK. */
                    /* It appears that recv may return the length of the
                       message if that is longer than the buffer. */
                    if (recvd > (int)length) recvd = length;
                    return Make_arbitrary_precision(taskData, recvd);
                }
                if (err == EWOULDBLOCK && c == 53 /* blocking */)
                {
                    /* Block until something arrives. */
                    WaitNet waiter(strm->device.sock, outOfBand != 0);
                    processes->BlockAndRestart(taskData, &waiter, false, POLY_SYS_network);
                    ASSERT(0); /* Must not have returned. */
                }
                else if (err != EINTR)
                    raise_syscall(taskData, "recv failed", err);
                /* else try again */
            }
        }

    case 54: /* Receive data into an array and return the sender's
                address along with the length.  In Windows this can
                only be used with datagrams. */
        // We should check for interrupts even if we're not going to block.
        processes->TestAnyEvents(taskData);
    case 63: /* Non-blocking receive. */
        {
            PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
            char *base = (char*)DEREFHANDLE(args)->Get(1).AsObjPtr()->AsBytePtr();
            unsigned int offset = get_C_ulong(taskData, DEREFHANDLE(args)->Get(2));
            unsigned int length = get_C_ulong(taskData, DEREFHANDLE(args)->Get(3));
            unsigned int peek = get_C_ulong(taskData, DEREFHANDLE(args)->Get(4));
            unsigned int outOfBand = get_C_ulong(taskData, DEREFHANDLE(args)->Get(5));
            int flags = 0, recvd;
            socklen_t addrLen;
            struct sockaddr resultAddr;

            if (peek != 0) flags |= MSG_PEEK;
            if (outOfBand != 0) flags |= MSG_OOB;
            if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);

            while (1) {
                int err;
                recvd = recvfrom(strm->device.sock, base+offset,
                                 length, flags, &resultAddr, &addrLen);
                err = GETERROR;

                if (recvd != SOCKET_ERROR) { /* OK. */
                    Handle addrHandle, lengthHandle, pair;
                    if (recvd > (int)length) recvd = length;
                    lengthHandle = Make_arbitrary_precision(taskData, recvd);
                    addrHandle = SAVE(Buffer_to_Poly(taskData, (char*)&resultAddr, addrLen));
                    pair = ALLOC(2);
                    DEREFHANDLE(pair)->Set(0, DEREFWORDHANDLE(lengthHandle));
                    DEREFHANDLE(pair)->Set(1, DEREFWORDHANDLE(addrHandle));
                    return pair;
                }
                if (err == EWOULDBLOCK && c == 54 /* blocking */)
                {
                    WaitNet waiter(strm->device.sock, outOfBand != 0);
                    processes->BlockAndRestart(taskData, &waiter, false, POLY_SYS_network);
                    ASSERT(0); /* Must not have returned. */
                }
                else if (err != EINTR)
                    raise_syscall(taskData, "recvfrom failed", err);
                /* else try again */
            }
        }

    case 55: /* Create a socket pair. */
#ifdef WINDOWS_PC
        /* Not implemented. */
        raise_syscall(taskData, "socketpair not implemented", -WSAEAFNOSUPPORT);
#else
        {
            Handle str_token1 = make_stream_entry(taskData);
            Handle str_token2 = make_stream_entry(taskData);
            Handle pair;
            PIOSTRUCT strm1, strm2;
            int stream_no1 = STREAMID(str_token1);
            int stream_no2 = STREAMID(str_token2);
            int af = get_C_long(taskData, DEREFHANDLE(args)->Get(0));
            int type = get_C_long(taskData, DEREFHANDLE(args)->Get(1));
            int proto = get_C_long(taskData, DEREFHANDLE(args)->Get(2));
            int onOff = 1;
            SOCKET skt[2];
            if (socketpair(af, type, proto, skt) != 0)
            {
                free_stream_entry(stream_no1);
                free_stream_entry(stream_no2);
                switch (GETERROR)
                {
                case EMFILE: /* too many open files */
                    {
                        if (emfileFlag) /* Previously had an EMFILE error. */
                            raise_syscall(taskData, "socket failed", EMFILE);
                        emfileFlag = true;
                        FullGC(taskData); /* May clear emfileFlag if we close a file. */
                        goto TryAgain;
                    }
                case EINTR: goto TryAgain;
                default: raise_syscall(taskData, "socketpair failed", GETERROR);
                }
            }
            /* Set the sockets to non-blocking mode. */
            if (ioctl(skt[0], FIONBIO, &onOff) < 0 ||
                ioctl(skt[1], FIONBIO, &onOff) < 0)
            {
                free_stream_entry(stream_no1);
                free_stream_entry(stream_no2);
                close(skt[0]);
                close(skt[1]);
                raise_syscall(taskData, "ioctl failed", GETERROR);
            }
            strm1 = &basic_io_vector[stream_no1];
            strm1->device.sock = skt[0];
            strm1->ioBits =
                IO_BIT_OPEN | IO_BIT_READ | IO_BIT_WRITE | IO_BIT_SOCKET ;
            strm2 = &basic_io_vector[stream_no2];
            strm2->device.sock = skt[1];
            strm2->ioBits =
                IO_BIT_OPEN | IO_BIT_READ | IO_BIT_WRITE | IO_BIT_SOCKET ;
            /* Return the two streams as a pair. */
            pair = ALLOC(2);
            DEREFHANDLE(pair)->Set(0, DEREFWORDHANDLE(str_token1));
            DEREFHANDLE(pair)->Set(1, DEREFWORDHANDLE(str_token2));
            return pair;
        }
#endif

    case 56: /* Create a Unix socket address from a string. */
#ifdef WINDOWS_PC
        /* Not implemented. */
        raise_syscall(taskData, "Unix addresses not implemented", -WSAEAFNOSUPPORT);
#else
        {
            struct sockaddr_un addr;
            int length;
            memset(&addr, 0, sizeof(addr));
            addr.sun_family = AF_UNIX;
#ifdef HAVE_STRUCT_SOCKADDR_UN_SUN_LEN
            addr.sun_len = sizeof(addr); // Used in FreeBSD only.
#endif
            length = Poly_string_to_C(DEREFWORD(args),
                        addr.sun_path, sizeof(addr.sun_path));
            if (length > (int)sizeof(addr.sun_path))
                raise_syscall(taskData, "Address too long", ENAMETOOLONG);
            return SAVE(Buffer_to_Poly(taskData, (char*)&addr, sizeof(addr)));
        }
#endif

    case 57: /* Get the file name from a Unix socket address. */
#ifdef WINDOWS_PC
        /* Not implemented. */
        raise_syscall(taskData, "Unix addresses not implemented", -WSAEAFNOSUPPORT);
#else
        {
            PolyStringObject * psAddr = (PolyStringObject *)args->WordP();
            struct sockaddr_un *psock = (struct sockaddr_un *)&psAddr->chars;
            return SAVE(C_string_to_Poly(taskData, psock->sun_path));
        }
#endif

    case 64: /* Blocking select call. Infinite timeout. */
        return selectCall(taskData, args, 1);

    case 65: /* Polling select call. Zero timeout. */
        return selectCall(taskData, args, 2);

    case 66: /* Select call with non-zero timeout. */
        return selectCall(taskData, args, 0);


    default:
        {
            char msg[100];
            sprintf(msg, "Unknown net function: %d", c);
            raise_exception_string(taskData, EXC_Fail, msg);
			return 0;
        }
    }
}

/* "Polymorphic" function to generate a list. */
static Handle makeList(TaskData *taskData, int count, char *p, int size, void *arg,
                       Handle (mkEntry)(TaskData *, void*, char*))
{
    Handle saved = taskData->saveVec.mark();
    Handle list = SAVE(ListNull);
    /* Start from the end of the list. */
    p += count*size;
    while (count > 0)
    {
        Handle value, next;
        p -= size; /* Back up to the last entry. */
        value = mkEntry(taskData, arg, p);
        next  = ALLOC(SIZEOF(ML_Cons_Cell));

        DEREFLISTHANDLE(next)->h = DEREFWORDHANDLE(value); 
        DEREFLISTHANDLE(next)->t = DEREFLISTHANDLE(list);

        taskData->saveVec.reset(saved);
        list = SAVE(DEREFHANDLE(next));
        count--;
    }
    return list;
}

static Handle mkAddr(TaskData *taskData, void *arg, char *p)
{
    int j;
    struct hostent *host = (struct hostent *)arg;
    unsigned long addr = 0;
    /* Addresses are in network order so this is fairly easy.
       In practice they will be 4 byte entries so we could
       just use ntohl. */
    for (j = 0; j < host->h_length; j++)
        addr = (addr << 8) | ((*(char**)p)[j] & 255);
    return Make_unsigned(taskData, addr);
}

/* Convert a host entry into a tuple for ML. */
static Handle makeHostEntry(TaskData *taskData, struct hostent *host)
{
    /* We need to do all this in the right order.  We cannot
       construct the result tuple until all the values are
       ready.  We have to save each entry on the save stack
       just in case of a garbage collection. */
    int i;
    char **p;
    Handle aliases, name, addrType, result;
    Handle addrList = SAVE(ListNull);

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, host->h_name));

    /* Aliases. */
    for (i=0, p = host->h_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, host->h_aliases);

    /* Address type. */
    addrType = Make_unsigned(taskData, host->h_addrtype);

    /* Addresses. */
    /* Count them first and then work from the end back. */
    for (i=0, p = host->h_addr_list; *p != NULL; p++, i++);
    addrList = makeList(taskData, i, (char*)host->h_addr_list, sizeof(char*), host, mkAddr);

    /* Make the result structure. */
    result = ALLOC(4);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(name));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(aliases));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(addrType));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(addrList));
    return result;
}

static Handle makeProtoEntry(TaskData *taskData, struct protoent *proto)
{
    int i;
    char **p;
    Handle aliases, name, protocol, result;

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, proto->p_name));

    /* Aliases. */
    for (i=0, p = proto->p_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, proto->p_aliases);

    /* Protocol number. */
    protocol = Make_unsigned(taskData, proto->p_proto);

    /* Make the result structure. */
    result = ALLOC(3);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(name));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(aliases));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(protocol));
    return result;
}

static Handle makeServEntry(TaskData *taskData, struct servent *serv)
{
    int i;
    char **p;
    Handle aliases, name, protocol, result, port;

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, serv->s_name));

    /* Aliases. */
    for (i=0, p = serv->s_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, serv->s_aliases);

    /* Port number. */
    port = Make_unsigned(taskData, ntohs(serv->s_port));

    /* Protocol name. */
    protocol = SAVE(C_string_to_Poly(taskData, serv->s_proto));

    /* Make the result structure. */
    result = ALLOC(4);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(name));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(aliases));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(port));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(protocol));
    return result;
}

static Handle makeNetEntry(TaskData *taskData, struct netent *net)
{
    int i;
    char **p;
    Handle aliases, name, addrType, result, network;

    /* Canonical name. */
    name = SAVE(C_string_to_Poly(taskData, net->n_name));

    /* Aliases. */
    for (i=0, p = net->n_aliases; *p != NULL; p++, i++);
    aliases = convert_string_list(taskData, i, net->n_aliases);

    /* Address type. */
    addrType = Make_unsigned(taskData, net->n_addrtype);

    /* Protocol name. */
    network = Make_unsigned(taskData, ntohl(net->n_net));

    /* Make the result structure. */
    result = ALLOC(4);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(name));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(aliases));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(addrType));
    DEREFHANDLE(result)->Set(3, DEREFWORDHANDLE(network));
    return result;
}

static Handle mkAftab(TaskData *taskData, void *arg, char *p)
{
    struct af_tab_struct *af = (struct af_tab_struct *)p;
    Handle result, name, num;
    /* Construct a pair of the string and the number. */
    name = SAVE(C_string_to_Poly(taskData, af->af_name));
    num = Make_unsigned(taskData, af->af_num);
    result = ALLOC(2);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(name));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(num));
    return result;
}

static Handle mkSktab(TaskData *taskData, void *arg, char *p)
{
    struct sk_tab_struct *sk = (struct sk_tab_struct *)p;
    Handle result, name, num;
    /* Construct a pair of the string and the number. */
    name = SAVE(C_string_to_Poly(taskData, sk->sk_name));
    num = Make_unsigned(taskData, sk->sk_num);
    result = ALLOC(2);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(name));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(num));
    return result;
}

/* This sets an option and can also be used to set an integer. */
static Handle setSocketOption(TaskData *taskData, Handle args, int level, int opt)
{
    PIOSTRUCT strm = get_stream(DEREFHANDLE(args)->Get(0).AsObjPtr());
    int onOff = get_C_long(taskData, DEREFHANDLE(args)->Get(1));
    if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
    if (setsockopt(strm->device.sock, level, opt,
        (char*)&onOff, sizeof(int)) != 0)
        raise_syscall(taskData, "setsockopt failed", GETERROR);
    return Make_arbitrary_precision(taskData, 0);
}

/* Get a socket option as a boolean */
static Handle getSocketOption(TaskData *taskData, Handle args, int level, int opt)
{
    PIOSTRUCT strm = get_stream(args->WordP());
    int onOff = 0;
    socklen_t size = sizeof(int);
    if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
    if (getsockopt(strm->device.sock, level, opt,
        (char*)&onOff, &size) != 0)
        raise_syscall(taskData, "getsockopt failed", GETERROR);
    return Make_arbitrary_precision(taskData, onOff == 0 ? 0 : 1);
}

/* Get a socket option as an integer */
static Handle getSocketInt(TaskData *taskData, Handle args, int level, int opt)
{
    PIOSTRUCT strm = get_stream(args->WordP());
    int optVal = 0;
    socklen_t size = sizeof(int);
    if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF);
    if (getsockopt(strm->device.sock, level, opt,
        (char*)&optVal, &size) != 0)
        raise_syscall(taskData, "getsockopt failed", GETERROR);
    return Make_arbitrary_precision(taskData, optVal);
}

/* Helper function for selectCall.  Creates the result vector of active sockets. */
static Handle getSelectResult(TaskData *taskData, Handle args, int offset, fd_set *pFds)
{
    /* Construct the result vectors. */
    PolyObject *inVec = DEREFHANDLE(args)->Get(offset).AsObjPtr();
    int nVec = OBJECT_LENGTH(inVec);
    int nRes = 0;
    int i;
    for (i = 0; i < nVec; i++) {
        PIOSTRUCT strm = get_stream(inVec->Get(i).AsObjPtr());
        if (FD_ISSET(strm->device.sock, pFds)) nRes++;
    }
    if (nRes == 0)
        return SAVE(EmptyString()); /* None - return empty vector. */
    else {
        Handle result = ALLOC(nRes);
        inVec = DEREFHANDLE(args)->Get(offset).AsObjPtr(); /* It could have moved as a result of a gc. */
        nRes = 0;
        for (i = 0; i < nVec; i++) {
            PIOSTRUCT strm = get_stream(inVec->Get(i).AsObjPtr());
            if (FD_ISSET(strm->device.sock, pFds))
                DEREFWORDHANDLE(result)->Set(nRes++, inVec->Get(i));
        }
        return result;
    }
}

/* Wrapper for "select" call.  The arguments are arrays of socket ids.  These arrays are
   updated so that "active" sockets are left unchanged and inactive sockets are set to
   minus one.  */
static Handle selectCall(TaskData *taskData, Handle args, int blockType)
{
    // We should check for interrupts even if we're not going to block.
    processes->TestAnyEvents(taskData);
    fd_set readers, writers, excepts;
    struct timeval timeout;
    int i, nVec, selectRes;
    Handle rdResult, wrResult, exResult, result;
    /* Set up the bitmaps for the select call from the arrays. */
    PolyObject *readVec = DEREFHANDLE(args)->Get(0).AsObjPtr();
    PolyObject *writeVec = DEREFHANDLE(args)->Get(1).AsObjPtr();
    PolyObject *excVec = DEREFHANDLE(args)->Get(2).AsObjPtr();
    FD_ZERO(&readers);
    FD_ZERO(&writers);
    FD_ZERO(&excepts);
    nVec = OBJECT_LENGTH(readVec);
    for (i = 0; i < nVec; i++) {
        PIOSTRUCT strm = get_stream(readVec->Get(i).AsObjPtr());
        if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF); 
        FD_SET(strm->device.sock, &readers);
    }
    nVec = OBJECT_LENGTH(writeVec);
    for (i = 0; i < nVec; i++) {
        PIOSTRUCT strm = get_stream(writeVec->Get(i).AsObjPtr());
        if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF); 
        FD_SET(strm->device.sock, &writers);
    }
    nVec = OBJECT_LENGTH(excVec);
    for (i = 0; i < nVec; i++) {
        PIOSTRUCT strm = get_stream(excVec->Get(i).AsObjPtr());
        if (strm == NULL) raise_syscall(taskData, "Stream is closed", EBADF); 
        FD_SET(strm->device.sock, &excepts);
    }
    /* Whatever the timeout specified we simply poll here. */
    memset(&timeout, 0, sizeof(timeout));
    selectRes = select(FD_SETSIZE, &readers, &writers, &excepts, &timeout);
    if (selectRes < 0) raise_syscall(taskData, "select failed", GETERROR);

    if (selectRes == 0) { /* Timed out.  Have to look at the timeout value. */
        switch (blockType)
        {
        case 0: /* Check the timeout. */
            {
            /* The time argument is an absolute time. */
#ifdef WINDOWS_PC
            FILETIME ftTime, ftNow;
            /* Get the file time. */
            get_C_pair(taskData, DEREFHANDLE(args)->Get(3),
                &ftTime.dwHighDateTime, &ftTime.dwLowDateTime);
			GetSystemTimeAsFileTime(&ftNow);
            /* If the timeout time is earlier than the current time
               we must return, otherwise we block. */
            if (CompareFileTime(&ftTime, &ftNow) <= 0)
                break; /* Return the empty set. */
            /* else drop through and block. */
#else /* Unix */
            struct timeval tv;
            /* We have a value in microseconds.  We need to split
               it into seconds and microseconds. */
            Handle hTime = SAVE(DEREFWORDHANDLE(args)->Get(3));
            Handle hMillion = Make_arbitrary_precision(taskData, 1000000);
            unsigned long secs =
                get_C_ulong(taskData, DEREFWORDHANDLE(div_longc(taskData, hMillion, hTime)));
            unsigned long usecs =
                get_C_ulong(taskData, DEREFWORDHANDLE(rem_longc(taskData, hMillion, hTime)));
            /* If the timeout time is earlier than the current time
               we must return, otherwise we block. */
            if (gettimeofday(&tv, NULL) != 0)
                raise_syscall(taskData, "gettimeofday failed", errno);
            if ((unsigned long)tv.tv_sec > secs ||
                ((unsigned long)tv.tv_sec == secs && (unsigned long)tv.tv_usec >= usecs))
                break;
            /* else block. */
#endif
        }
        case 1: /* Block until one of the descriptors is ready. */
            processes->BlockAndRestart(taskData, NULL, false, POLY_SYS_network);
            /*NOTREACHED*/
        case 2: /* Just a simple poll - drop through. */
            break;
        }
    }

    /* Construct the result vectors. */
    rdResult = getSelectResult(taskData, args, 0, &readers);
    wrResult = getSelectResult(taskData, args, 1, &writers);
    exResult = getSelectResult(taskData, args, 2, &excepts);

    result = ALLOC(3);
    DEREFHANDLE(result)->Set(0, DEREFWORDHANDLE(rdResult));
    DEREFHANDLE(result)->Set(1, DEREFWORDHANDLE(wrResult));
    DEREFHANDLE(result)->Set(2, DEREFWORDHANDLE(exResult));
    return result;
}

class Networking: public RtsModule
{
public:
    virtual void Init(void);
    virtual void Uninit(void);
};

// Declare this.  It will be automatically added to the table.
static Networking networkingModule;

void Networking::Init(void)
{
#ifdef WINDOWS_PC
#ifdef USEWINSOCK2
#define WINSOCK_MAJOR_VERSION   2
#define WINSOCK_MINOR_VERSION   2
#else
#define WINSOCK_MAJOR_VERSION   1
#define WINSOCK_MINOR_VERSION   1
#endif
    WSADATA wsaData;
    WORD wVersion = MAKEWORD(WINSOCK_MINOR_VERSION, WINSOCK_MAJOR_VERSION);
    /* Initialise the system and check that the version it supplied
       is the one we requested. */
    if(WSAStartup(wVersion, &wsaData) == 0)
    {
        if (wsaData.wVersion == wVersion)
            winsock_init = 1;
        else WSACleanup();
    }
#endif
}

void Networking::Uninit(void)
{
#ifdef WINDOWS_PC
    if (winsock_init) WSACleanup();
    winsock_init = 0;
#endif
}
