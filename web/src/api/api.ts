import { Schema } from 'effect'
import { HttpApi, HttpApiEndpoint, HttpApiGroup, HttpApiMiddleware } from 'effect/http-api'

export const SeedCompletion = Schema.Struct({
  kaepora: Schema.String.check(Schema.isPattern(/^[0-9]+\.[0-9]+\.[0-9]+$/u)),
  seed: Schema.String.check(Schema.isPattern(/^[A-Za-z0-9]{1,255}$/u)),
  // The generator owns what a setting's value is, so any JSON value is taken.
  settings: Schema.JsonObject,
  playSeconds: Schema.Finite.check(Schema.isInt32(), Schema.isGreaterThan(0)),
})
export type SeedCompletion = typeof SeedCompletion.Type

export const SeedKey = Schema.String.check(Schema.isPattern(/^[0-9a-f]{16}$/u))

export class SeedCompletionRecorded extends Schema.Class<SeedCompletionRecorded, { readonly brand: unique symbol }>(
  'SeedCompletionRecorded',
)({ key: SeedKey }, { httpApiStatus: 201 }) {}

export class SeedCompletionAlreadyRecorded extends Schema.Class<
  SeedCompletionAlreadyRecorded,
  { readonly brand: unique symbol }
>('SeedCompletionAlreadyRecorded')({ key: SeedKey }, { httpApiStatus: 200 }) {}

export const Api = HttpApi.make('kaepora')
  .add(
    HttpApiGroup.make('seeds').add(
      HttpApiEndpoint.put('recordCompletion', '/seeds/:id/completion', {
        params: { id: Schema.String.check(Schema.isUUID(7)) },
        payload: SeedCompletion,
        success: [SeedCompletionRecorded, SeedCompletionAlreadyRecorded],
      }),
    ),
  )
  .prefix('/v1')
  .middleware(HttpApiMiddleware.ProblemDetails)
