export const owlHead =
  'M2.5 5.5C5.5 6 8.5 7 10.5 8.5h11c2-1.5 5-2.5 8-3L28 17c0 7-5.4 12.5-12 12.5S4 24 4 17Z M10.75 9.75a5.25 5.25 0 1 0 0 10.5 5.25 5.25 0 0 0 0-10.5Z M21.25 9.75a5.25 5.25 0 1 0 0 10.5 5.25 5.25 0 0 0 0-10.5Z M16 18.5l1.6 2.5-1.6 3.5-1.6-3.5Z'

export function OwlMark({ className }: { className?: string }) {
  return (
    <svg viewBox="0 0 32 32" aria-hidden="true" className={className}>
      <path fill="currentColor" fillRule="evenodd" d={owlHead} />
      <circle cx="10.75" cy="15" r="2.5" className="fill-moon" />
      <circle cx="21.25" cy="15" r="2.5" className="fill-moon" />
    </svg>
  )
}
