// core.test.js — M11 회귀 테스트 (node --test)
// 기준 1-4 + P0 수정사항 (pressure 기록, z=0, 각도 관통, 브러시 렌더) + 뷰모드 레지스트리

const { test } = require('node:test');
const assert = require('node:assert/strict');

// 브라우저 환경 모의 (최소 DOM)
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

// 모듈 로드 (globalThis 기반)
global.window = fakeWindow;
global.document = fakeWindow.document;

// 수동으로 eval 방식 로드
const fs = require('fs');
const path = require('path');
const vm = require('vm');

function loadModule(file) {
  const code = fs.readFileSync(path.join(__dirname, '..', file), 'utf8');
  const context = vm.createContext({ window: fakeWindow, globalThis: fakeWindow });
  vm.runInContext(code, context, { filename: file });
}

loadModule('js/core.js');
loadModule('js/guides.js');
loadModule('js/viewModes.js');

const Core = fakeWindow.SketchCore;
const Guides = fakeWindow.SketchGuides;
const ViewModes = fakeWindow.SketchViewModes;

// ── 헬퍼 ──
function makeViewport(centerX = 0, centerY = 0, scale = 1) {
  return { centerX, centerY, scale };
}

function makeStroke(points, angle, viewport, pressure) {
  return Core.createStroke(points, angle, viewport, pressure);
}

// ═══════════════════════════════════════════
// 기준 1: 스케일 불변성
// ═══════════════════════════════════════════

test('스케일 불변성: 같은 월드 좌표, 다른 스케일 → 스크린 좌표만 변경', () => {
  // 기준 1의 올바른 해석: 같은 월드 좌표를 다른 스케일로 그리면 스크린 좌표는 달라야 하지만,
  // 월드 좌표(points3D)는 동일해야 함 (스케일 불변성)
  const worldPoint = { x: 10, y: 20, z: 0 };
  const angle = 45;

  const s1 = Core.worldToScreen(worldPoint.x, worldPoint.y, worldPoint.z, angle, makeViewport(0, 0, 1), 400, 400);
  const s2 = Core.worldToScreen(worldPoint.x, worldPoint.y, worldPoint.z, angle, makeViewport(0, 0, 2), 400, 400);
  const s3 = Core.worldToScreen(worldPoint.x, worldPoint.y, worldPoint.z, angle, makeViewport(0, 0, 4), 400, 400);

  // 스크린 좌표는 스케일에 따라 달라야 함
  assert.ok(Math.abs(s1.x - s2.x) > 0.001, `스크린 x가 달라야 함: ${s1.x} vs ${s2.x}`);
  assert.ok(Math.abs(s1.y - s2.y) > 0.001, `스크린 y가 달라야 함: ${s1.y} vs ${s2.y}`);

  // 월드 좌표는 동일 (불변) — 스트로크 모델에 저장되는 값
  // 이것은 스트로크 생성 시 스케일과 무관하게 월드 좌표가 저장됨을 의미
  const stroke1 = Core.createStroke([worldPoint], angle, makeViewport(0, 0, 1), null);
  const stroke2 = Core.createStroke([worldPoint], angle, makeViewport(0, 0, 2), null);
  assert.equal(stroke1.points3D[0].x, stroke2.points3D[0].x);
  assert.equal(stroke1.points3D[0].y, stroke2.points3D[0].y);
  assert.equal(stroke1.points3D[0].z, stroke2.points3D[0].z);
});

// ═══════════════════════════════════════════
// 기준 2: Pan/Zoom 후 재투영 일치
// ═══════════════════════════════════════════

test('Pan/Zoom 후 재투영 일치: 월드 좌표 불변, 스크린 좌표만 변경', () => {
  const stroke = makeStroke(
    [{ x: 10, y: 20, z: 0 }, { x: 30, y: 40, z: 0 }],
    0,
    makeViewport(0, 0, 1),
    null
  );

  const vp1 = makeViewport(0, 0, 1);
  const vp2 = makeViewport(100, 50, 2);

  const s1 = stroke.points3D.map(p => Core.worldToScreen(p.x, p.y, p.z, 0, vp1, 400, 400));
  const s2 = stroke.points3D.map(p => Core.worldToScreen(p.x, p.y, p.z, 0, vp2, 400, 400));

  // 스크린 좌표는 달라야 함
  assert.ok(Math.abs(s1[0].x - s2[0].x) > 0.001);
  assert.ok(Math.abs(s1[0].y - s2[0].y) > 0.001);

  // 월드 좌표는 동일 (불변)
  assert.equal(stroke.points3D[0].x, 10);
  assert.equal(stroke.points3D[0].y, 20);
  assert.equal(stroke.points3D[1].x, 30);
  assert.equal(stroke.points3D[1].y, 40);
});

// ═══════════════════════════════════════════
// 기준 3: 부분 병합 시 비대상 정점 불변
// ═══════════════════════════════════════════

test('부분 병합: 영역 밖 정점 불변, 영역 안 정점만 변경', () => {
  const vertices = [];
  for (let i = 0; i < 100; i++) {
    vertices.push({ x: i * 0.1, y: 0, z: 0 });
  }
  const sourceStrokes = [makeStroke([{ x: 0.5, y: 0, z: 0 }], 0, makeViewport(), null)];
  const targetRegion = { anchorPoint: { x: 0.5, y: 0, z: 0 }, radius: 0.3 };

  const affected = Core.partialMerge(vertices, sourceStrokes, targetRegion, 'blend');

  // 영역 안 정점만 변경
  assert.ok(affected.length > 0);
  assert.ok(affected.length < 100);

  // 영역 밖 정점은 불변
  for (let i = 0; i < 100; i++) {
    if (!affected.includes(i)) {
      assert.equal(vertices[i].x, i * 0.1);
      assert.equal(vertices[i].y, 0);
      assert.equal(vertices[i].z, 0);
    }
  }
});

// ═══════════════════════════════════════════
// 기준 4: Onion-Skin 각도/뷰포트 동시 적용
// ═══════════════════════════════════════════

test('Onion-Skin: 각도 차이 15° → opacity 0.65, 재투영 좌표 확인', () => {
  const stroke = makeStroke(
    [{ x: 0, y: 0, z: 0 }, { x: 10, y: 10, z: 0 }],
    45,
    makeViewport(50, 0, 1.5),
    null
  );

  const result = Core.renderOnionSkin(stroke, 30, makeViewport(0, 0, 1), 400, 400);
  assert.ok(result !== null, '15° 차이는 30° 미만이므로 렌더링되어야 함');

  // opacity = 1.0 - (15/30) * 0.7 = 0.65
  assert.ok(Math.abs(result.opacity - 0.65) < 0.001, `opacity: ${result.opacity}`);

  // 스크린 좌표가 반환되어야 함
  assert.equal(result.screenPoints.length, 2);
  assert.ok(typeof result.screenPoints[0].x === 'number');
  assert.ok(typeof result.screenPoints[0].y === 'number');
});

test('Onion-Skin: 각도 차이 30° 이상 → null 반환 (숨김)', () => {
  const stroke = makeStroke(
    [{ x: 0, y: 0, z: 0 }],
    45,
    makeViewport(),
    null
  );

  const result = Core.renderOnionSkin(stroke, 0, makeViewport(0, 0, 1), 400, 400);
  assert.equal(result, null, '45° 차이는 30° 이상이므로 null이어야 함');
});

// ═══════════════════════════════════════════
// P0 수정사항
// ═══════════════════════════════════════════

test('P0-1: createStroke stores pressure array', () => {
  const pressure = [0.5, 0.7, 0.9];
  const stroke = makeStroke(
    [{ x: 0, y: 0, z: 0 }, { x: 1, y: 1, z: 0 }],
    0,
    makeViewport(),
    pressure
  );
  assert.ok(Array.isArray(stroke.pressure));
  assert.equal(stroke.pressure.length, 3);
  assert.equal(stroke.pressure[0], 0.5);
  assert.equal(stroke.pressure[1], 0.7);
  assert.equal(stroke.pressure[2], 0.9);
});

test('P0-2: penPointsFromEvent z=0 고정 (pressure 3D 유입 차단)', () => {
  // input.js의 penPointsFromEvent는 별도 테스트 파일에서 검증
  // 여기서는 core 수준에서 z=0 원칙 확인
  const w = Core.screenToWorld(200, 200, 45, makeViewport(0, 0, 1), 400, 400);
  assert.equal(w.z, 0, 'z는 항상 0이어야 함 (2D 스케치 평면)');
});

test('P0-3: 각도 정규화 (normalizeAngle)', () => {
  assert.equal(Core.normalizeAngle(270), -90);
  assert.equal(Core.normalizeAngle(450), 90);
  assert.equal(Core.normalizeAngle(-200), 160);
  assert.equal(Core.normalizeAngle(-450), -90);
  assert.equal(Core.normalizeAngle(0), 0);
  assert.equal(Core.normalizeAngle(45), 45);
  assert.equal(Core.normalizeAngle(-45), -45);
});

// ═══════════════════════════════════════════
// 브러시 렌더
// ═══════════════════════════════════════════

test('브러시 4종 정의 확인 (연필/펜/사인펜/붓)', () => {
  const pencil = Core.getBrush('pencil');
  const pen = Core.getBrush('pen');
  const marker = Core.getBrush('marker');
  const brush = Core.getBrush('brush');

  assert.equal(pencil.baseWidth, 0.8);
  assert.equal(pen.baseWidth, 1.5);
  assert.equal(marker.baseWidth, 3.0);
  assert.equal(brush.baseWidth, 4.5);

  // 압력 반응
  assert.equal(pencil.pressureToWidth, 0.02);
  assert.equal(pen.pressureToWidth, 0.04);
  assert.equal(marker.pressureToWidth, 0.08);
  assert.equal(brush.pressureToWidth, 0.12);
});

// ═══════════════════════════════════════════
// 뷰모드 레지스트리
// ═══════════════════════════════════════════

test('뷰모드 레지스트리: standard/cel/dummy 3종 등록', () => {
  ViewModes.registerDefaultModes();
  const modes = ViewModes.getAllModes();
  assert.equal(modes.length, 3);
  assert.ok(modes.find(m => m.id === 'standard'));
  assert.ok(modes.find(m => m.id === 'cel'));
  assert.ok(modes.find(m => m.id === 'dummy'));
});

test('뷰모드 중복 등록 방지', () => {
  ViewModes.registerDefaultModes();
  const before = ViewModes.getAllModes().length;
  ViewModes.registerViewMode({ id: 'standard', name: 'standard', label: '중복', shader: 'pbr', description: '' });
  const after = ViewModes.getAllModes().length;
  assert.equal(before, after, '중복 등록은 무시되어야 함');
});

test('뷰모드 전환', () => {
  ViewModes.registerDefaultModes();
  ViewModes.setCurrentMode('cel');
  assert.equal(ViewModes.getCurrentMode().id, 'cel');
  ViewModes.setCurrentMode('dummy');
  assert.equal(ViewModes.getCurrentMode().id, 'dummy');
  ViewModes.setCurrentMode('standard');
  assert.equal(ViewModes.getCurrentMode().id, 'standard');
});

// ═══════════════════════════════════════════
// 대칭 프록시
// ═══════════════════════════════════════════

test('대칭 프록시: X 반전 + viewAngle 부호', () => {
  const original = makeStroke(
    [{ x: 10, y: 20, z: 0 }],
    45,
    makeViewport(),
    null
  );
  const sym = Core.createSymmetricProxy(original);
  assert.equal(sym.points3D[0].x, -10);
  assert.equal(sym.points3D[0].y, 20);
  assert.equal(sym.points3D[0].z, 0);
  assert.equal(sym.viewAngle, -45);
});

// ═══════════════════════════════════════════
// 가이드 메쉬
// ═══════════════════════════════════════════

test('가이드: 입체/구/원기둥/원뿔/원환 생성', () => {
  const cube = Guides.createCubeGuide(100);
  assert.equal(cube.vertices.length, 8);
  assert.equal(cube.faces.length, 6);

  const sphere = Guides.createSphereGuide(50, 8);
  assert.ok(sphere.vertices.length > 0);
  assert.ok(sphere.faces.length > 0);

  const cyl = Guides.createCylinderGuide(30, 100, 8);
  assert.ok(cyl.vertices.length > 0);
  assert.ok(cyl.faces.length > 0);

  const cone = Guides.createConeGuide(40, 100, 8);
  assert.ok(cone.vertices.length > 0);
  assert.ok(cone.faces.length > 0);

  const torus = Guides.createTorusGuide(40, 15, 8, 4);
  assert.ok(torus.vertices.length > 0);
  assert.ok(torus.faces.length > 0);
});

test('가이드: 목업인형 6종 (남성/여성/소년/소년남/소년여/아동남/아동여)', () => {
  const male = Guides.createMannequinGuide('male', 'adult');
  const female = Guides.createMannequinGuide('female', 'adult');
  const youth = Guides.createMannequinGuide('male', 'youth');
  const boy = Guides.createMannequinGuide('male', 'boy');
  const girl = Guides.createMannequinGuide('female', 'girl');
  const childM = Guides.createMannequinGuide('male', 'child');
  const childF = Guides.createMannequinGuide('female', 'child');

  for (const [name, mesh] of [['male', male], ['female', female], ['youth', youth], ['boy', boy], ['girl', girl], ['childM', childM], ['childF', childF]]) {
    assert.ok(mesh.vertices.length > 0, `${name}: vertices 있어야 함`);
    assert.ok(mesh.faces.length > 0, `${name}: faces 있어야 함`);
  }
});

// ═══════════════════════════════════════════
// 저장/복원
// ═══════════════════════════════════════════

test('저장/복원: 직렬화/역직렬화 일치', () => {
  const state = Core.createViewState();
  state.strokes = [makeStroke([{ x: 1, y: 2, z: 0 }], 45, makeViewport(10, 20, 2), [0.5])];
  state.currentAngle = 45;
  state.viewport = { centerX: 10, centerY: 20, scale: 2 };

  const json = Core.serializeState(state);
  const restored = Core.deserializeState(json);

  assert.equal(restored.strokes.length, 1);
  assert.equal(restored.currentAngle, 45);
  assert.equal(restored.viewport.centerX, 10);
  assert.equal(restored.viewport.centerY, 20);
  assert.equal(restored.viewport.scale, 2);
  assert.equal(restored.strokes[0].points3D[0].x, 1);
  assert.equal(restored.strokes[0].pressure[0], 0.5);
});

// ═══════════════════════════════════════════
// 좌표 변환 라운드트립
// ═══════════════════════════════════════════

test('좌표 변환 라운드트립: screen → world → screen', () => {
  const origScreen = { x: 250, y: 300 };
  const angle = 45;
  const vp = makeViewport(10, 20, 1.5);

  const world = Core.screenToWorld(origScreen.x, origScreen.y, angle, vp, 400, 400);
  const backToScreen = Core.worldToScreen(world.x, world.y, world.z, angle, vp, 400, 400);

  assert.ok(Math.abs(origScreen.x - backToScreen.x) < 0.001, `x: ${origScreen.x} vs ${backToScreen.x}`);
  assert.ok(Math.abs(origScreen.y - backToScreen.y) < 0.001, `y: ${origScreen.y} vs ${backToScreen.y}`);
});

test('월드→카메라→월드 라운드트립', () => {
  const world = { x: 10, y: 20, z: 5 };
  const angle = 30;

  const cam = Core.worldToCamera(world.x, world.y, world.z, angle);
  const back = Core.cameraToWorld(cam.x, cam.y, cam.z, angle);

  assert.ok(Math.abs(world.x - back.x) < 0.001, `x: ${world.x} vs ${back.x}`);
  assert.ok(Math.abs(world.y - back.y) < 0.001, `y: ${world.y} vs ${back.y}`);
  assert.ok(Math.abs(world.z - back.z) < 0.001, `z: ${world.z} vs ${back.z}`);
});

// ═══════════════════════════════════════════
// ViewState 초기값
// ═══════════════════════════════════════════

test('createViewState 초기값 확인', () => {
  const state = Core.createViewState();
  assert.equal(state.currentAngle, 0);
  assert.equal(state.viewport.centerX, 0);
  assert.equal(state.viewport.centerY, 0);
  assert.equal(state.viewport.scale, 1);
  assert.equal(state.onionSkinEnabled, true);
  assert.equal(state.strokes.length, 0);
});

console.log('M11 테스트 로드 완료');
