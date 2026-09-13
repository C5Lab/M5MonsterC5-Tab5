"""Validate the extraction SHA-256 compatibility core against hashlib."""
import hashlib,os,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
code=r'''
#include <stdio.h>
#include "mbedtls/sha256.h"
int main(void){mbedtls_sha256_context c;unsigned char b[7],out[32];size_t n;
mbedtls_sha256_init(&c);if(mbedtls_sha256_starts(&c,0))return 1;
while((n=fread(b,1,sizeof(b),stdin)))if(mbedtls_sha256_update(&c,b,n))return 2;
if(mbedtls_sha256_finish(&c,out))return 3;mbedtls_sha256_free(&c);
for(int i=0;i<32;i++)printf("%02x",out[i]);return 0;}
'''
with tempfile.TemporaryDirectory(prefix='tab5-sha-') as folder:
    p=Path(folder);(p/'test.c').write_text(code)
    def native(path):
        return subprocess.check_output(['wsl','--exec','wslpath','-a',str(path)],text=True).strip() if os.name=='nt' else str(path)
    prefix=['wsl','--exec'] if os.name=='nt' else []
    subprocess.run(prefix+['cc','-std=c11','-fsanitize=address,undefined','-I'+native(ROOT/'tools/ui_emulator/runtime'),native(ROOT/'tools/ui_emulator/vendor/sha256.c'),native(p/'test.c'),'-o',native(p/'test')],check=True)
    for data in [b'',b'abc',b'a'*55,b'a'*56,b'a'*64,b'a'*65,bytes(range(256))*17,b'a'*1000000]:
        result=subprocess.run(prefix+[native(p/'test')],input=data,capture_output=True,check=True)
        assert result.stdout.decode()==hashlib.sha256(data).hexdigest()
print('PASS: 8 SHA-256 vectors, streaming chunks and padding boundaries, ASan/UBSan')
