/* The smb2probe EE <-> IOP helper protocol (probe.c, probe_iop.c). */
#ifndef SMB2PROBE_RPC_H
#define SMB2PROBE_RPC_H

#define PROBE_RPC_ID        0x5B2B0001
#define PROBE_ALIVE         1
#define PROBE_SQUEEZE       2 // in: [0] = target total free bytes
#define PROBE_RELEASE       3
#define PROBE_REPLY_WORDS   6
#define PROBE_SQUEEZE_BLOCK 4096

#endif
