/***
 * Copyright (c)2023 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */


#include "_parser.h"
#include "parse_tokenizer.h"
#include "parse_tree.h"
#include "internal.h"
#include "pddl/pddl_struct.h"

#define PERR_SRC(E, TOK, T) \
    pddlErrSetSourceFilePointer((E), (TOK)->fn, (T)->line, (T)->column, 3)

#define _ERRS(E, TOK, T, MSG) \
    do { \
        PERR_SRC((E), (TOK), (T)); \
        ERR((E), MSG " line: %d, column: %d", (T)->line, (T)->column); \
    } while (0)

#define _ERR(E, RET, TOK, T, MSG) \
    do { \
        PERR_SRC((E), (TOK), (T)); \
        ERR_RET((E), (RET), MSG " line: %d, column: %d", (T)->line, (T)->column); \
    } while (0)

#define _ERRSV(E, TOK, T, FMT, ...) \
    do { \
        PERR_SRC((E), (TOK), (T)); \
        ERR((E), FMT " line: %d, column: %d", __VA_ARGS__, (T)->line, (T)->column); \
    } while (0)

#define _ERRV(E, RET, TOK, T, FMT, ...) \
    do { \
        PERR_SRC((E), (TOK), (T)); \
        ERR_RET((E), (RET), FMT " line: %d, column: %d", __VA_ARGS__, \
                (T)->line, (T)->column); \
    } while (0)

struct pddl_parse_ctx {
    pddl_t *pddl;
    pddl_err_t *err;
    int abort;
    int accept;
    pddl_parse_tokenizer_t *tokenizer;
    pddl_parse_token_t cur_token;
};
typedef struct pddl_parse_ctx pddl_parse_ctx_t;

struct pddl_parser {
    pddl_parse_tokenizer_t tok;
    void *parser;
    pddl_parse_ctx_t ctx;
};
typedef struct pddl_parser pddl_parser_t;


static void checkRequire(pddl_parse_ctx_t *ctx,
                         int require_flag,
                         const pddl_parse_token_t *tok,
                         const char *require_flag_str,
                         const char *used_feature,
                         const char *suffix)
{
    if (ctx->abort || require_flag)
       return;

    if (ctx->pddl->cfg.pedantic){
        _ERRSV(ctx->err, ctx->tokenizer, tok,
               "Missing %s in the :requirements section while %s is used%s.",
               require_flag_str, used_feature, suffix);
        ctx->abort = 1;
    }else{
        WARN(ctx->err, "Missing %s in the :requirements section while %s is used%s."
             " (line: %d, column: %d)",
             require_flag_str, used_feature, suffix, tok->line, tok->column);
    }
}

static int tokEitherToType(pddl_types_t *types,
                           const pddl_parse_toks_t *either,
                           const pddl_parse_token_t *either_tok,
                           const pddl_parse_tokenizer_t *tokenizer,
                           pddl_err_t *err)
{
    if (either->tok_size == 0)
        _ERR(err, -1, tokenizer, either_tok, "Empty (either ...) expression.");

    // Extract the set of types the (either ...) expression consists of
    PDDL_ISET(either_set);
    for (int i = 0; i < either->tok_size; ++i){
        int type = pddlTypesGet(types, either->tok[i].str);
        if (type < 0){
            pddlISetFree(&either_set);
            _ERRV(err, -1, tokenizer, either->tok + i,
                  "Unkown type %s.", either->tok[i].str);
        }
        pddlISetAdd(&either_set, type);
    }

    // If there is only one type, just return it
    if (pddlISetSize(&either_set) == 1)
        return pddlISetGet(&either_set, 0);

    // Create or get the either type
    int either_type = pddlTypesAddEither(types, &either_set);
    if (either_type < 0)
        _ERR(err, -1, tokenizer, either_tok, "Cannot create (either ...) type.");
    pddlISetFree(&either_set);

    return either_type;
}

static int _addParamsFromTypedLists(pddl_params_t *params,
                                    pddl_types_t *types,
                                    const pddl_parse_tok_types_t *toks,
                                    const pddl_parse_tokenizer_t *tokenizer,
                                    pddl_err_t *err)
{
    for (int i = 0; i < toks->tok_type_size; ++i){
        const pddl_parse_tok_type_t *tt = toks->tok_type + i;
        // Check that we have a variable
        if (tt->tok.token > PDDL_TOKEN_KEYWORDS){
            _ERRV(err, -1, tokenizer, &tt->tok,
                  "Expecting a variable name but got the" " keyword %s.",
                  tt->tok.str);

        }else if (tt->tok.token != PDDL_TOKEN_VAR_IDNT){
            _ERRV(err, -1, tokenizer, &tt->tok,
                  "Expecting a variable name but got '%s'.", tt->tok.str);
        }

        // Extract variable name
        const char *name = tt->tok.str;
        PANIC_IF(name == NULL, "Token without string!");

        // Check that the variable wasn't used already
        if (pddlParamsGetId(params, name) >= 0){
            _ERRV(err, -1, tokenizer, &tt->tok,
                  "The variable '%s' is used for the second time.", tt->tok.str);
        }

        // Extract type
        int type;
        if (tt->no_type){
            type = 0;

        }else if (tt->type_either != NULL){
            type = tokEitherToType(types, tt->type_either,
                                   &tt->type_either_tok, tokenizer, err);
            if (type < 0)
                TRACE_RET(err, -1);

        }else{
            ASSERT(tt->type.token >= 0);
            type = pddlTypesGet(types, tt->type.str);
            if (type < 0){
                _ERRV(err, -1, tokenizer, &tt->type,
                      "Unkown type '%s'.", tt->type.str);
            }
        }

        pddl_param_t *p = pddlParamsAdd(params);
        p->name = STRDUP(name);
        p->type = type;
    }

    return 0;
}

static int addParamsFromTypedLists(pddl_params_t *params,
                                   pddl_types_t *types,
                                   const pddl_parse_typed_lists_t *typed_lists,
                                   const pddl_parse_tokenizer_t *tokenizer,
                                   pddl_err_t *err)
{
    pddl_parse_tok_types_t toks;
    pddlParseTokTypesInitFromTypedLists(&toks, typed_lists);
    int ret = _addParamsFromTypedLists(params, types, &toks, tokenizer, err);
    pddlParseTokTypesFree(&toks);
    return ret;
}

static pddl_fm_t *fmFromParser(pddl_t *pddl,
                               const pddl_params_t *params,
                               const pddl_parse_fm_tree_t *fmtree,
                               const pddl_parse_tokenizer_t *tokenizer,
                               pddl_err_t *err);

static pddl_fm_t *_fmAtomFromParser(pddl_t *pddl,
                                    const pddl_preds_t *preds,
                                    const pddl_params_t *params,
                                    const pddl_parse_fm_tree_t *fmtree,
                                    const char *name,
                                    const pddl_parse_tokenizer_t *tokenizer,
                                    pddl_err_t *err)
{
    PANIC_IF(fmtree->atom == NULL, "Atom is empty.");
    PANIC_IF(fmtree->atom->tok_size < 1, "Atom is empty.");
    const char *pname = fmtree->atom->tok[0].str;
    int pred = pddlPredsGet(preds, pname);
    int arity = 0;
    if (pred >= 0){
        arity = preds->pred[pred].param_size;

    }else{
        _ERRV(err, NULL, tokenizer, fmtree->atom->tok + 0,
              "Unknown %s '%s'.", name, pname);
    }

    int num_args = fmtree->atom->tok_size - 1;
    if (num_args != arity){
        _ERRV(err, NULL, tokenizer, fmtree->atom->tok + 0,
              "The %s '%s' has arity %d, but the atom has %d arguments.",
              name, pname, arity, num_args);
    }

    pddl_fm_atom_t *a = pddlFmNewEmptyAtom(num_args);
    a->pred = pred;
    for (int ai = 0; ai < num_args; ++ai){
        const pddl_parse_token_t *tok = fmtree->atom->tok + ai + 1;
        if (tok->token == PDDL_TOKEN_VAR_IDNT){
            int param = -1;
            if (params != NULL)
                param = pddlParamsGetId(params, tok->str);
            if (param < 0){
                pddlFmDel(&a->fm);
                _ERRV(err, NULL, tokenizer, tok,
                      "Unknown variable %s.", tok->str);
            }

            int param_type = params->param[param].type;
            int pred_param_type = preds->pred[pred].param[ai];
            if (!pddlTypesIsParent(&pddl->type, param_type, pred_param_type)){
                pddlFmDel(&a->fm);
                _ERRV(err, NULL, tokenizer, tok, "Invalid type of variable %s."
                        " The predicate's type is too narrow, i.e.,"
                        " for some values of the variable, the predicate is"
                        " not defined.", tok->str);
            }
            a->arg[ai].param = param;
            a->arg[ai].obj = -1;

        }else if (tok->token == PDDL_TOKEN_IDNT){
            int obj = pddlObjsGet(&pddl->obj, tok->str);
            if (obj < 0){
                pddlFmDel(&a->fm);
                _ERRV(err, NULL, tokenizer, tok, "Unknown object %s.", tok->str);
            }
            a->arg[ai].obj = obj;
            a->arg[ai].param = -1;

        }else{
            pddlFmDel(&a->fm);
            _ERRV(err, NULL, tokenizer, tok,
                  "Unrecognized argument %s to atom %s.", pname, tok->str);
        }
    }

    return &a->fm;
}

static pddl_fm_t *fmAtomFromParser(pddl_t *pddl,
                                   const pddl_params_t *params,
                                   const pddl_parse_fm_tree_t *fmtree,
                                   const pddl_parse_tokenizer_t *tokenizer,
                                   pddl_err_t *err)
{
    return _fmAtomFromParser(pddl, &pddl->pred, params, fmtree,
                             "predicate", tokenizer, err);
}

static pddl_fm_t *fmFAtomFromParser(pddl_t *pddl,
                                    const pddl_params_t *params,
                                    const pddl_parse_fm_tree_t *fmtree,
                                    const pddl_parse_tokenizer_t *tokenizer,
                                    pddl_err_t *err)
{
    return _fmAtomFromParser(pddl, &pddl->func, params, fmtree,
                             "function", tokenizer, err);
}

static pddl_fm_t *fmNotFromParser(pddl_t *pddl,
                                  const pddl_params_t *params,
                                  const pddl_parse_fm_tree_t *fmtree,
                                  const pddl_parse_tokenizer_t *tokenizer,
                                  pddl_err_t *err)
{
    PANIC_IF(fmtree->child_size != 1,
             "Invalid FM_NOT atom. Expecting a single child node."
             " This is definitely a bug!");
    pddl_fm_t *child = fmFromParser(pddl, params, fmtree->child[0], tokenizer, err);
    if (child == NULL)
        TRACE_RET(err, NULL);

    if (pddlFmIsAtom(child)){
        pddl_fm_atom_t *atom = pddlFmToAtom(child);
        atom->neg = !atom->neg;
        return &atom->fm;

    }else{
        pddl_fm_t *neg = pddlFmNegate(child, pddl);
        pddlFmDel(child);
        return neg;
    }
}

static pddl_fm_t *fmAndFromParser(pddl_t *pddl,
                                  const pddl_params_t *params,
                                  const pddl_parse_fm_tree_t *fmtree,
                                  const pddl_parse_tokenizer_t *tokenizer,
                                  pddl_err_t *err)
{
    if (fmtree->child_size == 0){
        return pddlFmNewEmptyAnd();
        //return &pddlFmNewBool(pddl_true)->fm;
    }

    pddl_fm_t *out = pddlFmNewEmptyAnd();
    pddl_fm_and_t *and = pddlFmToAnd(out);
    for (int i = 0; i < fmtree->child_size; ++i){
        pddl_fm_t *fm = fmFromParser(pddl, params, fmtree->child[i], tokenizer, err);
        if (fm == NULL){
            pddlFmDel(out);
            TRACE_RET(err, NULL);
        }
        pddlFmJuncAdd(and, fm);
    }
    return out;
}

static pddl_fm_t *fmOrFromParser(pddl_t *pddl,
                                 const pddl_params_t *params,
                                 const pddl_parse_fm_tree_t *fmtree,
                                 const pddl_parse_tokenizer_t *tokenizer,
                                 pddl_err_t *err)
{
    if (fmtree->child_size == 0){
        return pddlFmNewEmptyOr();
        //return &pddlFmNewBool(pddl_false)->fm;
    }

    pddl_fm_t *out = pddlFmNewEmptyOr();
    pddl_fm_and_t *or = pddlFmToOr(out);
    for (int i = 0; i < fmtree->child_size; ++i){
        pddl_fm_t *fm = fmFromParser(pddl, params, fmtree->child[i], tokenizer, err);
        if (fm == NULL){
            pddlFmDel(out);
            TRACE_RET(err, NULL);
        }
        pddlFmJuncAdd(or, fm);
    }
    return out;
}

static pddl_fm_t *fmImplyFromParser(pddl_t *pddl,
                                    const pddl_params_t *params,
                                    const pddl_parse_fm_tree_t *fmtree,
                                    const pddl_parse_tokenizer_t *tokenizer,
                                    pddl_err_t *err)
{
    PANIC_IF(fmtree->child_size != 2, "Imply formula without two arguments."
             " This shouldn't be possible at this stage. Definitely a bug!");
    pddl_fm_t *left = fmFromParser(pddl, params, fmtree->child[0], tokenizer, err);
    if (left == NULL)
        TRACE_RET(err, NULL);

    pddl_fm_t *right = fmFromParser(pddl, params, fmtree->child[1], tokenizer, err);
    if (right == NULL){
        pddlFmDel(left);
        TRACE_RET(err, NULL);
    }

    return &pddlFmNewImply(left, right)->fm;
}

static pddl_fm_t *fmWhenFromParser(pddl_t *pddl,
                                   const pddl_params_t *params,
                                   const pddl_parse_fm_tree_t *fmtree,
                                   const pddl_parse_tokenizer_t *tokenizer,
                                   pddl_err_t *err)
{
    PANIC_IF(fmtree->child_size != 2, "When formula without two arguments."
             " This shouldn't be possible at this stage. Definitely a bug!");
    pddl_fm_t *left = fmFromParser(pddl, params, fmtree->child[0], tokenizer, err);
    if (left == NULL)
        TRACE_RET(err, NULL);

    pddl_fm_t *right = fmFromParser(pddl, params, fmtree->child[1], tokenizer, err);
    if (right == NULL){
        pddlFmDel(left);
        TRACE_RET(err, NULL);
    }

    return &pddlFmNewWhen(left, right)->fm;
}

static pddl_fm_t *fmQuantFromParser(pddl_t *pddl,
                                    const pddl_params_t *params,
                                    int quant_type,
                                    const pddl_parse_fm_tree_t *fmtree,
                                    const pddl_parse_tokenizer_t *tokenizer,
                                    pddl_err_t *err)
{
    PANIC_IF(fmtree->params == NULL, "Quantified formula without parameters."
             " This is definitely a bug!");
    PANIC_IF(fmtree->child_size != 1, "Quantified formula must have a single"
             " child formula. This is definitely a bug!");

    pddl_fm_quant_t *fm = pddlFmNewEmptyQuant(quant_type);
    if (addParamsFromTypedLists(&fm->param, &pddl->type, fmtree->params,
                                tokenizer, err) != 0){
        pddlFmDel(&fm->fm);
        TRACE_RET(err, NULL);
    }
    if (params != NULL)
        pddlParamsInherit(&fm->param, params);
    fm->qfm = fmFromParser(pddl, &fm->param, fmtree->child[0], tokenizer, err);
    if (fm->qfm == NULL){
        pddlFmDel(&fm->fm);
        TRACE_RET(err, NULL);
    }
    return &fm->fm;
}

static pddl_fm_t *fmForallFromParser(pddl_t *pddl,
                                     const pddl_params_t *params,
                                     const pddl_parse_fm_tree_t *fmtree,
                                     const pddl_parse_tokenizer_t *tokenizer,
                                     pddl_err_t *err)
{
    return fmQuantFromParser(pddl, params, PDDL_FM_FORALL, fmtree,
                             tokenizer, err);
}

static pddl_fm_t *fmExistsFromParser(pddl_t *pddl,
                                     const pddl_params_t *params,
                                     const pddl_parse_fm_tree_t *fmtree,
                                     const pddl_parse_tokenizer_t *tokenizer,
                                     pddl_err_t *err)
{
    return fmQuantFromParser(pddl, params, PDDL_FM_EXIST, fmtree,
                             tokenizer, err);
}

static pddl_fm_t *fmFuncOpFromParser(pddl_t *pddl,
                                     const pddl_params_t *params,
                                     int func_op_type,
                                     const pddl_parse_fm_tree_t *fmtree,
                                     const pddl_parse_tokenizer_t *tokenizer,
                                     pddl_err_t *err)
{
    PANIC_IF(fmtree->child_size != 2, "Func-op must have 2 arguments.");
    pddl_fm_t *dst = fmFromParser(pddl, params, fmtree->child[0], tokenizer, err);
    if (dst == NULL)
        TRACE_RET(err, NULL);
    if (!pddlFmIsAtom(dst)){
        _ERRS(err, tokenizer, &fmtree->ref_tok,
              "Functional operation requires an atom as a destination.");
        pddlFmDel(dst);
        return NULL;
    }

    pddl_fm_func_op_t *fm;
    if (fmtree->child[1]->kind == PDDL_PARSE_FM_INUM){
        int srcval = atoi(fmtree->child[1]->ref_tok.str);
        fm = pddlFmNewFuncOpVal(func_op_type, pddlFmToAtom(dst), srcval);

    }else{
        pddl_fm_t *src = fmFromParser(pddl, params, fmtree->child[1], tokenizer, err);
        if (src == NULL)
            TRACE_RET(err, NULL);
        if (!pddlFmIsAtom(src)){
            _ERRS(err, tokenizer, &fmtree->ref_tok,
                  "Functional operation for now supports only numbers and"
                  " atoms on the right-hand side.");
            pddlFmDel(src);
            pddlFmDel(dst);
            return NULL;
        }
        fm = pddlFmNewFuncOpFVal(func_op_type, pddlFmToAtom(dst),
                                 pddlFmToAtom(src));
    }

    return &fm->fm;
}

static pddl_fm_t *fmAssignFromParser(pddl_t *pddl,
                                     const pddl_params_t *params,
                                     const pddl_parse_fm_tree_t *fmtree,
                                     const pddl_parse_tokenizer_t *tokenizer,
                                     pddl_err_t *err)
{
    return fmFuncOpFromParser(pddl, params, PDDL_FM_ASSIGN, fmtree,
                              tokenizer, err);
}

static pddl_fm_t *fmIncreaseFromParser(pddl_t *pddl,
                                       const pddl_params_t *params,
                                       const pddl_parse_fm_tree_t *fmtree,
                                       const pddl_parse_tokenizer_t *tokenizer,
                                       pddl_err_t *err)
{
    return fmFuncOpFromParser(pddl, params, PDDL_FM_INCREASE, fmtree,
                              tokenizer, err);
}

static pddl_fm_t *fmFromParser(pddl_t *pddl,
                               const pddl_params_t *params,
                               const pddl_parse_fm_tree_t *fmtree,
                               const pddl_parse_tokenizer_t *tokenizer,
                               pddl_err_t *err)
{
    switch (fmtree->kind){
        case PDDL_PARSE_FM_ATOM:
            return fmAtomFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_FATOM:
            return fmFAtomFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_AND:
            return fmAndFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_OR:
            return fmOrFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_IMPLY:
            return fmImplyFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_WHEN:
            return fmWhenFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_NOT:
            return fmNotFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_EXISTS:
            return fmExistsFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_FORALL:
            return fmForallFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_ASSIGN:
            return fmAssignFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_INCREASE:
            return fmIncreaseFromParser(pddl, params, fmtree, tokenizer, err);

        case PDDL_PARSE_FM_LIST:
            PANIC("Got FM_LIST from the parser. This is definitely a bug!");
            return NULL;

        case PDDL_PARSE_FM_INUM:
            _ERRV(err, NULL, tokenizer, &fmtree->ref_tok,
                  "Malformed formula. Found '%s' and don't know what to do"
                  " about it.", fmtree->ref_tok.str);

        default:
            PANIC("Unkown type of fm_tree %d. This is definitely a bug!",
                  fmtree->kind);
            return NULL;
    }
}

static int addTypes(pddl_t *pddl,
                    const pddl_parse_typed_lists_t *lists,
                    const pddl_parse_tokenizer_t *tokenizer,
                    pddl_err_t *err)
{
    for (int ti = 0; ti < lists->list_size; ++ti){
        const pddl_parse_typed_list_t *tl = lists->list[ti];
        ASSERT(tl->list != NULL);
        if (tl->list->tok_size == 0)
            continue;

        if (tl->type_either != NULL){
            _ERR(err, -1, tokenizer, &tl->type_either_tok,
                 "The (either ...) expression is not allowed in the :types section.");
        }

        int parent_type = -1;
        if (tl->type.token >= 0){
            if (tl->type.token != PDDL_TOKEN_IDNT){
                if (tl->type.token == PDDL_TOKEN_VAR_IDNT){
                    _ERR(err, -1, tokenizer, &tl->type,
                         "Variables cannot be used in the :types section.");
                }else{
                    _ERRV(err, -1, tokenizer, &tl->type,
                          "Unexpected identifier '%s' in the :types section.",
                          tl->type.str);
                }
            }
            ASSERT(tl->type.str != NULL);
            parent_type = pddlTypesAdd(&pddl->type, tl->type.str, 0);
        }else{
            parent_type = 0;
        }
        ASSERT(parent_type >= 0);

        for (int i = 0; i < tl->list->tok_size; ++i){
            const pddl_parse_token_t *tok = tl->list->tok + i;
            ASSERT(tok->token >= 0 && tok->str != NULL);
            if (tok->token != PDDL_TOKEN_IDNT){
                if (tok->token == PDDL_TOKEN_VAR_IDNT){
                    _ERR(err, -1, tokenizer, tok,
                         "Variables cannot be used in the :types section.");
                }else{
                    _ERRV(err, -1, tokenizer, tok,
                          "Unexpected identifier '%s' in the :types section.",
                          tok->str);
                }
            }
            int type = pddlTypesGet(&pddl->type, tok->str);
            // Type "object" (with ID 0) can be defined multiple times
            if (type > 0){
                // TODO: Configure ignoring this
                fprintf(stderr, "%d %d\n", tok->line, tok->column);
                _ERRV(err, -1, tokenizer, tok,
                      "The type '%s' is defined for the second time.", tok->str);

            }else if (type < 0){
                pddlTypesAdd(&pddl->type, tok->str, parent_type);
            }
        }
    }

    return 0;
}

static int _addObjectsFromTokTypes(pddl_objs_t *objs,
                                   pddl_types_t *types,
                                   const pddl_parse_tok_types_t *toks,
                                   pddl_bool_t constants_section,
                                   const pddl_parse_tokenizer_t *tokenizer,
                                   pddl_err_t *err)
{
    const char *obj_sec = "object";
    if (constants_section)
        obj_sec = "constant";

    for (int i = 0; i < toks->tok_type_size; ++i){
        const pddl_parse_tok_type_t *tt = toks->tok_type + i;
        if (tt->tok.token != PDDL_TOKEN_IDNT){
            _ERRV(err, -1, tokenizer, &tt->tok,
                  "Expecting a name of an object but got '%s'.", tt->tok.str);
        }

        if (pddlObjsGet(objs, tt->tok.str) >= 0){
            _ERRV(err, -1, tokenizer, &tt->tok,
                  "The %s '%s' is already defined.", obj_sec, tt->tok.str);
        }

        pddl_obj_t *obj = pddlObjsAdd(objs, tt->tok.str);
        if (obj == NULL){
            _ERRV(err, -1, tokenizer, &tt->tok,
                  "Failed to add %s '%s'.", obj_sec, tt->tok.str);
        }
        obj->is_constant = constants_section;

        int type;
        if (tt->no_type){
            type = 0;

        }else if (tt->type_either != NULL){
            type = tokEitherToType(types, tt->type_either,
                                   &tt->type_either_tok, tokenizer, err);
            if (type < 0)
                TRACE_RET(err, -1);

        }else{
            ASSERT(tt->type.token >= 0);
            type = pddlTypesGet(types, tt->type.str);
            if (type < 0){
                _ERRV(err, -1, tokenizer, &tt->type,
                      "Type %s is not defined.", tt->type.str);
            }
        }

        obj->type = type;
        obj->is_constant = constants_section;
    }
    return 0;
}

static int _addObjects(pddl_objs_t *objs,
                       pddl_types_t *types,
                       const pddl_parse_typed_lists_t *lists,
                       pddl_bool_t constants_section,
                       const pddl_parse_tokenizer_t *tokenizer,
                       pddl_err_t *err)
{
    pddl_parse_tok_types_t toks;
    pddlParseTokTypesInitFromTypedLists(&toks, lists);
    int ret = _addObjectsFromTokTypes(objs, types, &toks, constants_section,
                                      tokenizer, err);
    pddlParseTokTypesFree(&toks);
    return ret;
}

static int addConstants(pddl_t *pddl,
                        const pddl_parse_typed_lists_t *lists,
                        const pddl_parse_tokenizer_t *tokenizer,
                        pddl_err_t *err)
{
    return _addObjects(&pddl->obj, &pddl->type, lists, pddl_true, tokenizer, err);
}

static int addObjects(pddl_t *pddl,
                      const pddl_parse_typed_lists_t *lists,
                      const pddl_parse_tokenizer_t *tokenizer,
                      pddl_err_t *err)
{
    return _addObjects(&pddl->obj, &pddl->type, lists, pddl_false, tokenizer, err);
}

static int _addPred(pddl_preds_t *preds,
                    pddl_types_t *types,
                    const pddl_parse_token_t *head,
                    const pddl_parse_typed_lists_t *args_list,
                    const char *pred_func,
                    const pddl_parse_tokenizer_t *tokenizer,
                    pddl_err_t *err)
{
    PANIC_IF(head->token != PDDL_TOKEN_IDNT, "Invalid %s name token.", pred_func);
    PANIC_IF(head->str == NULL, "Invalid (empty) %s name token.", pred_func);
    if (pddlPredsGet(preds, head->str) >= 0){
        _ERRV(err, -1, tokenizer, head,
              "Duplicate definition of %s %s.", pred_func, head->str);
    }

    pddl_params_t params;
    pddlParamsInit(&params);
    if (addParamsFromTypedLists(&params, types, args_list, tokenizer, err) != 0){
        pddlParamsFree(&params);
        TRACE_RET(err, -1);
    }

    pddl_pred_t *p = pddlPredsAdd(preds);
    pddlPredSetName(p, head->str);
    pddlPredAllocParams(p, params.param_size);
    for (int i = 0; i < params.param_size; ++i){
        if (pddlPredSetParamType(p, i, params.param[i].type) != 0){
            _ERRV(err, -1, tokenizer, head,
                  "Something went wrong when setting type of %d'th parameter"
                  " of %s %s.", i, pred_func, p->name);
        }
    }
    pddlParamsFree(&params);
    return p->id;
}

static int addPred(pddl_t *pddl,
                   const pddl_parse_token_t *head,
                   const pddl_parse_typed_lists_t *args_list,
                   const pddl_parse_tokenizer_t *tokenizer,
                   pddl_err_t *err)
{
    if (_addPred(&pddl->pred, &pddl->type, head, args_list, "predicate",
                 tokenizer, err) < 0){
        TRACE_RET(err, -1);
    }
    return 0;
}

static int addFunc(pddl_t *pddl,
                   const pddl_parse_token_t *head,
                   const pddl_parse_typed_lists_t *args_list,
                   const pddl_parse_tokenizer_t *tokenizer,
                   pddl_err_t *err)
{
    int pred_id = _addPred(&pddl->func, &pddl->type, head, args_list,
                           "function", tokenizer, err);
    if (pred_id < 0)
        TRACE_RET(err, -1);
    return 0;
}

static int addAction(pddl_t *pddl,
                     const pddl_parse_token_t *name,
                     const pddl_parse_typed_lists_t *params,
                     const pddl_parse_fm_tree_t *pre,
                     const pddl_parse_fm_tree_t *eff,
                     const pddl_parse_tokenizer_t *tokenizer,
                     pddl_err_t *err)
{
    if (name->token != PDDL_TOKEN_IDNT){
        _ERRV(err, -1, tokenizer, name,
              "Cannot use %s as an action name.", name->str);
    }

    pddl_action_t *a = pddlActionsAddEmpty(&pddl->action);
    a->name = STRDUP(name->str);

    if (addParamsFromTypedLists(&a->param, &pddl->type, params, tokenizer, err) != 0)
        TRACE_RET(err, -1);

    a->pre = fmFromParser(pddl, &a->param, pre, tokenizer, err);
    if (a->pre == NULL)
        TRACE_RET(err, -1);

    a->eff = fmFromParser(pddl, &a->param, eff, tokenizer, err);
    if (a->eff == NULL)
        TRACE_RET(err, -1);

    return 0;
}

static int setGoal(pddl_t *pddl, const pddl_parse_fm_tree_t *goal,
                   const pddl_parse_tokenizer_t *tokenizer,
                   pddl_err_t *err)
{
    if (pddl->goal != NULL)
        pddlFmDel(pddl->goal);
    pddl->goal = fmFromParser(pddl, NULL, goal, tokenizer, err);
    if (pddl->goal == NULL)
        TRACE_RET(err, -1);
    return 0;
}

static int setInit(pddl_t *pddl, const pddl_parse_fm_tree_t *fmtree,
                   const pddl_parse_tokenizer_t *tokenizer,
                   pddl_err_t *err)
{
    PANIC_IF(fmtree->kind != PDDL_PARSE_FM_LIST, "Initial state is not just"
             " a list of atoms. This is definitely a bug!");

    if (pddl->init != NULL)
        pddlFmDel(&pddl->init->fm);
    pddl->init = pddlFmToAnd(pddlFmNewEmptyAnd());

    for (int i = 0; i < fmtree->child_size; ++i){
        pddl_fm_t *fm = fmFromParser(pddl, NULL, fmtree->child[i],
                                     tokenizer, err);
        if (fm == NULL)
            TRACE_RET(err, -1);

        pddlFmJuncAdd(pddl->init, fm);
    }
    return 0;
}

static int setMetric(pddl_t *pddl, const pddl_parse_fm_tree_t *fmtree,
                     const pddl_parse_tokenizer_t *tokenizer,
                     pddl_err_t *err)
{
    pddl_fm_t *fm = fmFromParser(pddl, NULL, fmtree, tokenizer, err);
    if (fm == NULL)
        TRACE_RET(err, -1);
    if (!pddlFmIsAtom(fm)){
        pddlFmDel(fm);
        _ERR(err, -1, tokenizer, &fmtree->ref_tok,
             "Metric must optimize a function atom.");
    }
    pddl_fm_atom_t *opt = pddlFmToAtom(fm);
    int func = pddlPredsGet(&pddl->func, "total-cost");
    if (opt->pred != func){
        pddlFmDel(fm);
        _ERR(err, -1, tokenizer, &fmtree->ref_tok,
             "Only (:metric minimize (total-cost)) is supported.");
    }
    pddlFmDel(fm);

    pddl->metric = 1;
    return 0;
}

#ifndef PDDL_DEBUG
# define NDEBUG
#endif /* PDDL_DEBUG */

// This forces the parser to allocate stack dynamically
#define YYSTACKDEPTH 0
#include "_parser.c"


static int _parseSection(pddl_parser_t *p, int start_token, pddl_err_t *err)
{
    p->ctx.abort = 0;
    p->ctx.accept = 0;

    int ret = pddlParseTokenizerStartSection(&p->tok, start_token, err);
    if (ret < 0)
        TRACE_RET(err, -1);
    if (ret != 0)
        return ret;

    while (!p->ctx.abort
            && (ret = pddlParseTokenizerNext(&p->tok, &p->ctx.cur_token, err)) == 0){
        pddlInternalParse(p->parser, p->ctx.cur_token.token, p->ctx.cur_token);
    }
    pddlInternalParse(p->parser, 0, p->ctx.cur_token);
    if (ret < 0 || p->ctx.abort)
        TRACE_RET(err, -1);

    if (!p->ctx.accept){
        ERR_RET(err, -1, "Something went wrong. The parser did not accept"
                " the current section %d but did not report error earlier."
                " This is definitely a bug!", start_token);
    }

    return 0;
}

static int parseSection(pddl_parser_t *p,
                        int start_token,
                        const char *section_name,
                        pddl_bool_t required,
                        pddl_err_t *err)
{
    LOG(err, "Parsing %s...", section_name);
    pddlParseTokenizerRestart(&p->tok);
    int ret = _parseSection(p, start_token, err);
    if (ret < 0)
        TRACE_RET(err, -1);
    if (ret != 0){
        if (required)
            ERR_RET(err, -1, "Missing (%s ...) section.", section_name);
        LOG(err, "Missing (%s ...) section.", section_name);
    }
    return 0;
}

static int parseDomainName(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_DOMAIN, "domain", pddl_true, err);
}

static int parseRequirements(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_REQUIREMENTS, ":requirements",
                        pddl_false, err);
}

static int parseTypes(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_TYPES, ":types", pddl_false, err);
}

static int parsePredicates(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_PREDICATES, ":predicates", pddl_false, err);
}

static int parseFunctions(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_FUNCTIONS, ":functions", pddl_false, err);
}

static int parseConstants(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_CONSTANTS, ":constants", pddl_false, err);
}

static int _parseActionsAxioms(pddl_parser_t *p,
                               const char *log_name,
                               const char *section_name,
                               int token,
                               pddl_err_t *err)
{
    LOG(err, "Parsing %s...", log_name);
    pddlParseTokenizerRestart(&p->tok);
    int counter = 0;
    for (; 1; ++counter){
        int ret = _parseSection(p, token, err);
        if (ret < 0)
            TRACE_RET(err, -1);
        if (ret != 0)
            break;
    }

    LOG(err, "Parsed %d %s sections", counter, section_name);
    return 0;
}

static int parseActions(pddl_parser_t *p, pddl_err_t *err)
{
    return _parseActionsAxioms(p, "actions", ":action", PDDL_TOKEN_ACTION, err);
}

static int parseAxioms(pddl_parser_t *p, pddl_err_t *err)
{
    return _parseActionsAxioms(p, "axioms", ":derived", PDDL_TOKEN_DERIVED, err);
}

static int parseProblemName(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_PROBLEM, "problem", pddl_true, err);
}

static int parseProblemDomainName(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_PROB_DOMAIN, ":domain", pddl_true, err);
}

static int parseObjects(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_OBJECTS, ":objects", pddl_false, err);
}

static int parseInit(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_INIT, ":init", pddl_true, err);
}

static int parseGoal(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_GOAL, ":goal", pddl_true, err);
}

static int parseMetric(pddl_parser_t *p, pddl_err_t *err)
{
    return parseSection(p, PDDL_TOKEN_METRIC, ":metric", pddl_false, err);
}

static int parseAllParsed(pddl_parser_t *p, pddl_err_t *err)
{
    pddlParseTokenizerStart(&p->tok);
    pddl_parse_token_t t;
    int ret;
    if ((ret = pddlParseTokenizerNext(&p->tok, &t, err)) == 0
            && t.token == PDDL_TOKEN_LPAREN
            && (ret = pddlParseTokenizerNext(&p->tok, &t, err)) == 0
            && t.token == PDDL_TOKEN_DEFINE
            && (ret = pddlParseTokenizerNext(&p->tok, &t, err)) == 0
            && t.token == PDDL_TOKEN_RPAREN
            && (ret = pddlParseTokenizerNext(&p->tok, &t, err)) > 0){
        return 0;
    }

    if (ret == 0){
        _ERR(err, -1, &p->tok, &t, "Invalid format of the pddl file.");
    }else{
        ERR_RET(err, -1, "Invalid format of the pddl file.");
    }
}

static void *__alloc(size_t size)
{
    return MALLOC(size);
}

static void __free(void *ptr)
{
    FREE(ptr);
}

static int pddlParserInit(pddl_parser_t *p, pddl_t *pddl, const char *fn,
                          pddl_err_t *err)
{
    ZEROIZE(p);
    if (fn == NULL)
        return 0;

    if (pddlParseTokenizerInit(&p->tok, fn, err) != 0){
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    p->ctx.pddl = pddl;
    p->ctx.err = err;
    p->ctx.abort = 0;
    p->ctx.accept = 0;
    p->ctx.tokenizer = &p->tok;

    p->parser = pddlInternalParseAlloc(__alloc, &p->ctx);
    if (p->parser == NULL){
        pddlParseTokenizerFree(&p->tok);
        CTXEND(err);
        ERR_RET(err, -1, "Cannot create (lemon) parser!");
    }

    //pddlInternalParseTrace(stderr, "TRACE: ");

    return 0;
}

static void pddlParserFree(pddl_parser_t *p)
{
    pddlInternalParseFree(p->parser, __free);
    pddlParseTokenizerFree(&p->tok);
}

int pddlParseDomain(pddl_t *pddl, const char *fn, pddl_err_t *err)
{
    CTX(err, "Parse Domain");
    LOG(err, "Parsing domain file %s", fn);
    pddl_parser_t par;
    if (pddlParserInit(&par, pddl, fn, err) != 0)
        TRACE_RET(err, -1);

    // TODO: durative action
    // TODO: derived predicates
    if (parseDomainName(&par, err) != 0
            || parseRequirements(&par, err) != 0
            || parseTypes(&par, err) != 0
            || parseConstants(&par, err) != 0
            || parsePredicates(&par, err) != 0
            || parseFunctions(&par, err) != 0
            || parseActions(&par, err) != 0
            || parseAxioms(&par, err) != 0
            || parseAllParsed(&par, err) != 0){
        pddlParserFree(&par);
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    // TODO: Report clash between predicate, type, object, action names

    pddlParserFree(&par);
    CTXEND(err);
    return 0;
}

int pddlParseProblem(pddl_t *pddl, const char *fn, pddl_err_t *err)
{
    CTX(err, "Parse Problem");
    LOG(err, "Parsing problem file %s", fn);
    pddl_parser_t par;
    if (pddlParserInit(&par, pddl, fn, err) != 0)
        TRACE_RET(err, -1);

    if (parseProblemName(&par, err) != 0
            || parseProblemDomainName(&par, err) != 0
            || parseObjects(&par, err) != 0
            || parseInit(&par, err) != 0
            || parseGoal(&par, err) != 0
            || parseMetric(&par, err) != 0
            || parseAllParsed(&par, err) != 0){
        pddlParserFree(&par);
        CTXEND(err);
        TRACE_RET(err, -1);
    }

    // TODO: Report clash between predicate, type, object, action names

    pddlParserFree(&par);
    CTXEND(err);
    return 0;
}
