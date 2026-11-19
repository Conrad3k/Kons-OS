/* K1 card probe — 0.10.9 step 1, TRIAL CODE: a 1-second 1080p30 clip (testsrc2, made by FFmpeg on the build computer:
 * libx264 High, 4 Mbit/s, B-frames) built into the probe — it has no paths to read a file from. One clip only: the
 * trial image must fit the 128 MiB A/B slot (S31). */
    .section .rodata
    .globl k1_klip_h264, k1_klip_h264_koniec
k1_klip_h264:
    .incbin "src/aplikacje/sonda/klip.h264"
k1_klip_h264_koniec:
    .section .note.GNU-stack,"",@progbits
