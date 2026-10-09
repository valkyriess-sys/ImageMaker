// app.js — 메인 애플리케이션 (모듈 연결, UI 이벤트, 상태 관리)

(function(global) {
  'use strict';

  const Core = global.SketchCore;
  const Render = global.SketchRender;
  const Input = global.SketchInput;
  const Guides = global.SketchGuides;
  const ViewModes = global.SketchViewModes;

  // ── DOM 요소 ──
  const canvas = document.getElementById('canvas');
  const ctx = canvas.getContext('2d');
  const brushSelect = document.getElementById('brush-select');
  const guideSelect = document.getElementById('guide-select');
  const viewModesContainer = document.getElementById('view-modes');
  const viewModeLabel = document.getElementById('view-mode-label');
  const statusText = document.getElementById('status-text');
  const btnSave = document.getElementById('btn-save');
  const btnLoad = document.getElementById('btn-load');
  const btnClear = document.getElementById('btn-clear');

  // ── 상태 ──
  const state = Core.createViewState();
  let currentBrush = Core.getBrush('pen');
  let currentGuide = null;
  let inputController = null;

  // ── 각도 radio 동기화 ──
  // 단일 진실원천: state.currentAngle
  // - eventPoint → state.currentAngle 사용 (그리기 투영)
  // - selectedAngle() → radio값 읽기 (라벨 표시용)
  // - radio change → state.currentAngle 동기화
  // - 초기화 → selectedAngle()에서 읽어 45° radio와 일치
  function selectedAngle() {
    const checked = document.querySelector('input[name="angle"]:checked');
    return checked ? Number(checked.value) : 45;
  }

  // 초기값 하드코딩 제거: radio에서 읽어서 동기화
  state.currentAngle = selectedAngle();

  // radio change 리스너: state.currentAngle 동기화
  document.querySelectorAll('input[name="angle"]').forEach(radio => {
    radio.addEventListener('change', event => {
      state.currentAngle = Number(event.target.value);
      draw();
    });
  });

  // ── 캔버스 크기 조정 ──
  function resizeCanvas() {
    const container = document.getElementById('canvas-container');
    canvas.width = container.clientWidth;
    canvas.height = container.clientHeight;
    draw();
  }

  // ── 렌더 ──
  function draw() {
    Render.render(ctx, state, currentGuide, currentBrush, ViewModes.getCurrentMode().id, canvas.width, canvas.height);
  }

  // ── 스트로크 완료 ──
  function onStrokeComplete(stroke) {
    state.strokes.push(stroke);
    // 45도 스케치 → 대칭 프록시 자동 생성
    if (stroke.viewAngle === 45 || stroke.viewAngle === -45) {
      const sym = Core.createSymmetricProxy(stroke);
      state.strokes.push(sym);
    }
    draw();
    updateStatus();
  }

  // ── 상태 표시 ──
  function updateStatus() {
    const count = state.strokes.length;
    const angle = state.currentAngle;
    const scale = state.viewport.scale.toFixed(2);
    statusText.textContent = `스트로크: ${count}개 | 각도: ${angle}° | 줌: ${scale}x`;
  }

  // ── 브러시 변경 ──
  brushSelect.addEventListener('change', () => {
    currentBrush = Core.getBrush(brushSelect.value);
    draw();
  });

  // ── 가이드 변경 ──
  guideSelect.addEventListener('change', () => {
    currentGuide = Guides.getGuide(guideSelect.value);
    draw();
  });

  // ── 뷰모드 토글 UI ──
  ViewModes.registerDefaultModes();
  ViewModes.createToggleUI(viewModesContainer, viewModeLabel, () => {
    draw();
  });

  // ── 저장/불러오기/지우기 ──
  btnSave.addEventListener('click', () => {
    const json = Core.serializeState(state);
    localStorage.setItem('sketch3d_state', json);
    statusText.textContent = '저장 완료';
  });

  btnLoad.addEventListener('click', () => {
    const json = localStorage.getItem('sketch3d_state');
    if (json) {
      const loaded = Core.deserializeState(json);
      state.strokes = loaded.strokes;
      state.currentAngle = loaded.currentAngle;
      state.viewport = loaded.viewport;
      state.onionSkinEnabled = loaded.onionSkinEnabled;
      // radio 동기화
      const radio = document.querySelector(`input[name="angle"][value="${state.currentAngle}"]`);
      if (radio) radio.checked = true;
      draw();
      updateStatus();
      statusText.textContent = '불러오기 완료';
    } else {
      statusText.textContent = '저장된 데이터 없음';
    }
  });

  btnClear.addEventListener('click', () => {
    state.strokes = [];
    draw();
    updateStatus();
  });

  // ── 입력 컨트롤러 ──
  inputController = Input.createInputController(canvas, state, onStrokeComplete);

  // ── 초기화 ──
  window.addEventListener('resize', resizeCanvas);
  resizeCanvas();
  updateStatus();

  // ── 디버그용 전역 노출 ──
  global._sketchApp = { state, Core, Render, Input, Guides, ViewModes };

})(typeof window !== 'undefined' ? window : globalThis);
