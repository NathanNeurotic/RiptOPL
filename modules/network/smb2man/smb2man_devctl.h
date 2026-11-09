/*
  Devctls smb2man answers beyond the ps2smb.h set smbman and it share. Included by the driver and
  by the EE (src/sources/ethsupport.cpp), so the number is written once.
*/
#ifndef SMB2MAN_DEVCTL_H
#define SMB2MAN_DEVCTL_H

// Returns the SMB dialect negotiated for the open share as the devctl result (0x0202, 0x0210,
// 0x0300, 0x0302 or 0x0311), or -ENODEV when no share is open. smbman does not answer it.
#define SMB2_DEVCTL_GETDIALECT 0xC0DE0009

#endif
