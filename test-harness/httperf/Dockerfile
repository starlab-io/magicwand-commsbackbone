FROM ubuntu:trusty
MAINTAINER patricknevindwyer@gmail.com

USER root

RUN apt-get update
RUN apt-get install -y g++ gcc make wget unzip dh-autoreconf python gnuplot

# Get HTTPERF and build it
RUN wget https://github.com/httperf/httperf/archive/master.zip
RUN unzip master.zip
WORKDIR httperf-master
RUN autoreconf -i
RUN ./configure
RUN make
RUN make install

COPY run_httperf.sh /usr/local/bin
COPY parse_httperf.py /usr/local/bin
COPY performance_plot.gp /usr/local/src

RUN chmod ugo+x /usr/local/bin/run_httperf.sh
RUN chmod ugo+x /usr/local/bin/parse_httperf.py

VOLUME ["/var/log/httperf"]

# now we can run
CMD ["run_httperf.sh"]