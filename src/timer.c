/***
 * Copyright (c)2018 Daniel Fiser <danfis@danfis.cz>,
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

#include "pddl/timer.h"

void pddlTimerPrintElapsed(const pddl_timer_t *t, FILE *out,
                           const char *format, ...)
{
    va_list ap; 

    va_start(ap, format);
    pddlTimerPrintElapsed2(t, out, format, ap);
    va_end(ap);
}

void pddlTimerStopAndPrintElapsed(pddl_timer_t *t, FILE *out,
                                  const char *format, ...)
{
    va_list ap; 

    pddlTimerStop(t);
    va_start(ap, format);
    pddlTimerPrintElapsed2(t, out, format, ap);
    va_end(ap);
}
