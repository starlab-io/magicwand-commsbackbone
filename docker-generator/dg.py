#!/usr/bin/python
# -*- coding: UTF-8 -*-


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
        if repo_matches_filter(dockerfile, opts["repositories"]):
            if tag_matches_filter(dockerfile, opts["tags"]):
                print "%s  == %s" % (dockerfile.name(), dockerfile.imagedir)


def action_info(dockerfiles, opts):
    """
    Get per image information, optionally about one or more images.

    :param dockerfiles:
    :param opts:
    :return:
    """

    for dockerfile in dockerfiles.generate(pretend=True, verbose=opts["verbose"]):
        if repo_matches_filter(dockerfile, opts["repositories"]):
            if tag_matches_filter(dockerfile, opts["tags"]):

                # build our information set
                print "repository: %s" % (dockerfile.repository(), )
                print "       tag: %s" % (dockerfile.tag, )
                print "     files: %d" % (len(list(dockerfile.files())))
                for files in dockerfile.files(local=True, image=True):
                    print "        + (local) %s == %s (image)" % (files[0], files[1])
                print "     built: %s" % (dockerfile.built(),)


def action_build(dockerfiles, opts):
    """
    Build the matching dockerfile objects.

    :param dockerfiles:
    :param opts:
    :return:
    """
    for dockerfile in dockerfiles.generate(pretend=False, verbose=opts["verbose"]):
        if repo_matches_filter(dockerfile, opts["repositories"]):
            if tag_matches_filter(dockerfile, opts["tags"]):
                print "Building %s" % (dockerfile.name())
                if dockerfile.build(verbose=opts["verbose"]):
                    print "\t+ build succeeded"
                else:
                    print "\t- build failed"
                    if not opts["verbose"]:
                        print "   - run again with --verbose for more detailed error"


def repo_matches_filter(dockerfile, repolist):
    """
    Determine if the given dockerfile matches the repository list specified on
    the command line.

    :param dockerfile: Dockerfile object
    :param repolist: List of repository names or empty list
    :return: True or False
    """
    if repolist == [] or repolist is None:
        return True

    return dockerfile.repository() in repolist


def tag_matches_filter(dockerfile, taglist):
    """
    Determine if the given Dockfile matches the tag list specified on the
     command line.

    :param dockerfile: Dockerfile object
    :param taglist: List of tags or empty list
    :return: True or False
    """
    if taglist == [] or taglist is None:
        return True

    return dockerfile.tag in taglist


def getOptions():
    """
    Get the runtime options
    :return:
    """
    parser = argparse.ArgumentParser()

    # What are we trying to do?
    parser.add_argument("-l", "--list", action="store_const", const="list", dest="action", help="List the Docker images and paths that are described in the DockerGenerator.py")
    parser.add_argument("-i", "--info", action="store_const", const="info", dest="action", help="View information about each Dockerfile and it's built components")
    parser.add_argument("-b", "--build", action="store_const", const="build", dest="action")

    # control various aspects of our operations by repo or tag
    parser.add_argument("-r", "--repo", dest="repositories", nargs="*", help="Filter to the specified set of repositories", default=[])
    parser.add_argument("-t", "--tag", dest="tags", nargs="*", help="Filter to the specified set of tags", default=[])

    # what's our DockerGenerator file?
    parser.add_argument("-f", "--file", dest="dockerGenerator", nargs=1, default="DockerGenerator.py")

    # set the verbosity
    parser.add_argument("-v", "--verbose", action="store_true", dest="verbose", default=False)
    return parser.parse_args()


if __name__ == "__main__":

    args = vars(getOptions())

    if args["verbose"]:
        print "Outputting in VERBOSE mode"
        print args

    # find our user configured Dockerfile objects
    dockerfiles = findDockerGenerator(args).dockerfiles
    if dockerfiles is None:
        sys.exit(1)

    # build our dispatch map
    dispatchmap = {
        "list": action_list,
        "info": action_info,
        "build": action_build
    }

    # try and dispatch
    if args["action"] is None:
        print "! No action specified, bailing out..."
        sys.exit(1)
    elif args["action"] not in dispatchmap:
        print "! Unknown action [%s], maybe it isn't implemented yet?" % (args["action"], )
        sys.exit(1)
    ret = dispatchmap[args["action"]](dockerfiles, args)
