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
RUN chmod ugo+x /usr/local/bin/httpd-foreground
# ADD HTTPD to PATH at /httpd-2.2.11/httpd
ENV PATH $PATH:/httpd-2.2.11

EXPOSE 80
CMD ["httpd-foreground"]