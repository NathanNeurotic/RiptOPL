// tar: art/cfg/cht archive index built from a real (synthetic) ustar file, the on-disk index cache,
// concatenated archives, directories, oversize entries and name normalisation.
#include <fcntl.h>
#include <stdlib.h>
#include <string>
#include <string.h>
#include <sys/stat.h>
#include <vector>
#include "../../src/tar.cpp"
#include "check.h"

static std::string put(const char *name, const std::string &data, char type = '0', const char *prefix = "", long long fakeSize = -1)
{
    std::string h(TAR_BLOCK_SIZE, '\0');
    strncpy(&h[0], name, 100);
    snprintf(&h[124], 12, "%011llo", fakeSize >= 0 ? (unsigned long long)fakeSize : (unsigned long long)data.size());
    h[156] = type;
    strncpy(&h[345], prefix, 155);
    std::string body = (fakeSize >= 0) ? std::string() : data;
    body.resize((body.size() + 511) / 512 * 512, '\0');
    return h + body;
}
static std::string eof() { return std::string(1024, '\0'); }

static void write_file(const std::string &path, const std::string &bytes)
{
    FILE *f = fopen(path.c_str(), "wb");
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
}

int main()
{
    char dirbuf[] = "/tmp/riptopl-tar-XXXXXX";
    CHECK(mkdtemp(dirbuf) != NULL);
    std::string dir = std::string(dirbuf) + "/";
    std::string tar = dir + "art.tar";

    std::string cover(1000, 'C'), bg(513, 'B'), cfg("Name=Test\n");
    std::string archive =
        put("ART/", "", '5') +                                  // directory entry: skipped
        put("ART/SLUS_203.70_COV.png", cover) +                 // directory prefix stripped
        put("./SLUS_203.70_BG.jpg", bg) +                       // "./" stripped
        put("deep.png", "x", '0', "ART/sub") +                  // ustar prefix joined, then ART/ stripped -> sub/deep.png
        put("huge.bin", std::string(5 * 1024 * 1024, 'H')) +   // > MAX_FILE_SIZE: skipped, not indexed
        put("SLUS_203.70.cfg", cfg) +                           // lives in the first member
        eof() +                                                 // mid-file end-of-archive marker...
        put("SLUS_999.99_SCR.png", std::string(10, 'S')) +      // ...a concatenated second member must still be indexed
        eof();
    write_file(tar, archive);

    CHECK(tarLoadFile(TAR_KIND_ART, tar.c_str()) == 0);

    TarEntryBase *e = tarFindExact(TAR_KIND_ART, "SLUS_203.70_COV.png");
    CHECK(e != NULL);
    if (e) {
        CHECK(e->rawSize == 1000 && e->paddedSize == 1024);
        std::vector<char> buf(2048);
        CHECK(tarRead(TAR_KIND_ART, e, buf.data(), 999) == 0);   // buffer smaller than the file: refuse
        CHECK(tarRead(TAR_KIND_ART, e, buf.data(), buf.size()) == 1000);
        CHECK(memcmp(buf.data(), cover.data(), 1000) == 0);
    }
    CHECK(tarFindExact(TAR_KIND_ART, "slus_203.70_cov.PNG") == e);     // case-insensitive
    CHECK(tarFindExact(TAR_KIND_ART, "ART/SLUS_203.70_COV.png") == e); // query prefix stripped too
    CHECK(tarFindExact(TAR_KIND_ART, "./SLUS_203.70_COV.png") == e);
    CHECK(tarFindExact(TAR_KIND_ART, "SLUS_203.70_BG.jpg") != NULL);   // "./" entry
    TarEntryBase *d = tarFindExact(TAR_KIND_ART, "deep.png");          // basename of "sub/deep.png"
    CHECK(d != NULL && d->rawSize == 1);
    CHECK(tarFindExact(TAR_KIND_ART, "huge.bin") == NULL);             // oversize skipped
    CHECK(tarFindExact(TAR_KIND_ART, "ART") == NULL);                  // directories are not entries
    CHECK(tarFindExact(TAR_KIND_ART, "nope.png") == NULL);
    CHECK(tarFindExact(TAR_KIND_ART, "SLUS_999.99_SCR.png") != NULL);  // member after the mid-file EOF marker
    TarEntryBase *p = tarFindPrefixExact(TAR_KIND_ART, "slus_203.70_");
    CHECK(p != NULL);
    CHECK(tarFindPrefixExact(TAR_KIND_ART, "SLES_") == NULL);

    // The cache file next to the archive reproduces the same index (cache path is taken on reload).
    struct stat st;
    CHECK(stat((dir + "art_cache.bin").c_str(), &st) == 0);
    CHECK(tarClose(TAR_KIND_ART) == 0);
    CHECK(tarFindExact(TAR_KIND_ART, "SLUS_203.70_COV.png") == NULL);  // closed: nothing indexed
    CHECK(tarLoadFile(TAR_KIND_ART, tar.c_str()) == 0);
    e = tarFindExact(TAR_KIND_ART, "SLUS_203.70_COV.png");
    CHECK(e != NULL && e->rawSize == 1000);
    CHECK(tarFindExact(TAR_KIND_ART, "SLUS_999.99_SCR.png") != NULL);

    // Overlong names are truncated to what the entry holds (47 chars for ART: the field is 48 bytes), never
    // overflowing. (Used to keep 46: strncpy(nameMax) then fname[nameMax - 1] = 0.)
    std::string longName(90, 'n');
    write_file(dir + "long.tar", put(longName.c_str(), "z") + eof());
    CHECK(tarClose(TAR_KIND_ART) == 0);
    CHECK(tarLoadFile(TAR_KIND_ART, (dir + "long.tar").c_str()) == 0);
    CHECK(tarFindExact(TAR_KIND_ART, longName.substr(0, 47).c_str()) != NULL);
    CHECK(tarFindExact(TAR_KIND_ART, longName.substr(0, 48).c_str()) == NULL);

    // CFG kind uses its own (smaller) entry layout.
    write_file(dir + "cfg.tar", put("CFG/SLUS_203.70.cfg", cfg) + eof());
    CHECK(tarLoadFile(TAR_KIND_CFG, (dir + "cfg.tar").c_str()) == 0);
    TarEntryBase *c = tarFindExact(TAR_KIND_CFG, "SLUS_203.70.cfg");
    CHECK(c != NULL);
    char out[64] = {0};
    CHECK(c && tarRead(TAR_KIND_CFG, c, out, sizeof(out)) == cfg.size());
    CHECK_STR(out, cfg.c_str());

    // Failure modes: missing file and a truncated (short) header block.
    CHECK(tarLoadFile(TAR_KIND_CHT, (dir + "missing.tar").c_str()) != 0);
    write_file(dir + "short.tar", std::string(100, 'x'));
    CHECK(tarLoadFile(TAR_KIND_CHT, (dir + "short.tar").c_str()) != 0);

    tarClose(TAR_KIND_ART); tarClose(TAR_KIND_CFG); tarClose(TAR_KIND_CHT);
    std::string rm = "rm -rf " + std::string(dirbuf);
    (void)!system(rm.c_str());
    TEST_DONE("tar");
}
