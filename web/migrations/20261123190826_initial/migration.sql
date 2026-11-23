CREATE TABLE "seed_completions" (
	"seed" uuid PRIMARY KEY,
	"play_seconds" integer NOT NULL,
	"arrived_on" date DEFAULT (now() at time zone 'UTC') NOT NULL
);
--> statement-breakpoint
CREATE TABLE "seeds" (
	"id" uuid PRIMARY KEY,
	"key" text NOT NULL,
	"version" text NOT NULL,
	"settings" jsonb NOT NULL,
	"text" text,
	"arrived_on" date DEFAULT (now() at time zone 'UTC') NOT NULL
);
--> statement-breakpoint
ALTER TABLE "seed_completions" ADD CONSTRAINT "seed_completions_seed_seeds_id_fkey" FOREIGN KEY ("seed") REFERENCES "seeds"("id");