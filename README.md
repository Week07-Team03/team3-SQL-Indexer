# Mini SQL Processor with B+ Tree Index

기존 C 기반 SQL 처리기에 `users.id`용 메모리 기반 B+ 트리 인덱스를 연결한 프로젝트입니다.  
핵심 목표는 `INSERT -> 자동 ID 부여 -> 인덱스 등록 -> ID 기반 SELECT 가속` 흐름을 구현하고, 대용량 데이터에서 인덱스 경로와 선형 탐색 경로의 차이를 검증하는 것입니다.

## 1. What We Built
- 메모리 기반 B+ 트리 구현
- `users.id`를 기본 키로 사용
- 레코드 추가 시 자동 ID 부여 후 `id -> row_index` 인덱싱
- 기존 SQL 처리기와 연동
- 1,000,000건 이상 데이터로 성능 비교 가능

## 2. Problem
기존 구조에서는 `WHERE id = ?` 같은 조회도 결국 전체 레코드를 순회해야 했습니다.  
데이터가 커질수록 실행 시간은 row 수에 비례해 증가하고, 기본 키 조회가 느려지는 문제가 있었습니다.

이 프로젝트에서는 이 문제를 해결하기 위해:
- `INSERT` 시 자동으로 ID를 발급하고
- 같은 ID를 B+ 트리에 등록한 뒤
- `SELECT`에서 ID 조건이면 인덱스를 먼저 타도록 실행 경로를 분리했습니다.

## 3. How It Connects
이 프로젝트의 핵심은 B+ 트리를 따로 만든 것이 아니라, **기존 SQL 실행 경로에 인덱스를 접합한 것**입니다.

```text
SQL statement
  -> Parser
  -> Query
  -> Executor
       -> INSERT
            -> Storage
            -> row 저장
            -> auto-increment id 발급
            -> bptree_insert(id, row_index)

       -> SELECT
            -> Storage
            -> if ID predicate
                 -> bptree_search / bptree_range_search
                 -> row_index 획득
                 -> rows[row_index] 접근
            -> else
                 -> full table scan
```

영속 자원:
- `data/users.schema`: 스키마 메타데이터
- `data/users.data`: 실제 row 저장 파일

참고:
- 인덱스는 파일에 직접 저장하지 않습니다.
- 프로그램 시작 시 `data/users.data`를 다시 읽어 메모리에서 B+ 트리를 재구성합니다.

## 4. B+ Tree Core Logic
이 프로젝트의 B+ 트리는 다음 구조를 가집니다.

- 내부 노드: separator key와 child pointer를 가짐
- 리프 노드: 실제 key와 `row_index`, 그리고 다음 리프를 가리키는 `next` 포인터를 가짐
- 노드가 가득 차면 split하고, 부모도 가득 차 있으면 root split으로 트리 높이가 증가함

중요한 점:

> B+ Tree는 실제 데이터 접근 정보가 leaf에 있고, internal node는 탐색 경로를 위한 인덱스 역할을 한다.

현재 구현에서:
- 실제 row 데이터는 `rows[]` 배열에 저장됩니다
- B+ 트리는 `id -> row_index`를 저장합니다

즉:
```text
rows[0] = { id=1, name="alice", age=23 }
rows[1] = { id=2, name="bob", age=30 }

B+ Tree
1 -> 0
2 -> 1
```

## 5. Query Path
인덱스 사용 경로:
- `WHERE id = ?`
- `WHERE id < ?`
- `WHERE id <= ?`
- `WHERE id > ?`
- `WHERE id >= ?`
- `WHERE id BETWEEN ? AND ?`

비인덱스 경로:
- `name`, `age` 조건
- `OR`가 포함된 복합 조건

핵심은 파서 비용이 아니라 **executor 이후의 데이터 접근 경로가 달라진다**는 점입니다.

## 6. Performance
벤치마크는 `src/storage/benchmark.c`에서 수행합니다.

기준 실험:
- `1,000,000` rows insert
- `200` lookups
- `id` 기반 조회와 `name` 기반 조회 비교

커밋된 benchmark snapshot:
- `SELECT by id (B+ tree)`: `0.01 usec/query`
- `SELECT by name (linear scan)`: `4450.575 usec/query`
- reported speedup: `445057.50x`

결과 파일:
- [docs/benchmark/benchmark_report.md](docs/benchmark/benchmark_report.md)
- [docs/benchmark/benchmark_results.csv](docs/benchmark/benchmark_results.csv)
- [docs/benchmark/benchmark_results.svg](docs/benchmark/benchmark_results.svg)

## 7. Tests and Edge Cases
테스트 파일: [tests/tests.c](tests/tests.c)

검증한 주요 항목:
- 자동 ID 부여
- 명시적 ID INSERT
- 인덱스 조회 / 범위 조회
- 선형 탐색 경로
- 대소문자 섞인 SQL
- 역방향 비교식 (`7 <= id`, `30 > age`)
- `AND / OR` 조건
- 파싱 실패 메시지
- 대량 삽입 후 인덱스 높이 증가

발표에서 강조할 수 있는 edge case:
- 첫 삽입
- 없는 ID 조회
- 대량 삽입으로 인한 leaf split / root split
- 잘못된 SQL 입력 처리
- 중복 ID 방지

## 8. Trade-offs and Limits
- B+ 트리를 붙였다고 모든 쿼리가 빨라지는 것은 아닙니다.
- 현재는 `id` 계열 조건만 인덱스를 사용하고, `name`, `age`는 여전히 선형 탐색입니다.
- `OR`가 포함된 복합 조건은 안전하게 전체 탐색으로 처리합니다.
- 삽입 시 split 비용과 추가 메모리 사용량이 발생합니다.
- 현재는 `users` 단일 테이블만 지원합니다.
- B+ 트리는 메모리 기반 구현이며, 영속화되는 것은 row 데이터뿐입니다.

## 9. Demo Script
```sql
INSERT INTO users VALUES ('alice', 23);
INSERT INTO users VALUES ('bob', 30);
SELECT * FROM users WHERE id = 1;
SELECT * FROM users WHERE id BETWEEN 1 AND 2;
SELECT * FROM users WHERE name = 'alice';
.stats
```

## 10. Build and Run
```bash
make
./build/mini_sql
./build/mini_sql --stats
./build/mini_sql --benchmark 1000000 200
./build/mini_sql_tests
```

benchmark graph:
```bash
python3 scripts/benchmark_graph.py
```

## 11. Supported Commands
SQL:
- `INSERT INTO users VALUES ('alice', 23);`
- `INSERT INTO users VALUES (10, 'alice', 23);`
- `SELECT * FROM users;`
- `SELECT id, name FROM users WHERE id = 10;`
- `SELECT * FROM users WHERE name = 'alice';`
- `SELECT * FROM users WHERE id BETWEEN 10 AND 20;`
- `SELECT * FROM users WHERE id >= 250 AND age < 23;`
- `SELECT * FROM users WHERE id = 1 OR name = 'alice';`

Meta commands:
- `.help`
- `.tables`
- `.schema users`
- `.stats`
- `.benchmark [row_count] [lookup_count]`
- `.exit`

## 12. Project Layout
```text
.
|-- data/         # users.schema, users.data
|-- docs/         # B+ 트리 설명, benchmark 결과
|-- examples/     # 예제 SQL
|-- include/      # 공개 헤더
|-- scripts/      # benchmark 그래프 생성 스크립트
|-- src/
|   |-- app/      # main, CLI
|   |-- core/     # parser, executor, display
|   |-- index/    # B+ tree
|   `-- storage/  # file I/O, in-memory DB, benchmark
`-- tests/        # unit / functional tests
```

## 13. Reference
- B+ tree note: [docs/bptree.md](docs/bptree.md)
