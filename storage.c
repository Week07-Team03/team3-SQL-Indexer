#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bptree.h"
#include "storage.h"

struct Database {
    TableSchema schema;
    UserRow *rows;
    size_t row_count;
    size_t capacity;
    int next_id;
    BPlusTree primary_index;
};

static Database *g_database;
static const char *USERS_TABLE = "users";
static const char *USERS_SCHEMA_PATH = "users.schema";
static const char *USERS_DATA_PATH = "users.data";

/* 테이블 이름과 확장자로 파일 경로를 만든다. */
static void make_filename(const char *table_name, const char *ext, char *path, size_t size) {
    snprintf(path, size, "%s.%s", table_name, ext);
}

/* 스키마 메타데이터를 데이터베이스 소유 구조체로 복사한다. */
static int copy_schema(TableSchema *destination, const TableSchema *source) {
    if (source == NULL || destination == NULL) {
        return 0;
    }
    memcpy(destination, source, sizeof(TableSchema));
    return 1;
}

/* 필요한 개수만큼 담을 수 있도록 메모리 버퍼를 확장한다. */
static int ensure_row_capacity(Database *database, size_t required_capacity) {
    UserRow *resized_rows;
    size_t new_capacity = database->capacity == 0 ? 128 : database->capacity;

    while (new_capacity < required_capacity) {
        new_capacity *= 2;
    }

    resized_rows = (UserRow *)realloc(database->rows, new_capacity * sizeof(UserRow));
    if (resized_rows == NULL) {
        return 0;
    }

    database->rows = resized_rows;
    database->capacity = new_capacity;
    return 1;
}

/* 조회 결과 버퍼에 행 포인터를 추가한다. */
static int query_result_append(QueryResult *result, const UserRow *row) {
    const UserRow **resized_rows;
    size_t new_capacity;

    if (result->row_count == result->capacity) {
        new_capacity = result->capacity == 0 ? 8 : result->capacity * 2;
        resized_rows = (const UserRow **)realloc(result->rows, new_capacity * sizeof(UserRow *));
        if (resized_rows == NULL) {
            return 0;
        }
        result->rows = resized_rows;
        result->capacity = new_capacity;
    }

    result->rows[result->row_count++] = row;
    return 1;
}

/* 스키마 파일에서 지원하는 테이블 정의를 읽어온다. */
static int read_schema_file(const char *table_name, TableSchema *schema) {
    FILE *file;
    char path[64];
    char line[64];

    if (strcmp(table_name, USERS_TABLE) != 0) {
        return 0;
    }

    make_filename(table_name, "schema", path, sizeof(path));
    file = fopen(path, "r");
    if (file == NULL) {
        return 0;
    }

    schema->column_count = 0;
    while (fgets(line, sizeof(line), file) != NULL && schema->column_count < MAX_COLUMNS) {
        if (sscanf(line,
                   "%31[^|]|%15s",
                   schema->names[schema->column_count],
                   schema->types[schema->column_count]) == 2) {
            schema->column_count++;
        }
    }

    fclose(file);
    return schema->column_count > 0;
}

/* 벤치마크 구간의 경과 시간을 초 단위로 계산한다. */
static double elapsed_seconds(const struct timespec *start, const struct timespec *end) {
    double seconds = (double)(end->tv_sec - start->tv_sec);
    double nanoseconds = (double)(end->tv_nsec - start->tv_nsec) / 1000000000.0;

    return seconds + nanoseconds;
}

static int matches_numeric_condition(int actual_value, ConditionType condition_type, int expected_value) {
    if (condition_type == CONDITION_ID_EQ || condition_type == CONDITION_AGE_EQ) {
        return actual_value == expected_value;
    }
    if (condition_type == CONDITION_ID_LT || condition_type == CONDITION_AGE_LT) {
        return actual_value < expected_value;
    }
    if (condition_type == CONDITION_ID_LTE || condition_type == CONDITION_AGE_LTE) {
        return actual_value <= expected_value;
    }
    if (condition_type == CONDITION_ID_GT || condition_type == CONDITION_AGE_GT) {
        return actual_value > expected_value;
    }
    if (condition_type == CONDITION_ID_GTE || condition_type == CONDITION_AGE_GTE) {
        return actual_value >= expected_value;
    }

    return 0;
}

/* 초기화된 B+ 트리를 포함한 빈 메모리 데이터베이스를 생성한다. */
Database *database_create_empty(const TableSchema *schema) {
    Database *database = (Database *)calloc(1, sizeof(Database));

    if (database == NULL) {
        return NULL;
    }
    copy_schema(&database->schema, schema);
    database->next_id = 1;
    bptree_init(&database->primary_index);
    return database;
}

/* 데이터베이스 메모리와 인덱스 상태를 모두 해제한다. */
void database_destroy(Database *database) {
    if (database == NULL) {
        return;
    }
    free(database->rows);
    bptree_destroy(&database->primary_index);
    free(database);
}

/* 현재 적재된 행 수를 반환한다. */
size_t database_row_count(const Database *database) {
    return database == NULL ? 0 : database->row_count;
}

/* 다음 자동 증가 id 값을 반환한다. */
int database_next_id(const Database *database) {
    return database == NULL ? 1 : database->next_id;
}

/* 기본 키 B+ 트리의 현재 높이를 반환한다. */
int database_index_height(const Database *database) {
    return database == NULL ? 0 : bptree_height(&database->primary_index);
}

/* 사용자 행 하나를 메모리에 추가하고 필요하면 파일에도 기록한다. */
int database_append_user(Database *database,
                         int has_explicit_id,
                         int requested_id,
                         const char *name,
                         int age,
                         int persist_to_disk,
                         const char *data_path,
                         int *assigned_id) {
    FILE *file = NULL;
    int row_index;
    int next_id;
    int existing_row;

    if (database == NULL || name == NULL || age < 0) {
        return 0;
    }

    next_id = has_explicit_id ? requested_id : database->next_id;
    if (next_id <= 0) {
        return 0;
    }
    if (bptree_search(&database->primary_index, next_id, &existing_row)) {
        return 0;
    }

    if (persist_to_disk) {
        file = fopen(data_path, "a");
        if (file == NULL) {
            return 0;
        }
    }

    if (!ensure_row_capacity(database, database->row_count + 1)) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }

    row_index = (int)database->row_count;
    database->rows[row_index].id = next_id;
    snprintf(database->rows[row_index].name, sizeof(database->rows[row_index].name), "%s", name);
    database->rows[row_index].age = age;

    if (!bptree_insert(&database->primary_index, next_id, row_index)) {
        if (file != NULL) {
            fclose(file);
        }
        return 0;
    }

    database->row_count++;
    if (next_id >= database->next_id) {
        database->next_id = next_id + 1;
    }
    if (assigned_id != NULL) {
        *assigned_id = next_id;
    }

    if (file != NULL) {
        if (fprintf(file, "%d|%s|%d\n", next_id, database->rows[row_index].name, age) < 0) {
            fclose(file);
            return 0;
        }
        fclose(file);
    }

    return 1;
}

/* 데이터 파일의 저장된 행들을 메모리로 다시 적재한다. */
int database_load_from_file(Database *database, const char *data_path) {
    FILE *file;
    char line[128];
    int id;
    char name[32];
    int age;

    if (database == NULL) {
        return 0;
    }

    file = fopen(data_path, "r");
    if (file == NULL) {
        return 1;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        if (sscanf(line, "%d|%31[^|]|%d", &id, name, &age) != 3) {
            fclose(file);
            return 0;
        }
        if (!database_append_user(database, 1, id, name, age, 0, NULL, NULL)) {
            fclose(file);
            return 0;
        }
    }

    fclose(file);
    return 1;
}

/* 조건에 따라 인덱스를 활용해 SELECT 조회를 수행한다. */
int database_select_users(const Database *database, const Query *query, QueryResult *result) {
    size_t i;
    int row_index;
    int *row_indices = NULL;
    size_t row_count = 0;

    if (database == NULL || query == NULL || result == NULL) {
        return 0;
    }

    memset(result, 0, sizeof(QueryResult));

    if (query->condition_type == CONDITION_ID_EQ) {
        result->used_index = 1;
        if (bptree_search(&database->primary_index, query->condition_int_value, &row_index)) {
            return query_result_append(result, &database->rows[row_index]);
        }
        return 1;
    }

    if (query->condition_type == CONDITION_ID_RANGE) {
        result->used_index = 1;
        if (!bptree_range_search(&database->primary_index,
                                 query->condition_int_value,
                                 query->condition_second_int_value,
                                 &row_indices,
                                 &row_count)) {
            return 0;
        }
        for (i = 0; i < row_count; i++) {
            if (!query_result_append(result, &database->rows[row_indices[i]])) {
                free(row_indices);
                free_query_result(result);
                return 0;
            }
        }
        free(row_indices);
        return 1;
    }

    for (i = 0; i < database->row_count; i++) {
        int matches = 0;

        if (query->condition_type == CONDITION_NONE) {
            matches = 1;
        } else if (query->condition_type == CONDITION_ID_EQ ||
                   query->condition_type == CONDITION_ID_LT ||
                   query->condition_type == CONDITION_ID_LTE ||
                   query->condition_type == CONDITION_ID_GT ||
                   query->condition_type == CONDITION_ID_GTE) {
            matches = matches_numeric_condition(database->rows[i].id,
                                               query->condition_type,
                                               query->condition_int_value);
        } else if (query->condition_type == CONDITION_NAME_EQ) {
            matches = strcmp(database->rows[i].name, query->condition_text_value) == 0;
        } else if (query->condition_type == CONDITION_AGE_EQ ||
                   query->condition_type == CONDITION_AGE_LT ||
                   query->condition_type == CONDITION_AGE_LTE ||
                   query->condition_type == CONDITION_AGE_GT ||
                   query->condition_type == CONDITION_AGE_GTE) {
            matches = matches_numeric_condition(database->rows[i].age,
                                               query->condition_type,
                                               query->condition_int_value);
        }

        if (matches && !query_result_append(result, &database->rows[i])) {
            free_query_result(result);
            return 0;
        }
    }

    return 1;
}

/* 스키마와 데이터 파일로 전역 저장소를 초기화한다. */
int storage_init(void) {
    TableSchema schema;

    if (g_database != NULL) {
        return 1;
    }
    if (!read_schema_file(USERS_TABLE, &schema)) {
        return 0;
    }

    g_database = database_create_empty(&schema);
    if (g_database == NULL) {
        return 0;
    }
    if (!database_load_from_file(g_database, USERS_DATA_PATH)) {
        storage_shutdown();
        return 0;
    }
    return 1;
}

/* 전역 저장소 인스턴스를 정리한다. */
void storage_shutdown(void) {
    database_destroy(g_database);
    g_database = NULL;
}

/* 요청한 테이블의 스키마 메타데이터를 읽어온다. */
int load_schema(const char *table_name, TableSchema *schema) {
    return read_schema_file(table_name, schema);
}

/* 전역 저장소를 통해 사용자 행을 추가한다. */
int append_user(const Query *query, int *assigned_id) {
    if (g_database == NULL && !storage_init()) {
        return 0;
    }
    return database_append_user(g_database,
                                query->insert_has_id,
                                query->id,
                                query->name,
                                query->age,
                                1,
                                USERS_DATA_PATH,
                                assigned_id);
}

/* 전역 저장소를 통해 SELECT 조회를 수행한다. */
int select_users(const Query *query, QueryResult *result) {
    if (g_database == NULL && !storage_init()) {
        return 0;
    }
    return database_select_users(g_database, query, result);
}

/* 조회 결과가 소유한 힙 메모리를 해제한다. */
void free_query_result(QueryResult *result) {
    if (result == NULL) {
        return;
    }
    free(result->rows);
    memset(result, 0, sizeof(QueryResult));
}

/* 스키마 파일이 존재하는 테이블 목록을 출력한다. */
void list_tables(void) {
    FILE *file = fopen(USERS_SCHEMA_PATH, "r");

    if (file == NULL) {
        printf("no tables\n");
        return;
    }
    fclose(file);
    printf("users\n");
}

/* 지정한 테이블의 스키마 정의를 출력한다. */
int print_schema(const char *table_name) {
    TableSchema schema;
    int i;

    if (!load_schema(table_name, &schema)) {
        return 0;
    }
    for (i = 0; i < schema.column_count; i++) {
        printf("%s | %s\n", schema.names[i], schema.types[i]);
    }
    return 1;
}

/* 현재 메모리 저장소의 요약 정보를 출력한다. */
void print_storage_stats(void) {
    if (g_database == NULL && !storage_init()) {
        printf("storage init failed\n");
        return;
    }

    printf("table: users\n");
    printf("rows: %zu\n", database_row_count(g_database));
    printf("next_id: %d\n", database_next_id(g_database));
    printf("b+tree_height: %d\n", database_index_height(g_database));
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
    if (!read_schema_file(USERS_TABLE, &schema)) {
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
    strcpy(query.table_name, USERS_TABLE);
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
