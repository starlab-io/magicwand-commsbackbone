FROM ubuntu:trusty
MAINTAINER patricknevindwyer@gmail.com

USER root

RUN apt-get update
RUN apt-get install -y git golang

RUN mkdir -p /home/goloris/gocode
ENV GOPATH /home/goloris/gocode

RUN go get -u github.com/valyala/goloris
RUN go build github.com/valyala/goloris
