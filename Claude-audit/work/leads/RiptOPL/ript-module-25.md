## Symbols with no reference outside their definition (symbols-unreferenced.tsv). Class no-other-mention = the name appears nowhere else in the repo; only-prototype-or-export-mentions = it appears only in a prototype or export list
- third_party/ps2sdk-socket/socket.c:378  function `libcglue_inet_addr`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:390  function `libcglue_inet_ntoa`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:402  function `libcglue_inet_ntoa_r`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:414  function `libcglue_inet_aton`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:426  function `libcglue_ps2ip_setconfig`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:438  function `libcglue_ps2ip_getconfig`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:450  function `libcglue_dns_setserver`  (no-other-mention)
- third_party/ps2sdk-socket/socket.c:462  function `libcglue_dns_getserver`  (no-other-mention)

## Macros tested by #if/#ifdef that no source or build file defines (cond-macros-undefined.tsv). Compiler, SDK and toolchain macros also land here: check before using
- `F_accept`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:58
- `F_bind`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:97
- `F_connect`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:117
- `F_getpeername`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:337
- `F_getsockname`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:357
- `F_getsockopt`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:277
- `F_libcglue_dns_getserver`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:461
- `F_libcglue_dns_setserver`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:449
- `F_libcglue_inet_addr`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:377
- `F_libcglue_inet_aton`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:413
- `F_libcglue_inet_ntoa_r`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:401
- `F_libcglue_inet_ntoa`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:389
- `F_libcglue_ps2ip_getconfig`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:437
- `F_libcglue_ps2ip_setconfig`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:425
- `F_listen`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:137
- `F_recv`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:157
- `F_recvfrom`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:177
- `F_recvmsg`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:197
- `F_send`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:217
- `F_sendmsg`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:257
- `F_sendto`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:237
- `F_setsockopt`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:297
- `F_shutdown`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:317
- `F_socket`  tested at 1 site(s) in the repo, mentioned in 0 build file(s); here: third_party/ps2sdk-socket/socket.c:26

## Byte-identical files (dupes-exact-files.tsv)
- third_party/ps2sdk-ppctty/LICENSE is byte-identical to: RiptOPL/modules/bdmassault/LICENSE; RiptOPL/third_party/ps2sdk-socket/LICENSE
- third_party/ps2sdk-ppctty/LICENSE is byte-identical to: RiptOPL/modules/bdmassault/LICENSE; RiptOPL/third_party/ps2sdk-socket/LICENSE; POPSLoader/docs/third_party/okeanos_udpfs/AFL-2.0.txt; POPSLoader/docs/third_party/okeanos_udpfs/LICENSE; POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE
- third_party/ps2sdk-socket/LICENSE is byte-identical to: RiptOPL/modules/bdmassault/LICENSE; RiptOPL/third_party/ps2sdk-ppctty/LICENSE
- third_party/ps2sdk-socket/LICENSE is byte-identical to: RiptOPL/modules/bdmassault/LICENSE; RiptOPL/third_party/ps2sdk-ppctty/LICENSE; POPSLoader/docs/third_party/okeanos_udpfs/AFL-2.0.txt; POPSLoader/docs/third_party/okeanos_udpfs/LICENSE; POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE

## Largely identical files (dupes-file-pairs.tsv)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as Launchstation/iop/mx4sio_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as Launchstation/iop/oldlibs/ps2ftpd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as Launchstation/iop/usbmass_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as RiptOPL/modules/bdmassault/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as RiptOPL/modules/debug/ps2link/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as RiptOPL/modules/mx4sio_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as RiptOPL/modules/usb/usbmass_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as RiptOPL/ship/POPS/UDPFS-NOTICES/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as RiptOPL/third_party/ps2sdk-socket/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-ppctty/LICENSE is 10% the same as Simple-Media-System/iop/smbman-sms/LICENSE-AFL-2.0 (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as Launchstation/iop/mx4sio_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as Launchstation/iop/oldlibs/ps2ftpd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as Launchstation/iop/usbmass_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as RiptOPL/modules/bdmassault/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as RiptOPL/modules/debug/ps2link/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as RiptOPL/modules/mx4sio_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as RiptOPL/modules/usb/usbmass_bd/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as RiptOPL/ship/POPS/UDPFS-NOTICES/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as RiptOPL/third_party/ps2sdk-ppctty/LICENSE (15 cloned normalised lines)
- third_party/ps2sdk-socket/LICENSE is 10% the same as Simple-Media-System/iop/smbman-sms/LICENSE-AFL-2.0 (15 cloned normalised lines)

## Duplicate blocks (dupes-blocks.tsv)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  Launchstation/iop/mx4sio_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  Launchstation/iop/oldlibs/ps2ftpd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  Launchstation/iop/usbmass_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  RiptOPL/modules/bdmassault/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  RiptOPL/modules/debug/ps2link/LICENSE:131-146  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  RiptOPL/modules/mx4sio_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  RiptOPL/modules/usb/usbmass_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  RiptOPL/ship/POPS/UDPFS-NOTICES/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  RiptOPL/third_party/ps2sdk-socket/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-ppctty/LICENSE:123-138  ==  Simple-Media-System/iop/smbman-sms/LICENSE-AFL-2.0:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  Launchstation/iop/mx4sio_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  Launchstation/iop/oldlibs/ps2ftpd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  Launchstation/iop/usbmass_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  POPSLoader/iop/embed/BDMASSAULT_MX4SIO/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  RiptOPL/modules/bdmassault/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  RiptOPL/modules/debug/ps2link/LICENSE:131-146  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  RiptOPL/modules/mx4sio_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  RiptOPL/modules/usb/usbmass_bd/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  RiptOPL/ship/POPS/UDPFS-NOTICES/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  RiptOPL/third_party/ps2sdk-ppctty/LICENSE:123-138  (15 normalised lines)
- third_party/ps2sdk-socket/LICENSE:123-138  ==  Simple-Media-System/iop/smbman-sms/LICENSE-AFL-2.0:123-138  (15 normalised lines)
