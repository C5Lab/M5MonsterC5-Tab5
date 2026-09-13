// One native rotation restart only. No storage, files or general guide resume.
const prefix='#emu-story-once=';
const valid=value=>value?.id==='s19-rotation'&&value.payload&&typeof value.payload==='object'&&!Array.isArray(value.payload);
export function encodeRestartHandoff(id,payload){
 const value={id,payload};
 return valid(value)?prefix+encodeURIComponent(JSON.stringify(value)):'';
}
export function consumeRestartHandoff(location,history){
 const url=new URL(location.href);
 if(!url.hash.startsWith(prefix))return null;
 const encoded=url.hash.slice(prefix.length);
 url.hash='';history.replaceState(history.state,'',url.href);
 if(encoded.length>4096)return null;
 try{const value=JSON.parse(decodeURIComponent(encoded));return valid(value)?value:null;}catch{return null;}
}
