# users/vial/rules.mk
#
# QMK 的 userspace 机制：keymap 名为 `vial` 时，`users/vial/` 会自动被加入
# VPATH 与 include 路径（构建日志里能看到 `-Iusers/vial`），并且这个 rules.mk
# 会被 `builddefs/build_keyboard.mk` 自动 include。
#
# 我们用它来共享 dolphin52 / dolphin54 的键盘级代码 —— 两者原先各有一份
# 字节级完全相同的 `dolphin5x.c`，靠手工 cp 同步且 CI 无守卫，迟早漂移。
#
# 注意：本目录只对「keymap 名 = vial」的键盘生效。本仓库里正好只有
# dolphin52 与 dolphin54 用这个 keymap 名（ferris 用的是 yekingyan），
# 所以不会误伤归档项目。若以后新增别的键盘也叫 vial keymap，需要重新评估。
#
# `SRC` 用裸文件名，靠 VPATH 解析（QMK 里 QUANTUM_LIB_SRC / SRC 都是这个惯例）。
SRC += dolphin5x.c
