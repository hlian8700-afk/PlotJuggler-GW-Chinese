#!/bin/sh
set -eu

# Keep signing keys and the SQLite database on the same mounted volume.
state_dir=${RAILWAY_VOLUME_MOUNT_PATH:?Attach a persistent volume before starting BladeScope}
if [ "$state_dir" != /data ] || ! [ -d "$state_dir" ] ||
   ! awk -v path="$state_dir" '$5 == path { found=1 } END { exit !found }' /proc/self/mountinfo; then
    echo "Attach the Railway volume at /data." >&2
    exit 1
fi

export BLADESCOPE_BIND=0.0.0.0
export BLADESCOPE_MAIL_TRANSPORT=resend
: "${PORT:?Set the Railway service port to 8080.}"
: "${BLADESCOPE_RESEND_API_KEY:?Set the Resend sending key in Railway variables.}"
export BLADESCOPE_RESEND_FROM="${BLADESCOPE_RESEND_FROM:-BladeScope <onboarding@resend.dev>}"

python /app/server.py --data "$state_dir" init
python /app/server.py --data "$state_dir" bootstrap
exec python /app/server.py --data "$state_dir" serve
