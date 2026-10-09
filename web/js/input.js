// input.js — 마우스/태블릿 입력 처리, 스크린→월드 변환, pressure 기록

(function(global) {
  'use strict';

  const Core = global.SketchCore;

  // ── 이벤트 포인트 → 월드 좌표 변환 ──
  // angleDeg: 그리기 각도 (radio에서 선택된 값)
  // z=0 고정: pressure는 3D로 유입하지 않음 (2D 렌더링 전용)
  function penPointsFromEvent(event, angleDeg, viewport, canvasWidth, canvasHeight) {
    const rect = event.target.getBoundingClientRect();
    const screenX = event.clientX - rect.left;
    const screenY = event.clientY - rect.top;
    const world = Core.screenToWorld(screenX, screenY, angleDeg, viewport, canvasWidth, canvasHeight);
    return {
      x: world.x,
      y: world.y,
      z: 0,  // 2D 스케치 평면 고정
      pressure: event.pressure !== undefined ? event.pressure : 0.5
    };
  }

  // ── 입력 컨트롤러 ──
  function createInputController(canvas, state, onStrokeComplete) {
    let isDrawing = false;
    let currentPoints = [];
    let currentPressures = [];

    function getCanvasPos(event) {
      const rect = canvas.getBoundingClientRect();
      return {
        x: event.clientX - rect.left,
        y: event.clientY - rect.top
      };
    }

    function onPointerDown(event) {
      if (event.button !== 0) return; // 왼쪽 버튼만
      isDrawing = true;
      currentPoints = [];
      currentPressures = [];
      const pos = getCanvasPos(event);
      const point = penPointsFromEvent(
        { clientX: event.clientX, clientY: event.clientY, target: canvas, pressure: event.pressure },
        state.currentAngle,
        state.viewport,
        canvas.width,
        canvas.height
      );
      currentPoints.push(point);
      currentPressures.push(point.pressure);
      canvas.setPointerCapture(event.pointerId);
    }

    function onPointerMove(event) {
      if (!isDrawing) return;
      const point = penPointsFromEvent(
        { clientX: event.clientX, clientY: event.clientY, target: canvas, pressure: event.pressure },
        state.currentAngle,
        state.viewport,
        canvas.width,
        canvas.height
      );
      currentPoints.push(point);
      currentPressures.push(point.pressure);
    }

    function onPointerUp(event) {
      if (!isDrawing) return;
      isDrawing = false;
      if (currentPoints.length > 0) {
        const stroke = Core.createStroke(
          currentPoints,
          state.currentAngle,
          state.viewport,
          currentPressures
        );
        onStrokeComplete(stroke);
      }
      currentPoints = [];
      currentPressures = [];
    }

    function onWheel(event) {
      event.preventDefault();
      const delta = event.deltaY > 0 ? 0.9 : 1.1;
      state.viewport.scale *= delta;
      state.viewport.scale = Math.max(0.1, Math.min(10, state.viewport.scale));
    }

    // Pan (가운데 버튼 또는 Space+드래그)
    let isPanning = false;
    let panStart = null;

    function onPointerDownPan(event) {
      if (event.button === 1 || (event.button === 0 && event.shiftKey)) {
        isPanning = true;
        panStart = getCanvasPos(event);
        canvas.setPointerCapture(event.pointerId);
        event.preventDefault();
      }
    }

    function onPointerMovePan(event) {
      if (!isPanning) return;
      const pos = getCanvasPos(event);
      const dx = (pos.x - panStart.x) / state.viewport.scale;
      const dy = -(pos.y - panStart.y) / state.viewport.scale;
      state.viewport.centerX -= dx;
      state.viewport.centerY -= dy;
      panStart = pos;
    }

    function onPointerUpPan(event) {
      isPanning = false;
      panStart = null;
    }

    // 이벤트 바인딩
    canvas.addEventListener('pointerdown', onPointerDown);
    canvas.addEventListener('pointermove', onPointerMove);
    canvas.addEventListener('pointerup', onPointerUp);
    canvas.addEventListener('pointercancel', onPointerUp);
    canvas.addEventListener('wheel', onWheel, { passive: false });
    canvas.addEventListener('pointerdown', onPointerDownPan);
    canvas.addEventListener('pointermove', onPointerMovePan);
    canvas.addEventListener('pointerup', onPointerUpPan);

    return {
      destroy() {
        canvas.removeEventListener('pointerdown', onPointerDown);
        canvas.removeEventListener('pointermove', onPointerMove);
        canvas.removeEventListener('pointerup', onPointerUp);
        canvas.removeEventListener('pointercancel', onPointerUp);
        canvas.removeEventListener('wheel', onWheel);
        canvas.removeEventListener('pointerdown', onPointerDownPan);
        canvas.removeEventListener('pointermove', onPointerMovePan);
        canvas.removeEventListener('pointerup', onPointerUpPan);
      }
    };
  }

  // ── 내보내기 ──
  global.SketchInput = {
    penPointsFromEvent,
    createInputController
  };

})(typeof window !== 'undefined' ? window : globalThis);
