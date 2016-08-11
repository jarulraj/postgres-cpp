/*-------------------------------------------------------------------------
 *
 * nodeFuncs.h
 *		Various general-purpose manipulations of Node trees
 *
 * Portions Copyright (c) 1996-2015, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/nodes/nodeFuncs.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef NODEFUNCS_H
#define NODEFUNCS_H

#include "nodes/parsenodes.h"


/* flags bits for query_tree_walker and query_tree_mutator */
#define QTW_IGNORE_RT_SUBQUERIES	0x01		/* subqueries in rtable */
#define QTW_IGNORE_CTE_SUBQUERIES	0x02		/* subqueries in cteList */
#define QTW_IGNORE_RC_SUBQUERIES	0x03		/* both of above */
#define QTW_IGNORE_JOINALIASES		0x04		/* JOIN alias var lists */
#define QTW_IGNORE_RANGE_TABLE		0x08		/* skip rangetable entirely */
#define QTW_EXAMINE_RTES			0x10		/* examine RTEs */
#define QTW_DONT_COPY_QUERY			0x20		/* do not copy top Query */


extern Oid	exprType(const Node *expr);
extern int32 exprTypmod(const Node *expr);
extern bool exprIsLengthCoercion(const Node *expr, int32 *coercedTypmod);
extern Node *relabel_to_typmod(Node *expr, int32 typmod);
extern Node *strip_implicit_coercions(Node *node);
extern bool expression_returns_set(Node *clause);

extern Oid	exprCollation(const Node *expr);
extern Oid	exprInputCollation(const Node *expr);
extern void exprSetCollation(Node *expr, Oid collation);
extern void exprSetInputCollation(Node *expr, Oid inputcollation);

extern int	exprLocation(const Node *expr);

// Peloton : Alter function pointer to contain only two arguments
typedef bool (*expression_tree_walker_fptr)(Node *, void *);
extern bool expression_tree_walker(
    Node *node, bool (*walker)(Node *, void *), void *context);

typedef Node *(*expression_tree_mutator_fptr)(Node *, void *);
extern Node *expression_tree_mutator(
    Node *node, Node *(*mutator)(Node *, void *), void *context);

typedef bool (*query_tree_walker_fptr)(Node *, void *);
extern bool query_tree_walker(
    Query *query, bool (*walker)(Node *, void *), void *context, int flags);

typedef Node *(*query_tree_mutator_fptr)(Node *, void *);
extern Query *query_tree_mutator(
    Query *query, Node *(*mutator)(Node *, void *), void *context, int flags);

typedef bool (*range_table_walker_fptr)(Node *, void *);
extern bool range_table_walker(
    List *rtable, bool (*walker)(Node *, void *), void *context, int flags);

typedef Node *(*range_table_mutator_fptr)(Node *, void *);
extern List *range_table_mutator(
    List *rtable, Node *(*mutator)(Node *, void *), void *context, int flags);

typedef bool (*query_or_expression_tree_walker_fptr)(Node *, void *);
extern bool query_or_expression_tree_walker(
    Node *node, bool (*walker)(Node *, void *), void *context, int flags);

typedef Node *(*query_or_expression_tree_mutator_fptr)(Node *, void *);
extern Node *query_or_expression_tree_mutator(
    Node *node, Node *(*mutator)(Node *, void *), void *context, int flags);

typedef bool (*raw_expression_tree_walker_fptr)(Node *, void *);
extern bool raw_expression_tree_walker(
    Node *node, bool (*walker)(Node *, void *), void *context);

#endif   /* NODEFUNCS_H */
