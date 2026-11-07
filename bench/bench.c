/* SPDX-License-Identifier: Zlib */
/*
 * The benchmark. It mounts two demo file servers of NOPS entities each, one
 * with each variant's operations, and times batches of NOPS operations on
 * them. For each measurement it prints the median time of one operation,
 * its ratio to a reference, and a mark when the ratio is over its limit.
 *
 *	ninefold_bench [batches]
 *
 * The measurements that visit entities one at a time run twice: in ID
 * order, and in one fixed shuffled order. The others do the same work in
 * any order, so they run once. A bulk read or write moves all NOPS values in
 * one call, and its time is per value, as is the time of memcpy of the same
 * bytes. The kernel file row reads one small file.
 *
 * The baseline is a plain C loop over the demo's own column: the step from
 * ID to slot, then a copy of the position. Every read hands each value it
 * gets to consume(), which writes to a volatile variable, so that the
 * compiler keeps the work.
 *
 * The exit code is 0 whatever the timings. It is 1 only when the benchmark
 * cannot set up, or when a read gets the wrong values.
 */
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "demo.h"
#include "kernel_file.h"
#include "ninefold.h"

#define NOPS 10000
#define DEFAULT_BATCHES 51
#define MAX_BATCHES 100000

#define POS_SIZE (3 * sizeof(float))
#define PATH_SIZE 64

/* The orders of the entities, in this order. */
#define NORDERS 2
static const char *const order_names[NORDERS] = { "id", "shuffled" };

static volatile float sink;

static void consume(const float v[3])
{
	sink = v[0] + v[1] + v[2];
}

/*
 * One order of the entities, with everything a batch visits laid out in
 * it: entry i of each array belongs to the entity ids[i].
 */
struct order {
	uint32_t ids[NOPS];
	/* The entity's position. */
	float vals[NOPS][3];
	/* Its pos file in /world, open for reading and writing. */
	struct nine_handle hs[NOPS];
	/* Its pos file in /world for an even i, and in /variant for an odd i. */
	struct nine_handle alt[NOPS];
	/* The path of its pos file in /world. */
	char paths[NOPS][PATH_SIZE];
};

struct bench {
	struct nine_tree *t;
	struct demo *world;
	struct demo *variant;
	struct nine_ns *ns;
	/* The column of the demo at /world, and its bulk pos file. */
	struct demo_column col;
	struct nine_handle bulk;
	/* The values of the column, in slot order. */
	float (*column)[3];
	/* NOPS values, for the batched and bulk reads. */
	float (*buf)[3];
	int *errs;
	struct order *orders[NORDERS];
	struct kernel_file kf;
	bool has_kf;
};

static void baseline_read(struct bench *b, const struct order *o)
{
	float (*pos)[3] = b->col.pos;
	const uint32_t *slots = b->col.slots;
	float v[3];
	size_t i;

	for (i = 0; i < NOPS; i++) {
		memcpy(v, pos[slots[o->ids[i]]], POS_SIZE);
		consume(v);
	}
}

static void baseline_write(struct bench *b, const struct order *o)
{
	float (*pos)[3] = b->col.pos;
	const uint32_t *slots = b->col.slots;
	size_t i;

	for (i = 0; i < NOPS; i++)
		memcpy(pos[slots[o->ids[i]]], o->vals[i], POS_SIZE);
}

static void handle_read(struct bench *b, const struct order *o)
{
	float v[3] = { 0 };
	size_t i;

	(void)b;
	for (i = 0; i < NOPS; i++) {
		nine_read(&o->hs[i], v, POS_SIZE, 0);
		consume(v);
	}
}

static void handle_write(struct bench *b, const struct order *o)
{
	size_t i;

	(void)b;
	for (i = 0; i < NOPS; i++)
		nine_write(&o->hs[i], o->vals[i], POS_SIZE, 0);
}

static void batched_read(struct bench *b, const struct order *o)
{
	size_t i;

	nine_read_many(o->hs, NOPS, b->buf, POS_SIZE, b->errs);
	for (i = 0; i < NOPS; i++)
		consume(b->buf[i]);
}

static void alternating_read(struct bench *b, const struct order *o)
{
	float v[3] = { 0 };
	size_t i;

	(void)b;
	for (i = 0; i < NOPS; i++) {
		nine_read(&o->alt[i], v, POS_SIZE, 0);
		consume(v);
	}
}

static void path_lookup(struct bench *b, const struct order *o)
{
	struct nine_handle h;
	float v[3] = { 0 };
	size_t i;

	for (i = 0; i < NOPS; i++) {
		nine_open(b->ns, o->paths[i], NINE_RDONLY, &h);
		nine_read(&h, v, POS_SIZE, 0);
		consume(v);
	}
}

static void memcpy_out(struct bench *b, const struct order *o)
{
	(void)o;
	memcpy(b->buf, b->col.pos, NOPS * POS_SIZE);
	consume(b->buf[NOPS - 1]);
}

static void memcpy_in(struct bench *b, const struct order *o)
{
	(void)o;
	memcpy(b->col.pos, b->column, NOPS * POS_SIZE);
}

static void bulk_read(struct bench *b, const struct order *o)
{
	(void)o;
	nine_read(&b->bulk, b->buf, NOPS * POS_SIZE, 0);
	consume(b->buf[NOPS - 1]);
}

static void bulk_write(struct bench *b, const struct order *o)
{
	(void)o;
	nine_write(&b->bulk, b->column, NOPS * POS_SIZE, 0);
}

static void kernel_file(struct bench *b, const struct order *o)
{
	float v[3] = { 0 };
	size_t i;

	(void)o;
	for (i = 0; i < NOPS; i++) {
		kernel_file_read(&b->kf, v, POS_SIZE);
		consume(v);
	}
}

enum {
	BASELINE_READ,
	BASELINE_WRITE,
	HANDLE_READ,
	HANDLE_WRITE,
	BATCHED_READ,
	ALTERNATING_READ,
	PATH_LOOKUP,
	MEMCPY_OUT,
	MEMCPY_IN,
	BULK_READ,
	BULK_WRITE,
	KERNEL_FILE,
	NROWS
};

/*
 * struct row - One measurement.
 * @name: Its name in the table.
 * @batch: One batch of NOPS operations, in an order.
 * @ordered: Whether it runs in each order, or once.
 * @ref: The row it is compared with, which comes before it. An ordered row
 *	is compared with the same order of its reference, and a row that runs
 *	once with the first order.
 * @limit: The most times its reference it may take, or 0 for no limit.
 */
struct row {
	const char *name;
	void (*batch)(struct bench *b, const struct order *o);
	bool ordered;
	int ref;
	int limit;
};

static const struct row rows[NROWS] = {
	[BASELINE_READ] = { "baseline read", baseline_read, true, BASELINE_READ,
			    0 },
	[BASELINE_WRITE] = { "baseline write", baseline_write, true,
			     BASELINE_WRITE, 0 },
	[HANDLE_READ] = { "handle read", handle_read, true, BASELINE_READ, 5 },
	[HANDLE_WRITE] = { "handle write", handle_write, true, BASELINE_WRITE,
			   5 },
	[BATCHED_READ] = { "batched read", batched_read, true, BASELINE_READ,
			   5 },
	[ALTERNATING_READ] = { "alternating read", alternating_read, true,
			       BASELINE_READ, 5 },
	[PATH_LOOKUP] = { "path lookup", path_lookup, true, BASELINE_READ, 0 },
	[MEMCPY_OUT] = { "memcpy out", memcpy_out, false, MEMCPY_OUT, 0 },
	[MEMCPY_IN] = { "memcpy in", memcpy_in, false, MEMCPY_IN, 0 },
	[BULK_READ] = { "bulk read", bulk_read, false, MEMCPY_OUT, 2 },
	[BULK_WRITE] = { "bulk write", bulk_write, false, MEMCPY_IN, 2 },
	[KERNEL_FILE] = { "kernel file", kernel_file, false, BASELINE_READ, 0 },
};

static int64_t now_ns(void)
{
	struct timespec ts;

	timespec_get(&ts, TIME_UTC);
	return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}

static int compare_times(const void *a, const void *b)
{
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;

	return (x > y) - (x < y);
}

/*
 * Time @nbatches batches of @row in the order @o, after one more to warm
 * up, with room for each time in @times.
 *
 * Return: The median time of one operation in ns.
 */
static double time_row(struct bench *b, const struct row *row,
		       const struct order *o, int64_t *times, int nbatches)
{
	double median;
	int64_t start;
	int i;

	row->batch(b, o);
	for (i = 0; i < nbatches; i++) {
		start = now_ns();
		row->batch(b, o);
		times[i] = now_ns() - start;
	}

	qsort(times, (size_t)nbatches, sizeof(*times), compare_times);
	median = (double)times[nbatches / 2];
	if (nbatches % 2 == 0)
		median = (median + (double)times[nbatches / 2 - 1]) / 2;
	return median / NOPS;
}

static void print_header(int nbatches)
{
	printf("The median of %d batches of %d operations.\n", nbatches, NOPS);
	printf("A bulk row and a memcpy row move %d values at once, and give the time per value.\n",
	       NOPS);
#ifndef NDEBUG
	printf("This is not a release build, so the times mean little.\n");
#endif
	printf("\n%-17s %-9s %10s %8s  %-14s %s\n", "measurement", "order",
	       "ns/op", "ratio", "compared with", "limit");
}

static void print_row(const struct row *row, const char *order, double ns,
		      double ref_ns)
{
	double ratio = ref_ns > 0 ? ns / ref_ns : 0;
	char limit[8] = "-";

	if (row->limit)
		snprintf(limit, sizeof(limit), "%dx", row->limit);
	printf("%-17s %-9s %10.2f %8.2f  %-14s %s%s\n", row->name, order, ns,
	       ratio, rows[row->ref].name, limit,
	       row->limit && ratio > row->limit ? "  OVER" : "");
	fflush(stdout);
}

static void run_rows(struct bench *b, int64_t *times, int nbatches)
{
	double ns[NROWS][NORDERS] = { { 0 } };
	const struct row *row;
	int r, k, norders, ref_k;

	for (r = 0; r < NROWS; r++) {
		row = &rows[r];
		if (r == KERNEL_FILE && !b->has_kf) {
			printf("%-17s %-9s %10s\n", row->name, "-",
			       "skipped: Linux only");
			continue;
		}

		norders = row->ordered ? NORDERS : 1;
		for (k = 0; k < norders; k++) {
			ns[r][k] =
				time_row(b, row, b->orders[k], times, nbatches);
			ref_k = rows[row->ref].ordered ? k : 0;
			print_row(row, row->ordered ? order_names[k] : "-",
				  ns[r][k], ns[row->ref][ref_k]);
		}
	}
}

static int fail(const char *what, int err)
{
	fprintf(stderr, "bench: %s: error %d\n", what, -err);
	return err;
}

/* The position of the entity @id, in both demo instances. */
static void entity_pos(uint32_t id, float pos[3])
{
	pos[0] = (float)id + 0.5f;
	pos[1] = (float)id * -2.0f;
	pos[2] = (float)id * 0.25f;
}

/* Make NOPS entities, with the IDs 0 to NOPS - 1 in the same slots. */
static int create_entities(struct demo *d)
{
	float pos[3];
	uint32_t i, id;
	int err;

	for (i = 0; i < NOPS; i++) {
		err = demo_create(d, &id);
		if (err)
			return err;
		entity_pos(id, pos);
		demo_set_pos(d, id, pos);
	}
	return 0;
}

/* Shuffle @n IDs the same way on every run, with a fixed xorshift seed. */
static void shuffle(uint32_t *ids, uint32_t n)
{
	uint32_t x = 2463534242u, i, j, id;

	for (i = n - 1; i > 0; i--) {
		x ^= x << 13;
		x ^= x >> 17;
		x ^= x << 5;
		j = x % (i + 1);
		id = ids[i];
		ids[i] = ids[j];
		ids[j] = id;
	}
}

static int open_pos(struct nine_ns *ns, const char *mount, uint32_t id,
		    int mode, char *path, struct nine_handle *h)
{
	snprintf(path, PATH_SIZE, "%s/entities/%lu/components/transform/pos",
		 mount, (unsigned long)id);
	return nine_open(ns, path, mode, h);
}

static int fill_order(struct bench *b, struct order *o, bool shuffled)
{
	char path[PATH_SIZE];
	const char *mount;
	uint32_t i, id;
	int err;

	for (i = 0; i < NOPS; i++)
		o->ids[i] = i;
	if (shuffled)
		shuffle(o->ids, NOPS);

	for (i = 0; i < NOPS; i++) {
		id = o->ids[i];
		entity_pos(id, o->vals[i]);

		err = open_pos(b->ns, "/world", id, NINE_RDWR, o->paths[i],
			       &o->hs[i]);
		if (err)
			return fail(o->paths[i], err);

		mount = i % 2 ? "/variant" : "/world";
		err = open_pos(b->ns, mount, id, NINE_RDONLY, path, &o->alt[i]);
		if (err)
			return fail(path, err);
	}
	return 0;
}

static int make_tree(struct bench *b)
{
	const char *bulk = "/world/components/transform/pos";
	int err;

	b->t = nine_tree_new();
	b->world = demo_new();
	b->variant = demo_new();
	if (!b->t || !b->world || !b->variant)
		return fail("setup", -ENOMEM);

	err = create_entities(b->world);
	if (!err)
		err = create_entities(b->variant);
	if (err)
		return fail("create", err);

	err = nine_mount(b->t, "/world", demo_ops(), b->world);
	if (!err)
		err = nine_mount(b->t, "/variant", demo_variant_ops(),
				 b->variant);
	if (err)
		return fail("mount", err);
	b->ns = nine_tree_ns(b->t);

	err = nine_open(b->ns, bulk, NINE_RDWR, &b->bulk);
	if (err)
		return fail(bulk, err);

	demo_get_column(b->world, &b->col);
	return 0;
}

static int bench_init(struct bench *b)
{
	int err, k;

	memset(b, 0, sizeof(*b));
	b->column = malloc(NOPS * POS_SIZE);
	b->buf = malloc(NOPS * POS_SIZE);
	b->errs = malloc(NOPS * sizeof(*b->errs));
	if (!b->column || !b->buf || !b->errs)
		return fail("setup", -ENOMEM);
	for (k = 0; k < NORDERS; k++) {
		b->orders[k] = malloc(sizeof(*b->orders[k]));
		if (!b->orders[k])
			return fail("setup", -ENOMEM);
	}

	err = make_tree(b);
	if (err)
		return err;
	memcpy(b->column, b->col.pos, NOPS * POS_SIZE);

	for (k = 0; k < NORDERS; k++) {
		err = fill_order(b, b->orders[k], k > 0);
		if (err)
			return err;
	}

	err = kernel_file_make(&b->kf, b->column[0], POS_SIZE);
	if (err == -ENOTSUP)
		return 0;
	if (err)
		return fail("kernel file", err);
	b->has_kf = true;
	return 0;
}

static void bench_free(struct bench *b)
{
	int k;

	if (b->has_kf)
		kernel_file_remove(&b->kf);
	for (k = 0; k < NORDERS; k++)
		free(b->orders[k]);
	free(b->errs);
	free(b->buf);
	free(b->column);
	nine_tree_free(b->t);
	demo_free(b->variant);
	demo_free(b->world);
}

static bool misread(const char *what)
{
	fprintf(stderr, "bench: the %s got the wrong values\n", what);
	return false;
}

static bool read_matches(const struct nine_handle *h, const float want[3])
{
	float v[3];

	return nine_read(h, v, POS_SIZE, 0) == (ptrdiff_t)POS_SIZE &&
	       !memcmp(v, want, POS_SIZE);
}

static bool check_order(struct bench *b, const struct order *o)
{
	size_t i;

	for (i = 0; i < NOPS; i++) {
		if (memcmp(b->col.pos[b->col.slots[o->ids[i]]], o->vals[i],
			   POS_SIZE))
			return misread("baseline read");
		if (!read_matches(&o->hs[i], o->vals[i]))
			return misread("handle read");
		if (!read_matches(&o->alt[i], o->vals[i]))
			return misread("alternating read");
	}

	memset(b->buf, 0, NOPS * POS_SIZE);
	if (nine_read_many(o->hs, NOPS, b->buf, POS_SIZE, b->errs) ||
	    memcmp(b->buf, o->vals, NOPS * POS_SIZE))
		return misread("batched read");
	return true;
}

/*
 * Check that each read finds the values the entities were made with. The
 * writes write the same values, so this holds after them too.
 */
static bool check_reads(struct bench *b)
{
	float v[3];
	int k;

	for (k = 0; k < NORDERS; k++) {
		if (!check_order(b, b->orders[k]))
			return false;
	}

	memset(b->buf, 0, NOPS * POS_SIZE);
	if (nine_read(&b->bulk, b->buf, NOPS * POS_SIZE, 0) !=
		    (ptrdiff_t)(NOPS * POS_SIZE) ||
	    memcmp(b->buf, b->column, NOPS * POS_SIZE))
		return misread("bulk read");

	if (b->has_kf &&
	    (kernel_file_read(&b->kf, v, POS_SIZE) != (int)POS_SIZE ||
	     memcmp(v, b->column[0], POS_SIZE)))
		return misread("kernel file");
	return true;
}

static bool parse_batches(const char *arg, int *nbatches)
{
	char *end;
	long n;

	errno = 0;
	n = strtol(arg, &end, 10);
	if (errno || end == arg || *end || n < 1 || n > MAX_BATCHES)
		return false;

	*nbatches = (int)n;
	return true;
}

int main(int argc, char **argv)
{
	int nbatches = DEFAULT_BATCHES;
	int ret = EXIT_FAILURE;
	int64_t *times = NULL;
	struct bench b;

	if (argc > 2 || (argc == 2 && !parse_batches(argv[1], &nbatches))) {
		fprintf(stderr,
			"usage: ninefold_bench [batches]\n"
			"batches: 1 to %d, %d by default\n",
			MAX_BATCHES, DEFAULT_BATCHES);
		return EXIT_FAILURE;
	}

	if (bench_init(&b) || !check_reads(&b))
		goto out;
	times = malloc((size_t)nbatches * sizeof(*times));
	if (!times) {
		fail("setup", -ENOMEM);
		goto out;
	}

	print_header(nbatches);
	run_rows(&b, times, nbatches);
	if (check_reads(&b))
		ret = 0;
out:
	free(times);
	bench_free(&b);
	return ret;
}
