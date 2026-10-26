import test from 'node:test';
import assert from 'node:assert/strict';
import {AnalyzerGuide} from '../tools/ui_emulator/web/analyzer-story.mjs';

const context = {tab: 0, visible: true};
const state = (overrides = {}) => ({...context, page: false, home: true, scan: 0,
  state: 0, selected: false, proposal: false, ...overrides});

test('analyzer guide advances only on a fresh committed scan and native controls', () => {
  const guide = new AnalyzerGuide();
  guide.start(state({scan: 7}));
  guide.setContext(context);
  guide.accept(state({page: true, home: false, scan: 7}));
  assert.equal(guide.step, 1);
  guide.accept(state({page: true, home: false, scan: 7, state: 2}));
  guide.accept(state({page: true, home: false, scan: 8, state: 3}));
  assert.equal(guide.step, 1);
  guide.accept(state({page: true, home: false, scan: 8, state: 2}));
  assert.equal(guide.step, 2);
  guide.accept(state({page: true, home: false, scan: 8, state: 2, selected: true}));
  assert.equal(guide.step, 3);
  guide.accept(state({page: true, home: false, scan: 8, state: 2, selected: true, proposal: true}));
  assert.equal(guide.step, 4);
  guide.accept(state({page: false, home: true, scan: 8, state: 2}));
  assert.equal(guide.complete, true);
});

test('analyzer guide ignores other ports and requires a new scan after restart', () => {
  const guide = new AnalyzerGuide();
  guide.start(state({scan: 1}));
  guide.setContext(context);
  guide.accept(state({page: true, home: false}));
  guide.setContext({tab: 2, visible: true});
  guide.accept(state({tab: 2, page: true, scan: 4, state: 2}));
  assert.equal(guide.step, 1);
  guide.setContext(context);
  guide.start(state({scan: 4}));
  guide.accept(state({page: true, home: false, scan: 4, state: 2}));
  assert.equal(guide.step, 1);
});
