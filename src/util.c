#include <stdlib.h>
#include <string.h>
#include <garena/util.h>


typedef struct cell_s {
  void *val;
  struct cell_s *next;
} cell_t;

struct llist_s{
  cell_t *head;
};  

int llist_is_empty(llist_t desc) {
  return(desc->head == NULL);
} 

llist_t llist_alloc(void) {
  llist_t tmp;
  tmp = malloc(sizeof(llist_t));
  if (tmp == NULL)
    return NULL;
  tmp->head = NULL;
  return(tmp);   
}

void *llist_head(llist_t desc) {
  return (desc->head == NULL) ? NULL : desc->head->val;
}
 
void llist_del_head(llist_t desc) {
  if (desc->head != NULL)   
    desc->head = desc->head->next;
}

int llist_add_tail(llist_t desc, void *val) {
  cell_t *tmp;
  cell_t *ptr;

  tmp = malloc(sizeof(cell_t));
  if (tmp == NULL)
    return -1;
 
  tmp->next = NULL;
  tmp->val = val;
  
  if (desc->head == NULL) {
    desc->head = tmp;
  } else {
    for (ptr = desc->head; ptr->next != NULL; ptr = ptr->next);
    ptr->next = tmp;
  }
  return 0;
} 

int llist_add_head(llist_t desc, void *val) {
  cell_t *tmp;

  tmp = malloc(sizeof(cell_t));
  if (tmp == NULL) 
    return -1;   
  
  tmp->next = desc->head;  
  tmp->val = val;
  desc->head = tmp;
  return 0;
}

void llist_free(llist_t desc) {
  cell_t *tmp = desc->head;
  cell_t *old = NULL;
  
  while (tmp != NULL) {
    old = tmp;
    tmp = tmp->next;
    free(old);
  }
  free(desc);
}  


