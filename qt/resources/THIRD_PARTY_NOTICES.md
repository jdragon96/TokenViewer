# 오픈소스 고지

TokenViewer 는 MIT 라이선스입니다. Windows 배포본에는 아래 오픈소스 소프트웨어가 함께 들어 있습니다.
라이선스 전문은 설치 폴더의 `licenses` 폴더에 있습니다.

| 파일 | 내용 |
| --- | --- |
| `licenses\LGPL-3.0.txt` | GNU Lesser General Public License v3 |
| `licenses\GPL-3.0.txt` | GNU General Public License v3 (LGPLv3 가 바탕으로 삼는 라이선스) |
| `licenses\GCC-exception-3.1.txt` | GCC Runtime Library Exception 3.1 |
| `licenses\winpthreads-COPYING.txt` | MinGW-w64 winpthreads 라이선스 |
| `licenses\TokenViewer-LICENSE.txt` | TokenViewer 의 MIT 라이선스 |

## Qt 5.15.2

- 사용 모듈: Qt Core, Qt Gui, Qt Widgets, Qt Network
- 파일: `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`, `Qt5Network.dll`, `platforms\qwindows.dll`
- 라이선스: GNU Lesser General Public License v3 (LGPLv3)
- 저작권: The Qt Company Ltd. and other contributors
- Qt 는 고치지 않고 그대로 씁니다. 정적 링크 없이 DLL 로 동적 링크합니다.
- DLL 바꾸기: 설치 폴더의 Qt DLL 을 호환되는 Qt 5.15 (MinGW 8.1 64비트) 빌드의 같은 이름 DLL 로 바꿔 넣으면 TokenViewer 가 그 DLL 을 씁니다. 직접 고쳐 빌드한 Qt 도 됩니다.
- 소스: https://download.qt.io/archive/qt/5.15/5.15.2/single/
- Qt 안에 든 제3자 소프트웨어(zlib, libpng, PCRE2, HarfBuzz, FreeType, double-conversion 등)의 라이선스: https://doc.qt.io/qt-5/licenses-used-in-qt.html
- Portions of this software are copyright © The FreeType Project (www.freetype.org). All rights reserved.

## MinGW-w64 런타임 (GCC 8.1.0)

TokenViewer 는 MinGW-w64 GCC 8.1.0 (MinGW-W64-builds 4.3.5, x86_64-posix-seh) 으로 빌드했고, 그 런타임 DLL 을 함께 넣습니다.

- `libstdc++-6.dll`, `libgcc_s_seh-1.dll`
  - 라이선스: GNU General Public License v3 + GCC Runtime Library Exception 3.1
  - 저작권: Free Software Foundation, Inc.
  - GCC Runtime Library Exception 에 따라, 이 런타임과 함께 컴파일한 프로그램(TokenViewer)은 GPL 을 따르지 않아도 됩니다.
  - 소스: https://gcc.gnu.org/ (GCC 8.1.0), https://sourceforge.net/projects/mingw-w64/
- `libwinpthread-1.dll`
  - 라이선스: MIT 계열 (일부는 BSD 3조항)
  - 저작권: mingw-w64 project, Lockless Inc.
  - 소스: https://sourceforge.net/projects/mingw-w64/

HTTPS 는 Windows 에 들어 있는 WinHTTP 로 하므로 OpenSSL 은 넣지 않습니다.

## 설치 프로그램 (Inno Setup)

설치 프로그램 `TokenViewer-windows-setup.exe` 와 설치 폴더의 제거 프로그램 `unins000.exe` 는 Inno Setup 6 으로 만들었습니다.

- 라이선스: Inno Setup License (https://jrsoftware.org/files/is/license.txt)
- 저작권: Copyright (C) 1997-2026 Jordan Russell. Portions Copyright (C) 2000-2026 Martijn Laan. All rights reserved.
- 소스: https://github.com/jrsoftware/issrc
