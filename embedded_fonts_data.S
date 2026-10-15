/* SPDX-License-Identifier: MIT */
    .section .rodata
    .balign 16
    .globl font_regular_data
    .type font_regular_data, @object
font_regular_data:
    .incbin "assets/fonts/Inter-Regular.otf"
    .globl font_regular_end
font_regular_end:
    .balign 16
    .globl font_bold_data
    .type font_bold_data, @object
font_bold_data:
    .incbin "assets/fonts/Inter-Bold.otf"
    .globl font_bold_end
font_bold_end:
