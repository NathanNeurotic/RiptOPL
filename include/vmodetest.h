#ifndef __OPL_VMODETEST_H
#define __OPL_VMODETEST_H

/*
  Video mode round-trip self-test (diagnostic builds only, issue #132).

  Armed by a file named VMODETEST in the boot folder or the settings folder; output goes next to it
  (no TGA files on a memory card). Once the menu is up it switches modes on
  its own, the same way the Interface settings do (NTSC 640x224p -> PAL 640x512i FLICKER-FREE ->
  224p -> PAL 640x512i -> 224p -> NTSC 640x448i FLICKER-FREE -> 224p). After each switch it saves
  the displayed framebuffer twice to the settings folder, once with the real menu (VMTnn_mmM.TGA)
  and once with a fixed test screen (VMTnn_mmP.TGA), and logs the GS, renderer and font state to
  VMODERES.TXT. Every 224p capture is compared pixel by pixel with the first one, and the verdict
  is the last line of VMODERES.TXT. The VMODETEST file is left alone; delete it to stop the test.

  vmodeTestFrame() runs once per menu frame, right after the frame is shown. Other builds compile
  it away.
*/

#ifdef __OPLDIAG
void vmodeTestFrame(void);
void vmodeTestArm(void); // a "vmodetest" argument: arm without a file and boot in 224p
#else
static inline void vmodeTestArm(void)
{
}
static inline void vmodeTestFrame(void)
{
}
#endif

#endif
