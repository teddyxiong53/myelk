#ifndef _ELK_H_
#define _ELK_H_
#include <stdint.h>
#include <assert.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

#define JS_VERSIN "1.0.0"

typedef uint64_t jsval_t;
typedef uint32_t jsoff_t ;

struct js {
    const char *code;//currently parsed code snippet
    char errmsg[36];//err messsage placeholder
    uint8_t tok;//last parsed token value
    uint8_t flags;//excution flags
    uint16_t lev; //recursion level
    jsoff_t clen;//code snippet len
    jsoff_t pos; // current parsing position
    jsoff_t toff;//last parsed token offset
    jsoff_t tlen; //length of last parsed token
    jsval_t tval;//hold last parsed value
    jsval_t scope; //current scope
    uint8_t *mem;//available js memory
    jsoff_t size; //mem size
    jsoff_t brk;// current mem usage boundary
    jsoff_t ncbs;//number of FFI-end C "userdata" callback pointers
};

#define F_NOEXEC 1 //parse code ,but not execute
#define F_LOOP 2 //we are inside of a loop
#define F_CALL 4 // we are insde a function call
#define F_BREAK 8 //exit the loop
#define F_RETURN 16 //return has beed executed

/**
 * @brief 
 * T_OBJ, T_PROP, T_STR must go first.
 * That is required by the memory layout functions:
 * memory entity types are encoded in the 2 bits.
 * so the type value must be 0,1,2,3
 * 
 */
enum {
    T_OBJ,
    T_PROP,
    T_STR,
    T_UNDEF,
    T_NULL,
    T_NUM,
    T_BOOL,
    T_FUNC,
    T_CODEREF,
    T_ERR
};

enum {
    TOK_ERR,
    TOK_EOF

};

struct js *js_create(void *buf, size_t len);

#endif
