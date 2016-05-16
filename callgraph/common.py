'''Common functions used by multiple processes'''

import json
import traceback
import os
import subprocess

import networkx

def config(path='config.json'):
    with open(path) as f:
        return json.load(f)

def set_config(conf, path='config.json'):
    with open(path, 'w') as f:
        json.dump(conf, f)

# redirect callgrind outputs to /dev/null
f = open(os.devnull, 'w') 

def call(args):
    try:
        result_code = subprocess.call(args, stdout=f, stderr=f)
        return 'Done with {}, result {}'.format(' '.join(args), result_code), False
    except:
        return traceback.format_exc(), True

def fun(args):
    inner_fun, kwargs = args
    try:
        results = inner_fun(kwargs)
        return results, False
    except:
        return traceback.format_exc(), True

def imap(threadpool, logging, func, iterable):
    for result, err in threadpool.imap(func, iterable):
        if err:
            logging.error(result)
        else:
            logging.debug(result)

def networkx_dump(graph, filepath):
    data = {'class': graph.__class__.__name__,
            'nodes': graph.nodes(data=True),
            'edges': graph.edges(data=True)}
    with open(filepath, 'w') as f:
        json.dump(data, f)

def networkx_load(filepath):
    with open(filepath) as f:
        data = json.load(f)
    graph = getattr(networkx, data['class'])() # init class
    graph.add_nodes_from(data['nodes']) # add nodes
    graph.add_edges_from(data['edges']) # add edges
    return graph
