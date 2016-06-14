FROM ubuntu:trusty
MAINTAINER patricknevindwyer@gmail.com

USER root

RUN apt-get update
RUN apt-get install -y g++ gcc make wget unzip dh-autoreconf

# Get HTTPERF and build it
RUN wget https://github.com/httperf/httperf/archive/master.zip
RUN unzip master.zip
WORKDIR httperf-master
RUN autoreconf -i
RUN ./configure
RUN make
RUN make install

COPY run_httperf.sh /usr/local/bin
RUN chmod ugo+x /usr/local/bin/run_httperf.sh

# now we can run
CMD ["run_httperf.sh"]