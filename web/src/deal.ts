function shuffled(count: number) {
  const order = Array.from({ length: count }, (_, place) => place)
  for (let last = count - 1; last > 0; last--) {
    const pick = Math.floor(Math.random() * (last + 1))
    ;[order[last], order[pick]] = [order[pick], order[last]]
  }
  return order
}

// Runs in the page's head on every load, so the wall of pictures shows a new order each
// time. The order is settled before the first paint: the script after the wall and React
// both read it from the root element. The router runs head scripts again once it has
// loaded, so an order already dealt stays.
export const markDeal = (pictures: number) => `document.documentElement.dataset.deal ??= (${shuffled})(${pictures}).join()`
