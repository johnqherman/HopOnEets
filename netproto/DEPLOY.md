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

## Self-update (auto-download)

Clients send their bundle version in `hello`. If it's older than `LATEST_VERSION`, the relay points
them at the latest `.eetsmod` (a GitHub release asset); the client downloads it in the background,
verifies the sha256, overwrites its own bundle, and the loader re-unpacks on the next launch. While an
update is pending the client blocks ALL multiplayer and shows a boot "UPDATING" screen with a Restart
button.

Cutting a release:
1. Bump `version` in `hop_on_eets.cfg`, repack: `make pack` (or the deploy repack).
2. Create a GitHub release and upload `hop_on_eets.eetsmod` as an asset.
3. Compute its hash: `sha256sum hop_on_eets.eetsmod`.
4. Set relay env (e.g. in `netproto/.env`, read by docker-compose) and restart the relay:
   - `LATEST_VERSION=0.3.1`
   - `UPDATE_URL=https://github.com/<owner>/<repo>/releases/download/v0.3.1/hop_on_eets.eetsmod`
   - `UPDATE_SHA256=<sha256sum output>`
   - `MIN_VERSION=` (optional; clients below it are flagged "required" — currently any pending update
     already blocks multiplayer, so this is informational)

Leave `LATEST_VERSION` blank to disable update prompts. The download is https-only and follows the
GitHub→CDN redirect. The client never trusts the bundle without the sha256 match (when set).
