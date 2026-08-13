/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2026, Hungmv
 *
 * Hungmv <mvh97hg@gmail.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief RFC 9562 UUIDv7 generator API
 *
 * \author Hungmv <mvh97hg@gmail.com>
 */

#ifndef _ASTERISK_UUIDV7_H
#define _ASTERISK_UUIDV7_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UUIDV7_STR_SIZE 37

char *uuidv7(void);
int uuidv7_generate(char *out, size_t n);
int uuidv7_selftest(void);

#ifdef UUIDV7_TESTING
void uuidv7_test_set_last_ms(uint64_t ms);
uint64_t uuidv7_test_last_ms(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASTERISK_UUIDV7_H */
