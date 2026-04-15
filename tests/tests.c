#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "parser.h"
#include "storage.h"

/* 저장소 테스트에 사용할 users 스키마를 만든다. */
static TableSchema build_test_schema(void) {
    TableSchema schema;

    memset(&schema, 0, sizeof(schema));
    schema.column_count = 3;
    strcpy(schema.names[0], "id");
    strcpy(schema.types[0], "INT");
    strcpy(schema.names[1], "name");
    strcpy(schema.types[1], "TEXT");
    strcpy(schema.names[2], "age");
    strcpy(schema.types[2], "INT");
    return schema;
}

/* id 자동 할당 INSERT 파싱을 검증한다. */
static void test_parse_insert_with_auto_id(void) {
    Query query;

    assert(parse_query("INSERT INTO users VALUES ('alice', 23);", &query));
    assert(query.type == QUERY_INSERT);
    assert(query.insert_has_id == 0);
    assert(strcmp(query.name, "alice") == 0);
    assert(query.age == 23);
}

/* id를 직접 지정한 INSERT 파싱을 검증한다. */
static void test_parse_insert_with_explicit_id(void) {
    Query query;

    assert(parse_query("INSERT INTO users VALUES (7, 'alice', 23);", &query));
    assert(query.insert_has_id == 1);
    assert(query.id == 7);
}

/* SELECT에서 지원하는 WHERE 조건 파싱을 검증한다. */
static void test_parse_select_conditions(void) {
    Query query;

    assert(parse_query("SELECT * FROM users WHERE id = 7;", &query));
    assert(query.condition_type == CONDITION_ID_EQ);
    assert(query.condition_int_value == 7);

    assert(parse_query("SELECT * FROM users WHERE id=7;", &query));
    assert(query.condition_type == CONDITION_ID_EQ);
    assert(query.condition_int_value == 7);

    assert(parse_query("SELECT * FROM users WHERE id =7;", &query));
    assert(query.condition_type == CONDITION_ID_EQ);
    assert(query.condition_int_value == 7);

    assert(parse_query("SELECT * FROM users WHERE id= 7;", &query));
    assert(query.condition_type == CONDITION_ID_EQ);
    assert(query.condition_int_value == 7);

    assert(parse_query("SELECT id, name FROM users WHERE name = 'alice';", &query));
    assert(query.condition_type == CONDITION_NAME_EQ);
    assert(strcmp(query.condition_text_value, "alice") == 0);

    assert(parse_query("SELECT * FROM users WHERE id BETWEEN 3 AND 9;", &query));
    assert(query.condition_type == CONDITION_ID_RANGE);
    assert(query.condition_int_value == 3);
    assert(query.condition_second_int_value == 9);

    assert(parse_query("SELECT * FROM users WHERE id >= 7;", &query));
    assert(query.condition_type == CONDITION_ID_GTE);
    assert(query.condition_int_value == 7);

    assert(parse_query("SELECT * FROM users WHERE id>=7;", &query));
    assert(query.condition_type == CONDITION_ID_GTE);
    assert(query.condition_int_value == 7);

    assert(parse_query("SELECT * FROM users WHERE age < 30;", &query));
    assert(query.condition_type == CONDITION_AGE_LT);
    assert(query.condition_int_value == 30);
}

/* 인덱스 조회, 범위 조회, 선형 스캔 동작을 확인한다. */
static void test_database_index_and_linear_scan(void) {
    TableSchema schema = build_test_schema();
    Database *database = database_create_empty(&schema);
    Query query;
    QueryResult result;
    int assigned_id;
    int i;
    char name[32];

    assert(database != NULL);
    for (i = 0; i < 256; i++) {
        snprintf(name, sizeof(name), "user_%03d", i + 1);
        assert(database_append_user(database, 0, 0, name, 20 + (i % 10), 0, NULL, &assigned_id));
        assert(assigned_id == i + 1);
    }

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_EQ;
    query.condition_int_value = 120;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 1);
    assert(result.rows[0]->id == 120);
    free_query_result(&result);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_NAME_EQ;
    strcpy(query.condition_text_value, "user_120");
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 0);
    assert(result.row_count == 1);
    assert(strcmp(result.rows[0]->name, "user_120") == 0);
    free_query_result(&result);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_RANGE;
    query.condition_int_value = 100;
    query.condition_second_int_value = 105;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 6);
    assert(result.rows[0]->id == 100);
    assert(result.rows[5]->id == 105);
    free_query_result(&result);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_LT;
    query.condition_int_value = 5;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 4);
    assert(result.rows[0]->id == 1);
    assert(result.rows[3]->id == 4);
    free_query_result(&result);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_LTE;
    query.condition_int_value = 3;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 3);
    assert(result.rows[0]->id == 1);
    assert(result.rows[2]->id == 3);
    free_query_result(&result);

    assert(database_next_id(database) == 257);
    assert(database_index_height(database) >= 2);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_GT;
    query.condition_int_value = 250;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 6);
    assert(result.rows[0]->id == 251);
    assert(result.rows[5]->id == 256);
    free_query_result(&result);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_GTE;
    query.condition_int_value = 253;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 4);
    assert(result.rows[0]->id == 253);
    assert(result.rows[3]->id == 256);
    free_query_result(&result);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_AGE_LTE;
    query.condition_int_value = 22;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 0);
    assert(result.row_count == 78);
    free_query_result(&result);

    database_destroy(database);
}

/* 높이가 3 이상인 큰 인덱스에서도 id 조회가 정확한지 검증한다. */
static void test_database_large_index_lookup(void) {
    TableSchema schema = build_test_schema();
    Database *database = database_create_empty(&schema);
    Query query;
    QueryResult result;
    int assigned_id;
    int i;
    char name[32];
    const int lookup_ids[] = {10, 50000, 99999};

    assert(database != NULL);
    for (i = 0; i < 100000; i++) {
        snprintf(name, sizeof(name), "user%06d", i + 1);
        assert(database_append_user(database, 0, 0, name, 20 + (i % 43), 0, NULL, &assigned_id));
        assert(assigned_id == i + 1);
    }

    assert(database_index_height(database) >= 3);

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_EQ;

    for (i = 0; i < 3; i++) {
        query.condition_int_value = lookup_ids[i];
        assert(database_select_users(database, &query, &result));
        assert(result.used_index == 1);
        assert(result.row_count == 1);
        assert(result.rows[0]->id == lookup_ids[i]);
        free_query_result(&result);
    }

    memset(&query, 0, sizeof(query));
    query.type = QUERY_SELECT;
    query.select_all = 1;
    query.condition_type = CONDITION_ID_RANGE;
    query.condition_int_value = 10;
    query.condition_second_int_value = 20;
    assert(database_select_users(database, &query, &result));
    assert(result.used_index == 1);
    assert(result.row_count == 11);
    assert(result.rows[0]->id == 10);
    assert(result.rows[10]->id == 20);
    free_query_result(&result);

    database_destroy(database);
}

/* 파서와 저장소에 대한 단위 테스트를 실행한다. */
int main(void) {
    test_parse_insert_with_auto_id();
    test_parse_insert_with_explicit_id();
    test_parse_select_conditions();
    test_database_index_and_linear_scan();
    test_database_large_index_lookup();
    puts("all tests passed");
    return 0;
}
