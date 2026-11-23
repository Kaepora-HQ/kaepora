import { PgClient } from '@effect/sql-pg'
import { makeWithDefaults } from 'drizzle-orm/effect-postgres'
import { migrate } from 'drizzle-orm/effect-postgres/migrator'
import { Effect, Layer, Redacted } from 'effect'
import { SqlClient } from 'effect/sql'

const server = 'postgres://postgres:postgres@localhost:5432/postgres'

export const layer = Layer.unwrap(
  Effect.gen(function* () {
    const name = `test_${Bun.randomUUIDv7('hex')}`
    const onServer = (statement: (sql: SqlClient.SqlClient) => Effect.Effect<unknown, unknown>) =>
      SqlClient.SqlClient.pipe(
        Effect.flatMap(statement),
        Effect.provide(PgClient.layer({ url: Redacted.make(server) })),
        Effect.orDie,
      )
    yield* Effect.acquireRelease(
      onServer((sql) => sql`CREATE DATABASE ${sql(name)}`),
      () => onServer((sql) => sql`DROP DATABASE ${sql(name)} WITH (FORCE)`),
    )
    const url = new URL(server)
    url.pathname = `/${name}`
    const migrated = Layer.effectDiscard(
      Effect.flatMap(makeWithDefaults(), (drizzle) =>
        migrate(drizzle, { migrationsFolder: `${import.meta.dir}/../../migrations` }),
      ).pipe(Effect.orDie),
    )
    return migrated.pipe(Layer.provideMerge(PgClient.layer({ url: Redacted.make(url.href) })))
  }),
)
