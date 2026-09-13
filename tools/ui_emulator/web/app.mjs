import { canvasPoint, createPointerState, rgb565ToRgba } from './display.mjs';
import { BrowserDevice } from './model/bridge.mjs';
import { SettingsStore } from './persistence.mjs';
import { mountBluetoothGuide } from './stories.mjs';
import { wifiDemoSeed } from './wifi-stories.mjs';
import {encodeRestartHandoff,consumeRestartHandoff} from './story-restart.mjs';

const restartHandoff=consumeRestartHandoff(location,history);

const canvas = document.querySelector('#display');
const status = document.querySelector('#status');
const notice = document.querySelector('#notice');
const rotation = document.querySelector('#rotation');
const timing = document.querySelector('#timing');
const cancel = document.querySelector('#cancel');
const started = performance.now();
let disk, session;
try { disk = globalThis.localStorage; } catch { /* Browser denied storage. */ }
try { session = globalThis.sessionStorage; } catch { /* Browser denied storage. */ }
const settings = globalThis.emulatorSettings = new SettingsStore(disk, session);
const startupUrl = new URL(location.href);
const moduleCondition = document.querySelector('#module-condition');
const wifiCondition = document.querySelector('#wifi-condition');
const wifiVariant = startupUrl.searchParams.get('wifiDemo');
wifiCondition.value = ['hidden','empty','cancel'].includes(wifiVariant)?wifiVariant:'normal';
wifiCondition.addEventListener('change',()=>{
  const url=new URL(location.href);
  if(wifiCondition.value==='normal')url.searchParams.delete('wifiDemo');
  else url.searchParams.set('wifiDemo',wifiCondition.value);
  location.assign(url);
});
const conditionValue = startupUrl.searchParams.get('moduleCondition');
moduleCondition.value = ['missing-board','missing-sd','version-mismatch'].includes(conditionValue)?conditionValue:'normal';
moduleCondition.addEventListener('change',()=>{
  const url=new URL(location.href);
  if(moduleCondition.value==='normal')url.searchParams.delete('moduleCondition');
  else url.searchParams.set('moduleCondition',moduleCondition.value);
  location.assign(url);
});
if (startupUrl.searchParams.get('reset') === '1') {
  settings.reset();
  // If both storage tiers refuse writes, keep the reset intent in the URL so
  // reload cannot resurrect a readable but stale settings record.
  if (settings.mode !== 'memory') {
    startupUrl.searchParams.delete('reset');
    history.replaceState(null, '', startupUrl);
  }
}
const savedRotation = settings.get('scr_rot', 0);
function storageStatus() {
  const element = document.querySelector('#storage-status');
  const text =
    (settings.recovered ? 'Unrecognized settings were reset. ' : '') +
    (settings.mode === 'persistent' ? 'Settings saved in this browser.' : settings.mode === 'session'
      ? 'Settings last for this tab session; browser storage is unavailable.'
      : 'Settings last until reload; browser storage is unavailable.');
  if (element.textContent !== text) element.textContent = text;
}
storageStatus();
const rotationParameter = new URL(location.href).searchParams.get('rotation');
const requestedRotation = rotationParameter === null ? savedRotation : Number(rotationParameter);
rotation.value = String([0, 1, 2, 3].includes(requestedRotation) ? requestedRotation : 0);
rotation.addEventListener('change', () => {
  const url = new URL(location.href);
  url.searchParams.set('rotation', rotation.value);
  location.assign(url);
});
document.querySelector('#reset').addEventListener('click', () => {
  settings.reset();
  const url = new URL(location.href);
  url.searchParams.set('rotation', '0');
  url.searchParams.set('reset', '1');
  url.searchParams.delete('moduleCondition');
  url.searchParams.delete('wifiDemo');
  location.assign(url);
});
document.querySelector('#fullscreen').addEventListener('click', async () => {
  try {
    if (document.fullscreenElement) await document.exitFullscreen();
    else await document.querySelector(document.querySelector('#guide-panel').hidden?'.stage':'main').requestFullscreen();
  } catch { notice.textContent = 'Full screen is unavailable in this browser.'; }
});
globalThis.addEventListener('emulator-unavailable', event => {
  notice.textContent = `This prototype does not yet support ${event.detail}. Use Home, Settings, Wi-Fi Scan or Network Observer.`;
});
// A refused model operation must be visible; the C side cannot throw across the boundary.
globalThis.emulatorModelErrors = [];
globalThis.addEventListener('emulator-model-error', event => {
  globalThis.emulatorModelErrors.push(event.detail);
  notice.textContent = `Simulated device refused the operation: ${event.detail}`;
});

async function downloadWasm() {
  for (let attempt = 1; attempt <= 3; attempt++) {
    try {
      const response = await fetch(new URL('./emulator.wasm', import.meta.url));
      if (!response.ok) throw new Error(`Emulator download failed (${response.status})`);
      return new Uint8Array(await response.arrayBuffer());
    } catch (error) {
      if (attempt === 3) throw error;
      status.textContent = `Download interrupted. Retrying (${attempt + 1}/3)…`;
      await new Promise(resolve => setTimeout(resolve, 100 * attempt));
    }
  }
}

try {
  const scenarioResponse = await fetch(new URL('./scenario.json',import.meta.url));
  if (!scenarioResponse.ok) throw new Error('Scenario could not be loaded');
  const device = new BrowserDevice(wifiDemoSeed(await scenarioResponse.json(),wifiCondition.value));
  globalThis.emulatorDevice = device;
  const wasmBinary = await downloadWasm();
  const module = await globalThis.createEmulator({
    wasmBinary,
    locateFile: path => new URL(path, import.meta.url).href,
    onAbort: reason => { status.textContent = `Emulator stopped: ${reason}. Reset the demo to retry.`; },
  });
  if (!module._emu_init(Number(rotation.value))) throw new Error('Device initialization failed');
  if(wifiCondition.value==='cancel'){module._emu_set_timing(1);timing.value='1';}
  canvas.width = module._emu_width();
  canvas.height = module._emu_height();
  const context = canvas.getContext('2d', {alpha: false});
  const frame = context.createImageData(canvas.width, canvas.height);
  const pointer = createPointerState();
  const send = point => { if (point) module._emu_pointer(point.x, point.y, point.pressed); };
  const point = event => canvasPoint(event.clientX, event.clientY, canvas.getBoundingClientRect(), canvas.width, canvas.height);
  canvas.addEventListener('pointerdown', event => {
    if (event.button !== 0) return;
    const update = pointer.down(event.pointerId, point(event));
    if (!update) return;
    canvas.focus();
    canvas.setPointerCapture(event.pointerId);
    send(update);
    event.preventDefault();
  });
  canvas.addEventListener('pointermove', event => send(pointer.move(event.pointerId, point(event))));
  canvas.addEventListener('pointerup', event => {
    send(pointer.move(event.pointerId, point(event)));
    send(pointer.release(event.pointerId));
  });
  const cancelPointer = id => { if (pointer.release(id)) module._emu_pointer_cancel(); };
  for (const name of ['pointercancel', 'lostpointercapture']) canvas.addEventListener(name, event => cancelPointer(event.pointerId));
  globalThis.addEventListener('blur', () => cancelPointer());
  document.addEventListener('visibilitychange', () => { if (document.hidden) cancelPointer(); });
  canvas.addEventListener('wheel', event => {
    const p = point(event);
    const pixels = event.deltaY * (event.deltaMode === 1 ? 20 : event.deltaMode === 2 ? canvas.height : 1);
    module._emu_wheel(p.x, p.y, Math.max(-300, Math.min(300, pixels)));
    event.preventDefault();
  }, {passive: false});
  canvas.addEventListener('keydown', event => {
    if (event.ctrlKey || event.metaKey || event.altKey) return;
    if (event.key === 'Backspace') module._emu_key(8);
    else if (event.key === 'Enter') module._emu_key(13);
    else if (event.key === 'Escape') module._emu_key(27);
    else if (event.key.length === 1) module.ccall('emu_text', null, ['string'], [event.key]);
    else return;
    event.preventDefault();
  });
  const textInput = document.querySelector('#text-input');
  const transferText = event => {
    if (event.isComposing) return;
    if (event.inputType === 'deleteContentBackward') module._emu_key(8);
    else if (textInput.value) module.ccall('emu_text', null, ['string'], [textInput.value]);
    textInput.value = '';
  };
  textInput.addEventListener('input', transferText);
  textInput.addEventListener('compositionend', transferText);
  textInput.addEventListener('keydown', event => {
    if (event.key === 'Enter' || event.key === 'Escape' || event.key === 'Backspace') {
      module._emu_key(event.key === 'Enter' ? 13 : event.key === 'Escape' ? 27 : 8);
      event.preventDefault();
    }
  });
  timing.disabled = false;
  timing.addEventListener('change', () => {
    if (!module._emu_set_timing(Number(timing.value))) {
      timing.value = timing.value === '0' ? '1' : '0';
      notice.textContent = 'Finish or cancel the current operation before changing speed.';
    }
  });
  cancel.addEventListener('click', () => {
    const tab=module._emu_current_tab();
    if (!module._emu_scan_cancel(tab) && [0,2,3].includes(tab)) {
      device.cancel(device.snapshot(tab).active);
    }
    notice.textContent = 'Simulated operation cancelled.';
  });
  const measurements = {startupMs: 0, frames: 0, totalFrameMs: 0, maxFrameMs: 0, memoryBytes: 0};
  document.querySelector('#pcap-examples').addEventListener('click', () => {
    try {
      const tab=module._emu_current_tab();
      device.device.installPcapFixtures(device.module(tab));
      if(!module._emu_load_pcap_examples())throw new Error('Could not open local PCAP files');
      notice.textContent='Loaded offline HTTP, DNS and ICMP examples plus an invalid-file example.';
    } catch(error) { notice.textContent=error.message; }
  });
  // Read-only diagnostics and C entry points for the acceptance harness.
  globalThis.emulator = {module, measurements, device};
  const liveGps=document.querySelector('#live-gps'),liveSd=document.querySelector('#live-sd');
  liveGps.disabled=liveSd.disabled=false;
  liveSd.value=device.snapshot(0).sdPresent?'available':'missing';
  liveGps.addEventListener('change',()=>device.device.wardriveSetGps('grove',liveGps.value==='available'));
  liveSd.addEventListener('change',()=>device.device.setModule('grove',{sdPresent:liveSd.value==='available'}));
  const guide=mountBluetoothGuide(document.querySelector('.guide'),()=>({tab:module._emu_current_tab(),visible:!document.hidden}),()=>module._emu_startup_state(),()=>{module._emu_wifi_story_state();return globalThis.emulatorWifiEvidence;},kind=>{module._emu_recon_story_state(kind==='mesh'?1:0);return globalThis.emulatorReconEvidence;},entry=>entry.read(module,device));
  guide.resumeRestart(restartHandoff);
  let previous = performance.now();
  function render(now) {
    try {
      const frameStart = performance.now();
      guide.syncContext();
      module._emu_tick(now - previous);
      storageStatus();
      const restart = module._emu_restart_requested();
      if (restart) {
        const url = new URL(location.href);
        url.searchParams.set('rotation', String(restart - 1));
        const handoff=guide.prepareRestart(restart-1);
        if(handoff)url.hash=encodeRestartHandoff(handoff.id,handoff.payload);
        location.assign(url);
        return;
      }
      previous = now;
      const source = new Uint16Array(module.HEAPU8.buffer, module._emu_framebuffer(), canvas.width * canvas.height);
      rgb565ToRgba(source, frame.data);
      context.putImageData(frame, 0, 0);
      // CSS is the browser backlight adapter; no physical display is accessed.
      canvas.style.filter = `brightness(${module._emu_brightness() / 100})`;
      const duration = performance.now() - frameStart;
      measurements.frames++;
      measurements.totalFrameMs += duration;
      measurements.maxFrameMs = Math.max(measurements.maxFrameMs, duration);
      measurements.memoryBytes = module.HEAPU8.byteLength;
      if (measurements.frames === 1) measurements.startupMs = performance.now() - started;
      const tab=module._emu_current_tab();
      const snapshots=new Map([0,2,3].map(id=>[id,device.snapshot(id)]));
      const job=snapshots.has(tab) ? device.device.job(snapshots.get(tab).active) : null;
      const busy = job?.state === 'running';
      cancel.disabled = !busy;
      timing.disabled = [...snapshots.values()].some(state => state.active !== null);
      const connected=[0,2].filter(id=>snapshots.get(id).connected).length;
      status.textContent = busy ? (job.kind==='capture' ? 'Generating synthetic PCAP…' : 'Simulated operation running…') : `Ready · ${connected}/2 external boards connected`;
      if (measurements.frames % 30 === 1) document.querySelector('#metrics').textContent =
        `Startup ${measurements.startupMs.toFixed(0)} ms\nAverage frame ${(measurements.totalFrameMs / measurements.frames).toFixed(1)} ms\nPeak frame ${measurements.maxFrameMs.toFixed(1)} ms\nWasm memory ${(measurements.memoryBytes / 1048576).toFixed(0)} MiB`;
      requestAnimationFrame(render);
    } catch (error) { status.textContent = `Emulator stopped: ${error.message}. Reset the demo to retry.`; }
  }
  requestAnimationFrame(render);
} catch (error) {
  status.textContent = `Could not load the emulator: ${error.message}. Serve the built folder over HTTP and reset to retry.`;
}
