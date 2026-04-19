# Pixel Escape（像素闯关）

Windows 单机横版平台跳跃小游戏，使用 **Win32 + GDI / GDI+** 绘制，**CMake + MSVC** 构建，无第三方游戏引擎依赖。

## 操作说明

- **A / D**：左右移动  
- **空格**：跳跃  
- **ESC**：设置（**Enter** 继续，**R** 重开本关，**Q** 退出）  
- 进入真门通关，进入假门失败；通关/失败后关闭对话框会重新开始当前关卡。

## 构建（Windows）

1. 安装 [CMake](https://cmake.org/download/) 与 **Visual Studio 2022**（含「使用 C++ 的桌面开发」）。
2. 在 **仅含英文路径** 的目录执行 CMake（避免链接器 PDB 等问题），例如：

```bat
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
cmake -S "你的源码目录" -B D:\Ming\2\pixel_escape_build
cmake --build D:\Ming\2\pixel_escape_build --config Release
```

3. 将源码中的 `assets\fail_taunt.png`、`assets\win_taunt.png` 会由 CMake **复制到可执行文件同目录下的 `assets\`**；若手动拷贝 exe，请一并带上 `assets` 文件夹。

## 仓库结构

```
├── CMakeLists.txt
├── assets/          # 通关/失败界面图片（PNG）
└── src/
    └── main.cpp     # 游戏与窗口逻辑
```

## 许可证

若用于课程作业，请遵循学校关于引用与开源的要求。
