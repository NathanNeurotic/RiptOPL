/*
  RA: what RiptOPL writes into its copy of rapops (modules/network/rapops)
  before a PS1 launch. POPStarter loads that copy as MODULE_#.IRX
  without arguments, so the module carries its configuration: the loader
  finds the block by the magic and fills it in.

  The modules rapops starts travel in the same block. ps2build embeds IOP
  modules only into EE programs, and the loader already carries every one
  of them, so it copies them into blob[] instead of rapops carrying a
  second copy of each.
*/
#ifndef __RAPOPS_CFG_H__
#define __RAPOPS_CFG_H__

#include "ra_snap.h" /* RA_ARG_TAIL_MAX; brings ra_watch.h */

#define RAPOPS_MAGIC     "RIPTRA02"
#define RAPOPS_IPCFG_MAX 64

/* In load order: DEV9, SMSUTILS, SMSTCPIP, SMAP, raudp, rapull. */
enum { RAPOPS_DEV9,
       RAPOPS_SMSUTILS,
       RAPOPS_SMSTCPIP,
       RAPOPS_SMAP,
       RAPOPS_RAUDP,
       RAPOPS_RAPULL,
       RAPOPS_BLOBS };
/* Room for all six. A loader whose modules do not fit launches untracked. */
#define RAPOPS_BLOB_BYTES (112 * 1024)

struct rapops_cfg
{
    char magic[8] __attribute__((nonstring)); /* RAPOPS_MAGIC */
    int ipcfg_len;                            /* 0: not configured, the module stays out */
    char ipcfg[RAPOPS_IPCFG_MAX];             /* "ip\0mask\0gateway\0", as SMAP takes it */
    char game_id[16];                         /* the VCD watch key, e.g. "P123456789abcde" */
    unsigned int client_mode;
    unsigned int host_pinned;
    char host[RA_ARG_TAIL_MAX + 1]; /* "<ipv4>[,<image hash>]", as RA_ARG_HOST takes it */
    /* the watch list for rapull, the reader on the IOP; count 0: no list */
    unsigned int count;
    unsigned int bytes;
    unsigned int node_count;
    unsigned int list[RA_WATCH_MAX];
    struct ra_node nodes[RA_NODE_MAX_PS1];
    /* each module at blob + off[i], 16-byte aligned for LoadModuleBuffer; size 0: absent */
    unsigned int blob_off[RAPOPS_BLOBS];
    unsigned int blob_size[RAPOPS_BLOBS];
    unsigned char blob[RAPOPS_BLOB_BYTES] __attribute__((aligned(16)));
};

#endif
