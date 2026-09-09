#include "csapp.h"
#include <assert.h>

/* Define debug asserts */
#ifdef MYDEBUG
#define ASSERT(COND) assert(COND)
#else
#define ASSERT(COND) ((void) 0)
#endif

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define CACHE_LIST_SIZE (MAX_CACHE_SIZE / MAX_OBJECT_SIZE)

typedef struct {
    char *requestline;
    size_t filesize;
    void *contentp;
} cacheinfo_t;

typedef struct lru_t {
    struct lru_t *prev;
    int obj_idx;
    struct lru_t *next;
} lru_t;

typedef struct {
    cacheinfo_t *obj_list; /* List of cache objects */
    int n; /* Size of cache */
    int slots; /* Number of available slots in cache */
    lru_t *lru_head; /* Head node of lru doubly linked list*/
    lru_t *lru_tail; /* Tail node of lru doubly linked list*/
    lru_t **assoc_arr; /* Associative array of the lru */
    int read_cnt; /* Number of readers currently active */
    sem_t rc_mutex; /* */
    sem_t w;
    
} cache_t;

/* Function Prototypes */
void cache_init(cache_t *cache, int n);
void cache_clear(cache_t *cache);
void cache_insert(cache_t *cache, char *requestline, void *ptr, size_t filesize);
int cache_find(cache_t *cache, char *requestline, void **res, size_t *res_filesize);

/* Test Prototypes */
void test_cache();
