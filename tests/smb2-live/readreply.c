/*
 * READ reply framing in usmb2, against hand-built replies on a socketpair (no server needed):
 * the data starts at DataOffset, padding before it never reaches the caller's buffer, and a
 * reply that claims more bytes than it carried, or than were asked for, is refused. Signed
 * replies too: the padding is inside the signature, so a padded reply verifies and one whose
 * padding changed after signing is refused.
 *
 * The verify hooks are smb2man.c's (HMAC-SHA256 with smb2auth, the 16-byte signature at 48),
 * restated here so usmb2.c can be included alone; keepalive.c runs smb2man.c's own.
 */
#include <string.h>
#include "smb2auth.h"

static smb2auth_hmac_t mac;
static u8 reply_sig[16];

static int verify_begin(const uint8_t *key, uint8_t *hdr)
{
    if (!(hdr[16] & 0x08))
        return -1;
    memcpy(reply_sig, hdr + 48, 16);
    memset(hdr + 48, 0, 16);
    smb2auth_hmac_init(&mac, SMB2AUTH_SHA256, key, 16);
    smb2auth_hmac_update(&mac, hdr, 64);
    return 0;
}

static int verify_end(void)
{
    u8 digest[32];

    smb2auth_hmac_final(&mac, digest);
    return memcmp(digest, reply_sig, 16) ? -1 : 0;
}
#define USMB2_VERIFY_BEGIN(u, hdr) verify_begin((u)->session_key, hdr)
#define USMB2_VERIFY_UPDATE(p, n)  smb2auth_hmac_update(&mac, p, n)
#define USMB2_VERIFY_END()         verify_end()

#include "usmb2.c"

#include <stdlib.h>

static const uint8_t key[16] = "0123456789abcdef";

static int failures;

#define CHECK(cond, ...)                  \
    do {                                  \
        if (!(cond)) {                    \
            printf("FAIL: " __VA_ARGS__); \
            printf("\n");                 \
            failures++;                   \
        }                                 \
    } while (0)

/*
 * One READ reply: DataOffset off, DataLength len, then pad bytes of 'P' and body bytes of data.
 * signing: 0 off; 1 signed with key; 2 signed, then the byte at frame offset flip changed.
 */
static int32_t read_reply(struct usmb2_context *u, uint8_t off, uint32_t len, int pad, const char *body,
                          int bodylen, uint8_t *out, int count, int signing, int flip)
{
    uint8_t frame[4 + 64 + 16 + 256];
    uint8_t fid[16] = {0};
    int sv[2], n = 0;
    int32_t rv;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv)) {
        perror("socketpair");
        exit(2);
    }
    memset(frame, 0, sizeof(frame));
    n = 4;
    *(uint32_t *)&frame[n] = htole32(0x424d53fe); /* \xfeSMB, status 0 */
    *(uint16_t *)&frame[n + 4] = htole16(64);
    *(uint16_t *)&frame[n + 12] = htole16(CMD_READ);
    n += 64;
    frame[n] = 17;      /* StructureSize */
    frame[n + 2] = off; /* DataOffset */
    *(uint32_t *)&frame[n + 4] = htole32(len);
    n += 16;
    memset(&frame[n], 'P', pad);
    n += pad;
    memcpy(&frame[n], body, bodylen);
    n += bodylen;
    *(uint32_t *)&frame[0] = htonl(n - 4);
    if (signing) {
        smb2auth_hmac_t m;
        u8 digest[32];

        *(uint32_t *)&frame[4 + 16] = htole32(0x00000009);    /* SERVER_TO_REDIR | SIGNED */
        *(uint64_t *)&frame[4 + 24] = htole64(u->message_id); /* the MessageId the request will use */
        smb2auth_hmac_init(&m, SMB2AUTH_SHA256, key, 16);
        smb2auth_hmac_update(&m, &frame[4], n - 4);
        smb2auth_hmac_final(&m, digest);
        memcpy(&frame[4 + 48], digest, 16);
        if (signing == 2)
            frame[flip] ^= 0x01;
    }
    if (write(sv[1], frame, n) != n) {
        perror("write");
        exit(2);
    }
    shutdown(sv[1], SHUT_WR); /* a short frame then reads as a closed socket, not a hang */

    u->fd = sv[0];
    u->signing = signing != 0;
    memcpy(u->session_key, key, 16);
    memset(out, '-', count);
    rv = usmb2_pread(u, fid, out, count, 0);
    if (u->fd >= 0)
        close(u->fd);
    close(sv[1]);
    return rv;
}

int main(void)
{
    static struct usmb2_context u;
    uint8_t out[32];
    int32_t rv;

    rv = read_reply(&u, 0x50, 10, 0, "0123456789", 10, out, 10, 0, 0);
    CHECK(rv == 10 && !memcmp(out, "0123456789", 10), "plain READ: rv %d", (int)rv);

    rv = read_reply(&u, 0x58, 10, 8, "0123456789", 10, out, 10, 0, 0);
    CHECK(rv == 10 && !memcmp(out, "0123456789", 10), "READ with 8 bytes before DataOffset: rv %d, data %.10s",
          (int)rv, out);

    rv = read_reply(&u, 0x50, 10, 0, "", 0, out, 10, 0, 0);
    CHECK(rv < 0, "READ claiming 10 bytes, carrying none: rv %d", (int)rv);

    rv = read_reply(&u, 0x50, 10, 0, "01234", 5, out, 10, 0, 0);
    CHECK(rv < 0, "READ claiming 10 bytes, carrying 5: rv %d", (int)rv);

    rv = read_reply(&u, 0x50, 20, 0, "0123456789abcdefghij", 20, out, 10, 0, 0);
    CHECK(rv < 0, "READ claiming 20 bytes when 10 were asked for: rv %d", (int)rv);

    rv = read_reply(&u, 0x40, 10, 0, "0123456789", 10, out, 10, 0, 0);
    CHECK(rv < 0 && u.fd < 0, "READ with DataOffset inside the header: rv %d", (int)rv);

    rv = read_reply(&u, 0xF0, 10, 0, "0123456789", 10, out, 10, 0, 0);
    CHECK(rv < 0 && u.fd < 0, "READ with DataOffset past the frame: rv %d", (int)rv);

    rv = read_reply(&u, 0x50, 0, 0, "", 0, out, 10, 0, 0);
    CHECK(rv == 0, "READ of 0 bytes: rv %d", (int)rv);

    /* signed: padding at 84..91, data at 92.. */
    rv = read_reply(&u, 0x58, 10, 8, "0123456789", 10, out, 10, 1, 0);
    CHECK(rv == 10 && !memcmp(out, "0123456789", 10), "signed READ with padding: rv %d, data %.10s", (int)rv,
          out);

    rv = read_reply(&u, 0x58, 10, 8, "0123456789", 10, out, 10, 2, 4 + 64 + 16 + 3);
    CHECK(rv < 0 && u.fd < 0, "signed READ, padding changed after signing: rv %d", (int)rv);

    rv = read_reply(&u, 0x58, 10, 8, "0123456789", 10, out, 10, 2, 4 + 64 + 16 + 8 + 5);
    CHECK(rv < 0 && u.fd < 0, "signed READ, data changed after signing: rv %d", (int)rv);

    if (failures)
        return 1;
    printf("readreply: all cases passed\n");
    return 0;
}
