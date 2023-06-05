/***
 * Copyright (c)2022 Daniel Fiser <danfis@danfis.cz>. All rights reserved.
 * This file is part of cpddl licensed under 3-clause BSD License (see file
 * LICENSE, or https://opensource.org/licenses/BSD-3-Clause)
 */

#include "internal.h"
#include "pddl/pddl_struct.h"
#include "pddl/require_flags.h"

unsigned pddlRequireFlagsToMask(const pddl_require_flags_t *flags)
{
    unsigned mask = 0u;
    unsigned flag = 1u;
    if (flags->strips)
        mask |= flag;
    flag <<= 1;
    if (flags->typing)
        mask |= flag;
    flag <<= 1;
    if (flags->negative_pre)
        mask |= flag;
    flag <<= 1;
    if (flags->disjunctive_pre)
        mask |= flag;
    flag <<= 1;
    if (flags->equality)
        mask |= flag;
    flag <<= 1;
    if (flags->existential_pre)
        mask |= flag;
    flag <<= 1;
    if (flags->universal_pre)
        mask |= flag;
    flag <<= 1;
    if (flags->conditional_eff)
        mask |= flag;
    flag <<= 1;
    if (flags->numeric_fluent)
        mask |= flag;
    flag <<= 1;
    if (flags->object_fluent)
        mask |= flag;
    flag <<= 1;
    if (flags->durative_action)
        mask |= flag;
    flag <<= 1;
    if (flags->duration_inequality)
        mask |= flag;
    flag <<= 1;
    if (flags->continuous_eff)
        mask |= flag;
    flag <<= 1;
    if (flags->derived_pred)
        mask |= flag;
    flag <<= 1;
    if (flags->timed_initial_literal)
        mask |= flag;
    flag <<= 1;
    if (flags->preference)
        mask |= flag;
    flag <<= 1;
    if (flags->constraint)
        mask |= flag;
    flag <<= 1;
    if (flags->action_cost)
        mask |= flag;
    flag <<= 1;
    if (flags->multi_agent)
        mask |= flag;
    flag <<= 1;
    if (flags->unfactored_privacy)
        mask |= flag;
    flag <<= 1;
    if (flags->factored_privacy)
        mask |= flag;

    return mask;
}

void pddlRequireFlagsSetADL(pddl_require_flags_t *flags)
{
    flags->strips = 1;
    flags->typing = 1;
    flags->negative_pre = 1;
    flags->disjunctive_pre = 1;
    flags->equality = 1;
    flags->existential_pre = 1;
    flags->universal_pre = 1;
    flags->conditional_eff = 1;
}

void pddlRequireFlagsPrintPDDL(const pddl_require_flags_t *flags, FILE *fout)
{
    fprintf(fout, "(:requirements\n");
    if (flags->strips)
        fprintf(fout, "    :strips\n");
    if (flags->typing)
        fprintf(fout, "    :typing\n");
    if (flags->negative_pre)
        fprintf(fout, "    :negative-preconditions\n");
    if (flags->disjunctive_pre)
        fprintf(fout, "    :disjunctive-preconditions\n");
    if (flags->equality)
        fprintf(fout, "    :equality\n");
    if (flags->existential_pre)
        fprintf(fout, "    :existential-preconditions\n");
    if (flags->universal_pre)
        fprintf(fout, "    :universal-preconditions\n");
    if (flags->conditional_eff)
        fprintf(fout, "    :conditional-effects\n");
    if (flags->numeric_fluent)
        fprintf(fout, "    :numeric-fluents\n");
    if (flags->object_fluent)
        fprintf(fout, "    :object-fluents\n");
    if (flags->durative_action)
        fprintf(fout, "    :durative-actions\n");
    if (flags->duration_inequality)
        fprintf(fout, "    :duration-inequalities\n");
    if (flags->continuous_eff)
        fprintf(fout, "    :continuous-effects\n");
    if (flags->derived_pred)
        fprintf(fout, "    :derived-predicates\n");
    if (flags->timed_initial_literal)
        fprintf(fout, "    :timed-initial-literals\n");
    if (flags->preference)
        fprintf(fout, "    :preferences\n");
    if (flags->constraint)
        fprintf(fout, "    :constraints\n");
    if (flags->action_cost)
        fprintf(fout, "    :action-costs\n");
    if (flags->multi_agent)
        fprintf(fout, "    :multi-agent\n");
    if (flags->unfactored_privacy)
        fprintf(fout, "    :unfactored-privacy\n");
    if (flags->factored_privacy)
        fprintf(fout, "    :factored-privacy\n");
    fprintf(fout, ")\n");
}
