#ifndef GARENA_UTIL_H
#define GARENA_UTIL_H 1

#ifdef DEBUG
 #define IFDEBUG(x) x
#else
 #define IFDEBUG(x)
#endif

#define HASH_SIZE 256

typedef struct llist_s *llist_t;
typedef struct cell_s *cell_t;
typedef struct ihash_s *ihash_t;
typedef struct ihashitem_s *ihashitem_t;
typedef unsigned int ihash_keytype;

int llist_is_empty(llist_t desc);
llist_t llist_alloc(void);
void *llist_head(llist_t desc);
void llist_del_head(llist_t desc);
int llist_add_tail(llist_t desc, void *val);
int llist_add_head(llist_t desc, void *val);
void llist_free(llist_t desc);
void llist_free_val(llist_t desc);
void llist_empty(llist_t desc);
void llist_empty_val(llist_t desc);
cell_t llist_iter(llist_t desc);
cell_t llist_next(cell_t cell);
void *llist_val(cell_t cell);
void llist_del_item(llist_t desc, void *val);
int llist_add_before(llist_t desc, void *to_compare, void *to_add);
  
int ihash_num(ihash_t ihash);
int ihash_put(ihash_t ihash, ihash_keytype key, void *value);
void *ihash_get(ihash_t ihash, ihash_keytype key);
int ihash_del(ihash_t ihash, ihash_keytype key);
void ihash_free(ihash_t ihash);
void ihash_free_val(ihash_t ihash);
ihash_t ihash_init();
ihashitem_t ihash_iter(ihash_t ihash);
ihashitem_t ihash_next(ihash_t ihash, ihashitem_t iter);
void *ihash_val(ihashitem_t item);
int ihash_is_empty(ihash_t ihash);

#endif
