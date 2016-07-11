As we start instrumenting Apache and system resources to generate test and validation data, it has become useful to be able to vary certain settings and configuration options to create a multitude of systems under test. We can encode these variations in individual Dockerfiles that combine the settings and configurations relevant to testing.

Building these using some level of automation will allow us to start with a non-exhaustive set of variable settings and config options. Once generated and built, the test systems (Docker images) can be instrumented and generate test data.

A set of starting configuration options to work from:

 - Apache run mode (event, pre-fork, etc)
 - Apache version ? 
 - Process/Thread count
 - Memory allocated to Apache


The automated build out should also track the images and store metadata related to each build:

 - tags
 - versions (Apache, Ubuntu, etc)
 - build date
 - Configuration data
 
 # Generating New Configurations
 
 For each configuration we generate, we create a specific directory within the "images" directory, complete with
 `Dockerfile`, _Apache_ configuration, and a _metadata.json_ file. Where possible, the configuration options are used to create the
 combinatorial set of possible configurations.
 
 # Specifying Configuration Variants
 
 Each variable configuration option is defined within the dg.conf JSON file within this directory. Each set of
 options for a configuration variable (say, _threadCount_) has a corresponding implementation function within
 the `generate-docker-images` command. 
 
 # Using `generate-docker-images`
 
 The `generate-docker-images` command provides control over the process of generating, building, tagging, and pushing
 new images.