import { sql } from 'drizzle-orm'
import { date, integer, jsonb, snakeCase, text, uuid } from 'drizzle-orm/pg-core'
import type { SeedCompletion } from './api'

const arrivedOn = () =>
  date({ mode: 'string' })
    .notNull()
    .default(sql`(now() at time zone 'UTC')::date`)

export const seeds = snakeCase.table('seeds', {
  id: uuid().primaryKey(),
  key: text().notNull(),
  version: text().notNull(),
  settings: jsonb().$type<SeedCompletion['settings']>().notNull(),
  // Only the text the Random button makes, all digits: text a player typed can be a name.
  text: text(),
  arrivedOn: arrivedOn(),
})

export const seedCompletions = snakeCase.table('seed_completions', {
  seed: uuid()
    .primaryKey()
    .references(() => seeds.id),
  playSeconds: integer().notNull(),
  arrivedOn: arrivedOn(),
})
