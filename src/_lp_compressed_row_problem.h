/***
 * Copyright (c)2023 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#ifndef __PDDL__LP_COMPRESSED_ROW_PROBLEM_H__
#define __PDDL__LP_COMPRESSED_ROW_PROBLEM H__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef COMPRESSED_INT
#define COMPRESSED_INT int
#endif /* COMPRESSED_INT */

#ifndef COMPRESSED_COL_TYPE
#define COMPRESSED_COL_TYPE char
#endif /* COMPRESSED_COL_TYPE */

struct pddl_lp_compressed_row_problem {
    int num_col;
    double *col_obj;
    double *col_lb;
    double *col_ub;
    COMPRESSED_COL_TYPE *col_type;

    int num_row;
    int num_nz;
    double *row_lb;
    double *row_ub;
    double *row_rhs;
    char *row_sense;
    COMPRESSED_INT *row_beg;
    COMPRESSED_INT *row_ind;
    double *row_val;

    pddl_bool_t is_mip;
};
typedef struct pddl_lp_compressed_row_problem pddl_lp_compressed_row_problem_t;

static void compressedRowProblemInit(pddl_lp_compressed_row_problem_t *p,
                                     const pddl_lp_t *lp,
                                     COMPRESSED_COL_TYPE col_type_real,
                                     COMPRESSED_COL_TYPE col_type_int,
                                     COMPRESSED_COL_TYPE col_type_bin,
                                     pddl_bool_t use_row_ub_lb,
                                     double min_bound,
                                     double max_bound)
{
    ZEROIZE(p);
    p->num_col = lp->col_size;
    p->col_obj = ALLOC_ARR(double, lp->col_size);
    p->col_lb = ALLOC_ARR(double, lp->col_size);
    p->col_ub = ALLOC_ARR(double, lp->col_size);
    p->col_type = ALLOC_ARR(COMPRESSED_COL_TYPE, lp->col_size);
    for (int ci = 0; ci < lp->col_size; ++ci){
        p->col_obj[ci] = lp->col[ci].obj;
        p->col_lb[ci] = lp->col[ci].lb;
        p->col_ub[ci] = lp->col[ci].ub;
        if (p->col_lb[ci] <= PDDL_LP_MIN_BOUND)
            p->col_lb[ci] = min_bound;
        if (p->col_ub[ci] >= PDDL_LP_MAX_BOUND)
            p->col_ub[ci] = max_bound;
        switch (lp->col[ci].type){
            case PDDL_LP_COL_TYPE_REAL:
                p->col_type[ci] = col_type_real;
                break;
            case PDDL_LP_COL_TYPE_INT:
                p->col_type[ci] = col_type_int;
                p->is_mip = pddl_true;
                break;
            case PDDL_LP_COL_TYPE_BINARY:
                p->col_type[ci] = col_type_bin;
                p->is_mip = pddl_true;
                p->col_lb[ci] = 0.;
                p->col_ub[ci] = 1.;
                break;
        }
    }

    p->num_row = lp->row_size;
    p->num_nz = 0;
    for (int ri = 0; ri < lp->row_size; ++ri)
        p->num_nz += lp->row[ri].coef_size;

    if (use_row_ub_lb){
        p->row_lb = ALLOC_ARR(double, p->num_row);
        p->row_ub = ALLOC_ARR(double, p->num_row);
    }else{
        p->row_rhs = ALLOC_ARR(double, p->num_row);
    }
    p->row_sense = ALLOC_ARR(char, p->num_row);
    for (int i = 0; i < lp->row_size; ++i){
        p->row_sense[i] = lp->row[i].sense;
        if (use_row_ub_lb){
            switch (lp->row[i].sense){
                case 'L':
                    p->row_lb[i] = min_bound;
                    p->row_ub[i] = lp->row[i].rhs;
                    break;
                case 'G':
                    p->row_lb[i] = lp->row[i].rhs;
                    p->row_ub[i] = max_bound;
                    break;
                case 'E':
                    p->row_lb[i] = lp->row[i].rhs;
                    p->row_ub[i] = lp->row[i].rhs;
                    break;
                default:
                    PANIC("Unkown row sense '%c'", lp->row[i].sense);
                    break;
            }
        }else{
            p->row_rhs[i] = lp->row[i].rhs;
            if (p->row_rhs[i] <= PDDL_LP_MIN_BOUND){
                p->row_rhs[i] = min_bound;
            }else if (p->row_rhs[i] >= PDDL_LP_MAX_BOUND){
                p->row_rhs[i] = max_bound;
            }

        }
    }

    p->row_beg = ALLOC_ARR(COMPRESSED_INT, p->num_row);
    p->row_ind = ALLOC_ARR(COMPRESSED_INT, p->num_nz);
    p->row_val = ALLOC_ARR(double, p->num_nz);
    int ins = 0;
    for (int ri = 0; ri < p->num_row; ++ri){
        p->row_beg[ri] = ins;
        for (int ci = 0; ci < lp->row[ri].coef_size; ++ci){
            p->row_ind[ins] = lp->row[ri].coef[ci].col;
            p->row_val[ins] = lp->row[ri].coef[ci].coef;
            ++ins;
        }
    }
    ASSERT(ins == num_nz);
}

static void compressedRowProblemFree(pddl_lp_compressed_row_problem_t *p)
{
    if (p->col_obj != NULL)
        FREE(p->col_obj);
    if (p->col_lb != NULL)
        FREE(p->col_lb);
    if (p->col_ub != NULL)
        FREE(p->col_ub);
    if (p->col_type != NULL)
        FREE(p->col_type);

    if (p->row_lb != NULL)
        FREE(p->row_lb);
    if (p->row_ub != NULL)
        FREE(p->row_ub);
    if (p->row_rhs != NULL)
        FREE(p->row_rhs);
    if (p->row_sense != NULL)
        FREE(p->row_sense);
    if (p->row_beg != NULL)
        FREE(p->row_beg);
    if (p->row_ind != NULL)
        FREE(p->row_ind);
    if (p->row_val != NULL)
        FREE(p->row_val);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __PDDL__LP_COMPRESSED_ROW_PROBLEM H__ */
