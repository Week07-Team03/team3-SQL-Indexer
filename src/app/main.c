#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cli.h"
#include "storage.h"

/* 사용할 수 있는 명령행 실행 형식을 출력한다. */
static void print_usage(const char *program_name) {
    printf("Usage:\n");
    printf("  %s\n", program_name);
    printf("  %s --benchmark [row_count] [lookup_count]\n", program_name);
    printf("  %s --stats\n", program_name);
}

/* 프로그램을 CLI, 벤치마크, 통계 모드 중 하나로 분기한다. */
int main(int argc, char *argv[]) {
    if (argc == 1) {
        run_cli();
        return 0;
    }

    if (strcmp(argv[1], "--benchmark") == 0) {
        size_t row_count = 1000000;
        size_t lookup_count = 200;

        if (argc >= 3) {
            row_count = (size_t)strtoull(argv[2], NULL, 10);
        }
        if (argc >= 4) {
            lookup_count = (size_t)strtoull(argv[3], NULL, 10);
        }
        return run_benchmark(row_count, lookup_count) ? 0 : 1;
    }

    if (strcmp(argv[1], "--stats") == 0) {
        if (!storage_init()) {
            return 1;
        }
        print_storage_stats();
        storage_shutdown();
        return 0;
    }

    print_usage(argv[0]);
    return 1;
}
