current example usage:

docker rm magicserver1; docker build -t wbradmoore/magicserver .; docker run -p 5950:5950 --name magicserver1 -it wbradmoore/magicserver