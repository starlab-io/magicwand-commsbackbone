from dg.Dockerfile import Dockerfiles
from dg.Settings import ApacheDefaults, ApacheCore, ApacheMPMPrefork, ApacheMPMWorker

# setup our templating master
dockerfiles = Dockerfiles()

# add some variations
dockerfiles.add_configs(
    ApacheDefaults(
        timeout=[100, 200, 300]
    ),
    ApacheCore()
)

# We need a value called dockerfiles in the file context
#dockerfiles = d.generate()
