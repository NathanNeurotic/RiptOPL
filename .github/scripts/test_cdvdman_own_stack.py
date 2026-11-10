"""cdvdman must not run device I/O on its caller's stack (games over SMB2: PS2 logo, then a black screen).

When a game resets the IOP with its own IOPRP image, UDNL is started on MODLOAD's module-start thread, which has
a 2 KB stack, and its read of that image is the first disc access of the boot. The device mount (for SMB2: logon,
tree connect and image open, with the TCP/IP stack and SMAP below) ran on that stack, overflowed it, and the
kernel halted the IOP. The mount and the synchronous (Mode 2) read now run on cdvdman_cdread_Thread, which has a
4 KB stack of its own.

This is a source contract, not a run: it fails when a later change moves that work back into the caller.
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
cdvdman = (root / 'modules/iopcore/cdvdman/cdvdman.c').read_text()
ioops = (root / 'modules/iopcore/cdvdman/ioops.c').read_text()

failures = []


def body(source, signature):
    """The text of the function whose definition starts with `signature`."""
    match = re.search(re.escape(signature) + r'[^;{]*\{', source)  # skips a forward declaration
    if not match:
        failures.append('no definition of ' + signature)
        return ''
    brace = match.end() - 1
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == '{':
            depth += 1
        elif source[i] == '}':
            depth -= 1
            if depth == 0:
                return source[brace:i + 1]
    failures.append('unterminated function: ' + signature)
    return ''


def check(condition, message):
    if not condition:
        failures.append(message)


thread = body(cdvdman, 'static void cdvdman_cdread_Thread(void *args)')
sync_read = body(cdvdman, 'int cdvdman_SyncRead(')
mount = body(cdvdman, 'void cdvdman_mount_device(void)')
init = body(cdvdman, 'int cdvdman_init(void)')
semaphores = body(cdvdman, 'static void cdvdman_create_semaphores(void)')
start_threads = body(cdvdman, 'static void cdvdman_startThreads(void)')
fs_init = body(ioops, 'int cdvdman_fs_init(void)')


def report():
    if failures:
        print('cdvdman own-stack contract failed:')
        for failure in failures:
            print(' - ' + failure)
        sys.exit(1)


report()  # the checks below index into these bodies, so a missing one is reported here

# The mount: only the reading thread calls DeviceFSInit.
check(len(re.findall(r'\bDeviceFSInit\s*\(\s*\)\s*;', cdvdman + ioops)) == 1 and 'DeviceFSInit();' in thread,
      'DeviceFSInit must be called from cdvdman_cdread_Thread and nowhere else in cdvdman.c / ioops.c')
check('cdvdman_mount_device();' in fs_init, 'cdvdman_fs_init must mount through cdvdman_mount_device')
check(mount.index('SignalSema(cdrom_rthread_sema)') < mount.index('WaitSema(cdvdman_mountsema)'),
      'cdvdman_mount_device must queue the mount and then wait for it')
check(thread.index('DeviceFSInit();') < thread.index('SignalSema(cdvdman_mountsema)'),
      'the reading thread must report the mount only after DeviceFSInit returned')

# One caller at a time, and a caller that cannot wait is turned away before anything is queued.
check(re.search(r'if \(WaitSema\(cdvdman_initsema\) != 0\)\s*return 0;', init) is not None,
      'cdvdman_init must give up when it cannot take cdvdman_initsema')
check(init.index('WaitSema(cdvdman_initsema)') < init.index('cdvdman_fs_init()'),
      'cdvdman_init must hold cdvdman_initsema around cdvdman_fs_init')
check(len(re.findall(r'\breturn\b', init)) == 3
      and re.search(r'SignalSema\(cdvdman_initsema\);\s*return\s+result;\s*\}\s*$', init) is not None,
      'every path of cdvdman_init past the lock must release cdvdman_initsema')

# A mount request and a queued read each have a signal, so neither is lost.
check(re.search(r'smp\.max = 2;[^\n]*\n\s*cdrom_rthread_sema = CreateSema', semaphores) is not None,
      'cdrom_rthread_sema must hold two signals: one queued read and one mount request')

# The synchronous read: queued for the reading thread, completed by the caller as before.
check('cdvdman_read(' not in sync_read, 'cdvdman_SyncRead must not read on the caller\'s stack')
check('CDREAD_JOB_SYNC' in sync_read and 'WaitSema(cdvdman_syncsema)' in sync_read,
      'cdvdman_SyncRead must queue the read and wait for the reading thread')
check(re.search(r'if \(CpuSuspendIntr\(&OldState\) == KE_CPUDI\)\s*return cdvdman_AsyncRead\(', sync_read) is not None
      and sync_read.index('KE_CPUDI') < sync_read.index('CDREAD_JOB_SYNC'),
      'cdvdman_SyncRead must not queue a read it cannot wait for (interrupts disabled)')
check('CpuResumeIntr(OldState);' in sync_read
      and sync_read.index('KE_CPUDI') < sync_read.index('CpuResumeIntr(OldState);') < sync_read.index('CDREAD_JOB_SYNC'),
      'cdvdman_SyncRead must restore interrupts before it queues the read and waits')
check(sync_read.index('WaitSema(cdvdman_syncsema)') < sync_read.index('cdvdman_cb_event(SCECdFuncRead)'),
      'the game\'s callback must still follow the data in cdvdman_SyncRead')
check(re.search(r'if \(cdread_job == CDREAD_JOB_SYNC\) \{[^}]*SignalSema\(cdvdman_syncsema\);\s*continue;', thread)
      is not None, 'a sync read must be handed back to its caller without the reading thread completing it')

# The stack that work now runs on.
check('thread_param.stacksize = 0x1000;' in start_threads, 'the reading thread keeps its 4 KB stack')

report()
print('cdvdman own-stack contract: ok')
