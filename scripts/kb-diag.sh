#!/usr/bin/env bash
# Dolphin 键盘诊断脚本
#
# 用途：一条命令抓齐排查「USB 失声」所需的全部信息。
#
# 背景：WSL2 里没有 USB 子系统（/sys/bus/usb、/dev/hidraw* 都不存在），
# 所以必须绕道 Windows 的 PowerShell 查询 PnP 设备状态。本脚本封装了这些查询。
#
# 用法：
#   scripts/kb-diag.sh              查看当前状态并与基线对比
#   scripts/kb-diag.sh --baseline   把当前枚举时刻记为新基线（刷机后 / 重启后必做）
#   scripts/kb-diag.sh --log        额外输出最近的 USB/HID 与电源事件日志
#   scripts/kb-diag.sh --help
#
# 关键判读（详见 plan.md「下次失声时怎么做」）：
#   枚举时刻变了而你没拔线  => 期间发生过一次失声，并被自动恢复救回
#   恢复耗时约 2 秒         => 第 2 级 restart_usb_driver（E15 硬件锁死的预期表现）
#   恢复耗时约 10 秒        => 第 3 级 mcu_reset
#   完全没恢复             => 见 plan.md 情况 B，拔线前先按几个键等 6 秒

set -uo pipefail

VID="${KB_VID:-594B}"
BASELINE_FILE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.kb-baseline"
SHOW_LOG=0
SET_BASELINE=0

for arg in "$@"; do
    case "$arg" in
        --baseline) SET_BASELINE=1 ;;
        --log)      SHOW_LOG=1 ;;
        -h|--help)  sed -n '2,30p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数: $arg（试试 --help）" >&2; exit 2 ;;
    esac
done

command -v powershell.exe >/dev/null 2>&1 || {
    echo "找不到 powershell.exe —— 本脚本需要在 WSL 中运行（宿主为 Windows）。" >&2
    exit 1
}

# ---------------------------------------------------------------- 采集
# 用 stdin 喂给 PowerShell，避免 bash/PowerShell 双层引号转义。
PS_OUT="$(powershell.exe -NoProfile -Command - <<PSEOF 2>/dev/null
\$ErrorActionPreference = 'SilentlyContinue'
\$now = Get-Date
Write-Output ("NOW|" + \$now.ToString('yyyy-MM-dd HH:mm:ss'))

\$devs = Get-PnpDevice -PresentOnly | Where-Object { \$_.InstanceId -like '*VID_${VID}*' }
if (-not \$devs) { Write-Output "ABSENT|1"; exit }

# 以键盘 HID 接口(MI_00)为准取枚举时刻
\$main = \$devs | Where-Object { \$_.InstanceId -like '*MI_00*' } | Select-Object -First 1
if (-not \$main) { \$main = \$devs | Select-Object -First 1 }

\$arr = (Get-PnpDeviceProperty -InstanceId \$main.InstanceId -KeyName 'DEVPKEY_Device_LastArrivalDate').Data
if (\$arr) {
    Write-Output ("ARRIVAL|" + \$arr.ToString('yyyy-MM-dd HH:mm:ss'))
    Write-Output ("UPSEC|" + [int](\$now - \$arr).TotalSeconds)
}
\$pd = (Get-PnpDeviceProperty -InstanceId \$main.InstanceId -KeyName 'DEVPKEY_Device_PowerData').Data
if (\$pd) { Write-Output ("POWER|D" + ([BitConverter]::ToInt32(\$pd,4) - 1)) }

foreach (\$d in \$devs) {
    Write-Output ("IFACE|" + \$d.Status + "|" + \$d.ProblemCode + "|" + \$d.InstanceId)
}

# 卡住的修饰键：若固件把修饰键按下后失声，主机侧会残留按下状态
Add-Type -Namespace W -Name K -MemberDefinition '[DllImport("user32.dll")] public static extern short GetAsyncKeyState(int v);'
\$mods = @{ 'LWIN'=0x5B; 'RWIN'=0x5C; 'LALT'=0xA4; 'RALT'=0xA5; 'LCTRL'=0xA2; 'RCTRL'=0xA3; 'LSHIFT'=0xA0; 'RSHIFT'=0xA1 }
\$stuck = @()
foreach (\$k in \$mods.Keys) { if (([W.K]::GetAsyncKeyState(\$mods[\$k]) -band 0x8000) -ne 0) { \$stuck += \$k } }
Write-Output ("STUCKMODS|" + (\$stuck -join ','))

if (${SHOW_LOG} -eq 1) {
    \$since = if (\$arr) { \$arr } else { \$now.AddHours(-12) }
    Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=\$since; Level=1,2,3} |
        Where-Object { \$_.ProviderName -match 'USB|Hid|Kernel-PnP' } |
        Select-Object -First 12 | ForEach-Object {
            Write-Output ("ERRLOG|" + \$_.TimeCreated.ToString('MM-dd HH:mm:ss') + "|" + \$_.ProviderName + "|" + \$_.Id)
        }
    Get-WinEvent -FilterHashtable @{LogName='System'; StartTime=\$since; ProviderName='Microsoft-Windows-Kernel-Power'} |
        Where-Object { \$_.Id -in 42,107 } |
        Select-Object -First 12 | ForEach-Object {
            Write-Output ("PWRLOG|" + \$_.TimeCreated.ToString('MM-dd HH:mm:ss') + "|" + \$_.Id)
        }
}
PSEOF
)"
# PowerShell 输出是 CRLF，必须剥掉 \r —— 否则每个字段末尾都带回车，
# 会导致算术运算报错、以及「卡住修饰键」等空值判断全部失效。
PS_OUT="$(printf '%s' "$PS_OUT" | tr -d '\r')"

get() { echo "$PS_OUT" | grep "^$1|" | head -1 | cut -d'|' -f2-; }

NOW="$(get NOW)"
ARRIVAL="$(get ARRIVAL)"
UPSEC="$(get UPSEC)"
POWER="$(get POWER)"
STUCK="$(get STUCKMODS)"

echo "════════ Dolphin 键盘诊断 ════════"
echo "当前时间   : ${NOW:-?}"

if echo "$PS_OUT" | grep -q '^ABSENT|'; then
    echo "设备状态   : ✗ 未检测到 VID_${VID} 设备"
    echo
    echo "可能原因：USB 未插、插在另一台机器、或正处于 UF2(RPI-RP2) 刷机模式。"
    exit 0
fi

# ---------------------------------------------------------------- 运行时长
if [ -n "${UPSEC:-}" ]; then
    printf '枚举时刻   : %s（已运行 %dh%02dm%02ds）\n' \
        "$ARRIVAL" $((UPSEC/3600)) $(((UPSEC%3600)/60)) $((UPSEC%60))
fi
echo "电源状态   : ${POWER:-?}  (D0=正常供电；D2/D3=被主机挂起)"

# ---------------------------------------------------------------- 接口
echo "USB 接口   :"
echo "$PS_OUT" | grep '^IFACE|' | while IFS='|' read -r _ st pc id; do
    mark="✓"; [ "$st" = "OK" ] || mark="✗"
    short="$(echo "$id" | sed 's/.*\(MI_0[0-9]\).*/\1/; t; s/.*VIAL:.*/VIAL-serial/; t; s/.*&\(COL[0-9]*\).*/\1/')"
    printf '             %s %-12s status=%-6s problem=%s\n' "$mark" "$short" "$st" "${pc:-none}"
done

# ---------------------------------------------------------------- 修饰键
if [ -z "$STUCK" ]; then
    echo "卡住修饰键 : 无"
else
    echo "卡住修饰键 : ✗ $STUCK   ← 主机侧残留按下状态，值得注意"
fi

# ---------------------------------------------------------------- 基线对比
echo "────────────────────────────────"
if [ "$SET_BASELINE" = "1" ]; then
    printf '%s\n' "$ARRIVAL" > "$BASELINE_FILE"
    echo "已记录新基线: $ARRIVAL"
    echo "（刷机后、或重启电脑后，都应该重新执行一次 --baseline）"
elif [ -f "$BASELINE_FILE" ]; then
    BASE="$(head -1 "$BASELINE_FILE")"
    echo "基线枚举时刻: $BASE"
    if [ "$BASE" = "$ARRIVAL" ]; then
        echo "结论        : ✓ 枚举时刻未变 —— 期间没有发生过重新枚举"
        echo "              （若你确实遇到过失声且它自己好了，说明是第 1 级"
        echo "                远程唤醒救的，不产生重新枚举）"
    else
        echo "结论        : ⚠ 枚举时刻已变化！"
        echo "              $BASE  →  $ARRIVAL"
        echo
        echo "  如果这期间你**没有**手动拔插、也没重启电脑，那就是发生过一次失声"
        echo "  并被自动恢复救回。请回忆当时「卡了多久」："
        echo "    约 2 秒  → 第 2 级 restart_usb_driver（E15 硬件锁死的预期表现）"
        echo "    约 10 秒 → 第 3 级 mcu_reset"
        echo "  把这条记录追加到 plan.md 的观察记录表，然后跑 --baseline 更新基线。"
    fi
else
    echo "基线        : 尚未记录。先跑一次：scripts/kb-diag.sh --baseline"
fi

# ---------------------------------------------------------------- 日志
if [ "$SHOW_LOG" = "1" ]; then
    echo "────────────────────────────────"
    echo "USB/HID 错误日志（自本次枚举起）:"
    if echo "$PS_OUT" | grep -q '^ERRLOG|'; then
        echo "$PS_OUT" | grep '^ERRLOG|' | while IFS='|' read -r _ t p i; do
            printf '             %s  %s  id=%s\n' "$t" "$p" "$i"
        done
    else
        echo "             无（这本身是重要信息：失声时主机侧通常零错误）"
    fi
    echo "休眠/唤醒事件（Kernel-Power 42=进入休眠, 107=唤醒）:"
    if echo "$PS_OUT" | grep -q '^PWRLOG|'; then
        echo "$PS_OUT" | grep '^PWRLOG|' | while IFS='|' read -r _ t i; do
            printf '             %s  id=%s\n' "$t" "$i"
        done
    else
        echo "             无 —— 可排除休眠唤醒相关的诱因"
    fi
fi

echo "════════════════════════════════"
