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
    TOK_EOF,
    TOK_IDENTIFIER,
    TOK_NUMBER,
    TOK_STRING,
    TOK_SEMICOLON,
    TOK_LPAREN,//(
    TOK_RPAREN,//)
    TOK_LBRACE,//{
    TOK_RBRACE,//}
    //keyword token
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
    //JS operator token
    TOK_DOT = 100,
    TOK_CALL,
    TOK_POSTINC, //+=
    TOK_POSTDEC, //-=
    TOK_NOT, //!
    TOK_NEG ,//-
    TOK_TYPEOF,
    TOK_UPLUS,
    TOK_UMINUS,
    TOK_EXP,
    TOK_MUL,
    TOK_DIV,
    TOK_REM,
    TOK_PLUS,
    TOK_MINUS,
    TOK_SHL,
    TOK_SHR,
    TOK_ZSHR,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_EQ,
    TOK_NE,
    TOK_AND,
    TOK_XOR,
    TOK_OR,
    TOK_LAND,
    TOK_LOR,
    TOK_COLON,
    TOK_Q,
    TOK_ASSIGN,
    TOK_PLUS_ASSIGN,
    TOK_MINUS_ASSIGN,
    TOK_MUL_ASSIGN,
    TOK_DIV_ASSIGN,
    TOK_REM_ASSIGN,
    TOK_SHL_ASSIGN,
    TOK_SHR_ASSIGN,
    TOK_ZSHR_ASSIGN,
    TOK_AND_ASSIGN,
    TOK_XOR_ASSIGN,
    TOK_OR_ASSIGN,
    TOK_COMMA,
   

};
#define JS_EXPR_MAX 20

#define MAX_FF_ARGS 6

typdef uintptr_t jw_t;

typedef jsval_t (*w6w_t )(jw_t, jw_t, jw_t,jw_t, jw_t, jw_t );
union ffi_val {
    void *p;
    w6w_t fp;
    jw_t w;
    double d;
    uint64_t u64;
};


struct js *js_create(void *buf, size_t len);

#endif
