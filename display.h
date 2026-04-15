#ifndef DISPLAY_H
#define DISPLAY_H

#include "query.h"
#include "storage.h"

/* 요청한 컬럼 순서에 맞춰 SELECT 결과를 출력한다. */
int print_select_result(const Query *query, const TableSchema *schema, const QueryResult *result);

#endif
