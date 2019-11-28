/***
 * cpddl
 * -------
 * Copyright (c)2019 Daniel Fiser <danfis@danfis.cz>,
 * Agent Technology Center, Department of Computer Science,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file BDS-LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <boruvka/alloc.h>
#include <boruvka/fifo.h>
#include "pddl/open_list.h"

/** A structure containing a stored value */
struct node {
    pddl_state_id_t state_id;
};
typedef struct node node_t;

/** A node holding a key and all the values. */
struct keynode {
    bor_fifo_t fifo;              /*!< Structure containing all values */
    struct keynode *spe_left;  /*!< Connector to splay-tree */
    struct keynode *spe_right; /*!< Connector to splay-tree */
    int cost;
};
typedef struct keynode keynode_t;

/** Main structure */
struct pddl_open_list_splaytree {
    pddl_open_list_t list;
    keynode_t *root;        /*!< Root of splay-tree */
    keynode_t *pre_keynode; /*!< Preinitialized key-node */
};
typedef struct pddl_open_list_splaytree pddl_open_list_splaytree_t;

#define LIST_FROM_PARENT(parent) \
    bor_container_of(parent, pddl_open_list_splaytree_t, list)


static void pddlOpenListSplayTreeDel(pddl_open_list_t *list);
static void pddlOpenListSplayTreePush(pddl_open_list_t *list,
                                      int cost,
                                      pddl_state_id_t state_id);
static int pddlOpenListSplayTreePop(pddl_open_list_t *list,
                                    pddl_state_id_t *state_id,
                                    int *cost);
static int pddlOpenListSplayTreeTop(pddl_open_list_t *list,
                                    pddl_state_id_t *state_id,
                                    int *cost);
static void pddlOpenListSplayTreeClear(pddl_open_list_t *list);


static keynode_t *keynodeNew(void);
static void keynodeDel(keynode_t *kn);

/** Define splay-tree structure */
#define BOR_SPLAY_TREE_NODE_T keynode_t
#define BOR_SPLAY_TREE_T pddl_open_list_splaytree_t
#define BOR_SPLAY_KEY_T int
#define BOR_SPLAY_NODE_KEY(node) node->cost
#define BOR_SPLAY_NODE_SET_KEY(head, node, key) \
    node->cost = key
#define BOR_SPLAY_KEY_CMP(head, key1, key2) \
    (key1 == key2 ? 0 : (key1 < key2 ? -1 : 1))
#include "boruvka/splaytree_def.h"

pddl_open_list_t *pddlOpenListSplayTree(void)
{
    pddl_open_list_splaytree_t *list;

    list = BOR_ALLOC(pddl_open_list_splaytree_t);
    _pddlOpenListInit(&list->list,
                  pddlOpenListSplayTreeDel,
                  pddlOpenListSplayTreePush,
                  pddlOpenListSplayTreePop,
                  pddlOpenListSplayTreeTop,
                  pddlOpenListSplayTreeClear);
    list->pre_keynode = keynodeNew();

    borSplayInit(list);

    return &list->list;
}

static void pddlOpenListSplayTreeDel(pddl_open_list_t *_list)

{
    pddl_open_list_splaytree_t *list = LIST_FROM_PARENT(_list);
    pddlOpenListSplayTreeClear(&list->list);
    if (list->pre_keynode)
        keynodeDel(list->pre_keynode);
    borSplayFree(list);
    _pddlOpenListFree(&list->list);
    BOR_FREE(list);
}

static void pddlOpenListSplayTreePush(pddl_open_list_t *_list,
                                      int cost,
                                      pddl_state_id_t state_id)
{
    pddl_open_list_splaytree_t *list = LIST_FROM_PARENT(_list);
    keynode_t *kn;
    node_t node;

    // Try to insert pre-allocated key-node
    kn = borSplayInsert(list, cost, list->pre_keynode);

    if (kn == NULL){
        // Insertion was successful, remember the inserted key-node and
        // preallocate next key-node for next time.
        kn = list->pre_keynode;
        list->pre_keynode = keynodeNew();
    }

    // Push next node into key-node container
    node.state_id = state_id;
    borFifoPush(&kn->fifo, &node);
}

static int pddlOpenListSplayTreePop(pddl_open_list_t *_list,
                                    pddl_state_id_t *state_id,
                                    int *cost)
{
    pddl_open_list_splaytree_t *list = LIST_FROM_PARENT(_list);
    keynode_t *kn;
    node_t *n;

    if (list->root == NULL)
        return -1;

    // Find out minimal node
    kn = borSplayMin(list);

    // We know for sure that this key-node must contain some nodes because
    // an empty key-nodes are removed immediately.
    // Pop next node from the key-node.
    n = borFifoFront(&kn->fifo);
    *state_id = n->state_id;
    *cost = kn->cost;
    borFifoPop(&kn->fifo);

    // If the key-node is empty, remove it from the tree
    if (borFifoEmpty(&kn->fifo)){
        borSplayRemove(list, kn);
        keynodeDel(kn);
    }

    return 0;
}

static int pddlOpenListSplayTreeTop(pddl_open_list_t *_list,
                                  pddl_state_id_t *state_id,
                                  int *cost)
{
    pddl_open_list_splaytree_t *list = LIST_FROM_PARENT(_list);
    keynode_t *kn;
    node_t *n;

    if (list->root == NULL)
        return -1;

    // Find out minimal node
    kn = borSplayMin(list);

    // Get the next node from the key-node.
    n = borFifoFront(&kn->fifo);
    *state_id = n->state_id;
    *cost = kn->cost;
    return 0;
}

static void pddlOpenListSplayTreeClear(pddl_open_list_t *_list)
{
    pddl_open_list_splaytree_t *list = LIST_FROM_PARENT(_list);
    keynode_t *kn;

    while (list->root){
        kn = list->root;
        borSplayRemove(list, list->root);
        keynodeDel(kn);
    }
}

static keynode_t *keynodeNew(void)
{
    keynode_t *kn;
    kn = BOR_MALLOC(sizeof(keynode_t));
    borFifoInit(&kn->fifo, sizeof(node_t));
    return kn;
}

static void keynodeDel(keynode_t *kn)
{
    borFifoFree(&kn->fifo);
    BOR_FREE(kn);
}
