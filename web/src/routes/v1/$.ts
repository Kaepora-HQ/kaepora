import { createFileRoute } from '@tanstack/react-router'
import { HttpRouter } from 'effect/http'
import type { ServerRequest } from 'nitro/types'
import * as Server from '@/api/server'

const { handler } = HttpRouter.toWebHandler(Server.layer, { disableLogger: true })

// Nitro puts the platform's waitUntil on the request when there is one.
const respond = async (request: ServerRequest) => {
  const response = await handler(request)
  request.waitUntil?.(Server.aliveUntilIdleConnectionsClose())
  return response
}

export const Route = createFileRoute('/v1/$')({
  server: { handlers: { ANY: ({ request }) => respond(request) } },
})
