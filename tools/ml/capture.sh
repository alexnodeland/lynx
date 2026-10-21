#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Collect training images with lynx itself: it pans and tilts across the room and takes a snapshot at
# each stop, so the dataset has the angles, light and background the model will really see.
#   LYNX_HOST=lynx-42e0.local LYNX_TOKEN=... tools/ml/capture.sh <label> [count] [settle-seconds]
# Images land in datasets/<label>/. Upload them to Edge Impulse (see docs/custom-models.md).
set -euo pipefail

label=${1:?usage: capture.sh <label> [count] [settle-seconds]}
count=${2:-30}
settle=${3:-1.2}
host=${LYNX_HOST:?set LYNX_HOST, e.g. lynx-42e0.local}
token=${LYNX_TOKEN:?set LYNX_TOKEN (the device token)}
out="datasets/$label"
mkdir -p "$out"
auth=(-H "Authorization: Bearer $token")

curl -fsS "${auth[@]}" "http://$host/api/status" > /dev/null || { echo "can't reach lynx at $host (or the token is wrong)" >&2; exit 1; }

# a serpentine over pan -60..60 and tilt 30..55: neighbouring shots differ, the whole room gets covered
cols=$(( count < 6 ? count : 6 ))
rows=$(( (count + cols - 1) / cols ))
n=0
for ((r = 0; r < rows && n < count; r++)); do
  tilt=45
  if ((rows > 1)); then tilt=$((30 + r * 25 / (rows - 1))); fi
  for ((c = 0; c < cols && n < count; c++)); do
    cc=$(( r % 2 ? cols - 1 - c : c ))
    pan=0
    if ((cols > 1)); then pan=$((-60 + cc * 120 / (cols - 1))); fi
    curl -fsS -X POST "${auth[@]}" "http://$host/api/position" -d "{\"pan\":$pan,\"tilt\":$tilt}" > /dev/null
    sleep "$settle"
    f="$out/${label}_$(date +%Y%m%d-%H%M%S)_$(printf %03d $n).jpg"
    curl -fsS "${auth[@]}" -o "$f" "http://$host/snapshot.jpg"
    printf '\r%s: %d/%d (pan %4d, tilt %3d)' "$label" $((n + 1)) "$count" "$pan" "$tilt"
    n=$((n + 1))
  done
done
curl -fsS -X POST "${auth[@]}" "http://$host/api/center" > /dev/null
echo
echo "saved $n images to $out/"
