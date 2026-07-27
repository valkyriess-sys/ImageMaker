# ImageMaker 개발 프로세스 워크플로우

## 날짜: 2026-07-27
## 작성: Architect (시안)

---

## 역할 분담

| 프로파일 | 역할 |
|----------|------|
| architect (시안) | 기획(spec), 코드 검수, commit/push 결정권 |
| coder (시엘) | 구현, commit/push 실행 |
| creator (시아) | UI/UX 리뷰 (미사용) |

---

## 칸반 보드
- board: `sketch-sculpt` (ImageMaker 전용, Kiwoom과 분리)
- gateway dispatcher 자동 디스패치 (60초 틱)
- coder 모델: `deepseek-v4-pro` (2026-07-27 변경, 이전 poolside 503 문제)

---

## 검수 → 커밋 결정 원칙 (Architect 권한)

1. **PASS** → coder에게 `commit + push` 지시 (카드 코멘트: `## VERIFIED: commit+push 요청`)
2. **FAIL** → coder에게 수정 요청 + 카드 반려 (status: blocked/in_progress 유지)
3. **Architect는 코드 직접 수정/커밋 절대 안 함** (검수자 경계)

---

## 카드 라이프사이클

1. Architect가 M-N 카드 생성 (spec 기반, 상세 코멘트)
2. gateway가 coder에게 자동 디스패치
3. coder 구현 → `kanban complete`
4. Architect가 코드 읽어 검수 (빌드/실행 증거 확인)
5. PASS → commit+push 지시 / FAIL → 수정 요청+반려

---

## 검수 기준
- 실제 빌드 성공 (CMake)
- 실행 증거 (헤드리스 환경선 코드 검증으로 대체)
- spec 요구사항 충족
- 가짜 데이터/하드코딩 없음

---

## Git
- repo: git@github.com:valkyriess-sys/ImageMaker.git
- SSH key: /home/valkyrie/.ssh/id_ed25519_imagemaker
- branch: master
- Architect는 git 직접 조작 안 함 (coder가 수행)
