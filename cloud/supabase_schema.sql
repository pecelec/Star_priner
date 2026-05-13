-- Supabase print queue schema. Run this in Supabase SQL Editor.
create extension if not exists pgcrypto;

create table if not exists public.print_jobs (
  id uuid primary key default gen_random_uuid(),
  created_at timestamptz not null default now(),
  updated_at timestamptz not null default now(),
  claimed_at timestamptz,
  printed_at timestamptz,
  status text not null default 'pending' check (status in ('pending','printing','done','failed')),
  title text,
  payload jsonb not null,
  error text,
  attempts integer not null default 0,
  worker_name text
);

create index if not exists print_jobs_status_created_at_idx on public.print_jobs (status, created_at);
alter table public.print_jobs enable row level security;

drop policy if exists "anon can insert pending print jobs" on public.print_jobs;
create policy "anon can insert pending print jobs"
on public.print_jobs
for insert
to anon
with check (status = 'pending' and jsonb_typeof(payload) = 'object');

grant usage on schema public to anon;
grant insert on table public.print_jobs to anon;
grant usage on schema public to service_role;
grant select, insert, update, delete on table public.print_jobs to service_role;

create or replace view public.print_jobs_recent as
select id, created_at, updated_at, status, title, attempts, worker_name, error, printed_at
from public.print_jobs
order by created_at desc
limit 100;
