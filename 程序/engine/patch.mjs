import crypto from 'node:crypto';
export const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
export function buildFrame(recipe,fps){
 if(!Number.isInteger(fps)||fps<120||fps>1000)throw Error('目标 FPS 必须为 120–1000 的整数');
 const frame=Buffer.from(recipe.frameBase64,'base64'),bc=recipe.bytecode,c=recipe.fields.consts,q=recipe.fields.qualname,l=recipe.fields.linetable;
 if(frame.length!==recipe.frameEnd-recipe.frameStart||!frame.subarray(bc.start,bc.start+2).equals(Buffer.from([0x80,0]))||frame[c.start]!==0x2e||frame[c.start+1]!==2||frame[l.data_start]!==0x80||frame[l.data_start+1]!==0)throw Error('补丁模板与审核结构不匹配');
 const bytes=(tag,data)=>{const b=Buffer.alloc(5+data.length);b[0]=tag;b.writeUInt32LE(data.length,1);data.copy(b,5);return b;};
 const bytecode=Buffer.concat([frame.subarray(bc.start,bc.start+2),Buffer.from(recipe.opcodePrefix),frame.subarray(bc.start+2,bc.end)]);
 const constant=Buffer.alloc(5);constant[0]=0x3e;constant.writeInt32LE(fps,1);
 const consts=Buffer.concat([Buffer.from([0x2e,3]),frame.subarray(c.start+2,c.end),constant]);
 const qual=Buffer.from(recipe.patchedQualname,'utf8'),qualname=Buffer.concat([Buffer.from([frame[q.start],qual.length]),qual]);
 const lines=Buffer.concat([frame.subarray(l.data_start,l.data_start+2),Buffer.from(recipe.linePrefix),frame.subarray(l.data_start+2,l.end)]);
 const edits=[{start:bc.type_start,end:bc.end,data:bytes(frame[bc.type_start],bytecode)},{start:c.start,end:c.end,data:consts},{start:q.start,end:q.end,data:qualname},{start:l.start,end:l.end,data:bytes(frame[l.start],lines)}];
 let result=frame;for(const e of edits.sort((a,b)=>b.start-a.start))result=Buffer.concat([result.subarray(0,e.start),e.data,result.subarray(e.end)]);
 if(result.length!==frame.length)throw Error('函数序列化长度不一致');return result;
}
export function identifyModule(data,recipe){
 if(data.length!==recipe.decodedSize)throw Error('SettingManager 原始长度不匹配，游戏版本未支持');
 if(digest(data)===recipe.decodedSha256)return{kind:'official',fps:null};
 const frame=data.subarray(recipe.frameStart,recipe.frameEnd),constantAt=recipe.fields.consts.end+recipe.opcodePrefix.length;
 if(frame[constantAt]!==0x3e)throw Error('SettingManager 内容未知，停止修改');
 const fps=frame.readInt32LE(constantAt+1);
 if(!frame.equals(buildFrame(recipe,fps)))throw Error('检测到未知函数修改，停止写入');
 const normalized=Buffer.from(data);Buffer.from(recipe.frameBase64,'base64').copy(normalized,recipe.frameStart);
 if(digest(normalized)!==recipe.decodedSha256)throw Error('SettingManager 其他函数发生变化，停止写入');
 return{kind:'independent-patch',fps};
}
export function patchModule(data,recipe,fps){
 const before=identifyModule(data,recipe),out=Buffer.from(data);buildFrame(recipe,fps).copy(out,recipe.frameStart);
 if(!out.subarray(0,recipe.frameStart).equals(data.subarray(0,recipe.frameStart))||!out.subarray(recipe.frameEnd).equals(data.subarray(recipe.frameEnd)))throw Error('补丁修改范围超出函数');
 const after=identifyModule(out,recipe);if(after.fps!==fps)throw Error('补丁生成后检查失败');return{data:out,before,after};
}
