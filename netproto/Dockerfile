# Hop On Eets relay. Zero runtime deps; runs the TS directly via tsx. TLS is terminated by the Caddy
# reverse proxy (see docker-compose.yml), so the relay only speaks plain ws on $PORT inside the network.
FROM node:20-slim
WORKDIR /app
COPY package.json ./
RUN npm install --no-audit --no-fund
COPY *.ts ./
ENV PORT=38500 ELO_FILE=/data/elo.json
EXPOSE 38500
# the Elo ladder persists in /data (a mounted volume); EETS_DIR is intentionally unset (the authoritative
# re-sim verifier needs the game binary and is not part of the hosted relay).
CMD ["npx", "tsx", "relay.ts"]
