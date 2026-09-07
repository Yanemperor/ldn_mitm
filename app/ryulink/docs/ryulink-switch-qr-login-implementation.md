# RyuLink App 登录二维码：实现说明与需求

> 范围：Nintendo Switch 上的 `RyuLink.nro` 登录页。本文描述当前代码的实际工作方式，并给出继续维护或重做时必须满足的要求。

## 结论

登录二维码采用 OAuth 2.0 / OpenID Connect **Device Authorization Grant（设备授权流程，Device Flow）**。二维码不是登录凭据，也不包含账号密码或 `device_code`；它仅编码认证服务返回的浏览器授权链接。用户用手机或电脑完成注册、登录和授权后，Switch 按服务端给出的频率轮询 token endpoint，获得访问令牌并进入 App。

这适合 Switch：设备无需输入密码、没有回调地址，也不需要在 NRO 中保存 `client_secret`。

## 用户流程

```text
Switch                                      手机 / 电脑                         认证与业务服务
  | A：开始登录                                  |                                  |
  |-- 读取 OIDC discovery ---------------------->|                                  |
  |-- 申请 device_code ------------------------>|                                  |
  |<-- user_code + 授权链接 + 超时/间隔 --------|                                  |
  |  将授权链接编码为二维码，显示短码             |                                  |
  |                                               |-- 扫码、注册/登录、同意授权 ----->|
  |                                               |<-- 授权完成 ---------------------|
  |-- 按 interval 轮询 token endpoint --------->|                                  |
  |<-- access_token ----------------------------|                                  |
  |-- GET /player/me (Bearer access_token) ---->|                                  |
  |<-- 玩家资料、权限、VIP 状态 ----------------|                                  |
  |-- 换取并保存 RyuLink opaque session ------->|                                  |
  |<-- session token ---------------------------|                                  |
  |  进入大厅                                    |                                  |
```

## 当前实现位置

| 职责 | 文件 | 当前实现 |
| --- | --- | --- |
| 登录状态、Device Flow、HTTP 与 token 轮询 | `source/auth.c`、`source/auth.h` | `RyuLinkAuthSession` 在 Idle / AwaitingBrowser / Authenticated / Error 间流转。 |
| 登录页和二维码绘制 | `source/app.c` | `draw_login()` 显示二维码、短码、等待或错误状态。 |
| 二维码编码 | `source/qrcodegen.c`、`source/qrcodegen.h` | 纯 C QR Code generator，在本机将 URL 编为二维码矩阵。 |
| 主循环和异步时序 | `source/main.c`、`source/app.c` | 主循环每帧调用 `ryuLinkAppHandleInput()`；等待授权期间会按到期时间触发 `ryuLinkAuthUpdate()`。 |
| 持久会话 | `source/session_store.c`、`source/session_store.h` | 仅保存 RyuLink 业务 opaque session 和 installation id，带版本、CRC、临时文件后 rename 的写入保护。 |

## 前端展示要求

1. 未登录页面显示“使用手机登录”和 `A 开始登录`。
2. 成功申请设备码后，页面必须同时展示：
   - `verification_uri_complete` 生成的二维码；
   - 原样返回的 `user_code`；
   - 手动备用路径：`verification_uri` + `user_code`；
   - “等待授权”状态和 `B 取消`。
3. `verification_uri_complete` 缺失时，客户端应使用受信任的 `verification_uri` 加 URL-encode 后的 `user_code` 组装二维码链接；若二维码编码失败，仍须保留可读短码和手动打开地址。
4. 已授权后无需用户再次按键，自动加载玩家资料并进入大厅。
5. 用户按 B 取消、设备码过期、用户拒绝授权或发生不可恢复错误时，停止轮询、清空临时授权数据并回到可重试的登录入口。
6. 二维码应有白色底和足够的静区（quiet zone），黑白对比明确；不得把二维码放在动态背景、渐变或遮罩上。

当前 UI 在 1280×720 下预留 280×280 像素区域；二维码按有效 payload 缓存，避免每帧重复编码。库以中等纠错级别开始，并允许提升纠错等级。

## 客户端认证实现要求

### 1. OIDC discovery 与设备码申请

1. 从 `https://auth.ryulink.xyz/oidc/.well-known/openid-configuration` 读取 discovery。
2. 校验 `issuer` 精确等于 `https://auth.ryulink.xyz/oidc`。
3. 只使用 discovery 中的 `device_authorization_endpoint` 和 `token_endpoint`；两者必须是 `https://auth.ryulink.xyz/` 下的 HTTPS URL，不接受 IP、其他 host、非标准端口和跳转。
4. 向 device authorization endpoint 发送 form：

```text
client_id=<Switch public client id>
scope=openid ryulink:player
resource=https://api.ryulink.xyz/ryulink
```

5. Switch 应配置为 Native / public client；**不得**内置 `client_secret`，也不请求 `offline_access`。
6. 成功响应必须包含非空的 `device_code`、`user_code`、`verification_uri`、`expires_in`；`interval` 缺失时默认 5 秒。`verification_uri_complete` 可选。

`device_code` 是仅用于换 token 的机密值：只能留在内存，不能出现在屏幕、二维码、日志、崩溃包或 SD 卡中。

### 2. 二维码内容与生成

二维码 payload 的优先级：

```text
verification_uri_complete
    ↓（该字段缺失时）
verification_uri + "?user_code=" + url_encode(user_code)
```

不得用 `device_code`、access token、RyuLink session token、用户密码或任何后台权限信息替代该 payload。`user_code` 是用户可见的短码，允许显示并作为手动输入兜底；客户端不得假定它的固定长度、字符集或横杠格式。

当前实现使用 `qrcodegen_encodeText()`，版本范围为 1–20、自动 mask、`MEDIUM` 纠错并允许自动提升。编码失败时 `g_qr_ready=false`；这不会阻断 Device Flow，但页面需要保持手动登录说明。

### 3. 轮询 token endpoint

从设备码申请成功后的 `interval` 秒开始轮询，绝不快于当前间隔：

```text
grant_type=urn:ietf:params:oauth:grant-type:device_code
device_code=<仅内存中的 device_code>
client_id=<Switch public client id>
resource=https://api.ryulink.xyz/ryulink
```

| 响应 | 客户端行为 |
| --- | --- |
| `200` + `Bearer access_token` + 正的 `expires_in` | 请求玩家资料；成功后交换为 RyuLink session。 |
| `authorization_pending` | 继续按当前间隔等待。 |
| `slow_down` | 间隔增加 5 秒后再轮询。 |
| 网络失败、429、500、502、503、504 | 设备码未过期时退避重试：至少 5 秒、指数增长、最多 30 秒。 |
| `access_denied`、`expired_token`、其他 4xx 或无效 JSON | 停止本次登录，显示可重试错误。 |
| 到达 `expires_in` | 停止轮询、清除 device code，提示二维码已过期。 |

access token 仅临时用于调用 `/player/me` 和交换 RyuLink session；不要持久化 Logto access token、refresh token 或 ID token。

### 4. 业务会话与恢复登录

完成 Device Flow 后：

1. `GET https://api.ryulink.xyz/app-api/ryulink/player/me`，使用 `Authorization: Bearer <access token>`，校验玩家状态、测试资格与 VIP 状态。
2. `POST /app-api/ryulink/session/exchange`，将短期 access token 换为 RyuLink opaque session。
3. 本地生成 32 个十六进制字符的 installation id；将 installation id 与 opaque session 写入 `sdmc:/switch/RyuLink/session.dat`。
4. 下次启动时，先读取该文件并调用 `/player/me` 验证。若服务端返回 401，则删除本地会话并回到二维码登录。
5. 用户主动退出时调用 `/app-api/ryulink/session/logout`，删除本地会话，并清空内存中的 device code、access token 和 session。

`session.dat` 当前使用 `RYLS` magic、格式版本和 CRC32 检测损坏，并通过 `.tmp` 文件写完后 rename 降低断电造成的半写入风险。CRC32 只用于完整性检查，**不是加密或防篡改**；服务端必须始终验证 opaque session 的有效性、绑定关系和撤销状态。

## 网络与安全要求

- 所有认证与 API 请求仅允许 HTTPS；启用证书链和主机名校验，禁止关闭 TLS 验证，禁止跟随重定向。
- HTTP 响应体上限为 8 KiB，单一字符串缓冲区上限为 4 KiB；超限或 JSON 类型/字段不符均安全失败。
- 每个请求连接与总超时均为 5 秒；DNS 解析失败可使用预设解析回退，但 TLS URL host 仍必须为原 host。
- 使用编译期版本生成 `User-Agent: RyuLink/<semver>`。
- 日志、遥测、错误页面及诊断上传必须脱敏：不得输出密码、`device_code`、access/refresh/ID token、RyuLink session token 或 Authorization header。
- 账号禁用、没有玩家权限、没有测试资格或 VIP 不可在客户端绕过；客户端只显示状态并由服务端决定授权。

## 现状核对与待补齐项

| 项目 | 现状 | 建议的验收要求 |
| --- | --- | --- |
| Device Flow 与二维码 | 已实现 | 真机能扫码，浏览器授权一次后 Switch 自动进大厅。 |
| 备用短码 | 已实现 | `verification_uri_complete` 不存在或二维码无法生成时，仍能用地址 + 短码登录。 |
| 域名、HTTPS、证书与重定向限制 | 已实现 | 对非 `auth.ryulink.xyz` endpoint、HTTP、证书错误、3xx 均拒绝。 |
| 轮询节流与 `slow_down` | 已实现 | 抓包证明不快于服务端 interval，`slow_down` 后增加 5 秒。 |
| `Retry-After` | 未实现 | 429/503 如携带该 header，应把等待时间取为 `max(退避时间, Retry-After)`。 |
| 二维码尺寸上限 | 有边界 | 当前最高 QR version 20；对最长允许授权链接做编码与真机扫码测试。若模块宽度低于 4 px，应缩短链接、增大显示区域或提高允许版本。 |
| 持久会话说明 | 代码已实现，README 仍称“仅内存” | 以本文和 player API 文档为准；同步修正 README，明确仅持久化 opaque session，而非 Logto token。 |
| 自动化测试 | 未见 Device Flow / QR 专项测试 | 对 payload 生成、URL 白名单、到期/取消、轮询节流、所有 OAuth 错误和 session 文件损坏添加单元测试；再做一次真实账号真机验收。 |

## 最小验收清单

1. 新设备按 A 后在 5 秒内出现二维码与短码；手机扫码打开的域名只能是 `auth.ryulink.xyz`。
2. 手机完成注册、登录和授权后，Switch 无需额外操作即可进入大厅。
3. 手工输入备用地址和短码也可完成同一登录。
4. 扫码后拒绝授权、等待到期、按 B 取消、断网、服务端 429/5xx 都不会卡死页面或继续超频轮询。
5. 二维码或日志中绝不出现 `device_code`、access token、refresh token 或 RyuLink session token。
6. 退出后 session 文件被删除；另一设备登录或服务端撤销后，本设备下一次请求得到 401 并要求重新扫码。
7. 在目标 Switch 屏幕亮度、常见手机相机与中英文界面下均可稳定识别二维码。
