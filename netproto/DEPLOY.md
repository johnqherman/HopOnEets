# Hosting the Hop On Eets relay (VPS, Docker + Caddy)

The relay pairs players, runs the Bo3 ruleset, and keeps the ranked Elo ladder. Caddy fronts it with
automatic HTTPS so clients connect over `wss://`.

## One-time setup

1. Install Docker + the compose plugin on the VPS.
2. Point a DNS **A record** for your domain (e.g. `hoe.raccoonlagoon.com`) at the VPS public IP.
3. Open ports **80** and **443** (Caddy needs 80 for the ACME challenge, 443 for wss).
4. Copy the netproto/ directory to the VPS, then:

   ```sh
   cp .env.example .env
   $EDITOR .env            # set DOMAIN=hoe.raccoonlagoon.com
   docker compose up -d --build
   ```

Caddy fetches a Let's Encrypt cert automatically on first start. Clients connect to `wss://<DOMAIN>/`.

## Operating

- Logs:        `docker compose logs -f relay`
- Update:      `git pull && docker compose up -d --build`
- Elo ladder:  persisted in the `elo` volume at `/data/elo.json`
  (inspect: `docker compose exec relay cat /data/elo.json`)
- The Elo key is the client UUID, not the display name (names are spoofable). Identity is still
  client-trusted — fine for an informal ladder; real accounts/auth is future work.

## What is NOT hosted

The authoritative cross-platform re-sim verifier (`EETS_DIR`) needs the game binary and is left off the
hosted relay; ranked results are the provisional same-platform outcome.

## Client side

The mod connects directly to `wss://<DOMAIN>/` (no separate bridge process). Set the relay host in the
mod config (`relay_url`); see the mod README. The local `bridge.ts` is now only a dev/test tool.
