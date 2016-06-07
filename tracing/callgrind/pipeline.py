'''Essentially a script file that runs through all of the components.'''

import sys

import call_binary
import extract_graphs
from common import config
import setup_test

def main(config_path):
    print "Getting Config"
    conf = config(config_path)
    outdir = conf['outdir']
    print "Running Binary"
    call_binary.run(config_path)
    print "Extracting Call Graphs"
    extract_graphs.main(outdir)
    print "Finished"

if __name__ == '__main__':
    if len(sys.argv) != 2:
        print 'Usage: python pipeline.py <config_path>'
        print 'Try: python pipeline.py test/ls/config.json'
    else:
        if sys.argv[1] == 'test/ls/config.json':
            print 'Setting up test environment'
            setup_test.main()
        main(*sys.argv[1:])
