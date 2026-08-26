# RyuLink Switch 玩家身份接口（第一期）

RyuLink App 指 Switch 上的 NRO。注册、登录、找回密码和 MFA 全部由 Logto 的 OIDC 授权页处理；NRO 不向控制面提交密码。

## 固定配置

| 项目 | 值 |
| --- | --- |
| Logto endpoint | `https://auth.ryulink.xyz/oidc` |
| OIDC issuer (`iss`) | `https://auth.ryulink.xyz/oidc` |
| OIDC discovery | `https://auth.ryulink.xyz/oidc/.well-known/openid-configuration` |
| Switch Native App ID (`client_id`) | `oqjslw0gq1s83h65uyp7m` |
| API resource (`resource`) | `https://api.ryulink.xyz/ryulink` |
| API scope | `ryulink:player` |
| 控制面 API | `https://api.ryulink.xyz` |

App ID 是公开客户端标识，不是密钥。NRO 不得保存或请求 client secret。

> **部署状态（2026-08-01）**：生产控制面已部署玩家迁移 `0002_player_profiles.sql`、`/app-api/ryulink/player/me` 和 `https://api.ryulink.xyz` 反向代理。生产 issuer、Native Device Flow 应用和 `https://api.ryulink.xyz/ryulink` resource 已可用；该客户端没有后台角色关联。尚未发布独立测试/staging issuer 或 API 域名，Switch 不得把生产账号、token 或 App ID 重定向到猜测的测试域名。

## 部署验收状态（2026-08-01）

- 已验证：Discovery 返回的 `issuer` 为 `https://auth.ryulink.xyz/oidc`；`device_authorization_endpoint` 为 `https://auth.ryulink.xyz/oidc/device/auth`；`token_endpoint` 为 `https://auth.ryulink.xyz/oidc/token`。三者均是同一 host 的 HTTPS URL，且请求未发生重定向。
- 已验证：以 `client_id=oqjslw0gq1s83h65uyp7m`、`scope=openid ryulink:player` 和本文件指定 resource 发起 Device Flow，返回 HTTP 200 与设备码响应必需字段。响应中的设备码和用户码没有写入日志或验收记录。
- 已验证：未携带 Bearer token 调用 `GET https://api.ryulink.xyz/app-api/ryulink/player/me` 返回 HTTP/业务 code `401` 及 `{"code":401,"msg":"账号未登录","data":null}`，证明公开路由与认证网关已生效。
- 已验证：`auth.ryulink.xyz` 和 `api.ryulink.xyz` 均使用公有 CA（Let's Encrypt）证书；TLS 1.2、SNI、完整证书链和主机名验证均通过。
- 待完成：需要三个真实 Logto 测试账号各自完成 Device Flow 后，分别验收“启用且永久资格”、“启用且无资格”、“禁用”的 `/player/me` 脱敏响应；不得用伪造 token 或仅数据库预置行替代该验收。

## Logto 配置契约

- 创建一个 **Native / public** Logto 应用给 RyuLink Switch App；首期使用 **Device Flow**，由 Switch 展示用户代码/二维码并让用户在手机或电脑浏览器完成注册、登录与授权。不得在 NRO 内置 client secret。
- 若未来 NRO 能可靠启动浏览器并接收已登记的自定义 URI 回调，才可改用 Authorization Code + PKCE（`S256`）。
- 为 API 资源 `https://api.ryulink.xyz/ryulink` 授予 `ryulink:player` scope。
- 不向该应用授予 `ryulink:admin` scope。后台管理端继续单独使用 admin scope 和显式的管理员身份映射。
- Device Flow 不需要 NRO 回调 URI；轮询、超时和取消必须按 Logto 返回的间隔与错误码处理。
- 首期**不承诺持久化 refresh token**：Switch 关闭或用户按 B 取消登录后，丢弃本次会话并在下次启动重新走 Device Flow。因此 Switch 当前请求的 scope 不含 `offline_access`。日后如要启用 refresh token，必须先发布经评审的存储方案和轮换/撤销契约，见“本地凭据存储”。

## 当前接口

`GET /app-api/ryulink/player/me`

请求头：`Authorization: Bearer <Logto access token>`。

控制面验证签名、时间、audience `https://api.ryulink.xyz/ryulink` 和 `ryulink:player` scope。验证成功后以 `sub` 幂等创建或更新最小玩家档案，返回玩家状态和由运营后台授予的测试资格。此接口不返回 token、邮箱、密码或后台权限。

响应示例：

```json
{"code":0,"data":{"id":42,"displayName":"Player","status":0,"testAccessEnabled":false,"testAccessExpireTime":null}}
```

后台接口：`/admin-api/ryulink/player/page`、`/get`、`/access`，权限分别为 `ryulink:player:query` 与 `ryulink:player:update`。部署迁移后需将这些权限配置到相应后台角色；玩家没有也不会获得任何后台角色。

## Switch Device Flow

### 1. 申请设备码

```http
POST https://auth.ryulink.xyz/oidc/device/auth
Content-Type: application/x-www-form-urlencoded

client_id=oqjslw0gq1s83h65uyp7m
&scope=openid%20ryulink%3Aplayer
&resource=https%3A%2F%2Fapi.ryulink.xyz%2Fryulink
```

展示响应的 `user_code` 和 `verification_uri`；优先把 `verification_uri_complete` 生成二维码。用户在手机或电脑完成注册/登录和同意授权。`device_code` 仅留在内存，超过 `expires_in` 后丢弃。

```json
{
  "device_code": "...",
  "user_code": "ABCD-EFGH",
  "verification_uri": "https://auth.ryulink.xyz/...",
  "verification_uri_complete": "https://auth.ryulink.xyz/...",
  "expires_in": 600,
  "interval": 5
}
```

### 2. 轮询换取 token

从首次请求起按响应 `interval`（秒）轮询，绝不快于该间隔：

```http
POST https://auth.ryulink.xyz/oidc/token
Content-Type: application/x-www-form-urlencoded

grant_type=urn:ietf:params:oauth:grant-type:device_code
&device_code=<device_code>
&client_id=oqjslw0gq1s83h65uyp7m
```

- `authorization_pending`：继续等待。
- `slow_down`：增加轮询间隔。
- `access_denied`、`expired_token`：停止本次流程，回到登录入口。
- 成功：只在内存保存 `access_token` 和 `expires_in`，随后调用 `/player/me`。当前契约不请求、保存或使用 `refresh_token`。

### 3. 调用控制面

```http
GET https://api.ryulink.xyz/app-api/ryulink/player/me
Authorization: Bearer <access_token>
```

成功后控制面以 token 的 `sub` 幂等创建最小玩家档案。当前 Switch 应用只获 `openid` 与 `ryulink:player`，因此 `displayName` 可能为空字符串；如未来在 Logto 应用 Permissions 中额外授权 `profile`，控制面会读取 `name` 或 `username` 的最小快照：

```json
{
  "code": 0,
  "data": {
    "id": 42,
    "displayName": "Player",
    "status": 0,
    "testAccessEnabled": false,
    "testAccessExpireTime": null
  }
}
```

`status=1` 表示该玩家已被运营禁用；`testAccessEnabled=false` 说明已登录但尚未被运营授予测试资格。两者都不应在客户端绕过。`401` 表示 token 无效/过期，`403` 表示缺少 `ryulink:player` 或 resource 不匹配。

### 4. Token 生命周期与登出

Logto access token 仅用于完成 Device Flow 后兑换 RyuLink opaque session，且不会写入 SD 卡。兑换成功后，App 仅保存该 opaque session；控制面以最后一次成功认证为准，连续 90 天无活动即撤销。相同账号在另一台设备成功登录会撤销旧设备会话；旧设备的下一次联网请求返回 `401` 并清除本地会话。

`offline_access`、refresh token 与 Logto refresh-token 轮换仍未启用。用户选择退出时，App 撤销当前 RyuLink opaque session 并删除本地文件；不得持久化 Logto access token、refresh token、device code 或密码。

不要将 access token、refresh token、device code 或用户密码发送到控制面日志、分析服务或崩溃报告。

## OIDC / OAuth 响应契约

NRO 必须首先读取 discovery 文档，并使用其中的 `device_authorization_endpoint`、`token_endpoint`、`jwks_uri` 和（如未来启用）`revocation_endpoint`，而不是拼接未发布的路径。当前 Logto 版本的生产端点应分别解析为 `/oidc/device/auth` 和 `/oidc/token`；discovery 是唯一权威来源。

Discovery 文档本身及其中每个被使用的 endpoint 都必须为 HTTPS，且 URL host 必须精确等于 `auth.ryulink.xyz`。不接受跨域 host、IP 地址、非标准端口、HTTP URL 或任何重定向（包括 3xx）；任一检查失败即终止登录并提示“登录服务配置无效”。这避免错误 discovery 配置把 device code 或 token 发送到其他域名。

### `/device/auth` 成功响应

| 字段 | JSON 类型 | 可空 | 长度/格式 | 处理 |
| --- | --- | --- | --- | --- |
| `device_code` | string | 否 | Logto opaque string；服务端未发布最大长度 | 仅内存保存，不记录日志 |
| `user_code` | string | 否 | Logto opaque display code；字符集和最大长度未作平台承诺 | 原样显示，不要自行验证字符集 |
| `verification_uri` | string | 否 | HTTPS absolute URL | 原样显示 |
| `verification_uri_complete` | string | 是 | HTTPS absolute URL | 若有则生成二维码；缺失时展示 `verification_uri` + `user_code` |
| `expires_in` | integer | 否 | 正秒数 | 到期即停止轮询 |
| `interval` | integer | 是 | 正秒数 | 缺失时按 5 秒开始 |

NRO 的网络解析缓冲区必须能容纳 4 KiB 单个 JSON 字符串；任一必需字段超出此上限或不是预期 JSON 类型时，安全失败并提示“登录服务响应无效”。单个 HTTP 响应 body（成功或失败）最大为 **8 KiB**：客户端必须在读取时强制该上限，超过后立即中止读取、释放响应并报“登录服务响应过大”，不得扩容或重试。这些是客户端防御性上限，不是 Logto 的字段长度承诺。

### `/token` 成功响应

| 字段 | JSON 类型 | 可空 | 长度/格式 | 处理 |
| --- | --- | --- | --- | --- |
| `access_token` | string | 否 | API resource token；opaque 不透明对待 | 仅内存保存，作为 Bearer token |
| `token_type` | string | 否 | 必须为 `Bearer`（大小写不敏感） | 其他值失败 |
| `expires_in` | integer | 否 | 正秒数 | 权威有效期 |
| `scope` | string | 是 | 空格分隔 scopes | 若提供，必须包含 `ryulink:player` |
| `id_token` | string | 是 | OIDC JWT | 当前 `/player/me` 不使用 |
| `refresh_token` | string | 是 | 当前不应出现 | 收到后不得持久化；丢弃并记录非敏感诊断 |

Logto OAuth 失败体为 JSON：`{"error":"<machine_code>","error_description":"<human-readable text>","error_uri":"<optional URI>"}`。`error` 是必需 string；`error_description` 和 `error_uri` 是可选 string，**没有稳定最大长度或稳定文案，不能做逻辑分支或直接原样展示给用户**。NRO 将其截断到 256 UTF-8 字节后仅用于诊断日志。

Device Flow 可预期的失败 `error`：`authorization_pending`、`slow_down`、`access_denied`、`expired_token`、`invalid_client`、`invalid_grant`、`invalid_request`、`invalid_scope`、`unsupported_grant_type`、`server_error`、`temporarily_unavailable`。实现必须将未知 `error` 视为不可重试失败，而不是假定这是完整的 Logto 错误枚举。

## Device Flow 轮询与网络规则

- 初始间隔：使用 `interval`；缺失时 5 秒。
- `authorization_pending`：保持当前间隔。
- `slow_down`：**每收到一次增加 5 秒**，该新间隔作用于之后所有轮询。
- 用户按 **B**：停止计时器、清零 `device_code` 和界面；当前没有设备码取消接口，不能继续轮询。
- 连接超时、DNS 失败、TLS 失败、HTTP `429`、HTTP `500/502/503/504`：在 device-code 未到期且用户未取消时重试。等待 `max(当前 Device Flow 间隔, 5 秒)`，每次失败再翻倍，最大 30 秒；收到 `Retry-After` 时取两者较大值。
- 其他 HTTP 4xx：读取 OAuth error 后停止；响应不是 JSON 时按不可重试失败处理。

`verification_uri_complete` 不是强制字段，NRO 必须支持其缺失。Logto 未为 `user_code` 公开稳定字符集或最大长度；NRO 只显示服务端值，不能假定固定为 `ABCD-EFGH`。

## `/player/me` 服务端契约

### 成功（HTTP 200）

```json
{"code":0,"msg":"","data":{"id":42,"displayName":"Player","status":0,"testAccessEnabled":false,"testAccessExpireTime":null}}
```

| 字段 | JSON 类型 | 可空 | 最大长度/格式 |
| --- | --- | --- | --- |
| `code` | integer | 否 | `0` 表示成功 |
| `msg` | string | 否 | 成功时空字符串 |
| `data.id` | integer | 否 | 正 64 位整数 |
| `data.displayName` | string | 否 | 0–100 字符；来自 Logto `name`/`username` 的最小快照 |
| `data.status` | integer | 否 | `0` 启用；`1` 禁用 |
| `data.testAccessEnabled` | boolean | 否 | 是否获测试资格 |
| `data.testAccessExpireTime` | string | 是 | RFC 3339 UTC instant，例如 `2026-08-01T07:00:00Z`；`null` 表示永久有效或尚未设置，当前应结合 `testAccessEnabled` 判定 |

语义：`status=1` 时客户端可显示“账号已禁用”，但不能自行注销 Logto；`testAccessEnabled=false` 表示仅可登录，不能接入测试房间。`testAccessExpireTime=null` 且 `testAccessEnabled=true` 表示永久有效；`null` 且 false 表示未授予资格。

### 鉴权失败

```json
{"code":401,"msg":"账号未登录","data":null}
```

HTTP `401`/业务 `code=401`：缺少、过期、撤销或签名无效的 token；清除内存会话并重新 Device Flow。

```json
{"code":403,"msg":"没有该操作权限","data":null}
```

HTTP `403`/业务 `code=403`：token audience 不匹配，或没有 `ryulink:player` scope；停止自动重试，提示“账号未获玩家访问权限”。角色/权限回收会在下次签发 token 或 token 过期后生效；控制面不为已签发 token 提供即时玩家权限回调。

HTTP `500/502/503/504`：控制面暂时不可用；响应仍为 `{"code":<http-status>,"msg":"...","data":null}`，但 `msg` 不保证稳定。应用按网络退避规则有限重试，不应把它当作登录或权限错误。

`/player/me` 的有限重试：只对网络失败及 HTTP `500/502/503/504` 重试，最多 **3 次请求**（首次请求包含在内），总等待时间不超过 **15 秒**。建议等待 1 秒、2 秒；到达次数或总等待上限后退出加载页，显示可重试错误。HTTP `401`、`403`、任何其他 `4xx`、body 超限与 JSON 校验失败均不得自动重试。

## HTTPS、版本与本地凭据

- 仅允许 `https://auth.ryulink.xyz` 与 `https://api.ryulink.xyz`；TLS 最低版本为 1.2。NRO 必须校验证书链、有效期和主机名，**不接受任何忽略证书错误的实现**。
- 控制面当前不发布独立 CA bundle 或证书 pin。使用系统/平台信任库；证书轮换必须保留由公有 CA 签发的完整链，并在切换前至少提前 14 天通知客户端维护方。pinning 如要启用，必须另行设计双 pin 和紧急回退，当前不要自行 pin。
- Switch NRO 环境的 SD 卡文件不是受保护存储。当前威胁模型不接受在 SD 卡持久化 access/refresh token 或自行加密的 token；会话只在内存存活，退出即重新 Device Flow。
- 每个控制面请求包含 `User-Agent: RyuLink/<semver>`，来自编译期常量 `RYULINK_APP_VERSION`，不得手写维护。
- 当前 App **不执行启动版本检查**，也不调用 `GET /app-api/ryulink/app/version/latest`，没有版本更新/强制更新 UI。后续版本治理（如需）应附着在业务 API 上，另行设计。

房间分页、建房、选房和设备绑定属于独立的产品 API；它们不配置 `ldn_mitm` 的 relay profile。

## 一键上传诊断证据

RyuLink App `0.1.8` 底层保留上传接口，但当前版本已隐藏设置页的 X 触发入口：

```http
POST https://api.ryulink.xyz/app-api/ryulink/diagnostics/evidence
Authorization: Bearer <RyuLink opaque session>
User-Agent: RyuLink/0.1.8
Content-Type: multipart/form-data

evidence=@ryulink-diagnostic-evidence.txt
```

`evidence` 必须为有效 UTF-8 纯文本且不超过 1 MiB。客户端只可收集 ADR-0014 的固定日志
白名单和有界尾部，并在发送前脱敏；不得上传配置、Token、Ticket、密码、Nintendo 身份、
完整 SD 卡或游戏 payload。服务端必须二次脱敏，且不能记录请求正文或完整玩家 subject。

当前白名单为：启用日志后的 `sdmc:/ldn_mitm.log`、可选的
`sdmc:/config/ryulink/core_diagnostics.log`，以及最新 Atmosphère crash report。
`sdmc:/config/ldn_mitm/relay.cfg` 是固定 relay profile，**不得上传**。

成功：

```json
{"code":0,"msg":"","data":{"evidenceId":"0123456789abcdef0123456789abcdef","storedBytes":12345}}
```

稳定业务错误码：`41301`（空、超限或格式无效）、`42901`（上传过于频繁）、`50320`
（接收功能未启用）、`50321`（存储失败）。`401` 时 App 清除失效会话并重新登录。失败时
不得删除 SD 卡上的原始日志。

## 房间加入契约（当前 MVP）

`POST /app-api/ryulink/rooms/{roomId}/join` 只确认产品侧房间成员资格：

```json
{
  "roomId": 10001,
  "roomType": "PUBLIC",
  "roomStatus": "RUNNING"
}
```

- `roomStatus`：READY | STARTING | RUNNING | STOPPING | OFFLINE。
- 当前 MVP 中，所有房间入口均使用
  `sdmc:/config/ldn_mitm/relay.cfg` 中固定的 `ldn_mitm` relay profile，视为同一个
  虚拟 LAN；App 不接收或下发 endpoint、passphrase、ticket 或 room 隔离参数。
- App 在确认产品侧加入前，只读取 `ldn_mitm` 公开配置服务，确认 sysmodule、Internet
  Relay 和已选 Relay 均可用；它不会调用任何配置写入命令，也不会向核心传递 `roomId`。
- 多房间隔离将在未来以 `room → relay endpoint/port` 或
  `room_id → virtual network` 的独立协议实现。
