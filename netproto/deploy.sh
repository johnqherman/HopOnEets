#!/bin/bash
# Deploy the Hop On Eets relay on the VPS. Invoked by the GitHub Actions workflow over SSH
# (cd /var/www/hoponeets && ./netproto/deploy.sh) and runnable by hand for a manual redeploy.
# Pulls the latest main, rebuilds, and restarts the container. The `elo` named volume (the ranked
# Elo ladder) is preserved across redeploys - this script never runs `down -v`.
set -e

cd /var/www/hoponeets

git fetch origin
git reset --hard origin/main

cd netproto
docker compose up --build -d

docker image prune -f
