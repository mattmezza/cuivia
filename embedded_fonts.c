/* SPDX-License-Identifier: MIT */
#define _GNU_SOURCE /* memfd_create */
#include "embedded_fonts.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Defined in embedded_fonts.S via .incbin. */
extern const unsigned char font_regular_data[];
extern const unsigned char font_regular_end[];
extern const unsigned char font_bold_data[];
extern const unsigned char font_bold_end[];

static char *embed_to_memfd(const char *name, const unsigned char *data, size_t length) {
    const int fd = memfd_create(name, 0);
    if (fd < 0)
        return NULL;
    size_t offset = 0;
    while (offset < length) {
        const ssize_t written = write(fd, data + offset, length - offset);
        if (written <= 0) {
            close(fd);
            return NULL;
        }
        offset += (size_t)written;
    }
    char *path = malloc(64);
    if (!path || snprintf(path, 64, "/proc/self/fd/%d", fd) < 0) {
        close(fd);
        free(path);
        return NULL;
    }
    return path;
}

const char *embedded_font_path(unsigned index) {
    switch (index) {
    case 0:
        return embed_to_memfd("inter-regular", font_regular_data,
                              (size_t)(font_regular_end - font_regular_data));
    case 1:
        return embed_to_memfd("inter-bold", font_bold_data,
                              (size_t)(font_bold_end - font_bold_data));
    default:
        return NULL;
    }
}
