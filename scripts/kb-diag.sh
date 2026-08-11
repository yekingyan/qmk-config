#!/usr/bin/env bash
# Dolphin 键盘诊断脚本
#
# 用途：一条命令抓齐排查「USB 失声」所需的全部信息，并把事件自动记进历史。
#
# 背景：WSL2 里没有 USB 子系统（/sys/bus/usb、/dev/hidraw* 都不存在），
# 所以必须绕道 Windows 的 PowerShell 查询 PnP 设备状态。本脚本封装了这些查询。
#
# 用法：
#   scripts/kb-diag.sh              查看当前状态、与基线对比，并自动记录新事件
#   scripts/kb-diag.sh --baseline   把当前枚举时刻记为新基线（重启电脑后必做）
#   scripts/kb-diag.sh --flashed    刷机后专用：记基线 + 打一个「固件刷入」标记
#   scripts/kb-diag.sh --log        额外输出最近的 USB/HID 与电源事件日志
#   scripts/kb-diag.sh --history    打印事件历史（默认最近 20 条，可 --history 50）
#   scripts/kb-diag.sh --watch [秒] 后台盯着，每 N 秒采样一次（默认 60）
#   scripts/kb-diag.sh --quiet      只在检测到新事件时输出（给 --watch / cron 用）
#   scripts/kb-diag.sh --help
#
# 关键判读（详见 plan.md「下次失声时怎么做」）：
#   枚举时刻变了而你没拔线  => 期间发生过一次失声，并被自动恢复救回
#   恢复耗时约 2 秒         => 第 2 级 restart_usb_driver（E15 硬件锁死的预期表现）
#   恢复耗时约 10 秒        => 第 3 级整芯片复位
#   完全没恢复             => 见 plan.md 情况 B，拔线前先按几个键等 6 秒
#   出现「枚举失败」记录    => 主机侧弹过「无法识别的 USB 设备」，
#                             即恢复动作打断了自己的枚举，见 plan.md「自伤」一节
#
# 为什么「枚举失败」要跟固件刷入时刻比：Windows 会把失败枚举的残留节点
# （USB\VID_0000&PID_0002，FriendlyName 含 Device Descriptor Request Failed）
# 长期留在 PnP 库里，按物理端口归档。换了 USB 口就可能撞上那个口上几天前的旧记录，
# 拿它告警纯属误导。所以只有「晚于本次固件刷入」的才算证据 —— 用 --flashed 打标记。
#
# 为什么需要事件历史：Windows 的 LastArrivalDate 只保留**最后一次**枚举，
# 而 Kernel-PnP 的日志对「已安装设备的重新枚举」一条都不记（实测近 12 小时零记录）。
# 所以不主动定时采样的话，事件次数只能靠人的体感 —— 2026-08-11 上午就因此漏了两次。

set -uo pipefail

VID="${KB_VID:-594B}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASELINE_FILE="$SCRIPT_DIR/.kb-baseline"
STATE_FILE="$SCRIPT_DIR/.kb-state"      # 自动记录用的「上次看到的值」
EVENTS_FILE="$SCRIPT_DIR/.kb-events.log" # append-only 事件历史

FLASHED_AT=""   # 上次 --flashed 记下的固件刷入时刻，由 record_events 从 .kb-state 读出
NEW_EVENTS=0
PORT=""

SHOW_LOG=0
SET_BASELINE=0
MARK_FLASHED=0
SHOW_HISTORY=0
HISTORY_N=20
WATCH=0
WATCH_SEC=60
QUIET=0

while [ $# -gt 0 ]; do
    case "$1" in
        --baseline) SET_BASELINE=1 ;;
        --flashed)  SET_BASELINE=1; MARK_FLASHED=1 ;;
        --log)      SHOW_LOG=1 ;;
        --quiet)    QUIET=1 ;;
        --history)  SHOW_HISTORY=1
                    case "${2:-}" in [0-9]*) HISTORY_N="$2"; shift ;; esac ;;
        --watch)    WATCH=1
                    case "${2:-}" in [0-9]*) WATCH_SEC="$2"; shift ;; esac ;;
        -h|--help)  sed -n '2,40p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "未知参数: $1（试试 --help）" >&2; exit 2 ;;
    esac
    shift
done

# ---------------------------------------------------------------- --history
if [ "$SHOW_HISTORY" = "1" ]; then
    echo "════════ 事件历史（$EVENTS_FILE）════════"
    if [ -s "$EVENTS_FILE" ]; then
        tail -n "$HISTORY_N" "$EVENTS_FILE"
        echo "────────────────────────────────"
        printf '共 %s 条记录；其中重新枚举 %s 次、枚举失败 %s 次\n' \
            "$(wc -l < "$EVENTS_FILE" | tr -d ' ')" \
            "$(grep -c 'RE-ENUM' "$EVENTS_FILE" || true)" \
            "$(grep -c 'FAILED-ENUM' "$EVENTS_FILE" || true)"
    else
        echo "（还没有记录。跑一次不带参数的 kb-diag.sh 就会开始记；"
        echo "  想不漏事件请用 --watch 常驻。）"
    fi
    exit 0
fi

command -v powershell.exe >/dev/null 2>&1 || {
    echo "找不到 powershell.exe —— 本脚本需要在 WSL 中运行（宿主为 Windows）。" >&2
    exit 1
}

# ================================================================ 采集
collect() {
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

# 键盘所在的物理端口，用来把「枚举失败」残留节点关联到本键盘
\$port = \$null
foreach (\$d in \$devs) {
    \$l = (Get-PnpDeviceProperty -InstanceId \$d.InstanceId -KeyName 'DEVPKEY_Device_LocationInfo').Data
    if (\$l -like 'Port_#*') { \$port = \$l; break }
}
if (\$port) { Write-Output ("PORT|" + \$port) }

# 枚举失败的残留节点：主机弹「无法识别的 USB 设备」时，Windows 会建一个
# USB\VID_0000&PID_0002 节点（FriendlyName 含 Device Descriptor Request Failed）。
# 不加 -PresentOnly，因为枚举成功后这个节点会变成 not-present 但记录还在。
foreach (\$d in (Get-PnpDevice | Where-Object { \$_.InstanceId -like 'USB\VID_0000&PID_0002*' })) {
    \$l = (Get-PnpDeviceProperty -InstanceId \$d.InstanceId -KeyName 'DEVPKEY_Device_LocationInfo').Data
    if (\$port -and \$l -ne \$port) { continue }
    \$a = (Get-PnpDeviceProperty -InstanceId \$d.InstanceId -KeyName 'DEVPKEY_Device_LastArrivalDate').Data
    if (\$a) { Write-Output ("FAILENUM|" + \$a.ToString('yyyy-MM-dd HH:mm:ss') + "|" + \$l) }
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
}

get() { echo "$PS_OUT" | grep "^$1|" | head -1 | cut -d'|' -f2-; }

# ================================================================ 事件记录
# 与 .kb-state 里「上次看到的值」比较，有变化就 append 到事件历史。
# 这一步与 --baseline 无关：baseline 是给人看的对比基准（刷机后手动重置），
# state 是给自动记录用的游标。
record_events() {
    local arrival="$1" failenum="$2" now="$3"
    local prev_arrival="" prev_failenum="" n=0

    if [ -f "$STATE_FILE" ]; then
        prev_arrival="$(sed -n 's/^arrival=//p'  "$STATE_FILE" | head -1)"
        prev_failenum="$(sed -n 's/^failenum=//p' "$STATE_FILE" | head -1)"
        FLASHED_AT="$(sed -n 's/^flashed=//p'   "$STATE_FILE" | head -1)"
    fi

    if [ -n "$arrival" ] && [ -n "$prev_arrival" ] && [ "$arrival" != "$prev_arrival" ]; then
        printf '%s  RE-ENUM       %s  ->  %s\n' "$now" "$prev_arrival" "$arrival" >> "$EVENTS_FILE"
        n=$((n+1))
    fi
    # 只把「晚于本次刷机」的失败枚举记成事件，否则换 USB 口时会把旧端口记录当成新事件
    if [ -n "$failenum" ] && [ "$failenum" != "$prev_failenum" ]; then
        if [ -z "$FLASHED_AT" ] || [ ! "$failenum" \< "$FLASHED_AT" ]; then
            printf '%s  FAILED-ENUM   %s  （主机弹过「无法识别的 USB 设备」）\n' "$now" "$failenum" >> "$EVENTS_FILE"
            n=$((n+1))
        fi
    fi

    if [ "$MARK_FLASHED" = "1" ]; then
        FLASHED_AT="$now"
        printf '%s  FLASHED       固件刷入，观察窗口从此重新计起（端口 %s）\n' "$now" "${PORT:-?}" >> "$EVENTS_FILE"
    fi

    {
        printf 'arrival=%s\n' "$arrival"
        printf 'failenum=%s\n' "$failenum"
        printf 'flashed=%s\n' "${FLASHED_AT:-}"
    } > "$STATE_FILE"
    NEW_EVENTS="$n"
}

# ================================================================ 输出
report() {
    NOW="$(get NOW)"
    ARRIVAL="$(get ARRIVAL)"
    UPSEC="$(get UPSEC)"
    POWER="$(get POWER)"
    STUCK="$(get STUCKMODS)"
    PORT="$(get PORT)"
    FAILENUM="$(echo "$PS_OUT" | grep '^FAILENUM|' | cut -d'|' -f2 | sort | tail -1)"

    if echo "$PS_OUT" | grep -q '^ABSENT|'; then
        [ "$QUIET" = "1" ] && { echo "$NOW  ✗ 未检测到 VID_${VID} 设备"; return; }
        echo "════════ Dolphin 键盘诊断 ════════"
        echo "当前时间   : ${NOW:-?}"
        echo "设备状态   : ✗ 未检测到 VID_${VID} 设备"
        echo
        echo "可能原因：USB 未插、插在另一台机器、或正处于 UF2(RPI-RP2) 刷机模式。"
        return
    fi

    record_events "$ARRIVAL" "$FAILENUM" "${NOW:-?}"

    # --quiet：只在有新事件时说话，适合挂在 --watch / cron 里
    if [ "$QUIET" = "1" ]; then
        if [ "${NEW_EVENTS:-0}" != "0" ]; then
            tail -n "$NEW_EVENTS" "$EVENTS_FILE"
        fi
        return
    fi

    echo "════════ Dolphin 键盘诊断 ════════"
    echo "当前时间   : ${NOW:-?}"
    if [ -n "${UPSEC:-}" ]; then
        printf '枚举时刻   : %s（已运行 %dh%02dm%02ds）\n' \
            "$ARRIVAL" $((UPSEC/3600)) $(((UPSEC%3600)/60)) $((UPSEC%60))
    fi
    echo "电源状态   : ${POWER:-?}  (D0=正常供电；D2/D3=被主机挂起)"
    [ -n "$PORT" ] && echo "物理端口   : $PORT"

    echo "USB 接口   :"
    echo "$PS_OUT" | grep '^IFACE|' | while IFS='|' read -r _ st pc id; do
        mark="✓"; [ "$st" = "OK" ] || mark="✗"
        short="$(echo "$id" | sed 's/.*\(MI_0[0-9]\).*/\1/; t; s/.*VIAL:.*/VIAL-serial/; t; s/.*&\(COL[0-9]*\).*/\1/')"
        printf '             %s %-12s status=%-6s problem=%s\n' "$mark" "$short" "$st" "${pc:-none}"
    done

    if [ -z "$STUCK" ]; then
        echo "卡住修饰键 : 无"
    else
        echo "卡住修饰键 : ✗ $STUCK   ← 主机侧残留按下状态，值得注意"
    fi

    # 枚举失败残留节点。只有「晚于本次固件刷入」的才算证据 —— Windows 会把这类
    # 节点按物理端口长期留在 PnP 库里，换个 USB 口就可能撞上那个口上几天前的旧记录。
    if [ -n "$FAILENUM" ]; then
        if [ -n "$FLASHED_AT" ] && [ "$FAILENUM" \< "$FLASHED_AT" ]; then
            echo "枚举失败    : （端口 $PORT 上有一条 $FAILENUM 的旧记录，早于本次刷机"
            echo "              $FLASHED_AT，与当前固件无关，忽略）"
        else
            echo "枚举失败    : ⚠ $FAILENUM（同一物理端口 $PORT）"
            echo "              主机弹过「无法识别的 USB 设备」= 恢复动作打断了自己的枚举，"
            echo "              见 plan.md「自伤」一节。本次固件刷入于 ${FLASHED_AT:-未记录}。"
        fi
    else
        echo "枚举失败    : 无（同一端口上没有 Device Descriptor Request Failed 残留）"
    fi

    # ------------------------------------------------------------ 基线对比
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
            echo "  按 Vial 里配的 USB_DIAG 键还能把固件侧计数打出来。"
            echo "  记完后跑 --baseline 更新基线；完整历史看 --history。"
        fi
    else
        echo "基线        : 尚未记录。先跑一次：scripts/kb-diag.sh --baseline"
    fi

    if [ "${NEW_EVENTS:-0}" != "0" ]; then
        echo "────────────────────────────────"
        echo "本次新增 ${NEW_EVENTS} 条事件记录:"
        tail -n "$NEW_EVENTS" "$EVENTS_FILE" | sed 's/^/  /'
    fi

    # ------------------------------------------------------------ 日志
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
}

# ================================================================ 主流程
if [ "$WATCH" = "1" ]; then
    QUIET=1
    echo "开始盯着键盘，每 ${WATCH_SEC} 秒采样一次；只在检测到新事件时输出。"
    echo "事件历史: $EVENTS_FILE    （Ctrl-C 退出）"
    echo "建议挂后台：nohup scripts/kb-diag.sh --watch 60 >> scripts/.kb-watch.out 2>&1 &"
    while true; do
        collect
        report
        sleep "$WATCH_SEC"
    done
fi

collect
report
