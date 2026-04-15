# B+ Tree Notes

이 프로젝트는 `users.id` 기본 키에 대해 메모리 기반 B+ 트리를 유지합니다. 목표는 완전한 DB 엔진 구현이 아니라, 인덱스가 `SELECT` 경로를 어떻게 바꾸는지 작은 코드베이스 안에서 보기 쉽게 만드는 것입니다.

## What Uses The Index

- `WHERE id = ?`
- `WHERE id BETWEEN ? AND ?`

나머지 조건(`name`, `age`, `<`, `>`)은 전체 row 배열을 순회합니다.

## Source Map

- `src/index/bptree.c`: 노드 생성, split, exact search, range search
- `src/storage/database.c`: row 저장소와 인덱스 연결
- `src/storage/benchmark.c`: 인덱스 조회와 선형 조회 성능 비교

## Run

```bash
make
./build/mini_sql --benchmark 1000000 200
python3 scripts/benchmark_graph.py
```

벤치마크 스크립트는 필요하면 `build/mini_sql`을 먼저 만들고, 결과를 `build/benchmark/`에 기록합니다. 저장소에 커밋된 기준 결과는 `docs/benchmark/`에 남겨둡니다.
