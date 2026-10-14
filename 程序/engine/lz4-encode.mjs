import {decodeLz4} from './lz4-block.mjs';
// Pure raw LZ4, bounded match search. Does not change NXPK indexes/checksums.
function extended(n){return n<15?0:1+Math.floor((n-15)/255);}
function cost(t){return 1+t.literal+extended(t.literal)+(t.match?2+extended(t.match-4):0);}
export function encodeLz4Sized(input,targetSize,compressed){
 const previous=new Int32Array(input.length).fill(-1),heads=new Int32Array(65536).fill(-1),tokens=[];
 const hash=p=>Math.imul(input.readUInt32LE(p),0x9e3779b1)>>>16;
 const insert=p=>{const h=hash(p);previous[p]=heads[h];heads[h]=p;};
 let p=0,anchor=0;
 if(compressed){
  if(!decodeLz4(compressed,input.length).equals(input))throw Error('Compressor output does not match input');let q=0,start=0;
  function readLength(n){if(n===15){let byte;do{if(q>=compressed.length)throw Error('Invalid length');byte=compressed[q++];n+=byte;}while(byte===255);}return n;}
  while(q<compressed.length){const token=compressed[q++],literal=readLength(token>>>4);q+=literal;let match=0,offset=0;if(q<compressed.length){offset=compressed.readUInt16LE(q);q+=2;match=readLength(token&15)+4;}tokens.push({start,literal,match,offset});start+=literal+match;}
 }else{
 while(p<=input.length-12){let best=0,source=-1,candidate=heads[hash(p)],depth=0;while(candidate>=0&&p-candidate<=65535&&depth++<128){if(input.readUInt32LE(candidate)===input.readUInt32LE(p)){let n=4;while(p+n<input.length-5&&input[candidate+n]===input[p+n])n++;if(n>best){best=n;source=candidate;if(n>1024)break;}}candidate=previous[candidate];}
 if(best<4){insert(p++);continue;}
 tokens.push({start:anchor,literal:p-anchor,match:best,offset:p-source});const end=p+best;while(p<end){if(p<=input.length-4)insert(p);p++;}anchor=p;
 }
 tokens.push({start:anchor,literal:input.length-anchor,match:0,offset:0});
 }
 let size=tokens.reduce((n,t)=>n+cost(t),0);
 if(size>targetSize)throw Error(`独立补丁压缩后 ${size} 字节超过固定槽 ${targetSize} 字节`);
 // Move initial match bytes to literals to tune the encoded size without trailing junk.
 for(const t of tokens){if(size===targetSize)break;if(t.match<=4)continue;const old=cost(t),capacity=t.match-4,remaining=targetSize-size;let low=0,high=Math.min(capacity,remaining+2);
 const increase=n=>cost({...t,literal:t.literal+n,match:t.match-n})-old;
 while(low<high){const mid=Math.ceil((low+high)/2);if(increase(mid)<=remaining)low=mid;else high=mid-1;}
 if(low){const delta=increase(low);t.literal+=low;t.match-=low;size+=delta;}
 }
 if(size!==targetSize)throw Error(`无法生成精确等长 LZ4 槽：${size}/${targetSize}`);
 const out=Buffer.alloc(size);let q=0;
 const ext=n=>{if(n<15)return;for(n-=15;n>=255;n-=255)out[q++]=255;out[q++]=n;};
 for(const t of tokens){out[q++]=(Math.min(t.literal,15)<<4)|Math.min(Math.max(t.match-4,0),15);ext(t.literal);input.copy(out,q,t.start,t.start+t.literal);q+=t.literal;if(t.match){out.writeUInt16LE(t.offset,q);q+=2;ext(t.match-4);}}
 if(q!==size||!decodeLz4(out,input.length).equals(input))throw Error('独立压缩回读校验失败');return out;
}
