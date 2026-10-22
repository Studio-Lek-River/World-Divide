"""Native Unreal MCP transport only; no editor Python or console bypass.
Reads a list of already-discovered native calls and records their full receipts.
"""
import json,sys,urllib.request
from pathlib import Path
URL='http://127.0.0.1:8000/mcp'
headers={'Content-Type':'application/json','Accept':'application/json, text/event-stream'}
def rpc(method,params):
    req=urllib.request.Request(URL,json.dumps({'jsonrpc':'2.0','id':2,'method':method,'params':params}).encode(),headers)
    with urllib.request.urlopen(req,timeout=1800) as response:
        if response.headers.get('Mcp-Session-Id'):headers['Mcp-Session-Id']=response.headers['Mcp-Session-Id']
        data=json.load(response)
    if 'error' in data:raise RuntimeError(data)
    return data['result']
def main():
    rpc('initialize',{'protocolVersion':'2025-11-25','capabilities':{},'clientInfo':{'name':'hq379','version':'1'}})
    plan=json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
    for item in plan:
        result=rpc('tools/call',{'name':'call_tool','arguments':item['call']})
        Path(item['receipt']).write_text(json.dumps(result,indent=2),encoding='utf8')
        if result.get('isError'):raise RuntimeError(result)
        print(item['call']['tool_name'],json.dumps(result)[:2400],flush=True)
if __name__=='__main__':main()
