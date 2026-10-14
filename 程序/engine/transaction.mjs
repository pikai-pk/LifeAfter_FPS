import fs from 'node:fs';import path from 'node:path';import crypto from 'node:crypto';
import {readExact} from './archive.mjs';import {digest} from './patch.mjs';
export function outsideHash(fd,size,offset,length){
 const h=crypto.createHash('sha256'),buffer=Buffer.alloc(1024*1024);
 for(const [start,end] of [[0,offset],[offset+length,size]])for(let p=start;p<end;){const n=fs.readSync(fd,buffer,0,Math.min(buffer.length,end-p),p);if(!n)throw Error('包体校验读取不完整');h.update(buffer.subarray(0,n));p+=n;}
 return h.digest('hex');
}
function write(fd,data,offset){let n=0;while(n<data.length){const count=fs.writeSync(fd,data,n,data.length-n,offset+n);if(!count)throw Error('写入未完成');n+=count;}fs.fsyncSync(fd);}
export function transaction({archive,offset,data,expectedSlot,expectedOutside,backupRoot,metadata,beforeWrite,verify,afterWrite=()=>{}}){
 const lockPath=archive+'.independent-fps.lock';let lock,fd,old,backupDir,manifest,changed=false;
 try{
  try{lock=fs.openSync(lockPath,'wx');}catch{throw Error('写入锁不可用，请检查权限或已有操作');}fs.writeFileSync(lock,JSON.stringify({pid:process.pid,startedAt:new Date().toISOString()}));
  fd=fs.openSync(archive,'r+');const st=fs.fstatSync(fd);if(offset<32||offset+data.length>st.size)throw Error('写入范围无效');
  old=readExact(fd,data.length,offset);if(digest(old)!==expectedSlot||outsideHash(fd,st.size,offset,data.length)!==expectedOutside)throw Error('游戏包在预检后已变化，停止写入');
  fs.mkdirSync(backupRoot,{recursive:true});backupDir=fs.mkdtempSync(path.join(backupRoot,new Date().toISOString().replace(/[:.]/g,'-')+'-'));
  const backup=path.join(backupDir,'original.slot.bin');fs.writeFileSync(backup,old,{flag:'wx'});if(digest(fs.readFileSync(backup))!==expectedSlot)throw Error('备份校验失败');
  manifest={schema:1,tool:'independent-lifeafter-fps',createdAt:new Date().toISOString(),...metadata,archive:fs.realpathSync(archive),archiveSize:st.size,offset,slotLength:data.length,outsideHash:expectedOutside,originalHash:expectedSlot,targetHash:digest(data),status:'backed-up'};
  const save=()=>fs.writeFileSync(path.join(backupDir,'manifest.json'),JSON.stringify(manifest,null,2));save();beforeWrite();
  const current=fs.statSync(archive);if(current.dev!==st.dev||current.ino!==st.ino||current.size!==st.size||digest(readExact(fd,data.length,offset))!==expectedSlot||outsideHash(fd,st.size,offset,data.length)!==expectedOutside)throw Error('备份后包体或文件路径变化，停止写入');
  changed=true;write(fd,data,offset);afterWrite(fd);
  const reread=readExact(fd,data.length,offset);if(digest(reread)!==manifest.targetHash||fs.fstatSync(fd).size!==st.size||outsideHash(fd,st.size,offset,data.length)!==expectedOutside)throw Error('写入重读检查失败');verify(reread);
  const currentAfter=fs.statSync(archive);if(currentAfter.dev!==st.dev||currentAfter.ino!==st.ino||currentAfter.size!==st.size)throw Error('写入期间文件路径被替换');
  manifest.status='verified';save();return{backupDir,manifest};
 }catch(error){
  let rollback='not-needed';if(changed&&old&&fd!==undefined)try{write(fd,old,offset);if(digest(readExact(fd,old.length,offset))!==expectedSlot)throw Error('恢复摘要不符');rollback='slot-restored';}catch{rollback='failed';}
  if(manifest){manifest.status=rollback==='slot-restored'?'rolled-back':'failed';manifest.rollback=rollback;try{fs.writeFileSync(path.join(backupDir,'manifest.json'),JSON.stringify(manifest,null,2));}catch{}}
  const failure=new Error(error.message);Object.assign(failure,{backupDir,rollback,gameModified:rollback==='failed'?null:false});throw failure;
 }finally{if(old)old.fill(0);if(fd!==undefined)fs.closeSync(fd);if(lock!==undefined){fs.closeSync(lock);try{fs.unlinkSync(lockPath);}catch{}}}
}
