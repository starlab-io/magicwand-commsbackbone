DockerGenerator build sets of Docker Images from a combination of defined
parameters and value ranges:

```sh
🚀  ./dg.py --build
Building patricknevindwyer/dg-image-00:latest
	+ build succeeded
Building patricknevindwyer/dg-image-01:latest
	+ build succeeded
Building patricknevindwyer/dg-image-02:latest
	+ build succeeded
```

# About

As we start instrumenting Apache and system resources to generate test and 
validation data, it has become useful to be able to vary certain settings 
and configuration options to create a multitude of systems under test. We 
can encode these variations in individual Dockerfiles that combine the settings 
and configurations relevant to testing.

Building these using some level of automation allows us to generate a 
collection of variable settings and config options at will. Once generated 
and built, the Docker images can be:

 - Instrumented and used to generate test data.
 - Deployed to test mitigations and attacks
 - Other?

# dg.py commands

The `dg.py` command executes a user defined _*DockerGenerator.py*_ file that
defines a set of configuration options with which to build Docker Images. 

```python
from dg.Dockerfile import Dockerfiles
from dg.Settings import ApacheDefaults, ApacheCore

# Define a new configuration set
dockerfiles = Dockerfiles()

# Generate three images, each with different connection timeouts
dockerfiles.add_configs(
    ApacheDefaults(
        timeout=[100, 200, 300]
    ),
    ApacheCore()
)
```

The _*DockerGenerator.py*_ file can contain any valid Python code, and so
long as it defines a module level _*dockerfiles*_ variable, `dg.py` can 
operate over this set of configurations in a number of ways.
 
## --list

Get a list of defined images. By default the images are not build by this
command. The list includes a Docker Hub compatible repository and tag, as
well as the local file system build target for the image:

```sh
🚀  ./dg.py --list
patricknevindwyer/dg-image-00:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00
patricknevindwyer/dg-image-01:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01
patricknevindwyer/dg-image-02:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02
```

## --info

The _--info_ command generates and displays metadata for each image defined by the
configuration options.

```sh
🚀  ./dg.py --info
repository: patricknevindwyer/dg-image-00
       tag: latest
     files: 2
        + (local) httpd-default.conf == /usr/local/apache2/conf/extras/httpd-default.conf (image)
        + (local) httpd.conf == /usr/local/apache2/conf/httpd.conf (image)
     built: True
repository: patricknevindwyer/dg-image-01
       tag: latest
     files: 2
        + (local) httpd-default.conf == /usr/local/apache2/conf/extras/httpd-default.conf (image)
        + (local) httpd.conf == /usr/local/apache2/conf/httpd.conf (image)
     built: True
repository: patricknevindwyer/dg-image-02
       tag: latest
     files: 2
        + (local) httpd-default.conf == /usr/local/apache2/conf/extras/httpd-default.conf (image)
        + (local) httpd.conf == /usr/local/apache2/conf/httpd.conf (image)
     built: True
```

## --build

Images can be built from the configurations using the _--build_ command:

```sh
🚀  ./dg.py --build
Building patricknevindwyer/dg-image-00:latest
	+ build succeeded
Building patricknevindwyer/dg-image-01:latest
	+ build succeeded
Building patricknevindwyer/dg-image-02:latest
	+ build succeeded
```

## --push

Generated images can be pushed to Docker Hub with the _--push_ command:

```sh
🚀  ./dg.py --push
Pushing patricknevindwyer/dg-image-00:latest
	+ push succeeded
Pushing patricknevindwyer/dg-image-01:latest
	+ push succeeded
Pushing patricknevindwyer/dg-image-02:latest
	+ push succeeded
```

## --add-tags

Extra tags can be added to one or more images at the same time using the
_--add-tags_ command:

```sh
🚀  ./dg.py --add-tags --new-tags test 1.1
Tagging patricknevindwyer/dg-image-00:latest
	+ 2 tags added
Tagging patricknevindwyer/dg-image-01:latest
	+ 2 tags added
Tagging patricknevindwyer/dg-image-02:latest
	+ 2 tags added
```

When working with tags it can be especially useful to apply filters:

```sh
🚀  ./dg.py --add-tags --new-tags foo -r patricknevindwyer/dg-image-01
Tagging patricknevindwyer/dg-image-01:latest
	+ 1 tags added
```

## --version

Gather version information about the local Docker environment

```sh
🚀  ./dg.py --version
docker-compose - 1.7.1, build 0a9ab35
docker - 1.11.2, build b9f10c9
docker-machine - 0.7.0, build a650a40
```

## Filtering

Command line filters can be used to select images based on Repository name, image tag, and configuration
metadata. These filters can be combined with any of the `dg.py` commands (list, info, build, push, tag).

A single DockerGenerator.py file

```python
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
```

can define a large quantity of images:

```sh
🚀  ./dg.py -f DockerGeneratorLarge.py --list
patricknevindwyer/dg-image-00:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00
patricknevindwyer/dg-image-01:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01
patricknevindwyer/dg-image-02:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02
patricknevindwyer/dg-image-03:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-03
...snip...
patricknevindwyer/dg-image-464:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-464
patricknevindwyer/dg-image-465:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-465
patricknevindwyer/dg-image-466:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-466
patricknevindwyer/dg-image-467:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-467
```

Filtering using the repository name, tag, and metadata allows fine-grain control over selecting
smaller sets of images:

```sh
🚀  ./dg.py -f DockerGeneratorLarge.py --list --repo patricknevindwyer/dg-image-450
patricknevindwyer/dg-image-450:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-450
```

By enabling _--patterns_, you can match subsets of repository names:

```sh
🚀  ./dg.py --list --pattern -r image-3?1 -f DockerGeneratorLarge.py
patricknevindwyer/dg-image-301:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-301
patricknevindwyer/dg-image-311:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-311
patricknevindwyer/dg-image-321:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-321
patricknevindwyer/dg-image-331:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-331
patricknevindwyer/dg-image-341:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-341
patricknevindwyer/dg-image-351:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-351
patricknevindwyer/dg-image-361:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-361
patricknevindwyer/dg-image-371:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-371
patricknevindwyer/dg-image-381:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-381
patricknevindwyer/dg-image-391:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-391
```

or even query for images with specific metadata:

```sh
🚀  ./dg.py --list --pattern -m timeout:100 keepAliveTimeout:5 startServers:5 maxClients:50 -f DockerGeneratorLarge.py
patricknevindwyer/dg-image-00:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00
patricknevindwyer/dg-image-48:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-48
patricknevindwyer/dg-image-96:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-96
patricknevindwyer/dg-image-144:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-144
```


The repository and tag filters can be combined with any action command:

```sh
🚀  ./dg.py --repo patricknevindwyer/dg-image-01 --build
Building patricknevindwyer/dg-image-01:latest
	+ build succeeded
```

### Patterns for Repositories and Tags

When querying repository and tag values with a filter (and with _--pattern_ enabled), you can use a limited
 set of query sigils in the query value:

   Sigil  |  RegExp Equiv  |     Example    |  Description
 ---------|----------------|----------------|----------------
     ?    |        .?      |    image-3?1   |  Match any character between 3 & 1
     *    |        .*      |    dg-*-300    |  Match none or more characters
     +    |        .+      |    image-3+    |  Match one or more characters

### Metadata Queries

Each image stores it's configuration options (as defined in the _DockerGenerator.py_ file) as metadata that
can be queried. Metadata queries use the _--pattern_ and _--metadata_ options. You can query multiple pieces
of metadata at a time, with the results filtered to match _*all*_ queries. Metadata keys and values are
separated by a colon:

```sh
🚀  ./dg.py --list --pattern --metadata timeout:100 keepAliveTimeout:5 startServers:5 maxClients:50 -f DockerGeneratorLarge.py
patricknevindwyer/dg-image-00:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00
patricknevindwyer/dg-image-48:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-48
patricknevindwyer/dg-image-96:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-96
patricknevindwyer/dg-image-144:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-144
```
## Additional Options

There are a handful of additional useful flags for `dg.py`:

### --force-rebuild

This flag will cause a rebuild of the selected images before any command is run:

```sh
🚀  ./dg.py --force-rebuild --list
Building patricknevindwyer/dg-image-00:latest
	+ build succeeded
Building patricknevindwyer/dg-image-01:latest
	+ build succeeded
Building patricknevindwyer/dg-image-02:latest
	+ build succeeded
patricknevindwyer/dg-image-00:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00
patricknevindwyer/dg-image-01:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01
patricknevindwyer/dg-image-02:latest  == /Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02
```


### --no-cache

Ignore the Docker Image cache when (re)building images:

```sh
🚀  ./dg.py --no-cache --build
Building patricknevindwyer/dg-image-00:latest
	+ build succeeded
Building patricknevindwyer/dg-image-01:latest
	+ build succeeded
Building patricknevindwyer/dg-image-02:latest
	+ build succeeded
```

### --verbose

Print detailed debug/command information while executing:

```sh
🚀  ./dg.py --no-cache --repo patricknevindwyer/dg-image-00 --build --verbose
Outputting in VERBOSE mode
{'dockerGenerator': ['DockerGenerator.py'], 'verbose': True, 'add_tags': [], 'repositories': ['patricknevindwyer/dg-image-00'], 'tags': [], 'force_rebuild': False, 'no_cache': True, 'action': 'build'}
Using template [./templates/DockerfileTemplate]
	* found Dockerfile template [./templates/DockerfileTemplate]
	* Adding template [httpd-default]
	* Adding template [httpd-core]
	! Creating variant [image-00]
	+ Generating template output for [httpd-default] in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00/httpd-default.conf]
	+ Generating template output for [httpd-core] in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00/httpd.conf]
	+ Creating Dockerfile in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00/Dockerfile]
	+ Creating metadata in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-00/metadata.json]
	! Creating variant [image-01]
	+ Generating template output for [httpd-default] in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01/httpd-default.conf]
	+ Generating template output for [httpd-core] in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01/httpd.conf]
	+ Creating Dockerfile in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01/Dockerfile]
	+ Creating metadata in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-01/metadata.json]
	! Creating variant [image-02]
	+ Generating template output for [httpd-default] in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02/httpd-default.conf]
	+ Generating template output for [httpd-core] in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02/httpd.conf]
	+ Creating Dockerfile in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02/Dockerfile]
	+ Creating metadata in [/Users/patrick.dwyer/projects/magicwand/docker-generator/images/image-02/metadata.json]
Building patricknevindwyer/dg-image-00:latest
Sending build context to Docker daemon 20.48 kB
Step 1 : FROM patricknevindwyer/pinned-apache:latest
 ---> a5a85b7026e6
Step 2 : MAINTAINER patricknevindwyer@gmail.com
 ---> Running in 069878ad523e
 ---> ef62512dd658
Removing intermediate container 069878ad523e
Step 3 : USER root
 ---> Running in 8d16382e3aa0
 ---> 9217611fc363
Removing intermediate container 8d16382e3aa0
Step 4 : COPY httpd-default.conf /usr/local/apache2/conf/extras/httpd-default.conf
 ---> 6395289fe4d8
Removing intermediate container a09b9c847102
Step 5 : COPY httpd.conf /usr/local/apache2/conf/httpd.conf
 ---> 679109c0af65
Removing intermediate container d85017cd6979
Step 6 : RUN chmod ugo+r /usr/local/apache2/conf/extras/httpd-default.conf
 ---> Running in 043e99a0150b
 ---> 357a0c02844b
Removing intermediate container 043e99a0150b
Step 7 : RUN chmod ugo+r /usr/local/apache2/conf/httpd.conf
 ---> Running in e7d8ad189fde
 ---> 3ab7f3fc55e9
Removing intermediate container e7d8ad189fde
Step 8 : EXPOSE 80
 ---> Running in 89b9e5abbffa
 ---> e550bcfffcb5
Removing intermediate container 89b9e5abbffa
Step 9 : VOLUME /var/log/apacheperf /root/output
 ---> Running in 7f595a293806
 ---> 526768bd21e5
Removing intermediate container 7f595a293806
Step 10 : CMD start-pinned-apache.sh
 ---> Running in 9673da23e38f
 ---> 550486459e49
Removing intermediate container 9673da23e38f
Successfully built 550486459e49

	+ build succeeded
```

### --file

By default `dg.py` will look for a _DockerGenerator.py_ file in the local
directory. To use an alternative _DockerGenerator.py_ file, use the _--file_ flag:

```sh
🚀  ./dg.py --list | wc -l
     3
🚀  ./dg.py --list -f DockerGeneratorLarge.py | wc -l 
     468
```

# DockerGenerator.py Files

The _DockerGenerator.py_ file is a standard Python file, and can
contain any valid Python. `dg.py` loads this file through an _*exec*_
call, so any top level code in the _DockerGenerator.py_ file will
be executed. 

The code in _DockerGenerator.py_ can do anything you want, so long
as the variable _dockerfiles_ is defined at the root level of the
file. The _dockerfiles_ variable should be an Instance of the
_dg.Dockerfile.Dockerfiles_ object.

All _Dockerfiles_ objects need at the very least an _ApacheCore_
configuration object, which means the most basic valid _DockerGenerator.py_
is:
 
```python
from dg.Dockerfile import Dockerfiles
from dg.Settings import ApacheCore

# setup our templating master
dockerfiles = Dockerfiles()

# add some basic variations of connection timeout
dockerfiles.add_configs(
    ApacheCore()
)
```

# Configuration Options

Each of the available Configuration classes defines a set of Apache
options that can be used to define a range of test configurations. The
configuration objects are used by `dg.py` to fill in various configuration
templates used by Apache (you can see which files are generated using the
`dg.py --info` command, which lists the files generated for each image
by the current configuration).

All configuration options are declared as part of constructing each
individual configuration object. See the _DockerGenerator*.py_ files
for examples.

## Apache Core

_*import*_ dg.Settings.ApacheCore

There are no options to configure for the ApacheCore configuration
class - it merely defines and templates the root Apache _httpd.conf_
file.

## ApacheMPM - Prefork

_*import*_ dg.settings.ApacheMPMPrefork

Define options for the Apache _*Prefork*_ run mode. While it is
possible to define a _Dockerfiles_ group with multiple MPM run modes,
the behavior of the resulting Apache will be undefined.

option              |  default  |  type
--------------------|-----------|--------
startServers        |  [5]      | Integer
minSpareServers     |  [5]      | Integer
maxSpareServers     |  [10]     | Integer
maxClients          |  [150]    | Integer
maxRequestsPerChild |  [0]      | Integer


## ApacheMPM - Worker

_*import*_ dg.settings.ApacheMPMWorker

Define options for the Apache _*Worker*_ run mode. While it is
possible to define a _Dockerfiles_ group with multipl MPM run modes,
the behavior of the resulting Apache will be undefined.

option              |  default  |  type
--------------------|-----------|--------
startServers        |  [2]      |  Integer
maxClients          |  [150]    |  Integer
minSpareThreads     |  [25]     |  Integer
maxSpareThreads     |  [75]     |  Integer
threadsPerChild     |  [25]     |  Integer
maxRequestsPerChild |  [0]      |  Integer


## Apache Default Settings

_*import*_ dg.settings.ApacheDefaults

Define standard Apache connection control options.

option               |  default  |  type
---------------------|-----------|--------
timeout              | [300]     | Integer
keepAlive            | [True]    | Boolean
maxKeepAliveRequests | [100]     | Integer
keepAliveTimeout     | [5]       | Integer