FROM        ubuntu:trusty

# Setup packages.
RUN apt update
RUN apt upgrade -y
RUN apt install wget unzip make gcc autoconf libtool -y

RUN mkdir -p /opt/httpd
RUN mkdir -p /opt/httpd/test

WORKDIR /opt/httpd

RUN wget http://archive.apache.org/dist/httpd/httpd-2.2.11.tar.gz
RUN mkdir src
RUN tar -zxvf httpd-2.2.11.tar.gz -C src

WORKDIR /opt/httpd/src/httpd-2.2.11

RUN ./configure --prefix=/opt/httpd/ --with-included-apr
RUN make
RUN make install


COPY ./conf/httpd.conf /opt/httpd/conf
COPY ./conf/index.html /opt/httpd/htdocs
#COPY ./conf/httpd-mpm.conf /opt/httpd/conf/extra

WORKDIR /opt/httpd/bin

CMD ["/opt/httpd/bin/apachectl","-DFOREGROUND","-k","start"]

