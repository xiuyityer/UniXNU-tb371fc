#!/usr/bin/env python3
"""Prepare complete app install metadata on an offline local Data volume (21H461).

Default is read-only planning. --apply backs up changed metadata/xattrs/caches.
No device I/O and no historical physical-page delta is reused.
"""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import shutil
import stat

ATTRIBUTE='com.apple.installd.installType'
VALUE=(10).to_bytes(8,'little')

def metadata_type(path):
    m=plistlib.loads(path.read_bytes());objects=m['$objects']
    record=objects[m['$top']['root'].data]
    value=record['installType']
    return objects[value.data] if isinstance(value,plistlib.UID) else value

def local(root,path):
    path.resolve().relative_to(root)
    if path.is_symlink(): raise ValueError('Unexpected symlink: '+str(path))
    return path

def attr(path):
    try:return os.getxattr(path,ATTRIBUTE)
    except OSError as e:
        if e.errno in (getattr(__import__('errno'),'ENOATTR',93),getattr(__import__('errno'),'ENODATA',61)):return None
        raise

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--data-root',type=Path,required=True)
    ap.add_argument('--metadata-root',type=Path,help='Native metadata backup: <container>/BundleMetadata.plist; otherwise current metadata then V132 backup')
    ap.add_argument('--apply',action='store_true');a=ap.parse_args()
    root=a.data_root.resolve()
    if str(root)=='/' or not root.is_dir() or str(root).startswith('/dev/'):
        raise ValueError('Expected an offline local Data mount, not system root/device')
    containers=local(root,root/'containers/Bundle/Application')
    rows=[]
    for target in sorted(containers.iterdir()):
        if not target.is_dir():continue
        local(root,target)
        apps=list(target.glob('*.app'))
        if len(apps)!=1:raise ValueError('Expected one complete app per container')
        app=local(root,apps[0]);info=plistlib.loads(local(root,app/'Info.plist').read_bytes())
        executable=local(root,app/info['CFBundleExecutable'])
        if not executable.is_file() or (app/'PlaceholderEntitlements.plist').exists():
            raise ValueError('Complete original app required: '+str(app))
        current=local(root,target/'BundleMetadata.plist')
        source=(a.metadata_root/target.name/'BundleMetadata.plist') if a.metadata_root else current
        if not a.metadata_root:
            try:valid=metadata_type(current)==10
            except (FileNotFoundError,KeyError,ValueError,IndexError):valid=False
            if not valid:source=root/'db/tb-waiting-repair-v132/placeholder-bundles'/target.name/'BundleMetadata.plist'
        if source.is_symlink() or metadata_type(source)!=10:raise ValueError('Native installType 10 metadata required: '+str(source))
        old=attr(app)
        rows.append(dict(app=app,current=current,source=source,executable=executable,
            executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),old_attr=old,
            changed=not current.exists() or current.read_bytes()!=source.read_bytes() or old!=VALUE))
    if not rows:raise ValueError('No apps; stage full original apps before initializing Data')
    changed=[r for r in rows if r['changed']]
    summary=dict(apps=len(rows),changes=len(changed),apply=a.apply,install_type=10,attribute_bytes=8)
    if a.apply and changed:
        backup=root/'db'/('tb-app-metadata-v135-'+datetime.datetime.now().strftime('%Y%m%dT%H%M%S%f'))
        backup.mkdir(parents=True,exist_ok=False)
        caches=[local(root,p) for p in (root/'mobile/Containers/Data/InternalDaemon').glob('*/Library/Caches/*.csstore')]
        plan=[]
        # Save every preimage before mutating metadata or cache files.
        for row in changed:
            dest=backup/'metadata'/row['current'].parent.name/'BundleMetadata.plist';dest.parent.mkdir(parents=True)
            if row['current'].exists():shutil.copy2(row['current'],dest)
            plan.append(dict(app=str(row['app'].relative_to(root)),metadata=str(row['current'].relative_to(root)),metadata_existed=row['current'].exists(),old_attribute_hex=None if row['old_attr'] is None else row['old_attr'].hex(),executable_sha256=row['executable_sha256']))
        (backup/'plan.json').write_text(json.dumps(dict(apps=plan,caches=[str(p.relative_to(root)) for p in caches]),indent=2)+'\n')
        for row in changed:
            content=row['source'].read_bytes()
            if row['current'].exists():row['current'].write_bytes(content)
            else:
                shutil.copy2(row['source'],row['current'])
                ownership=row['current'].parent.stat();os.chown(row['current'],ownership.st_uid,ownership.st_gid)
            os.setxattr(row['app'],ATTRIBUTE,VALUE)
            if metadata_type(row['current'])!=10 or attr(row['app'])!=VALUE or hashlib.sha256(row['executable'].read_bytes()).hexdigest()!=row['executable_sha256']:
                raise ValueError('Postwrite app verification failed; inspect saved preimages')
        for cache in caches:
            dest=backup/'caches'/cache.relative_to(root);dest.parent.mkdir(parents=True,exist_ok=True);cache.rename(dest)
        summary['backup']=str(backup)
    print(json.dumps(summary,indent=2))

if __name__=='__main__':main()
