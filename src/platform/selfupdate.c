/*
  selfupdate.c -- the portable half of Check for Updates (include/selfupdate.h).

  Parsing only, so the host test (.github/scripts/test_selfupdate_host.py) builds it as is: the
  release's SHA256SUMS.txt, the build number in a version string, GitHub's redirect URLs and the
  file names beside the running ELF. The network, the card and the swap are in
  src/ui/selfupdate.cpp.
*/

#include "include/selfupdate.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int isDigit(char c)
{
    return c >= '0' && c <= '9';
}

static int hexValue(char c)
{
    if (isDigit(c))
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/* The revision in [p, end): skip 'v' and the dotted version, then the first part that is all digits. */
static int revisionIn(const char *p, const char *end)
{
    if (p < end && *p == 'v')
        p++;
    if (p >= end || !isDigit(*p))
        return -1;
    while (p < end && (isDigit(*p) || *p == '.'))
        p++;
    while (p < end && *p == '-') {
        const char *part = ++p;
        long value = 0;
        while (p < end && isDigit(*p) && value < 100000000) {
            value = value * 10 + (*p - '0');
            p++;
        }
        if (p > part && (p == end || *p == '-'))
            return (int)value;
        while (p < end && *p != '-')
            p++;
    }
    return -1;
}

int suRevision(const char *version)
{
    return version != NULL ? revisionIn(version, version + strlen(version)) : -1;
}

int suParseSums(const char *text, int len, const char *elf, unsigned char sha[32], int *revision)
{
    const char *p = text, *end = text + len;
    size_t elfLen = strlen(elf);
    int found = 0, rev = -1, i;

    while (p < end) {
        const char *eol = memchr(p, '\n', (size_t)(end - p)), *name;
        size_t nameLen;
        if (eol == NULL)
            eol = end;
        if (eol - p >= 66 && p[64] == ' ' && p[65] == ' ') {
            name = p + 66;
            nameLen = (size_t)(eol - name);
            if (nameLen > 0 && name[nameLen - 1] == '\r')
                nameLen--;
            for (i = 0; i < 64; i++)
                if (hexValue(p[i]) < 0)
                    return -1;
            if (nameLen == elfLen && !memcmp(name, elf, elfLen)) {
                for (i = 0; i < 32; i++)
                    sha[i] = (unsigned char)(hexValue(p[2 * i]) << 4 | hexValue(p[2 * i + 1]));
                found = 1;
            } else if (nameLen > 4 && !memcmp(name + nameLen - 4, ".zip", 4)) {
                /* The version is the archive name's last "-v" part: RIPTOPL-Korium-v1.2.0-Beta-4256.zip. */
                const char *v = NULL, *q;
                int r;
                for (q = name; q + 2 < name + nameLen; q++)
                    if (q[0] == '-' && q[1] == 'v' && isDigit(q[2]))
                        v = q + 1;
                if (v != NULL && (r = revisionIn(v, name + nameLen - 4)) >= 0) {
                    if (rev >= 0 && r != rev)
                        return -1; /* one publication carries one version */
                    rev = r;
                }
            }
        }
        p = eol + 1;
    }
    if (!found || rev < 0)
        return -1;
    *revision = rev;
    return 0;
}

const char *suSplitUrl(const char *url, char *host, int host_size)
{
    static const char *const hosts[] = {"github.com", "release-assets.githubusercontent.com", "objects.githubusercontent.com"};
    const char *h, *slash;
    size_t n, i;

    if (url == NULL || strncmp(url, "https://", 8))
        return NULL;
    h = url + 8;
    slash = strchr(h, '/');
    if (slash == NULL)
        return NULL;
    n = (size_t)(slash - h);
    for (i = 0; i < sizeof(hosts) / sizeof(hosts[0]); i++) {
        if (strlen(hosts[i]) == n && !memcmp(h, hosts[i], n) && (int)n < host_size) {
            memcpy(host, h, n);
            host[n] = '\0';
            return slash;
        }
    }
    return NULL; /* any other host, a port, user info or an IP address */
}

int suJoin(char *out, int size, const char *dir, const char *name, const char *suffix)
{
    size_t d = strlen(dir);
    int n = snprintf(out, (size_t)size, "%s%s%s%s", dir, d > 0 && dir[d - 1] != ':' && dir[d - 1] != '/' ? "/" : "", name, suffix);
    return n > 0 && n < size ? 0 : -1;
}

int suLooksLikeElf(const unsigned char *head, int len)
{
    /* ident, 32-bit, little-endian, executable, MIPS */
    return len >= 20 && head[0] == 0x7F && head[1] == 'E' && head[2] == 'L' && head[3] == 'F' && head[4] == 1 && head[5] == 1 &&
           head[16] == 2 && head[17] == 0 && head[18] == 8 && head[19] == 0;
}

static uint32_t readLe32(const unsigned char *p)
{
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

int suElfFormat(const unsigned char *head, int len)
{
    if (len < 52 || !suLooksLikeElf(head, len) || head[42] != 32 || head[43] != 0)
        return SU_ELF_UNKNOWN;

    uint32_t offset = readLe32(head + 28);
    unsigned int count = head[44] | head[45] << 8;
    if (offset < 52 || offset > (uint32_t)len || count > ((uint32_t)len - offset) / 32)
        return SU_ELF_UNKNOWN;

    /* Both linkers put the unpacked image at 0x00100000 with 128-byte alignment.
       PS2PACK emits one aligned load segment after the 52-byte header and its 32-byte table. */
    const unsigned char *load = NULL;
    for (unsigned int i = 0; i < count; i++) {
        const unsigned char *ph = head + offset + 32 * i;
        if (readLe32(ph) == 1) {
            if (load != NULL)
                return SU_ELF_UNKNOWN;
            load = ph;
        }
    }
    if (load == NULL)
        return SU_ELF_UNKNOWN;
    uint32_t data = readLe32(load + 4), address = readLe32(load + 8);
    uint32_t align = readLe32(load + 28), entry = readLe32(head + 24);
    if (entry < address || entry - address >= readLe32(load + 16))
        return SU_ELF_UNKNOWN;
    if (address == 0x00100000 && align == 128 && data == ((offset + count * 32 + 127) & ~127u))
        return SU_ELF_UNPACKED;
    if (offset == 52 && count == 1 && data == 96 && align == 16 && address != 0x00100000)
        return SU_ELF_PACKED;
    return SU_ELF_UNKNOWN;
}

int suApaDirectory(const char *dir, char *partition, int partition_size, char *path, int path_size)
{
    /* The launcher can report hdd0:__system:pfs:/launcher or hdd0:/__system/launcher. */
    if (strncmp(dir, "hdd0:", 5))
        return -1;
    const char *label = dir + 5;
    if (*label == '/')
        label++;
    size_t n = strcspn(label, ":/");
    if (n == 0 || n > 32 || n + 5 >= (size_t)partition_size)
        return -1;
    const char *folder = label + n;
    if (*folder == ':') {
        if (!strncmp(folder, ":pfs:", 5))
            folder += 5;
        else if (!strncmp(folder, ":pfs0:", 6) || !strncmp(folder, ":pfs1:", 6))
            folder += 6;
        else
            return -1;
    }
    if (strchr(folder, ':') || strchr(folder, '\\'))
        return -1;
    /* Only use the named partition and the exact launch directory. */
    int written = snprintf(path, (size_t)path_size, "%s%s", *folder == '/' ? "" : "/", folder);
    if (written < 0 || written >= path_size)
        return -1;
    snprintf(partition, (size_t)partition_size, "hdd0:%.*s", (int)n, label);
    return 0;
}
