#!/usr/bin/python
from dg import Settings, Dockerfile
import argparse
import sys

def findDockerGenerator(args):
    """
    Find the DockerGenerator.py file that builds our image list. It is most likely
    called DockerGenerator.py. We may have an alternate filename in the args.dockerGenerator, so
    we need to check that it ends with a ".py" extension

    :param args: Runtime arguments to the program
    :return:
    """
    dockerGenerator = args["dockerGenerator"]
    if not dockerGenerator.endswith(".py"):
        print "!ERROR DockerGenerator file must have the .py extension"
        return None

    dg_module = None
    try:
        exec("import %s as dg_module" % (dockerGenerator.replace(".py", "")))
    except ImportError as ie:
        print "Error finding DockerGenerator file: ", ie

    return dg_module


def action_list(dockerfiles, opts):
    """
    List the docker files by name and location.

    :param dockerfiles: List of dg.Dockerfile::Dockerfile objects
    :param opts: Runtime options
    :return: None
    """

    for dockerfile in dockerfiles.generate(pretend=True, verbose=opts["verbose"]):
        print "%s  == %s" % (dockerfile.name(), dockerfile.imagedir)


def getOptions():
    """
    Get the runtime options
    :return:
    """
    parser = argparse.ArgumentParser()

    # What are we trying to do?
    parser.add_argument("-l", "--list", action="store_const", const="list", dest="action")
    parser.add_argument("-i", "--info", action="store_const", const="info", dest="action")
    parser.add_argument("-b", "--build", action="store_const", const="build", dest="action")

    # what's our DockerGenerator file?
    parser.add_argument("-f", "--file", dest="dockerGenerator", nargs=1, default="DockerGenerator.py")

    # set the verbosity
    parser.add_argument("-v", "--verbose", action="store_true", dest="verbose", default=False)
    return parser.parse_args()


if __name__ == "__main__":

    args = vars(getOptions())

    if args["verbose"]:
        print "Outputting in VERBOSE mode"

    # find our user configured Dockerfile objects
    dockerfiles = findDockerGenerator(args).dockerfiles
    if dockerfiles is None:
        sys.exit(1)

    # build our dispatch map
    dispatchmap = {
        "list": action_list
    }

    # try and dispatch
    if args["action"] is None:
        print "! No action specified, bailing out..."
        sys.exit(1)
    elif args["action"] not in dispatchmap:
        print "! Unknown action [%s], maybe it isn't implemented yet?" % (args["action"], )
    ret = dispatchmap[args["action"]](dockerfiles, args)
