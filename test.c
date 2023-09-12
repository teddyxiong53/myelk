

#include "elk.h"
#include "mylog.h"
static void test_basic()
{
    struct js *js;
    char mem[400];
    js = js_create(mem, sizeof(mem));
    if (js == NULL) {
        myloge("js_create fail");
        return;
    }
    const char *expr = "null";
    js_eval(js, expr, strlen(expr));
    // const char *result = js_str(js, js_eval(js, expr, strlen(expr)));
    // printf("result:%s", result);
}

int main(int argc, char const *argv[])
{
    test_basic();
    return 0;
}
