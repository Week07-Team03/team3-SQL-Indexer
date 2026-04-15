#include <stdio.h>
#include <string.h>
#include "cli.h"
#include "executor.h"
#include "parser.h"
#include "storage.h"

#define CLI_LINE_BUFFER_SIZE 256
#define CLI_STATEMENT_BUFFER_SIZE 2048

/* 사용자 입력 끝의 공백과 개행 문자를 제거한다. */
static void trim_line(char *line) {
    int end = (int)strlen(line) - 1;

    while (end >= 0 &&
           (line[end] == '\n' || line[end] == '\r' || line[end] == ' ' || line[end] == '\t')) {
        line[end--] = '\0';
    }
}

static int read_statement(char *statement, size_t size) {
    char line[CLI_LINE_BUFFER_SIZE];
    size_t used = 0;
    int first_line = 1;

    statement[0] = '\0';

    while (1) {
        printf(first_line ? "mini-sql> " : "       > ");
        if (fgets(line, sizeof(line), stdin) == NULL) {
            return used > 0;
        }

        trim_line(line);
        if (first_line && line[0] == '\0') {
            continue;
        }

        if (used > 0) {
            if (used + 1 >= size) {
                return 0;
            }
            statement[used++] = ' ';
            statement[used] = '\0';
        }

        if (used + strlen(line) >= size) {
            return 0;
        }
        strcpy(statement + used, line);
        used += strlen(line);

        if (strchr(line, ';') != NULL || (first_line && line[0] == '.')) {
            return 1;
        }

        first_line = 0;
    }
}

/* 대화형 셸 명령과 예제 쿼리를 출력한다. */
static void print_help(void) {
    printf(".help\n");
    printf(".tables\n");
    printf(".schema users\n");
    printf(".stats\n");
    printf(".benchmark [row_count] [lookup_count]\n");
    printf(".exit\n");
    printf("INSERT INTO users VALUES ('alice', 23);\n");
    printf("INSERT INTO users VALUES (10, 'alice', 23);\n");
    printf("SELECT * FROM users;\n");
    printf("SELECT id, name FROM users WHERE id = 10;\n");
    printf("SELECT * FROM users WHERE name = 'alice';\n");
    printf("SELECT * FROM users WHERE id BETWEEN 10 AND 20;\n");
}

/* 점으로 시작하는 메타 명령을 처리하고 계속 실행할지 반환한다. */
static int handle_meta_command(const char *line) {
    char table_name[32];
    size_t row_count;
    size_t lookup_count;

    if (strcmp(line, ".exit") == 0) {
        return 0;
    }
    if (strcmp(line, ".help") == 0) {
        print_help();
        return 1;
    }
    if (strcmp(line, ".tables") == 0) {
        list_tables();
        return 1;
    }
    if (strcmp(line, ".stats") == 0) {
        print_storage_stats();
        return 1;
    }
    if (sscanf(line, ".schema %31s", table_name) == 1) {
        if (!print_schema(table_name)) {
            printf("table not found\n");
        }
        return 1;
    }
    if (sscanf(line, ".benchmark %zu %zu", &row_count, &lookup_count) == 2) {
        run_benchmark(row_count, lookup_count);
        return 1;
    }
    if (sscanf(line, ".benchmark %zu", &row_count) == 1) {
        run_benchmark(row_count, 200);
        return 1;
    }
    if (strcmp(line, ".benchmark") == 0) {
        run_benchmark(1000000, 200);
        return 1;
    }

    printf("unknown command\n");
    return 1;
}

/* REPL 루프를 실행하고 파싱된 쿼리를 실행기로 넘긴다. */
void run_cli(void) {
    char statement[CLI_STATEMENT_BUFFER_SIZE];
    Query query;

    if (!storage_init()) {
        printf("storage init failed\n");
        return;
    }

    while (1) {
        if (!read_statement(statement, sizeof(statement))) {
            break;
        }
        if (statement[0] == '\0') {
            continue;
        }

        if (statement[0] == '.') {
            if (!handle_meta_command(statement)) {
                break;
            }
            continue;
        }

        if (!parse_query(statement, &query)) {
            printf("syntax error\n");
            continue;
        }
        execute_query(&query);
    }

    storage_shutdown();
}
