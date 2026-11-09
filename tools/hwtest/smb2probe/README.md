# smb2probe

A hardware test for the SMB2 menu freeze (#312). It runs the menu's own network modules, embedded
from the same build, without the rest of the menu. It reports which of three things stalls the
SMB2 share handshake. The mechanisms are described in the campaign RCA (`P0_SMB2_FREEZE_RCA.md` §3).

| Verdict line | Meaning |
|---|---|
| `M1` | `OPENSHARE` did not return within 30 s, but the IOP still answers the helper. Something on the IOP is waiting with no end. The helper's `SetAlarm` and `heartbeat` columns show whether kernel alarms still work. |
| `M2` | The IOP stopped answering altogether. |
| `OPENSHARE completes here` | The handshake works on its own, so the freeze needs something only the full menu has. |

## Build

`ps2build` builds the `smb2probe_iop` and `smb2probe` targets with the rest of `ps2.yaml`. CI
uploads `build/bin/smb2probe*.elf` as the separate `smb2probe-gcc` and `smb2probe-llvm`
artifacts. The probe is not part of the `riptopl-ps2build-*` artifacts.

## Run it (ps2link)

```
ps2client execee host:smb2probe.elf ip=192.168.1.10 mask=255.255.255.0 gw=192.168.1.1 \
    srv=192.168.1.100 share=games back=mc0:/ps2link/PS2LINK.ELF
```

- `dhcp` replaces `ip=`, `mask=` and `gw=`.
- `port=` defaults to 445.
- `squeeze=BYTES` holds IOP RAM before LOGON, until only BYTES remain free. This reproduces a
  console that has more devices loaded.

The probe resets the IOP, so ps2link stops answering during the run.

- **On screen and on the memory card.** The probe writes everything to the screen and to
  `mc0:/SMB2PROBE.TXT`. The file goes through mcserv's own RPC channel, so it is written even while
  fileXio is stuck.
- **Back to ps2link.** At the end, `back=` powers DEV9 off (as the menu's `sysShutdownDev9` does),
  resets the IOP and starts that ELF, falling back to the BIOS loader if the embedded one returns.
  A 120 s EE watchdog does the same whatever state the run is in; it leaves DEV9 on when fileXio is
  still held, since powering it off would block behind the stuck call.
- **Checkpoints.** The card copy is rewritten after every module load, after the network comes up,
  after `OPENSHARE` returns and before the return reset, so a hang leaves its last step on the card.
  The screen starts a new page when full, since the debug screen does not scroll.
- **When a reset is needed.** If the IOP does not come back from reset (a true M2), the screen says
  so, and the console needs a power cycle.

Read the result afterwards, under ps2link. This mode does not reset the IOP; it prints the log over
ps2link's stdout:

```
ps2client execee host:smb2probe.elf dump
```
