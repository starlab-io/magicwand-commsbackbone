"""
Our collection of customized settings and variations
"""
import types

class ApacheVariantTemplate(object):
    """
    A templating object to hold a set of Key Value objects that will be injected into a template
    used in the generation of a configuration file. This object is generated by the Dockerfile
    control object, and any input template that needs to inject values will have the option of
    doing so.

    These templates know a few things about the environment:

      - Where its template lives
      - Where its final form lives in a Docker image
      - How to generate a COPY command in a Dockerfile to install itself
    """
    def __init__(self):
        self.templatevalues = {}

    def addvalues(self, valueDict):
        """
        Given a dictionary of possible template values we know about, set the value we'll
        output to our template when the final generation step happens.

        :param valueDict:
        :return:
        """
        for k, v in self.templatekeys.items():
            if k in valueDict:
                self.templatevalues[k] = valueDict[k]

    def generate(self, filename):
        """
        Generate our output to the given filename.

        :param filename:
        :return:
        """

        # make sure we have all of our values.
        for k, v in self.templatekeys.items():
            if k not in self.templatevalues:
                self.templatevalues[k] = v
            elif self.templatevalues[k] == None:
                self.templatevalues[k] = v

        # get our template
        template = open(self.template, "r").read()

        # write in all of our values
        for k, v in self.templatevalues.items():
            writekey = "{{%s}}" % (k,)
            template = template.replace(writekey, self._convert(v))

        # excellent, now let's write that out to our output file
        with open(filename, "w") as file:
            file.write(template)

    def metadata(self):
        """
        Similar to the _generate_ method, but return our configuration state as
        a dictionary.

        :return: Dictionary of config values
        """
        metamap = {k: v for k, v in self.templatevalues.items()}

        # make sure we have all of our values.
        for k, v in self.templatekeys.items():
            if k not in self.templatevalues:
                metamap[k] = v
            elif self.templatevalues[k] == None:
                metamap[k] = v

        return metamap

    def _convert(self, val):
        """
        Convert a pythonic value into something that works in Apache configs

            string => string
            integer => integer
            bool => On | Off

        :param val:
        :return:
        """

        if type(val) in types.StringTypes:
            return val
        elif type(val) == types.IntType:
            return str(val)
        elif type(val) == types.BooleanType:
            if val:
                return "On"
            else:
                return "Off"
        else:
            raise "ApacheVariantTemplate doesn't know how to deal with templated values of type %s" % (str(type(val)))


    def localfilename(self):
        """
        What is the local generated name of this file?
        :return:
        """
        return self.generatedname

    def imagefilename(self):
        """
        What is the name/path of this file in the build image?
        :return:
        """
        return self.targetname

    def onimagepermissions(self):
        """
        What are the permissions of this on the image? In the format
        of a string that can be called with CHMOD
        :return:
        """
        return self.permissions


class HttpDefaultVariantTemplate(ApacheVariantTemplate):
    """
    Work with the HTTPD Defaults config data
    """

    def __init__(self):
        super(HttpDefaultVariantTemplate, self).__init__()
        self.template = "./templates/ApacheDefaultsConfigTemplate"
        self.templatekeys = {
            "timeout": 300,
            "keepalive": True,
            "maxkeepaliverequests": 100,
            "keepalivetimeout": 5
        }

        self.generatedname = "httpd-default.conf"
        self.targetname = "/usr/local/apache2/conf/extras/httpd-default.conf"
        self.permissions = "ugo+r"


class MPMPreforkVariantTemplate(ApacheVariantTemplate):
    """
    Work with the MPM Prefork configuration
    """

    def __init__(self):
        super(MPMPreforkVariantTemplate, self).__init__()
        self.template = "./templates/ApacheMPMConfigTemplate-prefork"
        self.templatekeys = {
            "startservers": 5,
            "minspareservers": 5,
            "maxspareservers": 10,
            "maxclients": 150,
            "maxrequestsperchild": 0
        }

        self.generatedname = "httpd-mpm.conf"
        self.targetname = "/usr/local/apache2/conf/extras/httpd-mpm.conf"
        self.permissions = "ugo+r"


class MPMWorkerVariantTemplate(ApacheVariantTemplate):
    """
    Work with the MPM Worker configuration
    """

    def __init__(self):
        super(MPMWorkerVariantTemplate, self).__init__()
        self.template = "./templates/ApacheMPMConfigTemplate-worker"
        self.templatekeys = {
            "startservers": 2,
            "maxclients": 150,
            "minsparethreads": 25,
            "maxsparethreads": 75,
            "threadsperchild": 25,
            "maxrequestsperchild": 0
        }

        self.generatedname = "httpd-mpm.conf"
        self.targetname = "/usr/local/apache2/conf/extras/httpd-mpm.conf"
        self.permissions = "ugo+r"

class HttpCoreVariantTemplate(ApacheVariantTemplate):
    """
    Work with the core HTTPD config file, which guides everything else
    """

    def __init__(self):
        super(HttpCoreVariantTemplate, self).__init__()
        self.template = "./templates/ApacheConfigTemplate"
        self.templatekeys = {}

        self.generatedname = "httpd.conf"
        self.targetname = "/usr/local/apache2/conf/httpd.conf"
        self.permissions = "ugo+r"

class ApacheCore(object):
    """
    Chain together variations from the Apache Core configuration. Right now, this is a 1 element
    iteration.
    """
    def __init__(self):
        """
        Right now we do't need any config, as we're just a single element iterator
        """
        pass

    def needstemplates(self):
        """
        We need to include the core templates
        :return:
        """
        return ["httpd-core"]

    def __iter__(self):
        """
        We're only a single value iterator, so we can just return our value
        :return:
        """
        yield {"httpd-core": {}}


class ApacheMPMPrefork(object):
    """
    Create variations in the Apache MPM-Prefork operational mode.
    """

    def __init__(self, *kargs, **kwargs):
        """
        Setup the variants as well as the ranges of the variants used in generating this config
            block.
        """
        # grab our settings for this configuration template
        configdefaults = {
            "startServers": [5],
            "minSpareServers": [5],
            "maxSpareServers": [10],
            "maxClients": [150],
            "maxRequestsPerChild": [0]
        }

        # interpolate either KWARGed values or sane default
        for config in configdefaults.keys():
            configkey = "_variant_" + config.lower()
            configval = None
            if config in kwargs:
                configval = kwargs[config]
            else:
                configval = configdefaults[config]
            setattr(self, configkey, configval)

    def needstemplates(self):
        return ["apache-mpm-prefork"]

    def __iter__(self):
        for ss in self._variant_startservers:
            for minss in self._variant_minspareservers:
                for maxss in self._variant_maxspareservers:
                    for mc in self._variant_maxclients:
                        for mrpc in self._variant_maxrequestsperchild:
                            yield {
                                "apache-mpm-prefork": {
                                    "startservers": ss,
                                    "minspareservers": minss,
                                    "maxspareservers": maxss,
                                    "maxclients": mc,
                                    "maxrequestsperchild": mrpc
                                }
                            }



class ApacheMPMWorker(object):
    """
    Create variations in the Apache MPM-Worker operational mode.
    """

    def __init__(self, *kargs, **kwargs):
        """
        Setup the variants as well as the ranges of the variants used in generating this config
            block.
        """
        # grab our settings for this configuration template
        configdefaults = {
            "startServers": [2],
            "maxClients": [150],
            "minSpareThreads": [25],
            "maxSpareThreads": [75],
            "threadsPerChild": [25],
            "maxRequestsPerChild": [0]
        }

        # interpolate either KWARGed values or sane default
        for config in configdefaults.keys():
            configkey = "_variant_" + config.lower()
            configval = None
            if config in kwargs:
                configval = kwargs[config]
            else:
                configval = configdefaults[config]
            setattr(self, configkey, configval)

    def needstemplates(self):
        return ["apache-mpm-worker"]

    def __iter__(self):
        for ss in self._variant_startservers:
            for mc in self._variant_maxclients:
                for minst in self._variant_minsparethreads:
                    for maxst in self._variant_maxsparethreads:
                        for tpc in self._variant_threadsperchild:
                            for mrpc in self._variant_maxrequestsperchild:
                                yield {
                                    "apache-mpm-worker": {
                                        "startservers": ss,
                                        "maxclients": mc,
                                        "minsparethreads": minst,
                                        "maxsparethreads": maxst,
                                        "threadsperchild": tpc,
                                        "maxrequestsperchild": mrpc
                                    }
                                }


class ApacheDefaults(object):
    """
    Allow variation in the standard apache settings governing connection handling and timeouts.
    """

    def __init__(self, *kargs, **kwargs):
        """
        Setup the variants as well as the ranges of the variants used in generating this config
            block.
        """
        self._variant_timeout = []
        self._variant_keepalive = []
        self._variant_maxkeepaliverequests = []
        self._variant_keepalivetimeout = []

        # grab our settings for this configuration template
        configset = ["timeout", "keepAlive", "maxKeepAliveRequests", "keepAliveTimeout"]
        configdefaults = {
            "timeout": [300],
            "keepAlive": [True],
            "maxKeepAliveRequests": [100],
            "keepAliveTimeout": [5]
        }

        # interpolate either KWARGed values or sane default
        for config in configset:
            configkey = "_variant_" + config.lower()
            configval = None
            if config in kwargs:
                configval = kwargs[config]
            else:
                configval = configdefaults[config]
            setattr(self, configkey, configval)

        # We need to track whether or not keep alive is even set, because it changes
        # the range of valid values for other settings.
        self._is_variant_keepalive = False

    def timeouts(self):
        """
        An iterator over our timeout values.

        :return: Yield a timeout from our configured range
        """
        for timeout in self._variant_timeout:
            yield timeout

    def keepalive(self):
        """
        An iterator over our configured Keep Alive settings.

        :return: Yield a control value for keep alive
        """
        for vkeepalive in self._variant_keepalive:
            self._is_variant_keepalive = vkeepalive
            yield vkeepalive

    def maxkeepaliverequests(self):
        """
        Iterator over the range of max seconds for a keep alive request to stay open.

        :return: Yield a value from our keepAliveRequests series
        """
        if self._is_variant_keepalive:
            for maxkar in self._variant_maxkeepaliverequests:
                yield maxkar
        else:
            yield None

    def keepalivetimeout(self):
        """
        Iterator over the second to give each keepalive request before timing out the
        connection.

        :return: Yield a value from our keepAliveTimeout series
        """
        if self._is_variant_keepalive:
            for kat in self._variant_keepalivetimeout:
                yield kat
        else:
            yield None

    def needstemplates(self):
        """
        Return the names of the templates this options class depends on

        :return: Array of  String name sof our dependencies.
        """
        return ["httpd-default"]

    def __iter__(self):
        """
        Iterate through all of our config options, yielding a set of fields for a
        template object at each step.

        The variant yielded value looks like:

            {
                "template name": {
                    "template key1", "val1",
                    "template key2", "val2",
                    ...

                }
            }

        In this manner the
        :return: Dictionary of dictionary
        """
        for to in self.timeouts():
            for ka in self.keepalive():
                for mkar in self.maxkeepaliverequests():
                    for kat in self.keepalivetimeout():
                        yield {
                            "httpd-default": {
                                "timeout": to,
                                "keepalive": ka,
                                "maxkeepaliverequest": mkar,
                                "keepalivetimeout": kat
                            }
                        }