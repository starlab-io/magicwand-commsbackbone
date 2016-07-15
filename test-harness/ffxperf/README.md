Docker image for headless firefox. Currently just loads invincealabs.com and prints out the browser (page) title.

current example usage:

docker stop ffxperf1; docker rm ffxperf1; docker build -t wbradmoore/ffxperf .;docker run --name ffxperf1 -it wbradmoore/ffxperf