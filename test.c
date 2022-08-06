#include "elk.h"
#include "mylog.h"
static void test_basic()
{
    struct js *js;
    char mem[sizeof(*js)+2];
    js = js_create(mem, sizeof(mem));
    if (js == NULL) {
        myloge("js_create fail");
        return;
    }

}

int main(int argc, char const *argv[])
{
    test_basic();
    return 0;
}
