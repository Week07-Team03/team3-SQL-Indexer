#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "parser.h"

typedef enum {
    OPERAND_COLUMN,
    OPERAND_INT,
    OPERAND_TEXT
} OperandKind;

typedef struct {
    OperandKind kind;
    char column[32];
    int int_value;
    char text_value[32];
} Operand;

typedef struct {
    const char *text;
    ConditionType id_type;
    ConditionType age_type;
} OperatorType;

static char parser_error[128] = "syntax error";

static const OperatorType OPERATOR_TYPES[] = {
    {"=", CONDITION_ID_EQ, CONDITION_AGE_EQ},
    {"<", CONDITION_ID_LT, CONDITION_AGE_LT},
    {"<=", CONDITION_ID_LTE, CONDITION_AGE_LTE},
    {">", CONDITION_ID_GT, CONDITION_AGE_GT},
    {">=", CONDITION_ID_GTE, CONDITION_AGE_GTE}
};

/* 마지막 파싱 실패 이유를 반환한다. */
const char *parser_get_error(void) {
    return parser_error;
}

static void set_parse_error(const char *message) {
    snprintf(parser_error, sizeof(parser_error), "%s", message);
}

static void skip_spaces(const char **cursor) {
    while (**cursor && isspace((unsigned char)**cursor)) {
        (*cursor)++;
    }
}

static int is_identifier_char(char value) {
    return isalnum((unsigned char)value) || value == '_';
}

static int equals_ci(const char *left, const char *right) {
    while (*left && *right) {
        if (toupper((unsigned char)*left) != toupper((unsigned char)*right)) {
            return 0;
        }
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static int word_equals(const char *word, size_t length, const char *expected) {
    size_t i;

    if (strlen(expected) != length) {
        return 0;
    }
    for (i = 0; i < length; i++) {
        if (toupper((unsigned char)word[i]) != toupper((unsigned char)expected[i])) {
            return 0;
        }
    }
    return 1;
}

static int copy_canonical_column(const char *column, char output[32]) {
    if (equals_ci(column, "id")) {
        strcpy(output, "id");
        return 1;
    }
    if (equals_ci(column, "name")) {
        strcpy(output, "name");
        return 1;
    }
    if (equals_ci(column, "age")) {
        strcpy(output, "age");
        return 1;
    }
    return 0;
}

static void uppercase_unquoted(char *text) {
    int in_quote = 0;

    while (*text) {
        if (*text == '\'') {
            in_quote = !in_quote;
        } else if (!in_quote) {
            *text = (char)toupper((unsigned char)*text);
        }
        text++;
    }
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

static char *find_keyword_token(char *text, const char *keyword) {
    size_t keyword_length = strlen(keyword);
    int in_quote = 0;
    char *cursor;

    for (cursor = text; *cursor; cursor++) {
        if (*cursor == '\'') {
            in_quote = !in_quote;
            continue;
        }
        if (in_quote) {
            continue;
        }
        if ((cursor == text || !is_identifier_char(cursor[-1])) &&
            strncmp(cursor, keyword, keyword_length) == 0 &&
            !is_identifier_char(cursor[keyword_length])) {
            return cursor;
        }
    }
    return NULL;
}

static int starts_with_keyword(const char *text, const char *keyword) {
    size_t keyword_length = strlen(keyword);

    return strncmp(text, keyword, keyword_length) == 0 &&
           !is_identifier_char(text[keyword_length]);
}

static int consume_keyword(const char **cursor, const char *keyword) {
    size_t keyword_length = strlen(keyword);
    const char *start;

    skip_spaces(cursor);
    start = *cursor;
    if (strncmp(start, keyword, keyword_length) != 0 ||
        is_identifier_char(start[keyword_length])) {
        return 0;
    }
    *cursor = start + keyword_length;
    return 1;
}

static int begins_integer(const char *text) {
    if (*text == '+' || *text == '-') {
        text++;
    }
    return isdigit((unsigned char)*text);
}

static int parse_operand(const char **cursor, Operand *operand) {
    const char *start;
    char *end;
    long parsed_value;
    size_t length;

    skip_spaces(cursor);
    start = *cursor;

    if (*start == '\'') {
        start++;
        end = strchr(start, '\'');
        if (end == NULL) {
            set_parse_error("unterminated text literal in WHERE");
            return 0;
        }
        length = (size_t)(end - start);
        if (length >= sizeof(operand->text_value)) {
            set_parse_error("text literal is too long");
            return 0;
        }
        operand->kind = OPERAND_TEXT;
        memcpy(operand->text_value, start, length);
        operand->text_value[length] = '\0';
        *cursor = end + 1;
        return 1;
    }

    if (begins_integer(start)) {
        errno = 0;
        parsed_value = strtol(start, &end, 10);
        if (errno == ERANGE || parsed_value < INT_MIN || parsed_value > INT_MAX) {
            set_parse_error("integer literal is out of range");
            return 0;
        }
        operand->kind = OPERAND_INT;
        operand->int_value = (int)parsed_value;
        *cursor = end;
        return 1;
    }

    if (isalpha((unsigned char)*start) || *start == '_') {
        length = 0;
        while (is_identifier_char(start[length])) {
            if (length + 1 >= sizeof(operand->column)) {
                set_parse_error("column name is too long");
                return 0;
            }
            operand->column[length] = start[length];
            length++;
        }
        operand->column[length] = '\0';
        operand->kind = OPERAND_COLUMN;
        *cursor = start + length;
        return 1;
    }

    set_parse_error("expected a column or value in WHERE");
    return 0;
}

static int parse_operator(const char **cursor, char operator_text[3]) {
    skip_spaces(cursor);
    if (**cursor != '=' && **cursor != '<' && **cursor != '>') {
        set_parse_error("expected a comparison operator in WHERE");
        return 0;
    }

    operator_text[0] = **cursor;
    operator_text[1] = '\0';
    operator_text[2] = '\0';
    (*cursor)++;
    if (**cursor == '=' && (operator_text[0] == '<' || operator_text[0] == '>')) {
        operator_text[1] = '=';
        (*cursor)++;
    }
    return 1;
}

static const char *reverse_operator(const char *operator_text) {
    if (strcmp(operator_text, "<") == 0) {
        return ">";
    }
    if (strcmp(operator_text, "<=") == 0) {
        return ">=";
    }
    if (strcmp(operator_text, ">") == 0) {
        return "<";
    }
    if (strcmp(operator_text, ">=") == 0) {
        return "<=";
    }
    return operator_text;
}

static int build_numeric_condition(const char *column,
                                   const char *operator_text,
                                   int value,
                                   QueryCondition *condition) {
    int use_id = equals_ci(column, "id");
    size_t i;

    if (!use_id && !equals_ci(column, "age")) {
        set_parse_error("unknown numeric column in WHERE");
        return 0;
    }
    for (i = 0; i < sizeof(OPERATOR_TYPES) / sizeof(OPERATOR_TYPES[0]); i++) {
        if (strcmp(operator_text, OPERATOR_TYPES[i].text) == 0) {
            condition->type = use_id ? OPERATOR_TYPES[i].id_type : OPERATOR_TYPES[i].age_type;
            condition->int_value = value;
            return 1;
        }
    }
    return 0;
}

static int build_comparison_condition(const Operand *left,
                                      const char *operator_text,
                                      const Operand *right,
                                      QueryCondition *condition) {
    const Operand *column_operand = NULL;
    const Operand *value_operand = NULL;
    const char *effective_operator = operator_text;

    memset(condition, 0, sizeof(*condition));

    if (left->kind == OPERAND_COLUMN &&
        (right->kind == OPERAND_INT || right->kind == OPERAND_TEXT)) {
        column_operand = left;
        value_operand = right;
    } else if ((left->kind == OPERAND_INT || left->kind == OPERAND_TEXT) &&
               right->kind == OPERAND_COLUMN) {
        column_operand = right;
        value_operand = left;
        effective_operator = reverse_operator(operator_text);
    }

    if (column_operand == NULL) {
        set_parse_error("WHERE comparison must compare a column with a value");
        return 0;
    }
    if (value_operand->kind == OPERAND_INT) {
        if (equals_ci(column_operand->column, "name")) {
            set_parse_error("name must be compared with a quoted text value");
            return 0;
        }
        return build_numeric_condition(column_operand->column,
                                       effective_operator,
                                       value_operand->int_value,
                                       condition);
    }

    if (!equals_ci(column_operand->column, "name")) {
        set_parse_error("numeric columns must be compared with an integer");
        return 0;
    }
    if (strcmp(operator_text, "=") != 0) {
        set_parse_error("name only supports = comparison");
        return 0;
    }
    condition->type = CONDITION_NAME_EQ;
    snprintf(condition->text_value, sizeof(condition->text_value), "%s", value_operand->text_value);
    return 1;
}

static int parse_comparison_condition(const char *text, QueryCondition *condition) {
    Operand left;
    Operand right;
    char operator_text[3];
    const char *cursor = text;

    if (!parse_operand(&cursor, &left) ||
        !parse_operator(&cursor, operator_text) ||
        !parse_operand(&cursor, &right)) {
        return 0;
    }
    skip_spaces(&cursor);
    if (*cursor != '\0') {
        set_parse_error("unexpected text after WHERE condition");
        return 0;
    }
    return build_comparison_condition(&left, operator_text, &right, condition);
}

static int parse_between_condition(const char *text, QueryCondition *condition) {
    Operand column;
    Operand start_value;
    Operand end_value;
    const char *cursor = text;

    if (!parse_operand(&cursor, &column)) {
        return 0;
    }
    if (column.kind != OPERAND_COLUMN || !equals_ci(column.column, "id")) {
        set_parse_error("BETWEEN currently supports only id");
        return 0;
    }
    if (!consume_keyword(&cursor, "BETWEEN")) {
        set_parse_error("expected BETWEEN in range condition");
        return 0;
    }
    if (!parse_operand(&cursor, &start_value) || start_value.kind != OPERAND_INT) {
        set_parse_error("BETWEEN start must be an integer");
        return 0;
    }
    if (!consume_keyword(&cursor, "AND")) {
        set_parse_error("BETWEEN condition requires AND");
        return 0;
    }
    if (!parse_operand(&cursor, &end_value) || end_value.kind != OPERAND_INT) {
        set_parse_error("BETWEEN end must be an integer");
        return 0;
    }
    skip_spaces(&cursor);
    if (*cursor != '\0') {
        set_parse_error("unexpected text after BETWEEN condition");
        return 0;
    }
    if (start_value.int_value > end_value.int_value) {
        set_parse_error("BETWEEN start must be less than or equal to end");
        return 0;
    }

    memset(condition, 0, sizeof(*condition));
    condition->type = CONDITION_ID_RANGE;
    condition->int_value = start_value.int_value;
    condition->second_int_value = end_value.int_value;
    return 1;
}

static int parse_single_condition(char *text, QueryCondition *condition) {
    trim_token(text);
    if (*text == '\0') {
        set_parse_error("missing WHERE condition");
        return 0;
    }
    if (find_keyword_token(text, "BETWEEN") != NULL) {
        return parse_between_condition(text, condition);
    }
    return parse_comparison_condition(text, condition);
}

static char *find_next_logical_connector(char *text, LogicalOperator *operator_out, size_t *length_out) {
    int in_quote = 0;
    int skip_between_and = 0;
    char *cursor = text;

    while (*cursor) {
        if (*cursor == '\'') {
            in_quote = !in_quote;
            cursor++;
            continue;
        }
        if (in_quote) {
            cursor++;
            continue;
        }
        if ((isalpha((unsigned char)*cursor) || *cursor == '_') &&
            (cursor == text || !is_identifier_char(cursor[-1]))) {
            char *word_start = cursor;
            size_t word_length = 0;

            while (is_identifier_char(cursor[word_length])) {
                word_length++;
            }
            if (!is_identifier_char(cursor[word_length])) {
                if (word_equals(word_start, word_length, "BETWEEN")) {
                    skip_between_and = 1;
                } else if (word_equals(word_start, word_length, "AND")) {
                    if (skip_between_and) {
                        skip_between_and = 0;
                    } else {
                        *operator_out = LOGICAL_AND;
                        *length_out = word_length;
                        return word_start;
                    }
                } else if (word_equals(word_start, word_length, "OR")) {
                    *operator_out = LOGICAL_OR;
                    *length_out = word_length;
                    return word_start;
                }
            }
            cursor += word_length;
            continue;
        }
        cursor++;
    }
    return NULL;
}

static void copy_first_condition_to_legacy_fields(Query *query, const QueryCondition *condition) {
    query->condition_type = condition->type;
    query->condition_int_value = condition->int_value;
    query->condition_second_int_value = condition->second_int_value;
    snprintf(query->condition_text_value, sizeof(query->condition_text_value), "%s", condition->text_value);
}

static int append_condition(Query *query, const QueryCondition *condition, LogicalOperator pending_operator) {
    if (query->condition_count >= MAX_WHERE_CONDITIONS) {
        set_parse_error("too many WHERE conditions");
        return 0;
    }
    if (query->condition_count > 0) {
        query->condition_operators[query->condition_count - 1] = pending_operator;
    }
    query->conditions[query->condition_count] = *condition;
    query->condition_count++;
    if (query->condition_count == 1) {
        copy_first_condition_to_legacy_fields(query, condition);
    }
    return 1;
}

/* SELECT 컬럼 목록을 파싱해 Query 구조체에 채운다. */
static int parse_selected_columns(char *columns, Query *query) {
    char *token;
    char canonical_column[32];

    query->selected_column_count = 0;
    token = strtok(columns, ",");
    while (token != NULL) {
        if (query->selected_column_count >= 3) {
            set_parse_error("too many selected columns");
            return 0;
        }
        trim_token(token);
        if (!copy_canonical_column(token, canonical_column)) {
            set_parse_error("unknown column in SELECT list");
            return 0;
        }
        strcpy(query->selected_columns[query->selected_column_count], canonical_column);
        query->selected_column_count++;
        token = strtok(NULL, ",");
    }

    if (query->selected_column_count == 0) {
        set_parse_error("missing selected columns");
        return 0;
    }
    return 1;
}

/* users 테이블에서 지원하는 WHERE 조건을 파싱한다. */
static int parse_where_clause(char *where_clause, Query *query) {
    char *cursor = where_clause;
    char *connector;
    LogicalOperator found_operator;
    LogicalOperator pending_operator = LOGICAL_AND;
    size_t operator_length;
    QueryCondition condition;

    query->condition_type = CONDITION_NONE;
    query->condition_count = 0;

    while (1) {
        trim_token(cursor);
        connector = find_next_logical_connector(cursor, &found_operator, &operator_length);
        if (connector != NULL) {
            char *next = connector + operator_length;

            *connector = '\0';
            if (!parse_single_condition(cursor, &condition)) {
                if (query->condition_count > 0 && cursor[0] == '\0') {
                    set_parse_error("missing condition after logical operator");
                }
                return 0;
            }
            if (!append_condition(query, &condition, pending_operator)) {
                return 0;
            }
            pending_operator = found_operator;
            cursor = next;
            continue;
        }

        if (!parse_single_condition(cursor, &condition)) {
            if (query->condition_count > 0 && cursor[0] == '\0') {
                set_parse_error("missing condition after logical operator");
            }
            return 0;
        }
        return append_condition(query, &condition, pending_operator);
    }
}

/* id 유무에 따라 INSERT 구문을 파싱한다. */
static int parse_insert(char *sql, Query *query) {
    char parsed_table[32];

    query->type = QUERY_INSERT;

    if (sscanf(sql, "INSERT INTO %31s VALUES ( %d , '%31[^']' , %d )",
               parsed_table,
               &query->id,
               query->name,
               &query->age) == 4) {
        if (!equals_ci(parsed_table, "users")) {
            set_parse_error("unknown table in INSERT");
            return 0;
        }
        strcpy(query->table_name, "users");
        query->insert_has_id = 1;
        return 1;
    }

    if (sscanf(sql, "INSERT INTO %31s VALUES ( '%31[^']' , %d )",
               parsed_table,
               query->name,
               &query->age) == 3) {
        if (!equals_ci(parsed_table, "users")) {
            set_parse_error("unknown table in INSERT");
            return 0;
        }
        strcpy(query->table_name, "users");
        query->insert_has_id = 0;
        return 1;
    }

    set_parse_error("invalid INSERT syntax");
    return 0;
}

/* SELECT 구문과 선택 컬럼, 선택적 필터를 파싱한다. */
static int parse_select(char *sql, Query *query) {
    char *from_pos;
    char *where_pos;
    char *select_body = sql + strlen("SELECT");
    char columns[96];
    char table_name[32];

    query->type = QUERY_SELECT;
    query->condition_type = CONDITION_NONE;

    from_pos = find_keyword_token(select_body, "FROM");
    if (from_pos == NULL) {
        set_parse_error("SELECT is missing FROM");
        return 0;
    }
    *from_pos = '\0';
    if (strlen(select_body) >= sizeof(columns)) {
        set_parse_error("SELECT column list is too long");
        return 0;
    }
    strcpy(columns, select_body);

    where_pos = find_keyword_token(from_pos + strlen("FROM"), "WHERE");
    if (where_pos != NULL) {
        *where_pos = '\0';
    }

    if (strlen(from_pos + strlen("FROM")) >= sizeof(table_name)) {
        set_parse_error("table name is too long");
        return 0;
    }
    strcpy(table_name, from_pos + strlen("FROM"));
    trim_token(table_name);
    if (!equals_ci(table_name, "users")) {
        set_parse_error("unknown table in SELECT");
        return 0;
    }
    strcpy(query->table_name, "users");

    trim_token(columns);
    if (strcmp(columns, "*") == 0) {
        query->select_all = 1;
    } else if (!parse_selected_columns(columns, query)) {
        return 0;
    }

    if (where_pos != NULL) {
        char *where_clause = where_pos + strlen("WHERE");

        trim_token(where_clause);
        if (*where_clause == '\0') {
            set_parse_error("WHERE clause is empty");
            return 0;
        }
        if (!parse_where_clause(where_clause, query)) {
            return 0;
        }
    }

    return 1;
}

/* 원시 SQL 문자열을 내부 Query 표현으로 변환한다. */
int parse_query(const char *sql, Query *query) {
    char buffer[256];
    size_t sql_length;

    set_parse_error("syntax error");
    if (sql == NULL || query == NULL) {
        set_parse_error("query is empty");
        return 0;
    }

    sql_length = strlen(sql);
    if (sql_length >= sizeof(buffer)) {
        set_parse_error("query is too long");
        return 0;
    }

    memset(query, 0, sizeof(Query));
    strcpy(buffer, sql);
    trim_sql(buffer);
    if (buffer[0] == '\0') {
        set_parse_error("query is empty");
        return 0;
    }
    uppercase_unquoted(buffer);

    if (starts_with_keyword(buffer, "INSERT")) {
        return parse_insert(buffer, query);
    }
    if (starts_with_keyword(buffer, "SELECT")) {
        return parse_select(buffer, query);
    }

    set_parse_error("unsupported statement: use INSERT or SELECT");
    return 0;
}
