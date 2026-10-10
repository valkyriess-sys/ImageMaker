// m12_mesh.test.js — M12: Visual Hull 메시 생성 + watertight 패치 + 오버레이 판정 테스트
// 입력: 4뷰 실루엣 (front/deg45/side/back)
// 성공 기준: 메시 생성, watertight 패치, 점 내부 판정, 오버레이 판정, 4방향 렌더링 데이터

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
loadModule('js/mesh.js');

const Core = fakeWindow.SketchCore;
const Mesh = fakeWindow.SketchMesh;

// ═══════════════════════════════════════════
// 4뷰 실루엣 데이터 (e2e_multiview.test.js와 동일)
// ═══════════════════════════════════════════

const frontSilhouette = [
  { x: 0, y: 1.85, z: 0 }, { x: 0.08, y: 1.83, z: 0 }, { x: 0.10, y: 1.78, z: 0 },
  { x: 0.08, y: 1.73, z: 0 }, { x: 0, y: 1.72, z: 0 }, { x: -0.08, y: 1.73, z: 0 },
  { x: -0.10, y: 1.78, z: 0 }, { x: -0.08, y: 1.83, z: 0 }, { x: 0, y: 1.85, z: 0 },
  { x: 0.04, y: 1.72, z: 0 }, { x: 0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.72, z: 0 },
  { x: 0.14, y: 1.68, z: 0 }, { x: 0.18, y: 1.65, z: 0 }, { x: 0.20, y: 1.60, z: 0 },
  { x: -0.14, y: 1.68, z: 0 }, { x: -0.18, y: 1.65, z: 0 }, { x: -0.20, y: 1.60, z: 0 },
  { x: 0.18, y: 1.60, z: 0 }, { x: 0.16, y: 1.55, z: 0 }, { x: 0.12, y: 1.50, z: 0 },
  { x: -0.18, y: 1.60, z: 0 }, { x: -0.16, y: 1.55, z: 0 }, { x: -0.12, y: 1.50, z: 0 },
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

const sideSilhouette = [
  { x: 0, y: 1.85, z: 0 }, { x: 0.04, y: 1.83, z: 0.02 }, { x: 0.05, y: 1.78, z: 0.04 },
  { x: 0.04, y: 1.73, z: 0.02 }, { x: 0, y: 1.72, z: 0 }, { x: -0.04, y: 1.73, z: -0.02 },
  { x: -0.05, y: 1.78, z: -0.04 }, { x: -0.04, y: 1.83, z: -0.02 }, { x: 0, y: 1.85, z: 0 },
  { x: 0.02, y: 1.72, z: 0.01 }, { x: 0.02, y: 1.68, z: 0.01 }, { x: -0.02, y: 1.68, z: -0.01 }, { x: -0.02, y: 1.72, z: -0.01 },
  { x: 0.08, y: 1.68, z: 0.03 }, { x: 0.10, y: 1.65, z: 0.03 }, { x: 0.11, y: 1.60, z: 0.02 },
  { x: -0.08, y: 1.68, z: -0.03 }, { x: -0.10, y: 1.65, z: -0.03 }, { x: -0.11, y: 1.60, z: -0.02 },
  { x: 0.10, y: 1.60, z: 0.02 }, { x: 0.09, y: 1.55, z: 0.02 }, { x: 0.07, y: 1.50, z: 0.01 },
  { x: -0.10, y: 1.60, z: -0.02 }, { x: -0.09, y: 1.55, z: -0.02 }, { x: -0.07, y: 1.50, z: -0.01 },
  { x: 0.07, y: 1.50, z: 0.01 }, { x: 0.05, y: 1.45, z: 0.01 }, { x: 0.04, y: 1.40, z: 0 },
  { x: -0.07, y: 1.50, z: -0.01 }, { x: -0.05, y: 1.45, z: -0.01 }, { x: -0.04, y: 1.40, z: 0 },
  { x: 0.04, y: 1.40, z: 0 }, { x: 0.06, y: 1.35, z: 0 }, { x: 0.08, y: 1.30, z: 0.01 },
  { x: -0.04, y: 1.40, z: 0 }, { x: -0.06, y: 1.35, z: 0 }, { x: -0.08, y: 1.30, z: -0.01 },
  { x: 0.08, y: 1.30, z: 0.02 }, { x: 0.07, y: 1.20, z: 0.02 }, { x: 0.06, y: 1.10, z: 0.02 },
  { x: 0.06, y: 1.10, z: 0.02 }, { x: 0.05, y: 0.90, z: 0.02 }, { x: 0.04, y: 0.70, z: 0.02 },
  { x: 0.04, y: 0.70, z: 0.02 }, { x: 0.05, y: 0.60, z: 0.02 }, { x: 0.06, y: 0.50, z: 0.02 },
  { x: -0.08, y: 1.30, z: -0.02 }, { x: -0.07, y: 1.20, z: -0.02 }, { x: -0.06, y: 1.10, z: -0.02 },
  { x: -0.06, y: 1.10, z: -0.02 }, { x: -0.05, y: 0.90, z: -0.02 }, { x: -0.04, y: 0.70, z: -0.02 },
  { x: -0.04, y: 0.70, z: -0.02 }, { x: -0.05, y: 0.60, z: -0.02 }, { x: -0.06, y: 0.50, z: -0.02 },
  { x: -0.05, y: 1.40, z: -0.08 }, { x: -0.08, y: 1.30, z: -0.10 }, { x: -0.10, y: 1.20, z: -0.08 },
  { x: -0.08, y: 1.10, z: -0.05 },
];

const backSilhouette = [
  { x: 0, y: 1.85, z: 0 }, { x: 0.08, y: 1.83, z: 0 }, { x: 0.10, y: 1.78, z: 0 },
  { x: 0.08, y: 1.73, z: 0 }, { x: 0, y: 1.72, z: 0 }, { x: -0.08, y: 1.73, z: 0 },
  { x: -0.10, y: 1.78, z: 0 }, { x: -0.08, y: 1.83, z: 0 }, { x: 0, y: 1.85, z: 0 },
  { x: 0.04, y: 1.72, z: 0 }, { x: 0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.68, z: 0 }, { x: -0.04, y: 1.72, z: 0 },
  { x: 0.14, y: 1.68, z: 0 }, { x: 0.18, y: 1.65, z: 0 }, { x: 0.20, y: 1.60, z: 0 },
  { x: -0.14, y: 1.68, z: 0 }, { x: -0.18, y: 1.65, z: 0 }, { x: -0.20, y: 1.60, z: 0 },
  { x: 0.18, y: 1.60, z: 0 }, { x: 0.16, y: 1.55, z: 0 }, { x: 0.12, y: 1.50, z: 0 },
  { x: -0.18, y: 1.60, z: 0 }, { x: -0.16, y: 1.55, z: 0 }, { x: -0.12, y: 1.50, z: 0 },
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
  { x: 0.20, y: 1.60, z: -0.10 }, { x: 0.30, y: 1.55, z: -0.15 }, { x: 0.35, y: 1.45, z: -0.18 },
  { x: 0.30, y: 1.35, z: -0.15 }, { x: 0.20, y: 1.40, z: -0.10 },
  { x: -0.20, y: 1.60, z: -0.10 }, { x: -0.30, y: 1.55, z: -0.15 }, { x: -0.35, y: 1.45, z: -0.18 },
  { x: -0.30, y: 1.35, z: -0.15 }, { x: -0.20, y: 1.40, z: -0.10 },
];

const silhouettes = [frontSilhouette, deg45Silhouette, sideSilhouette, backSilhouette];
const viewAngles = [0, 45, 90, 180];

// ═══════════════════════════════════════════
// M12 테스트 1: 실루엣 포인트 추출
// ═══════════════════════════════════════════

test('M12-1: 실루엣 포인트 추출 — 4뷰 → 3D 포인트', () => {
  const points = Mesh.extractPointsFromSilhouettes(silhouettes, viewAngles);
  assert.ok(points.length > 0, `추출된 포인트 수: ${points.length}`);

  // 모든 포인트가 유효한 3D 좌표인지 확인
  for (let i = 0; i < points.length; i++) {
    const p = points[i];
    assert.ok(typeof p.x === 'number' && !isNaN(p.x), `point[${i}].x: 유효한 숫자`);
    assert.ok(typeof p.y === 'number' && !isNaN(p.y), `point[${i}].y: 유효한 숫자`);
    assert.ok(typeof p.z === 'number' && !isNaN(p.z), `point[${i}].z: 유효한 숫자`);
  }

  // 포인트 수 = 모든 실루엣 포인트 수의 합
  const expectedCount = silhouettes.reduce((sum, s) => sum + s.length, 0);
  assert.equal(points.length, expectedCount, `포인트 수 일치: ${points.length} = ${expectedCount}`);
});

// ═══════════════════════════════════════════
// M12 테스트 2: Visual Hull 메시 생성
// ═══════════════════════════════════════════

test('M12-2: Visual Hull 메시 생성 — vertices + faces', () => {
  const mesh = Mesh.buildVisualHull(silhouettes, viewAngles);

  assert.ok(mesh.vertices.length > 0, `vertices: ${mesh.vertices.length}`);
  assert.ok(mesh.faces.length > 0, `faces: ${mesh.faces.length}`);

  // 모든 face가 유효한 인덱스를 참조하는지 확인
  for (let i = 0; i < mesh.faces.length; i++) {
    const face = mesh.faces[i];
    assert.ok(face.length >= 3, `face[${i}]: 최소 3개 이상의 인덱스`);
    for (const idx of face) {
      assert.ok(idx >= 0 && idx < mesh.vertices.length, `face[${i}]: 인덱스 ${idx} 범위 내`);
    }
  }

  // 메시의 바운딩 박스 계산
  let minX = Infinity, maxX = -Infinity;
  let minY = Infinity, maxY = -Infinity;
  let minZ = Infinity, maxZ = -Infinity;
  for (const v of mesh.vertices) {
    if (v.x < minX) minX = v.x;
    if (v.x > maxX) maxX = v.x;
    if (v.y < minY) minY = v.y;
    if (v.y > maxY) maxY = v.y;
    if (v.z < minZ) minZ = v.z;
    if (v.z > maxZ) maxZ = v.z;
  }

  // 바운딩 박스가 유효한지 확인
  assert.ok(maxX > minX, `X 범위: ${minX.toFixed(3)} ~ ${maxX.toFixed(3)}`);
  assert.ok(maxY > minY, `Y 범위: ${minY.toFixed(3)} ~ ${maxY.toFixed(3)}`);
  assert.ok(maxZ >= minZ, `Z 범위: ${minZ.toFixed(3)} ~ ${maxZ.toFixed(3)}`);

  // 캐릭터 비율 확인 (키 대비 너비)
  const height = maxY - minY;
  const width = maxX - minX;
  const ratio = height / (width + 0.001);
  assert.ok(ratio > 1.0, `키/너비 비율: ${ratio.toFixed(2)} (1.0 이상)`);
});

// ═══════════════════════════════════════════
// M12 테스트 3: Watertight 패치
// ═══════════════════════════════════════════

test('M12-3: Watertight 패치 — hole filling', () => {
  const mesh = Mesh.buildVisualHull(silhouettes, viewAngles);
  const patched = Mesh.buildWatertightCaps(mesh);

  assert.ok(patched.vertices.length >= mesh.vertices.length, `patched vertices >= original: ${patched.vertices.length} >= ${mesh.vertices.length}`);
  assert.ok(patched.faces.length >= mesh.faces.length, `patched faces >= original: ${patched.faces.length} >= ${mesh.faces.length}`);

  // 패치된 메시의 모든 face가 유효한 인덱스를 참조하는지 확인
  for (let i = 0; i < patched.faces.length; i++) {
    const face = patched.faces[i];
    for (const idx of face) {
      assert.ok(idx >= 0 && idx < patched.vertices.length, `patched face[${i}]: 인덱스 ${idx} 범위 내`);
    }
  }

  // 경계 엣지 감소 확인
  function countBoundaryEdges(m) {
    const edgeCount = new Map();
    for (const face of m.faces) {
      const n = face.length;
      for (let i = 0; i < n; i++) {
        const a = face[i];
        const b = face[(i + 1) % n];
        const key = a < b ? `${a}_${b}` : `${b}_${a}`;
        edgeCount.set(key, (edgeCount.get(key) || 0) + 1);
      }
    }
    let boundaryCount = 0;
    for (const count of edgeCount.values()) {
      if (count === 1) boundaryCount++;
    }
    return boundaryCount;
  }

  const originalBoundary = countBoundaryEdges(mesh);
  const patchedBoundary = countBoundaryEdges(patched);
  assert.ok(patchedBoundary <= originalBoundary, `경계 엣지 감소: ${originalBoundary} → ${patchedBoundary}`);
});

// ═══════════════════════════════════════════
// M12 테스트 4: 점 내부 판정 (Ray Casting)
// ═══════════════════════════════════════════

test('M12-4: 점 내부 판정 — ray casting으로 내부/외부 구분', () => {
  const mesh = Mesh.buildVisualHull(silhouettes, viewAngles);
  const patched = Mesh.buildWatertightCaps(mesh);

  // 메시 중심점 (내부에 있어야 함)
  let cx = 0, cy = 0, cz = 0;
  for (const v of patched.vertices) {
    cx += v.x;
    cy += v.y;
    cz += v.z;
  }
  cx /= patched.vertices.length;
  cy /= patched.vertices.length;
  cz /= patched.vertices.length;

  const centerInside = Mesh.isInsideVisualHull({ x: cx, y: cy, z: cz }, patched);
  assert.ok(centerInside, `중심점 내부 판정: (${cx.toFixed(3)}, ${cy.toFixed(3)}, ${cz.toFixed(3)})`);

  // 바운딩 박스 바깥 점 (외부에 있어야 함)
  let minX = Infinity, maxX = -Infinity;
  let minY = Infinity, maxY = -Infinity;
  let minZ = Infinity, maxZ = -Infinity;
  for (const v of patched.vertices) {
    if (v.x < minX) minX = v.x;
    if (v.x > maxX) maxX = v.x;
    if (v.y < minY) minY = v.y;
    if (v.y > maxY) maxY = v.y;
    if (v.z < minZ) minZ = v.z;
    if (v.z > maxZ) maxZ = v.z;
  }

  const outsidePoint = { x: maxX + 10, y: maxY + 10, z: maxZ + 10 };
  const outsideResult = Mesh.isInsideVisualHull(outsidePoint, patched);
  assert.ok(!outsidePoint || !outsideResult, `바깥 점 외부 판정: (${outsidePoint.x}, ${outsidePoint.y}, ${outsidePoint.z})`);
});

// ═══════════════════════════════════════════
// M12 테스트 5: 오버레이 판정
// ═══════════════════════════════════════════

test('M12-5: 오버레이 판정 — 두 메시의 교차 영역', () => {
  const mesh = Mesh.buildVisualHull(silhouettes, viewAngles);
  const patched = Mesh.buildWatertightCaps(mesh);

  // 자기 자신과의 오버레이 = 전체가 교차
  const selfOverlay = Mesh.checkOverlay(patched, patched);
  assert.ok(selfOverlay.meshAOverlayCount > 0, `자기 오버레이 A: ${selfOverlay.meshAOverlayCount}`);
  assert.ok(selfOverlay.meshBOverlayCount > 0, `자기 오버레이 B: ${selfOverlay.meshBOverlayCount}`);
  assert.ok(selfOverlay.overlayRatioA > 0, `오버레이 비율 A: ${selfOverlay.overlayRatioA.toFixed(3)}`);
  assert.ok(selfOverlay.overlayRatioB > 0, `오버레이 비율 B: ${selfOverlay.overlayRatioB.toFixed(3)}`);

  // 다른 메시와의 오버레이 (shifted mesh)
  const shiftedMesh = {
    vertices: patched.vertices.map(v => ({ x: v.x + 100, y: v.y, z: v.z })),
    faces: patched.faces.slice()
  };
  const diffOverlay = Mesh.checkOverlay(patched, shiftedMesh);
  assert.ok(diffOverlay.meshAOverlayCount === 0, `다른 메시 오버레이 A: ${diffOverlay.meshAOverlayCount} (0이어야 함)`);
  assert.ok(diffOverlay.meshBOverlayCount === 0, `다른 메시 오버레이 B: ${diffOverlay.meshBOverlayCount} (0이어야 함)`);
});

// ═══════════════════════════════════════════
// M12 테스트 6: 4방향 렌더링 데이터 생성
// ═══════════════════════════════════════════

test('M12-6: 4방향 렌더링 데이터 생성 — front/side/top/3-4', () => {
  const mesh = Mesh.buildVisualHull(silhouettes, viewAngles);
  const patched = Mesh.buildWatertightCaps(mesh);

  const renderAngles = [0, 90, 180, 270];
  const renderData = Mesh.prepareRenderData(patched, renderAngles);

  assert.equal(renderData.length, 4, `렌더링 데이터 수: ${renderData.length}`);

  for (let i = 0; i < renderData.length; i++) {
    const rd = renderData[i];
    assert.equal(rd.angle, renderAngles[i], `각도 일치: ${rd.angle} = ${renderAngles[i]}`);
    assert.ok(rd.camVertices.length === patched.vertices.length, `카메라 정점 수 일치: ${rd.camVertices.length}`);
    assert.ok(rd.sortedFaces.length === patched.faces.length, `정렬된 면 수 일치: ${rd.sortedFaces.length}`);

    // 깊이 정렬이 올바른지 확인
    for (let j = 1; j < rd.sortedFaces.length; j++) {
      assert.ok(rd.sortedFaces[j].avgZ >= rd.sortedFaces[j - 1].avgZ - 0.001,
        `깊이 정렬 오류 at ${j}`);
    }
  }
});

// ═══════════════════════════════════════════
// M12 테스트 7: 전체 파이프라인 통합 검증
// ═══════════════════════════════════════════

test('M12-7: 전체 파이프라인 — 실루엣 → 메시 → watertight → 렌더링', () => {
  // 1. 실루엣 포인트 추출
  const points = Mesh.extractPointsFromSilhouettes(silhouettes, viewAngles);
  assert.ok(points.length > 0, `포인트 추출: ${points.length}개`);

  // 2. Visual Hull 메시 생성
  let mesh = Mesh.buildVisualHull(silhouettes, viewAngles);
  assert.ok(mesh.vertices.length > 0, `메시 생성: ${mesh.vertices.length} vertices`);
  assert.ok(mesh.faces.length > 0, `메시 생성: ${mesh.faces.length} faces`);

  // 3. Watertight 패치
  mesh = Mesh.buildWatertightCaps(mesh);
  assert.ok(mesh.vertices.length > 0, `watertight 패치: ${mesh.vertices.length} vertices`);
  assert.ok(mesh.faces.length > 0, `watertight 패치: ${mesh.faces.length} faces`);

  // 4. 점 내부 판정
  let cx = 0, cy = 0, cz = 0;
  for (const v of mesh.vertices) {
    cx += v.x;
    cy += v.y;
    cz += v.z;
  }
  cx /= mesh.vertices.length;
  cy /= mesh.vertices.length;
  cz /= mesh.vertices.length;
  const inside = Mesh.isInsideVisualHull({ x: cx, y: cy, z: cz }, mesh);
  assert.ok(inside, `중심점 내부 판정`);

  // 5. 오버레이 판정
  const overlay = Mesh.checkOverlay(mesh, mesh);
  assert.ok(overlay.meshAOverlayCount > 0, `오버레이 판정: ${overlay.meshAOverlayCount} faces`);

  // 6. 4방향 렌더링 데이터 생성
  const renderData = Mesh.prepareRenderData(mesh, [0, 90, 180, 270]);
  assert.equal(renderData.length, 4, `렌더링 데이터: ${renderData.length}개`);

  // 7. 요약 출력
  console.log('\n═══════════════════════════════════════════');
  console.log('M12 Visual Hull 메시 생성 파이프라인 결과');
  console.log('═══════════════════════════════════════════');
  console.log(`  포인트 추출: ${points.length} points`);
  console.log(`  메시 생성: ${mesh.vertices.length} vertices, ${mesh.faces.length} faces`);
  console.log(`  중심점 내부 판정: ${inside}`);
  console.log(`  오버레이 판정: ${overlay.meshAOverlayCount} faces`);
  console.log(`  렌더링 데이터: ${renderData.length} views`);
  console.log('═══════════════════════════════════════════\n');
});

console.log('M12 메시 생성 테스트 로드 완료');