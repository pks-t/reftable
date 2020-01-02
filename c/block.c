// Copyright 2019 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "block.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "zlib.h"

#include "reftable.h"
#include "constants.h"
#include "blocksource.h"
#include "record.h"

int block_writer_register_restart(struct block_writer *w, int n, bool restart,
                                  struct slice key);

void block_writer_init(struct block_writer *bw, byte typ, byte *buf,
                       uint32_t block_size, uint32_t header_off, int hash_size) {
  bw->buf = buf;
  bw->hash_size = hash_size;
  bw->block_size = block_size;
  bw->header_off = header_off;
  bw->buf[header_off] = typ;
  bw->next = header_off + 4;
  bw->restart_interval = 16;
}

byte block_writer_type(struct block_writer *bw) {
  return bw->buf[bw->header_off];
}

/* adds the record to the block. Returns -1 if it does not fit, 0 on
   success */
int block_writer_add(struct block_writer *w, struct record rec) {
  struct slice last = w->last_key;
  if (w->entries % w->restart_interval == 0) {
    last.len = 0;
  }

  struct slice out = {
      .buf = w->buf + w->next,
      .len = w->block_size - w->next,
  };

  struct slice start = out;

  bool restart = false;
  struct slice key = {};
  record_key(rec, &key);
  int n = encode_key(&restart, out, last, key, record_val_type(rec));
  if (n < 0) {
    goto err;
  }
  out.buf += n;
  out.len -= n;

  n = record_encode(rec, out, w->hash_size);
  if (n < 0) {
    goto err;
  }

  out.buf += n;
  out.len -= n;

  if (block_writer_register_restart(w, start.len - out.len, restart, key) < 0) {
    goto err;
  }

  free(slice_yield(&key));
  return 0;

err:
  free(slice_yield(&key));
  return -1;
}

int block_writer_register_restart(struct block_writer *w, int n, bool restart,
                                  struct slice key) {
  int rlen = w->restart_len;
  if (rlen >= MAX_RESTARTS) {
    restart = false;
  }

  if (restart) {
    rlen++;
  }
  if (2 + 3 * rlen + n > w->block_size - w->next) {
    return -1;
  }
  if (restart) {
    if (w->restart_len == w->restart_cap) {
      w->restart_cap = w->restart_cap * 2 + 1;
      w->restarts = realloc(w->restarts, sizeof(uint32_t) * w->restart_cap);
    }

    w->restarts[w->restart_len++] = w->next;
  }

  w->next += n;
  slice_copy(&w->last_key, key);
  w->entries++;
  return 0;
}

int block_writer_finish(struct block_writer *w) {
  for (int i = 0; i < w->restart_len; i++) {
    put_u24(w->buf + w->next, w->restarts[i]);
    w->next += 3;
  }

  put_u16(w->buf + w->next, w->restart_len);
  w->next += 2;
  put_u24(w->buf + 1 + w->header_off, w->next);

  if (block_writer_type(w) == BLOCK_TYPE_LOG) {
    int block_header_skip  = 4 + w->header_off;
    struct slice compressed = {};
    slice_resize(&compressed, w->next - block_header_skip);
    
    uLongf dest_len  = compressed.len;
    uLongf src_len  = w->next - block_header_skip;
    int z_err = compress2(compressed.buf, &dest_len,
			  w->buf + block_header_skip, src_len,
			  9);
    if (z_err != Z_OK) {
      free(slice_yield(&compressed));
      return ZLIB_ERROR;
    }
    memcpy(w->buf + block_header_skip, compressed.buf, dest_len);
    w->next = dest_len + block_header_skip;
  }
  return w->next;
}

byte block_reader_type(struct block_reader *r) {
  return r->block.data[r->header_off];
}

int block_reader_init(struct block_reader *br, struct block *block,
                      uint32_t header_off, uint32_t table_block_size, int hash_size) {
  uint32_t full_block_size = table_block_size;
  byte typ = block->data[header_off];

  if (!is_block_type(typ)) {
    return FORMAT_ERROR;
  }

  uint32_t sz = get_u24(block->data + header_off + 1);

  if (typ == BLOCK_TYPE_LOG) {
    struct slice uncompressed = {};
    slice_resize(&uncompressed, sz);
    int block_header_skip  = 4 + header_off;
    memcpy(uncompressed.buf, block->data, block_header_skip);
    
    uLongf dst_len = uncompressed.len - block_header_skip;
    uLongf src_len = block->len - block_header_skip;
    int z_err = uncompress2(uncompressed.buf + block_header_skip, &dst_len,
			    block->data + block_header_skip, &src_len);
    if (z_err != Z_OK) {
      free(slice_yield(&uncompressed));
      return ZLIB_ERROR;
    }
    
    block_source_return_block(block->source, block);
    block->data = uncompressed.buf;
    block->len = dst_len;
    block->source = malloc_block_source();
    full_block_size = src_len + block_header_skip;
  } else if (full_block_size == 0) {
    full_block_size = sz;
  }

  uint16_t restart_count = get_u16(block->data + sz - 2);
  uint32_t restart_start = sz - 2 - 3 * restart_count;

  byte *restart_bytes = block->data + restart_start;

  // transfer ownership.
  br->block = *block;
  block->data = NULL;
  block->len = 0;

  br->hash_size = hash_size;
  br->block_len = restart_start;
  br->full_block_size = full_block_size;
  br->header_off = header_off;
  br->restart_count = restart_count;
  br->restart_bytes = restart_bytes;

  return 0;
}

uint32_t block_reader_restart_offset(struct block_reader *br, int i) {
  return get_u24(br->restart_bytes + 3 * i);
}

void block_reader_start(struct block_reader *br, struct block_iter *it) {
  it->br = br;
  slice_resize(&it->last_key, 0);
  it->next_off = br->header_off + 4;
}

struct restart_find_args {
  struct slice key;
  struct block_reader *r;
  int error;
};

static int restart_key_less(int idx, void *args) {
  struct restart_find_args *a = (struct restart_find_args *)args;
  uint32_t off = block_reader_restart_offset(a->r, idx);
  struct slice in = {
      .buf = a->r->block.data,
      .len = a->r->block_len,
  };
  in.buf += off;
  in.len -= off;

  /* the restart key is verbatim in the block, so this could avoid the
     alloc for decoding the key */
  struct slice rkey = {};
  struct slice last_key = {};
  byte extra;
  int n = decode_key(&rkey, &extra, last_key, in);
  if (n < 0) {
    a->error = 1;
    return -1;
  }

  int result = slice_compare(a->key, rkey);

  free(slice_yield(&rkey));
  return result;
}

void block_iter_copy_from(struct block_iter *dest, struct block_iter *src) {
  dest->br = src->br;
  dest->next_off = src->next_off;
  slice_copy(&dest->last_key, src->last_key);
}

// return < 0 for error, 0 for OK, > 0 for EOF.
int block_iter_next(struct block_iter *it, struct record rec) {
  if (it->next_off >= it->br->block_len) {
    return 1;
  }

  struct slice in = {
      .buf = it->br->block.data + it->next_off,
      .len = it->br->block_len - it->next_off,
  };
  struct slice start = in;
  struct slice key = {};
  byte extra;
  int n = decode_key(&key, &extra, it->last_key, in);
  if (n < 0) {
    return -1;
  }

  in.buf += n;
  in.len -= n;
  n = record_decode(rec, key, extra, in, it->br->hash_size);
  if (n < 0) {
    return -1;
  }
  in.buf += n;
  in.len -= n;

  slice_copy(&it->last_key, key);
  it->next_off += start.len - in.len;
  free(slice_yield(&key));
  return 0;
}

int block_reader_first_key(struct block_reader *br, struct slice *key) {
  struct slice empty = {};
  int off = br->header_off + 4;
  struct slice in = {
      .buf = br->block.data + off,
      .len = br->block_len - off,
  };

  byte extra = 0;
  int n = decode_key(key, &extra, empty, in);
  if (n < 0) {
    return n;
  }
  return 0;
}

int block_iter_seek(struct block_iter *it, struct slice want) {
  return block_reader_seek(it->br, it, want);
}

void block_iter_close(struct block_iter *it) {
  free(slice_yield(&it->last_key));
}

int block_reader_seek(struct block_reader *br, struct block_iter *it,
                      struct slice want) {
  struct restart_find_args args = {
      .key = want,
      .r = br,
  };

  int i = binsearch(br->restart_count, &restart_key_less, &args);
  if (args.error) {
    return -1;
  }

  it->br = br;
  if (i > 0) {
    i--;
    it->next_off = block_reader_restart_offset(br, i);
  } else {
    it->next_off = br->header_off + 4;
  }

  struct record rec = new_record(block_reader_type(br));
  struct slice key = {};
  int result = 0;
  struct block_iter next = {};
  while (true) {
    block_iter_copy_from(&next, it);

    int err = block_iter_next(&next, rec);
    if (err < 0) {
      result = -1;
      goto exit;
    }

    record_key(rec, &key);

    if (err > 0 || slice_compare(key, want) >= 0) {
      result = 0;
      goto exit;
    }

    block_iter_copy_from(it, &next);
  }

exit:
  free(slice_yield(&key));
  free(slice_yield(&next.last_key));
  record_clear(rec);
  free(record_yield(&rec));
  return result;
}

void block_writer_reset(struct block_writer *bw) {
  bw->restart_len = 0;
  bw->last_key.len = 0;
}

void block_writer_clear(struct block_writer *bw) {
  free(bw->restarts);
  bw->restarts = NULL;
  free(slice_yield(&bw->last_key));
  // the block is not owned.
}
