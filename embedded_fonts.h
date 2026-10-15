/* Embedded font blobs: the Inter Regular/Bold OTF files are linked into the
 * executable via .incbin, then served to FreeType through descriptor paths
 * (Linux memfds or unlinked temporary files on macOS) so no font files are needed on disk at runtime.
 * SPDX-License-Identifier: MIT */
#ifndef TRIVIA_EMBEDDED_FONTS_H
#define TRIVIA_EMBEDDED_FONTS_H

/* Returns a heap-allocated path usable with cui_font_open for the embedded
 * face (0 = regular, 1 = bold), or NULL on failure. The returned path and the
 * underlying file descriptor intentionally live for the process lifetime
 * (FreeType keeps the stream open for lazy glyph loading). */
const char *embedded_font_path(unsigned index);

#endif
