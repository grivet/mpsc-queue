#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright(c) 2023 Gaëtan Rivet

import csv
import optparse
import os
import statistics
import shutil
import subprocess
import sys
import signal
import time

options = None
parser = None
commands = []


def sigint_handler(sig, frame):
    exit(0)
signal.signal(signal.SIGINT, sigint_handler)


def _sh(*args, **kwargs):
    shell = len(args) == 1
    if kwargs.get('capture', False):
        proc = subprocess.Popen(args, stdout=subprocess.PIPE, shell=shell)
        return proc.stdout.readlines()
    elif kwargs.get('check', True):
        subprocess.check_call(args, shell=shell)
    else:
        subprocess.call(args, shell=shell)


def eprint(*args, **kwargs):
    print(*args, file=sys.stderr, **kwargs)


def progressbar(it, prefix="", size=60, out=sys.stdout):
    count = len(it)
    def show(j):
        x = int(size*j/count)
        print("{}[{}{}] {}/{}".format(prefix, "#"*x, "."*(size-x), j, count),
                end='\r', file=out, flush=True)
    show(0)
    for i, item in enumerate(it):
        yield item
        show(i+1)
    print("\r", flush=True, file=out)


def printResult(result, human=True):
    maxlen = max([len(n) for n in result.keys()])
    for name, values in result.items():
        if human:
            mean = statistics.mean(values)
            stdev = statistics.pstdev(values)
            print('{: >{}s}: mean {:6.1f} | stdev {:6.1f} | min {:5.0f} | max {:5.0f}'.format(
                    name, maxlen, mean, stdev, min(values), max(values)))
        else:
            print('%s,%s' % (name, ','.join([str(x) for x in values])))


def run(args):
    n = options.n or 10
    test = ' '.join(args)
    result = dict()
    eprint('Running %d times %s:' % (int(n), test))
    for i in progressbar(range(n), out=sys.stderr):
        for line in _sh(test, capture=True):
            k, v = line.decode().strip().split(',')
            if k not in result:
                result[k] = []
            result[k] += [int(v)]
    printResult(result, options.human)


commands.append(run)


def loadResult(file: str):
    result = dict()
    reader = csv.reader(file)
    for row in reader:
        key = row.pop(0)
        result[key] = []
        for value in row:
            result[key] += [int(value)]
    return result


def show(args):
    for file in args:
        print('%s:' % file)
        with open(file, newline='') as csvfile:
            result = loadResult(csvfile)
        printResult(result)


commands.append(show)


def printResultComparison(a, b):
    maxlen = max([len(n) for n in a.keys()])

    def delta(x1: float, x2: float):
        diff = x2 - x1
        pc = x1 if x1 == 0 else (diff / x1) * 100
        return '{:+6.1f}% {:18s}'.format(pc,
                '({:.1f} -> {:.1f})'.format(x1, x2))

    def printDelta(name: str, v: list, w: list):
        m1 = statistics.mean(v)
        s1 = statistics.pstdev(v)
        m2 = statistics.mean(w)
        s2 = statistics.pstdev(w)
        print('{: >{}s}: mean {:s}\tstdev {:s}'.format(
            name, maxlen,
            delta(m1, m2), delta(s1, s2)))

    for k in a.keys():
        printDelta(k, a[k], b[k])


def compare(args):
    if len(args) < 2:
        eprint("Using 'compare' requires at least 2 files.")
        return

    for pairs in list(zip(args, args[1:])):
        print('Comparing %s -> %s:' % (pairs[0], pairs[1]))
        with open(pairs[0], newline='') as a:
            resultA = loadResult(a)
        with open(pairs[1], newline='') as b:
            resultB = loadResult(b)
        printResultComparison(resultA, resultB)


commands.append(compare)


def doc(args):
    parser.print_help()
    print("""
This program is used to batch runs of microbenchmarks and compare the results.

The 'run' command expects a command provided after '--' parameters.
This command must output its results in a CSV format as follow:

  test-section-0,value0
  test-section-1,value1
  ...
  test-section-N,valueN

Each runs is stored as a new column.

The outputs of several runs can then be compared between each
other with the 'compare' command, e.g. :

  ./bench.py run -- command -csv > run.1.csv
  ./bench.py run -- command -csv > run.2.csv
  ./bench.py compare run.{1,2}.csv

The mean and stdev of each rows is computed, then the comparison
shows the difference between the two measures.""")
    sys.exit(0)


commands.append(doc)


def main():
    global options
    global parser

    description = 'Benchmark runner and statistics generator.'
    cmd_names = [c.__name__ for c in commands]
    usage = 'usage: %prog' + ' [%s] [options] -- ...' % '|'.join(cmd_names)
    parser = optparse.OptionParser(usage=usage, description=description)

    parser.add_option('-n', '--n', dest='n', metavar='N', type='int',
                      help='Run command N times')
    parser.add_option('-H', '--human', dest='human', action='store_true',
                      default=False, help='Format output for a human reader')

    options, args = parser.parse_args()

    if len(args) == 0:
        print('No command provided.')
        sys.exit(1)

    command = args.pop(0)
    if command not in cmd_names:
        print('Unknown command ' + command)
        doc()

    for cmd in commands:
        if command == cmd.__name__:
            cmd(args)


if __name__ == '__main__':
    main()
