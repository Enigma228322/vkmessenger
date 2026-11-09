.pragma library

function ratio(photo) {
    const sizes = (photo.photo || {}).sizes || []
    let size = null
    for (const candidate of sizes) if (candidate.width > 0 && candidate.height > 0 && (!size || candidate.width * candidate.height > size.width * size.height)) size = candidate
    return size ? Math.max(0.65, Math.min(1.8, size.width / size.height)) : 1
}
function layout(photos, width, gap) {
    if (!photos.length || width <= 0) return {tiles: [], height: 0}
    const rows = Math.max(1, Math.round(Math.sqrt(photos.length)))
    const base = Math.floor(photos.length / rows), extra = photos.length % rows
    const tiles = []
    let index = 0, y = 0
    for (let row = 0; row < rows; ++row) {
        const count = base + (row >= rows - extra ? 1 : 0)
        const rowGap = Math.min(gap, width / (count * 2))
        const available = width - rowGap * (count - 1)
        let sum = 0
        for (let i = 0; i < count; ++i) sum += ratio(photos[index + i])
        const height = Math.max(1, Math.min(photos.length === 1 ? 420 : width * 0.85, available / sum))
        let x = 0
        for (let i = 0; i < count; ++i) {
            const tileWidth = i === count - 1 ? width - x : available * ratio(photos[index]) / sum
            tiles.push({index: index, x: x, y: y, width: tileWidth, height: height})
            x += tileWidth + rowGap; ++index
        }
        y += height + gap
    }
    return {tiles: tiles, height: y - gap}
}
function thumbnail(photo, width, height) {
    const sizes = (photo.photo || {}).sizes || []
    let selected = null, largest = null
    for (const size of sizes) {
        if (!size.url) continue
        const area = (size.width || 0) * (size.height || 0)
        if (!largest || area > (largest.width || 0) * (largest.height || 0)) largest = size
        if (size.width >= width && size.height >= height && (!selected || area < selected.width * selected.height)) selected = size
    }
    return (selected || largest || {}).url || ""
}
