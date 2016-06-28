FROM ubuntu:trusty
MAINTAINER wbradmoore@gmail.com

USER root

RUN apt-get update
RUN apt-get install -y g++ wget gcc make

RUN wget http://archive.apache.org/dist/httpd/httpd-2.2.11.tar.gz
RUN tar -zxvf httpd-2.2.11.tar.gz
WORKDIR httpd-2.2.11
RUN ./configure
RUN make
RUN make install
COPY httpd-foreground /usr/local/bin
COPY startscript.sh /usr/local/bin
RUN chmod ugo+x /usr/local/bin/httpd-foreground
RUN chmod ugo+x /usr/local/bin/startscript.sh
# ADD HTTPD to PATH at /httpd-2.2.11/httpd
ENV PATH $PATH:/httpd-2.2.11

WORKDIR /root

RUN wget https://github.com/buger/gor/releases/download/v0.14.1/gor_v0.14.1_x64.tar.gz
RUN tar -xvf gor_v0.14.1_x64.tar.gz

#VOLUME

EXPOSE 80
CMD startscript.sh `echo ${RECORDINGFILE} ${TARGETURL}`

#docker rm gorplay1; docker rmi wbradmoore/gorplay; docker build -t wbradmoore/gorplay .; docker run -v $PWD:/root/log --env TARGETURL="http://172.17.0.2" --name gorplay1 -it wbradmoore/gorplay