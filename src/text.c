/* SPDX-License-Identifier: Zlib */
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "internal.h"

/* The size in bytes of each element type. */
static const unsigned char elem_sizes[] = {
	[NINE_I8] = 1,	[NINE_I16] = 2, [NINE_I32] = 4,	 [NINE_I64] = 8,
	[NINE_U8] = 1,	[NINE_U16] = 2, [NINE_U32] = 4,	 [NINE_U64] = 8,
	[NINE_F32] = 4, [NINE_F64] = 8, [NINE_BOOL] = 1,
};

/* The number of elements of each shape that has a text form. */
static const unsigned char shape_counts[] = {
	[NINE_SCALAR] = 1, [NINE_VEC2] = 2, [NINE_VEC3] = 3,
	[NINE_VEC4] = 4,   [NINE_QUAT] = 4,
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

static bool is_integer(enum nine_elem e)
{
	return e >= NINE_I8 && e <= NINE_U64;
}

static bool is_float(enum nine_elem e)
{
	return e == NINE_F32 || e == NINE_F64;
}

/*
 * A bool and an enum are scalars, any number may be a vector too, and only
 * a float may be a quat.
 */
bool nine_value_has_text(const struct nine_field *f)
{
	if (f->elem == NINE_BOOL)
		return f->shape == NINE_SCALAR && !f->values;
	if (!is_integer(f->elem) && !is_float(f->elem))
		return false;
	if (f->values)
		return f->shape == NINE_SCALAR;
	if (f->shape == NINE_QUAT)
		return is_float(f->elem);

	return f->shape == NINE_SCALAR || f->shape == NINE_VEC2 ||
	       f->shape == NINE_VEC3 || f->shape == NINE_VEC4;
}

size_t nine_value_size(const struct nine_field *f)
{
	return elem_sizes[f->elem] * shape_counts[f->shape];
}

/* Give @buf of @len bytes the @n bytes at @s and a newline. */
static ptrdiff_t put_line(char *buf, size_t len, const char *s, size_t n)
{
	if (n >= len)
		return -ERANGE;

	memcpy(buf, s, n);
	buf[n] = '\n';
	return (ptrdiff_t)n + 1;
}

/*
 * Write the decimal form of the integer at @bin so that it ends just before
 * @end. Return: where it starts.
 */
static char *format_int(const struct nine_field *f, const void *bin, char *end)
{
	size_t size = elem_sizes[f->elem];
	uint64_t mag = load(bin, size);
	bool neg = is_signed(f) && mag >> (8 * size - 1);
	char *p = end;

	if (neg)
		mag = (0 - mag) & unsigned_max(size);

	do {
		*--p = (char)('0' + mag % 10);
		mag /= 10;
	} while (mag);
	if (neg)
		*--p = '-';
	return p;
}

/*
 * Write the text of the float at @bin so that it ends just before @end.
 * Return: where it starts.
 */
static char *format_float(const struct nine_field *f, const void *bin,
			  char *end)
{
	char text[NINE_F64_TEXT_MAX];
	uint32_t bits32;
	uint64_t bits64;
	size_t n;

	if (f->elem == NINE_F32) {
		memcpy(&bits32, bin, sizeof(bits32));
		n = nine_f32_format(bits32, text);
	} else {
		memcpy(&bits64, bin, sizeof(bits64));
		n = nine_f64_format(bits64, text);
	}
	memcpy(end - n, text, n);
	return end - n;
}

/* The first name of @f listed for the integer at @bin, or NULL for none. */
static const char *enum_name(const struct nine_field *f, const void *bin)
{
	size_t size = elem_sizes[f->elem];
	uint64_t sign = (uint64_t)1 << (8 * size - 1);
	uint64_t bits = load(bin, size);
	const struct nine_enum_value *e;
	size_t i;

	/* Compare as 64-bit patterns, so sign-extend a signed value. */
	if (is_signed(f))
		bits = (bits ^ sign) - sign;

	for (i = 0; i < f->nvalues; i++) {
		e = &f->values[i];
		if ((uint64_t)e->value == bits &&
		    (is_signed(f) || e->value >= 0))
			return e->name;
	}
	return NULL;
}

ptrdiff_t nine_value_format(const struct nine_field *f, const void *bin,
			    char *buf, size_t len)
{
	/* Four of the longest element, an f64, with a space after each. */
	char text[4 * (NINE_F64_TEXT_MAX + 1)];
	char *end = text + sizeof(text);
	size_t size = elem_sizes[f->elem];
	const char *name;
	char *p = end;
	size_t i;

	if (f->elem == NINE_BOOL) {
		if (*(const unsigned char *)bin)
			return put_line(buf, len, "true", 4);
		return put_line(buf, len, "false", 5);
	}
	if (f->values) {
		name = enum_name(f, bin);
		if (name)
			return put_line(buf, len, name, strlen(name));
	}

	/* Each element is written backwards, so the last one comes first. */
	for (i = shape_counts[f->shape]; i-- > 0;) {
		if (is_float(f->elem))
			p = format_float(f, (const char *)bin + i * size, p);
		else
			p = format_int(f, (const char *)bin + i * size, p);
		if (i)
			*--p = ' ';
	}
	return put_line(buf, len, p, (size_t)(end - p));
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
	uint64_t max = unsigned_max(elem_sizes[f->elem]);

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

/* Tell whether the @len bytes at @text are the NUL-terminated @s. */
static bool text_is(const char *text, size_t len, const char *s)
{
	return strlen(s) == len && !memcmp(text, s, len);
}

static int parse_bool(const char *text, size_t len, void *bin)
{
	unsigned char v;

	if (text_is(text, len, "true"))
		v = 1;
	else if (text_is(text, len, "false"))
		v = 0;
	else
		return -EINVAL;

	memcpy(bin, &v, 1);
	return 0;
}

/* Only a listed name is accepted, never a number. */
static int parse_enum(const struct nine_field *f, const char *text, size_t len,
		      void *bin)
{
	size_t i;

	for (i = 0; i < f->nvalues; i++) {
		if (text_is(text, len, f->values[i].name)) {
			store(bin, elem_sizes[f->elem],
			      (uint64_t)f->values[i].value);
			return 0;
		}
	}
	return -EINVAL;
}

static int parse_int(const struct nine_field *f, const char *text, size_t len,
		     void *bin)
{
	bool neg = false;
	uint64_t mag;

	if (len && text[0] == '-') {
		neg = true;
		text++;
		len--;
	}
	if (!parse_digits(text, len, &mag) || (neg && !mag))
		return -EINVAL;
	if (!fits(f, neg, mag) || !in_range(f, neg, mag))
		return -EINVAL;

	store(bin, elem_sizes[f->elem], neg ? 0 - mag : mag);
	return 0;
}

/*
 * Tell whether @v is in the range of @f, an f32: each limit becomes an f32
 * first. A range of all zeros means no range, and a range refuses a NaN.
 */
static bool f32_in_range(const struct nine_field *f, float v)
{
	if (!f->range.u.min && !f->range.u.max)
		return true;

	return v >= (float)f->range.f.min && v <= (float)f->range.f.max;
}

static int parse_f32(const struct nine_field *f, const char *text, size_t len,
		     void *bin)
{
	float v;

	if (nine_f32_parse(text, len, &v) || !f32_in_range(f, v))
		return -EINVAL;

	memcpy(bin, &v, sizeof(v));
	return 0;
}

/*
 * Tell whether @v is in the range of @f, an f64, whose limits are already
 * f64s. A range of all zeros means no range, and a range refuses a NaN.
 */
static bool f64_in_range(const struct nine_field *f, double v)
{
	if (!f->range.u.min && !f->range.u.max)
		return true;

	return v >= f->range.f.min && v <= f->range.f.max;
}

static int parse_f64(const struct nine_field *f, const char *text, size_t len,
		     void *bin)
{
	double v;

	if (nine_f64_parse(text, len, &v) || !f64_in_range(f, v))
		return -EINVAL;

	memcpy(bin, &v, sizeof(v));
	return 0;
}

static int parse_elem(const struct nine_field *f, const char *text, size_t len,
		      void *bin)
{
	if (f->elem == NINE_F32)
		return parse_f32(f, text, len, bin);
	if (f->elem == NINE_F64)
		return parse_f64(f, text, len, bin);
	return parse_int(f, text, len, bin);
}

/* Parse the elements of a number, with one space between them. */
static int parse_elems(const struct nine_field *f, const char *text, size_t len,
		       void *bin)
{
	size_t n = shape_counts[f->shape], size = elem_sizes[f->elem];
	const char *space;
	size_t i, elen;
	int err;

	for (i = 0; i < n; i++) {
		/* Each element but the last ends at a space. */
		space = len ? memchr(text, ' ', len) : NULL;
		if (!space != (i == n - 1))
			return -EINVAL;

		elen = space ? (size_t)(space - text) : len;
		err = parse_elem(f, text, elen, (char *)bin + i * size);
		if (err)
			return err;
		if (space) {
			text += elen + 1;
			len -= elen + 1;
		}
	}
	return 0;
}

int nine_value_parse(const struct nine_field *f, const char *text, size_t len,
		     void *bin)
{
	if (len && text[len - 1] == '\n')
		len--;

	if (f->elem == NINE_BOOL)
		return parse_bool(text, len, bin);
	if (f->values)
		return parse_enum(f, text, len, bin);
	return parse_elems(f, text, len, bin);
}
