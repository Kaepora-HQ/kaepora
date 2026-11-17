import { createFileRoute } from '@tanstack/react-router'
import { HomePage } from '@/components/home-page'
import { copy } from '@/copy'

export const Route = createFileRoute('/')({
  component: () => <HomePage copy={copy} />,
})
