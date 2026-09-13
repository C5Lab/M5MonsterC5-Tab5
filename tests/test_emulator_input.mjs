import { test } from 'node:test';
import assert from 'node:assert/strict';
import { canvasPoint, rgb565ToRgba } from '../tools/ui_emulator/web/display.mjs';
import * as input from '../tools/ui_emulator/web/display.mjs';

test('one pointer owns a gesture; cancellation releases its last position exactly once', () => {
  assert.equal(typeof input.createPointerState, 'function');
  const state = input.createPointerState();
  assert.deepEqual(state.down(7, {x:12,y:34}), {x:12,y:34,pressed:1});
  assert.equal(state.down(8, {x:90,y:90}), null);
  assert.equal(state.move(8, {x:90,y:90}), null);
  assert.deepEqual(state.move(7, {x:20,y:40}), {x:20,y:40,pressed:1});
  assert.equal(state.release(8), null);
  assert.deepEqual(state.release(7), {x:20,y:40,pressed:0});
  assert.equal(state.release(7), null);
  assert.equal(state.move(7, {x:30,y:50}), null);
  assert.deepEqual(state.down(8, {x:1,y:2}), {x:1,y:2,pressed:1});
  assert.deepEqual(state.release(), {x:1,y:2,pressed:0});
});

test('CSS scaling maps to logical pixels without applying rotation twice', () => {
  assert.deepEqual(canvasPoint(190, 340, {left:10, top:20, width:360, height:640}, 720,1280), {x:360,y:640});
  assert.deepEqual(canvasPoint(330, 200, {left:10, top:20, width:640, height:360}, 1280,720), {x:640,y:360});
});
test('captured pointer outside Canvas clamps to valid display pixels', () => {
  assert.deepEqual(canvasPoint(-50, 999, {left:10,top:20,width:360,height:640},720,1280), {x:0,y:1279});
  assert.throws(() => canvasPoint(0,0,{left:0,top:0,width:0,height:0},720,1280), RangeError);
});
test('RGB565 framebuffer preserves primary colors and opaque alpha', () => {
  const result = new Uint8ClampedArray(20);
  rgb565ToRgba(new Uint16Array([0xf800,0x07e0,0x001f,0xffff,0]), result);
  assert.deepEqual([...result], [255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255,0,0,0,255]);
});
