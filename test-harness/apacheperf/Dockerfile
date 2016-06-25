FROM ubuntu:trusty
MAINTAINER wbradmoore@gmail.com

USER root

RUN apt-get update
RUN apt-get install -y g++ wget gcc make python gnuplot autopoint gettext autoconf libtool automake pkg-config
RUN wget http://archive.apache.org/dist/httpd/httpd-2.2.11.tar.gz
RUN tar -zxvf httpd-2.2.11.tar.gz
WORKDIR httpd-2.2.11
RUN ./configure
RUN make
RUN make install
RUN wget https://gitlab.com/procps-ng/procps/repository/archive.tar.gz?ref=v3.3.11 -O procps-ng.tar.gz
RUN tar -xvf procps-ng.tar.gz
RUN mv procps-v* procps
WORKDIR procps
RUN ./autogen.sh
RUN ./configure --without-ncurses
RUN make
RUN mv vmstat /usr/bin/vmstat

COPY httpd-foreground /usr/local/bin
COPY run_apacheperf.sh /usr/local/bin
COPY parse_apacheperf.py /usr/local/bin
COPY performance_plot.gp /usr/local/src
RUN chmod ugo+x /usr/local/bin/run_apacheperf.sh
RUN chmod ugo+x /usr/local/bin/parse_apacheperf.py
RUN chmod ugo+x /usr/local/bin/httpd-foreground

ENV PATH $PATH:/httpd-2.2.11

EXPOSE 80

VOLUME ["/var/log/apacheperf"]

CMD ["run_apacheperf.sh"]