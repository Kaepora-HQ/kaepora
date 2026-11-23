import { PgClient } from '@effect/sql-pg'
import canonicalize from 'canonicalize'
import { eq } from 'drizzle-orm'
import { makeWithDefaults } from 'drizzle-orm/effect-postgres'
import { ByteSize, Config, Duration, Effect, Layer } from 'effect'
import { HttpServer, HttpServerRequest } from 'effect/http'
import { HttpApiBuilder, HttpApiMiddleware } from 'effect/http-api'
import { Api, type SeedCompletion, SeedCompletionAlreadyRecorded, SeedCompletionRecorded } from './api'
import { seedCompletions, seeds } from './tables'

const maxBodySize = ByteSize.kibibytes(16)

// canonicalize returns no text only for a value JSON cannot hold.
const seedKey = (seed: Pick<SeedCompletion, 'kaepora' | 'seed' | 'settings'>) =>
  Bun.CryptoHasher.hash('sha256', canonicalize(seed)!, 'hex').slice(0, 16)

const recordCompletions = HttpApiBuilder.group(Api, 'seeds', (handlers) =>
  Effect.gen(function* () {
    const drizzle = yield* makeWithDefaults()
    return handlers.handle('recordCompletion', ({ params, payload }) =>
      Effect.gen(function* () {
        // The generator reads a seed's text in upper case.
        const text = payload.seed.toUpperCase()
        return yield* drizzle.transaction((transaction) =>
          Effect.gen(function* () {
            yield* transaction
              .insert(seeds)
              .values({
                id: params.id,
                key: seedKey({ kaepora: payload.kaepora, seed: text, settings: payload.settings }),
                version: payload.kaepora,
                settings: payload.settings,
                text: /^[0-9]+$/u.test(text) ? text : null,
              })
              .onConflictDoNothing()
            const added = yield* transaction
              .insert(seedCompletions)
              .values({ seed: params.id, playSeconds: payload.playSeconds })
              .onConflictDoNothing()
              .returning({ seed: seedCompletions.seed })
            const [{ key }] = yield* transaction.select({ key: seeds.key }).from(seeds).where(eq(seeds.id, params.id))
            return added.length > 0 ? SeedCompletionRecorded.make({ key }) : SeedCompletionAlreadyRecorded.make({ key })
          }),
        )
      }).pipe(
        Effect.tapCause(Effect.logError),
        Effect.orDie,
      ),
    )
  }),
)

export const layerNoDeps = HttpApiBuilder.layer(Api).pipe(
  Layer.provide(recordCompletions),
  Layer.provide(HttpApiMiddleware.layerProblemDetails({ includeDetails: true })),
  Layer.provide(HttpServer.layerServices),
  Layer.provide(Layer.succeed(HttpServerRequest.MaxBodySize, maxBodySize)),
)

// Vercel's guide to connection pools on functions: a suspended instance runs no idle timeouts, so the connections its
// pool kept stay open on the database. The guide advises a short idle timeout, five seconds in its example, and keeping
// the instance alive briefly after a request so the pool can close them.
const idleTimeout = Duration.seconds(5)
// A statement that finds every connection busy fails after this long.
const acquireTimeout = Duration.seconds(5)
// Timers fire a little late, so the instance stays alive this much longer than the idle timeout.
const aliveMargin = Duration.millis(100)

export const layer = layerNoDeps.pipe(
  Layer.provide(
    PgClient.layerConfig({
      url: Config.Redacted('DATABASE_URL'),
      idleTimeout: Config.succeed(idleTimeout),
      acquireTimeout: Config.succeed(acquireTimeout),
    }),
  ),
)

let settleEarlier: (() => void) | undefined

// A promise for the platform's waitUntil: the instance stays alive until it settles, which is once the pool has had time
// to close the connections the request used. Each call settles the one before it, so only the latest request's promise
// is pending.
export const aliveUntilIdleConnectionsClose = () =>
  new Promise<void>((resolve) => {
    settleEarlier?.()
    const timer = setTimeout(resolve, Duration.toMillis(Duration.sum(idleTimeout, aliveMargin)))
    settleEarlier = () => {
      clearTimeout(timer)
      resolve()
    }
  })
