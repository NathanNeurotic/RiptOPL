// HOST-TEST: sdk-headers
// bdmtyped: the typed spelling (usb0:, ata0:, ilink0:, mx4sio0:) of a mass<N>: path for an ELF app's
// argv[0]. Pure string code; the slot/type lookup it is fed lives in bdmsupport.cpp and is not under test.
#include <string.h>
#include "include/bdmtyped.h"
#include "check.h"

int main()
{
    char out[64];
    const char *rest = NULL;

    // Tokens match the bd->path strings the block drivers set; UDPBD and CF have none.
    CHECK_STR(bdmTypedPathToken(BDM_TYPE_USB), "usb");
    CHECK_STR(bdmTypedPathToken(BDM_TYPE_ILINK), "ilink");
    CHECK_STR(bdmTypedPathToken(BDM_TYPE_SDC), "mx4sio");
    CHECK_STR(bdmTypedPathToken(BDM_TYPE_ATA), "ata");
    CHECK(bdmTypedPathToken(BDM_TYPE_UDPBD) == NULL);
    CHECK(bdmTypedPathToken(BDM_TYPE_PCCF) == NULL);
    CHECK(bdmTypedPathToken(BDM_TYPE_UNKNOWN) == NULL);

    // Slot parsing: numbered mass paths only.
    CHECK(bdmParseMassSlot("mass0:/APPS/x.elf", &rest) == 0);
    CHECK_STR(rest, ":/APPS/x.elf");
    CHECK(bdmParseMassSlot("mass3:APPS/x.elf", NULL) == 3);
    CHECK(bdmParseMassSlot("mass12:/", NULL) == 12);
    CHECK(bdmParseMassSlot("mass:/APPS/x.elf", NULL) == -1); // the bare selector is not a slot
    CHECK(bdmParseMassSlot("mass123:/", NULL) == -1);        // never that many slots
    CHECK(bdmParseMassSlot("massive:/", NULL) == -1);
    CHECK(bdmParseMassSlot("ata0:/APPS/x.elf", NULL) == -1); // already typed
    CHECK(bdmParseMassSlot("hdd0:__common:pfs:/x.elf", NULL) == -1);
    CHECK(bdmParseMassSlot("mc0:/x.elf", NULL) == -1);
    CHECK(bdmParseMassSlot("", NULL) == -1);
    CHECK(bdmParseMassSlot(NULL, NULL) == -1);

    // The rewrite keeps everything after the colon and numbers by the TYPED index, not the slot.
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, 0, "mass0:/APPS/x.elf", out, sizeof(out)) == 1);
    CHECK_STR(out, "usb0:/APPS/x.elf");
    CHECK(bdmFormatTypedPath(BDM_TYPE_ATA, 0, "mass1:/APPS/wLE.ELF", out, sizeof(out)) == 1);
    CHECK_STR(out, "ata0:/APPS/wLE.ELF"); // slot 1 (after a stick) is still the FIRST ata device
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, 1, "mass2:/APPS/x.elf", out, sizeof(out)) == 1);
    CHECK_STR(out, "usb1:/APPS/x.elf"); // second stick
    CHECK(bdmFormatTypedPath(BDM_TYPE_SDC, 0, "mass0:/a b/c.elf", out, sizeof(out)) == 1);
    CHECK_STR(out, "mx4sio0:/a b/c.elf");
    CHECK(bdmFormatTypedPath(BDM_TYPE_ILINK, 0, "mass0:", out, sizeof(out)) == 1);
    CHECK_STR(out, "ilink0:");

    // No typed name, no rewrite, buffer untouched.
    strcpy(out, "keep");
    CHECK(bdmFormatTypedPath(BDM_TYPE_UDPBD, 0, "mass0:/APPS/x.elf", out, sizeof(out)) == 0);
    CHECK_STR(out, "keep");
    CHECK(bdmFormatTypedPath(BDM_TYPE_PCCF, 0, "mass0:/APPS/x.elf", out, sizeof(out)) == 0);
    CHECK_STR(out, "keep");
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, 0, "mass:/APPS/x.elf", out, sizeof(out)) == 0);
    CHECK_STR(out, "keep");
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, -1, "mass0:/APPS/x.elf", out, sizeof(out)) == 0);
    CHECK_STR(out, "keep");

    // Too small: refused, never truncated into a path that names a different file.
    char tiny[12];
    CHECK(bdmFormatTypedPath(BDM_TYPE_SDC, 0, "mass0:/APPS/x.elf", tiny, sizeof(tiny)) == 0);
    CHECK(tiny[0] == '\0');
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, 0, "mass0:/x.elf", tiny, sizeof(tiny)) == 1); // exactly fits (11 + NUL)
    CHECK_STR(tiny, "usb0:/x.elf");
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, 0, "mass0:/x.elf", out, 0) == 0);
    CHECK(bdmFormatTypedPath(BDM_TYPE_USB, 0, "mass0:/x.elf", NULL, 8) == 0);

    TEST_DONE("bdmtyped");
}
