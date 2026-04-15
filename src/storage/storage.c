#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "storage_internal.h"

static Database *g_database;

/* 필요 시 전역 저장소를 초기화한 뒤 인스턴스를 반환한다. */
static Database *ensure_database(void) {
    if (g_database == NULL && !storage_init()) {
        return NULL;
    }
    return g_database;
}

/* 스키마와 데이터 파일로 전역 저장소를 초기화한다. */
int storage_init(void) {
    TableSchema schema;

    if (g_database != NULL) {
        return 1;
    }
    if (!storage_read_schema_file(STORAGE_TABLE_NAME, &schema)) {
        return 0;
    }

    g_database = database_create_empty(&schema);
    if (g_database == NULL) {
        return 0;
    }
    if (!database_load_from_file(g_database, STORAGE_DATA_PATH)) {
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
    return storage_read_schema_file(table_name, schema);
}

/* 전역 저장소를 통해 사용자 행을 추가한다. */
int append_user(const Query *query, int *assigned_id) {
    Database *database = ensure_database();

    if (database == NULL) {
        return 0;
    }
    return database_append_user(database,
                                query->insert_has_id,
                                query->id,
                                query->name,
                                query->age,
                                1,
                                STORAGE_DATA_PATH,
                                assigned_id);
}

/* 전역 저장소를 통해 SELECT 조회를 수행한다. */
int select_users(const Query *query, QueryResult *result) {
    Database *database = ensure_database();

    if (database == NULL) {
        return 0;
    }
    return database_select_users(database, query, result);
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
    TableSchema schema;

    if (!load_schema(STORAGE_TABLE_NAME, &schema)) {
        printf("no tables\n");
        return;
    }
    printf("%s\n", STORAGE_TABLE_NAME);
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
    Database *database = ensure_database();

    if (database == NULL) {
        printf("storage init failed\n");
        return;
    }

    printf("table: %s\n", STORAGE_TABLE_NAME);
    printf("rows: %zu\n", database_row_count(database));
    printf("next_id: %d\n", database_next_id(database));
    printf("b+tree_height: %d\n", database_index_height(database));
}
