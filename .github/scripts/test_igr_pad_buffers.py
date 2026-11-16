"""Run production menu pad reads with exactly sized libpad/libpad2 DMA areas."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
source = (Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'ee_core/src/padhook.c').read_text()


def function(signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


inputs = function('static void IGR_CheckInputs(')
inputs = inputs[:inputs.index('#ifdef RETROACHIEVEMENTS', inputs.index('if (Pad_Data.pad_buf'))]
inputs += '    if (intr) EI();\n}\n'
program = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#define IGR_MENU
#define IGR_LIBPAD 1
#define IGR_LIBPAD2 2
#define IGR_PAD_STABLE_V1 6
#define IGR_PAD_STABLE_V2 1
#define IGR_COMBO_R1_L1_R2_L2 0xf0
#define IGR_COMBO_START_SELECT 0xf6
#define IGR_COMBO_R3_L3 0xf9
#define USE_LOCAL_EECORE_CONFIG
#define UNCACHED_SEG(p) (p)
static int DI(void) { return 1; }
static void EI(void) {}
static int padOpen_hooked;
static struct { u16 libpad, libversion; u8 *pad_buf; int vb_count;
    int pos_combo1, pos_combo2, pos_state, pos_frame; u8 combo_type, prev_frame; } Pad_Data;
''' + '\n'.join(function(s) for s in ('static void Set_libpad_Params(',
    'static const u8 *IGR_PadBuf(', 'static u16 IGR_PadButtons(')) + inputs + r'''
static void frame(u8 *slot, u32 number, int stable, u16 buttons) {
    memcpy(slot + Pad_Data.pos_frame, &number, sizeof(number));
    slot[Pad_Data.pos_state] = stable;
    slot[Pad_Data.pos_combo1] = (u8)(~buttons >> 8);
    slot[Pad_Data.pos_combo2] = (u8)~buttons;
}
int main(void) {
    /* PS2SDK libpad: two 64-byte ROM slots or two 128-byte XPADMAN slots.
       Sony libpad2: two 128-byte DBC sides, frame counter at offset 124. */
    for (int layout=0; layout<3; layout++) {
        int stride = layout == 0 ? 64 : 128;
        u8 *area = calloc(2, stride);
        assert(area);
        Pad_Data.libpad = layout == 2 ? IGR_LIBPAD2 : IGR_LIBPAD;
        Pad_Data.libversion = layout == 0 ? 0x150 : layout == 1 ? 0x160 : 0x200;
        Set_libpad_Params(area);
        int stable = layout == 2 ? IGR_PAD_STABLE_V2 : IGR_PAD_STABLE_V1;
        for (int latest=0; latest<2; latest++) {
            /* Both low frame bytes match: selection must compare all 32 bits. */
            frame(area, latest == 0 ? 0x201 : 0x101, stable, 0x0f09);
            frame(area+stride, latest == 1 ? 0x201 : 0x101, stable, 0x1234);
            assert(IGR_PadBuf() == area + latest*stride);
            assert(IGR_PadButtons() == (latest ? 0x1234 : 0x0f09));
            Pad_Data.combo_type = 0;
            IGR_CheckInputs();
            assert(Pad_Data.combo_type == (latest ? 0 : IGR_COMBO_START_SELECT));
            frame(area+latest*stride, 0x301, 0, 0x0f09);
            assert(IGR_PadButtons() == 0);
        }
        frame(area, 7, stable, 0x0f09);
        frame(area+stride, 7, stable, 0x1234);
        assert(IGR_PadBuf() == area && IGR_PadButtons() == 0x0f09);
        free(area);
        Pad_Data.pad_buf = NULL;
        assert(IGR_PadButtons() == 0);
        IGR_CheckInputs();
    }
    puts("PASS: production libpad/libpad2 newest-side selection, buttons and combo sampling within exact DMA-area bounds");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp)
    (path / 'pad.c').write_text(program)
    subprocess.run(['gcc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-pie', '-no-pie', str(path / 'pad.c'), '-o', str(path / 'pad')], check=True)
    subprocess.run([str(path / 'pad')], check=True, timeout=10)
