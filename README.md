# Star Printer Cloud Queue

Architecture:

```text
Public website -> Supabase print_jobs queue -> central Mac/PC print_worker.py -> Star printer
```

The printer is not exposed to the internet. The central computer polls Supabase and prints locally.

## 1. Supabase

Create a Supabase project, open SQL Editor, and run:

```sql
cloud/supabase_schema.sql
```

## 2. Frontend

```bash
cd frontend
npm install
cp .env.example .env
```

Edit `.env` with your Supabase URL and anon/publishable key.

Run locally:

```bash
npm run dev
```

Deploy the frontend folder to Vercel/Netlify/etc.

## 3. Central print worker

On the central computer:

```bash
cd agent
python -m venv .venv
```

macOS/Linux:

```bash
source .venv/bin/activate
```

Windows PowerShell:

```powershell
.venv\Scripts\Activate.ps1
```

Install and configure:

```bash
pip install -r requirements.txt
cp .env.example .env
```

Edit `.env`:

```text
SUPABASE_URL=https://your-project-ref.supabase.co
SUPABASE_SERVICE_ROLE_KEY=your-service-role-key
PRINTER_IP=192.168.178.121
PRINTER_PORT=9100
```

Run:

```bash
python print_worker.py
```

## Security

The frontend uses the anon/publishable key.
The worker uses the service-role/secret key.
Never put the service-role/secret key in the frontend, GitHub, Vercel public env vars, or browser code.
