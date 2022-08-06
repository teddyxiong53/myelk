


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
static bool is_nan(jsval_t v)
{
    return (v>>52) == 0x7ff;
}
static uint8_t vtype(jsval_t v)
{
    if (is_nan(v)) {
        return (v>>48)&15;
    } else {
        return (uint8_t)T_NUM;
    }
}
static bool is_err(jsval_t v)
{
    return vtype(v) == T_ERR;
}
static bool is_space(int c) 
{
    return c==' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == '\v';
}
//skip space and comment
static jsoff_t skiptonext(const char *code, jsoff_t len, jsoff_t n)
{
    while (n < len) {
        if (is_space(code[n])) {
            n++;
        } else if(// parse comment //
            ((n+1) < len ) &&
            (code[n] == '/') &&
            (code[n+1] == '/')
        ) {
            for (n+=2; n < len && code[n]!='\n';) {
                n++;
            }
        } else if (
            (n+3 < len ) &&
            (code[n] == '/') &&
            (code[n+1] == '*')
        ) {
            for (n+=4; n < len && (code[n-2] != '*' || code[n-1] != '/');) {
                n++;
            }
        } else {
            break;
        }
    }
    return n;
}
static bool js_should_garbage_collect(struct js *js)
{
    return js->brk > js->size /2;
}

void js_gc(struct js *js)
{
    printf("js_gc to be implemented\n");
}
static jsval_t tov(double d)
{
    union {
        double d;
        jsval_t v;
    } u = {d};
    return u.v;
}
static double tod(jsval_t v)
{
    union {
        jsval_t v;
        double d;
    } u = {v};
    return u.d;
}
static bool is_alpha(int c)
{
    return (c>='a' && c<='z') || (c>='A' && c<='Z');
}
static bool is_digit(int c)
{
    return (c>='0' && c<='9') ;
}
static bool is_ident_begin(int c)
{
    return c == '_' || c == '$' || is_alpha(c);
}
static bool is_ident_continue(int c)
{
    return c == '_' || c == '$' || is_alpha(c) || is_digit(c);
}
static bool streq(const char *buf, size_t len, const char *p, size_t n)
{
    return n == len && memcmp(buf, p, len) == 0;
}
static uint8_t parsekeyword(const char *buf, size_t len)
{
    switch(buf[0]) {
        case 'b':
            if (streq("break", 5, buf, len)) {
                return TOK_BREAK;
            }
            break;
        case 'c':
            if (streq("class", 5 ,buf, len)) {
                return TOK_BREAK;
            }
            if (streq("case", 4, buf, len)) {
                return TOK_CASE;
            }
            if (streq("catch", 5, buf, len)) {
                return TOK_CATCH;
            }
            if (streq("const", 5, buf, len)) {
                return TOK_CONST;
            }
            if (streq("continue", 8, buf, len)) {
                return TOK_CONTINUE;
            }
            break;
        case 'd':
            if (streq("do", 2, buf, len)) {
                return TOK_DO;
            }
            if (streq("default", 7, buf, len)) {
                return TOK_DEFAULT;
            }
            break;
        case 'e':
            if (streq("else", 4, buf ,len)) {
                return TOK_ELSE;
            }
            break;
        case 'f':
            if (streq("for", 3, buf, len)) {
                return TOK_FOR;
            }
            if (streq("function", 8, buf, len)) {
                return TOK_FUNC;
            }
            if (streq("finally", 7, buf, len)) {
                return TOK_FINALLY;
            }
            if (streq("false", 5, buf, len)) {
                return TOK_FALSE;
            }
            break;
        case 'i':
            if (streq("if", 2, buf, len)) {
                return TOK_IF;
            }
            if (streq("in", 2, buf, len)) {
                return TOK_IN;
            }
            if (streq("instanceof", 10, buf, len)) {
                return TOK_INSTANCEOF;
            }
            break;
        caes 'l':
            if (streq("let", 3, buf, len)) {
                return TOK_LET;
            }
            break;
        case 'n':
            if (streq("new", 3, buf, len)) {
                return TOK_NEW;
            }
            if (streq("null", 4, buf, len)) {
                return TOK_NULL;
            }
            break;
        case 'r':
            if (streq("return", 6, buf, len)) {
                return TOK_RETURN;
            }
            break;
        case 's':
            if (streq("switch", 6, buf, len)) {
                return TOK_SWITCH;
            }
            break;
        case 't':
            if (streq("try", 3, buf, len)) {
                return TOK_TRY;
            }
            if (streq("this", 4, buf, len)) {
                return TOK_THIS;
            }
            if (streq("throw", 5, buf, len)) {
                return TOK_THROW;
            }
            if (streq("true", 4, buf, len)) {
                return TOK_TRUE;
            }
            if (streq("typeof", 6, buf, len)) {
                return TOK_TYPEOF;
            }
            break;
        case 'u':
            if (streq("undefined", 9, buf, len)) {
                return TOK_UNDEF;
            }
            break;
        case 'v':
            if (streq("var", 3, buf, len)) {
                return TOK_VAR;
            }
            if (streq("void", 4, buf, len)) {
                return TOK_VOID;
            }
            break;
        case 'w':
            if (streq("while", 5, buf, len)) {
                return TOK_WHILE;
            }
            if (streq("with", 4, buf, len)) {
                return TOK_WITH;
            }
            break;
        case 'y':
            if (streq("yield", 5, buf, len)) {
                return TOK_YIELD;
            }
            break;

    }
    return TOK_IDENTIFIER;
}
static uint8_t parseident(const char *buf, jsoff_t len, jsoff_t *tlen)
{
    if (is_ident_begin(buf[0])) {
        while (*tlen < len && is_ident_continue(buf[*tlen])) {
            (*tlen)++;
        }
        return parsekeyword(buf, *tlen);
    }
    return TOK_ERR;
}
static uint8_t nexttok(struct js *js)
{
    js->tok = TOK_ERR;
    js->toff = js->pos = skiptonext(js->code, js->clen, js->pos);
    js->tlen = 0;
    const char *buf = js->code + js->toff;
    if (js->toff >= js->clen) {
        js->tok = TOK_EOF;
        return js->tok;
    }
#define TOK(T, LEN) {js->tok = T; js->tlen = (LEN); break;}
#define LOOK(OFS, CH) js->toff + OFS < js->clen && buf[OFS] == CH

    switch (buf[0]) {
        case '?':
            TOK(TOK_Q, 1);
        case ':':
            TOK(TOK_COLON, 1);
        case '(':
            TOK(TOK_LPAREN, 1);
        case ')':
            TOK(TOK_RPAREN, 1);
        case '{':
            TOK(TOK_LBRACE, 1);
        case '}':
            TOK(TOK_RBRACE, 1);
        case ';':
            TOK(TOK_SEMICOLON, 1);
        case ',':
            TOK(TOK_COMMA, 1);
        case '!':
            if (LOOK(1, '=') && LOOK(2,'=')) {
                TOK(TOK_NE, 3);
                
            } else {
                TOK(TOK_NOT, 1);
            }
        case '.':
            TOK(TOK_DOT, 1);
        case '~':
            TOK(TOK_NEG, 1);
        case '-':
            if (LOOK(1,'-')) {//--
                TOK(TOK_POSTDEC, 2);
            } else if (LOOK(1, '=')){//-=
                TOK(TOK_MINUS_ASSIGN, 2);
            } else {
                //-
                TOK(TOK_MINUS, 1);
            }
        case '+':
            if (LOOK(1, '+')) {
                //++
                TOK(TOK_POSTINC, 2);
            } else if(LOOK(1, '=')) {
                //+=
                TOK(TOK_PLUS_ASSIGN, 2);
            } else {
                TOK(TOK_PLUS, 1);
            }
        case '*':
            if (LOOK(1, '*')) {
                //** 
                TOK(TOK_EXP, 2);
            } else if(LOOK(1, '=')) {
                TOK(TOK_MUL_ASSIGN, 2);
            } else {
                TOK(TOK_MUL, 1);
            }
        case '/':
            if (LOOK(1, '=')) {
                // /=
                TOK(TOK_DIV_ASSIGN, 2);
            } else {
                TOK(TOK_DIV ,1);
            }
        case '%':
            if (LOOK(1, '=')) {
                TOK(TOK_REM_ASSIGN,2);
            } else {
                TOK(TOK_REM, 1);
            }
        case '&':
            if (LOOK(1, '&')) {
                TOK(TOK_LAND, 2);
            } else if( LOOK(1, '=')) {
                TOK(TOK_AND_ASSIGN, 2);
            } else {
                TOK(TOK_AND, 1);
            }
        case '|':
            if (LOOK(1, '|')) {
                TOK(TOK_LOR, 2);
            } else if(LOOK(1, '=')) {
                TOK(TOK_OR_ASSIGN, 2);
            } else {
                TOK(TOK_OR, 1);
            }
        case '=': 
            if (LOOK(1, '=') && LOOK(2, '=')) {
                TOK(TOK_EQ, 3);
            } else {
                TOK(TOK_ASSIGN, 1);
            }
        case '<':
            if (LOOK(1, '<') && LOOK(2, '=')) {
                // <<=
                TOK(TOK_SHL_ASSIGN, 3);
            } else if (LOOK(1, '<')) {
                // <<
                TOK(TOK_SHL, 2);
            } else if (LOOK(1, '=')) {
                // <=
                TOK(TOK_LE, 2);
            } else {
                // <
                TOK(TOK_LT, 1);
            }
        case '>':
            if (LOOK(1, '>') && LOOK(2, '=')) {
                // >>=
                TOK(TOK_SHR_ASSIGN, 3);
            } else if( LOOK(1, '>')) {
                // >>
                TOK(TOK_SHR, 2);
            } else if (LOOK(1, '=')) {
                // >=
                TOK(TOK_GE, 2);
            } else {
                // >
                TOK(TOK_GT, 1);
            }
        case '^':
            if (LOOK(1, '=')) {
                TOK(TOK_XOR_ASSIGN, 2);
            } else {
                TOK(TOK_XOR, 1);
            }
        case '"':
        case '\'':
            js->tlen++;
            while (js->toff + js->tlen < js->clen && buf[js->tlen] != buf[0]) {
                uint8_t increment = 1;
                if (buf[js->tlen] == '\\') {
                    if (js->toff + js->tlen + 2 > js->clen) {
                        break;
                    }
                    increment = 2;
                    if (buf[js->tlen + 1] == 'x') {
                        if (js->toff + js->tlen + 4 > js->clen ) {
                            break;
                        }
                        increment = 4;
                    }
                }
                js->tlen += increment;
            }
            if (buf[0] == buf[js->tlen]) {
                js->token = TOK_STRING;
                js->tlen ++;
            }
            break;
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9': {
            char *end;
            js->tval = tov(strtod(buf, &end));
            TOK(TOK_NUMBER, (jsoff_t)(end-buf));
        }
        default:
            js->token = parseindent(buf, js->clen - js->toff, &js->tlen);
            break;
    }
    js->pos = js->toff + js->tlen;
    return js->tok;
}
static jsval_t js_continue(struct js *js)
{
    if (!(js->flags & F_LOOP)) {
        return js_err(js, "not in loop");
    }
    js->flags |= F_NOEXEC;
    return mkval(T_UNDEF, 0);
}
static jsval_t js_break(struct js *js)
{
    if (!(js->flags & F_LOOP)) {
        return js_err(js, "not in loop");
    }
    if (!(js->flags & F_NOEXEC)) {
        js->flags |= F_BREAK | F_NOEXEC;
    }
    return mkval(T_UNDEF, 0);
}
static bool is_op(uint8_t tok)
{
    return tok >= TOK_DOT;
}

static bool is_unary(uint8_t tok)
{
    return tok >= TOK_POSTINC && tok <= TOK_UMINUS;
}
static bool is_right_assoc(uint8_t tok)
{
    return (tok >= TOK_NOT && tok <= TOK_UMINUS) ||
        (tok >= TOK_Q && tok <= TOK_OR_ASSIGN);
}
//xhl:??
static jsval_t mkcoderef(jsval_t off, jsoff_t len)
{
    return mkval(T_CODEREF, 
        (off & 0xffffff) | ((len&0xffffff) << 24)
    );
}
static unsigned long vdata(jsval_t v)
{
    return (unsigned long)(v & ~((jsval_t)0x7fff << 48));
}
//js_call_params和js_expr递归调用了。
static jsval_t js_call_params(struct js *js)
{
    jsoff_t pos = js->pos;
    if (nexttok(js) == TOK_LPAREN) {
        return mkcoderef(pos, js->pos - pos - js->tlen);
    }
    js->pos -= js->tlen;
    uint8_t flags = js->flags;
    js->flags |= F_NOEXEC;
    do {
        jsval_t res = js_expr(js, TOK_COMMA, TOK_RPAREN);
        if (is_err(res)) {
            return res;
        }
        if (vdata(res) == 0) {
            js->tok = TOK_ERR;
        }

    } while (js->tok == TOK_COMMA);
    js->flags = flags;
    if (js->tok != TOK_RPAREN) {
        return js_err(js, "parse error");
    }
    return mkcoderef(pos, js->pos - pos - js->tlen);
}
static jsoff_t loadoff(struct js *js, jsoff_t off)
{
    jsoff_t v = 0;
    memcpy(&v, &js->mem[off], sizeof(v));
    return v;
}
// search for property in a single object
static jsoff_t lkp(struct js *js, jsval_t obj, const char *buf, size_t len)
{
    jsoff_t off = loadoff(js, vdata(obj)) &~3;// load first property
    while (off < js->brk && off != 0) {
        jsoff_t koff = loadoff(js, off + sizeof(off));
        jsoff_t klen = (loadoff(js, koff) >> 2) -1;
        const char *p = (char *)&js->mem[koff + sizeof(koff)];
        if (streq(buf, len , p, klen)) {
            return off;//found
        }
        off = loadoff(js, off) & ~3;//load next property offset
    }
    return 0;//not found
}
// lookup variable in the scope chain
static jsval_t lookup(struct js *js, const char *buf, size_t len)
{
    jsval_t scope = js->scope;
    for(;;) {
        jsoff_t off = lkp(js, scope, buf, len);
        if (off != 0) {
            return mkval(T_PROP, off);
        }
        if(vdata(scope) == 0) {
            break;
        }
        scope = mkval(T_OBJ, loadoff(js, vdata(scope) + sizeof(jsoff_t)));
    }
    return js_err(js, "'%.*s' not found", (int)len, buf);
}
jsval_t js_mkstr(struct js *js, const char *ptr, size_t len)
{
    return mkentity(js, (jsoff_t)(((len+1)<<2) | T_STR), ptr, len+1);
}

static jsval_t loadval(struct js *js, jsoff_t off)
{
    jsval_t v = 0;
    memcpy(&v, &js->mem[off], sizeof(v));
    return v;
}
static jsval_t setprop(struct js *js, jsval_t obj, jsval_t k, jsval_t v)
{
    jsoff_t koff = vdata(k);
    jsoff_t b;
    jsoff_t head = vdata(obj);
    char buf[sizeof(koff) + sizeof(v)];
    memcpy(&b, &js->mem[head], sizeof(b));
    memcpy(buf, &koff, sizeof(koff));
    memcpy(buf + sizeof(koff), &v, sizeof(v));
    jsoff_t brk = js->brk | T_OBJ;
    memcpy(&js->mem[head], &brk, sizeof(brk));
    return mkentity(js, (b&~3) | T_PROP, buf, sizeof(buf));
}
static jsval_t resolveprop(struct js *js, jsval_t v)
{
    if (vtype(v) != T_PROP) {
        return v;
    }
    v = loadval(js, vdata(v) + sizeof(jsoff_t) + sizeof(jsoff_t));
    return resolveprop(js, v);
}
static jsval_t js_obj_literal(struct js *js)
{
    uint8_t exe = !(js->flags & F_NOEXEC);
    jsval_t obj = 0;
    if (exe) {
        obj = mkobj(js, 0);
    }  else {
        obj = mkval(T_UNDEF, 0);
    }
    if (is_err(obj)) {
        return obj;
    }
    while (nexttok(js) != TOK_RBRACE) {
        if (js->tok != TOK_IDENTIFIER) {
            return js_err(js, "parse error");
        }
        size_t koff = js->toff;
        size_t klen = js->tlen;
        if (nexttok(js) != TOK_COLON) {
            return js_err(js, "parse error");
        }
        jsval_t val = js_expr(js, TOK_RBRACE, TOK_COMMA);
        if (exe) {
            if (is_err(val)) {
                return val;
            }
            jsval_t key = js_mkstr(js, js->code + koff, klen);
            if (is_err(key)) {
                return key;
            }
            jsval_t res = setprop(js, obj, key, resolveprop(js, val));
            if (is_err(res)) {
                return res;
            }
        }
        if (js->tok == TOK_RBRACE) {
            break;
        }
        if (js->tok != TOK_COMMA) {
            return js_err(js, "parse error");
        }
    }
    return obj;
}

static jsval_t js_str_literal(struct js *js)
{
    uint8_t *in = (uint8_t *)(&js->code[js->toff]);
    uint8_t *out = &js->mem[js->brk + sizeof(jsoff_t)];
    int n1 = 0, n2 = 0;
    if (js->brk + sizeof(jsoff_t) + js->tlen > js->size) {
        return js_err(js, "oom");
    }
    while (n2++ + 2 < (int)js->tlen) {
        if (in[n2] == '\\') {
            //有续行符的情况？先不处理。
        } else {//if (in[n2] == '\\')
            //先只考虑这种简单情况
            out[n1++] = js->code[js->toff + n2];
        }
    }
    return js_mkstr(js, NULL, n1);
}

static void mkscope(struct js *js)
{
    jsoff_t prev = vdata(js->scope);
    js->scope = mkobj(js, prev);
}
static jsval_t upper(struct js *js, jsval_t scope)
{
    return mkval(T_OBJ, loadoff(js, vdata(scope) + sizeof(jsoff_t)));
}
static void delscope(struct js *js)
{
    js->scope = upper(js, js->scope);
}
static jsval_t js_block(struct js *js, bool create_scope)
{
    jsval_t res = mkval(T_UNDEF, 0);
    jsoff_t brk1 = js->brk;
    if (create_scope) {
        mkscope(js);//enter new scope
    }
    jsoff_t brk2 = js->brk;
    while (js->tok != TOK_EOF && js->tok != TOK_RBRACE) {
        js->pos = skiptonext(js->code, js->clen, js->pos);
        if (js->pos < js->clen && js->code[js->pos] == '}') {
            break;
        }
        res = js_stmt(js, TOK_RBRACE);//这个又是一个递归
    }
    if (js->pos < js->clen && js->code[js->pos] == '}') {
        js->pos ++;
    }
    if (create_scope) {
        delscope(js);//exit scope
    }
    if (js->brk == brk2) {
        js->brk = brk1;//fast scope gc
    }
    return res;

}
static jsval_t js_func_literal(struct js *js)
{
    jsoff_t pos = js->pos;
    uint8_t flags = js->flags;//save current flags
    if (nexttok(js) != TOK_LPAREN) {
        return js_err(js, "parse error");
    }
    bool expect_ident = false;
    for(; nexttok(js) != TOK_EOF; expect_ident = true) {
        if (expect_ident && js->tok != TOK_IDENTIFIER) {
            return js_err(js, "parse error");
        }
        if (js->tok == TOK_RPAREN) {
            break;
        }
        if (js->tok != TOK_IDENTIFIER) {
            return js_err(js, "parse error");
        }
        if (nexttok(js) == TOK_RPAREN) {
            break;
        }
        if (js->tok != TOK_COMMA) {
            returns js_err(js, "parse error");
        }
    }
    if (js->tok != TOK_RPAREN) {
        return js_err(js, "parse error");
    }
    if (nexttok(js) != TOK_LBRACE) {
        return js_err(js, "pasre error");
    }
    js->flags |= F_NOEXEC;
    jsval_t res = js_block(js, false);//skip function block, not execute
    if (is_err(res)) {
        return res;
    }
    jsval_t str = js_mkstr(js, &js->code[pos], js->pos - pos);
    return mkval(T_FUNC, vdata(str));

}

static void sortops(uint8_t *ops, int nops, jsval_t *stk)
{
    uint8_t prios[] = {19, 19, 17, 17, 16, 16, 16, 16, 16, 15, 14, 14, 14, 13, 13,
                     12, 12, 12, 11, 11, 11, 11, 10, 10, 9,  8,  7,  6,  5,  4,
                     4,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  3,  1};
    bool done = false;
    for(; done == false;) {
        done = true;
        for (int i=0; i + 1 < nops; i++) {
            uint8_t o1 = vdata(stk[ops[i]]) & 255;
            uint8_t o2 = vdata(stk[ops[i+1]]) & 255;
            uint8_t a = prios[o1 - TOK_DOT];
            uint8_t b = prios[o2 - TOK_DOT];
            uint8_t tmp = ops[i];
            bool swap = a < b;
            if (o1 == o2 && is_right_assoc(o1) && ops[i] < ops[i+1]) {
                swap = 1;
            }
            if (swap) {
                ops[i] = ops[i+1];
                ops[i+1] = tmp;
                done = false;
            }
        }
    }
}

static uint8_t getri(uint32_t mask, uint8_t ri)
{
    while( ri > 0 && (mask & (1<<ri))) {
        ri --;
    }
    if (!(mask & (1<<ri))) {
        ri ++;
    }
    return ri;
}
static bool is_assign(uint8_t tok)
{
    return tok >= TOK_ASSIGN && tok <= TOK_OR_ASSIGN;
}
static jsoff_t offtolen(jsoff_t off)
{
    return (off >> 2) - 1;
}
static jsoff_t vstrlen(struct js *js, jsval_t v)
{
    return offtolen(loadoff(js, vdata(v)));
}
static bool js_truthy(struct js *js, jsval_t v)
{
    uint8_t t = vtype(v);
    if (t == T_BOOL && vdata(v) != 0) {
        return true;
    } else if(t == T_NUM && tod(v) != 0.0) {
        return true;
    } else if (t == T_OBJ || t == T_FUNC) {
        return true;
    } else if (t == T_STR && vstrlen(js, v) > 0) {
        return true;
    } else {
        return false;
    }
}

static jsval_t do_logic_or(struct js *js, jsval_t l, jsval_t r)
{
    if (js_truthy(js, l)) {
        return mkval(T_BOOL, 1);
    }
    return mkval(T_BOOL, js_truthy(js ,r) ? 1: 0);
}

static const char *typestr(uint8_t t)
{
    const char *names[] = {
        "object",
        "prop",
        "string",
        "undefined",
        "null",
        "number",
        "boolean",
        "function",
        "nan"
    };
    if (t < sizeof(names)/sizeof(names[0])) {
        return names[t];
    } else {
        return "??";
    }
}
static jsoff_t vstr(struct js *js, jsval_t value, jsoff_t *len)
{
    jsoff_t off = vdata(value);
    if (len) {
        *len = offtolen(loadoff(js, off));
    }
    return off + sizeof(off);
}

static jsoff_t coderefoff(jsval_t v)
{
    return v & 0xffffff;
}
static jsoff_t codreflen(jsval_t v)
{
    return (v>>24) && 0xffffff;
}
static uint8_t unhex(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'W';
    } else if (c >= 'A' && c <= 'F') {
        return c - '7';
    } else {
        return 0;
    }
}
static uint64_t unhexn(const uint8_t *s, int len)
{
    uint64_t v = 0;
    for(int i=0; i<len; i++) {
        if (i> 0) {
            v << 4;
        }
        v |= unhex(s[i]);
    }
    return v;
}

static jsval_t call_c (struct js *js, const char *fn, int fnlen, jsoff_t fnoff)
{
    union ffi_val args[MAX_FFI_ARGS];
    union ffi_val res;
    jsoff_t cbp = 0;
    int n = 0;
    int i;
    int type = fn[0]=='d'? 1: 0;
    for (i=1; i<fnlen && fn[i] != '@' && n < MAX_FF_ARGS; i++) {
        js->pos = skiptonext(js->code, js->clen, js->pos);
        if (js->pos >= js->clen) {
            return js_err(js, "bad arg %d", n+1);
        }
        jsval_t v = resolveprop(js, js_expr(js, TOK_COMMA, TOK_RPAREN));
        if (fn[i] == 'd' || 
            (fn[i] == 'j' && sizeof(jsval_t) > sizeof(jw_t))
        ) {
            type |= 1 << (n+1);
        }
        uint8_t t = vtype(v);
        switch (fn[i]) {
            case 'd':
                if (t != T_NUM) {
                    return js_err(js, "bad arg %d", n+1);
                }
                args[n++].d = tod(v);
                break;
            case 'b':
                if (t != T_BOOL) {
                    return js_err(js, "bad arg %d", n+1);
                }
                args[n++].w = vdata(v);
                break;
            case 'i':
                if (t != T_NUM && t != T_BOOL) {
                    return js_err(js, "bad args %d", n+1);
                }
                if (t == T_BOOL) {
                    args[n++].w = (long)vdata(v);
                } else {
                    args[n++].w = tod(v);
                }
                break;
            case 's':
                if (t != T_STR) {
                    return js_err(js, "bad arg %s", n+1);
                }
                args[n++].p = js->mem + vstr(js, v, NULL);
                break;
            case 'p':
                if (t != NUM) {
                    return js_err(js, "bad arg %s", n+1);
                }
                args[n++].w  = (jw_t)tod(v);
                break;
            case 'j':
                args[n++].u64 = v;
                break;
            case 'm':
                args[n++].p = js;
                break;
            case 'u':
                args[n++].p = &js->mem[cbp];
                break;
            default:
                return js_err(js, "bad sig");
                

        }// end switch
        js->pos = skiptonext(js->code, js->clen, js->pos);
        if (js->pos < js->clen && js->code[js->pos] == ',') {
            js->pos ++;
        }
    }
    uintptr_t f = (uintptr_t)unhexn((uint8_t *)&fn[i+1], fnlen -i -1);
    if (js->pos != js->clen) {
        return js_err(js, "num args");
    }
    if (fn[i] != '@') {
        return js_err(js, "ffi");
    }
    if (f == 0) {
        return js_err(js, "ffi");
    }
#ifndef WIN32
#define __cdecl
#endif
    switch (type) {
        case 0:
            res.u64 = ((uint64_t(__cdecl *)(jw_t, jw_t, jw_t, jw_t, jw_t, jw_t))f)
                (args[0].w, args[1].w, args[2].w, args[4].w, args[5].w);
            break;
        case 1:
            res.d =  ((double(__cdecl *)(jw_t, jw_t, jw_t, jw_t, jw_t, jw_t))f)
                (args[0].w, args[1].w, args[2].w, args[4].w, args[5].w);
            break;
        //TODO 
    }
}
static jsval_t do_call_op(struct js *js, jsval_t func, jsval_t args)
{
    if (vtype(func) != T_FUNC) {
        return js_err(js ,"calling non-function");
    }
    if (vtype(args) != T_CODEREF) {
        return js_err(js, "bad call");
    }
    jsoff_t fnlen;
    jsoff_t fnoff = vstr(js, func, &fnlen);
    const char *fn = (const char *)&js->mem[fnoff];
    const char *code = js->code;
    jsoff_t clen = js->clen;
    jsoff_t pos = js->pos;
    js->code = &js->code[coderefoff(args)];
    js->clen = codereflen(args);
    mylogd("call [%.*s] -> %.*s", (int)js->clen, js->code, (int)fnlen, fn);
    uint8_t tok = js->tok;
    uint8_t flags = js->flags;
    jsval_t res = 0;
    if (fn[0] == '(') {
        res = call_c(js, fn, fnlen, fnoff - sizeof(jsoff_t));
    } else {
        res = call_js(js, fn, fnlen);
    }

}
static jsval_t do_op(struct js *js , uint8_t op, jsval_t lhs, jsval_t rhs)
{
    jsval_t l = resolveprop(js, lhs);
    jsval_t r = resolveprop(js, rhs);
    if (is_assign(op) && vtype(lhs) != T_PROP) {
        return js_err(js, "bad lhs");
    }
    swtich (op) {
        case TOK_LAND:
            unsigned long val;
            if (js_truthy(js, l) && js_truthy(js, r)) {
                val = 1;
            } else {
                val = 0;
            }
            return mkval(T_BOOL, val);
        case TOK_LOR:
            return do_logic_or(js, l, r);
        case TOK_TYPEOF:
            return js_mkstr(js, typestr(vtype(v)), strlen(typestr(vtype(v))));
        case TOK_CALL:
            return do_call_op(js, l, r);


    }
}
static jsval_t js_expr(struct js *js, uint8_t etok, uint8_t etok2)
{
    jsval_t stk[JS_EXPR_MAX];
    uint8_t tok, ops[JS_EXPR_MAX], pt = TOK_ERR;
    uint8_t n = 0, nops = 0, nuops = 0;
    while (
        (tok = nexttok(js) != etok) &&
        (tok != etok2) &&
        (tok != TOK_EOF)
    ) {
        if (tok == TOK_ERR) {
            return js_err(js, "parse error");
        }
        if (n >= JS_EXPR_MAX) {
            return js_err(js, "expr too deep");
        }
        if (tok == TOK_LPAREN && (n>0 && !is_op(pt))) {
            tok = TOK_CALL;
        }
        if (is_op(tok)) {
            //convert this +/- to unary if required
            if (tok == TOK_PLUS || tok == TOK_MINUS) {
                bool convert = (n==0) || (
                    (pt != TOK_CALL) &&
                    (is_op(tok)) &&
                    (
                        !is_unary(pt) ||
                        is_right_assoc(pt)
                    )
                );
                if (convert && tok == TOK_PLUS) {
                    tok = TOK_UPLUS;
                }
                if (convert && tok == TOK_MINUS) {
                    tok = TOK_UMINUS;
                }
            }
            ops[nops++] = n;
            stk[n++] = mkval(T_ERR, tok);
            if (!is_unary(tok)) {
                nuops++;
            }
            // for function call, store arguments, but not evaluate right now
            if (tok == TOK_CALL) {
                stk[n++] = js_call_params(js);
                if (is_err(stk[n-1])) {
                    return stk[n-1];
                }
            }
        } else {// tok is not op
            switch(tok) {
                case TOK_IDENTIFIER:
                    if (js->flags & F_NOEXEC) {
                        stk[n] = 0;
                    } else {
                        if (n > 0 && 
                            is_op(vdata(stk[n-1]) & 255) && 
                            vdata(stk[n-1]) == TOK_DOT
                        ) {
                            stk[n] = mkcoderef((jsoff_t)js->toff, (jsoff_t)js->tlen);
                        } else {
                            stk[n] = lookup(js, js->code + js->toff, js->tlen);
                        }
                    }
                    n++;
                    break;
                case TOK_NUMBER:
                    stk[n++] = js->tval;
                    break;
                case TOK_LBRACE:
                    stk[n++] = js_obj_literal(js);
                    break;
                case TOK_STRING:
                    stk[n++] = js_str_literal(js);
                    break;
                case TOK_FUNC:
                    stk[n++] = js_func_literal(js);
                    break;
                case TOK_NULL:
                    stk[n++] = mkval(T_NULL, 0);
                    break;
                case TOK_UNDEF:
                    stk[n++] = mkval(T_UNDEF, 0);
                    break;
                case TOK_TRUE:
                    stk[n++] = mkval(T_BOOL, 1);
                    break;
                case TOK_FALSE:
                    stk[n++] = mkval(T_BOOL, 0);
                    break;
                case TOK_LPAREN:
                    stk[n++] = js_expr(js, TOK_RPAREN, TOK_EOF);
                    break;
                default:
                    return js_err(js, "unexpected token '%.*s'", (int)js->tlen, js->code + js->toff);


            }
        } //end of else
        if (!is_op(tok) && is_err(stk[n-1])) {
            return stk[n-1];
        }
        pt = tok;
    } //while tok

    if (js->flags & F_NOEXEC) {
        return mkval(T_UNDEF, n);//pass n to caller
    }
    if (n == 0) {
        return mkval(T_UNDEF, 0);
    }
    if (n != nops + nuops + 1) {
        return js_err(js ,"bad expr");
    }
    sortops(ops, nops, stk);// sort operations by priority
    uint32_t mask = 0;
    for (int i=0; i<nops; i++) {
        uint8_t idx = ops[i];
        uint8_t op = vdata(stk[idx]) & 255;
        uint8_t ri = idx;
        bool unary = is_unary(op);
        bool rassoc = is_right_assoc(op);
        bool needleft = unary && rassoc ? false :true;
        bool needright = unary && !rassoc ? false :true;
        jsval_t left = mkval(T_UNDEF ,0);
        jsval_t right = mkval(T_UNDEF, 0);
        mask |= 1<<idx;

        if (needleft) {
            if (idx < 1) {
                return js_err(js, "bad expr");
            }
            mask |= 1 << (idx - 1);
            ri = getri(mask, idx);
            left = stk[ri];
            if (is_err(left)) {
                return js_err(js, "bad expr");
            }
        }
        if (needright) {
            mask |= 1 << (idx+1);
            if (idx +1 >= n) {
                return js_err(js, "bad expr");
            }
            right = stk[idx+1];
            if(is_err(right)) {
                return js_err(js, "bad expr");
            }

        }
        stk[ri] = do_op(js, op, left, right);// perform operation
        if (is_err(stk[ri])) {
            return stk[ri];
        }

    }
    return stk[0];
}
static jsval_t js_let(struct js *js)
{
    uint8_t exe = !(js->flags & F_NOEXEC);
    for(;;) {
        uint8_t tok = nexttok(js);
        if (tok != TOK_IDENTIFIER) {
            return js_err(js, "parse error");
        }
        jsoff_t noff = js->toff;
        jsoff_t nlen = js->tlen;
        jsval_t v = mkval(T_UNDEF, 0);
        nexttok(js);
        if (js->tok == TOK_ASSIGN) {
            v = js_expr(js, TOK_COMMA, TOK_SEMICOLON);
        }
    }
}
static jsval_t js_stmt(struct js* js, uint8_t etok)
{
    jsval_t res;
    // before top level stmt, garbage collect
    if (js->lev == 0 && js_should_garbage_collect(js)) {
        js_gc(js);
    }
    js->lev ++;
    switch (nexttok(js)) {
        case TOK_CASE:
        case TOK_CATCH:
        case TOK_CLASS:
        case TOK_CONST:
        case TOK_DEFAULT:
        case TOK_DELETE:
        case TOK_DO:
        case TOK_FINALLY:
        case TOK_FOR:
        case TOK_IN:
        case TOK_INSTANCEOF:
        case TOK_NEW:
        case TOK_SWITCH:
        case TOK_THIS:
        case TOK_THROW:
        case TOK_TRY:
        case TOK_VAR:
        case TOK_VOID: 
        case TOK_WITH:
        case TOK_YIELD:
            res = js_err(js, "'%.*s' is not implemented", (int)js->tlen, js->code + js->toff);
            break;
        case TOK_CONTINUE:
            res = js_continue(js);
            break;
        case TOK_BREAK:
            res = js_break(js);
            break;
        case TOK_LET:
            res = js_let(js);
            break;
    }
}
static jsval_t js_eval_nogc(struct js *js, const char *buf, jsoff_t len)
{
    jsval_t res = mkval(T_UNDEF, 0);
    js->tok = TOK_ERR;
    js->code = buf;
    js->clen = len;
    js->pos = 0;
    while (js->tok != TOK_EOF && !is_err(res)) {
        js->pos = skiptonext(js->code, js->clen, js->pos);
        if (js->pos >= js->clen) {
            mylogd("parse to the end of code");
            break;
        }
        res = js_stmt(js, TOK_SEMICOLON);
    }
    return res;
}
jsval_t js_eval(struct js *js, const char *buf, size_t len)
{
    if (len == (size_t)~0) {
        len = strlen(buf);
    }
    return js_eval_nogc(js, buf, (jsoff_t)len);
}