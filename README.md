# JJAnimReviewKit

문서: [사용자 안내서](Docs/USER_GUIDE.ko.md) · [repo용 구현과 구조](Docs/ARCHITECTURE.ko.md)

UE 5.8 에디터 플러그인. AI가 만든 AnimSequence를 **이미지 없이 숫자로 검사**하고, 사람이 판단할 연기·느낌은 **컨택트 시트 한 장 + 텍스트 주석**으로 AI에 전달한다. 에셋은 수정·저장하지 않고, 결과는 `Saved/JJAnimReview/` 아래 리뷰 묶음으로 남는다.

## 설치

- 프로젝트 `Plugins/JJAnimReviewKit/`에 둔다. `EnabledByDefault: true`라 `.uproject` 편집은 필요 없다. 모듈은 `JJAnimReviewKitEditor`(Editor 타입, 커맨드렛에서도 로드) 하나다.
- 별도 Git 저장소로 관리한다. 개인용으로만 쓸 때는 소비 프로젝트의 `.git/info/exclude`에 `/Plugins/JJAnimReviewKit/`를 넣는다. 소비 프로젝트 코드는 이 플러그인에 의존하지 않는다.
- 에디터 타깃을 빌드한 뒤 사용한다.

## 사용 흐름

1. **AI 자동 검사(사용자 없음):** 커맨드렛 `-run=JJAnimReview -Asset=<AnimSequence>`를 headless로 실행 → `review.md` 확인 → 수정 → 재검사. 자세한 규칙은 `AI_USAGE.md`.
2. **사람 리뷰:** 애니메이션 에디터에서 AnimSequence를 열고 툴바 **리뷰 묶음 만들기**(검사 + 컨택트 시트) → **리뷰 주석** 탭에서
   - 현재 프레임·선택 본에 주석 추가(방향 ±X/±Y/±Z, 조금/많이),
   - 미리보기에서 본을 돌린 뒤 **포즈 지시 저장**(원본 대비 본별 차이를 주석으로 저장, 미리보기 수정은 직접 초기화),
   - 주석 클릭 → 그 프레임으로 이동, **해결/다시 열기**, 삭제,
   - **주석 표시 ±2f**: 뷰포트의 본 위치에 표시(이전 버전 주석은 노란색),
   - **이전 테이크 고스트**: 고른 이전 테이크의 스켈레톤을 자홍색 선으로 겹쳐 표시.
3. AI에게는 리뷰 묶음 폴더 경로만 넘긴다. AI는 `review.md`를 먼저 읽는다.

## 리뷰 묶음

`Saved/JJAnimReview/<에셋 키>/take_<번호>/`

| 파일 | 내용 |
|---|---|
| `review.md` | AI가 먼저 읽는 요약(6000 바이트 이하): 검사 실패·경고, 이번/이전 지적, 테이크 비교, 이미지 경로 |
| `review.json` | 같은 내용의 구조화 데이터(스키마 v1) |
| `pose.json` | 전체 프레임 포즈 샘플(테이크 비교·고스트용) |
| `sheet.png` | 컨택트 시트(기본 4각도 × 6프레임, GUI에서만) |

테이크는 포즈 내용이 바뀔 때만 새로 생기고, 열린 주석은 다음 테이크로 이월된다. 보존 기본 10개.

## 프로필

`JJAnimReviewProfile` DataAsset(콘텐츠 브라우저에서 생성)으로 스켈레톤별 발 본·접지 기준·바닥 높이·관통 검사 본·소켓 쌍·검사별 임계값과 켜기/끄기를 정한다. 없으면 본 이름으로 추정하고 "추정 프로필" 경고를 붙인다.

## 스크린샷

(비워 둠)

## 유지보수

AI 유지보수 전제, UE 5.8만 지원. 엔진 API 의존은 `Private/*Compat.cpp`에 모여 있고 가정 목록은 `PORTING.md`, 이식 절차와 코드 규칙은 `AGENTS.md`.
