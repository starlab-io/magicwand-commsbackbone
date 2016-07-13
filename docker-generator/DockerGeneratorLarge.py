from dg.Dockerfile import Dockerfiles
from dg.Settings import ApacheDefaults, ApacheCore, ApacheMPMPrefork

# setup our templating master
dockerfiles = Dockerfiles()

# add some variations
dockerfiles.add_configs(
    ApacheDefaults(
        timeout=[100, 200, 300],
        keepAlive=[True, False],
        maxKeepAliveRequests=[100, 200, 300],
        keepAliveTimeout=[5, 10, 15, 20]
    ),
    ApacheMPMPrefork(
        startServers=[5, 10, 20],
        maxClients=[50, 100, 150, 200]
    ),
    ApacheCore()
)