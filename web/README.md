# Kaepora website

The site for Kaepora: a homepage, a download page with the changelog, a page on what is randomized, and an FAQ. It is a TanStack Start app with React and Tailwind CSS, and it runs on Vercel's Bun runtime. It also serves the API that Kaepora's client calls, described under [The API](#the-api).

## Run it locally

```sh
bun install
bun run dev
```

`bun run typecheck` checks the types, Effect's language service included. `bun run test` runs the tests. `bun run build` writes the production build to `.output/` and needs no database.

The API's tests need a Postgres 18 server on `localhost:5432` with the password `postgres`. Each test file makes a database of its own on it, applies every migration, and drops the database at the end. Start the server in a container before the tests and remove it after:

```sh
podman run -d --name kaepora-postgres -e POSTGRES_PASSWORD=postgres -p 127.0.0.1:5432:5432 docker.io/library/postgres:18-alpine
bun run test
podman rm -f kaepora-postgres
```

To call the API from `bun run dev`, make a database on that server, set `DATABASE_URL` and `DATABASE_URL_UNPOOLED` to it, and run `bun run migrate`. The API then answers at `http://localhost:3000/v1/`.

`.github/workflows/web.yml` runs the three checks with Bun 1.4.4 on each pull request and each push to `main` that changes `web/` or a file outside it that the build reads. It runs the same Postgres as a service. Then it deploys the site, as [Deploy](#deploy) says.

- The theme (colours, fonts, sizes) is in `src/styles.css`. The drawings are in `src/components/night.tsx`.
- The homepage's wall shows every picture in `src/captures/`. Every load deals a new order and a new arrangement of large, medium and small tiles (`src/components/wall.tsx`). `bun run captures <folder>` makes that folder hold the screenshots of `<folder>`: it writes each PNG, JPEG or WebP that is at least 1920 px wide as WebP at 480, 960 and 1920 px, and removes the pictures no longer in `<folder>`. A picture's file name is its name. The wall crops to 16:9. `wall.alt` in `src/copy.ts` gives a picture its alt text, and one without is named by its file name.
- The download addresses are in `src/release.ts`.
- The download page reads `CHANGELOG.md` from the root of the repository at build time and renders it with Bun's built-in Markdown. Until that file exists, the page says there are no releases yet.
- The FAQ page reads `FAQ.md` the same way, and the page on what is randomized reads `RANDOMIZED.md`. Both files must exist, or the build fails. The homepage lists each section of `RANDOMIZED.md` with its first paragraph. That page's counts of chests and gossip stones are read from `remake/chests.tsv` and `remake/stones.tsv` at build time.
- `bun run link-preview` builds the site and photographs the `/link-preview` card into `public/link-preview.png`, the image link previews show. Run it after changing the card or `copy.preview.line`.

## The API

The API's address is `https://api.kaepora.org`. Every path starts with the API's version, now `/v1`. A version only grows: an endpoint or an optional field may be added, and nothing a released client sends or reads changes or goes away, because old clients stay in use. A change that would break a released client goes into a new version beside the old one.

The same deployment answers `kaepora.org` and `api.kaepora.org`. On `api.kaepora.org`, a path that does not start with a version sends the visitor to the same path on `kaepora.org`, with a 307. That rule is a route the Nitro plugin writes into Vercel's build output (`vite.config.ts`), so Vercel answers it before the site's files.

The API is an Effect `HttpApi` in `src/api/`. `src/routes/v1/$.ts` hands every request under `/v1/` to it; the pages stay prerendered. `api.ts` is the contract, `server.ts` the handler, and `tables.ts` the tables.

### Seeds

A seed a player made is the API's root resource, at `/v1/seeds/{id}`. Everything a client says about that seed goes under it.

- **The id** is a version 7 UUID. The client makes it when the seed is made and keeps it with the seed, so it names one player's copy of a seed: two players who make the same seed have two ids. The site never shows an id, and no answer gives one to another caller. An id sorts by the time on the client's clock, which the server does not check, so anything the site must trust (a list's order, a count) goes by the server's own day of arrival.
- **The key** is what equal seeds share: two players' copies of one seed have two ids and one key. It is the first 16 hex digits of the SHA-256 of the RFC 8785 (JSON Canonicalization Scheme) form of `{ "kaepora", "seed", "settings" }`, with the seed's text in upper case first, as the generator reads it. The order of the settings' names does not change it. The server works it out and keeps it on each seed. It will name the seed's page, `kaepora.org/seeds/<key>`.

### Record a completion

`PUT /v1/seeds/{id}/completion`. The request stands alone: the server makes the seed's record from it when there is none. The body is JSON:

| Field | What it holds |
| --- | --- |
| `kaepora` | The version that made the seed, as in `"0.7.1"`. |
| `seed` | The seed's text: 1 to 255 letters and digits. |
| `settings` | The spoiler log's `settings` object as it stands. The generator owns what a setting's value is (a string, a number or a list today), so any JSON value is taken. |
| `playSeconds` | The game's play time when the seed was completed, a whole number above 0. |

A seed has one completion: the first stands, and a later request changes nothing. The answer is 201 when the completion is new and 200 when that seed already has one, both with `{ "key": "<key>" }`, the key of the seed as it is stored. A request that does not fit, an id that is not a version 7 UUID among them, gets 400 with an RFC 9457 problem (`application/problem+json`) that lists what failed. A body over 16 KiB gets 413.

### What is stored

Two tables, in Neon Postgres.

- `seeds`, one row per copy a player made: its id, its key, its version, its settings, the day it arrived by the server's UTC date, and its text only when the text is all digits, as the client's Random button makes it. Text a player typed can be a name, so it is not stored.
- `seed_completions`, at most one row per seed, under its seed's id: the play time and the day it arrived.

Nothing stores or logs the address a request came from, its user agent or any of its headers. The request log is off. When the database fails, the server logs the database's error and the failed query, without its values. Vercel itself sees each request's address and keeps it in its logs for a short time.

### Migrations

`bun --bun drizzle-kit generate --name <what changed>` writes a migration to `migrations/` after a change to `src/api/tables.ts`. `bun run migrate` applies them; `vercel build` runs it before `bun run build` (the build command in `vercel.json`), on the direct connection, so a migration that fails stops the deploy. The deployment before keeps serving while the new one builds, so a migration must work with the code before it: add a table or a column in one release, and remove one only in a later release.

## Deploy

The Vercel project is `kaepora` in the team `pixelhabits-projects`, with `web` as its root directory. `.github/workflows/web.yml` deploys it with Vercel's CLI. It runs for a change to `web/`, to a file outside it that the build reads (`CHANGELOG.md`, `FAQ.md`, `RANDOMIZED.md`, `remake/chests.tsv` and `remake/stones.tsv`), to `tools/site-database.sh` or to the workflow, and a deploy runs only after the type check, the tests and the build pass. `vercel.json` sets `git.deploymentEnabled` to `false`, so Vercel's Git integration deploys no commit that carries it. A branch made before that file changed still gets the integration's deployments until the repository is disconnected in the Vercel project's Git settings. Cancelling a run stops its steps, and a deployment Vercel has already taken goes on.

- **Production.** A push to `main` migrates production's database, builds, and deploys to production. A newer push cancels the run before it. Drizzle applies the pending migrations in one transaction, so a migration cut short changes nothing.
- **Previews.** A pull request from this repository whose merge changes `web/` deploys a preview of GitHub's merge of it into its base, the commit its checks tested, on a Neon branch of its own, `preview/<git branch>`. A pull request that changes only the content outside `web/` gets the checks, which build the site with it, and no preview. The first deploy makes the Neon branch from production's database, and each deploy migrates it and sets it to expire 7 days later, so a preview's database that no closing or deletion reaches deletes itself. A comment on the pull request gives the preview's address, and each deploy updates it. A deploy that fails fails its job and writes no comment. A newer push, or the pull request's closing, cancels a run still in progress; if Vercel had already taken that run's deploy, the preview appears all the same.
- **Clean-up.** When a pull request closes, the workflow moves its database branch aside as `closed/<git branch>/<branch id>` and gives it an expiry a day later, when Neon deletes it. Under its new name, a later pull request from the same git branch makes a new preview branch instead of finding this one. A day is short, so a closed preview soon stops counting toward the project's storage and its limit on branches, and it leaves the day of the closing to read the preview's data. A branch whose expiry is cleared in Neon's console stays. A pull request with a merge conflict starts no workflow run, not even when it closes, so the deletion of its git branch retires its database as well. That deletion runs the workflow as it is on `main`.
- **Secrets.** The deploy jobs need two repository secrets: `VERCEL_TOKEN`, a Vercel token with access to the team, and `NEON_API_KEY`, a Neon API key with access to the project. A run without them, as for a pull request from a fork or from Dependabot, skips the deploy jobs and says so in one line. The workflow names the version of Vercel's CLI it runs through `bunx`, and the ids of the Vercel team, the Vercel project and the Neon project, none of which is a secret.
- **The database.** It is Neon, installed through Vercel's Marketplace. Its integration sets the keys in `.env.example` for production only, as sensitive variables, which `vercel pull` does not hand over. So `tools/site-database.sh` asks Neon's API for a database branch's addresses, masks each in the log, and passes them to the job's later steps: `DATABASE_URL`, the pooled connection the API uses, and `DATABASE_URL_UNPOOLED`, the direct one the migrations use. A preview's deployment takes its `DATABASE_URL` from there, and production's from the project. A build without `DATABASE_URL_UNPOOLED` fails. After a call that makes or changes a branch, the script waits for the operations Neon started, looking at each every 5 s as Neon's guide to operations suggests, and stops if one does not succeed. An answer that lacks a field the script reads, such as the expiry it sent, stops the script with a message that names the call and the field. Neon refuses a call with 423 while an operation holds the project; the script sends it again after 2, 4, 8 and 16 s, and stops with Neon's message after the fifth refusal.
- `vercel.json` sets `bunVersion` to `1.4.x`, so the server runs on Bun 1.4, and sets the build command to `bun run migrate && bun run build`.
- The Nitro plugin in `vite.config.ts` builds the server. Under `vercel build` it builds for Vercel and takes the Bun version from `vercel.json`.
- A suspended instance runs no idle timeouts, so the connections its pool kept would stay open on Neon ([Vercel's guide to connection pooling with functions](https://examples.vercel.com/kb/guide/connection-pooling-with-functions)). As that guide advises, the API's pool closes a connection 5 s after its last use, and the instance is kept alive just long enough for that: after each API request, `src/routes/v1/$.ts` gives the platform's `waitUntil` a promise that settles 100 ms past the idle timeout (`src/api/server.ts`). A later request's promise settles the earlier one. A statement waits at most 5 s for a connection. Nitro puts `waitUntil` on the request. The built server run with Bun has one too, and there the promise only delays a graceful stop. The tests call the API without the route, so nothing waits.
