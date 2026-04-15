#include <stdlib.h>
#include <string.h>
#include "bptree.h"

/* 새 B+ 트리 노드를 생성하고 통계에 반영한다. */
static BPlusNode *create_node(BPlusTree *tree, int is_leaf) {
    BPlusNode *node = (BPlusNode *)calloc(1, sizeof(BPlusNode));

    if (node == NULL) {
        return NULL;
    }
    node->is_leaf = is_leaf;
    tree->node_count++;
    return node;
}

/* 주어진 노드를 루트로 하는 서브트리를 재귀적으로 해제한다. */
static void destroy_node(BPlusNode *node) {
    int i;

    if (node == NULL) {
        return;
    }
    if (!node->is_leaf) {
        for (i = 0; i <= node->key_count; i++) {
            destroy_node(node->children[i]);
        }
    }
    free(node);
}

/* 주어진 키가 들어갈 리프 노드까지 트리를 내려간다. */
static BPlusNode *find_leaf(const BPlusTree *tree, int key) {
    BPlusNode *node = tree->root;
    int i;

    while (node != NULL && !node->is_leaf) {
        i = 0;
        while (i < node->key_count && key >= node->keys[i]) {
            i++;
        }
        node = node->children[i];
    }
    return node;
}

/* 노드 내부에서 키가 들어갈 정렬 위치를 찾는다. */
static int find_insert_position(const int *keys, int key_count, int key) {
    int position = 0;

    while (position < key_count && keys[position] < key) {
        position++;
    }
    return position;
}

/* 분할 없이 리프 노드에 키-값 쌍을 삽입한다. */
static void insert_into_leaf(BPlusNode *leaf, int key, int value) {
    int position = find_insert_position(leaf->keys, leaf->key_count, key);
    int move_count = leaf->key_count - position;

    if (move_count > 0) {
        memmove(&leaf->keys[position + 1], &leaf->keys[position], (size_t)move_count * sizeof(int));
        memmove(&leaf->values[position + 1], &leaf->values[position], (size_t)move_count * sizeof(int));
    }
    leaf->keys[position] = key;
    leaf->values[position] = value;
    leaf->key_count++;
}

static int insert_into_parent(BPlusTree *tree, BPlusNode *left, int key, BPlusNode *right);

/* 가득 찬 리프를 분할하고 분리 키를 상위로 전파한다. */
static int split_leaf_and_insert(BPlusTree *tree, BPlusNode *leaf, int key, int value) {
    int temp_keys[BPTREE_ORDER];
    int temp_values[BPTREE_ORDER];
    int insert_pos = find_insert_position(leaf->keys, leaf->key_count, key);
    int split = BPTREE_ORDER / 2;
    BPlusNode *new_leaf;
    int i;
    int j;

    for (i = 0, j = 0; i < leaf->key_count; i++, j++) {
        if (j == insert_pos) {
            j++;
        }
        temp_keys[j] = leaf->keys[i];
        temp_values[j] = leaf->values[i];
    }
    temp_keys[insert_pos] = key;
    temp_values[insert_pos] = value;

    new_leaf = create_node(tree, 1);
    if (new_leaf == NULL) {
        return 0;
    }

    leaf->key_count = 0;
    for (i = 0; i < split; i++) {
        leaf->keys[i] = temp_keys[i];
        leaf->values[i] = temp_values[i];
        leaf->key_count++;
    }

    for (i = split, j = 0; i < BPTREE_ORDER; i++, j++) {
        new_leaf->keys[j] = temp_keys[i];
        new_leaf->values[j] = temp_values[i];
        new_leaf->key_count++;
    }

    new_leaf->next = leaf->next;
    leaf->next = new_leaf;
    new_leaf->parent = leaf->parent;
    return insert_into_parent(tree, leaf, new_leaf->keys[0], new_leaf);
}

/* 내부 노드에 분리 키와 자식 포인터를 삽입한다. */
static int insert_into_internal(BPlusNode *node, int left_child_index, int key, BPlusNode *right_child) {
    int move_keys = node->key_count - left_child_index;
    int move_children = node->key_count - left_child_index;

    if (move_keys > 0) {
        memmove(&node->keys[left_child_index + 1],
                &node->keys[left_child_index],
                (size_t)move_keys * sizeof(int));
    }
    if (move_children > 0) {
        memmove(&node->children[left_child_index + 2],
                &node->children[left_child_index + 1],
                (size_t)move_children * sizeof(BPlusNode *));
    }
    node->keys[left_child_index] = key;
    node->children[left_child_index + 1] = right_child;
    node->key_count++;
    right_child->parent = node;
    return 1;
}

/* 가득 찬 내부 노드를 분할하고 가운데 키를 승격한다. */
static int split_internal_and_insert(BPlusTree *tree, BPlusNode *node, int left_child_index, int key, BPlusNode *right_child) {
    int temp_keys[BPTREE_ORDER];
    BPlusNode *temp_children[BPTREE_ORDER + 1];
    int split = BPTREE_ORDER / 2;
    int promote_key;
    BPlusNode *new_node;
    int i;
    int j;

    for (i = 0, j = 0; i < node->key_count; i++, j++) {
        if (j == left_child_index) {
            j++;
        }
        temp_keys[j] = node->keys[i];
    }
    for (i = 0, j = 0; i <= node->key_count; i++, j++) {
        if (j == left_child_index + 1) {
            j++;
        }
        temp_children[j] = node->children[i];
    }
    temp_keys[left_child_index] = key;
    temp_children[left_child_index + 1] = right_child;

    new_node = create_node(tree, 0);
    if (new_node == NULL) {
        return 0;
    }

    node->key_count = 0;
    for (i = 0; i < split; i++) {
        node->keys[i] = temp_keys[i];
        node->children[i] = temp_children[i];
        if (node->children[i] != NULL) {
            node->children[i]->parent = node;
        }
        node->key_count++;
    }
    node->children[i] = temp_children[i];
    if (node->children[i] != NULL) {
        node->children[i]->parent = node;
    }

    promote_key = temp_keys[split];

    for (i = split + 1, j = 0; i < BPTREE_ORDER; i++, j++) {
        new_node->keys[j] = temp_keys[i];
        new_node->children[j] = temp_children[i];
        if (new_node->children[j] != NULL) {
            new_node->children[j]->parent = new_node;
        }
        new_node->key_count++;
    }
    new_node->children[j] = temp_children[i];
    if (new_node->children[j] != NULL) {
        new_node->children[j]->parent = new_node;
    }

    new_node->parent = node->parent;
    return insert_into_parent(tree, node, promote_key, new_node);
}

/* 승격된 분리 키를 부모에 삽입하고 필요하면 새 루트를 만든다. */
static int insert_into_parent(BPlusTree *tree, BPlusNode *left, int key, BPlusNode *right) {
    BPlusNode *parent = left->parent;
    int left_child_index;

    if (parent == NULL) {
        BPlusNode *new_root = create_node(tree, 0);

        if (new_root == NULL) {
            return 0;
        }
        new_root->keys[0] = key;
        new_root->children[0] = left;
        new_root->children[1] = right;
        new_root->key_count = 1;
        left->parent = new_root;
        right->parent = new_root;
        tree->root = new_root;
        return 1;
    }

    left_child_index = 0;
    while (left_child_index <= parent->key_count && parent->children[left_child_index] != left) {
        left_child_index++;
    }
    if (left_child_index > parent->key_count) {
        return 0;
    }

    if (parent->key_count < BPTREE_MAX_KEYS) {
        return insert_into_internal(parent, left_child_index, key, right);
    }
    return split_internal_and_insert(tree, parent, left_child_index, key, right);
}

/* 빈 B+ 트리를 초기화한다. */
void bptree_init(BPlusTree *tree) {
    tree->root = NULL;
    tree->node_count = 0;
}

/* 트리가 소유한 모든 노드를 해제한다. */
void bptree_destroy(BPlusTree *tree) {
    destroy_node(tree->root);
    tree->root = NULL;
    tree->node_count = 0;
}

/* 중복되지 않는 키-값 쌍을 트리에 삽입한다. */
int bptree_insert(BPlusTree *tree, int key, int value) {
    BPlusNode *leaf;
    int ignored_value;

    if (bptree_search(tree, key, &ignored_value)) {
        return 0;
    }

    if (tree->root == NULL) {
        tree->root = create_node(tree, 1);
        if (tree->root == NULL) {
            return 0;
        }
        tree->root->keys[0] = key;
        tree->root->values[0] = value;
        tree->root->key_count = 1;
        return 1;
    }

    leaf = find_leaf(tree, key);
    if (leaf->key_count < BPTREE_MAX_KEYS) {
        insert_into_leaf(leaf, key, value);
        return 1;
    }
    return split_leaf_and_insert(tree, leaf, key, value);
}

/* 정확히 일치하는 키를 찾아 저장된 값을 반환한다. */
int bptree_search(const BPlusTree *tree, int key, int *value_out) {
    BPlusNode *leaf = find_leaf(tree, key);
    int i;

    if (leaf == NULL) {
        return 0;
    }
    for (i = 0; i < leaf->key_count; i++) {
        if (leaf->keys[i] == key) {
            if (value_out != NULL) {
                *value_out = leaf->values[i];
            }
            return 1;
        }
    }
    return 0;
}

/* 지정한 범위에 포함되는 키들의 값을 수집한다. */
int bptree_range_search(const BPlusTree *tree,
                        int start_key,
                        int end_key,
                        int **values_out,
                        size_t *count_out) {
    BPlusNode *leaf;
    int *values = NULL;
    size_t count = 0;
    size_t capacity = 0;
    int i;

    *values_out = NULL;
    *count_out = 0;
    leaf = find_leaf(tree, start_key);
    if (leaf == NULL) {
        return 1;
    }

    while (leaf != NULL) {
        for (i = 0; i < leaf->key_count; i++) {
            if (leaf->keys[i] < start_key) {
                continue;
            }
            if (leaf->keys[i] > end_key) {
                *values_out = values;
                *count_out = count;
                return 1;
            }
            if (count == capacity) {
                size_t new_capacity = (capacity == 0) ? 8 : capacity * 2;
                int *resized = (int *)realloc(values, new_capacity * sizeof(int));

                if (resized == NULL) {
                    free(values);
                    return 0;
                }
                values = resized;
                capacity = new_capacity;
            }
            values[count++] = leaf->values[i];
        }
        leaf = leaf->next;
    }

    *values_out = values;
    *count_out = count;
    return 1;
}

/* 루트부터 리프까지의 트리 높이를 계산한다. */
int bptree_height(const BPlusTree *tree) {
    int height = 0;
    BPlusNode *node = tree->root;

    while (node != NULL) {
        height++;
        node = node->is_leaf ? NULL : node->children[0];
    }
    return height;
}
