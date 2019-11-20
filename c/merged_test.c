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

#include <string.h>

#include "api.h"
#include "basics.h"
#include "block.h"
#include "reader.h"
#include "record.h"
#include "test_framework.h"
#include "pq.h"

void test_pq(void) {
  char *names[53];
  int N = 53;

  for (int i = 0; i < N; i++) {
    char name[100];
    sprintf(name, "%02d", i);
    names[i] = strdup(name);
  }

  merged_iter_pqueue pq  = {};

  int i = 1;
  do  {
    record rec = new_record(BLOCK_TYPE_REF);
    record_as_ref(rec)->ref_name = names[i];

    pq_entry e = {
		  .rec = rec,
    };
    merged_iter_pqueue_add(&pq, e);
    merged_iter_pqueue_check(pq);
    i = (i * 7 )% N;
  } while (i != 1);

  const char *last = NULL;
  while (!merged_iter_pqueue_is_empty(pq)) {
    pq_entry e = merged_iter_pqueue_remove(&pq);
    merged_iter_pqueue_check(pq);
    ref_record*ref = record_as_ref(e.rec);

    if (last != NULL) {
      assert(strcmp(last, ref->ref_name) < 0);
    }
    last = ref->ref_name;
    ref->ref_name = NULL;
    free(ref);
  }

  for (int i = 0; i <N; i++) {
    free(names[i]);
  }
}

void set_test_hash(byte *p, int i) {
  memset(p, (byte)i, HASH_SIZE);
}

void write_test_table(slice *buf, ref_record refs [], int n)  {
  int min = 0xffffffff;
  int max = 0;
  for (int i = 0; i < n; i++) {
    uint64 ui = refs[i].update_index;
    if (ui > max) {
      max =ui;
    }
    if (ui < min) {
      min = ui;
    }
  }
  
  write_options opts = {
      .block_size = 256,
      .max_update_index = max,
      .min_update_index = min,
  };

  writer *w = new_writer(&slice_write_void, buf, &opts);

  for (int i = 0; i < n; i++) {
    uint64 before = refs[i].update_index;
    int n = writer_add_ref(w, &refs[i]);
    assert(n == 0);
    assert(before == refs[i].update_index);
  }

  int err = writer_close(w);
  assert(err == 0);

  writer_free(w);
  w = NULL;
}

void test_merged(void) {
  byte hash1[HASH_SIZE];
  byte hash2[HASH_SIZE];

  set_test_hash(hash1, 1);
  set_test_hash(hash2, 2);
  ref_record r1[] =
    {
     {
      .ref_name = "a",
      .update_index = 1,
      .value = hash1,
     },
     {
      .ref_name = "b",
      .update_index = 1,
      .value = hash1,
     },
     {
      .ref_name = "c",
      .update_index = 1,
      .value = hash1,
     }
    };
  ref_record r2[] =
    {
      {
       .ref_name = "a",
       .update_index = 2,
      }
    };
  ref_record r3[] =
    {
     {
      .ref_name = "c",
      .update_index = 3,
      .value = hash2,
     },
     {
      .ref_name = "d",
      .update_index = 3,
      .value = hash1,
     },
    };

  ref_record *refs[] = {r1, r2, r3};
  int sizes[3] = {3, 1,2 };
  slice buf[3] = {};
  block_source source[3] = {};
  reader *rd[3] = {};
  for (int i =0 ; i < 3;i++) {
    write_test_table(&buf[i], refs[i] , sizes[i]);
    block_source_from_slice(&source[i], &buf[i]);

    int err = new_reader(&rd[i], source[i]);
    assert(err == 0);
  }

  merged_table *mt = NULL;
  int err = new_merged_table(&mt, rd, 3);
  assert(err == 0);

  iterator it = {};
  ref_record start = {
		     .ref_name = "a",
  };
  err = merged_table_seek_ref(mt, &it, &start);
  assert(err == 0);
  
  ref_record *out = NULL;
  int len = 0;
  int cap = 0;
  while (len < 100) {
    ref_record ref = {};
    int err = iterator_next_ref(it, &ref);
    if (err > 0){
      break;
    }
    ref_record_print(&ref);
    if (len == cap) {
      cap = 2*cap+1;
      out = realloc(out, sizeof(ref_record)*cap);
    }
    out[len++] = ref;
  }
  
  ref_record want[] =
    {
     r2[0],
     r1[1],
     r3[0],
     r3[1],
    };
  assert(ARRAYSIZE(want) == len);
  for (int i = 0; i < len; i++) {
    assert(ref_record_equal(&want[i], &out[i]));
  }
}


int main() {
  add_test_case("test_pq", &test_pq);
  add_test_case("test_merged", &test_merged);  
  test_main();
}