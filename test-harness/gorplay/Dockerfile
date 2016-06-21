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

RUN apt-get install wget -y
RUN wget https://github.com/buger/gor/releases/download/v0.14.1/gor_v0.14.1_x64.tar.gz
RUN tar -xvf gor_v0.14.1_x64.tar.gz

#VOLUME

EXPOSE 80
CMD ["startscript.sh"]

#apt-get install bsdmainutils

# FROM ubuntu:trusty
# MAINTAINER wbradmoore@gmail.com

# USER root

# RUN apt-get update

# EXPOSE 8000

# CMD ["/bin/bash"]


# RUN apt-get install -y g++ gcc make wget unzip dh-autoreconf python gnuplot

# Get HTTPERF and build it
# RUN wget https://github.com/httperf/httperf/archive/master.zip
# RUN unzip master.zip
# WORKDIR httperf-master
# RUN autoreconf -i
# RUN ./configure
# RUN make
# RUN make install

# COPY run_httperf.sh /usr/local/bin
# COPY parse_httperf.py /usr/local/bin
# COPY performance_plot.gp /usr/local/src

# RUN chmod ugo+x /usr/local/bin/run_httperf.sh
# RUN chmod ugo+x /usr/local/bin/parse_httperf.py

# VOLUME ["/var/log/httperf"]

# apt install flex bison wget make golang git -y
# wget http://www.tcpdump.org/release/libpcap-1.7.4.tar.gz
# ar xzf libpcap-1.7.4.tar.gz
# cd libpcap-1.7.4
# ./configure
# make install

# wget https://storage.googleapis.com/golang/go1.6.2.src.tar.gz
# tar -xvf go1.6.2.src.tar.gz
# cd go

# # Lets fetch Gor source code
# mkdir $HOME/gocode
# # See more information about GOPATH https://github.com/golang/go/wiki/GOPATH
# export GOPATH=$HOME/gocode
# # Fetch code from the Github
# go get github.com/buger/gor

# # Compile from source
# cd $HOME/gocode/src/github.com/buger/gor
# go build LDFLAGS = -ldflags "-extldflags \"-static\""
