/* SPDX-License-Identifier: Zlib */
#include <math.h>
#include <stdint.h>
#include <string.h>

#include "internal.h"

/* The longest enum name: with its \n it fits in NINE_TEXT_MAX. */
#define ENUM_NAME_MAX (NINE_TEXT_MAX - 1)

/*
 * Where converting a double to an f32 stops giving a useful value. A
 * magnitude at or above F32_OVERFLOW rounds to infinity, and one at or below
 * F32_UNDERFLOW rounds to zero.
 */
#define F32_OVERFLOW (0x1.ffffffp127)
#define F32_UNDERFLOW (0x1p-150)

/* The largest value of each signed element type. */
static const int64_t signed_max[] = {
	[NINE_I8] = INT8_MAX,
	[NINE_I16] = INT16_MAX,
	[NINE_I32] = INT32_MAX,
	[NINE_I64] = INT64_MAX,
};

/* The largest value of each unsigned element type. */
static const uint64_t unsigned_max[] = {
	[NINE_U8] = UINT8_MAX,
	[NINE_U16] = UINT16_MAX,
	[NINE_U32] = UINT32_MAX,
	[NINE_U64] = UINT64_MAX,
};

static bool is_signed(enum nine_elem e)
{
	return e >= NINE_I8 && e <= NINE_I64;
}

static bool is_unsigned(enum nine_elem e)
{
	return e >= NINE_U8 && e <= NINE_U64;
}

static bool is_float(enum nine_elem e)
{
	return e == NINE_F32 || e == NINE_F64;
}

static bool is_integer(enum nine_elem e)
{
	return is_signed(e) || is_unsigned(e);
}

static bool is_enum(const struct nine_field *f)
{
	return f->values || f->nvalues;
}

/* A bool and an enum are the two kinds of field that are not numeric. */
static bool is_numeric(const struct nine_field *f)
{
	return !is_enum(f) && (is_integer(f->elem) || is_float(f->elem));
}

/* Tell whether @f has a known element type, in a shape permitted for it. */
static bool type_valid(const struct nine_field *f)
{
	switch (f->shape) {
	case NINE_SCALAR:
		return is_integer(f->elem) || is_float(f->elem) ||
		       f->elem == NINE_BOOL;
	case NINE_VEC2:
	case NINE_VEC3:
	case NINE_VEC4:
		return is_numeric(f);
	case NINE_QUAT:
		return is_numeric(f) && is_float(f->elem);
	default:
		return false;
	}
}

/* ASCII text with no spaces, and only on a numeric field. */
static bool unit_valid(const struct nine_field *f)
{
	const unsigned char *p = (const unsigned char *)f->unit;

	if (!p)
		return true;
	if (!is_numeric(f))
		return false;

	for (; *p; p++) {
		if (*p <= ' ' || *p >= 0x7f)
			return false;
	}
	return true;
}

static bool fits_signed(enum nine_elem e, int64_t v)
{
	return v >= -signed_max[e] - 1 && v <= signed_max[e];
}

/* Tell whether an element of type @e can hold the limit @v. */
static bool float_limit_valid(enum nine_elem e, double v)
{
	double mag = v < 0 ? -v : v;

	if (isnan(v))
		return false;
	if (e == NINE_F64 || isinf(v) || v == 0)
		return true;

	return mag < F32_OVERFLOW && mag > F32_UNDERFLOW;
}

static bool range_valid(const struct nine_field *f)
{
	if (!is_numeric(f))
		return !f->range.u.min && !f->range.u.max;

	if (is_signed(f->elem))
		return f->range.i.min <= f->range.i.max &&
		       fits_signed(f->elem, f->range.i.min) &&
		       fits_signed(f->elem, f->range.i.max);
	if (is_unsigned(f->elem))
		return f->range.u.min <= f->range.u.max &&
		       f->range.u.max <= unsigned_max[f->elem];

	return float_limit_valid(f->elem, f->range.f.min) &&
	       float_limit_valid(f->elem, f->range.f.max) &&
	       f->range.f.min <= f->range.f.max;
}

static bool is_name_start(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static bool enum_name_valid(const char *name)
{
	size_t i;

	if (!name || !is_name_start(name[0]))
		return false;

	for (i = 1; name[i]; i++) {
		if (i == ENUM_NAME_MAX || !(is_name_start(name[i]) ||
					    (name[i] >= '0' && name[i] <= '9')))
			return false;
	}
	return true;
}

static bool enum_value_fits(enum nine_elem e, int64_t v)
{
	if (is_signed(e))
		return fits_signed(e, v);

	return v >= 0 && (uint64_t)v <= unsigned_max[e];
}

/* Tell whether a name before number @i of @f is the same as number @i. */
static bool enum_name_repeated(const struct nine_field *f, size_t i)
{
	size_t j;

	for (j = 0; j < i; j++) {
		if (strcmp(f->values[j].name, f->values[i].name) == 0)
			return true;
	}
	return false;
}

static bool enum_valid(const struct nine_field *f)
{
	size_t i;

	if (!is_enum(f))
		return true;
	if (!f->values || !f->nvalues || !is_integer(f->elem))
		return false;

	for (i = 0; i < f->nvalues; i++) {
		if (!enum_name_valid(f->values[i].name) ||
		    !enum_value_fits(f->elem, f->values[i].value) ||
		    enum_name_repeated(f, i))
			return false;
	}
	return true;
}

static bool field_valid(const struct nine_field *f)
{
	return nine_path_name_valid(f->name) && type_valid(f) &&
	       unit_valid(f) && range_valid(f) && enum_valid(f) &&
	       !(f->message && strchr(f->message, '\n'));
}

/* Tell whether a field before number @i of @s has the name of number @i. */
static bool field_name_repeated(const struct nine_schema *s, size_t i)
{
	size_t j;

	for (j = 0; j < i; j++) {
		if (strcmp(s->fields[j].name, s->fields[i].name) == 0)
			return true;
	}
	return false;
}

/* Examine all of @s against the rules of a descriptor. */
bool nine_schema_valid(const struct nine_schema *s)
{
	size_t i;

	if (!s || !nine_path_name_valid(s->name) || !s->fields || !s->nfields)
		return false;

	for (i = 0; i < s->nfields; i++) {
		if (!field_valid(&s->fields[i]) || field_name_repeated(s, i))
			return false;
	}
	return true;
}
