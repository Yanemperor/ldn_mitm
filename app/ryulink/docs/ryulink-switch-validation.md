# RyuLink Switch App 验收清单

## 当前 MVP

- App 使用 Logto Device Flow；可撤销的 opaque session 保存在 `sdmc:/switch/RyuLink/session.dat`。
- App 只展示一个 PUBLIC 房间。选房仅确认产品侧成员资格，不向核心下发房间网络参数。
- 所有玩家使用 `sdmc:/config/ldn_mitm/relay.cfg` 中固定的 RyuLink Relay，视为同一虚拟 LAN。
- `ldn_mitm` logging 开启后写入 `sdmc:/ldn_mitm.log`；App 的诊断上传只收集白名单日志并先脱敏。

## 构建与安装

从仓库根目录运行：

```bash
docker compose run --rm devkit
```

将 `out/sd/` 的内容合并复制到 SD 卡根目录。验收所需文件为：

```text
switch/RyuLink/RyuLink.nro
atmosphere/contents/4200000000000010/exefs.nsp
atmosphere/contents/4200000000000010/flags/boot2.flag
config/ldn_mitm/relay.cfg
```

每次构建后在本机重新计算 `out/sd/switch/RyuLink/RyuLink.nro` 的 SHA-256；不得复用旧核心版本的大小或哈希值。

## 真机验收

1. 启动 `ldn_mitm`，确认 `relay.cfg` 中 `enabled=1`、`selected=RyuLink`，并在配置 NRO 或 Tesla overlay 中打开 logging。
2. 从 HBMenu 启动 `switch/RyuLink/RyuLink.nro`，完成登录并进入唯一 PUBLIC 房间。
3. 返回 HOME，启动支持本地联机的游戏；两台设备应能通过同一 Relay 扫描、创建和加入 LDN 会话。
4. 如失败，保留 `sdmc:/ldn_mitm.log` 和最新 Atmosphère crash report，再从 App 上传已脱敏的诊断证据。

## MTP 安装

若通过 DBI MTP 写入 SD 卡，源文件为：

```text
out/sd/switch/RyuLink/RyuLink.nro
```

同时必须复制同一 `out/sd/` 构建中的 sysmodule、零字节 `flags/boot2.flag` 启动标记和 `config/ldn_mitm/relay.cfg`；单独更新 NRO 不足以完成该 MVP 的安装。
