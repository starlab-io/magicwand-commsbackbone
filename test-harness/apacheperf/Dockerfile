FROM ubuntu:trusty
MAINTAINER patricknevindwyer@gmail.com

# Ubuntu

USER root

RUN apt-get update
RUN apt-get install -y g++ wget gcc make

# Scope parent thread tracing in PIN
#CMD echo 0 > /proc/sys/kernel/yama/ptrace_scope

# Compile our version of APACHE
RUN wget http://archive.apache.org/dist/httpd/httpd-2.2.11.tar.gz
RUN tar -zxvf httpd-2.2.11.tar.gz
WORKDIR httpd-2.2.11
RUN ./configure
RUN make
RUN make install
COPY httpd-foreground /usr/local/bin



COPY run_apacheperf.sh /usr/local/bin
COPY parse_apacheperf.py /usr/local/bin
COPY performance_plot.gp /usr/local/src
RUN chmod ugo+x /usr/local/bin/run_apacheperf.sh
# todo RUN chmod ugo+x /usr/local/bin/parse_apacheperf.py
RUN chmod ugo+x /usr/local/bin/httpd-foreground

# ADD HTTPD to PATH at /httpd-2.2.11/httpd
ENV PATH $PATH:/httpd-2.2.11

EXPOSE 80

VOLUME ["/var/log/apacheperf"]

# now we can run
CMD ["run_apacheperf.sh"]