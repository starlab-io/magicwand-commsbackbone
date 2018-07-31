FROM        ubuntu:trusty

# Setup packages.
RUN apt update
RUN apt upgrade -y
RUN apt install wget unzip make gcc autoconf libtool -y

RUN mkdir -p /tmp/workspace
WORKDIR /tmp/workspace

RUN wget http://archive.apache.org/dist/httpd/httpd-2.2.11.tar.gz
RUN mkdir src
RUN tar -zxvf httpd-2.2.11.tar.gz -C src

WORKDIR /tmp/workspace/src/httpd-2.2.11

RUN ./configure --with-included-apr
RUN make
RUN make install

WORKDIR /usr/local/apache2/conf
COPY ./conf/httpd.conf .

COPY ./conf/index.html /usr/local/apache2/htdocs

WORKDIR /usr/local/apache2/bin

CMD ["/usr/local/apache2/bin/apachectl","-DFOREGROUND","-k","start"]

