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

/*!
 * \file
 * \brief UUIDv7 unit tests
 *
 * \author Hungmv <mvh97hg@gmail.com>
 *
 * \ingroup tests
 */

/*** MODULEINFO
	<depend>TEST_FRAMEWORK</depend>
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include <ctype.h>
#include <string.h>

#include "asterisk/module.h"
#include "asterisk/test.h"
#include "asterisk/uuidv7.h"

static int is_hex_lower(char c)
{
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static int validate_uuidv7_string(const char *uuid)
{
	size_t i;

	if (!uuid || strlen(uuid) != (UUIDV7_STR_SIZE - 1)) {
		return -1;
	}
	for (i = 0; i < UUIDV7_STR_SIZE - 1; i++) {
		if (i == 8 || i == 13 || i == 18 || i == 23) {
			if (uuid[i] != '-') {
				return -1;
			}
		} else if (!is_hex_lower(uuid[i])) {
			return -1;
		}
	}
	/* Version nibble is the first hex digit of the third group (index 14). */
	if (uuid[14] != '7') {
		return -1;
	}
	/* Variant: first hex digit of the fourth group must be 8, 9, a, or b. */
	if (uuid[19] != '8' && uuid[19] != '9' && uuid[19] != 'a' && uuid[19] != 'b') {
		return -1;
	}
	return 0;
}

AST_TEST_DEFINE(uuidv7_selftest_runs)
{
	switch (cmd) {
	case TEST_INIT:
		info->name = "uuidv7_selftest";
		info->category = "/main/uuidv7/";
		info->summary = "Run built-in UUIDv7 self-test";
		info->description =
			"Verifies that uuidv7_selftest() succeeds for format, ordering, and overflow handling.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (uuidv7_selftest() != 0) {
		ast_test_status_update(test, "uuidv7_selftest() failed\n");
		return AST_TEST_FAIL;
	}

	return AST_TEST_PASS;
}

AST_TEST_DEFINE(uuidv7_generate_format)
{
	char uuid[UUIDV7_STR_SIZE];
	char *ptr;
	int i;

	switch (cmd) {
	case TEST_INIT:
		info->name = "uuidv7_generate_format";
		info->category = "/main/uuidv7/";
		info->summary = "UUIDv7 string format and version/variant";
		info->description =
			"Checks uuidv7_generate() and uuidv7() produce canonical lowercase UUIDv7 strings.";
		return AST_TEST_NOT_RUN;
	case TEST_EXECUTE:
		break;
	}

	if (uuidv7_generate(NULL, UUIDV7_STR_SIZE) != -1) {
		ast_test_status_update(test, "uuidv7_generate(NULL) should fail\n");
		return AST_TEST_FAIL;
	}
	if (uuidv7_generate(uuid, 8) != -1) {
		ast_test_status_update(test, "uuidv7_generate() with short buffer should fail\n");
		return AST_TEST_FAIL;
	}

	memset(uuid, 0, sizeof(uuid));
	if (uuidv7_generate(uuid, sizeof(uuid)) != 0) {
		ast_test_status_update(test, "uuidv7_generate() failed\n");
		return AST_TEST_FAIL;
	}
	if (validate_uuidv7_string(uuid)) {
		ast_test_status_update(test, "Invalid UUIDv7 from uuidv7_generate(): %s\n", uuid);
		return AST_TEST_FAIL;
	}
	ast_test_status_update(test, "uuidv7_generate() -> %s\n", uuid);

	ptr = uuidv7();
	if (!ptr) {
		ast_test_status_update(test, "uuidv7() returned NULL\n");
		return AST_TEST_FAIL;
	}
	if (validate_uuidv7_string(ptr)) {
		ast_test_status_update(test, "Invalid UUIDv7 from uuidv7(): %s\n", ptr);
		return AST_TEST_FAIL;
	}

	/* Generate several and ensure uniqueness + non-decreasing lexical order. */
	for (i = 0; i < 32; i++) {
		char next[UUIDV7_STR_SIZE];

		if (uuidv7_generate(next, sizeof(next)) != 0) {
			ast_test_status_update(test, "uuidv7_generate() failed on iteration %d\n", i);
			return AST_TEST_FAIL;
		}
		if (validate_uuidv7_string(next)) {
			ast_test_status_update(test, "Invalid UUIDv7 on iteration %d: %s\n", i, next);
			return AST_TEST_FAIL;
		}
		if (strcmp(uuid, next) >= 0) {
			ast_test_status_update(test,
				"UUIDv7 not strictly increasing: %s then %s\n", uuid, next);
			return AST_TEST_FAIL;
		}
		memcpy(uuid, next, sizeof(uuid));
	}

	return AST_TEST_PASS;
}

static int unload_module(void)
{
	AST_TEST_UNREGISTER(uuidv7_selftest_runs);
	AST_TEST_UNREGISTER(uuidv7_generate_format);
	return 0;
}

static int load_module(void)
{
	AST_TEST_REGISTER(uuidv7_selftest_runs);
	AST_TEST_REGISTER(uuidv7_generate_format);
	return AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "UUIDv7 test module");
