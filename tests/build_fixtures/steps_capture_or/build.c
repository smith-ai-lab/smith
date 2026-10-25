#include <smith/build.h>

void build(SmithBuild *b) {
    const char *failed = smith_capture_or(b, (const char *[]){"false", nullptr}, "fb1");
    const char *missing = smith_capture_or(b, (const char *[]){"smith-no-such-program", nullptr}, "fb2");
    const char *worked = smith_capture_or(b, (const char *[]){"echo", "hi", nullptr}, "fb3");
    const char *noisy = smith_capture_or(b, (const char *[]){"sh", "-c", "echo oops >&2; exit 3", nullptr}, "fb4");
    const char *killed = smith_capture_or(b, (const char *[]){"sh", "-c", "kill -9 $$", nullptr}, "fb5");
    const char *none = smith_capture_or(b, (const char *[]){"false", nullptr}, nullptr);
    SmithStep step = {
        .command = (const char *[]){"sh", "-c", "echo \"$0|$1|$2|$3|$4|$5\" > \"$6\"", failed, missing, worked, noisy,
                                    killed, none == nullptr ? "null" : "set", "$out", nullptr},
        .outputs = (const char *[]){"result", nullptr},
    };
    smith_step(b, &step);
    smith_default(b, step.outputs);
}
