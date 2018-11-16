FROM mw-base-image

RUN apt-get update
RUN pip2 install pyxs

RUN mkdir /code
RUN mkdir /code/archive

COPY mw_netflow_consumer.py /code/mw_netflow_consumer.py
COPY mw_netflow.py /code/mw_netflow.py
COPY run_netflow.sh /code
COPY upload.sh /code
COPY sync_once.py /code
RUN chmod 777 /code/upload.sh

WORKDIR /code

CMD ["bash", "/code/run_netflow.sh"]
