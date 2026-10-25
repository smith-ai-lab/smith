// A deferred step whose need is never written: the manifest is out of date after every regeneration.
#include <smith/build.h>
#include <stddef.h>

static void more(SmithBuild *b) { (void)b; }

void build(SmithBuild *b) {
    SmithStep list = {
        .command = (const char *[]){"true", nullptr},
        .outputs = (const char *[]){"list", nullptr},
    };
    smith_step(b, &list);
    smith_default(b, list.outputs);
    smith_defer(b, list.outputs, more);
}
