# Mini SQL Processor

`users` 테이블 하나를 대상으로 동작하는 작은 C 기반 SQL 엔진입니다. 목표는 기능을 많이 넣는 것보다 `parser -> executor -> storage -> display` 흐름과 B+ 트리 인덱스 구조를 읽기 쉽게 유지하는 데 있습니다.

## Layout

```text
.
|-- data/         # users.schema, users.data
|-- docs/         # 설계/인덱스 문서
|-- examples/     # 예제 SQL
|-- include/      # 공개 헤더
|-- scripts/      # 보조 스크립트
|-- src/
|   |-- app/      # main, CLI
|   |-- core/     # parser, executor, display
|   |-- index/    # B+ tree
|   `-- storage/  # 파일 로딩, 메모리 DB, benchmark
`-- tests/        # 단위 테스트
```

## Build

```bash
make
```

빌드 결과물은 `build/` 아래에 생성됩니다.

## Run

```bash
./build/mini_sql
./build/mini_sql --stats
./build/mini_sql --benchmark 1000000 200
./build/mini_sql_tests
```

벤치마크 그래프와 CSV가 필요하면 아래 스크립트를 실행하면 됩니다.

```bash
python3 scripts/benchmark_graph.py
```

새로 생성한 산출물은 `build/benchmark/` 아래에 생성됩니다. 현재 저장소에 포함된 기준 벤치마크 결과는 `docs/benchmark/`에 보관됩니다.

## Supported SQL

- `INSERT INTO users VALUES ('alice', 23);`
- `INSERT INTO users VALUES (10, 'alice', 23);`
- `SELECT * FROM users;`
- `SELECT id, name FROM users WHERE id = 10;`
- `SELECT * FROM users WHERE name = 'alice';`
- `SELECT * FROM users WHERE id BETWEEN 10 AND 20;`

메타 명령:

- `.help`
- `.tables`
- `.schema users`
- `.stats`
- `.benchmark [row_count] [lookup_count]`
- `.exit`

## Notes

- 저장 파일은 `data/users.schema`, `data/users.data`를 사용합니다.
- `WHERE id = ?` 와 `WHERE id BETWEEN ? AND ?` 는 B+ 트리 인덱스를 사용합니다.
- `name`, `age` 조건은 선형 탐색으로 처리합니다.
- B+ 트리 구현 설명은 [docs/bptree.md](/Users/hong-yoonki/Desktop/krafton/vscode/codexproj/week7_sql/team3-SQL-Indexer/docs/bptree.md)에서 볼 수 있습니다.
