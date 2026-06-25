#!/bin/bash
set -e

cd /var/www/hoponeets

git fetch origin
git reset --hard origin/main

cd netproto
docker compose up --build -d

docker image prune -f
