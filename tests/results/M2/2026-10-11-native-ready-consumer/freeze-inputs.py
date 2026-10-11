from pathlib import Path
import subprocess,json,hashlib
r=Path('.cache/native-ready-consumer');h=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip();assert h=='e5c7a468b790b7440bfb998ef48d7d3e6bd21d31';assert not subprocess.check_output(['git','status','--porcelain'],text=True)
base='7d800f210258d77c82b3f43fa904c6e1a29d9a6c';selected=[]
for name in subprocess.check_output(['git','ls-files','-z'],text=True).split('\0'):
 if not name or not (name.startswith(('include/','src/','cmake/','tools/','.github/','third_party/')) or (name.startswith('tests/') and not name.startswith('tests/results/')) or name in ['CMakeLists.txt','.gitattributes']):continue
 blob=subprocess.check_output(['git','rev-parse',h+':'+name],text=True).strip();raw=subprocess.check_output(['git','cat-file','blob',blob]);checkout=Path(name).read_bytes();normal=None
 if raw!=checkout:
  assert name=='.github/scripts/prepare-windows-desktop.ps1' and checkout.replace(b'\r\n',b'\n')==raw
  normal='Known .gitattributes PowerShell CRLF checkout; exact original and checkout hashes retained'
 selected.append({'path':name,'gitBlob':blob,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'checkoutBytes':len(checkout),'checkoutSha256':hashlib.sha256(checkout).hexdigest(),'normalization':normal})
x={'source':h,'tree':subprocess.check_output(['git','rev-parse','HEAD^{tree}'],text=True).strip(),'preceding':base,'changedSourceFiles':subprocess.check_output(['git','diff','--name-only',base,h],text=True).splitlines(),'selectedInputs':selected,'initialCollectorRefusal':'Wrong expected PowerShell path; failed assertion before writing manifest. Corrected to actual known .github/scripts/prepare-windows-desktop.ps1; no file changes.'}
(r/'frozen-inputs.json').write_text(json.dumps(x,indent=2)+'\n');(r/'source-delta.diff').write_bytes(subprocess.check_output(['git','diff',base,h]));print('Retained',len(selected),'exact source/checkout bindings')
