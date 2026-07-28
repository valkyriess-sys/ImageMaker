# M9 Image Collection Pipeline

표정 레퍼런스 이미지 수집 파이프라인 — 웹 크롤링(B) + 로컬 폴더(A) 병행.

## Quick Start

```bash
# 로컬 폴더 스캔
python tools/image_collector/run.py scan /path/to/images

# 웹 크롤링 (Wikimedia Commons CC 라이선스)
python tools/image_collector/run.py crawl --max 10

# 둘 다 실행
python tools/image_collector/run.py all /path/to/images --crawl

# DB 통계 확인
python tools/image_collector/run.py stats
```

## Options

| 옵션 | 설명 |
|------|------|
| `--db PATH` | SQLite DB 경로 (기본: `./image_collection.db`) |
| `--copy-to DIR` | 로컬 이미지를 표정별로 정리된 레퍼런스 라이브러리로 복사 |
| `--dir DIR` | 웹 크롤링 다운로드 디렉토리 |
| `--max N` | 표정당 최대 이미지 수 (기본: 10) |

## Structure

```
tools/image_collector/
├── __init__.py      # Package init
├── db.py            # SQLite 메타데이터 DB (스키마 + CRUD)
├── scanner.py       # 로컬 폴더 스캐너 (A)
├── crawler.py       # 웹 크롤러 — Wikimedia Commons (B)
├── cli.py           # CLI 인터페이스
└── run.py           # Entry-point runner
```

## Database Schema

- `collections` — 수집 실행 기록
- `images` — 이미지 메타데이터 (SHA-256 dedup, 표정 태그, 라이선스, 소스)
- `collection_images` — many-to-many 조인

## Expression Tags

`joy`, `anger`, `sadness`, `surprise`, `fear`, `disgust`, `neutral`, `other`

## License Compliance

- Wikimedia Commons: CC0, CC-BY, CC-BY-SA, Public Domain
- Pixabay: Pixabay License (CC0-like)
- 라이선스 정보는 DB에 저장됨
- robots.txt / rate-limit 준수 (2초 delay)

## Integration

- M7 표정 템플릿 시스템과 연동: 수집된 이미지는 M10 자동 추출 파이프라인의 보정 데이터로 사용
- DB는 ImageMaker 프로젝트 루트에 `image_collection.db`로 저장 권장
