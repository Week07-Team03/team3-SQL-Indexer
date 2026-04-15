#ifndef STORAGE_INTERNAL_H
#define STORAGE_INTERNAL_H

#include "storage.h"

#define STORAGE_TABLE_NAME "users"
#define STORAGE_SCHEMA_PATH "data/users.schema"
#define STORAGE_DATA_PATH "data/users.data"

int storage_read_schema_file(const char *table_name, TableSchema *schema);

#endif
