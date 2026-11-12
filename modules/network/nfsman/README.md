# nfsman (the `nfs:` device, NFSv4.0)

The menu's NFS browse driver, loaded instead of smbman or smb2man when Settings > Network >
Network Protocol is NFS. RiptOPL connects one export, as `nfs:/NFS/`. Built by `ps2.yaml` (target
`nfsman`) and embedded in full builds only (no RiptOPLite carries NFS).

| | |
|---|---|
| Protocol | [unfs4](https://github.com/sahlberg/unfs4) by [Ronnie Sahlberg](https://github.com/sahlberg), BSD 2-Clause ([unfs4/LICENSE](unfs4/LICENSE)), commit `3c673fc` with three bounds checks and two fixes ([unfs4/VENDORED.txt](unfs4/VENDORED.txt)) |
| Front end | `nfsman.c`, `nfsman.h`, `imports.lst`, `irx_imports.h`, `linkfile`, `ps2iop-compat.h`: Launchstation's nfsman ([pull request 366](https://github.com/NathanNeurotic/Launchstation/pull/366), head `4928b53`), with two changes to `nfsman.c`: it includes `nfsman.h` from this folder, and it takes a URL option `ro` that refuses every change to the export in the driver (open for writing, make folder, delete, rename) |
| Interface | `nfsman.h`: `NFS_DEVCTL_CONNECT` (name, `nfs://<IPv4>/<export>[?ro]`, a seed) and `NFS_DEVCTL_DISCONNECT_ALL` |

TCP port 2049, NFSv4.0 only, no MOUNT protocol or portmapper. While Settings > Network > NFS
Writes is Off, the menu connects with `?ro`, so nothing in RiptOPL can change the share whatever
the server allows. Games go to Neutrino (`-bsd=nfs`), which reads the export with its own
read-only modules.
