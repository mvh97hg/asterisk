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
 *
 * \brief UUIDv7 dialplan function
 *
 * \author Hungmv <mvh97hg@gmail.com>
 *
 * \ingroup functions
 */

/*** MODULEINFO
	<support_level>extended</support_level>
 ***/

#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/logger.h"
#include "asterisk/uuidv7.h"

/*** DOCUMENTATION
	<function name="UUIDV7" language="en_US">
		<synopsis>
			Generates a UUIDv7.
		</synopsis>
		<syntax>
		</syntax>
		<description>
			<para>Returns a version 7 (time-ordered) Universally Unique Identifier
			(UUIDv7) as a lowercase canonical string per RFC 9562.</para>
			<example title="Generate a UUIDv7">
			same => n,Set(uuid=${UUIDV7()})
			</example>
		</description>
	</function>
 ***/

static int uuidv7_read(struct ast_channel *chan, const char *cmd, char *data,
		char *buf, size_t len)
{
	(void)chan;
	(void)cmd;
	(void)data;

	if (buf == NULL || len < UUIDV7_STR_SIZE) {
		return -1;
	}
	if (uuidv7_generate(buf, len) != 0) {
		buf[0] = '\0';
		return -1;
	}
	return 0;
}

static struct ast_custom_function uuidv7_function = {
	.name = "UUIDV7",
	.read = uuidv7_read,
	.read_max = UUIDV7_STR_SIZE,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&uuidv7_function);
}

static int load_module(void)
{
	if (uuidv7_selftest() != 0) {
		ast_log(LOG_ERROR, "uuidv7 self-test failed\n");
		return AST_MODULE_LOAD_DECLINE;
	}
	return ast_custom_function_register(&uuidv7_function);
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY,
	"UUIDv7 generation dialplan function");
