"""S13/S14 native pointer navigation and guide evidence, local compiled artifact only."""
import unittest
from emulator_browser import browser_session, objects, click_object, DIST

class Queue2NettoolsBrowser(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.session=browser_session();cls.browser,cls.url=cls.session.__enter__()
 @classmethod
 def tearDownClass(cls):cls.session.__exit__(None,None,None)
 def open(self,kind,rotation):
  self.context=self.browser.new_context();self.addCleanup(self.context.close);self.page=self.context.new_page()
  self.errors=[];self.page.on('pageerror',lambda e:self.errors.append(str(e)))
  self.page.route('**/emulator.wasm',lambda r:r.fulfill(path=str(DIST/'emulator.wasm'),content_type='application/wasm'))
  self.page.goto(self.url+f'/?rotation={rotation}');self.page.wait_for_function('globalThis.emulator?.measurements.frames>2')
  self.kind=kind
  self.page.locator('#queue2-story').select_option('queue2-'+kind);self.page.locator('#guide-queue2').click()
  self.poll()
 def poll(self,n=1):
  self.page.evaluate('n=>{for(let i=0;i<n;i++)emulator.module._emu_tick(100)}',n)
  self.page.wait_for_timeout(180)
 def key(self):
  return self.page.locator('#guide-title').inner_text()
 def click(self,text=None,binding=None):
  for _ in range(18):
   rows=objects(self.page);matches=[o for o in rows if binding in o['binding']] if binding else ([o for o in rows if o['text']==text] or [o for o in rows if text in o['text']])
   if matches:
    obj=matches[-1];w,h=self.page.locator('#display').evaluate('(c)=>[c.width,c.height]');x=obj['x']+obj['width']/2;y=obj['y']+obj['height']/2
    left,top,right,bottom=0,0,w,h;scroll_parent=None;parents={o['id']:o for o in rows};parent=parents.get(obj['parent'])
    while parent:
     if not (parent['x']<x<parent['x']+parent['width'] and parent['y']<y<parent['y']+parent['height']):scroll_parent=parent
     left=max(left,parent['x']);top=max(top,parent['y']);right=min(right,parent['x']+parent['width']);bottom=min(bottom,parent['y']+parent['height']);parent=parents.get(parent['parent'])
    if left<x<right and top<y<bottom:
     click_object(self.page,obj);self.poll();return
    if scroll_parent:
     left=max(0,scroll_parent['x']);top=max(0,scroll_parent['y']);right=min(w,scroll_parent['x']+scroll_parent['width']);bottom=min(h,scroll_parent['y']+scroll_parent['height'])
    if x>=w or x<0:
     self.page.evaluate('([x,y,d])=>emulator.module._emu_wheel(x,y,d)',[w//2,max(20,min(h-20,int(y))),220 if x>=w else -220])
     box=self.page.locator('#display').bounding_box();sy=box['y']+max(20,min(h-20,y))*box['height']/h
     self.page.mouse.move(box['x']+box['width']*.75,sy);self.page.mouse.down();self.page.mouse.move(box['x']+box['width']*.25,sy,steps=10);self.page.mouse.up()
    else:self.page.evaluate('([x,y,d])=>emulator.module._emu_wheel(x,y,d)',[max(10,min(w-10,int((left+right)/2))),max(10,min(h-10,int((top+bottom)/2))),180 if y>=bottom else -180])
   else:
    self.page.evaluate('emulator.module._emu_wheel(350,500,250)')
   self.page.wait_for_timeout(150);self.poll()
  self.fail(f'Unreachable native control {text or binding}')
 def scan(self):
  self.click('WiFi Scan\n& Attack');self.page.wait_for_function('emulator.module._emu_scan_state(0)===2');self.poll()
  bssid=self.page.evaluate("emulator.device.snapshot(0).networks.find(n=>n.ssid==='AFTERLIFE-GUEST').bssid")
  self.click(binding=bssid+'/select')
 def wait_job(self):self.poll(30);self.page.wait_for_timeout(100);self.poll()
 def activity(self):
  self.poll();self.page.wait_for_timeout(1150);self.poll(20)
 def back(self,part):self.click(binding=part)
 def assert_done(self):
  self.page.wait_for_function("document.querySelector('#guide-progress').textContent.endsWith('complete')",timeout=3000)
  self.assertEqual(self.page.locator('#guide-title').inner_text(),self.kind.upper()+' complete')
  self.assertEqual(self.errors,[])
 def test_s13_all_rotations(self):
  for rotation in range(4):
   for kind in ['arp','mitm','nmap']:
    with self.subTest(rotation=rotation,kind=kind):
     self.open(kind,rotation);self.scan();self.click(kind.upper() if kind!='nmap' else 'Nmap')
     if kind=='mitm':
      self.click('Connect & Start');self.wait_job();self.activity();self.click('STOP CAPTURE');self.wait_job();self.click('Cancel')
     else:
      self.click('Connect');self.wait_job();self.click('List Hosts');self.wait_job()
      if kind=='arp':
       self.click(binding='192.0.2.10/host');self.wait_job();self.activity();self.click('STOP');self.back('show_arp_poison_page.back_btn')
      else:
       for level in ['quick','medium','heavy']:
        self.click('Nmap All Hosts') if level=='heavy' else self.click(binding='192.0.2.10/host')
        self.click(binding=level+'/scan-level');self.wait_job();self.assertIn('80/tcp','\n'.join(o['text'] for o in objects(self.page)));self.click('STOP')
       self.back('show_nmap_page.back_btn')
     self.poll();self.assert_done();self.context.close()
 def test_s14_both_entries_all_rotations(self):
  for rotation in range(4):
   for kind in ['gitm-scan','gitm-global']:
    with self.subTest(rotation=rotation,kind=kind):
     self.open(kind,rotation)
     if kind.endswith('scan'):self.scan()
     else:self.click('Global WiFi\nAttacks')
     self.click('GITM');self.click('SCAN');self.wait_job();self.click('AFTERLIFE-GUEST');self.click('CONNECT');self.wait_job()
     self.click(binding='/ap:AP SSID:/field');self.page.keyboard.type('SIMULATED-GATEWAY')
     kb=next(o for o in objects(self.page) if 'show_gitm_page.ctx--gitm--keyboard' in o['binding'])
     click_object(self.page,{'x':kb['x']+kb['width']*.94,'y':kb['y']+kb['height']*.875,'width':1,'height':1})
     self.click('START GITM');self.wait_job();self.activity();self.back('show_gitm_page.back_btn');self.click('Keep capturing');self.back('show_gitm_page.back_btn');self.click('Stop and exit');self.wait_job();self.assert_done()
     self.assertTrue(self.page.evaluate("emulator.device.device.files('grove').some(f=>f.kind==='gitm')"));self.context.close()

 def test_gitm_stop_copy_and_missing_sd(self):
  self.open('gitm-global',0);self.click('Global WiFi\nAttacks');self.click('GITM');self.click('SCAN');self.wait_job();self.click('AFTERLIFE-GUEST');self.click('CONNECT');self.wait_job()
  self.click(binding='/ap:AP SSID:/field');self.page.keyboard.type('SIMULATED-GATEWAY')
  kb=next(o for o in objects(self.page) if 'show_gitm_page.ctx--gitm--keyboard' in o['binding'])
  click_object(self.page,{'x':kb['x']+kb['width']*.94,'y':kb['y']+kb['height']*.875,'width':1,'height':1})
  self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:false})");self.click('START GITM');self.wait_job()
  self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"));self.assertFalse(self.page.locator('#guide-progress').inner_text().endswith('complete'))
  self.page.evaluate("emulator.device.device.setModule('grove',{sdPresent:true})");self.click('START GITM');self.wait_job();self.click('STOP GITM');self.wait_job()
  self.assertIn('Simulated PCAP finalized','\n'.join(o['text'] for o in objects(self.page)))
  self.click('COPY TO TAB5 / ESPSHARK');self.wait_job();self.assertIn('.pcap','\n'.join(o['text'] for o in objects(self.page)));self.assertEqual(self.errors,[])

 def test_gitm_global_after_cancelled_scan_entry(self):
  self.open('gitm-scan',0);self.scan();self.click('GITM');self.back('show_gitm_page.back_btn')
  self.page.locator('#guide-leave').click();self.page.locator('#queue2-story').select_option('queue2-gitm-global');self.page.locator('#guide-queue2').click()
  self.click('Global WiFi\nAttacks');self.click('GITM');self.poll()
  self.assertEqual(self.key(),'Scan upstream networks')

 def test_nmap_secured_connect_accessible_all_rotations(self):
  for rotation in range(4):
   with self.subTest(rotation=rotation):
    self.open('nmap',rotation);self.click('WiFi Scan\n& Attack');self.page.wait_for_function('emulator.module._emu_scan_state(0)===2')
    self.click(binding='02:20:77:00:00:01/select');self.click('Nmap');self.click('Connect')
    self.assertIn('Enter password first','\n'.join(o['text'] for o in objects(self.page)))
    self.click(binding='show_nmap_page.nmap_password_input');self.page.keyboard.type('demo-only')
    self.click(binding='show_nmap_page.nmap_keyboard')
    keyboard=next(o for o in objects(self.page) if 'show_nmap_page.nmap_keyboard' in o['binding'])
    click_object(self.page,{'x':keyboard['x']+keyboard['width']*.94,'y':keyboard['y']+keyboard['height']*.875,'width':1,'height':1})
    self.poll();self.click('Connect');self.wait_job();self.click('List Hosts');self.wait_job()
    self.assertTrue(any(o['binding'].endswith('192.0.2.10/host') for o in objects(self.page)))
    self.assertEqual(self.key(),'Configure NMAP');self.context.close()

 def test_busy_connect_retries_without_crediting_other_job(self):
  self.open('nmap',0);self.scan();self.click('Nmap')
  busy=self.page.evaluate("emulator.device.device.toolStart('grove','iot')")
  self.click('Connect');self.poll()
  self.assertEqual(self.key(),'Connect to the selected network')
  self.assertEqual(self.page.evaluate("emulator.device.device.snapshot('grove').active"),busy)
  self.page.evaluate('(id)=>emulator.device.device.cancel(id)',busy);self.click('Connect');self.wait_job()
  self.assertEqual(self.key(),'Discover synthetic hosts')

 def test_s13_cancel_and_disconnect_do_not_complete(self):
  for kind in ['arp','mitm','nmap']:
   with self.subTest(kind=kind):
    self.open(kind,0);self.scan();self.click(kind.upper() if kind!='nmap' else 'Nmap')
    if kind=='mitm':
     before=self.page.evaluate("emulator.device.device.files('grove').length")
     self.click('Cancel');self.wait_job();self.assertEqual(self.page.evaluate("emulator.device.device.files('grove').length"),before)
    else:
     self.click('Connect');self.wait_job();self.click('List Hosts');self.wait_job();self.click(binding='192.0.2.10/host')
     if kind=='nmap':self.click('Cancel');self.assertEqual(self.key(),'Quick scan: 192.0.2.10');self.click(binding='192.0.2.10/host');self.click(binding='quick/scan-level')
     self.page.evaluate("emulator.device.device.setModule('grove',{connected:false})");self.wait_job()
     self.assertIsNone(self.page.evaluate("emulator.device.device.snapshot('grove').active"))
    self.assertFalse(self.page.locator('#guide-progress').inner_text().endswith('complete'));self.context.close()

if __name__=='__main__':unittest.main()
