from pathlib import Path
import hashlib,json,subprocess,tempfile,shutil

root=Path(__file__).resolve().parents[1]
manifest=json.loads((root/'versions/manifest.json').read_text(encoding='utf-8'))
temp_root=(root/'.patch-check').resolve()
assert temp_root.is_relative_to(root.resolve())
temp_root.mkdir(exist_ok=True)
def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()
for version,record in manifest.items():
    patch=root/'versions'/version/'changes.patch'
    assert digest(patch)==record['patch_sha256'],version
    with tempfile.TemporaryDirectory(prefix='lb-patch-check-',dir=temp_root) as task_dir:
        temp=Path(task_dir)
        assert temp.resolve().is_relative_to(temp_root)
        shutil.copytree(root/'baseline',temp,dirs_exist_ok=True)
        subprocess.run(['git','init','-q',str(temp)],check=True)
        subprocess.run(['git','-C',str(temp),'config','core.autocrlf','false'],check=True)
        for args in [['--check'],[]]:
            subprocess.run(['git','-C',str(temp),'apply',*args,str(patch)],check=True)
        for path,expected in record['files'].items():
            assert digest(root/'versions'/version/path)==expected,(version,path,'snapshot')
            assert digest(temp/path)==expected,(version,path,'patch output')
    print(version+' patch and source hashes match')
print(f'All {len(manifest)} independent patches verified')
