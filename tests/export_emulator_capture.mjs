import { readFile, writeFile } from 'node:fs/promises';
import { SimulatedDevice } from '../tools/ui_emulator/model/device.mjs';
const seed=JSON.parse(await readFile(new URL('../docs/ui-emulator/neon-district.seed.json',import.meta.url),'utf8'));
if(!process.argv[2])throw new Error('Output fixture path required');
const device=new SimulatedDevice(seed);
device.scan('grove');device.advance(seed.settings.scan_time_ms);device.select('grove','net-1');
device.capture('grove');device.advance(2000);
await writeFile(process.argv[2],device.readFile('grove',device.files('grove')[0].path));
