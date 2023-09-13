#ifndef _elk_h_
#define _elk_h_

#define JS_VERSION "3.0.0"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct js;
typedef uint64_t jsval_t;

struct js *js_create(void *buf, size_t len);
jsval_t js_mkerr(struct js *js, const char *xx, ...);

#endif
