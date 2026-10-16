#!/usr/bin/env bash
# 用 gcc 语法检查 Keil(ARMCC) 代码路径（不定义 FW_SIMULATOR），并统计告警。
# 需要 /tmp/keilcheck/stm32f10x.h 这个最小 SPL 桩（脚本会自动生成）。
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."
STUB=${STUB:-/tmp/keilcheck}
mkdir -p "$STUB"
cp OELD.h "$STUB/OLED.h"
if [ ! -f "$STUB/stm32f10x.h" ]; then
  echo "缺少 $STUB/stm32f10x.h（最小标准外设库桩），请参考 docs/精准修复报告.md 生成" >&2
  exit 1
fi
fail=0
for f in *.c; do
  out=$(gcc -fsyntax-only -std=c99 -Wall -Wextra -I"$STUB" -I. "$f" 2>&1 || true)
  n=$(printf '%s' "$out" | grep -c "warning:\|error:" || true)
  printf '%-24s %s\n' "$f" "$n warning(s)/error(s)"
  [ "$n" = "0" ] || { printf '%s\n' "$out" | head -5; fail=1; }
done
exit $fail
