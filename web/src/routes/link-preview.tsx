import { createFileRoute } from '@tanstack/react-router'
import { MoonAndOwl, Treeline } from '@/components/night'
import { copy } from '@/copy'
import { previewCard } from '@/preview-card'

export const Route = createFileRoute('/link-preview')({
  head: () => ({ meta: [{ name: 'robots', content: 'noindex' }] }),
  component: LinkPreview,
})

function LinkPreview() {
  return (
    <div style={previewCard} className="relative overflow-clip bg-linear-to-b from-midnight to-dusk">
      <div className="absolute inset-bs-0 inset-s-0 flex h-[470px] w-[620px] flex-col justify-center gap-6 ps-20">
        <p className="storybook text-[124px] leading-none font-semibold">Kaepora</p>
        <p className="storybook text-[44px] leading-tight text-mist">{copy.preview.line}</p>
      </div>
      <MoonAndOwl className="absolute inset-bs-6 inset-e-10 w-[560px]" />
      <Treeline className="absolute inset-be-0 h-[160px] w-full" />
    </div>
  )
}
