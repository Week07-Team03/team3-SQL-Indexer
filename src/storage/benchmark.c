#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "storage_internal.h"

/* 벤치마크 구간의 경과 시간을 초 단위로 계산한다. */
static double elapsed_seconds(const struct timespec *start, const struct timespec *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double nanoseconds = (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;

    return seconds + nanoseconds;
}

/* 대량 삽입과 인덱스 조회, 선형 조회 성능을 비교 측정한다. */
int run_benchmark(size_t record_count, size_t lookup_count) {
    TableSchema schema;
    Database *database;
    struct timespec insert_start;
    struct timespec insert_end;
    struct timespec index_start;
    struct timespec index_end;
    struct timespec linear_start;
    struct timespec linear_end;
    Query query;
    QueryResult result;
    size_t i;
    double insert_seconds;
    double index_seconds;
    double linear_seconds;
    char name[32];

    if (record_count == 0 || lookup_count == 0) {
        return 0;
    }
    if (!storage_read_schema_file(STORAGE_TABLE_NAME, &schema)) {
        return 0;
    }

    database = database_create_empty(&schema);
    if (database == NULL) {
        return 0;
    }

    clock_gettime(CLOCK_MONOTONIC, &insert_start);
    for (i = 0; i < record_count; i++) {
        snprintf(name, sizeof(name), "user_%07zu", i + 1);
        if (!database_append_user(database, 0, 0, name, 20 + (int)(i % 43), 0, NULL, NULL)) {
            database_destroy(database);
            return 0;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &insert_end);

    memset(&query, 0, sizeof(Query));
    query.type = QUERY_SELECT;
    strcpy(query.table_name, STORAGE_TABLE_NAME);
    query.select_all = 1;
    query.condition_type = CONDITION_ID_EQ;

    clock_gettime(CLOCK_MONOTONIC, &index_start);
    for (i = 0; i < lookup_count; i++) {
        query.condition_int_value = (int)((i * 7919) % record_count) + 1;
        if (!database_select_users(database, &query, &result)) {
            database_destroy(database);
            return 0;
        }
        free_query_result(&result);
    }
    clock_gettime(CLOCK_MONOTONIC, &index_end);

    query.condition_type = CONDITION_NAME_EQ;
    clock_gettime(CLOCK_MONOTONIC, &linear_start);
    for (i = 0; i < lookup_count; i++) {
        snprintf(query.condition_text_value,
                 sizeof(query.condition_text_value),
                 "user_%07zu",
                 ((i * 7919) % record_count) + 1);
        if (!database_select_users(database, &query, &result)) {
            database_destroy(database);
            return 0;
        }
        free_query_result(&result);
    }
    clock_gettime(CLOCK_MONOTONIC, &linear_end);

    insert_seconds = elapsed_seconds(&insert_start, &insert_end);
    index_seconds = elapsed_seconds(&index_start, &index_end);
    linear_seconds = elapsed_seconds(&linear_start, &linear_end);

    printf("benchmark records: %zu\n", record_count);
    printf("lookup repetitions: %zu\n", lookup_count);
    printf("insert time: %.6f sec\n", insert_seconds);
    printf("select by id (B+ tree): %.6f sec total, %.3f usec/query\n",
           index_seconds,
           (index_seconds * 1000000.0) / (double)lookup_count);
    printf("select by name (linear scan): %.6f sec total, %.3f usec/query\n",
           linear_seconds,
           (linear_seconds * 1000000.0) / (double)lookup_count);
    if (index_seconds > 0.0) {
        printf("speedup: %.2fx\n", linear_seconds / index_seconds);
    }
    printf("b+tree height after insert: %d\n", database_index_height(database));

    database_destroy(database);
    return 1;
}
