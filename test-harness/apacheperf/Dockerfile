FROM ubuntu:trusty
MAINTAINER wbradmoore@gmail.com

USER root

RUN echo "set nocompatible\nset backspace=2" > $HOME/.vimrc
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
COPY startscript.sh /usr/local/bin
COPY parse_apacheperf.py /usr/local/bin
COPY expand.sh /usr/local/bin
COPY performance_plot.gp /usr/local/src
RUN chmod ugo+x /usr/local/bin/startscript.sh
RUN chmod ugo+x /usr/local/bin/expand.sh
RUN chmod ugo+x /usr/local/bin/parse_apacheperf.py
RUN chmod ugo+x /usr/local/bin/httpd-foreground

#simple realistic page:
COPY varwww /usr/local/apache2/htdocs/

#cgi:
COPY cgiDatPy /usr/local/apache2/cgi-bin
RUN chmod 555 /usr/local/apache2/cgi-bin/cgiDatPy

ENV PATH $PATH:/httpd-2.2.11

EXPOSE 80

VOLUME ["/var/log/apacheperf"]

WORKDIR /root
CMD ["startscript.sh"]

# docker stop apacheperf1; docker rm apacheperf1; docker build -t wbradmoore/apacheperf .;docker run -p 80:80 --name apacheperf1 -d wbradmoore/apacheperf; docker exec -it --user root apacheperf1 bash