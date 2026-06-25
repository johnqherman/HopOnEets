# Hosting the Hop On Eets relay (VPS, Docker behind a shared nginx)

The relay pairs players, runs the Bo3 ruleset, and keeps the ranked Elo ladder. It speaks plain `ws`
bound to loopback; the VPS's shared **nginx** reverse proxy terminates TLS for the public domain and
forwards the WebSocket upgrade, so clients connect over `wss://`.

## One-time setup

1. Install Docker + the compose plugin on the VPS.
2. Point a DNS **A record** for your domain (e.g. `hoe.raccoonlagoon.com`) at the VPS public IP.
3. Deploy the code and bring up the relay:

   ```sh
   cp .env.example .env
   $EDITOR .env            # set APP_PORT (a free loopback port; nginx proxies to it)
   docker compose up -d --build
   ```

   The relay is now published on `127.0.0.1:${APP_PORT}` only.

4. Add an nginx vhost for your domain that terminates TLS (Let's Encrypt / certbot) and reverse-proxies
   to `127.0.0.1:${APP_PORT}`, passing the WebSocket upgrade:

   ```nginx
   location / {
       proxy_pass http://127.0.0.1:33710;   # = APP_PORT
       proxy_http_version 1.1;
       proxy_set_header Upgrade $http_upgrade;
       proxy_set_header Connection 'upgrade';
       proxy_set_header Host $host;
       proxy_read_timeout 600s;
   }
   ```

   Then `sudo certbot --nginx -d <domain>` and reload. Clients connect to `wss://<domain>/`.

## CI/CD

`.github/workflows/deploy.yml` deploys on every push to `main`: it SSHes to the VPS (appleboy/ssh-action)
and runs `./netproto/deploy.sh`, which fetches `main`, rebuilds, and restarts the container. Required repo
secrets: `VPS_HOST`, `VPS_USER`, `VPS_SSH_KEY` (private key), `SSH_PASSPHRASE`. Manual redeploy: run
`./netproto/deploy.sh` on the box.

## Operating

- Logs: `docker compose logs -f relay`
- Update: `./netproto/deploy.sh` (or `git pull && docker compose up -d --build`)
- Elo ladder: persisted in the `elo` volume at `/data/elo.json`
  (inspect: `docker compose exec relay cat /data/elo.json`). Redeploys never wipe it; never run
  `docker compose down -v`.
- The Elo key is the client UUID, not the display name (names are spoofable). Identity is still
  client-trusted — fine for an informal ladder; real accounts/auth is future work.

## What is NOT hosted

The authoritative cross-platform re-sim verifier (`EETS_DIR`) needs the game binary and is left off the
hosted relay; ranked results are the provisional same-platform outcome.

## Client side

The mod connects directly to `wss://<DOMAIN>/` (no separate bridge process). Set the relay host in the
mod config (`relay_url`); see the mod README. The local `bridge.ts` is now only a dev/test tool.
