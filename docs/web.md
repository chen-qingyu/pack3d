# Web 工作台使用与部署

`web/` 是 pack3d 的浏览器工作台。它通过 HTTP API 管理实例和运行，并在浏览器中查看三维装箱结果。

本文面向使用者和部署人员，包含：

- 本地开发环境启动
- 生产环境构建与部署
- 页面操作流程
- 输入 JSON 的准备要求
- 常见问题排查

## 1. 工作方式

Web 工作台由两个服务组成：

| 服务         | 默认地址                | 作用                                             |
| ------------ | ----------------------- | ------------------------------------------------ |
| FastAPI 后端 | `http://127.0.0.1:8000` | 实例、运行、输入和结果 API，并调用 pack3d 求解器 |
| Vite 前端    | `http://127.0.0.1:5173` | 开发时提供网页和 `/api` 代理                     |

生产环境通常由一个 Web 服务器托管 `web/dist`，并把 `/api/` 转发到 FastAPI。浏览器只需要访问 Web 服务器的地址。

后端会在仓库根目录创建 `instances/`，保存实例数据库、每次运行的输入和输出结果。不要删除或移动这个目录，否则历史实例和运行结果将无法读取。

## 2. 环境要求

### 2.1 必需软件

- Python 3.9+
- Node.js
- npm

### 2.2 安装依赖

在仓库根目录执行：

```bash
python -m build python
pip install ./python/dist/xxx.whl
pip install fastapi uvicorn
cd web
npm install
```

## 3. 本地启动

本地开发需要分别启动后端和前端。两个命令都应从仓库根目录对应位置执行。

### 3.1 启动后端

在仓库根目录打开第一个终端：

```bash
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```

看到 `Application startup complete` 后，后端已经启动。可访问以下地址检查：

```text
http://127.0.0.1:8000/api/instances
```

### 3.2 启动前端

在第二个终端进入 `web/`：

```bash
cd web
npm run dev
```

打开终端显示的地址，通常是：

```text
http://127.0.0.1:5173
```

开发服务器会把浏览器发往 `/api` 的请求代理到 `http://127.0.0.1:8000`。因此浏览器访问前端地址即可，不需要为 API 单独配置 CORS。

### 3.3 停止服务

在运行服务的终端按 `Ctrl+C`。后端停止后，正在运行的求解任务会被终止；已完成的结果仍保存在 `instances/` 中。

## 4. 生产部署

生产部署分为两部分：启动 FastAPI，构建并托管前端静态文件。

### 4.1 启动 FastAPI

在仓库根目录启动后端。生产环境建议只监听本机，由 Nginx 或其他网关对外提供访问：

```bash
python -m uvicorn server.main:app --host 127.0.0.1 --port 8000
```

需要长期运行时，请使用 systemd、Windows 服务、Docker 或其他进程管理器托管该进程。不要把工作目录改到仓库之外，因为后端使用相对路径访问 `instances/`。

### 4.2 构建前端

在 `web/` 目录执行：

```bash
npm install
npm run build
```

构建成功后，静态文件位于：

```text
web/dist/
```

将 `web/dist/` 部署到 Web 服务器的站点目录。不要把开发服务器 `npm run dev` 用作生产服务。

### 4.3 使用 Nginx 同源部署

下面的配置展示了最小部署方式。请将 `root` 改成实际的绝对路径，并将 `server_name` 改成域名或服务器地址：

```nginx
server {
    listen 80;
    server_name pack3d.example.com;

    root /opt/pack3d/web/dist;
    index index.html;

    location /api/ {
        proxy_pass http://127.0.0.1:8000;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
    }

    location / {
        try_files $uri $uri/ /index.html;
    }
}
```

部署完成后，浏览器访问：

```text
http://pack3d.example.com
```

前端请求使用相对路径 `/api/...`，因此前端和 API 必须由同一个站点提供，或者由网关将 `/api/` 正确转发到 FastAPI。若前端和后端使用不同域名，需要额外配置 CORS 和前端 API 地址；当前版本没有提供这个配置项。

### 4.4 不使用 Nginx 的预览方式

`vite preview` 只适合检查生产构建，不建议作为正式服务：

```bash
cd web
npm run preview -- --host 127.0.0.1 --port 4173
```

该命令只托管前端静态文件。生产环境仍需要单独运行 FastAPI，并确保 Web 服务器能把 `/api/` 转发到后端。

## 5. 页面使用

### 5.1 创建实例

1. 打开 Web 工作台。
2. 在左侧输入实例名称，点击加号或按 Enter。
3. 点击实例进入实例工作台。

实例用于组织多次运行。同一个实例可以保存多次输入、运行状态和结果。

### 5.2 创建运行

1. 在实例工作台点击“新建运行”。
2. 输入运行名称和随机种子。
3. 在“输入 JSON”区域粘贴 JSON，或导入 `.json` 文件。
4. 点击“格式化”检查并整理 JSON。
5. 点击“开始运行”。

提交前，页面会检查 JSON 语法。输入的结构和业务规则由引擎在校验。

### 5.3 查看运行状态

运行提交后会自动进入运行详情页：

- `运行中`：求解器仍在计算，可点击“取消运行”。
- `已完成`：可以查看统计、容器明细、三维方案和原始结果 JSON。
- `输入无效`：查看违规信息并返回修改输入。
- `失败` 或 `已取消`：查看状态信息，可使用原输入重新创建运行。

页面会自动轮询运行状态。切换到其他标签页后轮询频率会降低，返回页面时会立即刷新。

### 5.4 查看三维结果

结果页的三维视图支持：

- 查看全部容器，或从下拉框选择单个容器
- 默认、前、后、左、右、俯视和仰视视角
- 按箱型、站点或分组着色
- 鼠标悬浮查看箱子 ID、尺寸、位置、朝向等信息

全部容器模式会沿水平方向排列容器。容器外框和箱子边框始终显示。

### 5.5 管理历史运行

在实例工作台的“运行历史”中可以：

- 点击运行打开详情
- 重命名实例或运行
- 删除运行
- 删除整个实例

删除实例会同时删除它下面的所有运行、输入和输出文件。该操作不可恢复。

### 5.6 下载结果

在已完成或输入无效的运行详情中，可以打开原始输出 JSON，也可以下载结果文件。下载内容与后端保存的 `output.json` 一致。

## 6. 输入 JSON

输入必须包含以下三个顶层字段：

```json
{
  "container_types": [],
  "box_types": [],
  "boxes": []
}
```

常用字段如下：

- `container_types`：容器类型、内部尺寸、最大重量和数量限制。
- `box_types`：箱子类型、原始尺寸和允许朝向。
- `boxes`：箱子实例、箱型引用、重量、站点和分组。
- `algorithm`：`gep`、`glc`、`rgs` 或 `bsg`，默认使用 `gep`。
- `constraints.time_limit`：求解时间限制，默认 120 秒。
- `constraints.support_rate`：底面支撑率，范围为 0 到 1，默认 0。
- `route`：站点装载顺序。

完整字段、朝向枚举和约束说明见 [input.md](input.md)。可以直接使用仓库中的 [demo.json](../data/demo.json) 作为测试输入。

## 7. 结果说明

结果页中的平均体积利用率、容器数、已装箱数、未装箱数、站点拆分和分组拆分都来自后端结果。三维视图使用输出中的 `placements.x/y/z` 和 `placements.dx/dy/dz`，不会在浏览器中重新推导箱子朝向。

原始输出字段和状态说明见 [output.md](output.md)。HTTP API 字段和接口说明见 [api.md](api.md)。

## 8. 常见问题

### 页面显示 API 离线或无法连接

确认后端仍在运行，并检查：

```text
http://127.0.0.1:8000/api/instances
```

本地开发时确认后端端口是 `8000`。如果修改了端口，需要同步修改 [web/vite.config.ts](../web/vite.config.ts) 中的代理地址。

生产环境确认网关的 `/api/` location 已转发到 FastAPI，而不是只托管了 `web/dist`。

### 页面能打开，但刷新子路径后出现 404

Web 服务器需要把未知前端路径回退到 `web/dist/index.html`。Nginx 使用本文配置中的：

```nginx
try_files $uri $uri/ /index.html;
```

## 9. 开发检查命令

前端代码修改后，在 `web/` 目录执行：

```bash
npm run lint
npm run build
```

`npm run build` 会先执行 TypeScript 构建检查，再生成 `web/dist`。上述命令不启动后端，也不会修改实例数据。
