import { defineConfig } from 'drizzle-kit'

export default defineConfig({
  dialect: 'postgresql',
  schema: './src/api/tables.ts',
  out: './migrations',
  // Neon asks that migrations run on the direct connection, not the pooled one.
  dbCredentials: { url: process.env.DATABASE_URL_UNPOOLED },
})
