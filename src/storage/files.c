#include <stdio.h>
#include <string.h>
#include "storage_internal.h"

/* users.schema 파일을 읽어 테이블 메타데이터를 구성한다. */
int storage_read_schema_file(const char *table_name, TableSchema *schema) {
    FILE *file;
    char line[64];

    if (table_name == NULL ||
        schema == NULL ||
        strcmp(table_name, STORAGE_TABLE_NAME) != 0) {
        return 0;
    }

    file = fopen(STORAGE_SCHEMA_PATH, "r");
    if (file == NULL) {
        return 0;
    }

    memset(schema, 0, sizeof(*schema));
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
