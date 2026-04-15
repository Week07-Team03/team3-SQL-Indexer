#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "bptree.h"
#include "storage_internal.h"

struct Database {
    TableSchema schema;
    UserRow *rows;
    size_t row_count;
    size_t capacity;
    int next_id;
    BPlusTree primary_index;
};

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
        resized_rows = (const UserRow **)realloc(result->rows,
                                                 new_capacity * sizeof(*result->rows));
        if (resized_rows == NULL) {
            return 0;
        }
        result->rows = resized_rows;
        result->capacity = new_capacity;
    }

    result->rows[result->row_count++] = row;
    return 1;
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

static void get_query_condition(const Query *query, int index, QueryCondition *condition) {
    if (query->condition_count > 0) {
        *condition = query->conditions[index];
        return;
    }

    memset(condition, 0, sizeof(*condition));
    condition->type = query->condition_type;
    condition->int_value = query->condition_int_value;
    condition->second_int_value = query->condition_second_int_value;
    snprintf(condition->text_value, sizeof(condition->text_value), "%s", query->condition_text_value);
}

static int query_condition_count(const Query *query) {
    if (query->condition_count > 0) {
        return query->condition_count;
    }
    return query->condition_type == CONDITION_NONE ? 0 : 1;
}

/* 주어진 조건이 특정 row에 일치하는지 판단한다. */
static int row_matches_condition(const UserRow *row, const QueryCondition *condition) {
    switch (condition->type) {
        case CONDITION_NONE:
            return 1;
        case CONDITION_ID_EQ:
        case CONDITION_ID_LT:
        case CONDITION_ID_LTE:
        case CONDITION_ID_GT:
        case CONDITION_ID_GTE:
            return matches_numeric_condition(row->id,
                                             condition->type,
                                             condition->int_value);
        case CONDITION_NAME_EQ:
            return strcmp(row->name, condition->text_value) == 0;
        case CONDITION_AGE_EQ:
        case CONDITION_AGE_LT:
        case CONDITION_AGE_LTE:
        case CONDITION_AGE_GT:
        case CONDITION_AGE_GTE:
            return matches_numeric_condition(row->age,
                                             condition->type,
                                             condition->int_value);
        case CONDITION_ID_RANGE:
            return row->id >= condition->int_value &&
                   row->id <= condition->second_int_value;
    }

    return 0;
}

static int row_matches_query(const UserRow *row, const Query *query) {
    QueryCondition condition;
    int condition_count = query_condition_count(query);
    int result;
    int group_result;
    int i;

    if (condition_count == 0) {
        return 1;
    }

    get_query_condition(query, 0, &condition);
    group_result = row_matches_condition(row, &condition);
    result = 0;

    for (i = 1; i < condition_count; i++) {
        get_query_condition(query, i, &condition);
        if (query->condition_operators[i - 1] == LOGICAL_AND) {
            group_result = group_result && row_matches_condition(row, &condition);
        } else {
            result = result || group_result;
            group_result = row_matches_condition(row, &condition);
        }
    }
    return result || group_result;
}

static int condition_uses_id_index(const QueryCondition *condition) {
    return condition->type == CONDITION_ID_EQ ||
           condition->type == CONDITION_ID_LT ||
           condition->type == CONDITION_ID_LTE ||
           condition->type == CONDITION_ID_GT ||
           condition->type == CONDITION_ID_GTE ||
           condition->type == CONDITION_ID_RANGE;
}

static int find_index_condition(const Query *query, QueryCondition *condition) {
    QueryCondition current;
    int condition_count = query_condition_count(query);
    int found = 0;
    int i;

    for (i = 0; i < condition_count; i++) {
        if (i > 0 && query->condition_operators[i - 1] != LOGICAL_AND) {
            return 0;
        }
        get_query_condition(query, i, &current);
        if (!found && condition_uses_id_index(&current)) {
            *condition = current;
            found = 1;
        }
    }
    return found;
}

static int id_condition_bounds(const QueryCondition *condition, int *start_id, int *end_id) {
    switch (condition->type) {
        case CONDITION_ID_EQ:
            *start_id = condition->int_value;
            *end_id = condition->int_value;
            return 1;
        case CONDITION_ID_RANGE:
            *start_id = condition->int_value;
            *end_id = condition->second_int_value;
            return 1;
        case CONDITION_ID_LT:
            *start_id = 1;
            *end_id = condition->int_value <= 1 ? 0 : condition->int_value - 1;
            return 1;
        case CONDITION_ID_LTE:
            *start_id = 1;
            *end_id = condition->int_value < 1 ? 0 : condition->int_value;
            return 1;
        case CONDITION_ID_GT:
            if (condition->int_value == INT_MAX) {
                *start_id = 1;
                *end_id = 0;
            } else {
                *start_id = condition->int_value < 1 ? 1 : condition->int_value + 1;
                *end_id = INT_MAX;
            }
            return 1;
        case CONDITION_ID_GTE:
            *start_id = condition->int_value <= 1 ? 1 : condition->int_value;
            *end_id = INT_MAX;
            return 1;
        default:
            return 0;
    }
}

/* id 범위 조건을 B+ 트리로 조회해 결과 버퍼에 추가한다. */
static int append_index_range(const Database *database,
                              const Query *query,
                              QueryResult *result,
                              int start_id,
                              int end_id) {
    int *row_indices = NULL;
    size_t row_count = 0;
    size_t i;

    result->used_index = 1;
    if (start_id > end_id) {
        return 1;
    }
    if (!bptree_range_search(&database->primary_index, start_id, end_id, &row_indices, &row_count)) {
        return 0;
    }
    for (i = 0; i < row_count; i++) {
        const UserRow *row = &database->rows[row_indices[i]];

        if (row_matches_query(row, query) && !query_result_append(result, row)) {
            free(row_indices);
            free_query_result(result);
            return 0;
        }
    }
    free(row_indices);
    return 1;
}

/* 초기화된 B+ 트리를 포함한 빈 메모리 데이터베이스를 생성한다. */
Database *database_create_empty(const TableSchema *schema) {
    Database *database;

    if (schema == NULL) {
        return NULL;
    }

    database = (Database *)calloc(1, sizeof(Database));
    if (database == NULL) {
        return NULL;
    }

    database->schema = *schema;
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
    if (!ensure_row_capacity(database, database->row_count + 1)) {
        return 0;
    }

    if (persist_to_disk) {
        if (data_path == NULL) {
            return 0;
        }
        file = fopen(data_path, "a");
        if (file == NULL) {
            return 0;
        }
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

    if (database == NULL || data_path == NULL) {
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
    QueryCondition index_condition;
    int start_id;
    int end_id;

    if (database == NULL || query == NULL || result == NULL) {
        return 0;
    }

    memset(result, 0, sizeof(QueryResult));

    if (find_index_condition(query, &index_condition) &&
        id_condition_bounds(&index_condition, &start_id, &end_id)) {
        return append_index_range(database, query, result, start_id, end_id);
    }

    for (i = 0; i < database->row_count; i++) {
        if (row_matches_query(&database->rows[i], query) &&
            !query_result_append(result, &database->rows[i])) {
            free_query_result(result);
            return 0;
        }
    }

    return 1;
}
