#include <loadcore.h>
#include <irx.h>

#include "oplsmb.h"
#include "smbauth.h"

#define MODNAME "smb_init"
IRX_ID(MODNAME, 1, 1);

/** Complete negotiation before reporting successful non-resident module startup. */
int _start(int argc, char *argv[])
{
    int ret = smb_NegotiateProt(&SmbInitHashPassword);

    if (ret < 0)
        return ret;

    return MODULE_NO_RESIDENT_END;
}
