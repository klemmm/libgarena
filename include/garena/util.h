#ifndef GARENA_UTIL_H
#define GARENA_UTIL_H 1

#ifdef DEBUG
 #define IFDEBUG(x) x
#else
 #define IFDEBUG(x)
#endif

typedef struct llist_s *llist_t;
typedef struct cell_s *cell_t;

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
  

#endif
