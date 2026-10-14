// Bounded raw LZ4 block decoder; never interprets or executes contained bytecode.
export function decodeLz4(input,expected){
 if(!Number.isSafeInteger(expected)||expected<1||expected>16*1024*1024)throw Error('Invalid output bound');
 const out=Buffer.alloc(expected);let p=0,q=0;
 function length(n){if(n===15){let x;do{if(p>=input.length)throw Error('LZ4 missing length');x=input[p++];n+=x;}while(x===255);}return n;}
 while(p<input.length){const token=input[p++],literal=length(token>>>4);if(p+literal>input.length||q+literal>expected)throw Error('LZ4 literal out of bounds');input.copy(out,q,p,p+literal);p+=literal;q+=literal;if(p===input.length)break;if(p+2>input.length)throw Error('LZ4 offset missing');const off=input.readUInt16LE(p);p+=2;if(!off||off>q)throw Error('LZ4 offset invalid');const match=length(token&15)+4;if(q+match>expected)throw Error('LZ4 match out of bounds');for(let j=0;j<match;j++)out[q+j]=out[q+j-off];q+=match;}
 if(q!==expected)throw Error('LZ4 decoded length mismatch');return out;
}
