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
 * \brief RFC 9562 UUIDv7 generator
 *
 * \author Hungmv <mvh97hg@gmail.com>
 */

#include "asterisk.h"
#include "asterisk/uuidv7.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/types.h>

#if defined(__has_include)
#if __has_include(<sys/random.h>)
#include <sys/random.h>
#define UUIDV7_HAVE_GETRANDOM 1
#endif
#endif

#define UUIDV7_RAW_SIZE 16
#define UUIDV7_TAIL_SIZE 10
#define UUIDV7_TIME_MASK 0xFFFFFFFFFFFFULL

#if (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 201112L) \
		&& defined(__GNUC__)
#define _Thread_local __thread
#endif

#define UUIDV7_RNG_POOL_SIZE 256

static const char uuidv7_hex[] = "0123456789abcdef";
static pthread_mutex_t uuidv7_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t uuidv7_rng_mu = PTHREAD_MUTEX_INITIALIZER;
static uint64_t uuidv7_last_ms;
static unsigned char uuidv7_tail[UUIDV7_TAIL_SIZE];
static pid_t uuidv7_pid = (pid_t)-1;
static _Thread_local char uuidv7_tls_buf[UUIDV7_STR_SIZE];

static unsigned char uuidv7_rng_pool[UUIDV7_RNG_POOL_SIZE];
static size_t uuidv7_rng_off = UUIDV7_RNG_POOL_SIZE;
static int uuidv7_urandom_fd = -1;
static pid_t uuidv7_rng_pid = (pid_t)-1;

static int uuidv7_read_entropy(unsigned char *buf, size_t len)
{
	size_t offset = 0;

#ifdef UUIDV7_HAVE_GETRANDOM
	while(offset < len) {
		ssize_t n = getrandom(buf + offset, len - offset, 0);

		if(n > 0) {
			offset += (size_t)n;
			continue;
		}
		if(n < 0 && errno == EINTR) {
			continue;
		}
		break;
	}
	if(offset == len) {
		return 0;
	}
#endif

	if(uuidv7_urandom_fd < 0) {
		uuidv7_urandom_fd = open("/dev/urandom", O_RDONLY);
		if(uuidv7_urandom_fd < 0) {
			return -1;
		}
	}
	while(offset < len) {
		ssize_t n = read(uuidv7_urandom_fd, buf + offset, len - offset);

		if(n > 0) {
			offset += (size_t)n;
			continue;
		}
		if(n < 0 && errno == EINTR) {
			continue;
		}
		(void)close(uuidv7_urandom_fd);
		uuidv7_urandom_fd = -1;
		return -1;
	}
	return 0;
}

static void uuidv7_rng_reset_after_fork(void)
{
	pid_t pid = getpid();

	if(uuidv7_rng_pid == pid) {
		return;
	}
	uuidv7_rng_pid = pid;
	uuidv7_rng_off = UUIDV7_RNG_POOL_SIZE;
	if(uuidv7_urandom_fd >= 0) {
		(void)close(uuidv7_urandom_fd);
		uuidv7_urandom_fd = -1;
	}
}

/* Kernel CSPRNG pool: refill via getrandom (cached /dev/urandom fallback). */
static int uuidv7_random(unsigned char *buf, size_t len)
{
	size_t copied = 0;

	if(buf == NULL || len == 0) {
		return -1;
	}
	if(pthread_mutex_lock(&uuidv7_rng_mu) != 0) {
		return -1;
	}
	uuidv7_rng_reset_after_fork();
	while(copied < len) {
		size_t chunk;

		if(uuidv7_rng_off >= UUIDV7_RNG_POOL_SIZE) {
			if(uuidv7_read_entropy(uuidv7_rng_pool, sizeof(uuidv7_rng_pool))
					!= 0) {
				(void)pthread_mutex_unlock(&uuidv7_rng_mu);
				return -1;
			}
			uuidv7_rng_off = 0;
		}
		chunk = UUIDV7_RNG_POOL_SIZE - uuidv7_rng_off;
		if(chunk > len - copied) {
			chunk = len - copied;
		}
		memcpy(buf + copied, uuidv7_rng_pool + uuidv7_rng_off, chunk);
		uuidv7_rng_off += chunk;
		copied += chunk;
	}
	(void)pthread_mutex_unlock(&uuidv7_rng_mu);
	return 0;
}

static int uuidv7_now_ms(uint64_t *ts_ms)
{
	struct timespec ts;

	if(ts_ms == NULL || clock_gettime(CLOCK_REALTIME, &ts) != 0) {
		return -1;
	}
	*ts_ms = (uint64_t)ts.tv_sec * 1000ULL
			+ (uint64_t)(ts.tv_nsec / 1000000L);
	return 0;
}

static void uuidv7_apply_version_variant(unsigned char tail[UUIDV7_TAIL_SIZE])
{
	tail[0] = 0x70 | (tail[0] & 0x0F);
	tail[2] = 0x80 | (tail[2] & 0x3F);
}

static void uuidv7_commit_tail(uint64_t ts_ms,
		const unsigned char tail[UUIDV7_TAIL_SIZE])
{
	memcpy(uuidv7_tail, tail, sizeof(uuidv7_tail));
	uuidv7_last_ms = ts_ms;
	uuidv7_pid = getpid();
}

static int uuidv7_seed_tail(uint64_t ts_ms)
{
	unsigned char tail[UUIDV7_TAIL_SIZE];

	if(uuidv7_random(tail, sizeof(tail)) != 0) {
		return -1;
	}
	uuidv7_apply_version_variant(tail);
	uuidv7_commit_tail(ts_ms, tail);
	return 0;
}

static int uuidv7_increment_tail(void)
{
	int i;
	unsigned char value;

	for(i = UUIDV7_TAIL_SIZE - 1; i >= 0; i--) {
		switch(i) {
			case 0:
				value = (unsigned char)((uuidv7_tail[0] & 0x0F) + 1);
				uuidv7_tail[0] = 0x70 | (value & 0x0F);
				if((value & 0x10) == 0) {
					return 0;
				}
				break;
			case 2:
				value = (unsigned char)((uuidv7_tail[2] & 0x3F) + 1);
				uuidv7_tail[2] = 0x80 | (value & 0x3F);
				if((value & 0x40) == 0) {
					return 0;
				}
				break;
			default:
				uuidv7_tail[i]++;
				if(uuidv7_tail[i] != 0) {
					return 0;
				}
		}
	}
	return 1;
}

static uint64_t uuidv7_max_u64(uint64_t a, uint64_t b)
{
	return a > b ? a : b;
}

static int uuidv7_prepare_locked(uint64_t *ts_ms)
{
	pid_t pid;

	if(ts_ms == NULL) {
		return -1;
	}
	pid = getpid();
	*ts_ms = uuidv7_max_u64(*ts_ms, uuidv7_last_ms);

	if(uuidv7_pid != pid || uuidv7_last_ms != *ts_ms) {
		return uuidv7_seed_tail(*ts_ms);
	}
	if(uuidv7_increment_tail() == 0) {
		return 0;
	}
	*ts_ms = uuidv7_last_ms + 1;
	return uuidv7_seed_tail(*ts_ms);
}

static int uuidv7_fill_raw(
		unsigned char raw[UUIDV7_RAW_SIZE], uint64_t *ts_ms)
{
	if(raw == NULL || ts_ms == NULL || uuidv7_prepare_locked(ts_ms) != 0) {
		return -1;
	}
	raw[0] = (unsigned char)((*ts_ms >> 40) & 0xFF);
	raw[1] = (unsigned char)((*ts_ms >> 32) & 0xFF);
	raw[2] = (unsigned char)((*ts_ms >> 24) & 0xFF);
	raw[3] = (unsigned char)((*ts_ms >> 16) & 0xFF);
	raw[4] = (unsigned char)((*ts_ms >> 8) & 0xFF);
	raw[5] = (unsigned char)(*ts_ms & 0xFF);
	memcpy(&raw[6], uuidv7_tail, sizeof(uuidv7_tail));
	return 0;
}

static void uuidv7_pack_raw(unsigned char raw[UUIDV7_RAW_SIZE], uint64_t ts_ms)
{
	raw[0] = (unsigned char)((ts_ms >> 40) & 0xFF);
	raw[1] = (unsigned char)((ts_ms >> 32) & 0xFF);
	raw[2] = (unsigned char)((ts_ms >> 24) & 0xFF);
	raw[3] = (unsigned char)((ts_ms >> 16) & 0xFF);
	raw[4] = (unsigned char)((ts_ms >> 8) & 0xFF);
	raw[5] = (unsigned char)(ts_ms & 0xFF);
	memcpy(&raw[6], uuidv7_tail, sizeof(uuidv7_tail));
}

static void uuidv7_format_lower(const unsigned char raw[UUIDV7_RAW_SIZE],
		char out[UUIDV7_STR_SIZE])
{
	int i;
	char *p = out;

	for(i = 0; i < UUIDV7_RAW_SIZE; i++) {
		if(i == 4 || i == 6 || i == 8 || i == 10) {
			*p++ = '-';
		}
		*p++ = uuidv7_hex[(raw[i] >> 4) & 0x0F];
		*p++ = uuidv7_hex[raw[i] & 0x0F];
	}
	*p = '\0';
}

static uint64_t uuidv7_unpack_ms(
		const unsigned char raw[UUIDV7_RAW_SIZE])
{
	return ((uint64_t)raw[0] << 40) | ((uint64_t)raw[1] << 32)
			| ((uint64_t)raw[2] << 24) | ((uint64_t)raw[3] << 16)
			| ((uint64_t)raw[4] << 8) | (uint64_t)raw[5];
}

static int uuidv7_validate_string(const char *uuid)
{
	size_t i;

	if(uuid == NULL || uuid[UUIDV7_STR_SIZE - 1] != '\0') {
		return -1;
	}
	for(i = 0; i < UUIDV7_STR_SIZE - 1; i++) {
		if(i == 8 || i == 13 || i == 18 || i == 23) {
			if(uuid[i] != '-') {
				return -1;
			}
		} else if(!((uuid[i] >= '0' && uuid[i] <= '9')
						  || (uuid[i] >= 'a' && uuid[i] <= 'f'))) {
			return -1;
		}
	}
	return 0;
}

char *ast_uuidv7(void)
{
	if(ast_uuidv7_generate(uuidv7_tls_buf, sizeof(uuidv7_tls_buf)) != 0) {
		return NULL;
	}
	return uuidv7_tls_buf;
}

/*
 * Public generate path: do not hold uuidv7_mu across entropy syscalls.
 * Fast path (same ms): increment under lock only. Reseed: unlock → RNG → relock.
 */
int ast_uuidv7_generate(char *out, size_t n)
{
	unsigned char raw[UUIDV7_RAW_SIZE];
	unsigned char fresh_tail[UUIDV7_TAIL_SIZE];
	uint64_t ts_ms;
	uint64_t use_ms;
	pid_t pid;
	int have_fresh = 0;

	if(out == NULL || n < UUIDV7_STR_SIZE) {
		return -1;
	}

	for(;;) {
		if(pthread_mutex_lock(&uuidv7_mu) != 0) {
			return -1;
		}
		if(uuidv7_now_ms(&ts_ms) != 0) {
			(void)pthread_mutex_unlock(&uuidv7_mu);
			return -1;
		}
		use_ms = uuidv7_max_u64(ts_ms, uuidv7_last_ms);
		pid = getpid();

		if(uuidv7_pid == pid && uuidv7_last_ms == use_ms) {
			if(uuidv7_increment_tail() == 0) {
				uuidv7_pack_raw(raw, use_ms);
				uuidv7_format_lower(raw, out);
				(void)pthread_mutex_unlock(&uuidv7_mu);
				return 0;
			}
			use_ms = uuidv7_last_ms + 1;
			have_fresh = 0;
		} else if(have_fresh) {
			uuidv7_commit_tail(use_ms, fresh_tail);
			uuidv7_pack_raw(raw, use_ms);
			uuidv7_format_lower(raw, out);
			(void)pthread_mutex_unlock(&uuidv7_mu);
			return 0;
		}

		(void)pthread_mutex_unlock(&uuidv7_mu);

		if(uuidv7_random(fresh_tail, sizeof(fresh_tail)) != 0) {
			return -1;
		}
		uuidv7_apply_version_variant(fresh_tail);
		have_fresh = 1;
	}
}

int ast_uuidv7_selftest(void)
{
	unsigned char probe[UUIDV7_RAW_SIZE];
	unsigned char first[UUIDV7_RAW_SIZE];
	unsigned char second[UUIDV7_RAW_SIZE];
	unsigned char saved_tail[UUIDV7_TAIL_SIZE];
	char formatted[UUIDV7_STR_SIZE];
	char first_string[UUIDV7_STR_SIZE];
	char second_string[UUIDV7_STR_SIZE];
	uint64_t ts_ms;
	uint64_t fixed_ms;
	uint64_t overflow_ms;
	uint64_t saved_last_ms;
	pid_t saved_pid;
	int result = -1;

	if(pthread_mutex_lock(&uuidv7_mu) != 0) {
		return -1;
	}
	saved_last_ms = uuidv7_last_ms;
	memcpy(saved_tail, uuidv7_tail, sizeof(saved_tail));
	saved_pid = uuidv7_pid;

	if(uuidv7_now_ms(&ts_ms) != 0
			|| uuidv7_fill_raw(probe, &ts_ms) != 0
			|| (ts_ms & ~UUIDV7_TIME_MASK) != 0
			|| uuidv7_unpack_ms(probe) != ts_ms
			|| (probe[6] >> 4) != 0x07
			|| (probe[8] & 0xC0) != 0x80) {
		goto restore;
	}
	uuidv7_format_lower(probe, formatted);
	if(uuidv7_validate_string(formatted) != 0) {
		goto restore;
	}

	uuidv7_last_ms = 0;
	memset(uuidv7_tail, 0, sizeof(uuidv7_tail));
	uuidv7_pid = (pid_t)-1;
	fixed_ms = 123456789ULL;
	if(uuidv7_fill_raw(first, &fixed_ms) != 0) {
		goto restore;
	}
	fixed_ms = 123456789ULL;
	if(uuidv7_fill_raw(second, &fixed_ms) != 0) {
		goto restore;
	}
	uuidv7_format_lower(first, first_string);
	uuidv7_format_lower(second, second_string);
	if(strcmp(first_string, second_string) >= 0) {
		goto restore;
	}

	overflow_ms = 123456789ULL;
	uuidv7_last_ms = overflow_ms;
	memset(uuidv7_tail, 0xFF, sizeof(uuidv7_tail));
	uuidv7_tail[0] = 0x7F;
	uuidv7_tail[2] = 0xBF;
	uuidv7_pid = getpid();
	if(uuidv7_prepare_locked(&overflow_ms) != 0
			|| overflow_ms != 123456790ULL
			|| uuidv7_last_ms != overflow_ms) {
		goto restore;
	}
	result = 0;

restore:
	uuidv7_last_ms = saved_last_ms;
	memcpy(uuidv7_tail, saved_tail, sizeof(saved_tail));
	uuidv7_pid = saved_pid;
	(void)pthread_mutex_unlock(&uuidv7_mu);
	return result;
}

#ifdef UUIDV7_TESTING
void ast_uuidv7_test_set_last_ms(uint64_t ms)
{
	if(pthread_mutex_lock(&uuidv7_mu) == 0) {
		uuidv7_last_ms = ms;
		(void)pthread_mutex_unlock(&uuidv7_mu);
	}
}

uint64_t ast_uuidv7_test_last_ms(void)
{
	uint64_t ms = 0;

	if(pthread_mutex_lock(&uuidv7_mu) == 0) {
		ms = uuidv7_last_ms;
		(void)pthread_mutex_unlock(&uuidv7_mu);
	}
	return ms;
}
#endif
