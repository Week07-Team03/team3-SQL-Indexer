#include <stdio.h>
#include <string.h>
#include "display.h"

/* 컬럼 이름을 스키마 인덱스로 변환한다. */
static int find_column_index(const TableSchema *schema, const char *name) {
    int i;

    for (i = 0; i < schema->column_count; i++) {
        if (strcmp(schema->names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

/* SELECT 결과에 출력할 컬럼 인덱스 목록을 구성한다. */
static int build_selected_indices(const Query *query, const TableSchema *schema, int indices[3], int *count) {
    int i;

    if (query->select_all) {
        *count = schema->column_count;
        for (i = 0; i < *count; i++) {
            indices[i] = i;
        }
        return 1;
    }

    *count = query->selected_column_count;
    for (i = 0; i < *count; i++) {
        indices[i] = find_column_index(schema, query->selected_columns[i]);
        if (indices[i] < 0) {
            return 0;
        }
    }
    return 1;
}

/* ASCII 테이블 출력용 가로 경계를 그린다. */
static void print_border(const int widths[3], int count) {
    int i;
    int j;

    for (i = 0; i < count; i++) {
        putchar('+');
        for (j = 0; j < widths[i] + 2; j++) {
            putchar('-');
        }
    }
    puts("+");
}

/* 계산된 컬럼 너비에 맞춰 한 줄을 출력한다. */
static void print_row(char values[3][32], const int widths[3], int count) {
    int i;

    for (i = 0; i < count; i++) {
        printf("| %-*s ", widths[i], values[i]);
    }
    puts("|");
}

/* 요청한 컬럼에 맞는 행 데이터를 출력 문자열로 변환한다. */
static void get_cell_value(const UserRow *row, int column_index, char buffer[32]) {
    if (column_index == 0) {
        snprintf(buffer, 32, "%d", row->id);
    } else if (column_index == 1) {
        snprintf(buffer, 32, "%s", row->name);
    } else {
        snprintf(buffer, 32, "%d", row->age);
    }
}

/* SELECT 결과를 형식화된 테이블로 출력한다. */
int print_select_result(const Query *query, const TableSchema *schema, const QueryResult *result) {
    int indices[3];
    int widths[3] = {0, 0, 0};
    char cells[3][32];
    int count;
    size_t row;
    int col;
    int len;

    if (!build_selected_indices(query, schema, indices, &count)) {
        printf("invalid column\n");
        return 0;
    }

    for (col = 0; col < count; col++) {
        widths[col] = (int)strlen(schema->names[indices[col]]);
    }
    for (row = 0; row < result->row_count; row++) {
        for (col = 0; col < count; col++) {
            get_cell_value(result->rows[row], indices[col], cells[col]);
            len = (int)strlen(cells[col]);
            if (len > widths[col]) {
                widths[col] = len;
            }
        }
    }

    print_border(widths, count);
    for (col = 0; col < count; col++) {
        snprintf(cells[col], sizeof(cells[col]), "%s", schema->names[indices[col]]);
    }
    print_row(cells, widths, count);
    print_border(widths, count);
    for (row = 0; row < result->row_count; row++) {
        for (col = 0; col < count; col++) {
            get_cell_value(result->rows[row], indices[col], cells[col]);
        }
        print_row(cells, widths, count);
    }
    print_border(widths, count);
    printf("%zu row(s)\n", result->row_count);
    return 1;
}
