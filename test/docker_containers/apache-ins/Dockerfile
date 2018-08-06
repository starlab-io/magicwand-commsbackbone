FROM apache-performance-image


# ins driver
RUN apt update  -y
RUN apt upgrade -y
RUN apt install -y apt-utils
RUN apt install -y gcc make 

#For Debugging
#RUN apt install -y vim less locate netcat strace lsof file
#RUN apt install -y net-tools kmod
#RUN updatedb


COPY ./conf/httpd.conf /opt/httpd/conf
COPY ./conf/envvars /opt/httpd/bin

RUN mkdir -p /code/shim/common
RUN mkdir -p /code/shim/protvm/user/wrapper
COPY ./shim/ /code/shim
WORKDIR /code/shim/protvm/user/wrapper/
RUN make clean all
RUN mkdir -p /var/log/output

#Use this command to add custom index.html
COPY ./conf/index.html /opt/httpd/htdocs

WORKDIR /opt/httpd/bin

CMD ["/opt/httpd/bin/apachectl","-DFOREGROUND","-k","start"]
