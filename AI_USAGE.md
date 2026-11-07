# AI 애니메이션 리뷰 사용 안내

애니메이션 생성·수정을 맡은 AI(Claude/Codex)가 따르는 규칙이다. 리뷰 도구는 에셋을 수정·저장하지 않는다. 결과는 `Saved/JJAnimReview/` 아래 리뷰 묶음뿐이다.

## 수정 루프

1. 결과를 **AnimSequence**로 준비한다. Control Rig·Sequencer로 작업했다면 먼저 AnimSequence로 내보낸 뒤 리뷰한다(Level Sequence 리뷰는 아직 없음).
2. 커맨드렛으로 검사한다(사용자 없이 반복 가능, 렌더 불필요):

   ```text
   UnrealEditor-Cmd.exe "<Project>.uproject" -run=JJAnimReview -Asset=/Game/Anims/Walk.Walk -nullrhi -unattended -nosplash [-Profile=/Game/Review/WalkProfile.WalkProfile] [-Output=SessionA]
   ```

   대괄호는 선택 인자 표기다. `-Output`은 `Saved/JJAnimReview/` 아래 하위 폴더만 허용한다. 종료 코드: **0** = 실행 완료·실패 없음, **1** = 실행 완료·검사 실패 있음, **2** = 실행 오류(경로·프로필·저장소 문제). 파일을 열기 전에 종료 코드로 결과를 안다.
3. `review.md`를 먼저 읽는다. 순서: 추정 프로필 경고 → 실패 → 경고 → 이번 지적 → 이전 지적(미해결) → 테이크 비교 → 이미지 경로 → 생략 수.
4. 수치·주석 전체가 필요할 때만 `review.json`, 포즈 원본이 필요할 때만 `pose.json`(크다, 수 MB)을 읽는다.
5. 승인된 작성 도구로 수정하고 같은 명령으로 다시 검사한다. 실패가 사라지고 사용자 주석이 모두 해결될 때까지 반복한다.

## 검사 7종 (기본 임계값, 프로필로 변경)

| id | 내용 | 기본 |
|---|---|---|
| `foot_sliding` | 바닥 기준 높이 차이 3cm 이하·수직 속도 10cm/s 이하인 접지 발의 수평 이동 | 2 cm |
| `penetration` | 관통 검사 본이 바닥 − 허용치보다 낮음 | 1 cm |
| `pop` | 본 각도 변화의 급변 | 45 deg |
| `joint_limits` | PhysicsAsset 제약 각도 초과(있을 때만, swing/twist 근사) | 5 deg 여유 |
| `root_discontinuity` | 프레임 간 루트 이동 급변 | 10 cm |
| `loop_mismatch` | 루프 에셋의 처음·끝 포즈 차이 | 2 cm |
| `socket_drift` | 프로필에 지정한 소켓·본 쌍의 거리 변화 | 2 cm |

- 프로필이 없으면 본 이름(foot/ball, hand/palm)으로 추정하고 맨 위에 "추정 프로필" 경고가 붙는다. 추정 결과의 실패는 **오탐일 수 있다**. 같은 실패가 반복되면 사용자에게 `JJAnimReviewProfile` DataAsset(스켈레톤, 발 본, 바닥 높이, 소켓 쌍, 임계값)을 만들어 달라고 요청한다. 프로필을 직접 만들거나 저장하지 않는다.
- `joint_limits`는 PhysicsAsset 제약을 swing/twist로 근사하며 Free·Locked 축은 제외한다. 추정 프로필에서는 초과 결과가 warning이고 명시 프로필에서만 failed다. 메시지의 axis·angle·limit·motion으로 판단하며 경계값 근처는 시트나 사용자 판단으로 확인한다.
- 추정 프로필의 접지는 발 본별 클립 최저 Z를 바닥으로 사용하고 수평 속도 상한 `EstimatedContactMaxHorizontalSpeedCmPerSecond`(기본 50cm/s)으로 빠른 스윙을 제외한다. 수평 속도를 10cm/s 이하로 제한하지 않으므로 15cm/s 미끄러짐도 접지로 검출할 수 있다. 명시 프로필의 바닥·접지 동작은 유지한다.
- 검사는 앞 10000 샘플까지만 계산한다(경고로 표시). 내용 식별과 루프 끝점은 전체 포즈를 쓴다.

## 테이크와 주석 규칙

- 테이크 = 애니메이션 내용 버전(`pose_md5_v2` 포즈 해시). 내용이 같으면 최신 테이크를 갱신하고, 바뀌면 새 테이크(`take_000002` …)를 만든다. 수정 없이 반복 검사해도 테이크가 늘지 않는다.
- 사용자 주석은 프레임·본·방향(±X/±Y/±Z, little/much)·텍스트, 포즈 지시(본별 로컬 회전·이동 차이)를 가진다. 새 테이크가 생기면 열린 주석이 "이전 버전에서 작성" 표시로 넘어온다. `review.md`의 "이전 지적(미해결)"은 아직 사용자가 해결 표시하지 않은 것이다. 해결 표시는 사용자만 한다(에디터 주석 탭).
- 보존 기본 10개. 최신 테이크와, 최신 테이크에 열린 주석의 작성 테이크는 지우지 않는다.
- 이전 테이크가 있으면 `review.md`에 본별 최대 차이 상위 10개와 차이가 큰 프레임 구간이 붙는다(0.1cm / 0.5deg 미만 무시). 이전 테이크가 v1 해시면 비교를 건너뛴다(두 번 실행해 v2 테이크 두 개를 만든 뒤 비교된다).
- U-R1R4-4=A: 테이크 비교는 정보용 warning이며 `CheckStatus`와 종료 코드에 반영하지 않는다. 비교 구간은 본당 3개·전체 50개까지 기록하고 초과 구간 수를 경고로 요약한다. 이전 포즈를 읽지 못하면 `Comparison skipped: <이유>`를 남기고 새 리뷰를 저장한다. 이전 테이크는 비교 때문에 수정하지 않는다. 고스트 선택 목록에서는 v1을 제외한다.
- 해결된 이월 주석을 수정해도 해결 상태는 유지한다. 포즈 지시는 부모 공간의 `parent-space delta`이며 루트는 컴포넌트 공간을 쓴다.
- `.transaction` 파일이 있는 테이크는 읽지 않는다. 같은 명령을 다시 실행하면 저장소가 복구한다. 손상 오류가 반복되면 오류가 가리키는 테이크 폴더를 원본 보존한 채 에셋 폴더 밖으로 옮기고 다시 실행한다.

## 토큰 절약 규칙

- `review.md`(최대 6000 UTF-8 바이트)만으로 판단할 수 있으면 다른 파일을 열지 않는다.
- 검사별 실패·경고 줄은 합쳐서 5줄까지 표시한다. 예산을 넘으면 이후 항목은 문자열 생성 없이 생략 수만 세며 전체 결과는 JSON에 남긴다. 통과 합계 옆에 skipped 검사 이름·개수를 표시한다.
- 이미지(`sheet.png`)는 **사용자가 연기·느낌 판단을 요청했을 때만** 연다. 기계적 오류는 숫자로 판단한다.
- 컨택트 시트는 GUI 에디터에서만 만들어진다. 커맨드렛에서 `-Sheet`를 주면 시트를 건너뛰고 이유가 경고에 남는다. 시트가 필요하면 사용자에게 에디터 툴바 "리뷰 묶음 만들기"를 눌러 달라고 요청한다.
- 시트의 정면은 메시 +X를 가정한다. +Y 정면 메시에는 `ForwardYawOffset=90`을 지정한다(기본 0). `YawDegrees`는 그 정면 기준 각도이며 마지막 각도는 항상 elevated다. 각도가 하나면 그 한 장도 elevated다.

## 다른 진입점

- 에디터 콘솔: `JJAnimReview.Create <AnimSequence> [Profile or -] [Output]`(검사만), `JJAnimReview.Sheet <AnimSequence> [Profile or -] [Output]`(검사 + 시트).
- 에디터 Python(게임 스레드): `unreal.JJAnimReviewLibrary.create_review(asset, profile, output)`, `create_review_with_sheet(asset, profile, output, settings)` → (종료 코드, 테이크 폴더, 오류).
- 애니메이션 에디터 툴바: "리뷰 묶음 만들기"(검사 + 시트), "리뷰 주석"(주석 탭).

주석 탭과 고스트는 기본 출력 폴더(`-Output` 없음)의 리뷰 묶음만 읽는다. 유지보수는 `AGENTS.md`, 엔진 가정은 `PORTING.md`를 따른다.
