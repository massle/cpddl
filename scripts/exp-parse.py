#!/usr/bin/env python3

import sys
import os
import re
import pickle
from pprint import pprint

pat_task_dir = re.compile(r'^[0-9]+$')
pat_prop_line = re.compile(r'^( *)([a-zA-Z0-9_-]+) ?= *(.+)$')

pat_cpddl_line = re.compile(r'^\[([0-9.]+)s ([0-9]+)MB\] (.*)$')

def parseProp(fin):
    # TODO
    data = {}
    for line in fin:
        match = pat_prop_line.match(line)
        assert(match is not None)
        prefix = match.group(1)
        key = match.group(2)
        val = eval(match.group(3))
        data[key] = val
    return data

def parseTaskProp(fn):
    with open(fn, 'r') as fin:
        return parseProp(fin)

def parseCpddlLog(fn):
    d = { 'strips-ops' : None,
          'strips-facts' : None
        }
    with open(fn, 'r') as fin:
        for line in fin:
            line = line.strip()
            m = pat_cpddl_line.match(line)
            if m is None:
                continue
            time = float(m.group(1))
            mem = int(m.group(2))
            line = m.group(3)
            if line.startswith('STRIPS: Number of Strips Operators: '):
                d['strips-ops'] = int(line.split()[-1])
            elif line.startswith('STRIPS: Number of Strips Facts: '):
                d['strips-facts'] = int(line.split()[-1])
    return d

def parseTaskOut(fn):
    with open(fn, 'r') as fin:
        for line in fin:
            print(line, end = '')

def main():
    data = {}
    for root, dirs, files in os.walk('.'):
        s = root.strip('/').split('/')
        if pat_task_dir.match(s[-1]) is not None:
            assert(len(s) >= 2)
            assert('task.prop' in files)
            if 'task.finished' not in files:
                print('{0:100s}'.format(root + ' not finished'), file = sys.stderr)
                continue
            print('{0:100s}'.format(root), end = '\r', file = sys.stderr)

            topkey = s[-2]
            taskdir = s[-1]
            if topkey not in data:
                data[topkey] = {}
            d = parseTaskProp(os.path.join(root, 'task.prop'))
            d['taskdir'] = taskdir
            d['taskroot'] = str(root)
            d['plan_files'] = [f for f in files \
                    if f.startswith('plan.out') or f.startswith('sas_plan')]

            d['exit_status'] = None
            if 'task.status' in files:
                d['exit_status'] = \
                    int(open(os.path.join(root, 'task.status'), 'r').read().strip())
            d['timeout'] = ('task.timeout' in files)
            d['memout'] = ('task.memout' in files)
            d['signum'] = None
            if 'task.signum' in files:
                d['signum'] = \
                    int(open(os.path.join(root, 'task.signum'), 'r').read().strip())

            d['time'] = float(open(os.path.join(root, 'task.time'), 'r').read().strip())

            d_cpddl = parseCpddlLog(os.path.join(root, 'task.err'))
            d.update(d_cpddl)

            #d['out'] = parseTaskOut(os.path.join(root, 'task.out'))
            # prop.out
            # task,out
            # task.err
            # tr.log

            taskkey = (d['bench_name'], d['domain_name'], d['problem_name'])
            assert(taskkey not in data[topkey])
            data[topkey][taskkey] = d

    print('{0:100s}'.format('Parsing DONE'), file = sys.stderr)

    with open('data.pickle', 'wb') as fout:
        pickle.dump(data, fout)
        print('Written data.pickle', file = sys.stderr)
    #pprint(data)

if __name__ == '__main__':
    sys.exit(main())
