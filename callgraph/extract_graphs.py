'''Take the callgrind outputs and transform them into directed graphs with time in each state.'''

import os
import sys
import logging
from multiprocessing.pool import ThreadPool

import pygraphviz
import networkx

from common import config, call, networkx_dump, imap, fun

GPROF = '/home/davidslater/git/gprof2dot/gprof2dot.py'

def main(outdir):
    if not os.path.exists(outdir):
        raise ValueError('target directory does not exist')
    logging.basicConfig(filename=os.path.join(outdir, 'dot.log'), level=logging.DEBUG)
    conf = config(os.path.join(outdir, 'config.json'))

    # map the callgrind outputs to dot files
    call_dir = os.path.join(outdir, 'callgrind')
    output_dir = os.path.join(outdir, 'dot')
    #network_dir = os.path.join(outdir, 'networkx')
    for directory in (output_dir,): #(output_dir, network_dir):
        if not os.path.exists(directory):
            logging.debug('Creating directory {}'.format(directory))
            os.mkdir(directory)

    # create iterable
    iterable = []
    #networkx_iterable = []
    prefix = [sys.executable, GPROF, '--format=callgrind']
    for output in conf['outputs']:
        for i in range(conf['count']):
            output_run = output + '.{}'.format(i)
            source = os.path.join(call_dir, output_run)
            target = os.path.join(output_dir, output_run.replace('callgrind', 'dot'))
            args = prefix + ['--output={}'.format(target), source]
            iterable.append(args)
            logging.debug('Added: {}'.format(args))

            #final = os.path.join(network_dir, output_run.replace('callgrind', 'networkx.json'))
            #networkx_iterable.append((networkx_fun, {'input': target, 'output': final}))

    # transform callgrind outputs to .dot files
    threadpool = ThreadPool(conf.get('nproc')) # None defaults to cpu count
    logging.debug('Transforming callgrind outputs to DOT files')
    imap(threadpool, logging, call, iterable)
    #for result, err in threadpool.imap(call, iterable):
    #    if err:
    #        logging.error(result)
    #    else:
    #        logging.debug(result)

    #logging.debug('Transforming DOT files to networkx graphs in json format')
    #imap(threadpool, logging, fun, networkx_iterable)

    logging.debug('All transforms have been run')
    threadpool.close()
    threadpool.join()

def networkx_fun(kwargs):
    inp, out = kwargs['input'], kwargs['output']
    Gtmp = pygraphviz.AGraph(inp)
    G = networkx.DiGraph(Gtmp)
    networkx_dump(G, out)
    return '{} successfully transformed to {}'.format(inp, out)

if __name__ == '__main__':
    main(*sys.argv[1:])
