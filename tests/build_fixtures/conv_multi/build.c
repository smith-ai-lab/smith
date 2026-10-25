#include <smith/build.h>

void build(SmithBuild *b) {
    smith_defaults(b, &(SmithDefaults){
                          .flags = smith_list(b, "-I", "src", nullptr),
                          .folders = (const SmithFolder[]){{"src/base", smith_list(b, "-DFROM_BASE=1", nullptr),
                                                                    smith_list(b, "build/gen.h", nullptr)},
                                                                   {}},
                          .link_flags = smith_list(b, "-Wl,-headerpad,0x100", nullptr),
                      });
    smith_step(b, &(SmithStep){.command = smith_list(b, "sh", "-c", "printf '#define GEN_VALUE 7\\n' > build/gen.h", nullptr),
                               .outputs = smith_list(b, "build/gen.h", nullptr)});
    smith_app(b, &(SmithApp){});
    smith_app(b, &(SmithApp){.entry = "tools/tool.c", .name = "tool", .tool = true});
    smith_app_tests(b, &(SmithAppTests){});
    smith_write(b, "info.txt",
                smith_fmt(b, "sources: %s\nobjects: %s\nflags: %s\nlink: %s\nsource flags: %s\n",
                          smith_join(b, smith_sources(b, "src/base"), " "),
                          smith_join(b, smith_objects(b, "release", ""), " "),
                          smith_join(b, smith_profile_flags(b, "fast"), " "),
                          smith_join(b, smith_profile_link_flags(b, "release"), " "),
                          smith_join(b, smith_source_flags(b, "debug", "src/base/value.c"), " ")));
}
