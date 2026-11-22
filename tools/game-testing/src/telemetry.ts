/**
 * Traces go to an OTLP collector when the standard keys are set, `OTEL_EXPORTER_OTLP_ENDPOINT` and
 * `OTEL_TRACES_EXPORTER=otlp`; without them nothing is sent. Logs and metrics go too when `OTEL_LOGS_EXPORTER` or
 * `OTEL_METRICS_EXPORTER` is `otlp`. A run's record keeps the trace id of its start.
 */
import { Layer } from "effect";
import { FetchHttpClient } from "effect/http";
import { Otlp, OtlpSerialization } from "effect/observability";

export const layer = (version: string) =>
	Otlp.layerFromConfig({
		resource: { serviceName: "game-testing", serviceVersion: version },
	}).pipe(Layer.provide([FetchHttpClient.layer, OtlpSerialization.layerJson]));
