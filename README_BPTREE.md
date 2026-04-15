# Mini SQL Processor with B+ Tree Index

`users` 테이블 하나를 대상으로 동작하는 C 기반 mini SQL 엔진입니다. 이번 버전에서는 메모리 기반 B+ 트리 인덱스를 `id` 기본 키에 연결해서 `WHERE id = ?` 와 `WHERE id BETWEEN ? AND ?` 를 인덱스로 처리합니다.

지원 기능:

- `INSERT INTO users VALUES ('name', age);`
- `INSERT INTO users VALUES (id, 'name', age);`
- `SELECT * FROM users;`
- `SELECT id, name FROM users WHERE id = 10;`
- `SELECT * FROM users WHERE name = 'alice';`
- `SELECT * FROM users WHERE id BETWEEN 10 AND 20;`
- CLI 메타 커맨드: `.help`, `.tables`, `.schema users`, `.stats`, `.benchmark`, `.exit`

핵심 포인트:

- 자동 ID 부여: `INSERT` 시 ID를 생략하면 `next_id` 가 자동 할당됩니다.
- B+ 트리 인덱스: `id` 검색과 범위 검색은 리프 링크를 포함한 B+ 트리로 처리합니다.
- 선형 탐색 비교: `name` / `age` 조건은 배열 전체를 순회합니다.
- 기존 SQL 흐름 유지: `parser -> executor -> storage/display`
- 대용량 성능 실험: 임시 메모리 DB에서 1,000,000건 이상 삽입 후 인덱스/선형 탐색을 비교할 수 있습니다.

## Build

```bash
make
```

## Run

대화형 CLI:

```bash
./mini_sql
```

대규모 성능 테스트:

```bash
./mini_sql --benchmark 1000000 200
```

저장소 상태 확인:

```bash
./mini_sql --stats
```

단위/기능 테스트:

```bash
./mini_sql_tests
```

## Architecture

- `parser.c`: `INSERT`, `SELECT`, `WHERE` 절 파싱
- `executor.c`: 쿼리 타입에 따라 실행 경로 분기
- `storage.c`: 메모리 DB, 자동 ID, 파일 적재/저장, benchmark
- `bptree.c`: B+ 트리 exact search / range search
- `display.c`: SELECT 결과 테이블 렌더링
- `tests.c`: 파서와 인덱스/조회 동작 검증

## 차별화 요소

- `WHERE id BETWEEN a AND b` 범위 조회를 B+ 트리 리프 체인으로 지원
- `.stats` 로 현재 row 수, 다음 ID, B+ 트리 높이 확인 가능
- 벤치마크는 실제 저장 파일을 오염시키지 않고 임시 메모리 DB에서 수행
