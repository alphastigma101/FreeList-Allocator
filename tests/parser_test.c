#include <stdio.h>
#include <string.h>
#include <assert.h> // Development
#include <stdalign.h> // Development
#include "../parser/parser.h"

static const struct {
    const char* input;
    const char* description;
} test_cases[] = {
    { "Hello.ko",                                          "single module"                         },
    { "Hello.ko World.ko.zst I.ko am Ready To Rumbe",     "chain of modules with plain words"     },
    { "# this.ko is a comment",                           "comment suppresses module"              },
    { "# first comment\n# second comment\nreal.ko",       "chain of comments then a module"       },
    { "/path/to/module/foo.ko",                            "Path to a module"                     },
    { "/lib/modules/6.12.68/kernel/drivers/net/e1000e.ko /sys/bus/pci-devices/eth0.ko /opt/custom-drivers/realtek_r8169.ko", "three modules with paths, dots, and dashes" }
};


int main(void) {
    const int n = sizeof(test_cases) / sizeof(test_cases[0]);

    for (int i = 0; i < n; i++) {
        char str[512];
        strncpy(str, test_cases[i].input, sizeof(str) - 1);
        const int len = strlen(str);
        str[len] = '\0';

        printf("── Test %d: %s ──\n", i, test_cases[i].description);

        const tokens_t* deps = scanner(str, len);
        assert(deps != NULL && "parse must not return NULL");

        switch (i) {

            case 0: // single module
                assert(count_type(deps, 0x02) == 1  && "single module: exactly one MODULE token");
                assert(count_type(deps, 0x01)   == 1  && "single module: None token is expected!");
                assert(get_nth_type(deps, 0x02, 0) != NULL && "single module: MODULE token exists");
                assert(strcmp(get_nth_type(deps, 0x02, 0)->values.module, "Hello.ko") == 0 && "single module: name is 'Hello.ko'");
                break;

            case 1: // chain of modules with plain words
                assert(count_type(deps, 0x02) == 3  && "chain: exactly 3 MODULE tokens");
                assert(strcmp(get_nth_type(deps, 0x02, 0)->values.module, "Hello.ko") == 0 && "chain: 1st module is 'Hello'");
                assert(strcmp(get_nth_type(deps, 0x02, 1)->values.module, "World.ko.zst") == 0 && "chain: 2nd module is 'World'");
                assert(strcmp(get_nth_type(deps, 0x02, 2)->values.module, "I.ko")     == 0 && "chain: 3rd module is 'I'");
                assert(get_nth_type(deps, 0x02, 3) == NULL && "chain: no 4th MODULE token");
                assert(count_all(deps) >= 3 && "chain: list is fully walkable");
                break;

            case 2: // comment suppresses module
                assert(count_type(deps, 0x02)   == 0 && "comment: no MODULE tokens");
                //assert(count_type(deps, 0x08) >= 1 && "comment: COMMENTS token emitted");
                assert(get_nth_type(deps, 0x02, 0) == NULL && "comment: no MODULE at index 0");
                break;
                
            case 3: // chain of comments then a module
                assert(count_type(deps, 0x02)   == 1 && "comments+module: exactly one MODULE");
                //assert(count_type(deps, 0x08) >= 2 && "comments+module: at least 2 COMMENTS tokens");
                //assert(count_type(deps, 0x04)  >= 2 && "comments+module: newlines are tokenised");
                assert(get_nth_type(deps, 0x02, 0) != NULL && "comments+module: MODULE token exists");
                assert(strcmp(get_nth_type(deps, 0x02, 0)->values.module, "real.ko") == 0 && "comments+module: module name is 'real.ko'");
                assert(get_nth_type(deps, 0x02, 1) == NULL && "comments+module: no second MODULE token");
                break;
            case 4:
                assert(count_type(deps, 0x02)   == 1 && "path to module: should be just one module");
                break;
            case 5:
                assert(count_type(deps, 0x02)   == 3 && "multiple module paths: excepting three modules");
                break;
        }

        printf("   PASSED\n");
        print_tokens(deps);
        if (deps) clean_tokens((tokens_t*)deps);
        deps = (void*)0;
    }

    printf("\nAll tests passed.\n");
    return 0;
}