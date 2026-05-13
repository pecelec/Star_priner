"""
Central print worker for the Supabase print queue.
Run on the Mac/PC that can reach the Star printer.
"""
from __future__ import annotations
import os, time, traceback
from datetime import datetime, timezone
from typing import Any
from dotenv import load_dotenv
from supabase import create_client, Client
from star_dot import StarPrinter, BufferedTcpTransport

load_dotenv()
SUPABASE_URL=os.environ['SUPABASE_URL']
SUPABASE_SERVICE_ROLE_KEY=os.environ['SUPABASE_SERVICE_ROLE_KEY']
PRINTER_IP=os.getenv('PRINTER_IP','192.168.178.121')
PRINTER_PORT=int(os.getenv('PRINTER_PORT','9100'))
POLL_INTERVAL_SECONDS=float(os.getenv('POLL_INTERVAL_SECONDS','1.0'))
TCP_CLOSE_DELAY_SECONDS=float(os.getenv('TCP_CLOSE_DELAY_SECONDS','1.0'))
WORKER_NAME=os.getenv('WORKER_NAME','central-printer-worker')
supabase:Client=create_client(SUPABASE_URL,SUPABASE_SERVICE_ROLE_KEY)

def utc_now_iso()->str: return datetime.now(timezone.utc).isoformat()

def fetch_next_pending_job()->dict[str,Any]|None:
    r=(supabase.table('print_jobs').select('*').eq('status','pending').order('created_at').limit(1).execute())
    return r.data[0] if r.data else None

def update_job(job_id:str, values:dict[str,Any])->None:
    values['updated_at']=utc_now_iso()
    supabase.table('print_jobs').update(values).eq('id',job_id).execute()

def print_payload(payload:dict[str,Any])->None:
    title=payload.get('title')
    lines=payload.get('lines',[])
    two_colour=bool(payload.get('two_colour',True))
    feed_lines=int(payload.get('feed_lines',3))
    cut=bool(payload.get('cut',True))
    partial_cut=bool(payload.get('partial_cut',True))
    with BufferedTcpTransport(PRINTER_IP,PRINTER_PORT,close_delay=TCP_CLOSE_DELAY_SECONDS) as transport:
        p=StarPrinter(transport)
        p.initialize()
        if two_colour: p.two_colour_mode(True)
        if title:
            p.align('center').bold(True).double_width(True).black().write_line(str(title))
            p.double_width(False).bold(False).align('left').write_line()
        for line in lines:
            text=str(line.get('text',''))
            p.align(str(line.get('align','left')))
            p.bold(bool(line.get('bold',False)))
            p.double_width(bool(line.get('double_width',False)))
            p.double_height(bool(line.get('double_height',False)))
            p.underline(bool(line.get('underline',False)))
            p.red() if str(line.get('colour','black')).lower()=='red' else p.black()
            p.write_line(text)
            p.bold(False).double_width(False).double_height(False).underline(False).black()
        p.align('left').black()
        if feed_lines>0: p.feed_lines(feed_lines)
        if cut: p.cut(feed=True, partial=partial_cut)

def process_one_job()->bool:
    job=fetch_next_pending_job()
    if job is None: return False
    job_id=job['id']; attempts=int(job.get('attempts') or 0)
    print(f'Claiming job {job_id}')
    update_job(job_id, {'status':'printing','claimed_at':utc_now_iso(),'attempts':attempts+1,'worker_name':WORKER_NAME,'error':None})
    try:
        payload=job['payload']
        if not isinstance(payload,dict): raise ValueError('Job payload must be a JSON object')
        print_payload(payload)
        update_job(job_id, {'status':'done','printed_at':utc_now_iso(),'error':None})
        print(f'Done job {job_id}')
        return True
    except Exception as exc:
        err=''.join(traceback.format_exception_only(type(exc),exc)).strip()
        print(f'FAILED job {job_id}: {err}')
        update_job(job_id, {'status':'failed','error':err})
        return True

def main()->None:
    print('Star print worker started')
    print(f'Worker: {WORKER_NAME}')
    print(f'Printer: {PRINTER_IP}:{PRINTER_PORT}')
    while True:
        if not process_one_job(): time.sleep(POLL_INTERVAL_SECONDS)
if __name__=='__main__': main()
