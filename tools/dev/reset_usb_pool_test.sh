#!/usr/bin/env bash
set -Eeuo pipefail

YES=0
DRY_RUN=0
DEVS=()

while [[ $# -gt 0 ]]; do
  case "$1" in
    --yes|-y)
      YES=1
      shift
      ;;
    --dry-run|-n)
      DRY_RUN=1
      shift
      ;;
    --help|-h)
      cat <<'EOF'
Usage:
  sudo tools/dev/reset_usb_pool_test.sh [--dry-run] [--yes] /dev/sdX /dev/sdY

Example:
  sudo tools/dev/reset_usb_pool_test.sh --dry-run /dev/sda /dev/sdb
  sudo tools/dev/reset_usb_pool_test.sh /dev/sda /dev/sdb

This script only allows whole removable USB disks, not partitions and not NVMe/system disks.
EOF
      exit 0
      ;;
    *)
      DEVS+=("$1")
      shift
      ;;
  esac
done

die() {
  echo "ERROR: $*" >&2
  exit 1
}

run() {
  echo "+ $*"
  if [[ "$DRY_RUN" -eq 0 ]]; then
    "$@"
  fi
}

[[ "$(id -u)" -eq 0 ]] || die "run as root: sudo $0 ..."
[[ "${#DEVS[@]}" -ge 1 ]] || die "give at least one USB disk, for example /dev/sda /dev/sdb"

echo "=== Requested USB reset devices ==="

for dev in "${DEVS[@]}"; do
  [[ "$dev" =~ ^/dev/sd[a-z]+$ ]] || die "$dev is not a whole /dev/sdX disk. Refusing partitions like /dev/sdb1."
  [[ -b "$dev" ]] || die "$dev is not a block device"

  rm="$(lsblk -dn -o RM "$dev" | tr -d '[:space:]')"
  tran="$(lsblk -dn -o TRAN "$dev" | tr -d '[:space:]')"
  type="$(lsblk -dn -o TYPE "$dev" | tr -d '[:space:]')"
  model="$(lsblk -dn -o MODEL "$dev" | sed 's/[[:space:]]*$//')"
  size="$(lsblk -dn -o SIZE "$dev" | tr -d '[:space:]')"

  [[ "$type" == "disk" ]] || die "$dev is not TYPE=disk"
  [[ "$rm" == "1" ]] || die "$dev is not removable according to lsblk RM=$rm"
  [[ "$tran" == "usb" ]] || die "$dev is not USB according to lsblk TRAN=$tran"

  echo "OK: $dev  size=$size  model=$model  rm=$rm  tran=$tran"

  while IFS= read -r mp; do
    [[ -z "$mp" ]] && continue
    case "$mp" in
      /|/boot|/boot/efi|/home|/var|/usr|/opt|/srv/pqnas)
        die "$dev has protected mountpoint $mp. Refusing."
        ;;
    esac
  done < <(lsblk -nrpo MOUNTPOINTS "$dev" | sed '/^$/d')
done

echo
echo "=== Current layout ==="
lsblk -o NAME,PATH,RM,TRAN,SIZE,FSTYPE,TYPE,MOUNTPOINTS,MODEL "${DEVS[@]}"

echo
echo "=== Btrfs before reset ==="
btrfs filesystem show || true

echo
echo "WARNING: This will destroy all signatures, partitions and filesystems on:"
printf '  %s\n' "${DEVS[@]}"
echo

if [[ "$YES" -eq 0 && "$DRY_RUN" -eq 0 ]]; then
  echo "Type exactly this to continue:"
  echo "RESET USB TEST DISKS"
  read -r confirm
  [[ "$confirm" == "RESET USB TEST DISKS" ]] || die "confirmation did not match; aborting"
fi

for dev in "${DEVS[@]}"; do
  echo
  echo "=== Resetting $dev ==="

  echo "--- Unmounting mountpoints under $dev ---"
  mapfile -t nodes < <(lsblk -nrpo NAME "$dev" | sort -r)

  for node in "${nodes[@]}"; do
    while IFS= read -r mp; do
      [[ -z "$mp" ]] && continue
      run umount "$mp"
    done < <(findmnt -rn -S "$node" -o TARGET 2>/dev/null || true)
  done

  echo "--- Wiping child partitions first ---"
  for node in "${nodes[@]}"; do
    [[ "$node" == "$dev" ]] && continue
    if [[ -b "$node" ]]; then
      run wipefs -a "$node" || true
    fi
  done

  echo "--- Wiping whole disk signatures and partition tables ---"
  run wipefs -a "$dev"

  if command -v sgdisk >/dev/null 2>&1; then
    run sgdisk --zap-all "$dev" || true
  fi

  echo "--- Zeroing first and last 16 MiB ---"
  run dd if=/dev/zero of="$dev" bs=1M count=16 conv=fsync status=none

  sectors="$(blockdev --getsz "$dev")"
  if [[ "$sectors" -gt 32768 ]]; then
    seek="$((sectors - 32768))"
    run dd if=/dev/zero of="$dev" bs=512 seek="$seek" count=32768 conv=fsync status=none
  fi

  run partprobe "$dev" || true
done

run udevadm settle

if command -v btrfs >/dev/null 2>&1; then
  run btrfs device scan --forget || true
fi

echo
echo "=== After reset ==="
lsblk -o NAME,PATH,RM,TRAN,SIZE,FSTYPE,TYPE,MOUNTPOINTS,MODEL "${DEVS[@]}"

echo
echo "=== Remaining signatures check ==="
for dev in "${DEVS[@]}"; do
  echo "--- $dev ---"
  wipefs -n "$dev" || true
done

echo
if [[ "$DRY_RUN" -eq 1 ]]; then
  echo "Dry-run complete. Nothing was changed."
else
  echo "USB reset complete. Refresh Storage Manager before creating a new pool."
fi
