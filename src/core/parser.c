#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

static int parse_integer_comparison(const char *text,
                                    const char *column_name,
                                    int *out_value,
                                    ConditionType equal_type,
                                    ConditionType less_type,
                                    ConditionType less_equal_type,
                                    ConditionType greater_type,
                                    ConditionType greater_equal_type) {
    char parsed_column[32];
    char parsed_operator[3] = {0};
    const char *cursor = text;
    char *value_end;
    long parsed_value;
    size_t column_length = 0;

    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    while (*cursor &&
           !isspace((unsigned char)*cursor) &&
           *cursor != '=' &&
           *cursor != '<' &&
           *cursor != '>') {
        if (column_length + 1 >= sizeof(parsed_column)) {
            return 0;
        }
        parsed_column[column_length++] = *cursor++;
    }
    parsed_column[column_length] = '\0';
    if (column_length == 0 || strcmp(parsed_column, column_name) != 0) {
        return 0;
    }

    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }
    if (*cursor != '=' && *cursor != '<' && *cursor != '>') {
        return 0;
    }

    parsed_operator[0] = *cursor++;
    if (*cursor == '=' && (parsed_operator[0] == '<' || parsed_operator[0] == '>')) {
        parsed_operator[1] = *cursor++;
    }

    while (*cursor && isspace((unsigned char)*cursor)) {
        cursor++;
    }

    parsed_value = strtol(cursor, &value_end, 10);
    if (cursor == value_end) {
        return 0;
    }
    while (*value_end && isspace((unsigned char)*value_end)) {
        value_end++;
    }
    if (*value_end != '\0') {
        return 0;
    }

    *out_value = (int)parsed_value;
    if (strcmp(parsed_operator, "=") == 0) {
        return equal_type;
    }
    if (strcmp(parsed_operator, "<") == 0) {
        return less_type;
    }
    if (strcmp(parsed_operator, "<=") == 0) {
        return less_equal_type;
    }
    if (strcmp(parsed_operator, ">") == 0) {
        return greater_type;
    }
    if (strcmp(parsed_operator, ">=") == 0) {
        return greater_equal_type;
    }

    return 0;
}

/* 앞뒤 공백을 정리하고 끝의 세미콜론을 제거한다. */
static void trim_sql(char *text) {
    int start = 0;
    int end = (int)strlen(text) - 1;

    while (text[start] && isspace((unsigned char)text[start])) {
        start++;
    }
    while (end >= start && isspace((unsigned char)text[end])) {
        text[end--] = '\0';
    }
    if (end >= start && text[end] == ';') {
        text[end--] = '\0';
    }
    while (end >= start && isspace((unsigned char)text[end])) {
        text[end--] = '\0';
    }
    if (start > 0) {
        memmove(text, text + start, strlen(text + start) + 1);
    }
}

/* 토큰의 앞뒤 공백을 제자리에서 제거한다. */
static void trim_token(char *text) {
    char *start = text;
    char *end;

    while (*start && isspace((unsigned char)*start)) {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    if (*text == '\0') {
        return;
    }

    end = text + strlen(text) - 1;
    while (end >= text && isspace((unsigned char)*end)) {
        *end-- = '\0';
    }
}

/* SELECT 컬럼 목록을 파싱해 Query 구조체에 채운다. */
static int parse_selected_columns(char *columns, Query *query) {
    char *token;

    query->selected_column_count = 0;
    token = strtok(columns, ",");
    while (token != NULL) {
        if (query->selected_column_count >= 3) {
            return 0;
        }
        trim_token(token);
        if (strcmp(token, "id") != 0 &&
            strcmp(token, "name") != 0 &&
            strcmp(token, "age") != 0) {
            return 0;
        }
        strcpy(query->selected_columns[query->selected_column_count], token);
        query->selected_column_count++;
        token = strtok(NULL, ",");
    }

    return query->selected_column_count > 0;
}

/* users 테이블에서 지원하는 WHERE 조건을 파싱한다. */
static int parse_where_clause(char *where_clause, Query *query) {
    int start_id;
    int end_id;
    int condition_type;

    trim_token(where_clause);
    if (sscanf(where_clause, "id BETWEEN %d AND %d", &start_id, &end_id) == 2) {
        if (start_id > end_id) {
            return 0;
        }
        query->condition_type = CONDITION_ID_RANGE;
        query->condition_int_value = start_id;
        query->condition_second_int_value = end_id;
        return 1;
    }
    condition_type = parse_integer_comparison(where_clause,
                                              "id",
                                              &query->condition_int_value,
                                              CONDITION_ID_EQ,
                                              CONDITION_ID_LT,
                                              CONDITION_ID_LTE,
                                              CONDITION_ID_GT,
                                              CONDITION_ID_GTE);
    if (condition_type != 0) {
        query->condition_type = (ConditionType)condition_type;
        return 1;
    }
    condition_type = parse_integer_comparison(where_clause,
                                              "age",
                                              &query->condition_int_value,
                                              CONDITION_AGE_EQ,
                                              CONDITION_AGE_LT,
                                              CONDITION_AGE_LTE,
                                              CONDITION_AGE_GT,
                                              CONDITION_AGE_GTE);
    if (condition_type != 0) {
        query->condition_type = (ConditionType)condition_type;
        return 1;
    }
    if (sscanf(where_clause, "name = '%31[^']'", query->condition_text_value) == 1) {
        query->condition_type = CONDITION_NAME_EQ;
        return 1;
    }

    return 0;
}

/* id 유무에 따라 INSERT 구문을 파싱한다. */
static int parse_insert(char *sql, Query *query) {
    query->type = QUERY_INSERT;

    if (sscanf(sql, "INSERT INTO %31s VALUES ( %d , '%31[^']' , %d )",
               query->table_name,
               &query->id,
               query->name,
               &query->age) == 4) {
        query->insert_has_id = 1;
        return strcmp(query->table_name, "users") == 0;
    }

    if (sscanf(sql, "INSERT INTO %31s VALUES ( '%31[^']' , %d )",
               query->table_name,
               query->name,
               &query->age) == 3) {
        query->insert_has_id = 0;
        return strcmp(query->table_name, "users") == 0;
    }

    return 0;
}

/* SELECT 구문과 선택 컬럼, 선택적 필터를 파싱한다. */
static int parse_select(char *sql, Query *query) {
    char *from_pos;
    char *where_pos;
    char columns[96];
    char table_name[32];

    query->type = QUERY_SELECT;
    query->condition_type = CONDITION_NONE;

    from_pos = strstr(sql, " FROM ");
    if (from_pos == NULL) {
        return 0;
    }
    *from_pos = '\0';
    strncpy(columns, sql + 7, sizeof(columns) - 1);
    columns[sizeof(columns) - 1] = '\0';

    where_pos = strstr(from_pos + 6, " WHERE ");
    if (where_pos != NULL) {
        *where_pos = '\0';
    }

    strncpy(table_name, from_pos + 6, sizeof(table_name) - 1);
    table_name[sizeof(table_name) - 1] = '\0';
    trim_token(table_name);
    if (strcmp(table_name, "users") != 0) {
        return 0;
    }
    strcpy(query->table_name, table_name);

    trim_token(columns);
    if (strcmp(columns, "*") == 0) {
        query->select_all = 1;
    } else if (!parse_selected_columns(columns, query)) {
        return 0;
    }

    if (where_pos != NULL && !parse_where_clause(where_pos + 7, query)) {
        return 0;
    }

    return 1;
}

/* 원시 SQL 문자열을 내부 Query 표현으로 변환한다. */
int parse_query(const char *sql, Query *query) {
    char buffer[256];

    memset(query, 0, sizeof(Query));
    strncpy(buffer, sql, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';

    trim_sql(buffer);

    if (strncmp(buffer, "INSERT INTO ", 12) == 0) {
        return parse_insert(buffer, query);
    }
    if (strncmp(buffer, "SELECT ", 7) == 0) {
        return parse_select(buffer, query);
    }

    return 0;
}
