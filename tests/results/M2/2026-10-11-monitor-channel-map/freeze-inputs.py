from pathlib import Path
import subprocess,json,hashlib
r=Path('.cache/monitor-channel-map');h=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert h=='aecc0b79934a89a428e914e61ee20eb294538f85';assert not subprocess.check_output(['git','status','--porcelain'],text=True)
base='143c76af5e1fedc301ec6c9c5208f23f3738bf63';selected=[]
for name in subprocess.check_output(['git','ls-files','-z'],text=True).split('\0'):
 if not name or not (name.startswith(('include/','src/','cmake/','tools/','.github/','third_party/')) or (name.startswith('tests/') and not name.startswith('tests/results/')) or name in ['CMakeLists.txt','.gitattributes']):continue
 blob=subprocess.check_output(['git','rev-parse',h+':'+name],text=True).strip();raw=subprocess.check_output(['git','cat-file','blob',blob]);checkout=Path(name).read_bytes();normal=None
 if raw!=checkout:
  assert name=='.github/scripts/prepare-windows-desktop.ps1' and checkout.replace(b'\r\n',b'\n')==raw
  normal='Known .gitattributes PowerShell CRLF checkout; exact original and checkout hashes retained'
 selected.append({'path':name,'gitBlob':blob,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'checkoutBytes':len(checkout),'checkoutSha256':hashlib.sha256(checkout).hexdigest(),'normalization':normal})
x={'source':h,'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip(),'preceding':base,'changedSourceFiles':subprocess.check_output(['git','diff','--name-only',base,h],text=True).splitlines(),'selectedInputs':selected}
(r/'frozen-inputs.json').write_text(json.dumps(x,indent=2)+'\n');(r/'source-delta.diff').write_bytes(subprocess.check_output(['git','diff',base,h]));print('Retained',len(selected),'exact source/checkout bindings')
