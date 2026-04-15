#ifndef PARSER_H
#define PARSER_H

#include "query.h"

/* 지원하는 SQL 문자열을 Query 구조체로 파싱한다. */
int parse_query(const char *sql, Query *query);

#endif
