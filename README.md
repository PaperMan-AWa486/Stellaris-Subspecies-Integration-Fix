# Stellaris 亚种整合修复

适用于 Windows x64 版《群星》Cygnus v4.5.4。安装后，开启「整合入默认亚种」的模板会按游戏原有的月度速度转入你选定的默认模板。

## 修复的问题

假设「干活」是默认模板，「组装 3」已经开启整合。「组装 2」和「组装 3」的特质与其他物种权利相同。给「组装 2」也开启整合后，两套模板满足了游戏的月度自动合并条件。下一次月度结算可能一次性把「组装 2」的剩余人口转给「组装 3」，并把「组装 3」设为默认。

游戏每月分别执行限额整合和相似亚种合并。普通整合使用每个殖民地每月 25 人口的基础值，受游戏内修正影响。相似亚种合并可以一次转换整个模板的人口，并在共用的人口转换函数里写入默认模板。补丁让正在整合的亚种跳过这次相似模板合并，并阻止普通的相似模板合并覆盖已选定的默认模板。其他相似模板仍可自动合并，玩家也可以照常手动设置默认模板。

## 下载与安装

1. 完全退出《群星》。
2. 在 [Releases](https://github.com/PaperMan-AWa486/Stellaris-Subspecies-Integration-Fix/releases/latest) 的 **Assets** 下载 `version.dll`。
3. 把 DLL 放进 `stellaris.exe` 所在的目录。Steam 用户可在游戏库右键《群星》，选择「管理 → 浏览本地文件」找到该目录。
4. 正常启动游戏。打开游戏目录的 `subspecies_integration_fix.log`，看到 `INSTALLED` 就表示两处补丁已加载。

如果目录里已有同名 DLL，请保留它。已安装 [Stellaris-Mod-Injector](https://github.com/MyresMkG/Stellaris-Mod-Injector) v1.2.0 或更新版本的玩家，可以将本补丁重命名为 `subspecies_integration_fix.dll`，放在 `injected_mods\subspecies_integration_fix\` 下，由现有加载器加载。日志会出现在补丁 DLL 旁边。

要验证曾遇到的情况，请读回合并前的存档，开启整合并推进到下个月。已写进存档的人口转换和默认模板变更需要通过读回旧档恢复。

## 版本与停用

- 仅验证过 Windows x64 的 Cygnus v4.5.4。支持的 `stellaris.exe` SHA-256 为 `10D2613A392FE8AC7982701899DC2E43EBF4C1C721EA1BAEBA6C3FB25DB36EB1`。
- 日志显示 `SKIPPED` 表示游戏版本或补丁位置与预期不符。游戏升级后请先检查是否有对应更新。
- 补丁只改动运行时内存。退出游戏后，移走本补丁的 DLL，下次启动即停用。

## 从源码构建

打开 Visual Studio 的 **x64 Native Tools Command Prompt**，进入仓库目录并运行以下命令。

```bat
cl /nologo /std:c++17 /O2 /MT /W4 /WX /c subspecies_integration_fix.cpp
lib /nologo /DEF:version.def /MACHINE:X64 /OUT:version.lib
link /nologo /DLL /INCREMENTAL:NO /OUT:version.dll subspecies_integration_fix.obj version.exp
```

运行时调用点补丁参考 [Stellaris-Market-Fix](https://github.com/firefly1596/Stellaris-Market-Fix)。
