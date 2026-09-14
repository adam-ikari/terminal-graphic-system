#!/usr/bin/env bash
#
# Run the real TGS compositor (SDL backend) on a headless machine via Xvfb,
# then capture a screenshot of its window.
#
# Usage: scripts/run-xvfb.sh [output.png]   (default: xvfb-shot.png)
#
set -euo pipefail

OUT_ARG="${1:-xvfb-shot.png}"

# ── Paths ────────────────────────────────────────────────────────────────────
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"
COMPOSITOR="$BUILD/tgs-compositor"
APP="$BUILD/simple_form"

# Resolve the output path now, before we cd anywhere.
case "$OUT_ARG" in
    /*) OUT="$OUT_ARG" ;;
    *)  OUT="$PWD/$OUT_ARG" ;;
esac

SCREEN_W=1024
SCREEN_H=768
SCREEN_D=24

# ── Tool checks ──────────────────────────────────────────────────────────────
die() { echo "error: $*" >&2; exit 1; }

command -v Xvfb >/dev/null 2>&1 || die "Xvfb not found (install xvfb)"
command -v xdpyinfo >/dev/null 2>&1 || die "xdpyinfo not found (install x11-utils)"
command -v xdotool >/dev/null 2>&1 || die "xdotool not found (install xdotool)"

SHOT_TOOL=""
for t in import scrot; do
    if command -v "$t" >/dev/null 2>&1; then SHOT_TOOL="$t"; break; fi
done
[ -n "$SHOT_TOOL" ] || die "no screenshot tool found (install imagemagick or scrot)"

[ -x "$COMPOSITOR" ] || die "$COMPOSITOR not found — run 'make build' (cmake -DTGS_USE_SDL=ON)"
[ -x "$APP" ] || die "$APP not found — run 'make build'"

# ── Xvfb lifecycle ───────────────────────────────────────────────────────────
XVFB_PID=""
COMP_PID=""

cleanup() {
    local rc=$?
    if [ -n "$COMP_PID" ] && kill -0 "$COMP_PID" 2>/dev/null; then
        kill -TERM "$COMP_PID" 2>/dev/null || true
        wait "$COMP_PID" 2>/dev/null || true
    fi
    if [ -n "$XVFB_PID" ] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill -TERM "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
    fi
    return $rc
}
trap cleanup EXIT

# Claim a free display. Probing for socket files is unreliable — a running server
# may hold only the abstract socket, or leave a stale file behind — so we try to
# start Xvfb and move on if it loses the race.
# Sets DISPLAY_NUM and XVFB_PID on success.
start_xvfb() {
    local n
    for n in $(seq 99 130); do
        Xvfb ":$n" -screen 0 "${SCREEN_W}x${SCREEN_H}x${SCREEN_D}" -nolisten tcp >/dev/null 2>&1 &
        XVFB_PID=$!
        sleep 0.3
        # A busy display makes Xvfb exit within milliseconds.
        if ! kill -0 "$XVFB_PID" 2>/dev/null; then
            wait "$XVFB_PID" 2>/dev/null || true
            XVFB_PID=""
            continue
        fi
        for _ in $(seq 1 50); do
            if DISPLAY=":$n" xdpyinfo >/dev/null 2>&1; then
                sleep 0.2
                if kill -0 "$XVFB_PID" 2>/dev/null; then
                    DISPLAY_NUM=":$n"
                    return 0
                fi
                break
            fi
            kill -0 "$XVFB_PID" 2>/dev/null || break
            sleep 0.1
        done
        kill -TERM "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
        XVFB_PID=""
    done
    return 1
}

echo "==> starting Xvfb (${SCREEN_W}x${SCREEN_H}x${SCREEN_D})"
start_xvfb || die "could not start Xvfb on any display in :99-:130"
echo "==> Xvfb on $DISPLAY_NUM (pid $XVFB_PID)"

export DISPLAY="$DISPLAY_NUM"

# ── Launch the real compositor ───────────────────────────────────────────────
echo "==> launching compositor: tgs-compositor simple_form"
# Run from the build dir so the sibling 'ime_app' helper resolves.
( cd "$BUILD" && exec "./tgs-compositor" "./simple_form" ) >/tmp/tgs-xvfb-compositor.log 2>&1 &
COMP_PID=$!

# Wait for its window ("TGS") to appear and render.
WID=""
for _ in $(seq 1 60); do
    WID="$(xdotool search --name '^TGS$' 2>/dev/null | head -n 1 || true)"
    [ -n "$WID" ] && break
    kill -0 "$COMP_PID" 2>/dev/null || {
        echo "--- compositor log ---" >&2
        cat /tmp/tgs-xvfb-compositor.log >&2
        die "compositor exited before creating a window"
    }
    sleep 0.1
done

if [ -z "$WID" ]; then
    echo "--- compositor log ---" >&2
    cat /tmp/tgs-xvfb-compositor.log >&2
    die "no 'TGS' window appeared"
fi

echo "==> window id $WID — letting it render"
sleep 3   # give LVGL + the SDL presenter time to draw a full frame

# ── Capture ──────────────────────────────────────────────────────────────────
rm -f "$OUT"
echo "==> capturing screenshot ($SHOT_TOOL)"
captured=0

if [ "$SHOT_TOOL" = "import" ]; then
    if import -window "$WID" "$OUT" 2>/dev/null; then
        captured=1
    elif import -window root "$OUT" 2>/dev/null; then
        captured=1
    fi
elif scrot "$OUT" 2>/dev/null; then
    captured=1
fi

[ "$captured" -eq 1 ] && [ -s "$OUT" ] || die "screenshot capture failed"

# ── Stop compositor cleanly, then Xvfb ───────────────────────────────────────
kill -TERM "$COMP_PID" 2>/dev/null || true
wait "$COMP_PID" 2>/dev/null || true
COMP_PID=""

# ── Report ───────────────────────────────────────────────────────────────────
SIZE="$(stat -c %s "$OUT" 2>/dev/null || stat -f %z "$OUT")"

# Blank == every pixel identical. A file-size floor is only a crude proxy (a
# flat-colour form compresses to ~2 KB), so prefer real pixel variance.
VAR=""
if command -v identify >/dev/null 2>&1; then
    VAR="$(identify -format '%[fx:(standard_deviation>0.001)?1:0]' "$OUT" 2>/dev/null || true)"
fi

if [ -n "$VAR" ]; then
    [ "$VAR" = "1" ] || die "screenshot is blank (uniform pixels, ${SIZE} bytes): $OUT"
elif [ "$SIZE" -le 3072 ]; then
    die "screenshot looks blank (${SIZE} bytes <= 3072): $OUT"
fi

echo "screenshot: $OUT"
echo "size:       ${SIZE} bytes"
identify -format 'image:      %wx%h, %[colors] colours, stddev %[fx:standard_deviation]\n' "$OUT" 2>/dev/null || true
