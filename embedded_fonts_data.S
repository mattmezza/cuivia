/* SPDX-License-Identifier: MIT */
#ifdef __APPLE__
    .section __TEXT,__const
#define font_regular_data _font_regular_data
#define font_regular_end _font_regular_end
#define font_bold_data _font_bold_data
#define font_bold_end _font_bold_end
#else
    .section .rodata
#endif
    .balign 16
    .globl font_regular_data
#ifndef __APPLE__
    .type font_regular_data, @object
#endif
font_regular_data:
    .incbin "assets/fonts/Inter-Regular.otf"
    .globl font_regular_end
font_regular_end:
    .balign 16
    .globl font_bold_data
#ifndef __APPLE__
    .type font_bold_data, @object
#endif
font_bold_data:
    .incbin "assets/fonts/Inter-Bold.otf"
    .globl font_bold_end
font_bold_end:
