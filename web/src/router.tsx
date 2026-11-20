import { createRouter } from '@tanstack/react-router'
import { routeTree } from './routeTree.gen'

export function getRouter() {
  // Without this the router scrolls a page it has just hydrated to its top, which undoes the place a reload came back to.
  return createRouter({ routeTree, scrollRestoration: true })
}
