/*
  Devctls smb2man answers beyond the ps2smb.h set smbman and it share. Included by the driver and
  by the EE (src/sources/ethsupport.cpp), so the number is written once.
*/
#ifndef SMB2MAN_DEVCTL_H
#define SMB2MAN_DEVCTL_H

// Returns the SMB dialect negotiated for the open share as the devctl result (0x0202, 0x0210,
// 0x0300 or 0x0302), or -ENODEV when no share is open. smbman does not answer it.
#define SMB2_DEVCTL_GETDIALECT 0xC0DE0009

// Writes two u32 into buf: total free IOP RAM and the largest free block, in bytes. Never takes
// the driver lock, so it answers while a share is still being opened. smbman does not answer it.
#define SMB2_DEVCTL_GETIOPFREE 0xC0DE000A

// OPENSHARE without holding the caller: same argument as SMB_DEVCTL_OPENSHARE. Returns 0 once the
// connect is handed to the driver's worker thread; the menu then polls SMB2_DEVCTL_CONNECTSTATE, so
// fileXio's single RPC thread is never held for the network handshake. -ENOSYS when the worker
// thread did not start (use SMB_DEVCTL_OPENSHARE). smbman answers neither (-EINVAL).
#define SMB2_DEVCTL_OPENSHARE_ASYNC 0xC0DE000B

// Lock-free state of the last OPENSHARE: SMB2_CONNECT_PENDING, 0 (share open), the negative error
// SMB_DEVCTL_OPENSHARE would have returned, or -ENODEV before any. While a connect is pending,
// every other call that needs the session returns -EBUSY instead of waiting behind it.
#define SMB2_DEVCTL_CONNECTSTATE 0xC0DE000C
#define SMB2_CONNECT_PENDING     1

#endif
