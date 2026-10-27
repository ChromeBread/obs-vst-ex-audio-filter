[English](README.md) | [日本語](README.ja.md) | **한국어** | [Bahasa Indonesia](README.id.md) | [Français](README.fr.md) | [繁體中文](README.zh-TW.md)

# OBS VST Ex Audio Filter

OBS Studio용 확장 오디오 필터 창입니다. **VST2 / VST3** 호스팅, 노래 방송을 위한 즉시 ON/OFF 전환, 지연 측정 도구와
체인 보기를 제공합니다. OBS 기본 "필터" 창과 같은 필터 체인을 다루므로 두 창을 함께 써도 호환됩니다.

- Windows x64, OBS Studio 31 이상 (33.0에서 확인)
- 제작자: BlackDream feat. ChromeBread · 라이선스: GPL-2.0

## 설치

1. [Releases](../../releases)에서 `obs-vst-ex-audio-filter-1.0.0-windows-x64.zip`을 내려받습니다.
2. OBS를 종료하고 zip을 `C:\ProgramData\obs-studio\plugins\`에 풉니다
   (`C:\ProgramData\obs-studio\plugins\obs-vst-ex-audio-filter\bin\64bit\...`가 됩니다).
3. OBS를 시작합니다. 처음 시작할 때 썸네일, 스크린샷, 기록의 저장 위치를 묻습니다.

제거: OBS를 종료하고 그 `obs-vst-ex-audio-filter` 폴더를 삭제합니다
(도구 > VST Ex Audio Filter > 디버그 모드에 삭제할 폴더가 모두 표시됩니다).

## 기능

**필터 (VST Ex) 창** — 오디오 믹서에서 소스를 우클릭 > "필터 (VST Ex)"
- 큰 ON/OFF 버튼, 다중 선택, 드래그로 순서 변경, 복제 (Ctrl+D), 모두 삭제, 필터별 메모
- 실행 취소 / 다시 실행, OBS를 다시 시작해도 남는 자동 저장 기록
- 필터별 지연과 합계, 동기화 오프셋으로 보정하는 버튼
- **프리셋**: 필터 체인 전체 저장 / 적용 / 내보내기 / 가져오기
- **ON/OFF 세트** (동기화 오프셋, 볼륨 포함): 노래 중에 벌스 → 후렴을 즉시, 단축키로도 전환
- **4비트로 측정**: BPM120 테스트 곡에 맞춰 박수를 치면 (또는 이어폰을 마이크에 대면) 동기화 오프셋을 계산.
  ms 눈금이 있는 확대 가능한 파형으로 수동 미세 조정도 가능

**VST 플러그인**
- VST2와 VST3, 별도 프로세스에서 안전하게 스캔 (직접 시작, 예상 시간 표시)
- 목록 / 썸네일 / 상세 보기 브라우저, 정렬, 검색, 즐겨찾기, HID (숨김), 메모, 최근 사용
- 편집기를 처음 열 때 썸네일 자동 촬영, 일괄 생성 가능. 필터별 스크린샷
- 편집기 창에서 플러그인 설정 내보내기 / 가져오기

**내장 필터**
- "VST Ex 피치 / 팬": 반음 단위 피치와 팬. 장치 간 타이밍을 맞출 때 구별하기 쉬워집니다
- "VST Ex 의사 스테레오": DAW 없이 모노 마이크를 넓게 만듭니다 (하스 효과). 필터 하나로 추가 / 삭제

**도크와 도구**
- "VST Ex" 도크: ON/OFF 세트 버튼과 CPU 사용률 (OBS 전체와 VST 몫)
- "VST Ex Mixer" 도크: 오디오 믹서 미터 아래에 소스별 열, VST마다 버튼
- "VST Ex Chain View" (도구 메뉴): 장치부터 출력까지 모든 소스, 지연, 레벨, 사이드체인, 반원 이미저 (선택),
  이미지로 저장
- 도구 > VST Ex Audio Filter: 언어 (EN / JP / KR / ID / FR / TW), 버전, 디버그 정보

<!-- 스크린샷: 여기에 한국어 UI의 작은 이미지를 추가 (예: docs/ko/*.png). -->

## 빌드

Visual Studio 2022와 CMake 3.28 이상: `cmake --preset windows-x64` 후
`cmake --build build_x64 --config RelWithDebInfo`.
