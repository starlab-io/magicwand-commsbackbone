FROM ubuntu:trusty
MAINTAINER wbradmoore@gmail.com

USER root

RUN echo "set nocp\nset backspace=2" > /root/.vimrc
RUN echo "\ndeb http://downloads.sourceforge.net/project/ubuntuzilla/mozilla/apt all main" >> /etc/apt/sources.list
RUN apt-key adv --recv-keys --keyserver keyserver.ubuntu.com C1289A29
RUN apt-get update

RUN apt-get install -y curl python python-setuptools xvfb libdbus-glib-1-2 libgtk2.0-0 libasound2 firefox firefox-mozilla-build
RUN apt-get install -y tcpdump
RUN easy_install pip
RUN pip install pyvirtualdisplay selenium

COPY ffx.py /usr/local/bin
RUN chmod ugo+x /usr/local/bin/ffx.py

COPY startscript.sh /usr/local/bin
RUN chmod ugo+x /usr/local/bin/startscript.sh

# VOLUME ["/var/log/httperf"]

# now we can run
CMD startscript.sh

# docker stop ffxperf1; docker rm ffxperf1; docker build -t wbradmoore/ffxperf .;docker run --name ffxperf1 -d wbradmoore/ffxperf; docker exec -it --user root ffxperf1 bash

