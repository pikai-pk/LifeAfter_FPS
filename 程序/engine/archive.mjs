import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
export function readExact(fd,n,p){const b=Buffer.alloc(n);let at=0;while(at<n){const count=fs.readSync(fd,b,at,n-at,p+at);if(!count)throw Error('文件读取不完整');at+=count;}return b;}
function decrypt(b){const key=Buffer.from([96,99,8,216,163,44,120,32,19,210,108,47,34,111,104,109]),c=crypto.createDecipheriv('aes-128-ecb',key,null);c.setAutoPadding(false);return Buffer.concat([c.update(b),c.final()]);}
export function openArchive(archive,recipe){
 const fd=fs.openSync(archive,'r');try{const size=fs.fstatSync(fd).size;if(size<1048576||size>0xffffffff)throw Error('NXPK 文件大小不支持');const h=decrypt(readExact(fd,32,0));if(h.toString('ascii',8,12)!=='NXPK'||h.readUInt32LE(12)!==3)throw Error('游戏包不是支持的 NXPK v3');const offset=h.readUInt32LE(16),count=h.readUInt32LE(20);const length=Math.ceil(count*48/16)*16;if(count<1||count>500000||offset<32||offset+length>size)throw Error('NXPK 索引边界无效');const index=decrypt(readExact(fd,length,offset)),matches=[];
 for(let i=0;i<count;i++){const at=i*48;if(index.readUInt32LE(at)!==recipe.nameHash||index.readUInt32LE(at+4)!==recipe.nameId)continue;const slot=index.readUInt32LE(at+8),compressed=index.readUInt32LE(at+12),original=index.readUInt32LE(at+16),compression=index.readUInt32LE(at+28);if(compressed!==recipe.slotSize||original!==recipe.decodedSize||compression!==2)throw Error('SettingManager 长度或压缩格式不匹配');if(slot<32||slot+compressed>offset)throw Error('SettingManager 槽越过数据边界');matches.push({archive,size,offset:slot,length:compressed,original,compression,index:i,data:readExact(fd,compressed,slot)});}
 if(matches.length!==1)throw Error('SettingManager 未找到或存在重复记录');return matches[0];
 }finally{fs.closeSync(fd);}
}
export function policy(archive){
 const dir=path.dirname(archive),file=path.join(dir,'file_hash_pack.bin');let known=false,protectedFile=false,theme=null;
 try{const b=fs.readFileSync(file);known=b.toString('ascii',0,4)==='FHPK';const name=Buffer.from(path.basename(archive),'latin1'),prefix=Buffer.alloc(2);prefix.writeUInt16LE(name.length);protectedFile=known&&b.indexOf(Buffer.concat([prefix,name]))>=0;}catch{}
 try{const c=JSON.parse(fs.readFileSync(path.join(dir,'configs/clientconfig'),'utf8'));theme=c.last_login_server_theme;}catch{}
 const block=!known?'未识别到完整性清单，停止写入':typeof theme!=='string'||!theme?'无法确认上次区服，停止写入':protectedFile&&theme.toLowerCase()==='yk'?'此受保护区服不支持修改游戏包':null;
 return{known,protected:protectedFile,serverTheme:theme,block};
}
