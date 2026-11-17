# ript-core-04.b notes (lens defects)

Done: ee_core/src/igs_api.c 1-790, iopmgr.c 1-552, main.c 1-185, modmgr.c 1-285, padhook.c 1-1110 (all read full)

Kept candidates (to findings):
- igs_api.c busy-wait delays + fioPutc per byte (perf) rank 3
- igs_api.c:272-306 slices==0 for frames <128KB -> oversize QWC + div by zero (rank 1)
- igs_api.c error paths BlinkColour forever (no mc1 / card full / 255 files) -> hang instead of IGR_Exit (rank 2)

Dropped:
- FindEmptyArea off-by-one qword: harmless, IGS runs after game shutdown then IGR_Exit (padhook.c:646-659)
- iopmgr ResetIopSpecial suffix off-by-one: unreachable (payload<=70)
- modmgr NOWAIT stack arg / SifCallRpc-fail leak: upstream, theoretical
- padhook CHECK rescan in hooks: upstream, cost one-off, removal risk unknown
- padhook handler refire / kmode: upstream, hardware-tested design

Next: write findings json, validate.
