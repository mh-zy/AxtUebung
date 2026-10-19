# 图标（SVG）

不同图标集的来源和许可证状态见 `docs/图标来源清单.md`。新增图标时请同时更新该清单和根目录的 `THIRD_PARTY_NOTICES.md`。

放这里的 `.svg` 会被构建一起复制到程序旁边（`<exe目录>\assets\icons\`），
代码里用 `quiz::render::loadIconTexture()` 取纹理给 ImGui 用。

## 放哪

| 位置 | 说明 |
| --- | --- |
| `assets/icons/*.svg` | 打包进程序（正式图标放这里） |
| `<exe目录>\data\icons\*.svg` | 用户自加/覆盖（目前加载器只读 assets，需要时再加） |

## 支持的 SVG 子集

* `<svg viewBox>`、`<g transform>`、`<path>`、`<rect>`、`<circle>`、`<ellipse>`、
  `<line>`、`<polyline>`、`<polygon>`
* `path` 的 `d`：`M L H V C S Q T A Z`（含小写相对写法；椭圆弧会转成折线）
* `fill` / `fill-rule` / `stroke` / `stroke-width` / `stroke-linecap` / `stroke-linejoin` /
  `opacity` / `fill-opacity` / `stroke-opacity`
* `transform`：`translate` / `scale` / `rotate` / `matrix`
* 颜色：`#rgb`、`#rrggbb`、`none`、`currentColor`（用主题色）、少量常用色名
* 不支持：渐变、图案、滤镜、蒙版、`<text>`、`<use>`、CSS `<style>`

## 写图标的建议

1. 用 24×24 的 `viewBox`（和常见图标集一致），描边宽度 1.5~2
2. 颜色统一写 `currentColor`（或用 `fill="currentColor" stroke="currentColor"`），
   这样换主题色时只要代码里传新颜色，图标会自动重新生成
3. 需要「空心」效果就 `fill="none"` + `stroke="currentColor"`；
   实心图标只用 `fill`
4. 曲线尽量用 `C`/`Q`；椭圆弧 `A` 会被转成折线（画圆优先用 `<circle>`）

## 按钮 → 图标对照

| 按钮 | 图标文件 |
| --- | --- |
| 设置 | `settings.svg` |
| 载入 / 导入 | `upload.svg` |
| 清除数据（题库卡片右侧） | `eraser.svg` |
| 星标 / 收藏（答题页，选中时换成实心版） | `star.svg` / `star-filled.svg` |
| 回首页（答题页 / 结果页 / 结算页 / 统计页左上角） | `home.svg` |
| 删除题库（题库卡片右侧，两下确认） | `trash-x.svg` |
| 标签（答题页；点开可加自己的标签） | `export-tags.svg` |
| 顺序刷题 | `layout-arrow-right.svg` |
| 乱序刷题 | `arrows-shuffle.svg` |
| 权重刷题 | `current-location.svg` |
| 错题本（题库卡片右侧，只刷该题库的错题） | `file-x.svg` |
| 导出题库 | `download.svg` |
| 导出错题 | `export-wrong.svg` |
| 导出收藏题 | `export-marked.svg` |
| 导出标签题 | `export-tags.svg` |

## 调试

```powershell
# 把图标单独渲染成图片看效果（不开窗口）
.\build\win\bin\AxtQuiz.exe --icon assets\icons\download.svg out.ppm --size 96x96 --color 96DCB4
```
