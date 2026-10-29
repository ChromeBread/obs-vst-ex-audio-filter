[English](README.md) | [日本語](README.ja.md) | **한국어** | [Bahasa Indonesia](README.id.md) | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

OBS Studio용 확장 오디오 필터 창입니다. 노래 방송을 위한 즉시 ON/OFF 전환, 지연 측정, 체인 보기를 갖추고,
자체 **VST2 / VST3** 호스트 **VST Enigmax Loader**가 함께 제공됩니다. "필터 (VST Ex)" 창은 OBS 기본 "필터" 창과
같은 필터 체인을 다루므로 어느 쪽에서든 조작할 수 있습니다.

OBS 플러그인 2개가 설치됩니다: **VST Ex** (창, 도크, 내장 필터)와 **VST Enigmax Loader** (VST 호스트).
VST 필터는 VST Enigmax Loader 소속이므로, 이것을 제거하면 OBS가 그 필터를 불러올 수 없습니다.
제거하기 전에 "인계" (아래)로 OBS 기본 VST 2.x 필터나 atkAudio로 옮기세요.

- Windows x64, OBS Studio 31 이상 (33.0에서 확인)
- 제작자: BlackDream feat. ChromeBread · 라이선스: GPL-2.0

## 설치

1. [Releases](../../releases)에서 `obs-vst-ex-audio-filter-<버전>-windows-x64-setup.exe`를 다운로드합니다.
2. OBS를 종료하고 실행합니다. "전체 설치"는 모두, "사용자 지정 설치"는 다음 중에서 고를 수 있습니다:
   - VST Ex Audio Filter (필수): "필터 (VST Ex)", 피치 / 의사 스테레오 필터, 도구 > VST Ex Audio Filter
   - VST Enigmax Loader: VST2 / VST3 플러그인
   - VST Ex Chain View, VST Ex Mixer 도크, VST Ex Switcher 도크

   VST Ex의 선택 항목은 나중에 도구 > VST Ex Audio Filter에서 켜고 끌 수 있습니다 (OBS 재시작 후 적용).
3. OBS를 시작합니다. 처음 시작할 때 썸네일, 스크린샷, 기록을 저장할 곳을 묻습니다.

**설치 없는 버전**: `obs-vst-ex-audio-filter-<버전>-windows-x64-portable.zip`. OBS를 종료하고
`C:\ProgramData\obs-studio\plugins\`에 압축을 풀면 `obs-vst-ex-audio-filter` (VST Ex)와 `vst-enigmax-loader`
(VST Enigmax Loader) 두 폴더가 생깁니다. 필요 없는 폴더는 넣지 않아도 됩니다. 제거는 OBS를 종료하고 폴더를 지우면
됩니다 (VST 필터는 먼저 인계하세요. 아래 참조).

**제거** (Windows 설정 > 앱 > OBS VST Ex Audio Filter). 제거 프로그램은:
1. 장면 모음에 VST Enigmax Loader 필터가 남아 있으면 경고하여 먼저 인계할 수 있게 합니다
2. 플러그인 썸네일, 고해상도 스크린샷, 설정 / 프리셋 / 기록을 삭제할지 각각 묻습니다
   ("모두 내보낸 뒤 삭제"도 선택 가능)
3. 폴더 안에 플러그인이 만들지 않은 파일 (다른 파일, `.bak` / `.org` 같은 이름을 바꾼 복사본)이 있으면
   목록으로 보여 주고 하나씩 삭제하거나 남길 수 있습니다

## 기능

**필터 (VST Ex) 창** — 오디오 믹서에서 소스를 우클릭 > "필터 (VST Ex)"
- 큰 ON/OFF 버튼, 다중 선택, 드래그로 순서 변경, 복제 (Ctrl+D), 모두 삭제, 필터별 메모
- 실행 취소 / 다시 실행, OBS를 다시 시작해도 남는 자동 저장 기록
- 필터별 지연과 합계, 동기화 오프셋으로 보정하는 버튼
- **프리셋**: 필터 체인 전체 저장 / 적용 / 내보내기 / 가져오기
- **ON/OFF 세트** (동기화 오프셋, 볼륨 포함): 노래 중에 벌스 → 후렴을 즉시, 단축키로도 전환
- **4비트로 측정**: BPM120 테스트 곡에 맞춰 박수를 치면 (또는 이어폰을 마이크에 대면) 동기화 오프셋을 계산.
  ms 눈금이 있는 확대 가능한 파형으로 수동 미세 조정도 가능

**VST Enigmax Loader** (VST2 / VST3)
- VST2와 VST3, 별도 프로세스에서 안전하게 스캔 (직접 시작, 예상 시간 표시)
- 목록 / 썸네일 / 상세 보기 브라우저, 정렬, 검색, 즐겨찾기, HID (숨김), 메모, 최근 사용
- 편집기를 처음 열 때 썸네일 자동 촬영, 일괄 생성 가능. 필터별 스크린샷
- 편집기 창에서 플러그인 설정 내보내기 / 가져오기
- **인계** (도구 > VST Enigmax Loader > 디버그 모드): VST 필터를 설정째 그 자리에서 OBS 기본 "VST 2.x 플러그인" 필터 (VST2)나
  "atkAudio PluginHost" (VST3)와 주고받습니다.
- 썸네일과 고해상도 스크린샷은 별도 폴더에 저장

**내장 필터**
- "VST Ex 피치 / 팬": 반음 단위 피치와 팬. 장치 간 타이밍을 맞출 때 구별하기 쉬워집니다
- "VST Ex 의사 스테레오": DAW 없이 모노 마이크를 넓게 만듭니다 (하스 효과). 필터 하나로 추가 / 삭제

**도크와 도구**
- "VST Ex Switcher" 도크: ON/OFF 세트 버튼과 CPU 사용률 (OBS 전체와 VST 몫)
- "VST Ex Mixer" 도크: 오디오 믹서 미터 아래에 소스별 열, VST마다 버튼
- "VST Ex Chain View" (도구 메뉴): 장치부터 출력까지 모든 소스, 지연, 레벨, 사이드체인, 반원 이미저 (선택),
  이미지로 저장
- 도구 > VST Ex Audio Filter: 언어 (EN / JP / KR / ID / FR / TW), 버전, 선택 항목 켜기 / 끄기, 디버그 정보,
  모든 설정 내보내기 / 가져오기 (디버그 모드)

## 미리 보기

![필터 (VST Ex) 창, VST Ex Mixer 도크, VST Ex Switcher 도크](docs/images/preview-filters.jpg)
*필터 (VST Ex) 창, VST Ex Mixer 도크, VST Ex Switcher 도크*

![VST 플러그인 선택 (썸네일 보기)](docs/images/preview-browser.jpg)
*VST 플러그인 선택 (썸네일 보기)*

## 빌드

Visual Studio 2022와 CMake 3.28 이상: `cmake --preset windows-x64` 후
`cmake --build build_x64 --config RelWithDebInfo`.

VST는 Steinberg Media Technologies GmbH의 등록 상표입니다.
