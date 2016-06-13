#!/usr/bin/env bash
COUNT=`docker-compose exec -T apache ps -aux | grep httpd | wc -l`
echo "=$COUNT streams active in Apache"