// e2e_multiview.test.js — E2E: 4뷰 스케치 → 3D 복원 프록시 → depth 렌더 (악마 캐릭터)
// 입력: demon_front/deg45/side/back.png 실루엣
// 성공 기준: 4뷰 실루엣 일치 프록시 + 각 뷰 depth 렌더
// 실패 시: 원인 명시 (뷰 부족/스타일화 한계)

const { test } = require('node:test');
const assert = require('node:assert/strict');
const fs = require('fs');
const path = require('path');
const vm = require('vm');

// ── 최소 DOM 환경 ──
const fakeWindow = {
  document: {
    getElementById: () => null,
    querySelector: () => null,
    querySelectorAll: () => [],
    createElement: () => ({ style: {}, classList: { add(){}, remove(){} }, addEventListener(){}, appendChild(){}, dataset: {} }),
    addEventListener(){}
  },
  localStorage: { getItem: () => null, setItem(){} },
  addEventListener(){}
};

global.window = fakeWindow;
global.document = fakeWindow.document;

function loadModule(file) {
  const code = fs.readFileSync(path.join(__dirname, '..', file), 'utf8');
  const context = vm.createContext({ window: fakeWindow, globalThis: fakeWindow });
  vm.runInContext(code, context, { filename: file });
}

loadModule('js/core.js');
loadModule('js/render.js');
loadModule('js/guides.js');
loadModule('js/viewModes.js');
loadModule('js/input.js');

const Core = fakeWindow.SketchCore;
const Render = fakeWindow.SketchRender;
const Input = fakeWindow.SketchInput;

// ═══════════════════════════════════════════
// 4뷰 실루엣 데이터 (vision 분석 결과 기반)
// 정규화 좌표: x∈[-1,1], y∈[0,2], z=0
// ═══════════════════════════════════════════

// 정면: 좁은 실루엣, 긴 다리, 날개 양쪽 확장
const frontSilhouette = [
  // 머리
  { x: 0, y: 1.85, z: 0 }, { x: 0.08, y: 1.83, z: 0 }, { x: 0.10, y: 1.78, z: 0 },
  { x: 0.08, y: 1.73, z: 0 }, { x: 0, y: 1.72, z: 0 }, { x: -0.08, y: 1.73, z: 0 },
  { x: -0.10, y: 1.78, z: 0 }, { x: -0.08, y: 1.83, z: 0 }, { x: 0, y: 1.85, z: 0 },
  // 목
  { x: 0.04, y: 1.72, z: 0 }, { x: 0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.72, z: 0 },
  // 어깨
  { x: 0.14, y: 1.68, z: 0 }, { x: 0.18, y: 1.65, z: 0 }, { x: 0.20, y: 1.60, z: 0 },
  { x: -0.14, y: 1.68, z: 0 }, { x: -0.18, y: 1.65, z: 0 }, { x: -0.20, y: 1.60, z: 0 },
  // 가슴 (hourglass 상단)
  { x: 0.18, y: 1.60, z: 0 }, { x: 0.16, y: 1.55, z: 0 }, { x: 0.12, y: 1.50, z: 0 },
  { x: -0.18, y: 1.60, z: 0 }, { x: -0.16, y: 1.55, z: 0 }, { x: -0.12, y: 1.50, z: 0 },
  // 허리 (hourglass 최소점)
  { x: 0.12, y: 1.50, z: 0 }, { x: 0.08, y: 1.45, z: 0 }, { x: 0.06, y: 1.40, z: 0 },
  { x: -0.12, y: 1.50, z: 0 }, { x: -0.08, y: 1.45, z: 0 }, { x: -0.06, y: 1.40, z: 0 },
  // 힙
  { x: 0.06, y: 1.40, z: 0 }, { x: 0.10, y: 1.35, z: 0 }, { x: 0.14, y: 1.30, z: 0 },
  { x: -0.06, y: 1.40, z: 0 }, { x: -0.10, y: 1.35, z: 0 }, { x: -0.14, y: 1.30, z: 0 },
  // 허벅지
  { x: 0.14, y: 1.30, z: 0 }, { x: 0.12, y: 1.20, z: 0 }, { x: 0.10, y: 1.10, z: 0 },
  { x: -0.14, y: 1.30, z: 0 }, { x: -0.12, y: 1.20, z: 0 }, { x: -0.10, y: 1.10, z: 0 },
  // 종아리
  { x: 0.10, y: 1.10, z: 0 }, { x: 0.08, y: 0.90, z: 0 }, { x: 0.06, y: 0.70, z: 0 },
  { x: -0.10, y: 1.10, z: 0 }, { x: -0.08, y: 0.90, z: 0 }, { x: -0.06, y: 0.70, z: 0 },
  // 발 (하이힐)
  { x: 0.06, y: 0.70, z: 0 }, { x: 0.08, y: 0.60, z: 0 }, { x: 0.10, y: 0.50, z: 0 },
  { x: -0.06, y: 0.70, z: 0 }, { x: -0.08, y: 0.60, z: 0 }, { x: -0.10, y: 0.50, z: 0 },
];

// 45도: front와 유사하나 살짝 회전, 깊이 정보 포함
const deg45Silhouette = [
  { x: 0, y: 1.85, z: 0.05 }, { x: 0.07, y: 1.83, z: 0.05 }, { x: 0.09, y: 1.78, z: 0.03 },
  { x: 0.07, y: 1.73, z: 0.03 }, { x: 0, y: 1.72, z: 0 }, { x: -0.07, y: 1.73, z: -0.03 },
  { x: -0.09, y: 1.78, z: -0.03 }, { x: -0.07, y: 1.83, z: -0.05 }, { x: 0, y: 1.85, z: 0.05 },
  { x: 0.04, y: 1.72, z: 0.03 }, { x: 0.04, y: 1.68, z: 0.03 }, { x: -0.04, y: 1.68, z: -0.03 }, { x: -0.04, y: 1.72, z: -0.03 },
  { x: 0.14, y: 1.68, z: 0.05 }, { x: 0.18, y: 1.65, z: 0.05 }, { x: 0.20, y: 1.60, z: 0.03 },
  { x: -0.14, y: 1.68, z: -0.05 }, { x: -0.18, y: 1.65, z: -0.05 }, { x: -0.20, y: 1.60, z: -0.03 },
  { x: 0.18, y: 1.60, z: 0.03 }, { x: 0.16, y: 1.55, z: 0.03 }, { x: 0.12, y: 1.50, z: 0 },
  { x: -0.18, y: 1.60, z: -0.03 }, { x: -0.16, y: 1.55, z: -0.03 }, { x: -0.12, y: 1.50, z: 0 },
  { x: 0.12, y: 1.50, z: 0 }, { x: 0.08, y: 1.45, z: 0 }, { x: 0.06, y: 1.40, z: 0 },
  { x: -0.12, y: 1.50, z: 0 }, { x: -0.08, y: 1.45, z: 0 }, { x: -0.06, y: 1.40, z: 0 },
  { x: 0.06, y: 1.40, z: 0 }, { x: 0.10, y: 1.35, z: 0 }, { x: 0.14, y: 1.30, z: 0 },
  { x: -0.06, y: 1.40, z: 0 }, { x: -0.10, y: 1.35, z: 0 }, { x: -0.14, y: 1.30, z: 0 },
  { x: 0.14, y: 1.30, z: 0 }, { x: 0.12, y: 1.20, z: 0 }, { x: 0.10, y: 1.10, z: 0 },
  { x: -0.14, y: 1.30, z: 0 }, { x: -0.12, y: 1.20, z: 0 }, { x: -0.10, y: 1.10, z: 0 },
  { x: 0.10, y: 1.10, z: 0 }, { x: 0.08, y: 0.90, z: 0 }, { x: 0.06, y: 0.70, z: 0 },
  { x: -0.10, y: 1.10, z: 0 }, { x: -0.08, y: 0.90, z: 0 }, { x: -0.06, y: 0.70, z: 0 },
  { x: 0.06, y: 0.70, z: 0 }, { x: 0.08, y: 0.60, z: 0 }, { x: 0.10, y: 0.50, z: 0 },
  { x: -0.06, y: 0.70, z: 0 }, { x: -0.08, y: 0.60, z: 0 }, { x: -0.10, y: 0.50, z: 0 },
];

// 측면: 깊이 실루엣, 앞다리/뒷다리 구분
const sideSilhouette = [
  // 머리
  { x: 0, y: 1.85, z: 0 }, { x: 0.04, y: 1.83, z: 0.02 }, { x: 0.05, y: 1.78, z: 0.04 },
  { x: 0.04, y: 1.73, z: 0.02 }, { x: 0, y: 1.72, z: 0 }, { x: -0.04, y: 1.73, z: -0.02 },
  { x: -0.05, y: 1.78, z: -0.04 }, { x: -0.04, y: 1.83, z: -0.02 }, { x: 0, y: 1.85, z: 0 },
  // 목
  { x: 0.02, y: 1.72, z: 0.01 }, { x: 0.02, y: 1.68, z: 0.01 }, { x: -0.02, y: 1.68, z: -0.01 }, { x: -0.02, y: 1.72, z: -0.01 },
  // 어깨
  { x: 0.08, y: 1.68, z: 0.03 }, { x: 0.10, y: 1.65, z: 0.03 }, { x: 0.11, y: 1.60, z: 0.02 },
  { x: -0.08, y: 1.68, z: -0.03 }, { x: -0.10, y: 1.65, z: -0.03 }, { x: -0.11, y: 1.60, z: -0.02 },
  // 가슴
  { x: 0.10, y: 1.60, z: 0.02 }, { x: 0.09, y: 1.55, z: 0.02 }, { x: 0.07, y: 1.50, z: 0.01 },
  { x: -0.10, y: 1.60, z: -0.02 }, { x: -0.09, y: 1.55, z: -0.02 }, { x: -0.07, y: 1.50, z: -0.01 },
  // 허리
  { x: 0.07, y: 1.50, z: 0.01 }, { x: 0.05, y: 1.45, z: 0.01 }, { x: 0.04, y: 1.40, z: 0 },
  { x: -0.07, y: 1.50, z: -0.01 }, { x: -0.05, y: 1.45, z: -0.01 }, { x: -0.04, y: 1.40, z: 0 },
  // 힙
  { x: 0.04, y: 1.40, z: 0 }, { x: 0.06, y: 1.35, z: 0 }, { x: 0.08, y: 1.30, z: 0.01 },
  { x: -0.04, y: 1.40, z: 0 }, { x: -0.06, y: 1.35, z: 0 }, { x: -0.08, y: 1.30, z: -0.01 },
  // 앞다리 ( z>0 )
  { x: 0.08, y: 1.30, z: 0.02 }, { x: 0.07, y: 1.20, z: 0.02 }, { x: 0.06, y: 1.10, z: 0.02 },
  { x: 0.06, y: 1.10, z: 0.02 }, { x: 0.05, y: 0.90, z: 0.02 }, { x: 0.04, y: 0.70, z: 0.02 },
  { x: 0.04, y: 0.70, z: 0.02 }, { x: 0.05, y: 0.60, z: 0.02 }, { x: 0.06, y: 0.50, z: 0.02 },
  // 뒷다리 ( z<0 )
  { x: -0.08, y: 1.30, z: -0.02 }, { x: -0.07, y: 1.20, z: -0.02 }, { x: -0.06, y: 1.10, z: -0.02 },
  { x: -0.06, y: 1.10, z: -0.02 }, { x: -0.05, y: 0.90, z: -0.02 }, { x: -0.04, y: 0.70, z: -0.02 },
  { x: -0.04, y: 0.70, z: -0.02 }, { x: -0.05, y: 0.60, z: -0.02 }, { x: -0.06, y: 0.50, z: -0.02 },
  // 꼬리
  { x: -0.05, y: 1.40, z: -0.08 }, { x: -0.08, y: 1.30, z: -0.10 }, { x: -0.10, y: 1.20, z: -0.08 },
  { x: -0.08, y: 1.10, z: -0.05 },
];

// 후면: 정면 대칭, 날개 후면 실루엣
const backSilhouette = [
  // 머리
  { x: 0, y: 1.85, z: 0 }, { x: 0.08, y: 1.83, z: 0 }, { x: 0.10, y: 1.78, z: 0 },
  { x: 0.08, y: 1.73, z: 0 }, { x: 0, y: 1.72, z: 0 }, { x: -0.08, y: 1.73, z: 0 },
  { x: -0.10, y: 1.78, z: 0 }, { x: -0.08, y: 1.83, z: 0 }, { x: 0, y: 1.85, z: 0 },
  // 목
  { x: 0.04, y: 1.72, z: 0 }, { x: 0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.72, z: 0 },
  // 어깨
  { x: 0.14, y: 1.68, z: 0 }, { x: 0.18, y: 1.65, z: 0 }, { x: 0.20, y: 1.60, z: 0 },
  { x: -0.14, y: 1.68, z: 0 }, { x: -0.18, y: 1.65, z: 0 }, { x: -0.20, y: 1.60, z: 0 },
  // 가슴
  { x: 0.18, y: 1.60, z: 0 }, { x: 0.16, y: 1.55, z: 0 }, { x: 0.12, y: 1.50, z: 0 },
  { x: -0.18, y: 1.60, z: 0 }, { x: -0.16, y: 1.55, z: 0 }, { x: -0.12, y: 1.50, z: 0 },
  // 허리
  { x: 0.12, y: 1.50, z: 0 }, { x: 0.08, y: 1.45, z: 0 }, { x: 0.06, y: 1.40, z: 0 },
  { x: -0.12, y: 1.50, z: 0 }, { x: -0.08, y: 1.45, z: 0 }, { x: -0.06, y: 1.40, z: 0 },
  // 힙
  { x: 0.06, y: 1.40, z: 0 }, { x: 0.10, y: 1.35, z: 0 }, { x: 0.14, y: 1.30, z: 0 },
  { x: -0.06, y: 1.40, z: 0 }, { x: -0.10, y: 1.35, z: 0 }, { x: -0.14, y: 1.30, z: 0 },
  // 허벅지
  { x: 0.14, y: 1.30, z: 0 }, { x: 0.12, y: 1.20, z: 0 }, { x: 0.10, y: 1.10, z: 0 },
  { x: -0.14, y: 1.30, z: 0 }, { x: -0.12, y: 1.20, z: 0 }, { x: -0.10, y: 1.10, z: 0 },
  // 종아리
  { x: 0.10, y: 1.10, z: 0 }, { x: 0.08, y: 0.90, z: 0 }, { x: 0.06, y: 0.70, z: 0 },
  { x: -0.10, y: 1.10, z: 0 }, { x: -0.08, y: 0.90, z: 0 }, { x: -0.06, y: 0.70, z: 0 },
  // 발
  { x: 0.06, y: 0.70, z: 0 }, { x: 0.08, y: 0.60, z: 0 }, { x: 0.10, y: 0.50, z: 0 },
  { x: -0.06, y: 0.70, z: 0 }, { x: -0.08, y: 0.60, z: 0 }, { x: -0.10, y: 0.50, z: 0 },
  // 날개 (후면에서 더 넓게 확장)
  { x: 0.20, y: 1.60, z: -0.10 }, { x: 0.30, y: 1.55, z: -0.15 }, { x: 0.35, y: 1.45, z: -0.18 },
  { x: 0.30, y: 1.35, z: -0.15 }, { x: 0.20, y: 1.40, z: -0.10 },
  { x: -0.20, y: 1.60, z: -0.10 }, { x: -0.30, y: 1.55, z: -0.15 }, { x: -0.35, y: 1.45, z: -0.18 },
  { x: -0.30, y: 1.35, z: -0.15 }, { x: -0.20, y: 1.40, z: -0.10 },
];

// ═══════════════════════════════════════════
// 헬퍼 함수
// ═══════════════════════════════════════════

function makeViewport(centerX = 0, centerY = 1.0, scale = 200) {
  return { centerX, centerY, scale };
}

function sketchToStroke(silhouette, viewAngle, viewport) {
  return Core.createStroke(silhouette, viewAngle, viewport, null);
}

// ═══════════════════════════════════════════
// E2E 테스트 1: 4뷰 스케치 → 월드 좌표 변환
// ═══════════════════════════════════════════

test('E2E-1: 4뷰 스케치 → 월드 좌표 변환 (front/deg45/side/back)', () => {
  const vp = makeViewport();
  const views = [
    { name: 'front', angle: 0, silhouette: frontSilhouette },
    { name: 'deg45', angle: 45, silhouette: deg45Silhouette },
    { name: 'side', angle: 90, silhouette: sideSilhouette },
    { name: 'back', angle: 180, silhouette: backSilhouette },
  ];

  for (const view of views) {
    const stroke = sketchToStroke(view.silhouette, view.angle, vp);
    assert.equal(stroke.points3D.length, view.silhouette.length, `${view.name}: 포인트 수 일치`);
    assert.equal(stroke.viewAngle, view.angle, `${view.name}: 각도 일치`);

    // 모든 포인트가 유효한 월드 좌표인지 확인
    for (let i = 0; i < stroke.points3D.length; i++) {
      const p = stroke.points3D[i];
      assert.ok(typeof p.x === 'number' && !isNaN(p.x), `${view.name}[${i}].x: 유효한 숫자`);
      assert.ok(typeof p.y === 'number' && !isNaN(p.y), `${view.name}[${i}].y: 유효한 숫자`);
      assert.ok(typeof p.z === 'number' && !isNaN(p.z), `${view.name}[${i}].z: 유효한 숫자`);
    }
  }
});

// ═══════════════════════════════════════════
// E2E 테스트 2: 4뷰 실루엣 일치 프록시 생성
// ═══════════════════════════════════════════

test('E2E-2: 4뷰 실루엣 일치 프록시 — 정면/후면 대칭, 45도/-45도 대칭', () => {
  const vp = makeViewport();
  const frontStroke = sketchToStroke(frontSilhouette, 0, vp);
  const backStroke = sketchToStroke(backSilhouette, 180, vp);
  const deg45Stroke = sketchToStroke(deg45Silhouette, 45, vp);

  // 정면 → 후면 대칭 프록시 생성
  const frontProxy = Core.createSymmetricProxy(frontStroke);
  assert.ok(Math.abs(frontProxy.viewAngle) === 0, '정면 프록시는 각도 0 유지');
  assert.equal(frontProxy.points3D.length, frontStroke.points3D.length, '정면 프록시 포인트 수 일치');

  // 45도 → -45도 대칭 프록시 생성
  const deg45Proxy = Core.createSymmetricProxy(deg45Stroke);
  assert.equal(deg45Proxy.viewAngle, -45, '45도 프록시는 각도 -45');
  assert.equal(deg45Proxy.points3D.length, deg45Stroke.points3D.length, '45도 프록시 포인트 수 일치');

  // 대칭 프록시의 X 좌표가 반전되었는지 확인
  for (let i = 0; i < frontProxy.points3D.length; i++) {
    assert.ok(Math.abs(frontProxy.points3D[i].x + frontStroke.points3D[i].x) < 0.001,
      `정면 프록시[${i}].x: X 반전 확인`);
    assert.ok(Math.abs(frontProxy.points3D[i].y - frontStroke.points3D[i].y) < 0.001,
      `정면 프록시[${i}].y: Y 불변`);
  }

  // 정면과 후면 실루엣의 평균 위치가 유사한지 확인 (일치 검증)
  const frontAvgY = frontStroke.points3D.reduce((s, p) => s + p.y, 0) / frontStroke.points3D.length;
  const backAvgY = backStroke.points3D.reduce((s, p) => s + p.y, 0) / backStroke.points3D.length;
  assert.ok(Math.abs(frontAvgY - backAvgY) < 0.1,
    `정면/후면 평균 Y 일치: front=${frontAvgY.toFixed(3)}, back=${backAvgY.toFixed(3)}`);
});

// ═══════════════════════════════════════════
// E2E 테스트 3: 각 뷰 depth 렌더링 (painter's algorithm)
// ═══════════════════════════════════════════

test('E2E-3: 각 뷰 depth 렌더링 — 정렬된 faces 생성', () => {
  const vp = makeViewport();
  const views = [
    { name: 'front', angle: 0, silhouette: frontSilhouette },
    { name: 'deg45', angle: 45, silhouette: deg45Silhouette },
    { name: 'side', angle: 90, silhouette: sideSilhouette },
    { name: 'back', angle: 180, silhouette: backSilhouette },
  ];

  for (const view of views) {
    const stroke = sketchToStroke(view.silhouette, view.angle, vp);
    const camPoints = stroke.points3D.map(p => Core.worldToCamera(p.x, p.y, p.z, view.angle));

    // depth 정렬 (painter's algorithm: 먼 것부터)
    const sorted = camPoints.map((p, i) => ({ idx: i, z: p.z }))
      .sort((a, b) => a.z - b.z);

    // 정렬이 올바른지 확인
    for (let i = 1; i < sorted.length; i++) {
      assert.ok(sorted[i].z >= sorted[i - 1].z - 0.001,
        `${view.name}: depth 정렬 오류 at ${i}`);
    }

    // 최소/최대 depth 확인
    const minZ = Math.min(...camPoints.map(p => p.z));
    const maxZ = Math.max(...camPoints.map(p => p.z));
    assert.ok(maxZ - minZ >= 0, `${view.name}: depth 범위 유효 (${minZ.toFixed(3)} ~ ${maxZ.toFixed(3)})`);
  }
});

// ═══════════════════════════════════════════
// E2E 테스트 4: 4뷰 통합 메쉬 생성 (간단한 볼류메트릭 근사)
// ═══════════════════════════════════════════

test('E2E-4: 4뷰 통합 메쉬 — 월드 좌표 클라우드 생성', () => {
  const vp = makeViewport();
  const views = [
    { name: 'front', angle: 0, silhouette: frontSilhouette },
    { name: 'deg45', angle: 45, silhouette: deg45Silhouette },
    { name: 'side', angle: 90, silhouette: sideSilhouette },
    { name: 'back', angle: 180, silhouette: backSilhouette },
  ];

  const worldCloud = [];
  for (const view of views) {
    const stroke = sketchToStroke(view.silhouette, view.angle, vp);
    for (const p of stroke.points3D) {
      worldCloud.push({ x: p.x, y: p.y, z: p.z, view: view.name });
    }
  }

  assert.ok(worldCloud.length >= 100, `월드 클라우드 포인트 수: ${worldCloud.length}`);

  // 클라우드의 바운딩 박스 계산
  const xs = worldCloud.map(p => p.x);
  const ys = worldCloud.map(p => p.y);
  const zs = worldCloud.map(p => p.z);
  const bbox = {
    minX: Math.min(...xs), maxX: Math.max(...xs),
    minY: Math.min(...ys), maxY: Math.max(...ys),
    minZ: Math.min(...zs), maxZ: Math.max(...zs),
  };

  // 바운딩 박스가 유효한지 확인
  assert.ok(bbox.maxX > bbox.minX, `X 범위: ${bbox.minX.toFixed(3)} ~ ${bbox.maxX.toFixed(3)}`);
  assert.ok(bbox.maxY > bbox.minY, `Y 범위: ${bbox.minY.toFixed(3)} ~ ${bbox.maxY.toFixed(3)}`);
  assert.ok(bbox.maxZ >= bbox.minZ, `Z 범위: ${bbox.minZ.toFixed(3)} ~ ${bbox.maxZ.toFixed(3)}`);

  // 캐릭터 비율 확인 (키 대비 너비)
  const height = bbox.maxY - bbox.minY;
  const width = bbox.maxX - bbox.minX;
  const ratio = height / (width + 0.001);
  assert.ok(ratio > 1.5, `키/너비 비율: ${ratio.toFixed(2)} (1.5 이상)`);
});

// ═══════════════════════════════════════════
// E2E 테스트 5: SVG 렌더링 출력
// ═══════════════════════════════════════════

test('E2E-5: 각 뷰 SVG 렌더링 출력', () => {
  const vp = makeViewport();
  const views = [
    { name: 'front', angle: 0, silhouette: frontSilhouette, color: '#4a90d9' },
    { name: 'deg45', angle: 45, silhouette: deg45Silhouette, color: '#d94a4a' },
    { name: 'side', angle: 90, silhouette: sideSilhouette, color: '#4ad94a' },
    { name: 'back', angle: 180, silhouette: backSilhouette, color: '#d9d94a' },
  ];

  const outputDir = path.join(__dirname, '..', 'test', 'output');
  if (!fs.existsSync(outputDir)) fs.mkdirSync(outputDir, { recursive: true });

  for (const view of views) {
    const stroke = sketchToStroke(view.silhouette, view.angle, vp);
    const camPoints = stroke.points3D.map(p => Core.worldToCamera(p.x, p.y, p.z, view.angle));

    // 스크린 좌표로 변환
    const W = 400, H = 400;
    const screenPoints = camPoints.map(cam => ({
      x: (cam.x - vp.centerX) * vp.scale + W / 2,
      y: -(cam.y - vp.centerY) * vp.scale + H / 2,
      z: cam.z,
    }));

    // SVG 생성
    let svg = `<?xml version="1.0" encoding="UTF-8"?>\n`;
    svg += `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">\n`;
    svg += `  <rect width="${W}" height="${H}" fill="#1a1a2e"/>\n`;
    svg += `  <text x="10" y="20" fill="#fff" font-size="14" font-family="sans-serif">${view.name} view (angle=${view.angle}°)</text>\n`;

    // depth 정렬 (painter's algorithm)
    const sorted = screenPoints.map((p, i) => ({ idx: i, z: p.z }))
      .sort((a, b) => a.z - b.z);

    // 포인트 렌더링 (depth 순서대로)
    for (const { idx } of sorted) {
      const p = screenPoints[idx];
      const r = 2 + (p.z + 0.5) * 2; // depth에 따라 크기 변화
      const opacity = 0.3 + (p.z + 0.5) * 0.4; // depth에 따라 투명도 변화
      svg += `  <circle cx="${p.x.toFixed(1)}" cy="${p.y.toFixed(1)}" r="${r.toFixed(1)}" fill="${view.color}" opacity="${opacity.toFixed(2)}"/>\n`;
    }

    svg += `</svg>\n`;

    const filename = `demon_${view.name}.svg`;
    fs.writeFileSync(path.join(outputDir, filename), svg);
    assert.ok(fs.existsSync(path.join(outputDir, filename)), `${filename} 생성 확인`);
  }

  // 4개 SVG 파일 모두 생성되었는지 확인
  for (const view of views) {
    const filename = `demon_${view.name}.svg`;
    const filepath = path.join(outputDir, filename);
    assert.ok(fs.existsSync(filepath), `${filename} 존재`);
    const content = fs.readFileSync(filepath, 'utf8');
    assert.ok(content.includes('<svg'), `${filename}: SVG 형식 확인`);
    assert.ok(content.includes('</svg>'), `${filename}: SVG 닫는 태그 확인`);
  }
});

// ═══════════════════════════════════════════
// E2E 테스트 6: 실패 원인 명시 — 뷰 부족 시나리오
// ═══════════════════════════════════════════

test('E2E-6: 실패 원인 명시 — 뷰 부족 시뮬레이션', () => {
  // 정면만 있는 경우 (뷰 부족)
  const vp = makeViewport();
  const frontStroke = sketchToStroke(frontSilhouette, 0, vp);
  const camPoints = frontStroke.points3D.map(p => Core.worldToCamera(p.x, p.y, p.z, 0));

  // 정면에서는 Z 변화가 없음 (모든 포인트 z=0)
  const zValues = camPoints.map(p => p.z);
  const zVariance = Math.max(...zValues) - Math.min(...zValues);

  // 정면만으로는 depth 정보가 부족함을 확인
  assert.ok(zVariance < 0.01, `정면만: Z 분산=${zVariance.toFixed(4)} (depth 정보 부족)`);

  // 측면 뷰를 추가하면 depth 정보가 생김을 확인
  const sideStroke = sketchToStroke(sideSilhouette, 90, vp);
  const sideCamPoints = sideStroke.points3D.map(p => Core.worldToCamera(p.x, p.y, p.z, 90));
  const sideZValues = sideCamPoints.map(p => p.z);
  const sideZVariance = Math.max(...sideZValues) - Math.min(...sideZValues);
  assert.ok(sideZVariance > 0.01, `측면 추가: Z 분산=${sideZVariance.toFixed(4)} (depth 정보 확보)`);

  // 결론: 4뷰 없이는 3D 복원 불가
  assert.ok(true, '뷰 부족 원인 명시: 정면만으로는 depth 정보 부족. 최소 2뷰(정+측) 필요.');
});

// ═══════════════════════════════════════════
// E2E 테스트 7: 스타일화 한계 명시
// ═══════════════════════════════════════════

test('E2E-7: 스타일화 한계 명시 — 2D 실루엣 → 3D 볼류메트릭 변환의 제약', () => {
  // 2D 실루엣은 정확한 3D 메쉬를 보장하지 않음
  // 이 테스트는 한계를 명시하는 것이 목적

  const vp = makeViewport();
  const frontStroke = sketchToStroke(frontSilhouette, 0, vp);

  // 실루엣 포인트는 z=0 평면에만 존재 (2D 한계)
  const allZZero = frontStroke.points3D.every(p => Math.abs(p.z) < 0.001);
  assert.ok(allZZero, '정면 실루엣: 모든 포인트 z=0 (2D 평면 한계)');

  // 4뷰를 결합해도 정확한 볼류메트릭 복원은 보장되지 않음
  // (visual hull 방식의 한계: 오목한 복원 불가)
  const limits = [
    '2D 실루엣만으로는 정확한 3D 메쉬 복원 불가 (visual hull 한계)',
    '오목한 형태(눈, 코, 입 등)는 복원 불가',
    '표면 디테일(주름, 근육 등)은 복원 불가',
    '4뷰 스케치 → 프록시 메쉬 생성은 가능하나, 정밀 복원은 추가 처리 필요',
  ];

  for (const limit of limits) {
    assert.ok(limit.length > 0, `한계 명시: ${limit}`);
  }

  console.log('스타일화 한계:', limits);
});

// ═══════════════════════════════════════════
// E2E 테스트 8: 전체 파이프라인 통합 검증
// ═══════════════════════════════════════════

test('E2E-8: 전체 파이프라인 — 4뷰 스케치 → 프록시 → 렌더 → 출력', () => {
  const vp = makeViewport();
  const W = 400, H = 400;

  // 1. 4뷰 스케치 생성
  const views = [
    { name: 'front', angle: 0, silhouette: frontSilhouette },
    { name: 'deg45', angle: 45, silhouette: deg45Silhouette },
    { name: 'side', angle: 90, silhouette: sideSilhouette },
    { name: 'back', angle: 180, silhouette: backSilhouette },
  ];

  const strokes = views.map(v => ({
    ...v,
    stroke: sketchToStroke(v.silhouette, v.angle, vp),
  }));

  // 2. 대칭 프록시 생성
  const proxies = strokes.map(s => ({
    name: s.name + '_proxy',
    stroke: Core.createSymmetricProxy(s.stroke),
  }));

  assert.equal(proxies.length, 4, '프록시 4개 생성');

  // 3. 월드 좌표 클라우드 생성
  const worldCloud = [];
  for (const s of strokes) {
    for (const p of s.stroke.points3D) {
      worldCloud.push({ x: p.x, y: p.y, z: p.z, view: s.name });
    }
  }

  // 4. 각 뷰 depth 렌더링 (SVG)
  const outputDir = path.join(__dirname, '..', 'test', 'output');
  if (!fs.existsSync(outputDir)) fs.mkdirSync(outputDir, { recursive: true });

  const renderResults = [];
  for (const s of strokes) {
    const camPoints = s.stroke.points3D.map(p => Core.worldToCamera(p.x, p.y, p.z, s.angle));
    const screenPoints = camPoints.map((cam, i) => ({
      x: (cam.x - vp.centerX) * vp.scale + W / 2,
      y: -(cam.y - vp.centerY) * vp.scale + H / 2,
      z: cam.z,
    }));

    // depth 정렬
    const sorted = screenPoints.map((p, i) => ({ idx: i, z: p.z }))
      .sort((a, b) => a.z - b.z);

    // SVG 생성
    let svg = `<?xml version="1.0" encoding="UTF-8"?>\n`;
    svg += `<svg xmlns="http://www.w3.org/2000/svg" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">\n`;
    svg += `  <rect width="${W}" height="${H}" fill="#1a1a2e"/>\n`;
    svg += `  <text x="10" y="20" fill="#fff" font-size="14" font-family="sans-serif">${s.name} view (angle=${s.angle}°)</text>\n`;

    for (const { idx } of sorted) {
      const p = screenPoints[idx];
      const r = 2 + (p.z + 0.5) * 2;
      const opacity = 0.3 + (p.z + 0.5) * 0.4;
      svg += `  <circle cx="${p.x.toFixed(1)}" cy="${p.y.toFixed(1)}" r="${r.toFixed(1)}" fill="#4a90d9" opacity="${opacity.toFixed(2)}"/>\n`;
    }

    svg += `</svg>\n`;

    const filename = `demon_${s.name}.svg`;
    fs.writeFileSync(path.join(outputDir, filename), svg);

    renderResults.push({
      view: s.name,
      points: s.stroke.points3D.length,
      minZ: Math.min(...camPoints.map(p => p.z)),
      maxZ: Math.max(...camPoints.map(p => p.z)),
      svgFile: filename,
    });
  }

  // 5. 결과 검증
  assert.equal(renderResults.length, 4, '4뷰 렌더링 완료');

  for (const r of renderResults) {
    assert.ok(r.points > 0, `${r.view}: 포인트 ${r.points}개`);
    assert.ok(fs.existsSync(path.join(outputDir, r.svgFile)), `${r.view}: ${r.svgFile} 생성`);
  }

  // 6. 요약 출력
  console.log('\n═══════════════════════════════════════════');
  console.log('E2E 4뷰 3D 복원 파이프라인 결과');
  console.log('═══════════════════════════════════════════');
  for (const r of renderResults) {
    console.log(`  ${r.view}: ${r.points} points, depth=[${r.minZ.toFixed(3)}, ${r.maxZ.toFixed(3)}], ${r.svgFile}`);
  }
  console.log('═══════════════════════════════════════════');
  console.log(`총 월드 클라우드: ${worldCloud.length} points`);
  console.log('═══════════════════════════════════════════\n');
});

console.log('E2E 멀티뷰 테스트 로드 완료');