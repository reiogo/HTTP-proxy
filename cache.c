#include "cache.h"
/* Helper Prototypes */
/* void _insert(void **cache, cacheinfo_t **info_list, */
/*         char *requestline, void *ptr, int filesize); */
int _find(cache_t *cache, char *requestline);
void _clear_obj(cache_t *cache, int idx);
void _lru_insert(cache_t *cache, int added_idx);
void _lru_add_end(lru_t *new_node, lru_t *tail);
int _lru_peek(cache_t *cache);
void _lru_take_out(lru_t *node);
void _lru_remove(lru_t **assoc_arr, int obj_idx);
int _evict(cache_t *cache);
int _find_empty(cache_t *cache);
void visualize_lru(cache_t *cache);

/* Test Prototypes */
void test_cache();
void test_cache_clear();
void test_cache_insert();
void test_lru_add_end();
void test_lru_helper();
void test_find_helper();
void test_find_empty_helper();
void test_evict_helper();


/* Initialize the cache */
void cache_init(cache_t *cache, int n)
{
    lru_t *head, *tail;

    cache->obj_list = Calloc(n, sizeof(cacheinfo_t));
    cache->n = n;
    cache->slots = n;

    /* lru */
    head = Malloc(sizeof(lru_t));
    tail = Malloc(sizeof(lru_t));

    head->prev = NULL;
    head->obj_idx = -1;
    head->next = tail;

    tail->prev = head;
    tail->obj_idx = -1;
    tail->next = NULL;

    cache->lru_head = head;
    cache->lru_tail = tail;
    cache->assoc_arr = Calloc(n, sizeof(lru_t *));
    
    /* Concurrency related variables */
    cache->read_cnt = 0;
    Sem_init(&(cache->rc_mutex), 0, 1);
    Sem_init(&(cache->w), 0, 1);

}

/* Clear a given object
 * Checks that the obj does not have a filesize of 0*/
void _clear_obj(cache_t *cache, int obj_idx)
{
    if (cache->obj_list[obj_idx].filesize != 0) {
        Free(cache->obj_list[obj_idx].requestline);
        cache->obj_list[obj_idx].requestline = NULL;
        cache->obj_list[obj_idx].filesize = 0;
        Free(cache->obj_list[obj_idx].contentp);
        cache->obj_list[obj_idx].contentp = NULL;
    }
}

/* Frees the cache allocated memory and zeros out the variables */
/* Does not free the cache struct */
void cache_clear(cache_t *cache)
{
    size_t i;
    for(i = 0; i < cache->n; i++){
        if (cache->obj_list[i].filesize != 0){
            _clear_obj(cache, i);
        }
        if (cache->assoc_arr[i] != NULL) {
            Free(cache->assoc_arr[i]);
            cache->assoc_arr[i] = NULL;
        }
    }
    cache->n = 0;
    cache->slots = 0;
    cache->read_cnt = 0;
}

void test_cache_clear()
{
    cache_t c;
    char *r, *co;

    cache_init(&c, 3);
    r = Malloc(2 * sizeof(char));
    co = Malloc(4 * sizeof(char));
    strcpy(r, "hi");
    strcpy(co, "hihi");
    c.obj_list[0].requestline = r;
    c.obj_list[0].filesize = 4;
    c.obj_list[0].contentp = co;
    cache_clear(&c);
    ASSERT(r == NULL);
    ASSERT(co == NULL);
}

/* Insert cache object into the cache
 * If cache is full LRU evict and insert
 * */
void cache_insert(cache_t *cache, char *requestline, void *ptr, size_t filesize)
{
    ASSERT(filesize > 0);
    ASSERT(cache->slots >= 0);
    int new_idx;


    P(&(cache->w)); /* Write lock */

    /* Find new index */
    if (cache->slots <= 0) {
        new_idx = _evict(cache);
    } else {
        new_idx = _find_empty(cache);
    }

    /* Fill the obj */
    cache->obj_list[new_idx].requestline 
        = Malloc(sizeof(char) * strlen(requestline));

    strcpy(cache->obj_list[new_idx].requestline, requestline);
    cache->obj_list[new_idx].filesize = filesize;
    cache->obj_list[new_idx].contentp = Malloc(sizeof(char) * filesize);
    memcpy(cache->obj_list[new_idx].contentp, ptr, filesize);
    cache->slots -= 1;

    /* Update the cache */
    _lru_insert(cache, new_idx);

    V(&(cache->w)); /* Write unlock */

    return;
}

void test_cache_insert()
{
    cache_t c;
    cache_init(&c, 3);
    char data[] = "Hin\n";
    char request[] = "GET http://www.google.com HTTP/1.1";
    int res_idx;

    cache_insert(&c, request, (void *)data, sizeof(char) * strlen(data));
    res_idx = _find(&c, request);

    ASSERT(strcmp(c.obj_list[res_idx].requestline, request) == 0); 
    ASSERT(c.obj_list[res_idx].filesize == strlen(data) * sizeof(char));
    ASSERT(strcmp(c.obj_list[res_idx].contentp, data) == 0); 

    cache_insert(&c, "req1", (void *)"data1", sizeof(char) * strlen("data1"));
    cache_insert(&c, "req2", (void *)"data2", sizeof(char) * strlen("data2"));
    /* The cache is full, the first should get evicted and replaced */
    cache_insert(&c, "req3", (void *)"data3", sizeof(char) * strlen("data3"));
    res_idx = _find(&c, "req3");

    ASSERT(strcmp(c.obj_list[res_idx].requestline, "req3") == 0); 
    ASSERT(c.obj_list[res_idx].filesize == strlen("data3") * sizeof(char));
    ASSERT(strcmp(c.obj_list[res_idx].contentp, "data3") == 0); 

    res_idx = _find(&c, request);
    ASSERT(res_idx == -1);
    cache_clear(&c);
}

/* Finds a match in the cache based on the requestline.
 * res is given a pointer to the cached data(res_filesize)
 * return object index when match is found, otherwise return -1
 */
int cache_find(cache_t *cache, char *requestline, void **res, size_t *res_filesize)
{
    int obj_idx;

    /* Read Locking */
    P(&(cache->rc_mutex));
    cache->read_cnt++;
    if (cache->read_cnt == 1)
    {
        P(&(cache->w));
    }
    V(&(cache->rc_mutex));
    /* Read Locking */

    obj_idx = _find(cache, requestline);

    if (obj_idx >= 0) {
        *res = cache->obj_list[obj_idx].contentp;
        *res_filesize = cache->obj_list[obj_idx].filesize;
    }

    /* Read unlock */
    P(&(cache->rc_mutex));
    cache->read_cnt--;
    if (cache->read_cnt == 0) /* Last reader */
        V(&(cache->w));
    V(&(cache->rc_mutex));
    /* Read unlock */

    return obj_idx;
}

/* Find cache object in cache using the request line.
 * If found return index
 * Else return -1 */
int _find(cache_t *cache, char *requestline)
{
    size_t i;

    for(i = 0; i < cache->n; i++){
        if (cache->obj_list[i].filesize !=  0) {
            if (strcmp(cache->obj_list[i].requestline, requestline) == 0){
                return i;
            }
        }
    }
    return -1;
}


void test_find_helper()
{
    cache_t c;
    cache_init(&c, 3);
    char data[] = "Hin\n";
    char request[] = "GET http://www.google.com HTTP/1.1";
    int res_idx;

    cache_insert(&c, request, (void *)data, sizeof(char) * strlen(data));
    res_idx = _find(&c, request);

    ASSERT(res_idx == 0);

    res_idx = _find(&c, "not in cache");
    ASSERT(res_idx == -1);
    cache_clear(&c);

}

void _lru_insert(cache_t *cache, int added_idx)
{
    lru_t *new_node;

    new_node = Malloc(sizeof(lru_t));
    new_node->obj_idx = added_idx;
    _lru_add_end(new_node, cache->lru_tail);
    cache->assoc_arr[added_idx] = new_node;
}

void test_lru_insert()
{
    cache_t c;
    char *r, *co;
    int obj_idx;

    cache_init(&c, 3);

    /* Fill first object */
    r = Malloc(2 * sizeof(char));
    co = Malloc(4 * sizeof(char));
    strcpy(r, "hi");
    strcpy(co, "hihi");
    c.obj_list[0].requestline = r;
    c.obj_list[0].filesize = 4;
    c.obj_list[0].contentp = co;

    _lru_insert(&c, 0);
    obj_idx = c.lru_head->next->obj_idx;
    ASSERT(strcmp(c.obj_list[obj_idx].requestline, r) == 0);
    ASSERT(c.obj_list[obj_idx].filesize == 4);
    ASSERT(strcmp(c.obj_list[obj_idx].contentp, co) == 0);

    obj_idx = c.assoc_arr[0]->obj_idx;
    ASSERT(strcmp(c.obj_list[obj_idx].requestline, r) == 0);
    ASSERT(c.obj_list[obj_idx].filesize == 4);
    ASSERT(strcmp(c.obj_list[obj_idx].contentp, co) == 0);
    cache_clear(&c);
}

/* Add a node to the end of the LRU 
 * Does not update the associative array */
void _lru_add_end(lru_t *new_node, lru_t *tail)
{
    lru_t *second_to_last;

    /* Add to doubly linked list */
    second_to_last = tail->prev;

    second_to_last->next = new_node;
    new_node->prev = second_to_last;
    new_node->next = tail;
    tail->prev = new_node;
}

void test_lru_add_end()
{
    lru_t a, b, c, d;

    a.obj_idx = 1;
    b.obj_idx = 2;
    c.obj_idx = 3;
    d.obj_idx = 4;

    /* a<->b<->c */
    a.next = &b;
    b.prev = &a;
    b.next = &c;
    c.prev = &b;

    
    _lru_add_end(&d,&c);

    ASSERT(b.next == &d);
    ASSERT(c.prev == &d);
    
}


/* Determine the least recently used object index */
int _lru_peek(cache_t *cache)
{
    int res;
    res = cache->lru_head->next->obj_idx;
    return res;
}

/* Remove node from the doubly linked list */
void _lru_take_out(lru_t *node)
{
    lru_t *first, *second;

    first = node->prev;
    second = node->next;

    first->next = second;
    second->prev = first;

}

/* Remove given node from the lru list */
void _lru_remove(lru_t **assoc_arr, int obj_idx)
{
    _lru_take_out(assoc_arr[obj_idx]);
    Free(assoc_arr[obj_idx]);
    assoc_arr[obj_idx] = NULL;
}

/* Update given object to be the most recently used */
void _lru_use(cache_t *cache, int obj_idx)
{
    lru_t *node;
    node = cache->assoc_arr[obj_idx];
    _lru_take_out(node);
    _lru_add_end(node, cache->lru_tail);
}

void test_lru()
{
    cache_t c;
    cache_init(&c, 3);
    char data[] = "Hin\n";
    char request[] = "GET http://www.google.com HTTP/1.1";
    char request1[] = "1";
    char request2[] = "2";
    char request3[] = "3";

    cache_insert(&c, request, (void *)data, sizeof(char) * strlen(data));
    cache_insert(&c, request1, (void *)data, sizeof(char) * strlen(data));
    cache_insert(&c, request2, (void *)data, sizeof(char) * strlen(data));

    _evict(&c);
    cache_insert(&c, request3, (void *)data, sizeof(char) * strlen(data));

    /* index 1 should be the least recently used */
    ASSERT(_lru_peek(&c) == 1);
    _lru_use(&c,1);
    ASSERT(_lru_peek(&c) == 2);
    cache_clear(&c);
}



/* Evict the lru object 
 * - Clear the object
 * - Remove it from the lru list and associative array
 * - Decrements slots variable
 * - Returns index of evicted object  */
int _evict(cache_t *cache)
{
    int evict_idx;

    evict_idx = _lru_peek(cache);
    _lru_remove(cache->assoc_arr, evict_idx);
    _clear_obj(cache, evict_idx);
    cache->slots += 1;
    return evict_idx;
}


void test_evict_helper()
{
    cache_t c;
    cache_init(&c, 2);
    char data[] = "Hin\n";
    char request[] = "GET http://www.google.com HTTP/1.1";
    char request2[] = "GET http://www.google.com HTTP/1.1";
    int res_idx;

    cache_insert(&c, request, (void *)data, sizeof(char) * strlen(data));
    cache_insert(&c, request2, (void *)data, sizeof(char) * strlen(data));
    res_idx = _evict(&c);
    ASSERT(res_idx == 0);
    ASSERT(c.obj_list[res_idx].filesize == 0);

    cache_insert(&c, request, (void *)data, sizeof(char) * strlen(data));
    res_idx = _evict(&c);
    ASSERT(res_idx == 1);
    cache_clear(&c);

}

/* Finds an empty slot
 * Returns the index
 * -1 if not found */
int _find_empty(cache_t *cache)
{
    ASSERT(cache->slots > 0);
    size_t i;

    for(i = 0; i < cache->n; i++){
        if (cache->obj_list[i].filesize == 0){
            return i;
        }
    }
    return -1;
}

void test_find_empty_helper()
{
    cache_t c;
    cache_init(&c, 2);
    char data[] = "Hin\n";
    char request[] = "GET http://www.google.com HTTP/1.1";
    int res_idx;

    cache_insert(&c, request, (void *)data, sizeof(char) * strlen(data));
    res_idx = _find_empty(&c);
    ASSERT(c.obj_list[res_idx].filesize == 0);
    ASSERT(res_idx == 1);
    cache_clear(&c);
}

void visualize_lru(cache_t *cache)
{
    lru_t *ptr;
    ptr = cache->lru_head;
    printf("\n");
    while (ptr != NULL)
    {
        printf("%p | %d | %p\n", ptr->prev, ptr->obj_idx, ptr->next);
        ptr = ptr->next;
    }
    printf("\n");
}

void test_cache()
{
    test_cache_insert();
    test_find_helper();
    test_find_empty_helper();
    test_evict_helper();
    test_lru_add_end();
}
