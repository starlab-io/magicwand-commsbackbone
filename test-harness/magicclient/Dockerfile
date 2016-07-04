FROM ubuntu:trusty
MAINTAINER wbradmoore@gmail.com

USER root

RUN apt-get update
RUN apt-get install -y nodejs-legacy curl

WORKDIR /root

#VOLUME

# EXPOSE 5950
COPY startscript.sh /usr/local/bin
RUN chmod ugo+x /usr/local/bin/startscript.sh
COPY leech.js /usr/local/bin
CMD startscript.sh `echo ${TARGETIP}`

# `echo ${RECORDINGFILE} ${TARGETURL}`