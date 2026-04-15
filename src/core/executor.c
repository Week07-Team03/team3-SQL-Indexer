#include <stdio.h>
#include <time.h>
#include "display.h"
#include "executor.h"
#include "storage.h"

/* INSERT 쿼리를 저장하고 할당된 id를 출력한다. */
static int execute_insert(const Query *query) {
    int assigned_id;

    if (!append_user(query, &assigned_id)) {
        printf("insert failed\n");
        return 0;
    }
    printf("1 row inserted (id=%d)\n", assigned_id);
    return 1;
}

/* SELECT 쿼리 결과를 조회한 뒤 화면에 출력한다. */
static int execute_select(const Query *query) {
    TableSchema schema;
    QueryResult result;
    int printed;

    if (!load_schema(query->table_name, &schema)) {
        printf("table not found\n");
        return 0;
    }
    if (!select_users(query, &result)) {
        printf("select failed\n");
        return 0;
    }

    printed = print_select_result(query, &schema, &result);
    free_query_result(&result);
    return printed;
}

static double elapsed_milliseconds(const struct timespec *start, const struct timespec *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec) * 1000.0;
    double nanoseconds = (double)(end->tv_nsec - start->tv_nsec) / 1000000.0;

    return seconds + nanoseconds;
}

/* 파싱된 쿼리를 타입에 맞는 실행 경로로 분기한다. */
int execute_query(const Query *query) {
    struct timespec start_time;
    struct timespec end_time;
    int success = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    if (query->type == QUERY_INSERT) {
        success = execute_insert(query);
    } else if (query->type == QUERY_SELECT) {
        success = execute_select(query);
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    printf("execution time: %.3f ms\n", elapsed_milliseconds(&start_time, &end_time));
    return success;
}
