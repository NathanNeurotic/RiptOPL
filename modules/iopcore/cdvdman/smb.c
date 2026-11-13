/*
  Copyright 2009, jimmikaelkael
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
   */

#include <stdio.h>
#include <errno.h>
#include <sysclib.h>
#include "smstcpip.h"
#include <limits.h>
#include <thbase.h>
#include <thsemap.h>
#include <intrman.h>
#include <sifman.h>

#include "oplsmb.h"
#include "smb.h"
#include "cdvd_config.h"

#include "smsutils.h"

#define USE_CUSTOM_RECV 1

// The NT status of a response (SMB_FLAGS2_32BIT_STATUS): Eclass holds its low 16 bits and Ecode its
// high 16 bits. Both are signed shorts, so each is taken unsigned first: shifting a negative Ecode
// (every error status) is undefined in C, and a negative Eclass would set the high bits.
#define SMB_STATUS(h) ((u32)(u16)(h).Eclass | ((u32)(u16)(h).Ecode << 16))

//Round up the erasure amount, so that memset can erase memory word-by-word.
#define ZERO_PKT_ALIGNED(hdr, hdrSize) memset((hdr), 0, ((hdrSize) + 3) & ~3)

/* Limit the maximum chunk size of receiving operations, to avoid triggering the congestion avoidance algorithm of the SMB server.
   This is because the IOP cannot clear the received frames fast enough, causing the number of bytes in flight to grow exponentially.
   The TCP congestion avoidence algorithm may induce some latency, causing extremely poor performance.
   The value to use should be smaller than the TCP window size. Right now, it is 10240 (according to lwipopts.h). */
#define CLIENT_MAX_BUFFER_SIZE 8192      //Allow up to 8192 bytes to be received.
#define CLIENT_MAX_XMIT_SIZE   USHRT_MAX //Allow up to 65535 bytes to be transmitted.
#define CLIENT_MAX_RECV_SIZE   8192      //Allow up to 8192 bytes to be received.

int smb_io_sema = -1;

#define WAITIOSEMA(x)   WaitSema(x)
#define SIGNALIOSEMA(x) SignalSema(x)

// !!! ps2ip exports functions pointers !!!
extern int (*plwip_close)(int s);                                                                                                                 // #6
extern int (*plwip_connect)(int s, struct sockaddr *name, socklen_t namelen);                                                                     // #7
extern int (*plwip_recv)(int s, void *mem, int len, unsigned int flags);                                                                          // #9
extern int (*plwip_recvfrom)(int s, void *mem, int hlen, void *payload, int plen, unsigned int flags, struct sockaddr *from, socklen_t *fromlen); // #10
extern int (*plwip_send)(int s, void *dataptr, int size, unsigned int flags);                                                                     // #11
extern int (*plwip_socket)(int domain, int type, int protocol);                                                                                   // #13
extern int (*plwip_setsockopt)(int s, int level, int optname, const void *optval, socklen_t optlen);                                              // #19
extern u32 (*pinet_addr)(const char *cp);                                                                                                         // #24

extern struct cdvdman_settings_smb cdvdman_settings;

// Local function prototypes
static void nb_SetSessionMessage(u32 size);  // Write Session Service message
static int nb_GetSessionMessageLength(void); // Read Session Service message length
static u8 nb_GetPacketType(void);            // Read message type

static server_specs_t server_specs;

#define LM_AUTH   0
#define NTLM_AUTH 1

static u16 UID, TID;
static int main_socket = -1;

static struct
{
    //Direct transport packet header. This is also a NetBIOS session header.
    u32 sessionHeader; //The lower 24 bytes are the length of the payload in network byte-order, while the upper 8 bits must be set to 0 (Session Message Packet).
    union
    {
        u8 u8buff[MAX_SMB_BUF + MAX_SMB_BUF_HDR];
        u16 u16buff[(MAX_SMB_BUF + MAX_SMB_BUF_HDR) / sizeof(u16)];
        s16 s16buff[(MAX_SMB_BUF + MAX_SMB_BUF_HDR) / sizeof(s16)];
        NegotiateProtocolRequest_t negotiateProtocolRequest;
        NegotiateProtocolResponse_t negotiateProtocolResponse;
        SessionSetupAndXRequest_t sessionSetupAndXRequest;
        SessionSetupAndXResponse_t sessionSetupAndXResponse;
        TreeConnectAndXRequest_t treeConnectAndXRequest;
        TreeConnectAndXResponse_t treeConnectAndXResponse;
        OpenAndXRequest_t openAndXRequest;
        OpenAndXResponse_t openAndXResponse;
        ReadAndXRequest_t readAndXRequest;
        ReadAndXResponse_t readAndXResponse;
        WriteAndXRequest_t writeAndXRequest;
        WriteAndXResponse_t writeAndXResponse;
        CloseRequest_t closeRequest;
        CloseResponse_t closeResponse;
    } smb;
} __attribute__((packed)) SMB_buf;

//-------------------------------------------------------------------------
static void nb_SetSessionMessage(u32 size) // Write Session Service header: careful it's raw TCP transport here and not NBT transport
{
    // maximum for raw TCP transport (24 bits) !!!
    // Byte-swap length into network byte-order.
    SMB_buf.sessionHeader = ((size & 0xff0000) >> 8) | ((size & 0xff00) << 8) | ((size & 0xff) << 24);
}

//-------------------------------------------------------------------------
static int nb_GetSessionMessageLength(void) // Read Session Service header length: careful it's raw TCP transport here and not NBT transport
{
    u32 size;

    // maximum for raw TCP transport (24 bits) !!!
    // Byte-swap length from network byte-order.
    size = ((SMB_buf.sessionHeader << 8) & 0xff0000) | ((SMB_buf.sessionHeader >> 8) & 0xff00) | ((SMB_buf.sessionHeader >> 24) & 0xff);

    return (int)size;
}

static u8 nb_GetPacketType(void) // Read Session Service header type.
{
    // Byte-swap length from network byte-order.
    return ((u8)(SMB_buf.sessionHeader & 0xff));
}

//-------------------------------------------------------------------------
int OpenTCPSession(struct in_addr dst_IP, u16 dst_port)
{
    register int sock, ret;
    int opt;
    struct sockaddr_in sock_addr;

    // Creating socket
    sock = plwip_socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
        return -2;

    opt = 1;
    plwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char *)&opt, sizeof(opt));

    memset(&sock_addr, 0, sizeof(sock_addr));
    sock_addr.sin_addr = dst_IP;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = htons(dst_port);

    // With LWIP_TCPIP_CORE_LOCKING (the shipped SMSTCPIP), connect returns once the SYN is queued;
    // lwIP retransmits a lost SYN. smb_NegotiateProtocol opens a fresh socket when no reply comes.
    ret = plwip_connect(sock, (struct sockaddr *)&sock_addr, sizeof(sock_addr));
    if (ret < 0) {
        plwip_close(sock);
        return -1;
    }

    return sock;
}

//-------------------------------------------------------------------------
static int SendData(int sock, char *buf, int size)
{
    int remaining, result;
    char *ptr;

    ptr = buf;
    remaining = size;
    while (remaining > 0) {
        result = plwip_send(sock, ptr, remaining, 0);
        if (result <= 0)
            return result;

        ptr += result;
        remaining -= result;
    }

    return size;
}

// SMSTCPIP exports no select/ioctl and does not implement socket read timeouts.
// Its recvfrom(MSG_DONTWAIT) is supported, including the SMB-specific split-buffer path.
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40 // SMSTCPIP/include/lwip/sockets.h, not menu ps2ip's value
#endif
#define SMB_REPLY_TIMEOUT_TICKS    368640000U // 10 seconds at the IOP 36.864 MHz clock
// Negotiation at launch: up to 4 attempts of 10 s, 1 s apart (about 43 s) before smbinit fails.
#define SMB_CONNECT_ATTEMPTS       4
#define SMB_CONNECT_RETRY_DELAY_US 1000000

static iop_sys_clock_t smb_reply_start;

// Where the socket stands in the reply stream. A reply that misses its deadline is not a dead
// session: the server is often only slow (a disk spinning up). So a timeout keeps the session, and
// the next request first takes the rest of that late reply off the socket (smb_Resync), instead
// of closing the session and failing every read and VMC write until the next launch.
static struct
{
    u8 hdr[4];     // NBSS header of the message being received
    int hdr_got;   // bytes of hdr received (0 to 4)
    int body_left; // with hdr_got == 4: body bytes of that message still on the socket
    int open;      // the last request's reply (an NBSS session message) is not fully received
    int timed_out; // the last receive stopped at its deadline, with the session still up
    int idle;      // drains in a row that timed out without one byte arriving
} smb_rx;

#define SMB_DRAIN_IDLE_LIMIT 3 // a server silent for three drain deadlines in a row is gone

static void smb_StartDeadline(void)
{
    GetSystemTime(&smb_reply_start);
}

static void smb_BeginReply(void)
{
    smb_StartDeadline();
    smb_rx.open = 1;
}

static void smb_ResetStream(void)
{
    memset(&smb_rx, 0, sizeof(smb_rx));
}

static int smb_ReplyExpired(void)
{
    iop_sys_clock_t now;
    GetSystemTime(&now);
    return (u32)(now.lo - smb_reply_start.lo) >= SMB_REPLY_TIMEOUT_TICKS;
}

// Byte j of the n bytes one recvfrom() call took off the stream, or -1 when it was not stored.
// With hlen == 0 they are all in payload. The split call (lwip_recvfrom in SMSTCPIP sockets.c)
// puts a segment shorter than 63 bytes, or one whose data starts past its end, into header whole.
// Otherwise header gets the first 63 bytes and payload the bytes from header[hlen] + 4 on.
static int smb_StreamByte(const u8 *header, int hlen, const u8 *payload, int n, int j)
{
    int index;

    if (hlen == 0)
        return payload[j];
    if (n < 63)
        return header[j];
    index = header[hlen] + 4;
    if (index > n)
        return header[j];
    if (j >= index)
        return payload[j - index];
    return j < 63 ? header[j] : -1;
}

// Follow the NBSS messages in n received bytes, so that a late reply can be taken off whole.
static int smb_TrackStream(const u8 *header, int hlen, const u8 *payload, int n)
{
    int pos = 0, b, take;

    while (pos < n) {
        if (smb_rx.hdr_got < 4) {
            b = smb_StreamByte(header, hlen, payload, n, pos);
            if (b < 0)
                return -1;
            smb_rx.hdr[smb_rx.hdr_got++] = (u8)b;
            pos++;
            if (smb_rx.hdr_got < 4)
                continue;
            // Raw TCP transport: a 24-bit length, as nb_GetSessionMessageLength reads it.
            smb_rx.body_left = (smb_rx.hdr[1] << 16) | (smb_rx.hdr[2] << 8) | smb_rx.hdr[3];
        } else {
            take = n - pos < smb_rx.body_left ? n - pos : smb_rx.body_left;
            pos += take;
            smb_rx.body_left -= take;
        }
        if (smb_rx.body_left == 0) {
            smb_rx.hdr_got = 0;
            if (smb_rx.hdr[0] == 0) // a session message; keep-alives (0x85) are not the reply
                smb_rx.open = 0;
        }
    }
    return 0;
}

// Every piece of one SMB response shares the same deadline, including keep-alives
// and partial TCP segments. The custom header/payload receive path is preserved.
static int smb_RecvTimed(int sock, void *header, int hlen, void *payload, int plen)
{
    int result, polls = 0;
    if (sock < 0)
        return -2;
    for (;;) {
        if (smb_ReplyExpired()) {
            // The stream position is kept: the next request takes the late reply off first.
            smb_rx.timed_out = 1;
            return -2;
        }
        result = plwip_recvfrom(sock, header, hlen, payload, plen, MSG_DONTWAIT, NULL, NULL);
        if (result == 0) {
            smb_Disconnect(); // EOF: do not leave a dead socket in service.
            return 0;
        }
        if (result > 0) {
            if (smb_TrackStream(header, hlen, payload, result) < 0) {
                smb_Disconnect(); // A message boundary that cannot be followed: the stream is lost.
                return -2;
            }
            return result;
        }
        /* A blocking receive, as upstream OPL's, wakes when the next segment arrives. A read reply comes as
           several TCP segments a fraction of a millisecond apart, so sleeping 1 ms before every retry added
           about a millisecond to each 8 KB read (#399). Poll every 100 us for the first 2 ms, then every 1 ms
           once the reply is late (a server disk spinning up), so a slow server costs no more wake-ups. */
        if (DelayThread(polls++ < 20 ? 100 : 1000) < 0) {
            // Never busy-spin in the CDVD thread when the IOP cannot sleep.
            smb_Disconnect();
            return -2;
        }
    }
}

static int smb_InStep(void)
{
    return !smb_rx.open && smb_rx.hdr_got == 0 && smb_rx.body_left == 0;
}

// Before a request is sent: take the rest of any late reply off the socket, so that this request
// reads its own reply. Returns 0 when in step, or < 0 (the request must not be sent).
static int smb_Resync(void)
{
    static u8 scratch[256]; // SMB_buf may already hold the request being sent
    int n, got = 0, r;

    smb_rx.timed_out = 0;
    if (main_socket < 0)
        return -1;
    if (smb_InStep())
        return 0;

    smb_StartDeadline();
    while (!smb_InStep()) {
        // Ask for no more than the late message holds, so that nothing after it is taken.
        if (smb_rx.hdr_got < 4)
            n = 4 - smb_rx.hdr_got;
        else
            n = smb_rx.body_left < (int)sizeof(scratch) ? smb_rx.body_left : (int)sizeof(scratch);
        r = smb_RecvTimed(main_socket, NULL, 0, scratch, n);
        if (r <= 0) {
            if (smb_rx.timed_out && !got && ++smb_rx.idle >= SMB_DRAIN_IDLE_LIMIT) {
                smb_Disconnect(); // Bounded: a reply that never comes cannot stall every request.
                smb_rx.timed_out = 0;
            }
            return -2;
        }
        got += r;
    }
    smb_rx.idle = 0;
    return 0;
}

// A request whose reply missed its deadline, on a session that is still up, is sent again: the late
// reply is taken off first. SMB reads, writes at an offset, opens and session/tree setups can repeat.
#define SMB_REQUEST_ATTEMPTS 3

static int smb_RetryRequest(int attempt)
{
    return smb_rx.timed_out && main_socket >= 0 && attempt + 1 < SMB_REQUEST_ATTEMPTS;
}

static int RecvData(int sock, char *buf, int size)
{
    int remaining = size, result;
    char *ptr = buf;

    while (remaining > 0) {
        result = smb_RecvTimed(sock, NULL, 0, ptr, remaining);
        if (result <= 0)
            return result;
        ptr += result;
        remaining -= result;
    }
    return size;
}

//-------------------------------------------------------------------------
static int GetSMBServerReply(int shdrlen, void *spayload, int rhdrlen)
{
    int rcv_size, totalpkt_size, size;

    if (smb_Resync() < 0)
        return -2;

    totalpkt_size = nb_GetSessionMessageLength() + 4;

    if (shdrlen == 0) {
        //Send the whole message, including the 4-byte direct transport packet header.
        rcv_size = SendData(main_socket, (char *)&SMB_buf, totalpkt_size);
        if (rcv_size <= 0)
            return -1;
    } else {
        size = shdrlen + 4;

        //Send the headers, followed by the payload.
        rcv_size = SendData(main_socket, (char *)&SMB_buf, size);
        if (rcv_size <= 0)
            return -1;

        rcv_size = SendData(main_socket, spayload, totalpkt_size - size);
        if (rcv_size <= 0)
            return -1;
    }

    smb_BeginReply(); // A slow request send must not consume the response budget.
    //Read NetBIOS session message header. Drop NBSS Session Keep alive messages (type == 0x85, with no body), but process session messages (type == 0x00).
    do {
        rcv_size = RecvData(main_socket, (char *)&SMB_buf.sessionHeader, sizeof(SMB_buf.sessionHeader));
        if (rcv_size <= 0)
            return -2;
    } while (nb_GetPacketType() != 0);

    totalpkt_size = nb_GetSessionMessageLength();

    // Only READ_ANDX receives a partial header here; its payload is read separately.
    // Reject untrusted NBSS lengths before writing into this fixed IOP buffer.
    size = (rhdrlen == 0) ? totalpkt_size : rhdrlen;
    if (totalpkt_size < (int)sizeof(SMBHeader_t) || size > (int)sizeof(SMB_buf.smb) || size > totalpkt_size) {
        smb_Disconnect(); // The invalid frame cannot be left on a reusable stream.
        return -2;
    }
    rcv_size = RecvData(main_socket, (char *)&SMB_buf.smb, size);
    if (rcv_size <= 0)
        return -2;

    return totalpkt_size;
}

//-------------------------------------------------------------------------
//These functions will process UTF-16 characters on a byte-level, so that they will be safe for use with byte-alignment.
static int asciiToUtf16(char *out, const char *in)
{
    int len;
    const char *pIn;
    char *pOut;

    for (pIn = in, pOut = out, len = 0; *pIn != '\0'; pIn++, pOut += 2, len += 2) {
        pOut[0] = *pIn;
        pOut[1] = '\0';
    }

    pOut[0] = '\0'; //NULL terminate.
    pOut[1] = '\0';
    len += 2;

    return len;
}

static int setStringField(char *out, const char *in)
{
    int len;

    if (server_specs.Capabilities & SERVER_CAP_UNICODE) {
        len = asciiToUtf16(out, in);
    } else {
        len = strlen(in) + 1;
        strcpy(out, in);
    }

    return len;
}

//-------------------------------------------------------------------------
int smb_NegotiateProtocol(char *SMBServerIP, int SMBServerPort, char *Username, char *Password, u32 *capabilities, OplSmbPwHashFunc_t hash_callback)
{
    char *dialect = "NT LM 0.12";
    NegotiateProtocolRequest_t *NPR = &SMB_buf.smb.negotiateProtocolRequest;
    NegotiateProtocolResponse_t *NPRsp = &SMB_buf.smb.negotiateProtocolResponse;
    register int length;
    int reply_len, attempt;
    struct in_addr dst_addr;
    iop_sema_t smp;

    smp.initial = 1;
    smp.max = 1;
    smp.option = 0;
    smp.attr = 1;
    smb_io_sema = CreateSema(&smp);
    if (smb_io_sema < 0)
        return -1;

    dst_addr.s_addr = pinet_addr(SMBServerIP);

    // The first SYN to a host not yet in the ARP cache is dropped (ARP_QUEUEING is off), and the
    // server may still be waking up. A lost packet must not end the launch on the red screen, so
    // each attempt uses a fresh socket (and a fresh SYN) with its own reply deadline.
    for (attempt = 0;; attempt++) {
        if (attempt) {
            smb_Disconnect();
            if (attempt >= SMB_CONNECT_ATTEMPTS)
                goto negotiate_failed;
            DelayThread(SMB_CONNECT_RETRY_DELAY_US);
        }

        main_socket = OpenTCPSession(dst_addr, SMBServerPort);
        if (main_socket < 0)
            continue;

        ZERO_PKT_ALIGNED(NPR, sizeof(NegotiateProtocolRequest_t));

        NPR->smbH.Magic = SMB_MAGIC;
        NPR->smbH.Cmd = SMB_COM_NEGOTIATE;
        NPR->smbH.Flags = SMB_FLAGS_CASELESS_PATHNAMES;
        NPR->smbH.Flags2 = SMB_FLAGS2_KNOWS_LONG_NAMES;
        length = strlen(dialect);
        NPR->ByteCount = length + 2;
        NPR->DialectFormat = 0x02;
        strcpy(NPR->DialectName, dialect);

        nb_SetSessionMessage(sizeof(NegotiateProtocolRequest_t) + length + 1);
        reply_len = GetSMBServerReply(0, NULL, 0);
        if (reply_len >= (int)sizeof(NegotiateProtocolResponse_t) &&
            NPRsp->smbH.Magic == SMB_MAGIC && NPRsp->smbH.Eclass == STATUS_SUCCESS &&
            NPRsp->smbWordcount == 17 && NPRsp->KeyLength <= sizeof(server_specs.EncryptionKey) &&
            NPRsp->ByteCount >= NPRsp->KeyLength &&
            reply_len >= (int)sizeof(NegotiateProtocolResponse_t) + NPRsp->ByteCount)
            break;
    }

    *capabilities = NPRsp->Capabilities & (CLIENT_CAP_LARGE_WRITEX | CLIENT_CAP_LARGE_READX | CLIENT_CAP_UNICODE | CLIENT_CAP_LARGE_FILES | CLIENT_CAP_STATUS32);

    server_specs.Capabilities = NPRsp->Capabilities;
    server_specs.SecurityMode = (NPRsp->SecurityMode & NEGOTIATE_SECURITY_USER_LEVEL) ? SERVER_USER_SECURITY_LEVEL : SERVER_SHARE_SECURITY_LEVEL;
    server_specs.PasswordType = (NPRsp->SecurityMode & NEGOTIATE_SECURITY_CHALLENGE_RESPONSE) ? SERVER_USE_ENCRYPTED_PASSWORD : SERVER_USE_PLAINTEXT_PASSWORD;

    // copy to global struct to keep needed information for further communication
    server_specs.MaxBufferSize = NPRsp->MaxBufferSize;
    server_specs.MaxMpxCount = NPRsp->MaxMpxCount;
    server_specs.SessionKey = NPRsp->SessionKey;
    memcpy(server_specs.EncryptionKey, &NPRsp->ByteField[0], NPRsp->KeyLength);
    memset(server_specs.PrimaryDomainServerName, 0, sizeof(server_specs.PrimaryDomainServerName));
    {
        unsigned int domain_len = NPRsp->ByteCount - NPRsp->KeyLength;
        if (domain_len >= sizeof(server_specs.PrimaryDomainServerName))
            domain_len = sizeof(server_specs.PrimaryDomainServerName) - 1;
        memcpy(server_specs.PrimaryDomainServerName, &NPRsp->ByteField[NPRsp->KeyLength], domain_len);
    }
    strncpy(server_specs.Username, Username, sizeof(server_specs.Username));
    server_specs.Username[sizeof(server_specs.Username) - 1] = '\0';
    strncpy(server_specs.Password, Password, sizeof(server_specs.Password));
    server_specs.Password[sizeof(server_specs.Password) - 1] = '\0';
    server_specs.IOPaddr = (void *)&server_specs;
    server_specs.HashedFlag = (server_specs.PasswordType == SERVER_USE_ENCRYPTED_PASSWORD) ? 0 : -1;

    hash_callback(&server_specs);

    return 1;

negotiate_failed:
    smb_Disconnect();
    DeleteSema(smb_io_sema);
    smb_io_sema = -1;
    return -1;
}

//-------------------------------------------------------------------------
static int smb_SessionSetupAndXOnce(u32 capabilities)
{
    SessionSetupAndXRequest_t *SSR = &SMB_buf.smb.sessionSetupAndXRequest;
    SessionSetupAndXResponse_t *SSRsp = &SMB_buf.smb.sessionSetupAndXResponse;
    register int offset, useUnicode;
    int AuthType = NTLM_AUTH;
    int password_len = 0;
    int reply_len;

lbl_session_setup:
    ZERO_PKT_ALIGNED(SSR, sizeof(SessionSetupAndXRequest_t));

    useUnicode = (server_specs.Capabilities & SERVER_CAP_UNICODE) ? 1 : 0;

    SSR->smbH.Magic = SMB_MAGIC;
    SSR->smbH.Cmd = SMB_COM_SESSION_SETUP_ANDX;
    SSR->smbH.Flags = SMB_FLAGS_CASELESS_PATHNAMES;
    SSR->smbH.Flags2 = SMB_FLAGS2_KNOWS_LONG_NAMES | SMB_FLAGS2_32BIT_STATUS;
    if (server_specs.Capabilities & SERVER_CAP_UNICODE)
        SSR->smbH.Flags2 |= SMB_FLAGS2_UNICODE_STRING;
    SSR->smbWordcount = 13;
    SSR->smbAndxCmd = SMB_COM_NONE; // no ANDX command
    SSR->MaxBufferSize = CLIENT_MAX_BUFFER_SIZE;
    SSR->MaxMpxCount = server_specs.MaxMpxCount >= 2 ? 2 : (u16)server_specs.MaxMpxCount;
    SSR->VCNumber = 1;
    SSR->SessionKey = server_specs.SessionKey;
    SSR->Capabilities = capabilities;

    // Fill ByteField
    offset = 0;

    if (server_specs.SecurityMode == SERVER_USER_SECURITY_LEVEL) {
        password_len = server_specs.PasswordLen;
        // Copy the password accordingly to auth type
        memcpy(&SSR->ByteField[offset], &server_specs.Password[(AuthType << 4) + (AuthType << 3)], password_len);
        // fill SSR->AnsiPasswordLength or SSR->UnicodePasswordLength accordingly to auth type
        if (AuthType == LM_AUTH)
            SSR->AnsiPasswordLength = password_len;
        else
            SSR->UnicodePasswordLength = password_len;
        offset += password_len;
    }

    if ((useUnicode) && (!(password_len & 1))) {
        SSR->ByteField[offset] = '\0';
        offset++; // pad needed only for unicode as aligment fix if password length is even
    }

    // Add User name
    offset += setStringField((char *)&SSR->ByteField[offset], server_specs.Username);

    // PrimaryDomain, acquired from Negotiate Protocol Response data
    offset += setStringField((char *)&SSR->ByteField[offset], server_specs.PrimaryDomainServerName);

    // NativeOS
    if (useUnicode && ((offset & 1) != 0)) {
        //If Unicode is used, the field must begin on a 16-bit aligned address.
        SSR->ByteField[offset] = '\0';
        offset++;
    }
    offset += setStringField((char *)&SSR->ByteField[offset], "PlayStation 2");

    // NativeLanMan
    if (useUnicode && ((offset & 1) != 0)) {
        //If Unicode is used, the field must begin on a 16-bit aligned address.
        SSR->ByteField[offset] = '\0';
        offset++;
    }
    offset += setStringField((char *)&SSR->ByteField[offset], "SMBMAN");

    SSR->ByteCount = offset;

    nb_SetSessionMessage(sizeof(SessionSetupAndXRequest_t) + offset);
    reply_len = GetSMBServerReply(0, NULL, 0);
    if (reply_len < (int)sizeof(SMBHeader_t))
        return -2;

    // check sanity of SMB header
    if (SSRsp->smbH.Magic != SMB_MAGIC)
        return -1;

    // check there's no auth failure
    if ((server_specs.SecurityMode == SERVER_USER_SECURITY_LEVEL) && (SMB_STATUS(SSRsp->smbH) == STATUS_LOGON_FAILURE) && (AuthType == NTLM_AUTH)) {
        AuthType = LM_AUTH;
        goto lbl_session_setup;
    }

    // check there's no error (NT STATUS error type!)
    if ((SSRsp->smbH.Eclass | SSRsp->smbH.Ecode) != STATUS_SUCCESS)
        return -1000;

    if (reply_len < (int)sizeof(SessionSetupAndXResponse_t))
        return -1;

    // keep UID
    UID = SSRsp->smbH.UID;

    return 1;
}

//-------------------------------------------------------------------------
static int smb_TreeConnectAndXOnce(char *ShareName)
{
    TreeConnectAndXRequest_t *TCR = &SMB_buf.smb.treeConnectAndXRequest;
    TreeConnectAndXResponse_t *TCRsp = &SMB_buf.smb.treeConnectAndXResponse;
    register int offset;
    int AuthType = NTLM_AUTH;
    int password_len = 0;
    int reply_len;

    ZERO_PKT_ALIGNED(TCR, sizeof(TreeConnectAndXRequest_t));

    TCR->smbH.Magic = SMB_MAGIC;
    TCR->smbH.Cmd = SMB_COM_TREE_CONNECT_ANDX;
    TCR->smbH.Flags = SMB_FLAGS_CASELESS_PATHNAMES;
    TCR->smbH.Flags2 = SMB_FLAGS2_KNOWS_LONG_NAMES | SMB_FLAGS2_32BIT_STATUS;
    if (server_specs.Capabilities & SERVER_CAP_UNICODE)
        TCR->smbH.Flags2 |= SMB_FLAGS2_UNICODE_STRING;
    TCR->smbH.UID = UID;
    TCR->smbWordcount = 4;
    TCR->smbAndxCmd = SMB_COM_NONE; // no ANDX command

    // Fill ByteField
    offset = 0;

    password_len = 1;
    if (server_specs.SecurityMode == SERVER_SHARE_SECURITY_LEVEL) {
        password_len = server_specs.PasswordLen;
        // Copy the password accordingly to auth type
        memcpy(&TCR->ByteField[offset], &server_specs.Password[(AuthType << 4) + (AuthType << 3)], password_len);
    }
    TCR->PasswordLength = password_len;
    offset += password_len;

    if ((server_specs.Capabilities & SERVER_CAP_UNICODE) && (!(password_len & 1))) {
        TCR->ByteField[offset] = '\0';
        offset++; // pad needed only for unicode as aligment fix is password len is even
    }

    // Add share name
    offset += setStringField((char *)&TCR->ByteField[offset], ShareName);

    memcpy(&TCR->ByteField[offset], "?????\0", 6); // Service, any type of device
    offset += 6;

    TCR->ByteCount = offset;

    nb_SetSessionMessage(sizeof(TreeConnectAndXRequest_t) + offset);
    reply_len = GetSMBServerReply(0, NULL, 0);
    if (reply_len < (int)sizeof(SMBHeader_t))
        return -2;

    // check sanity of SMB header
    if (TCRsp->smbH.Magic != SMB_MAGIC)
        return -1;

    // check there's no error (NT STATUS error type!)
    if ((TCRsp->smbH.Eclass | TCRsp->smbH.Ecode) != STATUS_SUCCESS)
        return -1000;

    if (reply_len < (int)sizeof(TreeConnectAndXResponse_t))
        return -1;

    // keep TID
    TID = TCRsp->smbH.TID;

    return 1;
}

//-------------------------------------------------------------------------
int smb_SessionSetupAndX(u32 capabilities)
{
    int result, attempt;

    for (attempt = 0;; attempt++) {
        result = smb_SessionSetupAndXOnce(capabilities);
        if (result > 0 || !smb_RetryRequest(attempt))
            return result;
    }
}

int smb_TreeConnectAndX(char *ShareName)
{
    int result, attempt;

    for (attempt = 0;; attempt++) {
        result = smb_TreeConnectAndXOnce(ShareName);
        if (result > 0 || !smb_RetryRequest(attempt))
            return result;
    }
}

//-------------------------------------------------------------------------
// Called with smb_io_sema held.
static int smb_OpenAndXOnce(char *filename, u8 *FID, int Write)
{
    OpenAndXRequest_t *OR = &SMB_buf.smb.openAndXRequest;
    OpenAndXResponse_t *ORsp = &SMB_buf.smb.openAndXResponse;
    register int offset;
    int reply_len;

    ZERO_PKT_ALIGNED(OR, sizeof(OpenAndXRequest_t));

    OR->smbH.Magic = SMB_MAGIC;
    OR->smbH.Cmd = SMB_COM_OPEN_ANDX;
    OR->smbH.Flags = SMB_FLAGS_CANONICAL_PATHNAMES;
    OR->smbH.Flags2 = SMB_FLAGS2_KNOWS_LONG_NAMES | SMB_FLAGS2_32BIT_STATUS;
    if (server_specs.Capabilities & SERVER_CAP_UNICODE)
        OR->smbH.Flags2 |= SMB_FLAGS2_UNICODE_STRING;
    OR->smbH.UID = UID;
    OR->smbH.TID = TID;
    OR->smbWordcount = 15;
    OR->smbAndxCmd = SMB_COM_NONE; // no ANDX command
    // Access mode 0 read / 2 read-write, sharing mode 4 (0x40) deny none. The default, compatibility
    // mode, makes a Windows server deny every other reader of a non-.exe file (MS-CIFS appendix A).
    // A console switched off or reset mid-game never closes its VMC, so that handle outlives the
    // session on the server and the menu could not open the card again until the server dropped it.
    OR->AccessMask = (Write ? 2 : 0) | 0x40;
    OR->FileAttributes = Write ? EXT_ATTR_NORMAL : EXT_ATTR_READONLY;
    OR->CreateOptions = 1;

    offset = 0;
    if (server_specs.Capabilities & SERVER_CAP_UNICODE) {
        OR->ByteField[offset] = '\0';
        offset++; // pad needed only for unicode as aligment fix
    }

    // Add filename
    offset += setStringField((char *)&OR->ByteField[offset], filename);
    OR->ByteCount = offset;

    nb_SetSessionMessage(sizeof(OpenAndXRequest_t) + offset + 1);
    reply_len = GetSMBServerReply(0, NULL, 0);
    if (reply_len < (int)sizeof(SMBHeader_t))
        return -2;

    // check sanity of SMB header
    if (ORsp->smbH.Magic != SMB_MAGIC)
        return -1;

    // check there's no error
    if (ORsp->smbH.Eclass != STATUS_SUCCESS)
        return -1000;

    if (reply_len < (int)sizeof(OpenAndXResponse_t))
        return -1;
    memcpy(FID, &ORsp->FID, 2);

    return 1;
}

int smb_OpenAndX(char *filename, u8 *FID, int Write)
{
    int result, attempt;

    WAITIOSEMA(smb_io_sema);
    for (attempt = 0;; attempt++) {
        result = smb_OpenAndXOnce(filename, FID, Write);
        if (result > 0 || !smb_RetryRequest(attempt))
            break;
    }
    SIGNALIOSEMA(smb_io_sema);

    return result;
}

//-------------------------------------------------------------------------
//Do not call WaitSema() from this function because it would have been already called.
int smb_Close(int FID)
{
    int r;
    CloseRequest_t *CR = &SMB_buf.smb.closeRequest;
    CloseResponse_t *CRsp = &SMB_buf.smb.closeResponse;

    //  WAITIOSEMA(smb_io_sema);

    ZERO_PKT_ALIGNED(CR, sizeof(CloseRequest_t));

    CR->smbH.Magic = SMB_MAGIC;
    CR->smbH.Cmd = SMB_COM_CLOSE;
    CR->smbH.Flags = SMB_FLAGS_CANONICAL_PATHNAMES;
    CR->smbH.Flags2 = SMB_FLAGS2_KNOWS_LONG_NAMES | SMB_FLAGS2_32BIT_STATUS;
    CR->smbH.UID = (u16)UID;
    CR->smbH.TID = (u16)TID;
    CR->smbWordcount = 3;
    CR->FID = (u16)FID;

    // Closing runs at shutdown and IGR: do not wait out a late reply for it. Ending the session
    // closes the files on the server too.
    if (!smb_InStep()) {
        smb_Disconnect();
        return -EIO;
    }

    nb_SetSessionMessage(sizeof(CloseRequest_t));
    r = GetSMBServerReply(0, NULL, 0);
    if (r <= 0) {
        //      SIGNALIOSEMA(smb_io_sema);
        return -EIO;
    }

    // check sanity of SMB header
    if (CRsp->smbH.Magic != SMB_MAGIC) {
        //      SIGNALIOSEMA(smb_io_sema);
        return -EIO;
    }

    // check there's no error
    if (SMB_STATUS(CRsp->smbH) != STATUS_SUCCESS) {
        //      SIGNALIOSEMA(smb_io_sema);
        return -EIO;
    }

    //  SIGNALIOSEMA(smb_io_sema);

    return 0;
}

#ifdef USE_CUSTOM_RECV
//-------------------------------------------------------------------------
// Receive the reply in SMB_buf from byte *rcv_size up to byte size (session header included).
// Returns 0, or -2 when the connection was closed (0) or failed (< 0) first.
static int smb_RecvReplyRest(int *rcv_size, int size)
{
    int r;

    while (*rcv_size < size) {
        r = smb_RecvTimed(main_socket, NULL, 0, &((u8 *)&SMB_buf)[*rcv_size], size - *rcv_size);
        if (r <= 0)
            return -2;
        *rcv_size += r;
    }

    return 0;
}

//-------------------------------------------------------------------------
// Take the rest of a refused reply (up to byte size) off the socket, so that the next request does
// not read it as its reply. Returns -EIO, or -2 when the connection was closed or failed first.
static int smb_DrainReply(int rcv_size, int size)
{
    int r, n;

    while (rcv_size < size) {
        n = size - rcv_size;
        if (n > (int)sizeof(SMB_buf))
            n = (int)sizeof(SMB_buf);
        r = smb_RecvTimed(main_socket, NULL, 0, &SMB_buf, n);
        if (r <= 0)
            return -2;
        rcv_size += r;
    }

    return -EIO;
}
#endif

//-------------------------------------------------------------------------
static int smb_ReadAndX(u16 FID, u32 offsetlow, u32 offsethigh, void *readbuf, int nbytes)
{
    ReadAndXRequest_t *RR = &SMB_buf.smb.readAndXRequest;
    ReadAndXResponse_t *RRsp = &SMB_buf.smb.readAndXResponse;
    register int r, DataLength;
#ifdef USE_CUSTOM_RECV
    int rcv_size, expected_size, header_size;
#else
    int padding;
#endif

    ZERO_PKT_ALIGNED(RR, sizeof(ReadAndXRequest_t));

    RR->smbH.Magic = SMB_MAGIC;
    RR->smbH.Flags2 = SMB_FLAGS2_32BIT_STATUS;
    RR->smbH.Cmd = SMB_COM_READ_ANDX;
    RR->smbH.UID = (u16)UID;
    RR->smbH.TID = (u16)TID;
    RR->smbWordcount = 12;
    RR->smbAndxCmd = SMB_COM_NONE; // no ANDX command
    RR->FID = (u16)FID;
    RR->OffsetLow = offsetlow;
    RR->OffsetHigh = offsethigh;
    RR->MaxCountLow = (u16)nbytes;
    RR->MaxCountHigh = (u16)(nbytes >> 16);

    nb_SetSessionMessage(sizeof(ReadAndXRequest_t));

#ifdef USE_CUSTOM_RECV
    if (smb_Resync() < 0)
        return -2;
    //Send the whole message, including the 4-byte direct transport packet header.
    r = SendData(main_socket, (char *)&SMB_buf, sizeof(ReadAndXRequest_t) + 4);
    if (r <= 0)
        return -1;
    smb_BeginReply();

    //offset 49 is the offset of the DataOffset field within the ReadAndXResponse structure.
    //recvfrom() is a custom function that will receive the reply.
    do {
        rcv_size = smb_RecvTimed(main_socket, &SMB_buf, 49, readbuf, nbytes);
        if (rcv_size <= 0)
            return -2;
    } while (nb_GetPacketType() != 0); // dropping NBSS Session Keep alive

    // recvfrom() puts a segment shorter than a READ_ANDX response header (63 bytes with the session
    // header) into SMB_buf whole and nothing into readbuf. So a reply whose first segment was that short
    // may not have its whole header, or even its length, yet: collect the rest of the header in SMB_buf
    // before reading its length, status or DataOffset. An error response is shorter than that header,
    // so it is collected whole and nothing of it is left on the socket for the next request to read.
    if (smb_RecvReplyRest(&rcv_size, 4) < 0)
        return -2;
    expected_size = nb_GetSessionMessageLength() + 4;
    header_size = (int)sizeof(ReadAndXResponse_t) + 4;
    if (smb_RecvReplyRest(&rcv_size, expected_size < header_size ? expected_size : header_size) < 0)
        return -2;

    // check there's no error. An error response (an invalid handle, a failed read) is shorter than a
    // READ_ANDX response, so the DataLength read below would be left over from the request, and the
    // read would seem to succeed with whatever readbuf held.
    // An error response with more than its header: take the rest off the socket too.
    if (SMB_STATUS(RRsp->smbH) != STATUS_SUCCESS)
        return smb_DrainReply(rcv_size, expected_size);
    // A success shorter than a READ_ANDX response, or whose data starts inside its header or past the
    // end of the reply or of SMB_buf, is not a read reply: its fields would be the request's.
    if (expected_size < header_size || RRsp->DataOffset < (int)sizeof(ReadAndXResponse_t) || RRsp->DataOffset + 4 > expected_size || RRsp->DataOffset + 4 > (int)sizeof(SMB_buf))
        return smb_DrainReply(rcv_size, expected_size);
    // More data than was asked for would be written past the end of readbuf, and a DataLength larger
    // than the data the reply carries would count bytes that never arrived.
    DataLength = (int)(((u32)RRsp->DataLengthHigh << 16) | RRsp->DataLengthLow);
    if (expected_size - RRsp->DataOffset - 4 > nbytes || (u32)DataLength > (u32)(expected_size - RRsp->DataOffset - 4))
        return smb_DrainReply(rcv_size, expected_size);
    // Padding between the header and the data, when the first segment ended before it.
    if (smb_RecvReplyRest(&rcv_size, RRsp->DataOffset + 4) < 0)
        return -2;

    // Handle fragmented packets
    while (rcv_size < expected_size) {
        r = smb_RecvTimed(main_socket, NULL, 0, &((u8 *)readbuf)[rcv_size - RRsp->DataOffset - 4], expected_size - rcv_size);
        // 0: the connection is closed, < 0: an error
        if (r <= 0)
            return -2;
        rcv_size += r;
    }
#else
    r = GetSMBServerReply(0, NULL, sizeof(ReadAndXResponse_t));
    if (r <= 0)
        return -EIO;

    // check there's no error
    if (SMB_STATUS(RRsp->smbH) != STATUS_SUCCESS)
        return -EIO;

    //Skip any padding bytes.
    padding = RRsp->DataOffset - sizeof(ReadAndXResponse_t);
    if (padding > 0) {
        r = RecvData(main_socket, (char *)(RRsp + 1), padding);
        if (r <= 0)
            return -2;
    }

    DataLength = (int)(((u32)RRsp->DataLengthHigh << 16) | RRsp->DataLengthLow);
    if (DataLength > 0) {
        r = RecvData(main_socket, readbuf, DataLength);
        if (r <= 0)
            return -2;
    }
#endif

    return DataLength;
}

int smb_ReadFile(u16 FID, u32 offsetlow, u32 offsethigh, void *readbuf, int nbytes)
{
    int result, remaining, toRead, attempt;
    char *ptr;

    remaining = nbytes;
    ptr = readbuf;

    WAITIOSEMA(smb_io_sema);

    while (remaining > 0) {
        toRead = remaining > CLIENT_MAX_RECV_SIZE ? CLIENT_MAX_RECV_SIZE : remaining;

        for (attempt = 0;; attempt++) {
            result = smb_ReadAndX(FID, offsetlow, offsethigh, ptr, toRead);
            if (result >= 0 || !smb_RetryRequest(attempt))
                break;
        }
        if (result <= 0) {
            SIGNALIOSEMA(smb_io_sema);
            // Zero bytes means the read reached the end of the file (MS-CIFS 3.3.5.36). Return the
            // bytes read before it as a short count; callers that need all nbytes compare with nbytes.
            return (result == 0) ? nbytes - remaining : result;
        }

        //Check for and handle overflow.
        if (offsetlow + result < offsetlow)
            offsethigh++;
        offsetlow += result;
        ptr += result;
        remaining -= result;
    }

    SIGNALIOSEMA(smb_io_sema);

    return nbytes;
}

//-------------------------------------------------------------------------
static int smb_WriteAndX(u16 FID, u32 offsetlow, u32 offsethigh, void *writebuf, int nbytes)
{
    const int padding = 1; //1 padding byte, to keep the payload aligned for writing performance.
    WriteAndXRequest_t *WR = &SMB_buf.smb.writeAndXRequest;
    WriteAndXResponse_t *WRsp = &SMB_buf.smb.writeAndXResponse;
    int r;

    ZERO_PKT_ALIGNED(WR, sizeof(WriteAndXRequest_t) + padding);

    WR->smbH.Magic = SMB_MAGIC;
    WR->smbH.Flags2 = SMB_FLAGS2_32BIT_STATUS;
    WR->smbH.Cmd = SMB_COM_WRITE_ANDX;
    WR->smbH.UID = (u16)UID;
    WR->smbH.TID = (u16)TID;
    WR->smbWordcount = 14;
    WR->smbAndxCmd = SMB_COM_NONE; // no ANDX command
    WR->FID = (u16)FID;
    WR->OffsetLow = offsetlow;
    WR->OffsetHigh = offsethigh;
    WR->WriteMode = 0x0001; //WritethroughMode
    WR->DataOffset = sizeof(WriteAndXRequest_t) + padding;
    WR->Remaining = (u16)nbytes;
    WR->DataLengthLow = (u16)nbytes;
    WR->DataLengthHigh = (u16)(nbytes >> 16);
    WR->ByteCount = (u16)nbytes + padding;

    nb_SetSessionMessage(sizeof(WriteAndXRequest_t) + padding + nbytes);
    r = GetSMBServerReply(sizeof(WriteAndXRequest_t) + padding, writebuf, 0);
    if (r <= 0)
        return -EIO;

    // check there's no error
    if (SMB_STATUS(WRsp->smbH) != STATUS_SUCCESS)
        return -EIO;

    // A success status does not mean every byte was written: MS-CIFS 2.2.4.43.2 lets a server report
    // a full disk with an error status (caught above) or with success and a Count of zero, and a
    // server may store less than it was sent. Return the server's Count, so that a VMC page it did
    // not store is reported as a failed write. smb_WriteFile sends at most CLIENT_MAX_XMIT_SIZE
    // (65535) bytes per request, so the 16-bit Count holds the whole answer and CountHigh is not
    // needed. A reply shorter than the six-word response (45 bytes), a WordCount under 6, or a Count
    // over what was sent is a failed write.
    if (r < (int)sizeof(WriteAndXResponse_t) || WRsp->smbWordcount < 6 || WRsp->Count > nbytes)
        return -EIO;

    return WRsp->Count;
}

int smb_WriteFile(u16 FID, u32 offsetlow, u32 offsethigh, void *writebuf, int nbytes)
{
    int result, remaining, toWrite, attempt;
    char *ptr;

    remaining = nbytes;
    ptr = writebuf;

    WAITIOSEMA(smb_io_sema);

    while (remaining > 0) {
        toWrite = remaining > CLIENT_MAX_XMIT_SIZE ? CLIENT_MAX_XMIT_SIZE : remaining;

        for (attempt = 0;; attempt++) {
            result = smb_WriteAndX(FID, offsetlow, offsethigh, ptr, toWrite);
            if (result > 0 || !smb_RetryRequest(attempt))
                break;
        }
        if (result <= 0) {
            SIGNALIOSEMA(smb_io_sema);
            return result;
        }

        //Check for and handle overflow.
        if (offsetlow + result < offsetlow)
            offsethigh++;
        offsetlow += result;
        ptr += result;
        remaining -= result;
    }

    SIGNALIOSEMA(smb_io_sema);

    return nbytes;
}

//-------------------------------------------------------------------------
int smb_ReadCD(unsigned int lsn, unsigned int nsectors, void *buf, int part_num)
{
    return smb_ReadFile(cdvdman_settings.FIDs[part_num], lsn * 2048, lsn >> 21, buf, (int)(nsectors * 2048));
}

void smb_CloseAll(void)
{
    int i, fd;

    for (i = 0; i < cdvdman_settings.common.NumParts; i++) {
        fd = cdvdman_settings.FIDs[i];
        if (fd >= 0) {
            smb_Close(fd);
            cdvdman_settings.FIDs[i] = -1;
        }
    }
}

//-------------------------------------------------------------------------
int smb_Disconnect(void)
{
    if (main_socket >= 0) {
        plwip_close(main_socket);
        main_socket = -1;
    }
    smb_ResetStream();

    return 1;
}
