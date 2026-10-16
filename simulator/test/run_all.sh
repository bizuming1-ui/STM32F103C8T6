#!/usr/bin/env bash
# 一键验证：编译 wasm -> 73 项行为测试 -> 渲染 PNG -> 页面代码冒烟测试
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

echo "== 1/4 编译固件 (wasm) =="
bash simulator/build.sh

echo
echo "== 2/4 行为回归测试 =="
node simulator/test/verify.mjs

echo
echo "== 3/4 渲染界面截图 (simulator/test/out/*.png) =="
node simulator/test/render_menu.mjs >/dev/null && echo "OK"

echo
echo "== 4/4 页面代码冒烟测试 (需要先启动: python3 -m http.server 8000 --directory simulator) =="
if curl -sf -o /dev/null http://127.0.0.1:8000/index.html; then
  node simulator/test/ui_smoke.mjs
else
  echo "跳过：本地 8000 端口没有静态服务器"
fi
