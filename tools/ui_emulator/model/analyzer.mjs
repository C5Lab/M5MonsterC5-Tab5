/** WFA/1 records for the native Tab5 analyzer. Source observations are scenario data. */
const channels24 = Array.from({length: 14}, (_, i) => i + 1);
const channels5 = [36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112,
  116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177];
const allChannels = [...channels24, ...channels5];
const auth = {WPA2: 'WPA2_PSK', WPA3: 'WPA3_PSK', 'WPA2/WPA3 Mixed': 'WPA2_WPA3_PSK', Open: 'OPEN'};
const utf8 = new TextEncoder();
const line = (prefix, record) => `[${prefix}] ${JSON.stringify(record)}\n`;

export function analyzerRequest(request) {
  if (!request || !['both', '2.4', '5'].includes(request.band) ||
      !['quick', 'detailed', 'passive'].includes(request.profile) ||
      !Number.isInteger(request.limit) || request.limit < 1 || request.limit > 128 ||
      typeof request.channels !== 'string' || request.channels.length > 159)
    throw new Error('Invalid analyzer request');
  const allowed = request.band === 'both' ? allChannels : request.band === '2.4' ? channels24 : channels5;
  const channels = request.channels === '' ? [...allowed] : request.channels.split(',').map(part => {
    if (!/^(?:[1-9]|[1-9][0-9]|1[0-7][0-9])$/.test(part)) throw new Error('Invalid analyzer channel plan');
    return Number(part);
  });
  if (channels.length === 0 || channels.length > 42 || new Set(channels).size !== channels.length ||
      channels.some(channel => !allowed.includes(channel))) throw new Error('Invalid analyzer channel plan');
  return {...request, channels};
}

function geometry(network) {
  const channel = network.channel;
  const frequency = channel === 14 ? 2484 : channel <= 14 ? 2407 + 5 * channel : 5000 + 5 * channel;
  if (channel <= 14) return {bandwidth: '20', secondary: 'none', center1_mhz: frequency, center2_mhz: null};
  const center = channel <= 48 ? 5210 : channel >= 149 && channel <= 161 ? 5775 : 0;
  if (!center) return {bandwidth: null, secondary: null, center1_mhz: null, center2_mhz: null};
  return {bandwidth: '80', secondary: channel === 40 || channel === 48 || channel === 153 || channel === 161 ? 'below' : 'above',
    center1_mhz: center, center2_mhz: null};
}

export function analyzerLines(networks, request, scan, startedMs, durationMs) {
  const options = analyzerRequest(request);
  const selected = networks.filter(network => options.channels.includes(network.channel));
  const returned = selected.slice(0, options.limit);
  const boot = 'c5e0000000000001';
  const identity = {v: 1, boot, scan};
  const lines = [line('WFACTL1', {v: 1, type: 'caps', protocol: 'WFA/1', default_records: 64,
    max_records: 128, max_line_bytes: 1024, bands: ['2.4', '5'], profiles: ['quick', 'detailed', 'passive'],
    width_metadata: 'sdk_unverified', channel_scope: 'requested_driver_filtered'}),
    line('WFA1', {...identity, type: 'begin', limit: options.limit, band: options.band,
      channels: options.channels, profile: options.profile, started_ms: Math.floor(startedMs)})];
  returned.forEach((network, seq) => {
    const bytes = utf8.encode(network.ssid ?? '').slice(0, 32);
    const ssid_hex = Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
    lines.push(line('WFA1', {...identity, type: 'ap', seq, bssid: network.bssid,
      ssid_hex, band: network.channel <= 14 ? '2.4' : '5', primary: network.channel,
      rssi: network.rssi, auth: auth[network.security] ?? 'UNKNOWN',
      phy: network.channel <= 14 ? ['11n'] : ['11ac'], ...geometry(network)}));
  });
  lines.push(line('WFA1', {...identity, type: 'end', status: 'ok', found: selected.length,
    returned: returned.length, truncated: selected.length > returned.length, duration_ms: durationMs}));
  if (lines.some(record => utf8.encode(record.trimEnd()).length > 1024)) throw new Error('Analyzer record too long');
  return lines;
}
