#ifndef API_H
#define API_H

#include <stdint.h>
#include <stdio.h> // debug

#include "basics.h"
#include "slice.h"
#include "constants.h"

typedef struct record_t record;

typedef struct {
  uint64 (*size)(void *source);
  int (*read_block)(void* source, byte **dest, uint64 off, uint32 size);
  int (*return_block)(void *source, byte *block);
  void (*close)(void *source);
} block_source_ops;

typedef struct {
  block_source_ops *ops;
} block_source;



typedef struct {
  bool unpadded ;
  uint32 block_size;
  uint32 min_update_index;
  uint32 max_update_index;
  bool index_objects;
  int restart_interval;
} write_options;

typedef struct {
  char* ref_name;
  uint64 update_index;
  byte* value;
  byte* target_value;
  char* target;
} ref_record;

typedef struct {
  char *ref_name;
  uint64 update_index;
  char *new_hash;
  char *old_hash;
  char *name;
  char *email;
  uint64 time;
  uint64 tz_offset;
  char *message;
} log_record;

typedef struct _record_ops record_ops;

// value type
typedef struct record_t {
  void *data;
  record_ops *ops;
} record;

void record_free(record rec);
void record_from_ref(record*rec, ref_record *refrec);
void record_from_log(record*rec, log_record *objrec);

typedef struct {
  int (*next)(void *iter_arg, record rec);
  void (*close)(void *iter_arg);
} iterator_ops;

typedef struct {
  iterator_ops *ops;
  void *iter_arg;
} iterator;

// < 0: error, 0 = OK, > 0: end of iteration
int iterator_next(iterator it, record rec);
void iterator_close(iterator it);
void iterator_set_empty(iterator *it);
			
typedef struct {
  int entries;
  int restarts;
  int blocks;
  int index_blocks;
  int max_index_level;

  uint64 offset;
  uint64 index_offset;
} block_stats;

typedef struct {
  int blocks;
  block_stats ref_stats;
  block_stats obj_stats;
  block_stats idx_stats;
  // todo: log stats.
  int object_id_len;
} stats;

#define IO_ERROR -2
#define FORMAT_ERROR -3

#endif
