#ifndef BPTREE_H
#define BPTREE_H

#include <stddef.h>

#define BPTREE_ORDER 32
#define BPTREE_MAX_KEYS (BPTREE_ORDER - 1)

typedef struct BPlusNode {
    int is_leaf;
    int key_count;
    int keys[BPTREE_MAX_KEYS];
    int values[BPTREE_MAX_KEYS];
    struct BPlusNode *children[BPTREE_ORDER];
    struct BPlusNode *parent;
    struct BPlusNode *next;
} BPlusNode;

typedef struct {
    BPlusNode *root;
    size_t node_count;
} BPlusTree;

/* 빈 B+ 트리 구조체를 초기화한다. */
void bptree_init(BPlusTree *tree);
/* 트리가 소유한 모든 노드를 해제한다. */
void bptree_destroy(BPlusTree *tree);
/* 중복되지 않는 키-값 쌍을 트리에 삽입한다. */
int bptree_insert(BPlusTree *tree, int key, int value);
/* 단일 키를 조회하고 찾으면 값을 기록한다. */
int bptree_search(const BPlusTree *tree, int key, int *value_out);
/* 지정한 범위에 포함되는 키들의 값을 수집한다. */
int bptree_range_search(const BPlusTree *tree,
                        int start_key,
                        int end_key,
                        int **values_out,
                        size_t *count_out);
/* 현재 트리 높이를 반환한다. */
int bptree_height(const BPlusTree *tree);

#endif
