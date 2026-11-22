/* SPDX-License-Identifier: Zlib */

/*
 * The text form of a float. Copied code does the hard part: Ryu finds the
 * shortest digits of a value, and ffc.h parses a number. The code here
 * applies the layout of a read and the grammar of a write. Neither copy
 * calls printf or strtod, so the C locale cannot change the text.
 *
 * The copies are built here and nowhere else. Each external name they
 * define is renamed, so that an app that links its own copy of Ryu or of
 * ffc.h does not clash with ours.
 */
#define d2s nine_ryu_d2s
#define d2s_buffered nine_ryu_d2s_buffered
#define d2s_buffered_n nine_ryu_d2s_buffered_n
#define f2s nine_ryu_f2s
#define f2s_buffered nine_ryu_f2s_buffered
#define f2s_buffered_n nine_ryu_f2s_buffered_n

#define ffc_format_double_fixed nine_ffc_format_double_fixed
#define ffc_from_chars_double nine_ffc_from_chars_double
#define ffc_from_chars_double_options nine_ffc_from_chars_double_options
#define ffc_from_chars_float nine_ffc_from_chars_float
#define ffc_from_chars_float_options nine_ffc_from_chars_float_options
#define ffc_from_chars_i32 nine_ffc_from_chars_i32
#define ffc_from_chars_i32_options nine_ffc_from_chars_i32_options
#define ffc_from_chars_i64 nine_ffc_from_chars_i64
#define ffc_from_chars_i64_options nine_ffc_from_chars_i64_options
#define ffc_from_chars_u32 nine_ffc_from_chars_u32
#define ffc_from_chars_u32_options nine_ffc_from_chars_u32_options
#define ffc_from_chars_u64 nine_ffc_from_chars_u64
#define ffc_from_chars_u64_options nine_ffc_from_chars_u64_options
#define ffc_parse_double nine_ffc_parse_double
#define ffc_parse_double_simple nine_ffc_parse_double_simple
#define ffc_parse_float nine_ffc_parse_float
#define ffc_parse_float_simple nine_ffc_parse_float_simple
#define ffc_parse_i32 nine_ffc_parse_i32
#define ffc_parse_i32_simple nine_ffc_parse_i32_simple
#define ffc_parse_i64 nine_ffc_parse_i64
#define ffc_parse_i64_simple nine_ffc_parse_i64_simple
#define ffc_parse_json_number nine_ffc_parse_json_number
#define ffc_parse_options_default nine_ffc_parse_options_default
#define ffc_parse_u32 nine_ffc_parse_u32
#define ffc_parse_u32_simple nine_ffc_parse_u32_simple
#define ffc_parse_u64 nine_ffc_parse_u64
#define ffc_parse_u64_simple nine_ffc_parse_u64_simple

/*
 * MSVC gives some warnings from its back end, after the code is compiled,
 * where /external:W0 does not reach them: C4701 and C4703 for a variable
 * that may be used before it is set, and C4702 for unreachable code. The
 * copied code gives C4701. The back end reads the warning state at the end
 * of each function, and it may inline the copied code into the functions
 * below, so the warnings stay off for the whole file.
 */
#ifdef _MSC_VER
#pragma warning(disable : 4701 4702 4703)
#endif

/* Ryu's own tables for f32, not the larger ones it shares with f64. */
#define RYU_FLOAT_FULL_TABLE
#include "ryu/f2s.c"

/* The f32 and the f64 code each define a static to_chars(). */
#define to_chars d2s_to_chars
#include "ryu/d2s.c"
#undef to_chars

#define FFC_IMPL
#include "ffc.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "internal.h"

#define F32_SIGN 0x80000000u
#define F32_EXP_MASK 0x7f800000u
#define F32_QUIET_NAN 0x7fc00000u

#define F64_SIGN 0x8000000000000000u
#define F64_EXP_MASK 0x7ff0000000000000u
#define F64_QUIET_NAN 0x7ff8000000000000u

/* The most digits that Ryu gives for a float, which an f64 needs. */
#define DIGITS_MAX 17

/* Copy the @n bytes at @s to @p. Return: the end of the copy. */
static char *put_bytes(char *p, const char *s, size_t n)
{
	memcpy(p, s, n);
	return p + n;
}

static char *put_zeros(char *p, size_t n)
{
	memset(p, '0', n);
	return p + n;
}

/*
 * Give @digits the digits of the @len bytes at @text, which Ryu wrote for a
 * value that is finite, positive and not zero, with no zero at the end. Ryu
 * writes d.dddEx or dEx, with a minus before a negative x. Set @exp to x:
 * the value is d.ddd times 10 to the @exp. Return: the number of digits.
 */
static size_t shortest(const char *text, int len, char digits[DIGITS_MAX],
		       int *exp)
{
	size_t n = 0;
	int i = 0;
	bool neg;

	while (text[i] != 'E') {
		if (text[i] != '.')
			digits[n++] = text[i];
		i++;
	}
	neg = text[++i] == '-';
	i += neg;
	for (*exp = 0; i < len; i++)
		*exp = *exp * 10 + (text[i] - '0');
	if (neg)
		*exp = -*exp;

	while (n > 1 && digits[n - 1] == '0') {
		n--;
		(*exp)++;
	}
	return n;
}

/* Write the @n @digits of a value of exponent @exp as a decimal number. */
static char *put_decimal(char *p, const char *digits, size_t n, int exp)
{
	int point = exp + 1;

	if (point <= 0) {
		p = put_bytes(p, "0.", 2);
		p = put_zeros(p, (size_t)-point);
		return put_bytes(p, digits, n);
	}
	if ((size_t)point < n) {
		p = put_bytes(p, digits, (size_t)point);
		*p++ = '.';
		return put_bytes(p, digits + point, n - (size_t)point);
	}
	p = put_bytes(p, digits, n);
	return put_zeros(p, (size_t)point - n);
}

/* Write the @n @digits of a value of exponent @exp with an exponent. */
static char *put_exponent(char *p, const char *digits, size_t n, int exp)
{
	*p++ = digits[0];
	if (n > 1) {
		*p++ = '.';
		p = put_bytes(p, digits + 1, n - 1);
	}
	*p++ = 'e';
	if (exp < 0) {
		*p++ = '-';
		exp = -exp;
	}
	if (exp >= 100)
		*p++ = (char)('0' + exp / 100);
	if (exp >= 10)
		*p++ = (char)('0' + exp / 10 % 10);
	*p++ = (char)('0' + exp % 10);
	return p;
}

/*
 * Write the value for which Ryu wrote the @len bytes at @text in the layout
 * of a read: a decimal number from 1e-4 up to 1e9, as the shortest digits
 * give it, and a number with an exponent otherwise.
 */
static char *put_shortest(char *p, const char *text, int len)
{
	char digits[DIGITS_MAX];
	size_t n;
	int exp;

	n = shortest(text, len, digits, &exp);
	if (exp >= -4 && exp < 9)
		return put_decimal(p, digits, n, exp);
	return put_exponent(p, digits, n, exp);
}

size_t nine_f32_format(uint32_t bits, char *buf)
{
	uint32_t mag = bits & ~F32_SIGN;
	char text[16], *p = buf;
	float v;

	if (mag > F32_EXP_MASK)
		return (size_t)(put_bytes(p, "nan", 3) - buf);
	if (bits & F32_SIGN)
		*p++ = '-';
	if (mag == F32_EXP_MASK)
		return (size_t)(put_bytes(p, "inf", 3) - buf);
	if (!mag)
		return (size_t)(put_bytes(p, "0", 1) - buf);

	memcpy(&v, &mag, sizeof(v));
	p = put_shortest(p, text, f2s_buffered_n(v, text));
	return (size_t)(p - buf);
}

size_t nine_f64_format(uint64_t bits, char *buf)
{
	uint64_t mag = bits & ~F64_SIGN;
	char text[25], *p = buf;
	double v;

	if (mag > F64_EXP_MASK)
		return (size_t)(put_bytes(p, "nan", 3) - buf);
	if (bits & F64_SIGN)
		*p++ = '-';
	if (mag == F64_EXP_MASK)
		return (size_t)(put_bytes(p, "inf", 3) - buf);
	if (!mag)
		return (size_t)(put_bytes(p, "0", 1) - buf);

	memcpy(&v, &mag, sizeof(v));
	p = put_shortest(p, text, d2s_buffered_n(v, text));
	return (size_t)(p - buf);
}

static const char *skip_digits(const char *s, const char *end)
{
	while (s < end && *s >= '0' && *s <= '9')
		s++;
	return s;
}

/*
 * Skip the digits at @s, of which there must be one or more, with no zero
 * before the first digit that is not zero. Return: their end, or NULL.
 */
static const char *skip_integer(const char *s, const char *end)
{
	const char *p = skip_digits(s, end);

	if (p == s || (p - s > 1 && *s == '0'))
		return NULL;
	return p;
}

/*
 * Tell whether the @len bytes at @s are a number as a read writes one: an
 * integer, then an optional point and digits, then an optional exponent,
 * which is an integer with an optional minus but is never -0. A zero may
 * end the digits after the point, as in 2.0.
 */
static bool is_number(const char *s, size_t len)
{
	const char *end = s + len, *at;

	if (s < end && *s == '-')
		s++;
	s = skip_integer(s, end);
	if (s && s < end && *s == '.') {
		at = s + 1;
		s = skip_digits(at, end);
		if (s == at)
			return false;
	}
	if (s && s < end && *s == 'e') {
		at = ++s;
		if (s < end && *s == '-')
			s++;
		s = skip_integer(s, end);
		if (s && at[0] == '-' && s - at == 2 && at[1] == '0')
			return false;
	}
	return s == end;
}

/* Tell whether the @len bytes at @text are the NUL-terminated @s. */
static bool text_is(const char *text, size_t len, const char *s)
{
	return strlen(s) == len && !memcmp(text, s, len);
}

/* Tell whether the @len bytes at @text are a word: nan, inf or -inf. */
static bool is_word(const char *text, size_t len)
{
	return text_is(text, len, "nan") || text_is(text, len, "inf") ||
	       text_is(text, len, "-inf");
}

int nine_f32_parse(const char *text, size_t len, float *v)
{
	uint32_t bits;
	ffc_result r;

	if (is_word(text, len)) {
		bits = text[0] == 'n' ? F32_QUIET_NAN : F32_EXP_MASK;
		if (text[0] == '-')
			bits |= F32_SIGN;
		memcpy(v, &bits, sizeof(bits));
		return 0;
	}
	if (!is_number(text, len))
		return -EINVAL;

	/* Out of range: too large for an f32, or not zero but rounds to it. */
	r = ffc_parse_float(len, text, v);
	if (r.outcome != FFC_OUTCOME_OK || r.ptr != text + len)
		return -EINVAL;
	return 0;
}

int nine_f64_parse(const char *text, size_t len, double *v)
{
	uint64_t bits;
	ffc_result r;

	if (is_word(text, len)) {
		bits = text[0] == 'n' ? F64_QUIET_NAN : F64_EXP_MASK;
		if (text[0] == '-')
			bits |= F64_SIGN;
		memcpy(v, &bits, sizeof(bits));
		return 0;
	}
	if (!is_number(text, len))
		return -EINVAL;

	/* Out of range: too large for an f64, or not zero but rounds to it. */
	r = ffc_parse_double(len, text, v);
	if (r.outcome != FFC_OUTCOME_OK || r.ptr != text + len)
		return -EINVAL;
	return 0;
}
