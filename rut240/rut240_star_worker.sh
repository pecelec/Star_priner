#!/bin/sh
#
# rut240_star_worker.sh
#
# Supabase web-queue worker for RUT240.
#
# This version respects /tmp/star_web_enabled.
# It also mirrors that state to the RUT240 DOUT1 hardware output:
#   enabled  -> output ON
#   disabled -> output OFF
#
# The OSC binary also controls the same flag/output:
#   /web/enable
#   /web/disable
#   /web/toggle

SUPABASE_URL="https://YOUR-PROJECT-REF.supabase.co"
SUPABASE_SERVICE_ROLE_KEY="YOUR_SUPABASE_SERVICE_ROLE_KEY"

PRINTER_IP="192.168.178.121"
PRINTER_PORT="9100"

POLL_SECONDS="2"
WORKER_NAME="rut240-star-web-worker"

WEB_ENABLE_FILE="/tmp/star_web_enabled"
DOUT_NAME="DOUT1"

JOB_JSON="/tmp/star_job.json"
JOB_B64="/tmp/star_job.b64"
JOB_BIN="/tmp/star_job.bin"
RESP_JSON="/tmp/star_resp.json"

API_BASE="$SUPABASE_URL/rest/v1/print_jobs"

log() {
  echo "$(date '+%Y-%m-%d %H:%M:%S') $*"
}

set_dout() {
  VALUE="$1"

  if command -v ubus >/dev/null 2>&1; then
    if ubus call ioman.gpio.dout1 update "{\"value\":\"$VALUE\"}" >/dev/null 2>&1; then
      log "DOUT1 set to $VALUE via ubus"
      return 0
    fi
  fi

  if command -v gpio.sh >/dev/null 2>&1; then
    CURRENT="$(gpio.sh get "$DOUT_NAME" 2>/dev/null | tr -dc '01' | head -c 1)"

    if [ -z "$CURRENT" ]; then
      log "Could not read $DOUT_NAME via gpio.sh"
      return 1
    fi

    if [ "$CURRENT" = "$VALUE" ]; then
      return 0
    fi

    if gpio.sh invert "$DOUT_NAME" >/dev/null 2>&1; then
      log "$DOUT_NAME set to $VALUE via gpio.sh invert"
      return 0
    fi
  fi

  log "Could not set hardware output"
  return 1
}

check_deps() {
  for cmd in curl nc sed grep cut tr; do
    if ! command -v "$cmd" >/dev/null 2>&1; then
      log "Missing command: $cmd"
      exit 1
    fi
  done

  if ! command -v base64 >/dev/null 2>&1 && ! command -v openssl >/dev/null 2>&1; then
    log "Missing base64 decoder: install openssl-util or base64"
    exit 1
  fi
}

web_enabled() {
  # Default enabled if the flag file does not exist yet.
  if [ ! -f "$WEB_ENABLE_FILE" ]; then
    echo "1" > "$WEB_ENABLE_FILE"
    set_dout 1
    return 0
  fi

  if grep -q '^0' "$WEB_ENABLE_FILE"; then
    set_dout 0
    return 1
  fi

  set_dout 1
  return 0
}

json_escape_basic() {
  echo "$1" | tr '\r\n' '  ' | sed 's/\\/\\\\/g; s/"/\\"/g'
}

get_next_job() {
  curl -sS \
    -H "apikey: $SUPABASE_SERVICE_ROLE_KEY" \
    -H "Authorization: Bearer $SUPABASE_SERVICE_ROLE_KEY" \
    "$API_BASE?status=eq.pending&select=id,payload,attempts&order=created_at.asc&limit=1" \
    > "$JOB_JSON"

  if grep -q '"id"' "$JOB_JSON"; then
    return 0
  fi

  return 1
}

extract_job_id() {
  sed -n 's/.*"id"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$JOB_JSON" | head -n 1
}

extract_star_b64() {
  sed -n 's/.*"star_b64"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$JOB_JSON" | head -n 1
}

patch_job() {
  JOB_ID="$1"
  PATCH_BODY="$2"

  curl -sS -X PATCH \
    -H "apikey: $SUPABASE_SERVICE_ROLE_KEY" \
    -H "Authorization: Bearer $SUPABASE_SERVICE_ROLE_KEY" \
    -H "Content-Type: application/json" \
    -H "Prefer: return=minimal" \
    "$API_BASE?id=eq.$JOB_ID" \
    --data "$PATCH_BODY" \
    > "$RESP_JSON"
}

claim_job() {
  JOB_ID="$1"
  NOW="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  patch_job "$JOB_ID" "{\"status\":\"printing\",\"claimed_at\":\"$NOW\",\"updated_at\":\"$NOW\",\"worker_name\":\"$WORKER_NAME\",\"error\":null}"
}

mark_done() {
  JOB_ID="$1"
  NOW="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  patch_job "$JOB_ID" "{\"status\":\"done\",\"printed_at\":\"$NOW\",\"updated_at\":\"$NOW\",\"error\":null}"
}

mark_failed() {
  JOB_ID="$1"
  ERROR_TEXT="$(json_escape_basic "$2")"
  NOW="$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  patch_job "$JOB_ID" "{\"status\":\"failed\",\"updated_at\":\"$NOW\",\"error\":\"$ERROR_TEXT\"}"
}

decode_b64_to_job_bin() {
  if command -v base64 >/dev/null 2>&1; then
    base64 -d "$JOB_B64" > "$JOB_BIN" 2>/tmp/star_base64_err.txt && return 0
    return 1
  fi

  if command -v openssl >/dev/null 2>&1; then
    openssl enc -base64 -d -A -in "$JOB_B64" -out "$JOB_BIN" 2>/tmp/star_base64_err.txt && return 0
    return 1
  fi

  return 1
}

print_b64_job() {
  B64="$1"
  echo "$B64" > "$JOB_B64"

  if ! decode_b64_to_job_bin; then
    return 2
  fi

  if nc -w 5 "$PRINTER_IP" "$PRINTER_PORT" < "$JOB_BIN"; then
    sleep 1
    return 0
  fi

  return 3
}

process_once() {
  if ! web_enabled; then
    log "Web/Supabase printing disabled by OSC control"
    return 1
  fi

  if ! get_next_job; then
    log "No pending Supabase job"
    return 1
  fi

  # Re-check just before claiming, in case OSC disabled while the request was in-flight.
  if ! web_enabled; then
    log "Web/Supabase disabled before claiming job"
    return 1
  fi

  JOB_ID="$(extract_job_id)"
  STAR_B64="$(extract_star_b64)"

  if [ -z "$JOB_ID" ]; then
    log "Could not extract Supabase job id"
    return 1
  fi

  if [ -z "$STAR_B64" ]; then
    log "Job $JOB_ID has no payload.star_b64"
    mark_failed "$JOB_ID" "No payload.star_b64 in job."
    return 0
  fi

  log "Claiming Supabase job $JOB_ID"
  claim_job "$JOB_ID"

  if print_b64_job "$STAR_B64"; then
    log "Printed Supabase job $JOB_ID"
    mark_done "$JOB_ID"
  else
    CODE="$?"
    log "Failed printing Supabase job $JOB_ID code $CODE"
    mark_failed "$JOB_ID" "RUT240 print failed with code $CODE"
  fi

  return 0
}

loop() {
  check_deps

  # Default enabled on boot.
  if [ ! -f "$WEB_ENABLE_FILE" ]; then
    echo "1" > "$WEB_ENABLE_FILE"
  fi
  web_enabled >/dev/null

  log "RUT240 Supabase web worker started"
  log "Web enable file: $WEB_ENABLE_FILE"
  log "Printer: $PRINTER_IP:$PRINTER_PORT"

  while true; do
    process_once
    sleep "$POLL_SECONDS"
  done
}

case "$1" in
  once)
    check_deps
    process_once
    ;;
  loop|"")
    loop
    ;;
  set-output-on)
    set_dout 1
    ;;
  set-output-off)
    set_dout 0
    ;;
  *)
    echo "Usage: sh $0 [once|loop|set-output-on|set-output-off]"
    exit 1
    ;;
esac
