/** The C display is already in logical orientation; CSS only scales it. */
export function createPointerState() {
  let owner = null, point = null;
  return {
    down(id, next) {
      if (owner !== null) return null;
      owner = id;
      point = next;
      return {...point, pressed: 1};
    },
    move(id, next) {
      if (owner !== id) return null;
      point = next;
      return {...point, pressed: 1};
    },
    release(id = owner) {
      if (owner === null || id !== owner) return null;
      owner = null;
      return {...point, pressed: 0};
    },
  };
}

export function canvasPoint(clientX, clientY, rect, width, height) {
  if (!(rect.width > 0 && rect.height > 0)) throw new RangeError('Canvas has no visible area');
  return {
    x: Math.max(0, Math.min(width - 1, Math.floor((clientX - rect.left) * width / rect.width))),
    y: Math.max(0, Math.min(height - 1, Math.floor((clientY - rect.top) * height / rect.height))),
  };
}

export function rgb565ToRgba(source, target) {
  for (let i = 0; i < source.length; i++) {
    const p = source[i], r = p >>> 11, g = (p >>> 5) & 63, b = p & 31;
    target[i * 4] = (r << 3) | (r >>> 2);
    target[i * 4 + 1] = (g << 2) | (g >>> 4);
    target[i * 4 + 2] = (b << 3) | (b >>> 2);
    target[i * 4 + 3] = 255;
  }
}
