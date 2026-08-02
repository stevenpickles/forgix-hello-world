#!/usr/bin/env bash
# Long-duration USB CDC soak harness for the Forgix lockup investigation.
#
# Opens the serial port exactly once and holds it open for the whole run. It
# never toggles control lines and never reopens after a failure, because a single
# controlled reopen is itself one of the experiments -- doing it by accident
# destroys the observation that separates a wedged device from a recoverable
# host session.
#
# Serial from Git Bash uses the MSYS device namespace: COM<n> is /dev/ttyS<n-1>,
# configured with stty and held on file descriptor 3 for the duration.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

port="COM3"
baud=115200
duration_minutes=0
gap_warn_seconds=5
gap_fail_seconds=30
send_interval_seconds=0
ping_command="status"
log_dir="build/soak-logs"
validate_only=0
self_test=0

usage() {
  cat <<'USAGE'
Usage: ./scripts/soak_serial.sh [options]

  --port COM3               Serial port (COM<n>, mapped to /dev/ttyS<n-1>)
  --baud 115200             Baud rate
  --duration-minutes 0      0 runs until failure or Ctrl-C
  --gap-warn 5              Warn after this many seconds with no line
  --gap-fail 30             Treat this many seconds with no line as a failure
  --send-interval 0         0 stays passive; otherwise send a numbered ping
  --ping status             Command sent as the ping
  --log-dir build/soak-logs Where the timestamped log is written
  --validate-only           Check arguments and exit without opening the port
  --self-test               Run the line/gap logic against synthetic input
USAGE
}

while (( $# )); do
  case "$1" in
    --port) port="${2:?}"; shift 2 ;;
    --baud) baud="${2:?}"; shift 2 ;;
    --duration-minutes) duration_minutes="${2:?}"; shift 2 ;;
    --gap-warn) gap_warn_seconds="${2:?}"; shift 2 ;;
    --gap-fail) gap_fail_seconds="${2:?}"; shift 2 ;;
    --send-interval) send_interval_seconds="${2:?}"; shift 2 ;;
    --ping) ping_command="${2:?}"; shift 2 ;;
    --log-dir) log_dir="${2:?}"; shift 2 ;;
    --validate-only) validate_only=1; shift ;;
    --self-test) self_test=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) printf 'Unknown argument: %s\n\n' "$1" >&2; usage >&2; exit 2 ;;
  esac
done

is_number() { [[ "$1" =~ ^[0-9]+$ ]]; }

for name in baud duration_minutes gap_warn_seconds gap_fail_seconds send_interval_seconds; do
  is_number "${!name}" || { printf '%s must be a whole number: %s\n' "$name" "${!name}" >&2; exit 2; }
done
(( baud > 0 )) || { printf 'baud must be positive\n' >&2; exit 2; }
(( gap_warn_seconds > 0 )) || { printf 'gap-warn must be positive\n' >&2; exit 2; }
(( gap_fail_seconds > gap_warn_seconds )) || {
  printf 'gap-fail (%s) must exceed gap-warn (%s)\n' "$gap_fail_seconds" "$gap_warn_seconds" >&2
  exit 2
}
if (( send_interval_seconds > 0 )) && [[ -z "${ping_command// }" ]]; then
  printf 'ping is required when send-interval is greater than zero\n' >&2
  exit 2
fi
[[ "$port" =~ ^COM[0-9]+$ ]] || {
  printf 'port must name a Windows serial port, for example COM3: %s\n' "$port" >&2
  exit 2
}

# COM1 is /dev/ttyS0, so subtract one.
port_number="${port#COM}"
device="/dev/ttyS$(( port_number - 1 ))"

[[ "$log_dir" = /* ]] || log_dir="$repo_root/$log_dir"
mkdir -p "$log_dir"
log_file="$log_dir/soak-$port-$(date +%Y%m%d-%H%M%S).log"

stamp() { date +"%Y-%m-%d %H:%M:%S"; }

printf 'Soak configuration\n'
printf '    %-22s%s\n' \
  port "$port ($device)" \
  baud "$baud" \
  duration "$( (( duration_minutes == 0 )) && echo 'until failure or Ctrl-C' || echo "$duration_minutes min" )" \
  gap-warn "${gap_warn_seconds}s" \
  gap-fail "${gap_fail_seconds}s" \
  send-interval "$( (( send_interval_seconds == 0 )) && echo 'passive (no pings)' || echo "${send_interval_seconds}s" )" \
  ping "$ping_command" \
  log "$log_file"

if (( validate_only )); then
  printf '\nArguments and log directory are valid. No port was opened.\n'
  exit 0
fi

received_lines=0
sequence=0
last_line="<none>"
max_gap=0
saw_boot_report=0
failure_reason=""
start_epoch=$(date +%s)
last_receive=$start_epoch
last_send=$start_epoch
warned_gap=0

log() { printf '%s  %s\n' "$(stamp)" "$1" >>"$log_file"; }

for setting in "port=$port" "device=$device" "baud=$baud" "gap_fail=$gap_fail_seconds"; do
  log "# $setting"
done

handle_line() {
  local line="${1%$'\r'}"
  [[ -n "$line" ]] || return 0
  local now gap
  now=$(date +%s)
  gap=$(( now - last_receive ))
  (( gap > max_gap )) && max_gap=$gap
  last_receive=$now
  received_lines=$(( received_lines + 1 ))
  last_line="$line"
  warned_gap=0
  log "RX  $line"
  # A boot report on the still-open port means the board reset under us; the
  # retained marker names where the previous foreground stopped.
  if [[ "$line" == diag:\ boot=* ]]; then
    saw_boot_report=1
    log "!!  device reported a boot after the session started: $line"
    printf '    RESET DETECTED: %s\n' "$line"
  fi
}

if (( self_test )); then
  # Exercises the parsing and gap bookkeeping without a device, so the logic can
  # be verified on a machine with no board attached.
  printf '\nSelf-test\n'
  handle_line $'id=B5 status=01\r'
  handle_line 'diag: boot=watchdog marker=3 loop=612 usb=0 health=00000000'
  handle_line ''
  last_receive=$(( $(date +%s) - 9 ))
  idle=$(( $(date +%s) - last_receive ))
  printf '    lines parsed        %s (expected 2)\n' "$received_lines"
  printf '    boot report seen    %s (expected 1)\n' "$saw_boot_report"
  printf '    blank line ignored  %s\n' "$( (( received_lines == 2 )) && echo yes || echo NO )"
  printf '    idle accounting     %ss -> %s\n' "$idle" \
    "$( (( idle >= gap_fail_seconds )) && echo 'would fail' || echo 'would warn' )"
  rm -f "$log_file"
  (( received_lines == 2 && saw_boot_report == 1 )) || exit 1
  printf '    self-test passed\n'
  exit 0
fi

[[ -e "$device" || -c "$device" ]] || true   # MSYS nodes are virtual; open decides

printf '\nOpening %s at %s baud (the only open of the run)\n' "$port" "$baud"
if ! stty -F "$device" "$baud" cs8 -cstopb -parenb -echo -icanon -ixon min 0 time 10 2>/dev/null; then
  printf 'Could not configure %s (%s).\n' "$port" "$device" >&2
  printf 'Check the board is connected and no other program holds the port.\n' >&2
  exit 1
fi
exec 3<>"$device" || { printf 'Could not open %s\n' "$device" >&2; exit 1; }
log "opened $device"
printf '    Logging to %s\n' "$log_file"
printf '    Ctrl-C to stop. The port is never reopened automatically.\n'

finish() {
  local elapsed=$(( $(date +%s) - start_epoch ))
  exec 3<&- 2>/dev/null || true
  exec 3>&- 2>/dev/null || true

  printf '\nSoak summary\n'
  local summary=(
    "elapsed:$(( elapsed / 3600 ))h $(( (elapsed % 3600) / 60 ))m $(( elapsed % 60 ))s"
    "received lines:$received_lines"
    "last line:$last_line"
    "last sequence:$sequence"
    "largest gap:${max_gap}s"
    "boot report seen:$saw_boot_report"
    "result:${failure_reason:-completed}"
  )
  local entry
  for entry in "${summary[@]}"; do
    printf '    %-20s%s\n' "${entry%%:*}" "${entry#*:}"
    log "# ${entry%%:*}: ${entry#*:}"
  done

  if [[ -n "$failure_reason" ]]; then
    cat >&2 <<CHECKLIST

Capture this evidence BEFORE touching the board:
  1. Record the exact time, the LED color, and the last sequence number above.
     green=healthy  red=endpoint wedged, loop alive  magenta=suspended/SOF frozen
     blue=DTR low   white x3=FPGA reconfigured and recovered
  2. Check whether Device Manager and USBView still list the device and its CDC interface.
  3. Save the Windows USB ETW trace and device events.
  4. Close this window, then attempt exactly one clean reopen of $port.
  5. Only then try 'picotool reboot -f -u'; it deliberately changes device state.
  6. Power-cycle last, after every observation above is recorded.

Then read the result against the mode-1 decision tree in
docs/lockup-investigation-plan.md and append a row to the results log in
docs/usb-cdc-debugging.md.
CHECKLIST
  fi
  printf '\nLog: %s\n' "$log_file"
}
trap finish EXIT

deadline=0
(( duration_minutes > 0 )) && deadline=$(( start_epoch + duration_minutes * 60 ))

while (( deadline == 0 )) || (( $(date +%s) < deadline )); do
  # min 0 time 10 makes this return after at most one second with no data.
  if IFS= read -r -u 3 line; then
    handle_line "$line"
  fi

  now=$(date +%s)
  if (( send_interval_seconds > 0 )) && (( now - last_send >= send_interval_seconds )); then
    sequence=$(( sequence + 1 ))
    if printf '%s\r\n' "$ping_command" >&3 2>/dev/null; then
      log "TX  seq=$sequence $ping_command"
    else
      failure_reason="write failed at seq=$sequence"
      break
    fi
    last_send=$now
  fi

  idle=$(( $(date +%s) - last_receive ))
  if (( idle >= gap_fail_seconds )); then
    failure_reason="no data for ${idle}s (limit ${gap_fail_seconds}s)"
    break
  fi
  if (( idle >= gap_warn_seconds )) && (( warned_gap == 0 )); then
    warned_gap=1
    log "WARN gap ${idle}s with no received line"
    printf '    gap %ss\n' "$idle"
  fi
done

[[ -n "$failure_reason" ]] && exit 1
exit 0
