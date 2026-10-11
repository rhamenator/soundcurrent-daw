# SPDX-License-Identifier: GPL-3.0-only
"""Bounded in-memory Git source proof. Never imports objects into a checkout."""
import hashlib,io,json,re,zipfile
class FrozenSource:
    def __init__(self,root,commits):
        meta=json.loads((root/'source-provenance.json').read_bytes());raw=(root/'source-provenance.zip').read_bytes()
        assert meta['format']=='sc-git-source-packet-v1' and meta['commits']==list(commits)
        assert len(raw)==meta['bytes']<4*1024*1024 and hashlib.sha256(raw).hexdigest()==meta['sha256']
        self.objects={}
        with zipfile.ZipFile(io.BytesIO(raw)) as z:
            assert len(z.namelist())==len(set(z.namelist()))==meta['objects']<=2048
            assert sum(e.file_size for e in z.infolist())==meta['expandedBytes']<8*1024*1024
            for e in z.infolist():
                assert re.fullmatch(r'(commit|tree|blob)/[0-9a-f]{40}',e.filename) and e.file_size<512*1024 and not e.flag_bits&1
                kind,oid=e.filename.split('/');value=z.read(e)
                assert hashlib.sha1(kind.encode()+b' '+str(len(value)).encode()+b'\0'+value).hexdigest()==oid
                self.objects[(kind,oid)]=value
        self.roots={};self.trees={}
        for commit in commits:
            value=self.objects['commit',commit];line=value.splitlines()[0]
            assert re.fullmatch(rb'tree [0-9a-f]{40}',line)
            self.roots[commit]=line[5:].decode()
    def tree(self,oid):
        if oid in self.trees:return self.trees[oid]
        value=self.objects['tree',oid];rows={};at=0
        while at<len(value):
            space=value.index(b' ',at);nul=value.index(b'\0',space)
            mode=value[at:space];name=value[space+1:nul].decode('utf-8');end=nul+21
            assert name and name not in ('.','..') and '/'not in name and end<=len(value) and name not in rows
            rows[name]=(mode,value[nul+1:end].hex());at=end
        assert at==len(value);self.trees[oid]=rows;return rows
    def files(self,commit):
        paths=set()
        def walk(oid,prefix):
            assert prefix.count('/')<32
            for name,(mode,child)in self.tree(oid).items():
                path=prefix+name
                if mode==b'40000':walk(child,path+'/')
                elif mode in (b'100644',b'100755') and path.endswith(('.cpp','.hpp','.h','.c')):paths.add(path)
        root=self.tree(self.roots[commit])
        for family in ('include','src','ui','worker','tests'):
            if family in root:
                mode,oid=root[family];assert mode==b'40000';walk(oid,family+'/')
        assert root['CMakeLists.txt'][0]in (b'100644',b'100755');paths.add('CMakeLists.txt');return paths
    def read(self,commit,path):
        parts=path.split('/');assert parts and all(p and p not in ('.','..')for p in parts)
        oid=self.roots[commit]
        for part in parts[:-1]:
            mode,oid=self.tree(oid)[part];assert mode==b'40000'
        mode,oid=self.tree(oid)[parts[-1]];assert mode in (b'100644',b'100755')
        return self.objects['blob',oid]
