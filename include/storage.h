#ifndef STORAGE_H
#define STORAGE_H

#include <stddef.h>
#include "query.h"

#define MAX_COLUMNS 3
#define MAX_COLUMN_NAME_LENGTH 32
#define MAX_COLUMN_TYPE_LENGTH 16
#define MAX_VALUE_LENGTH 32

typedef struct {
    int column_count;
    char names[MAX_COLUMNS][MAX_COLUMN_NAME_LENGTH];
    char types[MAX_COLUMNS][MAX_COLUMN_TYPE_LENGTH];
} TableSchema;

typedef struct {
    int id;
    char name[MAX_VALUE_LENGTH];
    int age;
} UserRow;

typedef struct {
    const UserRow **rows;
    size_t row_count;
    size_t capacity;
    int used_index;
} QueryResult;

typedef struct Database Database;

/* 디스크 파일을 바탕으로 전역 저장소를 초기화한다. */
int storage_init(void);
/* 전역 저장소와 내부 버퍼를 해제한다. */
void storage_shutdown(void);
/* 지정한 테이블의 스키마 메타데이터를 읽어온다. */
int load_schema(const char *table_name, TableSchema *schema);
/* 파싱된 INSERT 정보를 이용해 사용자 행을 추가한다. */
int append_user(const Query *query, int *assigned_id);
/* 전역 저장소에 대해 SELECT 조회를 수행한다. */
int select_users(const Query *query, QueryResult *result);
/* QueryResult가 소유한 메모리를 해제한다. */
void free_query_result(QueryResult *result);
/* 사용 가능한 테이블 이름을 출력한다. */
void list_tables(void);
/* 지정한 테이블의 스키마 정의를 출력한다. */
int print_schema(const char *table_name);
/* users 테이블의 행 수와 인덱스 통계를 출력한다. */
void print_storage_stats(void);
/* 메모리 데이터베이스에서 삽입과 조회 성능을 측정한다. */
int run_benchmark(size_t record_count, size_t lookup_count);

/* 주어진 스키마로 독립적인 메모리 데이터베이스를 생성한다. */
Database *database_create_empty(const TableSchema *schema);
/* 메모리 데이터베이스 인스턴스를 해제한다. */
void database_destroy(Database *database);
/* 저장된 행들을 메모리 데이터베이스로 적재한다. */
int database_load_from_file(Database *database, const char *data_path);
/* 메모리 데이터베이스에 행 하나를 추가하고 필요하면 저장한다. */
int database_append_user(Database *database,
                         int has_explicit_id,
                         int requested_id,
                         const char *name,
                         int age,
                         int persist_to_disk,
                         const char *data_path,
                         int *assigned_id);
/* 메모리 데이터베이스 인스턴스에 대해 SELECT 조회를 수행한다. */
int database_select_users(const Database *database, const Query *query, QueryResult *result);
/* 데이터베이스에 저장된 행 수를 반환한다. */
size_t database_row_count(const Database *database);
/* 다음 자동 생성 id 값을 반환한다. */
int database_next_id(const Database *database);
/* 기본 키 인덱스 트리의 현재 높이를 반환한다. */
int database_index_height(const Database *database);

#endif
