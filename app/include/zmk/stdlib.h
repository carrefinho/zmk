/*
 * Copyright (c) 2022 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdlib.h> /* for size_t */

/*
 * ANSI C version of strlcpy
 * Based on the NetBSD strlcpy man page.
 *
 * Nathan Myers <ncm-nospam@cantrip.org>, 2003/06/03
 * Placed in the public domain.
 */

size_t strlcpy(char *dst, const char *src, size_t size);

/*
 * Parse an unsigned decimal, writing the first unconsumed character to
 * *endptr when non-NULL. Deliberately not strtoul: the only callers parse
 * digit runs out of internal settings keys ("keymap/l/3/12"), and picolibc's
 * strtoul drags tinystdio's shared conversion core -- vfscanf, fgetc, ungetc
 * -- into the image, over 1 KB for something this does in a dozen
 * instructions. No whitespace, sign, base prefix or overflow handling.
 */
unsigned long zmk_parse_udec(const char *str, char **endptr);
