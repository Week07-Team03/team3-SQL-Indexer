#ifndef QUERY_H
#define QUERY_H

typedef enum {
    QUERY_INSERT,
    QUERY_SELECT
} QueryType;

typedef enum {
    CONDITION_NONE,
    CONDITION_ID_EQ,
    CONDITION_ID_LT,
    CONDITION_ID_LTE,
    CONDITION_ID_GT,
    CONDITION_ID_GTE,
    CONDITION_NAME_EQ,
    CONDITION_AGE_EQ,
    CONDITION_AGE_LT,
    CONDITION_AGE_LTE,
    CONDITION_AGE_GT,
    CONDITION_AGE_GTE,
    CONDITION_ID_RANGE
} ConditionType;

#define MAX_WHERE_CONDITIONS 8

typedef enum {
    LOGICAL_AND,
    LOGICAL_OR
} LogicalOperator;

typedef struct {
    ConditionType type;
    int int_value;
    int second_int_value;
    char text_value[32];
} QueryCondition;

typedef struct {
    QueryType type;
    char table_name[32];
    int insert_has_id;
    int id;
    char name[32];
    int age;
    int select_all;
    char selected_columns[3][32];
    int selected_column_count;
    ConditionType condition_type;
    int condition_int_value;
    int condition_second_int_value;
    char condition_text_value[32];
    QueryCondition conditions[MAX_WHERE_CONDITIONS];
    LogicalOperator condition_operators[MAX_WHERE_CONDITIONS - 1];
    int condition_count;
} Query;

#endif
