"""
Operations on Dockerfile templates and image directories.
"""
from .Settings import HttpDefaultVariantTemplate, HttpCoreVariantTemplate, MPMPreforkVariantTemplate, MPMWorkerVariantTemplate
import os
import json
import itertools
import types

class Dockerfiles(object):
    """
    General operations on Dockerfiles, built files, configs, and templates for
    configuration.
    """

    def __init__(self):
        """
        Setup our general tracking structure with some sane defaults
        """
        self.buildDirectory = "./images"
        self.templates = []
        self.template = "./templates/DockerfileTemplate"

        # set the mapping of known templates
        self.knowntemplates = {
            "httpd-default": HttpDefaultVariantTemplate,
            "httpd-core": HttpCoreVariantTemplate,
            "apache-mpm-prefork": MPMPreforkVariantTemplate,
            "apache-mpm-worker": MPMWorkerVariantTemplate
        }

        # hold our configs
        self._configs = []

    def starttemplate(self, templatename):
        """
        Spin up a new instance of this template
        :param templatename:
        :return:
        """
        return self.knowntemplates[templatename]()

    def set_build_dir(self, bd):
        """
        Set a new build direcotry

        :param bd: Root build directory
        :return:
        """
        self.buildDirectory = bd

    def add_config(self, t):
        """
        Add the values from a Settings configuration object, like ApacheDefaults
        :param t:
        :return:
        """
        self._configs.append(t)

    def add_configs(self, *kargs, **kwargs):
        for ts in kargs:
            if type(ts) == types.ListType:
                [self.add_config(t) for t in ts]
            else:
                self.add_config(ts)

    def __add__(self, other):
        self.add_config(other)

    def generate(self, nameTemplate="image_%(iter)02d", pretend=False):
        """
        Generate the set of Docker directories and files from our current configuration set. We'll need to
        create our Template objects depending on the configuration objects we have.

        The _nameTemplate_ has a dictionary of values passed to it from each iteration through the
        generated templates.

        :param nameTemplate: Templating name for build directories inside the buildDirector directory
        :param pretend: Pretend to write out the files? Useful for generating config sets without really building
        :return: List of configurations built
        """
        print "Using template [%s]" % (self.template,)

        with open(self.template, "r") as template:
            print "\t* found Dockerfile template [%s]" % (self.template,)

        # determine which templates we need
        req_templates = []
        for config in self._configs:
            req_templates += config.needstemplates()
        req_templates = list(set(req_templates))
        for req_template in req_templates:
            print "\t* Adding template [%s]" % (req_template,)

        # set our base directory
        base_directory = os.path.realpath( self.buildDirectory )

        # generated information
        built_configs = []

        # controls for iteration and naming
        config_iter = 0

        # now we can run each iteration of our configs
        for variants in itertools.product(*self._configs):

            # track our template metadata
            image_metadata = {"files": [], "config": {}}

            # name our image
            naming_vars = {
                "iter": config_iter,
                "pretend": pretend
            }
            for variant in variants:
                for v, vs in variant.items():
                    naming_vars.update(vs)

            image_name = nameTemplate % (naming_vars)
            image_metadata["name"] = image_name
            print "\t! Creating variant [%s]" % (image_name)

            # create our output directory
            target_directory = os.path.join(base_directory, image_name)
            if not pretend:
                self._mkdir(target_directory, 0777)

            # setup our templates by instantiating a Template handler for each template type
            # we need
            variant_templates = {}
            for variant in variants:
                for variant_key in variant.keys():
                    if variant_key not in variant_templates:
                        variant_templates[variant_key] = self.starttemplate(variant_key)

            # fill in templates
            for variant in variants:
                for vkey, vdata in variant.items():
                    variant_templates[vkey].addvalues(vdata)

            # generate necessary files
            for template_key in variant_templates.keys():
                onlocalname = variant_templates[template_key].localfilename()
                inimagename = variant_templates[template_key].imagefilename()
                onimageperms = variant_templates[template_key].onimagepermissions()
                image_metadata["files"].append({"local": onlocalname, "image": inimagename, "permissions": onimageperms})

                # generate the output data
                templateoutputname = os.path.join(target_directory, onlocalname)
                print "\t+ Generating template output for [%s] in [%s]" % (template_key, templateoutputname)

                image_metadata["config"][template_key] = variant_templates[template_key].metadata()
                if not pretend:
                    variant_templates[template_key].generate(templateoutputname)

            # generate Dockerfile
            dockerfilename = self._writeself(image_metadata, directory=target_directory, pretend=pretend)
            image_metadata["dockerfile"] = dockerfilename

            # generate metadata file
            metapath = os.path.join(target_directory, "metadata.json")
            print "\t+Creating metadata in [%s]" % (metapath,)
            if not pretend:
                with open(metapath, "w") as metafile:
                    json.dump(image_metadata, metafile)

            # clean up and manage
            built_configs.append(image_metadata)
            config_iter += 1

        # carry on by returning the metadata
        return [Dockerfile(built_config) for built_config in built_configs]

    def _writeself(self, metadata, directory="images/temp", pretend=False, filename="Dockerfile"):
        """
        Write the actual docker file, given the metadata on hand. Currently the following
        seconds can be appended to:

            * COPY
            * CHMOD

        :param metadata: Metadata for the Dockerfile to be generated
        :return: Name of the written dockerfile
        """
        COPYdata = []
        CHMODdata = []
        for copy in metadata["files"]:
            COPYdata.append("COPY %s %s" % (copy["local"], copy["image"]))
            CHMODdata.append("CHMOD %s %s" % (copy["permissions"], copy["image"]))

        # build the actual file
        raw = open(self.template, 'r').read()
        filled = raw.replace("{{COPY}}", "\n".join(COPYdata)).replace("{{CHMOD}}", "\n".join(CHMODdata))

        # actually write the file
        fullpath = os.path.join(directory, filename)
        print "\t+ Creating Dockerfile in [%s]" % (fullpath,)
        if not pretend:
            with open(fullpath, "w") as file:
                file.write(filled)

        return fullpath

    def _mkdir(self, pathname, perm):
        """
        Given a path, make sure all the intermediate directories exist.

        :param pathname:
        :return: True or False, if the path was created
        """
        bits = []
        stack = pathname
        while stack != "/":
            stack, bit = os.path.split(stack)
            bits.append(bit)
        bits.reverse()
        base = "/"
        for bit in bits:
            base = os.path.join(base, bit)
            if not os.path.exists(base):
                print "\t^ creating directory %s" % (base,)
                os.mkdir(base, perm)
        return True


class Dockerfile(object):
    """
    This class is build out of configuration data from the Dockerfiles class. Operations
    on individual Dockerfile images can be handled with this class.
    """

    def __init__(self, conf):
        """
        Create a new wrapper object for our dockerfile. This is a loose wrapper at best.
        """
        self.config = conf
        self.dockerfile = conf["dockerfile"]
        self.repositoryTemplate = "patricknevindwyer/dg-%(name)s"
        self.tag = "latest"

    def setrepotemplate(self, t):
        self.repositoryTemplate = t

    def name(self):
        """
        Get the fully qualified name of this repo
        :return:
        """
        return "%s:%s" % (self.repository(), self.tag)

    def repository(self):
        """
        Get the formatted repository of this image

        :param kargs:
        :param kwargs:
        :return:
        """
        return self.repositoryTemplate % self.config

    def files(self, local=False, image=False):
        """
        Iterator over the generated files included in this build image. Can return
        a combination of the local and image paths for the files.

        Local:

            dockerfilel.files(local=True)

                -> [file1, file2, file3]
        :param local:
        :param image:
        :return:
        """
        for fileset in self.config["files"]:
            fs = []
            if local:
                fs.append(fileset["local"])
            if image:
                fs.append(fileset["image"])
            if len(fs) == 1:
                yield fs[0]
            else:
                yield fs


