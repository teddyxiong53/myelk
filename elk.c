


#include "elk.h"

#include "mylog.h"

//4 bytes align
static jsoff_t  align32(jsoff_t v) 
{
    return ((v+3)>>2)<<2;
}


static jsval_t mkval(uint8_t type, unsigned long data)
{
    return (
        ((jsval_t)0x7ff0 << 48) |
        ((jsval_t)(type) << 48) |
        data
    );
}
// return T_OBJ/T_PROP/T_STR entity size 
static inline jsoff_t esize(jsoff_t w) {
    switch (w&3)
    {
    case T_OBJ:
        return sizeof(jsoff_t) +sizeof(jsoff_t);
    case T_PROP:
        return sizeof(jsoff_t) +sizeof(jsoff_t) + sizeof(jsval_t);
    case T_STR:
        return sizeof(jsoff_t) + align32(w>>2);
    default:
        return (jsoff_t)~0;
    }
}


static jsoff_t js_alloc(struct js *js, size_t size)
{
    jsoff_t ofs = js->brk;
    size = align32((jsoff_t)size);
    if (js->brk + size > js->size) {
        myloge("out of memory");
        return ~(jsoff_t)0;
    }
    js->brk += (jsoff_t)size;
    return ofs;
}

static jsval_t js_err(struct js *js, const char *fmt, ...) 
{
    va_list ap;
    size_t n = snprintf(js->errmsg, sizeof(js->errmsg), "%s", "ERROR: ");
    va_start(ap, fmt);
    vsnprintf(js->errmsg+n, sizeof(js->errmsg)-n, fmt, ap);
    va_end(ap);
    js->errmsg[sizeof(js->errmsg)-1] = '\0';
    js->pos = js->clen;// we are done, jump to the end of code
    js->tok = TOK_EOF;
    return mkval(T_ERR, 0);
}
static jsval_t mkentity(struct js *js, jsoff_t b, const void *buf, size_t len)
{
    jsoff_t ofs = js_alloc(js, sizeof(b) + len);
    if (ofs == (jsoff_t)~0) {
        return js_err(js, "oom");
    }
    memcpy(&js->mem[ofs], &b, sizeof(b));
    if (buf != NULL) {
        memmove(&js->mem[ofs + sizeof(b)], buf, len);
    }
    if ((b&3) == T_STR) {
        js->mem[ofs + sizeof(b) + len -1] = 0;// 0-terminate
    }
    return mkval(b&3, ofs);
}
static jsval_t mkobj(struct js* js, jsoff_t parent)
{
    return mkentity(js, 0 | T_OBJ, &parent, sizeof(parent));
}
struct js * js_create(void *buf, size_t len)
{
    struct js* js = NULL;
    if (len < sizeof(*js)+esize(T_OBJ)) {
        myloge("memory too small");
        return js;
    }
    memset(buf, 0, len);
    js = (struct js *)buf;
    js->mem = (uint8_t *)(js+1);
    js->size = (jsoff_t)(len-sizeof(*js));
    js->scope = mkobj(js, 0);
    return js;
}