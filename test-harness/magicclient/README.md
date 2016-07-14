current example usage:

docker rm magicclient1; docker build -t wbradmoore/magicclient .; docker run --env TARGETURL="172.17.0.3" --name magicclient1 -it wbradmoore/magicclient