#include "elk.h"

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
    TOK_XOR_ASSIGN, // ^=\
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


static jsoff_t align32(jsoff_t v)
{
    return (((v+3)>>2) << 2);
}


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
    js->tok = TOK_EOF;
    js->consumed = 0; //这3句表示出错就跳转到文件末尾。
    return mkval(T_ERR, 0);
}

/*
    b表示什么？

*/
static jsval_t mkentity(struct js* js, jsoff_t b, const char *buf, size_t len)
{
    jsoff_t ofs = js_alloc(js, len + sizeof(b));//mkobj的时候，长度是8
    if (ofs == ~0) {
        return js_mkerr(js, "oom");
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
}