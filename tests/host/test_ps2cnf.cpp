// ps2cnf: SYSTEM.CNF BOOT2 parsing. The real parser, run against files written to a temp dir.
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../src/platform/ps2cnf.cpp"
#include "check.h"

static char dir[] = "/tmp/riptopl-ps2cnf-XXXXXX";

static int boot(const char *content, char *out)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/SYSTEM.CNF", dir);
    FILE *f = fopen(path, "wb");
    fwrite(content, 1, strlen(content), f);
    fclose(f);
    memset(out, 0, CNF_PATH_LEN_MAX + 1);
    return ps2cnfGetBootFile(path, out);
}

int main()
{
    CHECK(mkdtemp(dir) != NULL);
    char out[CNF_PATH_LEN_MAX + 1];

    CHECK(boot("BOOT2 = cdrom0:\\SLUS_203.70;1\nVER = 1.00\nVMODE = NTSC\n", out) == 0);
    CHECK_STR(out, "cdrom0:\\SLUS_203.70;1");
    CHECK(boot("BOOT2=cdrom0:\\SLES_500.01;1\r\nVER = 1.00\r\n", out) == 0); // no spaces, CRLF
    CHECK_STR(out, "cdrom0:\\SLES_500.01;1");
    CHECK(boot("VER = 1.00\nVMODE = PAL\nBOOT2 = cdrom0:\\SCES_123.45;1\n", out) == 0); // not the first line
    CHECK_STR(out, "cdrom0:\\SCES_123.45;1");
    CHECK(boot("  \t BOOT2 \t=\t cdrom0:\\A.B;1\n", out) == 0); // leading/inner whitespace
    CHECK_STR(out, "cdrom0:\\A.B;1");
    CHECK(boot("BOOT2 = cdrom0:\\LAST_000.00;1", out) == 0); // no trailing newline
    CHECK_STR(out, "cdrom0:\\LAST_000.00;1");

    // BOOT (PS1) is not BOOT2, and a CNF without BOOT2 is an error, not a crash.
    CHECK(boot("BOOT = cdrom:\\SLUS_000.01;1\nVER = 1.00\n", out) == -1);
    CHECK(boot("", out) == -1);
    CHECK(boot("VER = 1.00\nVMODE = NTSC\n", out) == -1);
    CHECK(boot("BOOT2", out) == -1); // truncated right after the key

    // missing file
    CHECK(ps2cnfGetBootFile("/nonexistent/SYSTEM.CNF", out) == ENOENT);

    // Regression: "BOOT2 cdrom0:\X" with no '=' used to dereference NULL (CNFGetToken("=") returns NULL on a
    // non-match, which was only compared against (const char *)-1).
    CHECK(boot("BOOT2 cdrom0:\\SLUS_203.70;1\nVER = 1.00\n", out) == -1);
    CHECK(boot("BOOT2 : cdrom0:\\X\n", out) == -1);

    char path[256];
    snprintf(path, sizeof(path), "%s/SYSTEM.CNF", dir);
    remove(path);
    rmdir(dir);
    TEST_DONE("ps2cnf");
}
