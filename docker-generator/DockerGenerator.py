from dg.Dockerfile import Dockerfiles
from dg.Settings import ApacheDefaults, ApacheCore

# setup our templating master
dockerfiles = Dockerfiles()

# add some basic variations of connection timeout
dockerfiles.add_configs(
    ApacheDefaults(
        timeout=[100, 200, 300]
    ),
    ApacheCore()
)