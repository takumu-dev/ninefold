/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "internal.h"

/* The size in bytes of each element type. */
static const unsigned char elem_sizes[] = {
	[NINE_I8] = 1, [NINE_I16] = 2, [NINE_I32] = 4, [NINE_I64] = 8,
	[NINE_U8] = 1, [NINE_U16] = 2, [NINE_U32] = 4, [NINE_U64] = 8,
};

static bool is_signed(const struct nine_field *f)
{
	return f->elem <= NINE_I64;
}

/* The largest value of an unsigned integer of @size bytes. */
static uint64_t unsigned_max(size_t size)
{
	return UINT64_MAX >> (64 - 8 * size);
}

/* Read the integer of @size bytes at @bin, as its unsigned bit pattern. */
static uint64_t load(const void *bin, size_t size)
{
	uint8_t v8;
	uint16_t v16;
	uint32_t v32;
	uint64_t v64;

	switch (size) {
	case 1:
		memcpy(&v8, bin, size);
		return v8;
	case 2:
		memcpy(&v16, bin, size);
		return v16;
	case 4:
		memcpy(&v32, bin, size);
		return v32;
	default:
		memcpy(&v64, bin, size);
		return v64;
	}
}

/* Write the low @size bytes of @v to @bin. */
static void store(void *bin, size_t size, uint64_t v)
{
	uint8_t v8 = (uint8_t)v;
	uint16_t v16 = (uint16_t)v;
	uint32_t v32 = (uint32_t)v;

	switch (size) {
	case 1:
		memcpy(bin, &v8, size);
		break;
	case 2:
		memcpy(bin, &v16, size);
		break;
	case 4:
		memcpy(bin, &v32, size);
		break;
	default:
		memcpy(bin, &v, size);
		break;
	}
}

bool nine_value_has_text(const struct nine_field *f)
{
	return f->elem >= NINE_I8 && f->elem <= NINE_U64 &&
	       f->shape == NINE_SCALAR && !f->values;
}

size_t nine_value_size(const struct nine_field *f)
{
	return elem_sizes[f->elem];
}

ptrdiff_t nine_value_format(const struct nine_field *f, const void *bin,
			    char *buf, size_t len)
{
	char text[sizeof("-9223372036854775808\n")];
	char *end = text + sizeof(text);
	char *p = end;
	size_t size = nine_value_size(f);
	uint64_t mag = load(bin, size);
	bool neg = is_signed(f) && mag >> (8 * size - 1);

	if (neg)
		mag = (0 - mag) & unsigned_max(size);

	*--p = '\n';
	do {
		*--p = (char)('0' + mag % 10);
		mag /= 10;
	} while (mag);
	if (neg)
		*--p = '-';

	if ((size_t)(end - p) > len)
		return -ERANGE;

	memcpy(buf, p, (size_t)(end - p));
	return end - p;
}

/* Parse @n decimal digits with no zero before the first non-zero digit. */
static bool parse_digits(const char *s, size_t n, uint64_t *v)
{
	uint64_t digit;
	size_t i;

	if (!n || (n > 1 && s[0] == '0'))
		return false;

	*v = 0;
	for (i = 0; i < n; i++) {
		if (s[i] < '0' || s[i] > '9')
			return false;
		digit = (uint64_t)(s[i] - '0');
		if (*v > (UINT64_MAX - digit) / 10)
			return false;
		*v = *v * 10 + digit;
	}
	return true;
}

/* Tell whether the value of sign @neg and magnitude @mag fits in @f. */
static bool fits(const struct nine_field *f, bool neg, uint64_t mag)
{
	uint64_t max = unsigned_max(nine_value_size(f));

	if (!is_signed(f))
		return !neg && mag <= max;

	return mag <= (max >> 1) + neg;
}

/*
 * Tell whether the value of sign @neg and magnitude @mag, which fits in @f,
 * is in the range of @f. A range of all zeros means no range.
 */
static bool in_range(const struct nine_field *f, bool neg, uint64_t mag)
{
	int64_t v;

	if (!is_signed(f))
		return (!f->range.u.min && !f->range.u.max) ||
		       (mag >= f->range.u.min && mag <= f->range.u.max);

	/* The step through mag - 1 keeps INT64_MIN from overflowing. */
	v = neg ? -(int64_t)(mag - 1) - 1 : (int64_t)mag;
	return (!f->range.i.min && !f->range.i.max) ||
	       (v >= f->range.i.min && v <= f->range.i.max);
}

int nine_value_parse(const struct nine_field *f, const char *text, size_t len,
		     void *bin)
{
	bool neg = false;
	uint64_t mag;

	if (len && text[len - 1] == '\n')
		len--;
	if (len && text[0] == '-') {
		neg = true;
		text++;
		len--;
	}
	if (!parse_digits(text, len, &mag) || (neg && !mag))
		return -EINVAL;
	if (!fits(f, neg, mag) || !in_range(f, neg, mag))
		return -EINVAL;

	store(bin, nine_value_size(f), neg ? 0 - mag : mag);
	return 0;
}
