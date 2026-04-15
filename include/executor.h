#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "query.h"

/* 파싱된 쿼리를 실행하고 사용자용 출력을 처리한다. */
int execute_query(const Query *query);

#endif
