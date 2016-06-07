'''Use callgrind on target binary to extract call graph features'''

import sys
import os
import itertools
from multiprocessing.pool import ThreadPool
import logging

from common import config, set_config, call

#def run(config_path, callgrind='valgrind --tool=callgrind --callgrind-out-file={} --compress-strings=no'):
def run(config_path, callgrind='valgrind --tool=callgrind --callgrind-out-file={}'):
    '''Run a series of processes with callgrind to generate output'''
    # get config
    conf = config(config_path)
    outdir = conf['outdir'] 
    command = conf['command']
    args = conf['args']
    targets = conf['targets']
    count = conf['count']
    
    # log to output directory
    if not os.path.exists(outdir):
        os.mkdir(outdir)
    logging.basicConfig(filename=os.path.join(outdir, 'callgrind.log'), level=logging.DEBUG)
    logging.debug(conf)
    
    # create command line arguments and output directory 
    
    runs = [x for x in itertools.product(*[x for x in [[command], args, targets] if x])]
    runs = [[i for i in x if i] for x in runs] # filter out empty args
    outputs = ['{}.callgrind'.format(x) for x in range(len(runs))]
    for directory in (os.path.join(outdir, 'callgrind'),):
        if not os.path.exists(directory):
            logging.debug('Creating directory {}'.format(directory))
            os.mkdir(directory)
    
    # write new config (including command line args) to output directory
    conf['runs'] = runs
    conf['outputs'] = outputs
    set_config(conf, os.path.join(outdir, 'config.json')) 

    logging.debug('Creating iterable')
    iterable = []
    for output, run in zip(outputs, runs):
        for i in range(count):
            prefix = callgrind.split()
            prefix[2] = prefix[2].format(os.path.join(outdir, 'callgrind', output + '.{}'.format(i)))
            prefix.extend(run)
            logging.debug('Adding {}'.format(' '.join(prefix)))
            iterable.append(prefix)
    
    logging.debug('Running through all calls')
    threadpool = ThreadPool(conf.get('nproc')) # None defaults to cpu count
    for result, err in threadpool.imap(call, iterable):
        if err:
            logging.error(result)
        else:
            logging.debug(result)

    logging.debug('All calls have finished')
    threadpool.close()
    threadpool.join()

def main(outdir, callgrind='valgrind --tool=callgrind --callgrind-out-file={}'):
    '''Run a series of processes with callgrind to generate output'''
    if not os.path.exists(outdir):
        os.mkdir(outdir)
    logging.basicConfig(filename=os.path.join(outdir, 'callgrind.log'), level=logging.DEBUG)
    
    # get config
    conf = config()
    logging.debug(conf)
    command = conf['command']
    args = conf['args']
    targets = conf['targets']
    count = conf['count']
    runs = [x for x in itertools.product([command], args, targets)]
    runs = [[i for i in x if i] for x in runs] # filter out empty args
    conf['runs'] = runs
    outputs = ['{}.callgrind'.format(x) for x in range(len(runs))]
    conf['outputs'] = outputs

    for directory in (os.path.join(outdir, 'callgrind'),):
        if not os.path.exists(directory):
            logging.debug('Creating directory {}'.format(directory))
            os.mkdir(directory)
    
    set_config(conf, os.path.join(outdir, 'config.json')) 

    logging.debug('Running through all calls')
    threadpool = ThreadPool(conf.get('nproc')) # None defaults to cpu count
    iterable = []
    for output, run in zip(outputs, runs):
        for i in range(count):
            prefix = callgrind.split()
            prefix[2] = prefix[2].format(os.path.join(outdir, 'callgrind', output + '.{}'.format(i)))
            prefix.extend(run)
            logging.debug('Starting {}'.format(' '.join(prefix)))
            iterable.append(prefix)
    
    for result, err in threadpool.imap(call, iterable):
        if err:
            logging.error(result)
        else:
            logging.debug(result)

    logging.debug('All calls have been input')
    threadpool.close()
    threadpool.join()

if __name__ == '__main__':
    run(*sys.argv[1:])
