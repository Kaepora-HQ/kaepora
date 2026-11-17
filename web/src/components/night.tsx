import { owlHead } from '@/components/owl-mark'

const stars = [
  [40, 60, 1.6], [92, 168, 1.2], [64, 296, 1], [150, 34, 1.4], [528, 66, 1.8],
  [566, 188, 1.2], [496, 26, 1], [20, 226, 1.2], [584, 292, 1.4], [440, 20, 1],
] as const

export function MoonAndOwl({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 600 420" aria-hidden="true" className={className}>
      <defs>
        <radialGradient id="moonlight" cx="44%" cy="38%" r="62%">
          <stop offset="0%" style={{ stopColor: 'var(--color-moon-bright)' }} />
          <stop offset="100%" style={{ stopColor: 'var(--color-moon)' }} />
        </radialGradient>
      </defs>
      <circle cx="300" cy="196" r="196" className="fill-moon/8" />
      <circle cx="300" cy="196" r="156" fill="url(#moonlight)" />
      <g className="fill-dusk/25">
        <circle cx="236" cy="128" r="20" />
        <circle cx="384" cy="244" r="26" />
        <circle cx="356" cy="104" r="11" />
        <circle cx="222" cy="262" r="13" />
      </g>
      <g className="fill-starlight">
        {stars.map(([x, y, r]) => (
          <circle key={`${x}-${y}`} cx={x} cy={y} r={r} />
        ))}
      </g>

      <g className="fill-none stroke-silhouette" strokeLinecap="round">
        <path strokeWidth="11" d="M600 322c-60-4-130-10-200-4s-150 16-280 22" />
        <path strokeWidth="6" d="M150 338c-30 2-60 0-90-8" />
        <path strokeWidth="5" d="M500 316c12-22 28-38 50-48" />
      </g>
      <g className="fill-silhouette">
        <path d="M550 268c10-12 26-14 34-10-8 10-22 14-34 10Z" />
        <path d="M536 280c-2-14 6-26 16-30 2 12-6 24-16 30Z" />
        <path d="M74 332c-12-8-26-8-34-4 8 8 22 10 34 4Z" />
        <ellipse cx="300" cy="272" rx="52" ry="48" />
        <path fillRule="evenodd" d={owlHead} transform="translate(236 120) scale(4)" />
      </g>
      <g className="fill-dusk">
        <path d="M252 252c-8 28 0 54 24 68-8-22-10-46-24-68Z" />
        <path d="M348 252c8 28 0 54-24 68 8-22 10-46 24-68Z" />
      </g>
      <path
        className="fill-none stroke-dusk"
        strokeWidth="2.5"
        strokeLinecap="round"
        strokeLinejoin="round"
        d="M286 258l6 5 6-5M302 258l6 5 6-5M278 272l6 5 6-5M294 272l6 5 6-5M310 272l6 5 6-5M286 286l6 5 6-5M302 286l6 5 6-5"
      />
      <g className="origin-center fill-silhouette [transform-box:fill-box] motion-safe:animate-blink">
        <circle cx="279" cy="180" r="9" />
        <circle cx="321" cy="180" r="9" />
      </g>
      <path
        className="fill-none stroke-silhouette"
        strokeWidth="4"
        strokeLinecap="round"
        d="M286 314v12M293 316v12M307 316v12M314 314v12"
      />
    </svg>
  )
}

const ground = 172

function pine([x, h]: readonly [number, number]) {
  const w = h * 0.42
  const tier = ground - h * 0.5
  const skirt = ground - h * 0.12
  return `M${x} ${ground - h}L${x + w * 0.5} ${tier}H${x + w * 0.28}L${x + w * 0.62} ${skirt}H${x - w * 0.62}L${x - w * 0.28} ${tier}H${x - w * 0.5}ZM${x - 3} ${skirt}h6V${ground}h-6Z`
}

const pines = [
  [210, 96], [258, 128], [304, 84], [520, 72], [566, 110], [612, 88], [842, 118], [892, 80], [1150, 74], [1196, 106],
  [1256, 92], [1304, 124], [1346, 82], [1396, 108], [1436, 70], [1730, 64], [1766, 90],
  [2272, 86], [2322, 128], [2370, 96], [2422, 142], [2482, 104], [2540, 118], [2596, 84],
  [2700, 96], [2748, 122], [2798, 78], [3030, 108], [3080, 84], [3290, 90], [3338, 130], [3384, 100], [3436, 76], [3660, 112], [3710, 86],
] as const

const fireflies = [
  [420, 132], [980, 146], [1376, 138], [1462, 118], [1812, 148], [2240, 128], [2358, 108], [2518, 150], [2900, 124], [3540, 140],
] as const

const forest = { width: 3840, height: 200 }

export function Treeline({ className }: { className: string }) {
  return (
    <svg
      viewBox={`0 0 ${forest.width} ${forest.height}`}
      preserveAspectRatio="xMidYMax slice"
      aria-hidden="true"
      style={{ minBlockSize: `calc(100vw * ${forest.height} / ${forest.width})` }}
      className={className}
    >
      <path className="fill-dusk" d="M0 112C360 80 960 182 1200 122c120-30 240-24 360-12s240-30 400-14 280-26 440-4 180 8 240 4s360 20 600 10 400-30 600-20V200H0Z" />
      <path className="fill-silhouette" d={`M0 166C400 160 1000 176 1200 170c200-10 400-4 620-8s380-4 580 4 180-2 240-6s300 4 600 8 400-6 600-4V200H0Z${pines.map(pine).join('')}`} />
      <g className="fill-moon">
        {fireflies.map(([x, y]) => (
          <circle key={`${x}-${y}`} cx={x} cy={y} r="2.2" className="drop-shadow-[0_0_6px_var(--color-moon)]" />
        ))}
      </g>
    </svg>
  )
}

const drawingProps = {
  viewBox: '0 0 120 96',
  'aria-hidden': true,
  fill: 'none',
  strokeWidth: 2.5,
  strokeLinecap: 'round',
  strokeLinejoin: 'round',
} as const

export function OpenChest({ className }: { className?: string }) {
  return (
    <svg {...drawingProps} className={className}>
      <path className="stroke-moon" d="M60 40V12M46 42 38 18M74 42l8-24M34 46 22 30M86 46l12-16" />
      <path className="stroke-mist" d="M22 48 34 26h52l14 22M20 48h80v36H20ZM20 62h80M55 52h10v12H55Z" />
    </svg>
  )
}

export function Signpost({ className }: { className?: string }) {
  return (
    <svg {...drawingProps} className={className}>
      <path className="stroke-mist" d="M60 90V18M60 28h36l8 7-8 7H60M60 50H24l-8 7 8 7h36M44 90q6-8 8 0M68 90q6-10 9 0" />
      <path className="stroke-moon" d="M92 12v6M89 15h6" />
    </svg>
  )
}

export function Lanterns({ className }: { className?: string }) {
  return (
    <svg {...drawingProps} className={className}>
      <path className="stroke-mist" d="M40 8v8M30 22h20l-4-6H34ZM32 22h16v32H32ZM30 54h20v4H30ZM80 16v8M70 30h20l-4-6H74ZM72 30h16v32H72ZM70 62h20v4H70Z" />
      <circle cx="40" cy="38" r="5" className="fill-moon stroke-moon" />
      <circle cx="80" cy="46" r="5" className="fill-moon stroke-moon" />
    </svg>
  )
}

export function FoldedMap({ className }: { className?: string }) {
  return (
    <svg {...drawingProps} className={className}>
      <path className="stroke-mist" d="M14 24 44 16l32 8 30-8v56l-30 8-32-8-30 8ZM44 16v56M76 24v56" />
      <path className="stroke-moon" strokeDasharray="1 6" d="M24 66c12-16 26-6 36-20s24-10 34-16" />
      <path className="stroke-moon" d="m90 22 8 8m0-8-8 8" />
    </svg>
  )
}
