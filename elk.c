#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elk.h"
#include "mylog.h"

typedef uint32_t jsoff_t;

struct js {
    jsoff_t css;//运行时最大的C栈的大小
    jsoff_t lwm;//最少要保留的内存，低于这个值就可能导致问题。
    const char *code;//当前解析的代码的位置。
    char errmsg[33];
    uint8_t tok;//当前解析得到的tok
    uint8_t consumed;//当前解析到的token有没有被消费掉。
    uint8_t flags;
#define F_NOEXEC 1U  //不要执行
#define F_LOOP   2U  //我们当前在一个loop里面
#define F_CALL   4U //当前在一个函数调用内部。
#define F_BREAK  8U //退出循环
#define F_RETURN  16U // return已经被执行

    jsoff_t clen;// 代码的总长度。
    jsoff_t pos; // 当前解析的位置。
    jsoff_t toff; //token offset，上一个token的偏移位置。
    jsoff_t tlen ;// 上一个token的len
    jsoff_t nogc; //不需要被gc的entity的位置。

    jsval_t tval;// 上一个解析得到的num或者str的值。
    jsval_t scope;// 当前的scope

    uint8_t *mem;// 工作的内存区域。
    jsoff_t size;// 内存的大小。
    jsoff_t brk;//内存的top位置
    jsoff_t gct;// gc threshold， brk超过这个位置，就启动gc。

    jsoff_t maxcss;//允许的最大的C栈大小。
    void *cstk;// c栈pointer，在启动js_eval时的位置。
};

enum {
    TOK_ERR,
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    TOK_SEMICOLON,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    //后面看自己能不能在这里扩展数组的支持.
    // keyword
    TOK_BREAK = 50,
    TOK_CASE,
    TOK_CATCH,
    TOK_CLASS,
    TOK_CONST,
    TOK_CONTINUE,
    TOK_DEFAULT,
    TOK_DELETE,
    TOK_DO,
    TOK_ELSE,
    TOK_FINALLY,
    TOK_FOR,
    TOK_FUNC,
    TOK_IF,
    TOK_IN,
    TOK_INSTANCEOF,
    TOK_LET,
    TOK_NEW,
    TOK_RETURN,
    TOK_SWITCH,
    TOK_THIS,
    TOK_THROW,
    TOK_TRY,
    TOK_VAR,
    TOK_VOID,
    TOK_WHILE,
    TOK_WITH,
    TOK_YIELD,
    TOK_UNDEF,
    TOK_NULL,
    TOK_TRUE,
    TOK_FALSE,

    //operator
    TOK_DOT = 100,//.
    TOK_CALL,//()
    TOK_POSTINC, // i++
    TOK_POSTDEC, // i--
    TOK_NOT, //!
    TOK_TILDA, //~
    TOK_TYPEOF,//typeof
    TOK_UPLUS, // unary plus 一元加法
    /*
    +1    // => 1: 操作数本身就是数字，则直接返回这个数字
    +'1'  // => 1: 把字符串转换为数字
    +'-1' // => -1: 把字符串转换为数字
    */
    TOK_UMINUS,
    TOK_EXP, //指数
    TOK_MUL,
    TOK_DIV,
    TOK_REM, //%求余数
    TOK_PLUS,
    TOK_MINUS,
    TOK_SHL,//<<
    TOK_SHR,//>>
    TOK_ZSHR,//>>且补零
    TOK_LT, // <
    TOK_LE, // <=
    TOK_GT,
    TOK_GE,
    TOK_EQ,
    TOK_NE,
    TOK_AND, // &
    TOK_XOR, // ^
    TOK_OR, // |
    TOK_LAND, // &&
    TOK_LOR, // ||
    TOK_COLON, // :
    TOK_Q, //?
    TOK_ASSIGN, // =
    TOK_PLUS_ASSIGN, // +=
    TOK_MINUS_ASSIGN, // -=
    TOK_MUL_ASSIGN, // *=
    TOK_DIV_ASSIGN, // /=
    TOK_REM_ASSIGN, // %=
    TOK_SHL_ASSIGN, // <<=
    TOK_SHR_ASSIGN, // >>=
    TOK_ZSHR_ASSIGN,
    TOK_AND_ASSIGN, // &=
    TOK_XOR_ASSIGN, // ^=
    TOK_OR_ASSIGN, // |=
    TOK_COMMA, // ,

};


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
    T_CFUNC,
    T_ERR
};

static jsval_t tov(double d)
{
    union {
        double d;
        jsval_t v;
    } u = {
        d
    };
    return u.v;
}

static double tod(jsval_t v)
{
    union {
        jsval_t v;
        double d;

    } u = {
        v
    };
    return u.d;
}
static jsval_t mkval(uint8_t type, uint64_t data)
{
    return ((jsval_t)0x7ff0U << 48) | \
        ((jsval_t)(type)<<48) | \
        (data & 0xffffffffffffUL);

}
static bool is_nan(jsval_t v)
{
    return (v>>52)==0x7ffU;
}

static uint8_t vtype(jsval_t v)
{
    if (is_nan(v)) {
        return (v>>48U)&15U;
    } else {
        return (uint8_t)T_NUM;
    }
}
static size_t vdata(jsval_t v)
{
    return (size_t) (v & ~((jsval_t) 0x7fffUL << 48U));
}
static bool is_unary(uint8_t tok)
{
    return tok>=TOK_POSTINC && tok<=TOK_UMINUS;
}
static bool is_assign(uint8_t tok)
{
    return tok>=TOK_ASSIGN && tok<=TOK_OR_ASSIGN;
}
static jsval_t loadval(struct js* js, jsoff_t off)
{
    jsval_t v = 0;
    memcpy(&v, &js->mem[off], sizeof(v));
    return v;
}
static jsoff_t align32(jsoff_t v)
{
    return (((v+3)>>2) << 2);
}

static bool is_err(jsval_t v)
{
    return vtype(v) == T_ERR;
}

static bool is_space(int c)
{
    return c==' ' || c=='\r' || c=='\n' || c=='\f' || c=='\v' || c=='\t';
}
static bool is_digit(int c)
{
    return c>='0' && c<='9';
}
static bool is_xdigit(int c)
{
    return is_digit(c) || (c>='a' && c<='f') || (c>='A'&&c<='F');
}
static bool is_alpha(int c)
{
    return (c>='a' && c<='z') || (c>='A' && c<='Z');
}
static bool is_ident_begin(int c)
{
    return c=='_' || c== '$' || is_alpha(c);
}
static bool is_ident_continue(int c)
{
    return c=='_' || c== '$' || is_alpha(c) || is_digit(c);
}
/*
    这里为什么是24bit？
    有48bit可以用。
    其中24bit用来表示长度，放在高位。
*/
static jsval_t mkcoderef(jsval_t off, jsoff_t len)
{
    return mkval(T_CODEREF, 
        (off & 0xffffffU) | ((jsval_t)(len & 0xffffffU)<<24U)
    );
}
static jsoff_t coderefoff(jsval_t v)
{
    return v&0xffffffU;
}
static jsoff_t codereflen(jsval_t v)
{
    return (v>>24U)&0xffffffU;
}
//希望下一个tok是什么，如果不是，报错。
#define EXPECT(_tok, _e) do { \
    if (next(js) != _tok) { \
        _e; \
        return js_mkerr(js, "parse error"); \
    }; \
    js->consumed = 1; \
} while(0)

static size_t cpy(char *dst, size_t dstlen, const char *src, size_t srclen)
{
    size_t i = 0;
    for(i=0; i<dstlen && i<srclen && src[i]!=0; i++) {
        dst[i] = src[i];
    }
    if (dstlen > 0) {
        if (i < dstlen) {
            dst[i] = '\0';
        } else {
            dst[dstlen-1] = '\0';
        }
    }
    return i;
}

static inline jsoff_t esize(jsoff_t w)
{
    switch (w&3U)
    {
    case T_OBJ:
        return (jsoff_t)(sizeof(jsoff_t) + sizeof(jsoff_t));
    case T_PROP:
        return (jsoff_t)(sizeof(jsoff_t) + sizeof(jsoff_t) + sizeof(jsval_t));
    case T_STR:
        return (jsoff_t)(sizeof(jsoff_t) + align32(w>>2U));
    default:
        return (jsoff_t)~0U;
    }
}

static jsoff_t js_alloc(struct js *js, size_t size)
{
    jsoff_t ofs = js->brk;
    size = align32(size);
    if (js->brk + size > js->size) {
        myloge("oom");
        return ~0U;
    }
    js->brk += size;
    return ofs;
}

jsval_t js_mkerr(struct js *js, const char *xx, ...)
{
    va_list ap;
    size_t n = cpy(js->errmsg, sizeof(js->errmsg), "ERROR: ", 7);
    va_start(ap, xx);
    vsnprintf(js->errmsg+n, sizeof(js->errmsg), xx, ap);
    va_end(ap);
    js->errmsg[sizeof(js->errmsg) - 1] = '\0';
    js->pos = js->clen;
    js->tok = TOK_EOF;//为什么是给EOF，而不是给ERR呢？
    js->consumed = 0; //这3句表示出错就跳转到文件末尾。
    return mkval(T_ERR, 0);
}

/*
    b表示什么？

*/
static jsval_t mkentity(struct js* js, jsoff_t b, const char *buf, size_t len)
{
    jsoff_t ofs = js_alloc(js, len + sizeof(b));//mkobj的时候，长度是8
    if (ofs == (jsoff_t)~0) {
        return js_mkerr(js, "oom");
    }
    memcpy(&js->mem[ofs], &b, sizeof(b));
    if (buf != NULL) {
        //memmove可以处理内存重叠的情况，比memcpy更加安全。
        //当然，你明确知道内部不会重叠的时候，还是优先用memcpy
        memmove(&js->mem[ofs+sizeof(b)], buf, len);
    }
    if ((b&3) == T_STR) {
        js->mem[ofs + sizeof(b) + len - 1] = 0;
    }
    return mkval(b&3, ofs);

}

static const char *typestr(uint8_t)
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
        "coderef",//这个具体指什么？
        "cfunc",
        "err",
        "nan"
    };
    if (t < sizeof(names)/sizeof(names[0])) {
        return names[t];
    } else {
        return "??";
    }
}
/*
    parent表示要创建的obj的parent的entity所在的offset位置。
    为0表示第一个对象，是全局对象。
*/
static jsval_t mkobj(struct js* js, jsoff_t parent)
{
    return mkentity(js, 0 | T_OBJ, &parent, sizeof(parent));
}

jsval_t js_mkundef(void)
{
    return mkval(T_UNDEF, 0);
}
jsval_t js_mknull(void)
{
    return mkval(T_NULL, 0);
}
jsval_t js_mktrue(void)
{
    return mkval(T_BOOL, 1);
}
jsval_t js_mkfalse(void)
{
    return mkval(T_BOOL, 0);
}
jsval_t js_mknum(double value)
{
    return tov(value);
}
jsval_t js_mkobj(struct js *js)
{
    return mkobj(js, 0);
}

jsval_t js_mkfun(jsval_t (*fn)(struct js *, jsval_t *, int))
{
    return mkval(T_FUNC, (size_t)(void *)fn);
}

struct js * js_create(void *buf, size_t len)
{
    struct js *js = NULL;
    if (len < sizeof(*js) + esize(T_OBJ)) {
        return js;
    }
    memset(buf, 0, len);
    js = (struct js *)buf;
    js->mem = (uint8_t *)(js+1);//先跳过js结构体大小，再把指针转成uint8_t的。
    js->size = (jsoff_t)(len - sizeof(*js));
    js->scope = mkobj(js, 0);
    js->size = js->size/8U * 8U;// 8字节对齐
    js->lwm = js->size;
    js->gct = js->size/2;
    return js;
}
/*
    n表示当前的解析的位置
    跳到下一个有效字符上。
*/
static jsoff_t skiptonext(const char *code, jsoff_t len, jsoff_t n)
{
    while (n < len) {
        if (is_space(code[n])) {
            n++;//跳过空白字符
        } else if(n+1 < len && code[n]=='/' && code[n+1]=='/') {
            // 是单行注释的情况
            for (n+=2; n<len && code[n]!='\n';) {
                n++;
            }
        } else if(n+3 < len && code[n]=='/' && code[n+1]=='*') {
            //多行注释的情况
            for (n+=4; n<len && (code[n-2]!='*' || (code[n-1]!='/')); ) {
                n++;
            }
        } else {
            break;//非空白，非注释
        }
    }
    return n;
}

static bool streq(const char *buf, size_t len, const char *p, size_t n)
{
    return n == len && (memcmp(buf, p, len) == 0);
}

static uint8_t parsekeyword(const char *buf, size_t len)
{
    switch (buf[0]) {
        case 'b':
            if ((streq("break", 5, buf, len))) {
                return TOK_BREAK;
            }
            break;
        case 'c':
            if (streq("class", 5, buf, len)) {
                return TOK_CLASS;
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
            if (streq("else", 4, buf, len)) {
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
        case 'l':
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
        default:
            break;
    }
    return TOK_IDENTIFIER;
}

static uint8_t parseident(const char* buf, jsoff_t len, jsoff_t *tlen)
{
    if (is_ident_begin(buf[0])) {
        while (*tlen < len && is_ident_continue(buf[*tlen])) {
            return parsekeyword(buf, *tlen);
        }
    }
    return TOK_ERR;
}
static uint8_t next(struct js *js)
{
    if (js->consumed == 0) {
        return js->tok;//当前的tok还没有消费掉，那么直接返回当前的tok
    }
    js->consumed = 0;
    js->tok = TOK_ERR;
    js->toff = js->pos = skiptonext(js->code, js->clen, js->pos);
    //当前到了有效字符上了。
    js->tlen = 0;
    const char *buf = js->code + js->toff;
    //buf指向有效的字符位置
    if (js->toff >= js->clen) {
        //已经到末尾了
        js->tok = TOK_EOF;
        return js->tok;//会导致外面跳出循环
    }
#define TOK(T, LEN) {js->tok = T; js->tlen = (LEN); break;}
#define LOOK(OFS, CH) js->toff+OFS < js->clen && buf[OFS]==CH
    switch(buf[0]) {
        case '?': TOK(TOK_Q, 1);
        case ':': TOK(TOK_COLON, 1);
        case '(': TOK(TOK_LPAREN, 1);
        case ')': TOK(TOK_RPAREN, 1);
        case '{': TOK(TOK_LBRACE, 1);
        case '}': TOK(TOK_RBRACE, 1);
        case ';': TOK(TOK_SEMICOLON, 1);
        case ',': TOK(TOK_COMMA, 1);
        case '!': if(LOOK(1,'=') && LOOK(2, '=')) TOK(TOK_NE, 3); TOK(TOK_NOT, 1);
        case '.': TOK(TOK_DOT, 1);
        case '~': TOK(TOK_TILDA, 1);
        case '-': if (LOOK(1, '-')) TOK(TOK_POSTDEC, 2); if (LOOK(1,'=')) TOK(TOK_MINUS_ASSIGN, 2); TOK(TOK_MINUS, 1);
        case '+': if (LOOK(1, '+')) TOK(TOK_POSTINC, 2); if (LOOK(1, '=')) TOK(TOK_PLUS_ASSIGN, 2); TOK(TOK_PLUS, 1);
        case '*': if (LOOK(1, '*')) TOK(TOK_EXP, 2); if (LOOK(1, '=')) TOK(TOK_MUL_ASSIGN, 2); TOK(TOK_MUL, 1);
        case '/': if (LOOK(1, '=')) TOK(TOK_DIV_ASSIGN, 2); TOK(TOK_DIV, 1);
        case '%': if (LOOK(1, '=')) TOK(TOK_REM_ASSIGN, 2); TOK(TOK_REM, 1);
        case '&': if (LOOK(1, '&')) TOK(TOK_LAND, 2); if (LOOK(1, '=')) TOK(TOK_AND_ASSIGN, 2); TOK(TOK_AND, 1);
        case '|': if (LOOK(1, '|')) TOK(TOK_LOR, 2); if (LOOK(1, '=')) TOK(TOK_OR_ASSIGN, 2); TOK(TOK_OR, 1);
        case '=': if (LOOK(1, '=') && LOOK(2, '=')) TOK(TOK_EQ, 3); TOK(TOK_ASSIGN, 1);
        case '<': if (LOOK(1, '<') && LOOK(2, '=')) TOK(TOK_SHL_ASSIGN, 3); if (LOOK(1, '<')) TOK(TOK_SHL, 2); if (LOOK(1, '=')) TOK(TOK_LE, 2); TOK(TOK_LT, 1);
        case '>': if (LOOK(1, '>') && LOOK(2, '=')) TOK(TOK_SHR_ASSIGN, 3); if (LOOK(1, '>')) TOK(TOK_SHR, 2); if (LOOK(1, '=')) TOK(TOK_GE, 2); TOK(TOK_GT, 1);
        case '^': if (LOOK(1, '=')) TOK(TOK_XOR_ASSIGN, 2); TOK(TOK_XOR, 1);
        case '"': 
        case '\'':
            js->tlen++;
            while (js->toff + js->tlen < js->clen && buf[js->tlen] != buf[0]) {//buf[0]表示是引号，这里表示没有到字符串结束的位置
                uint8_t increment = 1;
                if (buf[js->tlen] == '\\') {
                    //考虑使用转义的情况
                    if (js->toff + js->tlen + 2 > js->clen) {
                        break;
                    }
                    increment = 2;
                    if (buf[js->tlen + 1] == 'x') {
                        // \\x 的情况
                        if (js->toff + js->tlen + 4 > js->clen) {
                            break;
                        }
                        increment = 4;

                    }
                }
                js->tlen += increment;
            }
            if (buf[0] == buf[js->tlen]) {
                js->tok = TOK_STRING;
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
        case '9':
            //数字的情况
            {
                char *end;
                js->tval = tov(strtod(buf, &end));
                TOK(TOK_NUMBER, (jsoff_t)(end - buf));//这里面有braek了
            }
        default://默认就是普通字母的情况。
            js->tok = parseident(buf, js->clen - js->toff, &js->tlen);
            break;

    }
}

static jsval_t js_continue(struct js *js)
{
    if (js->flags & F_NOEXEC) {
        // 
    } else {
        if (!(js->flags & F_LOOP)) {
            return js_mkerr(js, "not in loop");
        }
        js->flags |= F_NOEXEC;
    }
    js->consumed = 1;
    return js_mkundef();
}

static jsval_t resolveprop(struct js *js, jsval_t v)
{
    if (vtype(v) != T_PROP) {
        return v;
    }
    return resolveprop(js, 
        loadval(js, (jsoff_t)(vdata(v) + sizeof(jsoff_t)*2));
    )
}

jsval_t js_mkstr(struct js *js, const void *ptr, size_t len)
{
    jsoff_t n = (jsoff_t)(len+1);
    return mkentity(js, (jsoff_t)((n<<2) | T_STR), ptr, n);
}

/*
    作用是：把mem里偏移到off位置的offset值取出来。
*/
static jsoff_t loadoff(struct js *js, jsoff_t off)
{
    jsoff_t v = 0;
    assert(js->brk <= js->size);
    memcpy(&v, &js->mem[off], sizeof(v));
    return v;
}

/*
    把off转成len
*/
static jsoff_t offtolen(jsoff_t off)
{
    return (off>>2) - 1;
}
/*
    返回js 字符串的mem offset和长度
*/
static jsoff_t vstr(struct js* js, jsval_t value, jsoff_t *len)
{
    jsoff_t off = (jsoff_t)vdata(value);
    if (len) {
        *len = offtolen(loadoff(js, off));
    }
    return (jsoff_t)(off + sizeof(off));
}

static jsval_t upper(struct js *js, jsval_t scope)
{
    return mkval(T_OBJ, 
        loadoff(js, (jsoff_t)(vdata(scope) + sizeof(jsoff_t)))
    );
}
static void mkscope(struct js *js)
{
    assert((js->flags & F_NOEXEC) == 0);//那就要确保要exe
    jsoff_t prev = (jsoff_t)vdata(js->scope);
    js->scope = mkobj(js, prev);
}

static void delscope(struct js *js)
{
    js->scope = upper(js, js->scope);
}
static jsval_t call_js(struct js *js, const char *fn, jsoff_t fnlen)
{
    jsoff_t fnpos = 1;
    mkscope(js);//创建函数调用scope
    while (fnpos < fnlen) {
        fnpos = skiptonext(fn, fnlen, fnpos);
        if (fnpos < fnlen && fn[fnpos] == ')') {
            break;
        }
        jsoff_t identlen = 0;
        uint8_t tok = parseident(&fn[fnpos], fnlen-fnpos, &identlen);
        if (tok != TOK_IDENTIFIER) {
            break;
        }
        // 到这里，我们拿到了arg name，计算arg value
        
    }
}
static jsval_t do_call_op(struct js *js, jsval_t func, jsval_t args)
{
    if (vtype(args) != T_CODEREF) {
        return js_mkerr(js, "bad call");
    }
    if (vtype(func) != T_FUNC && vtype(func) != T_CFUNC) {
        return js_mkerr(js, "calling non-function");
    }
    const char *code = js->code;
    jsoff_t clen = js->clen;
    jsoff_t pos = js->pos;
    js->code = &js->code[coderefoff(args)];
    js->clen = codereflen(args);
    js->pos = skiptonext(js->code, js->clen, 0);//调到第一个参数上
    uint8_t tok = js->tok;
    uint8_t flags = js->flags;//保存flags
    jsoff_t nogc = js->nogc;
    jsval_t res = js_mkundef();
    if (vtype(func) == T_FUNC) {
        jsoff_t fnlen = 0;
        jsoff_t fnoff = vstr(js, func, &fnlen);//拿到函数名字
        js->nogc = (jsoff_t)(fnoff - sizeof(jsoff_t));//标记这个内容不要被gc回收。
        res = call_js(js, (const char *)(&js->mem[fnoff]), fnlen);
    } else {
        res = call_c(js, (jsval_t (*)(struct js*, jsval_t*, int))vdata(func));
    }

}
static jsval_t do_op(strut js* js, uint8_t op, jsval_t lhs, jsval_t rhs)
{
    if (js->flags & F_NOEXEC) {
        return 0;//返回0意义是什么？
    }
    jsval_t l = resolveprop(js, lhs);
    jsval_t r = resolveprop(js, rhs);
    // setlwm(js); TODO
    if (is_err(l)) {
        return l;
    }
    if (is_err(r)) {
        return r;
    }
    if (is_assign(op) && vtype(lhs) != T_PROP) {
        return js_mkerr(js, "bad lhs");
    }
    switch (op) {
        // typeof是运算符号，所以可以不加括号的, typeof a这样。
        case TOK_TYPEOF:
            return js_mkstr(js, typestr(vtype(r)), strlen(typestr(vtype(r))));
        case TOK_CALL:
            return do_call_op(js, l, r);
        
    }
}
// 从右到左的二元操作
#define RTL_BINOP(_f1, _f2, _cond)  \
    jsval_t res = _f1(js);                 \
    while (!(is_err(res)) && (_cond)) {    \
        uint8_t op = js->tok;              \
        js->consumed = 1;                  \
        jsval_t rhs = _f2(js);             \
        if (is_err(rhs)) {                 \
            return rhs;                    \
        }                                  \
        res = do_op(js, op, res, rhs);     \
    }                                      \
    return res;
static jsval_t js_break(struct js *js)
{
    if (js->flags & F_NOEXEC) {
        //
    } else {
        if (!(js->flags & F_LOOP)) {
            return js_mkerr(js, "not in loop");
        }
        js->flags |= F_BREAK | F_NOEXEC;
    }
    js->consumed = 1;
    return js_mkundef();
}
//这是三元操作符 TODO
static jsval_t js_tenary(struct js *js)
{
    jsval_t res = js_mkundef();
    return res;
}
static jsval_t js_assignment(struct js *js)
{
    RTL_BINOP(js_ternary, js_assignment, 
        (next(js) == TOK_ASSIGN || js->tok == TOK_PLUS_ASSIGN \
            || js->tok == TOK_MINUS_ASSIGN \
            || js->tok == TOK_MUL_ASSIGN \
            || js->tok == TOK_DIV_ASSIGN \
            || js->tok == TOK_REM_ASSIGN \
            || js->tok == TOK_SHL_ASSIGN \
            || js->tok == TOK_SHR_ASSIGN \
            || js->tok == TOK_ZSHR_ASSIGN \
            || js->tok == TOK_AND_ASSIGN \
            || js->tok == TOK_OR_ASSIGN \
            || js->tok == TOK_XOR_ASSIGN \
        )
    );
}
//表达式只有赋值表达式
static jsval_t js_expr(strut js *js)
{
    return js_assignment(js);
}
// let a = 1;
static jsval_t js_let(struct js *js)
{
    uint8_t exe = !(js->flags & F_NOEXEC);
    js->consumed = 1;
    for (;;) {
        EXPECT(TOK_IDENTIFIER, );
        js->consumed = 0;
        jsoff_t noff = js->toff;//试图取出后面的identifier
        jsoff_t nlen = js->tlen;//n是指next的意思
        char *name = (char *)&js->code[noff];//拿到id的name
        jsval_t v = js_mkundef();
        js->consumed = 1;
        //id后面就应该是一个赋值符号。
        if (next(js) == TOK_ASSIGN) {
            js->consumed = 1;
            v = js_expr(js);
            if (is_err(v)) {
                return v;
            }
        }
    }
}
static jsval_t js_stmt(struct js *js)
{
    jsval_t res;
    if (js->brk > js->gct) {
        // js_gc(js);
    }
    switch(next(js)) {
        case TOK_CASE:
        case TOK_CATCH:
        case TOK_CLASS:
        case TOK_CONST:
        case TOK_DEFAULT:
        case TOK_DELETE:
        case TOK_DO:
        case TOK_FINALLY:
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
        case TOK_WHILE:
        case TOK_YIELD:
            res = js_mkerr(js, "'%.*s not implemented", (int)js->tlen, js->code+js->toff);
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
        default:
            break;
    }
    return res;
}

jsval_t js_eval(struct js *js, const char *buf, size_t len)
{
    jsval_t res = js_mkundef();
    if (len == (size_t)~0U) {
        len = strlen(buf);
    }
    js->consumed = 1;
    js->tok = TOK_ERR;
    js->code = buf;
    js->clen = (jsoff_t)len;
    js->pos = 0;
    js->cstk = &res;//为什么指向这个？因为是C栈的第一个局部变量。
    while (next(js) != TOK_EOF && !is_err(res)) {
        res = js_stmt(js);
    }
}