# 演示图片

这些图片使用当前版本 Codex Meter 的原生界面绘制函数离线导出，通常为 200% 缩放（native-detail-preview.png 为 100%），直接保存为无损 PNG，没有经过桌面截图的 JPEG 压缩。使用虚构账号、额度与 Token 用量，不读取真实登录信息。导出采用应用支持的实色背景，不包含系统合成的磨砂材质、外窗圆角及阴影。

| 图片 | 展示内容 |
| --- | --- |
| compact.png | 640×284，紧凑模式，多账号短周期及周额度 |
| compact-controls.png | 640×284，悬停红绿灯时的矢量符号 |
| details.png | 760×1504，多账号完整详情与七天趋势曲线 |
| account-preview.png | 760×856，单账号详情、重置卡和七天趋势曲线 |
| native-detail-preview.png | 380×442，100% 原生分辨率文字及红绿灯预览 |
| edge-dock.png | 336×180，贴顶横向收纳，默认文字头像和双额度圆环 |
| edge-dock-vertical.png | 168×336，左右贴边的纵向收纳 |

## 重新导出（Windows / MSVC）

```powershell
cmake -S . -B build/docs-render -A x64
cmake --build build/docs-render --config Release --target render_demo
.\build\docs-render\Release\render_demo.exe .\docs\images
```

`render_demo` 是显式构建的文档工具，不加入默认构建或发行包。它调用应用的 `paint`、按钮绘制与演示数据函数，以隐藏绘制目标导出，不启动查询进程或读取账号。

## 交互体验

```powershell
.\codex_meter.exe --demo --windowed --data-dir "$env:TEMP\CodexMeter-DocsDemo"
```

1. 启动后为紧凑模式；绿色按钮展开详情，黄色按钮收起。
2. 拖动顶栏至屏幕边缘进入收纳模式，或从右键菜单选择“边缘收纳模式”。
3. 单击圆环条恢复；按住拖动可沿当前边缘移动。

演示数据的重置时间相对启动时刻生成，因此重新导出时日期与倒计时会不同。演示图片中的重置卡属于演示数据，实际账号只展示官方服务返回的信息。实际窗口的磨砂效果、字体与尺寸可能随 Windows 版本、背景、显示缩放及设置而不同。
