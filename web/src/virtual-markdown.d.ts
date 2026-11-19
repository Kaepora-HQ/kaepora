declare module 'virtual:changelog' {
  const html: string | null
  export default html
}

declare module 'virtual:faq' {
  const html: string
  export default html
  /** The level-2 headings, with the ids the HTML gives them. */
  export const sections: { id: string; title: string }[]
}
