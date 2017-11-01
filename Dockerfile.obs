FROM ubuntu:16.04
MAINTAINER Benjamin Henrion <zoobab@gmail.com>

RUN apt-get update
RUN apt-get install -yy wget
RUN wget -nv https://download.opensuse.org/repositories/network:messaging:zeromq:git-draft/xUbuntu_16.04/Release.key -O Release.key
RUN apt-key add - < Release.key
RUN echo 'deb http://download.opensuse.org/repositories/network:/messaging:/zeromq:/git-draft/xUbuntu_16.04/ /' > /etc/apt/sources.list.d/glar150.list
RUN apt-get update
RUN apt-get install -yy glar150

ENTRYPOINT ["glard","-i","eth0"]
