# RyuLink App「我的」页接口文档

> 适用对象：Switch RyuLink App 的“我的资料 / MY PROFILE”页；以当前客户端实现为准。

## 概览

“我的”页由三个业务接口组成：读取玩家资料、读取节点列表、保存首选节点；退出登录另调用一个会话注销接口。所有接口使用当前登录态的 Bearer token，基础地址为：

```text
https://api.ryulink.xyz/app-api/ryulink
```

公共请求头：

```http
Authorization: Bearer <登录态 access token 或 RyuLink opaque session>
User-Agent: RyuLink/<app-version>
```

成功响应采用统一包装：

```json
{"code":0,"msg":"","data":{}}
```

客户端只把 `code = 0` 当作业务成功。`401` 会清除本地会话并要求重新扫码；`403` 提示账号没有玩家访问权限。`/player/me` 对网络错误和 5xx 最多尝试 3 次（首次也计入）；节点接口当前不做客户端自动重试。

## 1. 读取玩家资料

```http
GET /player/me
```

完整 URL：

```text
https://api.ryulink.xyz/app-api/ryulink/player/me
```

### 成功响应

```json
{
  "code": 0,
  "msg": "",
  "data": {
    "id": 42,
    "displayName": "Player",
    "playerCode": "RL-000001",
    "status": 0,
    "testAccessEnabled": true,
    "testAccessExpireTime": null,
    "vipActive": true,
    "myPrivateRooms": [
      { "name": "My Private Server" }
    ]
  }
}
```

| 字段 | 类型 | 客户端要求 | “我的”页展示 |
| --- | --- | --- | --- |
| `data.id` | integer | 服务端玩家主键；当前 UI 不展示。 | 不展示 |
| `data.displayName` | string | **必填**，最长 100 字符。 | 顶部玩家名；空字符串时显示“玩家”。 |
| `data.playerCode` | string | 可选，最长 32 字符。 | “龙联 ID”；缺失时显示 `-`。 |
| `data.status` | integer | **必填**；`0` 正常，`1` 已禁用。 | 已禁用时显示“账号已禁用”。 |
| `data.testAccessEnabled` | boolean | **必填**。 | `true` 显示“已启用”，否则“未授予”。 |
| `data.testAccessExpireTime` | string / null | 可选 RFC 3339 UTC 时间。当前 UI 不展示，供后续显示到期日或业务校验使用。 | 不展示 |
| `data.vipActive` | boolean | 可选；缺失按 `false`。 | “VIP 已激活”或“无 VIP”。 |
| `data.myPrivateRooms` | array | 可选；客户端只读取第一个对象的 `name`。 | 显示“私服”名称。 |
| `data.myVipRoom` | object | 历史兼容字段，仅在 `myPrivateRooms` 为空或无效时读取其中的 `name`。 | 同上 |

### 状态语义

- `status = 1`：账号被运营禁用。客户端可提示，但不得以 UI 逻辑绕过服务端限制。
- `testAccessEnabled = false`：用户能登录，但没有测试区准入资格。
- `vipActive = false`：用户仍可使用非 VIP 功能；页面引导到 `account.ryulink.xyz` 开通会员。
- 私服仅为展示信息；玩家能否加入仍由房间接口在服务端决定。

## 2. 获取可选节点与当前首选项

```http
GET /nodes
```

完整 URL：

```text
https://api.ryulink.xyz/app-api/ryulink/nodes
```

### 成功响应

```json
{
  "code": 0,
  "msg": "",
  "data": [
    {
      "id": "jp-tokyo-1",
      "name": "Tokyo",
      "available": true,
      "isPreferred": true
    }
  ]
}
```

| 字段 | 类型 | 要求 |
| --- | --- | --- |
| `id` | string | **必填**，稳定节点标识；保存首选项时原样回传。 |
| `name` | string | **必填**，最长 100 字符；用于 UI 展示。 |
| `available` | boolean | **必填**，当前节点是否可服务。 |
| `isPreferred` | boolean | **必填**，当前玩家是否已将该节点设为首选。 |

客户端最多读取前 8 个节点，并优先选中 `isPreferred = true` 的第一项。接口应只返回玩家可见的节点；若数组为空，客户端显示“没有可用节点”。

## 3. 保存首选节点

```http
PUT /nodes/preferred
Content-Type: application/json

{
  "nodeId": "jp-tokyo-1"
}
```

完整 URL：

```text
https://api.ryulink.xyz/app-api/ryulink/nodes/preferred
```

### 成功响应

```json
{"code":0,"msg":"","data":null}
```

请求的 `nodeId` 必须来自 `GET /nodes` 的 `id`，不得信任用户自由输入。服务端应验证节点存在、可用且该玩家有权选择；否则返回业务错误而不是静默成功。当前客户端保存成功后保持当前选中项；下次进入大厅会重新获取节点列表和服务端首选项。

## 4. 退出登录

```http
POST /session/logout
```

完整 URL：

```text
https://api.ryulink.xyz/app-api/ryulink/session/logout
```

客户端以当前 Bearer session 发起注销，并且无论网络请求是否成功，都会删除本机的 `sdmc:/switch/RyuLink/session.dat`、清空内存登录态，回到登录页。因此服务端接口应设计为幂等：已经撤销或不存在的 session 也不应影响客户端完成本地退出。

## 错误响应

```json
{"code":401,"msg":"账号未登录","data":null}
```

| HTTP / 业务状态 | 客户端行为 | 服务端建议 |
| --- | --- | --- |
| 200 / `code=0` | 更新页面或完成操作。 | 返回契约内 JSON。 |
| 401 | 清除本地持久会话，回到二维码登录。 | 用于过期、撤销、另一设备登录等无效登录态。 |
| 403 | 停止当前操作并提示无玩家权限。 | 用于缺少 `ryulink:player` scope 或资源不匹配。 |
| 400、404、422 | 显示可重试错误，不做自动重试。 | `msg` 仅供展示，不作为客户端逻辑分支。 |
| 429、500、502、503、504 或网络失败 | `/player/me` 有限重试后显示“控制平台暂时不可用”；节点接口当前直接返回错误。 | 保持统一错误包装；429/503 建议返回 `Retry-After`。 |

## 服务端实现约束

1. 所有接口都必须验证 Bearer token / opaque session 的有效性、玩家绑定关系和撤销状态，不能相信客户端传入的会员、测试权限或节点状态。
2. `GET /player/me` 不返回密码、邮箱、OIDC token、后台角色或管理员权限。
3. `displayName`、`status`、`testAccessEnabled` 是 Switch 当前客户端的必需字段；缺失或类型不符会导致客户端判定资料响应无效。
4. `GET /nodes` 中每个节点的 `id`、`name`、`available`、`isPreferred` 都是当前客户端的必需字段。
5. 所有响应通过 HTTPS 返回，客户端响应体上限为 8 KiB；避免返回不受限的大字段或调试内容。

## 与 App 的字段映射

```text
/player/me ──► displayName / playerCode / status / testAccessEnabled
          └──► vipActive / myPrivateRooms[0].name
                         │
                         ▼
                    “我的资料”页面
                         ▲
GET /nodes ──────────────┤
PUT /nodes/preferred ────┘
POST /session/logout ───► 清理登录态并回到登录页
```

源码对应：资料解析在 `source/auth.c` 的 `player_me()`；节点接口在 `ryuLinkApiListNodes()` 与 `ryuLinkApiSetPreferredNode()`；页面字段展示在 `source/app.c` 的 `draw_profile()`。
