/***
 * cpddl
 * -------
 * Copyright (c)2020 Daniel Fiser <danfis@danfis.cz>,
 * Faculty of Electrical Engineering, Czech Technical University in Prague.
 * All rights reserved.
 *
 * This file is part of cpddl.
 *
 * Distributed under the OSI-approved BSD License (the "License");
 * see accompanying file LICENSE for details or see
 * <http://www.opensource.org/licenses/bsd-license.php>.
 *
 * This software is distributed WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the License for more information.
 */

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boruvka/alloc.h>
#include <boruvka/iarr.h>
#include <boruvka/htable.h>
#include <boruvka/hfunc.h>
#include "pddl/endomorphism.h"
#include "assert.h"

struct pre_eff_vars {
    int id;
    bor_iset_t pre;
    bor_iset_t eff;
    bor_htable_key_t key;
    bor_list_t htable;
};
typedef struct pre_eff_vars pre_eff_vars_t;

struct op_groups {
    bor_iset_t *group;
    int group_size;
    int group_alloc;
    bor_htable_t *htable;
};
typedef struct op_groups op_groups_t;

static bor_htable_key_t preEffComputeHash(const pre_eff_vars_t *v)
{
    uint64_t key;
    ((uint32_t *)&key)[0] = borFastHash_32(v->pre.s, v->pre.size, 13);
    ((uint32_t *)&key)[1] = borFastHash_32(v->eff.s, v->eff.size, 13);
    return key;
}

static bor_htable_key_t preEffHash(const bor_list_t *l, void *_)
{
    const pre_eff_vars_t *v = BOR_LIST_ENTRY(l, pre_eff_vars_t, htable);
    return v->key;
}

static int preEffEq(const bor_list_t *l1, const bor_list_t *l2, void *_)
{
    const pre_eff_vars_t *v1 = BOR_LIST_ENTRY(l1, pre_eff_vars_t, htable);
    const pre_eff_vars_t *v2 = BOR_LIST_ENTRY(l2, pre_eff_vars_t, htable);
    return borISetEq(&v1->pre, &v2->pre) && borISetEq(&v1->eff, &v2->eff);
}

static void assignToGroup(op_groups_t *opgs, const pddl_fdr_op_t *op)
{
    pre_eff_vars_t *pev = BOR_ALLOC(pre_eff_vars_t);
    bzero(pev, sizeof(*pev));
    for (int fi = 0; fi < op->pre.fact_size; ++fi)
        borISetAdd(&pev->pre, op->pre.fact[fi].var);
    for (int fi = 0; fi < op->eff.fact_size; ++fi)
        borISetAdd(&pev->eff, op->eff.fact[fi].var);
    pev->key = preEffComputeHash(pev);
    borListInit(&pev->htable);

    bor_list_t *found;
    if ((found = borHTableInsertUnique(opgs->htable, &pev->htable)) == NULL){
        if (opgs->group_size == opgs->group_alloc){
            if (opgs->group_alloc == 0)
                opgs->group_alloc = 2;
            opgs->group_alloc *= 2;
            opgs->group = BOR_REALLOC_ARR(opgs->group, bor_iset_t,
                                          opgs->group_alloc);
        }
        int group_id = opgs->group_size++;
        bor_iset_t *g = opgs->group + group_id;
        borISetInit(g);
        pev->id = group_id;
        borISetAdd(g, op->id);

    }else{
        pev = BOR_LIST_ENTRY(found, pre_eff_vars_t, htable);
        borISetAdd(opgs->group + pev->id, op->id);
    }
}

static void opGroupsInit(op_groups_t *opg, const pddl_fdr_t *fdr)
{
    bzero(opg, sizeof(*opg));
    opg->htable = borHTableNew(preEffHash, preEffEq, NULL);
    for (int oi = 0; oi < fdr->op.op_size; ++oi)
        assignToGroup(opg, fdr->op.op[oi]);
}

static void opGroupsFree(op_groups_t *opg)
{
    for (int i = 0; i < opg->group_size; ++i)
        borISetFree(opg->group + i);
    if (opg->group != NULL)
        BOR_FREE(opg->group);

    bor_list_t list;
    borListInit(&list);
    borHTableGather(opg->htable, &list);
    while (!borListEmpty(&list)){
        bor_list_t *item = borListNext(&list);
        borListDel(item);
        pre_eff_vars_t *v = BOR_LIST_ENTRY(item, pre_eff_vars_t, htable);
        borISetFree(&v->pre);
        borISetFree(&v->eff);
        BOR_FREE(v);
    }
    borHTableDel(opg->htable);
}

struct pddl_endomorphism_fdr_csp_constr {
    int size;
    int *cvar;
    int **val;
    int val_size;
    int val_alloc;
};
typedef struct pddl_endomorphism_fdr_csp_constr
    pddl_endomorphism_fdr_csp_constr_t;

struct pddl_endomorphism_fdr_csp {
    int fact_size;
    int cvar_size;
    pddl_endomorphism_fdr_csp_constr_t *constr;
    int constr_size;
    int constr_alloc;
    int *map;
};
typedef struct pddl_endomorphism_fdr_csp pddl_endomorphism_fdr_csp_t;

static int opIdToCVar(const pddl_endomorphism_fdr_csp_t *e, int op_id)
{
    return op_id + e->fact_size;
}

static int cvarToOpId(const pddl_endomorphism_fdr_csp_t *e, int cvar)
{
    return cvar - e->fact_size;
}

static pddl_endomorphism_fdr_csp_constr_t *
    addConstr(pddl_endomorphism_fdr_csp_t *e, int size)
{
    if (e->constr_size == e->constr_alloc){
        if (e->constr_alloc == 0)
            e->constr_alloc = 2;
        e->constr_alloc *= 2;
        e->constr = BOR_REALLOC_ARR(e->constr,
                                    pddl_endomorphism_fdr_csp_constr_t,
                                    e->constr_alloc);
    }

    pddl_endomorphism_fdr_csp_constr_t *c = e->constr + e->constr_size++;
    bzero(c, sizeof(*c));
    c->size = size;
    c->cvar = BOR_CALLOC_ARR(int, c->size);
    return c;
}

static int *constrAddVal(pddl_endomorphism_fdr_csp_constr_t *c)
{
    if (c->val_size == c->val_alloc){
        if (c->val_alloc == 0)
            c->val_alloc = 1;
        c->val_alloc *= 2;
        c->val = BOR_REALLOC_ARR(c->val, int *, c->val_alloc);
    }
    int **v = c->val + c->val_size++;
    *v = BOR_CALLOC_ARR(int, c->size);
    return *v;
}

static void setInitConstr(pddl_endomorphism_fdr_csp_t *e,
                          const pddl_fdr_t *fdr)
{
    pddl_endomorphism_fdr_csp_constr_t *c = addConstr(e, fdr->var.var_size);
    int *val = constrAddVal(c);
    for (int v = 0; v < fdr->var.var_size; ++v){
        c->cvar[v] = fdr->var.var[v].val[fdr->init[v]].global_id;
        val[v] = fdr->init[v];
    }
}

static void setGoalConstr(pddl_endomorphism_fdr_csp_t *e,
                          const pddl_fdr_t *fdr)
{
    pddl_endomorphism_fdr_csp_constr_t *c = addConstr(e, fdr->goal.fact_size);
    int *val = constrAddVal(c);
    for (int fi = 0; fi < fdr->goal.fact_size; ++fi){
        int gvar = fdr->goal.fact[fi].var;
        int gval = fdr->goal.fact[fi].val;
        c->cvar[fi] = fdr->var.var[gvar].val[gval].global_id;
        val[fi] = gval;
    }
}

static void setOpPreConstr(pddl_endomorphism_fdr_csp_t *e,
                           const pddl_fdr_t *fdr,
                           const pddl_fdr_op_t *op,
                           const bor_iset_t *group)
{
    int size = op->pre.fact_size + 1;
    pddl_endomorphism_fdr_csp_constr_t *c = addConstr(e, size);
    c->cvar[0] = opIdToCVar(e, op->id);
    for (int fi = 0; fi < op->pre.fact_size; ++fi){
        int pvar = op->pre.fact[fi].var;
        int pval = op->pre.fact[fi].val;
        c->cvar[fi + 1] = fdr->var.var[pvar].val[pval].global_id;
    }

    int op_cost = op->cost;
    int op_id;
    BOR_ISET_FOR_EACH(group, op_id){
        const pddl_fdr_op_t *op = fdr->op.op[op_id];
        if (op->cost > op_cost)
            continue;
        int *val = constrAddVal(c);
        val[0] = op_id;
        for (int fi = 0; fi < op->pre.fact_size; ++fi)
            val[fi + 1] = op->pre.fact[fi].val;
    }
}

static void setOpEffConstr(pddl_endomorphism_fdr_csp_t *e,
                           const pddl_fdr_t *fdr,
                           const pddl_fdr_op_t *op,
                           const bor_iset_t *group)
{
    int size = op->eff.fact_size + 1;
    pddl_endomorphism_fdr_csp_constr_t *c = addConstr(e, size);
    c->cvar[0] = opIdToCVar(e, op->id);
    for (int fi = 0; fi < op->eff.fact_size; ++fi){
        int pvar = op->eff.fact[fi].var;
        int pval = op->eff.fact[fi].val;
        c->cvar[fi + 1] = fdr->var.var[pvar].val[pval].global_id;
    }

    int op_cost = op->cost;
    int op_id;
    BOR_ISET_FOR_EACH(group, op_id){
        const pddl_fdr_op_t *op = fdr->op.op[op_id];
        if (op->cost > op_cost)
            continue;
        int *val = constrAddVal(c);
        val[0] = op_id;
        for (int fi = 0; fi < op->eff.fact_size; ++fi)
            val[fi + 1] = op->eff.fact[fi].val;
    }
}

static void setOpConstrs(pddl_endomorphism_fdr_csp_t *e,
                         const pddl_fdr_t *fdr,
                         const bor_iset_t *group)
{
    int op_id;
    BOR_ISET_FOR_EACH(group, op_id){
        const pddl_fdr_op_t *op = fdr->op.op[op_id];
        setOpPreConstr(e, fdr, op, group);
        setOpEffConstr(e, fdr, op, group);
    }
}

static void pddlEndomorphismFdrCspInit(pddl_endomorphism_fdr_csp_t *e,
                                       const pddl_fdr_t *fdr)
{
    bzero(e, sizeof(*e));
    e->fact_size = fdr->var.global_id_size;
    e->cvar_size = e->fact_size + fdr->op.op_size;

    setInitConstr(e, fdr);
    setGoalConstr(e, fdr);

    op_groups_t opg;
    opGroupsInit(&opg, fdr);
    for (int group_id = 0; group_id < opg.group_size; ++group_id)
        setOpConstrs(e, fdr, opg.group + group_id);
    opGroupsFree(&opg);

    e->map = BOR_CALLOC_ARR(int, e->cvar_size);
    for (int i = 0; i < e->cvar_size; ++i)
        e->map[i] = -1;
}

static void pddlEndomorphismFdrCspFree(pddl_endomorphism_fdr_csp_t *e)
{
    // TODO
}

static void printXCSP(const pddl_endomorphism_fdr_csp_t *e,
                      const pddl_fdr_t *fdr,
                      FILE *fout)
{
    fprintf(fout, "<instance format=\"XCSP3\" type=\"CSP\">\n");
    //fprintf(fout, "<instance format=\"XCSP3\" type=\"COP\">\n");
    fprintf(fout, "<variables>\n");
    for (int gid = 0; gid < fdr->var.global_id_size; ++gid){
        const pddl_fdr_val_t *val = fdr->var.global_id_to_val[gid];
        const pddl_fdr_var_t *var = fdr->var.var + val->var_id;
        fprintf(fout, "<var id=\"fact%d\"> %d..%d</var>\n",
                gid, 0, var->val_size - 1);
    }
    for (int oid = 0; oid < fdr->op.op_size; ++oid){
        fprintf(fout, "<var id=\"op%d\"> %d..%d</var>\n",
                oid, 0, fdr->op.op_size - 1);
    }
    fprintf(fout, "</variables>\n");
    fprintf(fout, "<constraints>\n");
    for (int cid = 0; cid < e->constr_size; ++cid){
        const pddl_endomorphism_fdr_csp_constr_t *c = e->constr + cid;
        fprintf(fout, "  <extension>\n");
        fprintf(fout, "    <list>");
        for (int i = 0; i < c->size; ++i){
            if (c->cvar[i] >= opIdToCVar(e, 0)){
                fprintf(fout, " op%d", cvarToOpId(e, c->cvar[i]));
            }else{
                fprintf(fout, " fact%d", c->cvar[i]);
            }
        }
        fprintf(fout, "</list>\n");
        fprintf(fout, "    <supports>");
        for (int ci = 0; ci < c->val_size; ++ci){
            if (c->size > 1)
                fprintf(fout, "(");
            for (int i = 0; i < c->size; ++i){
                if (i != 0)
                    fprintf(fout, ",");
                fprintf(fout, "%d", c->val[ci][i]);
            }
            if (c->size > 1)
                fprintf(fout, ")");
        }
        fprintf(fout, "</supports>\n");
        fprintf(fout, "  </extension>\n");
    }
    fprintf(fout, "  <intension>\n");
    fprintf(fout, "    or(");
    for (int oid = 0; oid < fdr->op.op_size; ++oid){
        if (oid != 0)
            fprintf(fout, ", ");
        fprintf(fout, "ne(op%d,%d)", oid, oid);
    }
    fprintf(fout, ")\n");
    fprintf(fout, "  </intension>\n");

    fprintf(fout, "</constraints>\n");
#if 0
    fprintf(fout, "<objectives>\n");
    fprintf(fout, "<minimize type=\"sum\">\n");
    fprintf(fout, "<list>");
    for (int oid = 0; oid < fdr->op.op_size; ++oid){
        fprintf(fout, " op%d", oid);
    }
    fprintf(fout, "</list>\n");
    fprintf(fout, "</minimize>\n");
    fprintf(fout, "</objectives>\n");
#endif
    fprintf(fout, "</instance>\n");
}

struct read_buf {
    char *buf;
    int buf_size;
    int buf_alloc;
    int eof;
    int *cvar;
};

static void readBufInit(struct read_buf *rb)
{
    bzero(rb, sizeof(*rb));
    rb->buf_alloc = 256;
    rb->buf = BOR_ALLOC_ARR(char, rb->buf_alloc);
}

static void readBufFree(struct read_buf *rb)
{
    BOR_FREE(rb->buf);
    if (rb->cvar != NULL)
        BOR_FREE(rb->cvar);
}

static void readBuf(struct read_buf *rb, int fd)
{
    if (rb->buf_alloc < rb->buf_size + 256){
        while (rb->buf_alloc < rb->buf_size + 256)
            rb->buf_alloc *= 2;
        rb->buf = BOR_REALLOC_ARR(rb->buf, char, rb->buf_alloc);
    }

    size_t maxsize = rb->buf_alloc - rb->buf_size;
    ssize_t rsize = read(fd, rb->buf + rb->buf_size, maxsize);
    if (rsize == 0){
        rb->eof = 1;
    }else if (rsize > 0){
        rb->buf_size += rsize;
    }else{
        perror("read() failed:");
        exit(-1);
    }
}

static void picatParseVarList(const pddl_endomorphism_fdr_csp_t *e,
                              const char *line,
                              int *out)
{
    const char *c = strstr(line, "<list>");
    if (c == NULL)
        return;
    int ins = 0;
    c += 6;
    while (*c != '<'){
        for (; *c == ' '; ++c);
        if (*c == '<')
            break;
        if (*c == 'f'){
            c += 4;
            int id = strtol(c, NULL, 10);
            out[ins++] = id;

        }else if (*c == 'o'){
            c += 2;
            int id = strtol(c, NULL, 10);
            out[ins++] = opIdToCVar(e, id);

        }else{
            BOR_FATAL("Unkwnown character '%c' while parsing var list", *c);
        }
        for (; *c >= '0' && *c <= '9'; ++c);
    }

    ASSERT(ins == e->cvar_size);
}

static void picatParseValues(const pddl_endomorphism_fdr_csp_t *e,
                             const char *line,
                             const int *cvar)
{
    const char *c = strstr(line, "<values>");
    if (c == NULL)
        return;
    int i = 0;
    c += 8;
    while (*c != '<'){
        for (; *c == ' '; ++c);
        if (*c == '<')
            break;
        if (*c < '0' || *c > '9')
            BOR_FATAL("Unkwnown character '%c' while parsing var list", *c);
        int val = strtol(c, NULL, 10);
        e->map[cvar[i++]] = val;
        for (; *c >= '0' && *c <= '9'; ++c);
    }

    ASSERT(i == e->cvar_size);
}

static void readExtractLines(pddl_endomorphism_fdr_csp_t *e,
                             struct read_buf *rb,
                             const char *spec,
                             bor_err_t *err)
{
    int last_end = -1;
    const char *start = rb->buf;
    for (int i = 0; i < rb->buf_size; ++i){
        if (rb->buf[i] == '\n'){
            last_end = i;
            rb->buf[i] = 0x0;
            BOR_INFO(err, "%s '%s'", spec, start);
            if (e != NULL
                    && start[0] == 'v'
                    && strstr(start, "<list>") != NULL){
                rb->cvar = BOR_ALLOC_ARR(int, e->cvar_size);
                picatParseVarList(e, start, rb->cvar);
            }
            if (e != NULL
                    && start[0] == 'v'
                    && strstr(start, "<values>") != NULL){
                picatParseValues(e, start, rb->cvar);
            }
            start = rb->buf + i + 1;
        }
    }

    if (last_end > 0){
        int ins = 0;
        for (int i = last_end + 1; i < rb->buf_size; ++i)
            rb->buf[ins++] = rb->buf[i];
        rb->buf_size = ins;
    }
}

static void readPicat(pddl_endomorphism_fdr_csp_t *e,
                      int fd_out,
                      int fd_err,
                      bor_iset_t *prune_op,
                      bor_err_t *err)
{
    struct read_buf bout, berr;
    readBufInit(&bout);
    readBufInit(&berr);

    fd_set read_fds;
    do {
        FD_ZERO(&read_fds);
        if (fd_out >= 0)
            FD_SET(fd_out, &read_fds);
        if (fd_err >= 0)
            FD_SET(fd_err, &read_fds);
        int maxfd = BOR_MAX(fd_out, fd_err);
        if (maxfd < 0)
            break;

        int sel_ret = select(maxfd + 1, &read_fds, NULL, NULL, NULL);
        if (sel_ret < 0){
            perror("select() failed:");
            exit(-1);
        }

        if (FD_ISSET(fd_out, &read_fds)){
            readBuf(&bout, fd_out);
            if (bout.eof)
                fd_out = -1;
            readExtractLines(e, &bout, "Picat out:", err);
        }
        if (FD_ISSET(fd_err, &read_fds)){
            readBuf(&berr, fd_err);
            if (berr.eof)
                fd_err = -1;
            readExtractLines(NULL, &bout, "Picat err:", err);
        }
    } while (1);

    readBufFree(&bout);
    readBufFree(&berr);
}

static void runPicat(pddl_endomorphism_fdr_csp_t *e,
                     const char *xml_file,
                     const char *picat_bin,
                     const char *picat_xcsp,
                     bor_iset_t *prune_op,
                     bor_err_t *err)
{
    int pipe_out[2], pipe_err[2];

    BOR_INFO(err, "Running picat: %s %s %s ...",
             picat_bin, picat_xcsp, xml_file);

    if (pipe(pipe_out) == -1) {
        perror("pipe() failed:");
        exit(-1);
    }
    if (pipe(pipe_err) == -1) {
        perror("pipe() failed:");
        exit(-1);
    }


    fflush(stdout);
    fflush(stderr);
    int pid = fork();
    if (pid < 0){
        perror("fork() failed:");
        exit(-1);

    }else if (pid == 0){
        close(1);
        close(2);
        dup2(pipe_out[1], 1);
        dup2(pipe_err[1], 2);
        close(pipe_out[0]);
        close(pipe_err[0]);
        execl(picat_bin, picat_bin, picat_xcsp, xml_file, NULL);
        fprintf(stderr, "execl(\"%s\", \"%s\", \"%s\") failed!\n",
                picat_bin, picat_xcsp, xml_file);
        exit(-1);

    }else{
        close(pipe_out[1]);
        close(pipe_err[1]);
        readPicat(e, pipe_out[0], pipe_err[0], prune_op, err);
        close(pipe_out[0]);
        close(pipe_err[0]);
        waitpid(pid, NULL, 0);
    }

    BOR_INFO(err, "Picat: %s %s %s DONE",
             picat_bin, picat_xcsp, xml_file);
}

void pddlPruneWithEndomorphism(const pddl_fdr_t *fdr,
                               bor_iset_t *prune_op,
                               bor_err_t *err)
{
    static const char *tmp_xml_file = "/tmp/prob.xml";
    static const char *_picat_bin = "/home/danfis/dev/csp/Picat/picat";
    static const char *_picat_xcsp = "/home/danfis/dev/csp/Picat/xcsp.pi";
    char picat_bin[128];
    char picat_xcsp[128];
    const char *picat_dir = NULL;
    if ((picat_dir = getenv("PICAT_DIR")) != NULL){
        sprintf(picat_bin, "%s/picat", picat_dir);
        sprintf(picat_xcsp, "%s/xcsp.pi", picat_dir);
    }else{
        strcpy(picat_bin, _picat_bin);
        strcpy(picat_xcsp, _picat_xcsp);
    }

    BOR_INFO2(err, "Endomorphisms on FDR ...");
    pddl_endomorphism_fdr_csp_t end;
    pddlEndomorphismFdrCspInit(&end, fdr);
    BOR_INFO2(err, "  FDR-Endomorphism constraints constructed.");

    FILE *fout = fopen(tmp_xml_file, "w");
    printXCSP(&end, fdr, fout);
    fclose(fout);
    BOR_INFO(err, "  XCSP file '%s' generated.", tmp_xml_file);
    runPicat(&end, tmp_xml_file, picat_bin, picat_xcsp, prune_op, err);
    if (end.map[0] < 0){
        BOR_INFO2(err, "endomorphism: UNSATISFIABLE");
        fprintf(stderr, "UNSAT!\n");

    }else{
        int diff = 0;
        for (int i = 0; i < end.cvar_size; ++i){
            if (i < fdr->var.global_id_size){
                if (end.map[i] != fdr->var.global_id_to_val[i]->val_id){
                    fprintf(stderr, "fact-%d <- %d [%d]\n",
                            i, end.map[i],
                            fdr->var.global_id_to_val[i]->val_id);
                    diff = 1;
                }
            }else{
                int id = cvarToOpId(&end, i);
                if (id != end.map[i]){
                    fprintf(stderr, "op-%d -> %d\n", id, end.map[i]);
                    diff = 1;
                }
            }
        }
        if (!diff)
            BOR_INFO2(err, "endomorphism: IDENTITY");
    }
    pddlEndomorphismFdrCspFree(&end);
}
