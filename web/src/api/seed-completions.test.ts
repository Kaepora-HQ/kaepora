import { afterAll, expect, test } from 'bun:test'
import { eq } from 'drizzle-orm'
import { makeWithDefaults } from 'drizzle-orm/effect-postgres'
import { Effect, Layer, ManagedRuntime } from 'effect'
import { HttpRouter } from 'effect/http'
import { layerNoDeps } from './server'
import { seedCompletions, seeds } from './tables'
import * as TestDatabase from './test-database'
import betweenTwoPrices from './test-seeds/between-two-prices.json'
import onePrice from './test-seeds/one-price.json'

const database = ManagedRuntime.make(TestDatabase.layer)
const api = HttpRouter.toWebHandler(layerNoDeps.pipe(Layer.provide(TestDatabase.layer)), {
  disableLogger: true,
  memoMap: database.memoMap,
})
afterAll(async () => {
  await api.dispose()
  await database.dispose()
})

const complete = (id: string, body: string) =>
  api.handler(
    new Request(`https://api.kaepora.org/v1/seeds/${id}/completion`, {
      method: 'PUT',
      headers: { 'content-type': 'application/json' },
      body,
    }),
  )

const stored = (id: string) =>
  database.runPromise(
    Effect.gen(function* () {
      const drizzle = yield* makeWithDefaults()
      return {
        seeds: yield* drizzle.select().from(seeds).where(eq(seeds.id, id)),
        completions: yield* drizzle.select().from(seedCompletions).where(eq(seedCompletions.seed, id)),
      }
    }),
  )

const everything = () =>
  database.runPromise(
    Effect.gen(function* () {
      const drizzle = yield* makeWithDefaults()
      return { seeds: yield* drizzle.select().from(seeds), completions: yield* drizzle.select().from(seedCompletions) }
    }),
  )

const todayInUtc = () => Temporal.Now.plainDateISO('UTC').toString()

const settings = {
  'Shuffled dungeons': ['Deku Tree', 'Forest Temple', "Ganon's Castle"],
  'Small and boss keys': 'Anywhere in their dungeon',
  'Rainbow Bridge': 'After the Light Arrows',
  Skip: ['Opening movie', 'Tutorial messages'],
  'Start with': [],
  'Gossip stones': 'Give hints',
}
const completion = { kaepora: '0.7.1', seed: '6332265119', settings, playSeconds: 36125 }

test('a new completion is recorded, answers 201 with the seed key, and its seed has a record', async () => {
  const id = '0192f6a1-7c3e-7b9a-8f00-000000000001'
  const before = todayInUtc()
  const answer = await complete(id, JSON.stringify(completion))
  const after = todayInUtc()

  expect(answer.status).toBe(201)
  expect(await answer.json()).toEqual({ key: '0c1c0e90e99b3c06' })
  const recorded = await stored(id)
  expect(recorded.seeds).toEqual([
    {
      id,
      key: '0c1c0e90e99b3c06',
      version: '0.7.1',
      settings,
      text: '6332265119',
      arrivedOn: expect.any(String),
    },
  ])
  expect(recorded.completions).toEqual([{ seed: id, playSeconds: 36125, arrivedOn: expect.any(String) }])
  expect([before, after]).toContain(recorded.seeds[0]?.arrivedOn)
  expect([before, after]).toContain(recorded.completions[0]?.arrivedOn)
})

test('the same request sent again answers 200 and adds nothing', async () => {
  const id = '0192f6a1-7c3e-7b9a-8f00-000000000002'
  const again = JSON.stringify({ ...completion, seed: '1111111111' })
  const first = await complete(id, again)
  const before = await everything()

  const second = await complete(id, again)

  expect(first.status).toBe(201)
  expect(second.status).toBe(200)
  expect(await second.json()).toEqual(await first.json())
  expect(await everything()).toEqual(before)
})

test("two players' copies of one seed, whatever the order of its settings and the case of its text, are two seeds with one key", async () => {
  const first = '0192f6a1-7c3e-7b9a-8f00-000000000003'
  const second = '0192f6a1-7c3e-7b9a-8f00-000000000004'
  const reordered = Object.fromEntries(Object.entries(settings).reverse())

  const answers = [
    await complete(first, JSON.stringify({ ...completion, seed: 'Kokiri' })),
    await complete(second, JSON.stringify({ ...completion, seed: 'KOKIRI', settings: reordered, playSeconds: 41000 })),
  ]

  expect(answers.map((answer) => answer.status)).toEqual([201, 201])
  const [{ key }, secondKey] = await Promise.all(answers.map((answer) => answer.json()))
  expect(secondKey).toEqual({ key })
  expect((await stored(first)).seeds.map((row) => row.key)).toEqual([key])
  expect((await stored(second)).seeds.map((row) => row.key)).toEqual([key])
})

test('a seed whose text is all digits keeps its text, and one with a letter keeps none', async () => {
  const digits = '0192f6a1-7c3e-7b9a-8f00-000000000005'
  const named = '0192f6a1-7c3e-7b9a-8f00-000000000006'
  await complete(digits, JSON.stringify({ ...completion, seed: '0123456789' }))
  await complete(named, JSON.stringify({ ...completion, seed: 'Saria4ever' }))

  expect((await stored(digits)).seeds.map((row) => row.text)).toEqual(['0123456789'])
  expect((await stored(named)).seeds.map((row) => row.text)).toEqual([null])
})

test('settings that hold a number and a list of numbers are recorded as they came', async () => {
  const cases = [
    { id: '0192f6a1-7c3e-7b9a-8f00-000000000008', seed: onePrice, key: '86faeaccc6640f1d' },
    { id: '0192f6a1-7c3e-7b9a-8f00-000000000009', seed: betweenTwoPrices, key: 'f525fc1862d21d17' },
  ]

  for (const { id, seed, key } of cases) {
    const answer = await complete(id, JSON.stringify({ ...seed, playSeconds: 52000 }))

    expect(answer.status).toBe(201)
    expect(await answer.json()).toEqual({ key })
    expect((await stored(id)).seeds.map((row) => row.settings)).toEqual([seed.settings])
  }
})

test('a request outside the contract is refused and records nothing', async () => {
  const before = await everything()
  const id = '0192f6a1-7c3e-7b9a-8f00-000000000007'
  const broken = [
    'not json',
    JSON.stringify({ ...completion, kaepora: 'v0.7.1' }),
    JSON.stringify({ ...completion, seed: '' }),
    JSON.stringify({ ...completion, seed: 'Link is cool' }),
    JSON.stringify({ ...completion, seed: 'A'.repeat(256) }),
    JSON.stringify({ ...completion, settings: ['Opening movie'] }),
    JSON.stringify({ ...completion, playSeconds: 0 }),
    JSON.stringify({ ...completion, playSeconds: 1.5 }),
    JSON.stringify({ ...completion, playSeconds: 2 ** 31 }),
    JSON.stringify({ kaepora: '0.7.1', seed: '6332265119', settings }),
  ]

  for (const body of broken) {
    const answer = await complete(id, body)
    expect(answer.status).toBe(400)
    expect(answer.headers.get('content-type')).toBe('application/problem+json')
    expect(await answer.json()).toMatchObject({ type: 'about:blank', status: 400, title: 'Bad Request' })
  }
  expect((await complete('seed-7', JSON.stringify(completion))).status).toBe(400)
  expect((await complete('0192f6a1-7c3e-4b9a-8f00-000000000007', JSON.stringify(completion))).status).toBe(400)
  const tooLarge = { ...completion, settings: { ...settings, Padding: 'x'.repeat(64 * 1024) } }
  expect((await complete(id, JSON.stringify(tooLarge))).status).toBe(413)
  expect(await everything()).toEqual(before)
})
