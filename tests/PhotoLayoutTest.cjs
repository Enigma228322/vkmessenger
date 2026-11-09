const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const context = vm.createContext({});
vm.runInContext(fs.readFileSync(require('node:path').join(__dirname, '../qml/PhotoLayout.js'), 'utf8').replace(/^\.pragma library\s*/, ''), context);
const photo = (width, height) => ({type: 'photo', photo: {sizes: [{url: 'https://assets.test/thumb', width, height}]}});
test('collages fit the message, preserve order and never overlap', () => {
  for (let count = 1; count <= 30; count++) for (const width of [1, 80, 240, 420, 580]) {
    const photos = Array.from({length: count}, (_, i) => i % 2 ? photo(900, 1600) : photo(1600, 900));
    const result = context.layout(photos, width, 3);
    assert.equal(result.tiles.length, count);
    for (let i = 0; i < count; i++) {
      const tile = result.tiles[i];
      assert.equal(tile.index, i);
      assert.ok(tile.width > 0 && tile.height > 0);
      assert.ok(tile.x >= 0 && tile.y >= 0 && tile.x + tile.width <= width + 0.001);
      assert.ok(tile.y + tile.height <= result.height + 0.001);
      for (const previous of result.tiles.slice(0, i)) {
        assert.ok(tile.x >= previous.x + previous.width - 0.001 || previous.x >= tile.x + tile.width - 0.001 || tile.y >= previous.y + previous.height - 0.001 || previous.y >= tile.y + tile.height - 0.001);
      }
    }
  }
  assert.equal(context.layout([], 420, 3).height, 0);
});
test('thumbnail selection uses the smallest sufficient size and handles unsorted sizes', () => {
  const item = {photo: {sizes: [{url: 'large', width: 1600, height: 1200}, {url: 'small', width: 100, height: 75}, {url: 'medium', width: 400, height: 300}]}};
  assert.equal(context.thumbnail(item, 200, 150), 'medium');
  assert.equal(context.thumbnail(item, 800, 600), 'large');
  assert.equal(context.thumbnail(item, 3000, 2000), 'large');
  assert.equal(context.thumbnail({photo: {sizes: []}}, 100, 100), '');
  assert.equal(context.ratio(item), 4 / 3);
});
