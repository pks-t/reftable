/*
Copyright 2020 Google LLC

Use of this source code is governed by a BSD-style
license that can be found in the LICENSE file or at
https://developers.google.com/open-source/licenses/bsd
*/

#include "reftable.h"
#include "record.h"
#include "reader.h"
#include "merged.h"

struct reftable_table_vtable {
	int (*seek_record)(void *tab, struct reftable_iterator *it,
			   struct reftable_record *);
	uint32_t (*hash_id)(void *tab);
	uint64_t (*min_update_index)(void *tab);
	uint64_t (*max_update_index)(void *tab);
};

static int reftable_reader_seek_void(void *tab, struct reftable_iterator *it,
				     struct reftable_record *rec)
{
	return reader_seek((struct reftable_reader *)tab, it, rec);
}

static uint32_t reftable_reader_hash_id_void(void *tab)
{
	return reftable_reader_hash_id((struct reftable_reader *)tab);
}

static uint64_t reftable_reader_min_update_index_void(void *tab)
{
	return reftable_reader_min_update_index((struct reftable_reader *)tab);
}

static uint64_t reftable_reader_max_update_index_void(void *tab)
{
	return reftable_reader_max_update_index((struct reftable_reader *)tab);
}

static struct reftable_table_vtable reader_vtable = {
	.seek_record = reftable_reader_seek_void,
	.hash_id = reftable_reader_hash_id_void,
	.min_update_index = reftable_reader_min_update_index_void,
	.max_update_index = reftable_reader_max_update_index_void,
};

static int reftable_merged_table_seek_void(void *tab,
					   struct reftable_iterator *it,
					   struct reftable_record *rec)
{
	return merged_table_seek_record((struct reftable_merged_table *)tab, it,
					rec);
}

static uint32_t reftable_merged_table_hash_id_void(void *tab)
{
	return reftable_merged_table_hash_id(
		(struct reftable_merged_table *)tab);
}

static uint64_t reftable_merged_table_min_update_index_void(void *tab)
{
	return reftable_merged_table_min_update_index(
		(struct reftable_merged_table *)tab);
}

static uint64_t reftable_merged_table_max_update_index_void(void *tab)
{
	return reftable_merged_table_max_update_index(
		(struct reftable_merged_table *)tab);
}

static struct reftable_table_vtable merged_table_vtable = {
	.seek_record = reftable_merged_table_seek_void,
	.hash_id = reftable_merged_table_hash_id_void,
	.min_update_index = reftable_merged_table_min_update_index_void,
	.max_update_index = reftable_merged_table_max_update_index_void,
};

int reftable_table_seek_ref(struct reftable_table *tab,
			    struct reftable_iterator *it, const char *name)
{
	struct reftable_ref_record ref = {
		.refname = (char *)name,
	};
	struct reftable_record rec = { 0 };
	reftable_record_from_ref(&rec, &ref);
	return tab->ops->seek_record(tab->table_arg, it, &rec);
}

void reftable_table_from_reader(struct reftable_table *tab,
				struct reftable_reader *reader)
{
	assert(tab->ops == NULL);
	tab->ops = &reader_vtable;
	tab->table_arg = reader;
}

void reftable_table_from_merged_table(struct reftable_table *tab,
				      struct reftable_merged_table *merged)
{
	assert(tab->ops == NULL);
	tab->ops = &merged_table_vtable;
	tab->table_arg = merged;
}

int reftable_table_read_ref(struct reftable_table *tab, const char *name,
			    struct reftable_ref_record *ref)
{
	struct reftable_iterator it = { 0 };
	int err = reftable_table_seek_ref(tab, &it, name);
	if (err)
		goto done;

	err = reftable_iterator_next_ref(&it, ref);
	if (err)
		goto done;

	if (strcmp(ref->refname, name) ||
	    reftable_ref_record_is_deletion(ref)) {
		reftable_ref_record_clear(ref);
		err = 1;
		goto done;
	}

done:
	reftable_iterator_destroy(&it);
	return err;
}

int reftable_table_seek_record(struct reftable_table *tab,
			       struct reftable_iterator *it,
			       struct reftable_record *rec)
{
	return tab->ops->seek_record(tab->table_arg, it, rec);
}

uint64_t reftable_table_max_update_index(struct reftable_table *tab)
{
	return tab->ops->max_update_index(tab->table_arg);
}

uint64_t reftable_table_min_update_index(struct reftable_table *tab)
{
	return tab->ops->min_update_index(tab->table_arg);
}

uint32_t reftable_table_hash_id(struct reftable_table *tab)
{
	return tab->ops->hash_id(tab->table_arg);
}
