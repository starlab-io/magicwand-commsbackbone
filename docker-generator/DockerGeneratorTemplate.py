from dg.Dockerfile import Dockerfiles
from dg.Settings import ApacheCore

# setup our templating master
dockerfiles = Dockerfiles()

# add some basic variations of connection timeout
dockerfiles.add_configs(
    ApacheCore()
)