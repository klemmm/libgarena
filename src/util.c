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

struct ihashitem_s {
  ihash_keytype key;
  void *value;
  struct ihashitem_s *next;
};

struct ihash_s {
  unsigned int size;
  struct ihashitem_s **h;
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

static unsigned int ihash_func(ihash_keytype id) {
  return (id ^ (id >> 8) ^ (id >> 16) ^ (id >> 24));
} 

int ihash_num(ihash_t ihash) {
  ihashitem_t item;
  unsigned int i;
  int num = 0;
  
  for (i = 0; i < ihash->size; i++) {
    for (item = ihash->h[i]; item != NULL; item = item->next)
      num++;
  }
  return num;
  
}  

int ihash_put(ihash_t ihash, ihash_keytype key, void *value) {
  ihashitem_t item;
  unsigned int hv = ihash_func(key) % ihash->size;

  item = malloc(sizeof(struct ihashitem_s));
  if (item == NULL) {
    return -1;
  }
   
  item->key = key;
  item->value = value;

  item->next = ihash->h[hv];
  ihash->h[hv] = item;

  return 0;
}

int ihash_is_empty(ihash_t ihash) {
  int i;
  for (i = 0; i < ihash->size; i++) {
    if (ihash->h[i] != NULL)
      return 0;
  }
  return 1;
}
ihashitem_t ihash_iter(ihash_t ihash) {
  int i;
  for (i = 0; i < ihash->size; i++) {
    if (ihash->h[i] != NULL)
      return ihash->h[i];
  }
  return NULL;
}

ihashitem_t ihash_next(ihash_t ihash, ihashitem_t iter) {
  ihashitem_t tmp;
  int i;
  if (iter->next != NULL)
    return iter->next;
  
  for (i = (ihash_func(iter->key) % ihash->size) + 1; i < ihash->size; i++) {
    if (ihash->h[i] != NULL)
      return ihash->h[i];
  }
  return NULL;
}


void *ihash_val(ihashitem_t item) {
  return item->value;
}

void *ihash_get(ihash_t ihash, ihash_keytype key) {
  ihashitem_t item;
  unsigned int hv = ihash_func(key) % ihash->size;

  for (item = ihash->h[hv]; item != NULL; item = item->next) {
    if (key == item->key)
      return item->value;
  }
   
  return NULL;
}

int ihash_del(ihash_t ihash, ihash_keytype key) {
  ihashitem_t item, *prev;
  unsigned int hv = ihash_func(key) % ihash->size;

  prev = &ihash->h[hv];
  item = NULL;
  for (item = ihash->h[hv]; item != NULL; item = item->next) {
    if (key == item->key)
    {
      *prev = item->next;
      free(item);
      return 0;  
    } else prev = &item->next;
  }
   
  return -1;
}

void ihash_free(ihash_t ihash) {
  unsigned int i;
  ihashitem_t item,old;
  
  for (i = 0 ; i < ihash->size; i++) {
    item = ihash->h[i];
    while(item != NULL) {
      old = item;
      item = item->next;
      free(old);
    }
  }  
  free(ihash->h);
  free(ihash);   
}

void ihash_free_val(ihash_t ihash) {
  unsigned int i;
  ihashitem_t item,old;
  
  for (i = 0 ; i < ihash->size; i++) {
    item = ihash->h[i];
    while(item != NULL) {
      old = item;
      item = item->next;
      free(old->value);
      free(old);
    }
  }  
  free(ihash->h);
  free(ihash);   
}

ihash_t ihash_init() {
  ihash_t ihash;
  int size = HASH_SIZE;
  
  ihash = malloc(sizeof(struct ihash_s));
  if(ihash == NULL)
    return  NULL; 
  ihash->h = malloc(size * sizeof(struct ihashitem_s*));
  if (ihash->h == NULL) {
    free(ihash);
    return (NULL);
  }
  ihash->size = size;
  memset(ihash->h, 0, size*sizeof(struct ihashitem_s*));
  return(ihash);
}
