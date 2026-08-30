# RyuLink Switch 首次部署手册

适用范围：将已验证的 RyuLink App 与 LDN_MITM Core 部署到**一台新的测试 Switch**。本手册只覆盖 Switch SD 卡；不操作 Backend、数据库、Admin/Web 或其他 Switch。

## 必须同时部署的四个文件

从同一份已批准构件中准备下列文件，并保持相对路径不变：

```text
switch/RyuLink/RyuLink.nro
atmosphere/contents/4200000000000010/exefs.nsp
atmosphere/contents/4200000000000010/flags/boot2.flag
config/ldn_mitm/relay.cfg
```

`boot2.flag` 是零字节启动标记，但不能省略。只部署 NRO，或把旧的 `contents.disabled/420000000000000B` 改名为新 Title ID，都会导致 Core IPC 不可用，并显示“LDN_MITM 中继设置失败”。

不要部署任何其他 sysmodule、不要修改现有其它 Atmosphere content、不要改写 relay.cfg 以外的 SD 卡配置。

## 部署前检查

1. 使用经批准的构件；不要从未提交工作区复制文件。
2. 确认 Core 的 Title ID 为 `4200000000000010`，并包含 App 使用的 IPC：`SetVirtualIp`（65015）和 `SetInternetRelayEnabled`（65014）。
3. 检查 relay.cfg：包含一个 `RyuLink` profile，且 `enabled=1`、`broadcast=1`、`selected=RyuLink`；不得包含密码、token 或私钥。
4. 为这三处现有内容创建可恢复备份：

   ```text
   atmosphere/contents/4200000000000010/
   atmosphere/contents.disabled/420000000000000B/
   config/ldn_mitm/
   ```

   不删除或改名 `contents.disabled/420000000000000B`。

## 写卡步骤

1. 将四个文件复制到 SD 卡根目录下的相同相对路径。
2. 只替换这四个目标文件；不要更新 `RyuLink.nro` 以外的 App，也不要触碰其它 sysmodule。
3. 复制后重新计算四个目标文件的 SHA-256，并与候选构件逐一比对。
4. 清除 macOS 自动生成的 `._*` 资源叉副文件；目标目录中不应存在它们。
5. 安全弹出 SD 卡。

## 启动与验收

1. 将 SD 卡插入 Switch，**完整关机**后启动 CFW；不能只休眠、重新打开 HBMenu 或热替换 Core。
2. 确认没有 Atmosphere fatal。
3. 启动 RyuLink，登录或恢复会话并加入房间。
4. 通过标准：

   - 不出现“无法注册此设备”或“LDN_MITM 中继设置失败”；
   - App 获得服务器分配的虚拟 IP；
   - Internet Relay 已启用；
   - 返回/退出后再次进入房间，仍能重新启用 Relay；
   - 服务端保持 running、RestartCount=0、无 OOM，且无新增 5xx；
   - 同一设备重试不生成重复 device 或 virtual-IP lease。

`ldn_mitm.log` 只有在 Tesla overlay 或 `ldnmitm_config` 中单独开启 Logging 后才会产生；没有该日志本身不表示 Core 未加载。

## 失败处理与回退

若失败，停止进一步部署；不要修改 Backend、数据库、App 源码或重新生成 Core。保留并收集：App 错误、Core SHA-256、relay.cfg（脱敏后）、`ldn_mitm.log`（若 Logging 已开启）及 Atmosphere crash report。

若出现启动 fatal 或明确 Core 回归，恢复本机部署前备份中的同一路径文件，再完整关机重启 CFW。

## 已验证参考构件（2026-08-27）

```text
App NRO SHA-256:       b590f6fda0046565b64b0a0d0e6fd24de3bb41da14d641afe567ead9d6f1fcaf
LDN_MITM Core SHA-256: c14ccb9651e4c747bff6eb827a5a043d86120d4f249aa72358e4fbcad9d5cb58
relay.cfg SHA-256:     b12765f3933bf6d1715093765e2f3c3f1ef4ccda68522a1bb6f53ba5d81b5c09
boot2.flag:            0 bytes
```

这些哈希仅适用于该已验证构件。重新构建或更换 relay.cfg 后，必须重新审计并记录相应哈希。
