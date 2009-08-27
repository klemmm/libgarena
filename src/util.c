#include <stdlib.h>
#include <string.h>
#include <garena/util.h>


struct cell_s {
  void *val;
  struct cell_s *next;
};

struct llist_s {
  struct cell_s *head;
};  

int llist_is_empty(llist_t desc) {
  return(desc->head == NULL);
} 

llist_t llist_alloc(void) {
  llist_t tmp;
  tmp = malloc(sizeof(struct llist_s));
  if (tmp == NULL)
    return NULL;
  tmp->head = NULL;
  return(tmp);   
}

void *llist_val(cell_t cell) {
  return cell->val;
}


cell_t llist_iter(llist_t desc) {
  return desc->head;
}
cell_t llist_next(cell_t cell) {
  return cell->next;
}

void *llist_head(llist_t desc) {
  return (desc->head == NULL) ? NULL : desc->head->val;
}
 
void llist_del_head(llist_t desc) {
  if (desc->head != NULL)   
    desc->head = desc->head->next;
}

int llist_add_before(llist_t desc, void *to_compare, void *to_add) {
 cell_t ptr;
 cell_t tmp;
 tmp = malloc(sizeof(struct cell_s));
 if (tmp == NULL)
   return -1;
 tmp->val = to_add;
 tmp->next = NULL;
 
 if (desc->head == NULL) {
   desc->head = tmp;
   return 0;
 }
 
 if (desc->head->val == to_compare) {
   tmp->next = desc->head;
   desc->head = tmp;
   return 0;
 }
 
 for (ptr = desc->head; ptr->next != NULL; ptr = ptr->next) {
   if (ptr->next->val == to_compare) {
     tmp->next = ptr->next;
     ptr->next = tmp;
     return 0;
   }
 }
 return -1;
}


int llist_add_tail(llist_t desc, void *val) {
  cell_t tmp;
  cell_t ptr;

  tmp = malloc(sizeof(struct cell_s));
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
  cell_t tmp;

  tmp = malloc(sizeof(struct cell_s));
  if (tmp == NULL) 
    return -1;   
  
  tmp->next = desc->head;  
  tmp->val = val;
  desc->head = tmp;
  return 0;
}

void llist_del_item(llist_t desc, void *val) {
  cell_t todel;
  cell_t tmp = desc->head;
  
  if (tmp == NULL)
    return;
    
  if (tmp->val == val) {
    desc->head = tmp->next;
    free(tmp);
    return;
  }
  
  while(tmp->next != NULL) {
    if (tmp->next->val == val) {
      todel = tmp->next;
      tmp->next = tmp->next->next;
      free(todel);
      return;
    }
    tmp = tmp->next;
  }
}

void llist_free(llist_t desc) {
  cell_t tmp = desc->head;
  cell_t old = NULL;
  if (desc == NULL)
    return;  
  while (tmp != NULL) {
    old = tmp;
    tmp = tmp->next;
    free(old);
  }
  free(desc);
}  

void llist_free_val(llist_t desc) {
  cell_t tmp = desc->head;
  cell_t old = NULL;
  if (desc == NULL)
    return;
  
  while (tmp != NULL) {
    old = tmp;
    tmp = tmp->next;
    free(old->val);
    free(old);
  }
  free(desc);
}  

void llist_empty(llist_t desc) {
  cell_t tmp = desc->head;
  cell_t old = NULL;
  
  while (tmp != NULL) {
    old = tmp;
    tmp = tmp->next;
    free(old);
  }
  desc->head = NULL;
}  


void llist_empty_val(llist_t desc) {
  cell_t tmp = desc->head;
  cell_t old = NULL;
  
  while (tmp != NULL) {
    old = tmp;
    tmp = tmp->next;
    free(old->val);
    free(old);
  }
  desc->head = NULL;
}  

