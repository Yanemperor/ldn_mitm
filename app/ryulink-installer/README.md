# RyuLink Installer

跨平台的 RyuLink SD 卡安装器，源于 `gitcode.com:qq_31772349/ryu_ldn_nx` 的
RyuLink Assistant 框架（检查版本 `551f321`），已改为当前 RyuLink 的五文件白名单。

它只识别同时含有 `atmosphere/` 和 `switch/` 的可移动 SD 卡，并且只写入：

```text
switch/RyuLink/RyuLink.nro
atmosphere/contents/4200000000000010/exefs.nsp
atmosphere/contents/4200000000000010/flags/boot2.flag
switch/.overlays/ldnmitm_config.ovl
config/ldn_mitm/relay.cfg
```

安装前会在本机应用数据目录备份原有 Core、旧 disabled Core、Tesla Overlay 和 Relay 配置；每个写入文件
都会通过 SHA-256 回读验证。`payload/` 由发布脚本从仓库根目录的 `out/sd/` 准备，不能混用
其他构建的文件。

## 测试

```bash
dotnet run --project tests/RyuLink.Installer.Core.Tests
```

## 发布

```bash
python3 scripts/package-release.py --version 1.0.0 --target windows-x64
```

Windows 产物是自包含单文件 `.exe`；macOS 产物需在对应架构的 macOS 主机完成代码签名和公证后再分发。
