#include <smith/build.h>
#include <stdio.h>

static int play(SmithRun *run) {
    printf("function line first\n");
    int shown = smith_run_program(run, (const char *[]){"sh", "-c", "echo \"program says $SMITH_TEST_WORD and $HOME_KEPT\"; echo err-line >&2", nullptr},
                                  (const char *[]){"SMITH_TEST_WORD=two words $x", "HOME_KEPT=kept", nullptr});
    int three = smith_run_program(run, (const char *[]){"sh", "-c", "exit 3", nullptr}, nullptr);
    int killed = smith_run_program(run, (const char *[]){"sh", "-c", "kill -9 $$", nullptr}, nullptr);
    int missing = smith_run_program(run, (const char *[]){"smith-no-such-program", nullptr}, nullptr);
    printf("statuses %d %d %d %d\n", shown, three, killed, missing);
    return 0;
}

void build(SmithBuild *b) {
    SmithStep step = {.run = play};
    smith_task(b, "play", &step);
    smith_default(b, smith_list(b, nullptr));
}
