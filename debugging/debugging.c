#include "debugging.h"
#include <stdio.h>

logger_t logger = {0};
typedef struct {
    int priority : 3;
    char* key;
    char* desc;
} entries_t;

FORCE_INLINE void add(int line, const char* file, const char* desc) {

}
FORCE_INLINE void clean(char* key) {

}
FORCE_INLINE void write() {

} 

FORCE_INLINE struct entries_t* find(const char* key) {

    return NULL;
}